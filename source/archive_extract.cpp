#include "archive_extract.hpp"
#include "model.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <set>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>
#ifndef S_ISREG
#define S_ISREG(m) (((m)&_S_IFMT)==_S_IFREG)
#define S_ISDIR(m) (((m)&_S_IFMT)==_S_IFDIR)
#endif
#else
#include <sys/statvfs.h>
#include <unistd.h>
#endif
#include "file_ops.hpp"

namespace gitdler {
namespace {
const char* suffixes[] = {".tar.gz",".tar.bz2",".tar.xz",".tar.zst",".tar.lz4",".zip",".7z",".rar",".tar",".tgz",".tbz2",".tbz",".txz",".tzst",".gz",".bz2",".xz",".zst",".lz4"};
bool ends(const std::string& value,const std::string& suffix) {
    return value.size() >= suffix.size() && value.compare(value.size()-suffix.size(),suffix.size(),suffix) == 0;
}
bool singleStream(const std::string& source) {
    const auto name = lowerAscii(source);
    for (const char* ext : {".gz",".bz2",".xz",".zst",".lz4"})
        if (ends(name,ext) && !ends(name,std::string(".tar")+ext)) return true;
    return false;
}
bool linked(const std::string& name) {
#ifdef _WIN32
    auto flags = GetFileAttributesA(name.c_str());
    return flags != INVALID_FILE_ATTRIBUTES && (flags & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    struct stat st{}; return lstat(name.c_str(),&st) == 0 && S_ISLNK(st.st_mode);
#endif
}
bool ensureDirectory(const std::string& name, bool create) {
    if (linked(name)) return false;
    struct stat st{};
    if (stat(name.c_str(),&st) == 0) return S_ISDIR(st.st_mode);
    if (!create || errno != ENOENT) return false;
#ifdef _WIN32
    return _mkdir(name.c_str()) == 0;
#else
    return mkdir(name.c_str(),0777) == 0;
#endif
}
bool parentsSafe(const std::string& path, bool create) {
    // Check each ancestor, including those in the chosen destination, so a
    // pre-existing symlink cannot redirect writes outside that destination.
    for (size_t i = 1; i < path.size(); ++i) if (path[i] == '/') {
        if (path[i-1] == ':') continue;
        const auto part = path.substr(0,i);
        if (!ensureDirectory(part,create)) return false;
    }
    return true;
}
double availableBytes(const std::string& root) {
#ifdef _WIN32
    ULARGE_INTEGER available;
    return GetDiskFreeSpaceExA(root.c_str(),&available,nullptr,nullptr) ? static_cast<double>(available.QuadPart) : -1;
#else
    struct statvfs info{};
    return statvfs(root.c_str(),&info) == 0 ? static_cast<double>(info.f_bavail)*info.f_frsize : -1;
#endif
}
std::string normalizedSource(std::string value) {
    std::replace(value.begin(),value.end(),'\\','/');
    std::vector<std::string> parts;
    size_t begin = 0;
    while (begin < value.size()) {
        auto end = value.find('/',begin); if (end == std::string::npos) end = value.size();
        auto part = value.substr(begin,end-begin);
        if (part == "..") { if (!parts.empty() && parts.back().find(':') == std::string::npos) parts.pop_back(); }
        else if (!part.empty() && part != ".") parts.push_back(part);
        begin = end+1;
    }
    std::string result = !value.empty() && value.front() == '/' ? "/" : "";
    for (const auto& part : parts) { if (!result.empty() && result.back() != '/') result += '/'; result += part; }
    return lowerAscii(result);
}
}
bool archiveName(const std::string& name) {
    const auto lower = lowerAscii(name);
    for (const auto* suffix : suffixes) if (ends(lower,suffix)) return true;
    return false;
}
bool detectArchive(const std::string& path) {
    if (archiveName(path)) return true;
    FILE* file = fopen(path.c_str(),"rb"); if (!file) return false;
    unsigned char header[512]{}; size_t size = fread(header,1,sizeof(header),file); fclose(file);
    if (size < 6) return false;
    return !memcmp(header,"PK\003\004",4) || !memcmp(header,"PK\005\006",4) ||
        !memcmp(header,"7z\xbc\xaf\x27\x1c",6) || !memcmp(header,"Rar!",4) ||
        (header[0] == 0x1f && header[1] == 0x8b) || !memcmp(header,"BZh",3) ||
        !memcmp(header,"\xfd" "7zXZ\0",6) || !memcmp(header,"\x28\xb5\x2f\xfd",4) ||
        !memcmp(header,"\x04\x22\x4d\x18",4) || (size > 262 && !memcmp(header+257,"ustar",5));
}
bool normalizeArchivePath(const std::string& input, std::string& relative) {
    relative.clear();
    if (input.empty() || input.size() > 1000) return false;
    std::string name = input; std::replace(name.begin(),name.end(),'\\','/');
    if (name.front() == '/') return false;
    size_t begin = 0;
    while (begin < name.size()) {
        size_t end = name.find('/',begin); if (end == std::string::npos) end = name.size();
        const auto part = name.substr(begin,end-begin);
        if (part != "." && !part.empty()) {
            if (!safeName(part)) return false;
            if (!relative.empty()) relative += '/';
            relative += part;
        }
        begin = end+1;
    }
    return true;
}

struct ArchiveExtractor::Impl {
    std::string source, root, target, temporary;
    bool replace, raw = false;
    archive* reader = nullptr;
    FILE* output = nullptr;
    ExtractProgress status;
    std::set<std::string> names;
    std::array<char,64*1024> buffer{};
    uint64_t needed = 0, inspected = 0;
    unsigned sequence = 0;

    Impl(const std::string& src,const std::string& dst,bool overwrite) : source(src),root(dst),replace(overwrite) {
        std::replace(root.begin(),root.end(),'\\','/');
        if (root.empty()) { fail("Choose an extraction directory."); return; }
        if (root.back() != '/') root += '/';
        if (!parentsSafe(root,false) || !ensureDirectory(root,false)) { fail("Extraction destination is not a safe directory."); return; }
        open();
    }
    ~Impl() { cleanup(); }
    void cleanup() {
        if (output) { fclose(output); output = nullptr; }
        if (!temporary.empty()) { remove(temporary.c_str()); temporary.clear(); }
        if (reader) { archive_read_free(reader); reader = nullptr; }
    }
    void fail(const std::string& message) { status.error = message; status.phase = ExtractPhase::Failed; cleanup(); }
    std::string libraryError() const {
        const char* message = reader ? archive_error_string(reader) : nullptr;
        return message ? message : "Unsupported or damaged archive.";
    }
    void open() {
        reader = archive_read_new();
        if (!reader) { fail("Cannot allocate archive reader."); return; }
        archive_read_support_format_zip(reader); archive_read_support_format_tar(reader);
        archive_read_support_format_7zip(reader); archive_read_support_format_rar(reader); archive_read_support_format_rar5(reader);
        archive_read_support_filter_gzip(reader); archive_read_support_filter_bzip2(reader);
        archive_read_support_filter_xz(reader); archive_read_support_filter_zstd(reader); archive_read_support_filter_lz4(reader);
        // Raw fallback is limited to single compressed streams; a broken ZIP must
        // never be mistaken for a regular file and reported as extracted.
        if (singleStream(source) || !archiveName(source)) archive_read_support_format_raw(reader);
        if (archive_read_open_filename(reader,source.c_str(),64*1024) != ARCHIVE_OK) fail(libraryError());
    }
    bool validate(archive_entry* entry,std::string& relative,bool& dir) {
        if (archive_entry_is_encrypted(entry) > 0) { fail("Encrypted archives are not supported."); return false; }
        const char* name = archive_entry_pathname_utf8(entry);
        if (!name) name = archive_entry_pathname(entry);
        raw = archive_format(reader) == ARCHIVE_FORMAT_RAW;
        std::string member = name ? name : "";
        if (raw) {
            if (archive_filter_code(reader,0) == ARCHIVE_FILTER_NONE) { fail("File is not a compressed stream."); return false; }
            member = fileName(source);
            if (singleStream(source)) { auto dot = member.find_last_of('.'); if (dot != std::string::npos) member.resize(dot); }
            else member += ".unpacked";
            if (member.empty()) member = "extracted-file";
        }
        dir = archive_entry_filetype(entry) == AE_IFDIR;
        if (archive_entry_symlink(entry) || archive_entry_hardlink(entry) || (!dir && archive_entry_filetype(entry) != AE_IFREG)) {
            fail("Archive contains a link or unsupported special file."); return false;
        }
        if (!normalizeArchivePath(member,relative) || (!dir && relative.empty())) { fail("Archive contains an unsafe path."); return false; }
        if (root.size()+relative.size() > 1000) { fail("Extraction path is too long."); return false; }
        if (normalizedSource(root+relative) == normalizedSource(source)) { fail("An archive entry would replace the downloaded archive."); return false; }
        if (linked(root+relative)) { fail("Extraction target is a symbolic link."); return false; }
        return true;
    }
    bool stageFile() {
        if (!parentsSafe(target,true)) { fail("Cannot create extraction folders, or an ancestor is a link."); return false; }
        for (int attempts = 0; attempts < 100; ++attempts) {
            auto name = root+".gitdler-extract-"+std::to_string(++sequence)+".part";
#ifdef _WIN32
            int fd = _open(name.c_str(),_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY,_S_IREAD|_S_IWRITE);
#else
            int fd = ::open(name.c_str(),O_WRONLY|O_CREAT|O_EXCL,0666);
#endif
            if (fd < 0) { if (errno == EEXIST) continue; fail("Cannot create extraction file. Check SD space."); return false; }
            temporary = name;
#ifdef _WIN32
            output = _fdopen(fd,"wb"); if (!output) _close(fd);
#else
            output = fdopen(fd,"wb"); if (!output) close(fd);
#endif
            if (!output) { fail("Cannot open extraction file."); return false; }
            return true;
        }
        fail("Too many leftover extraction staging files."); return false;
    }
    void step() {
        if (!reader) return;
        if (output) {
            auto got = archive_read_data(reader,buffer.data(),buffer.size());
            if (got < 0) { fail(libraryError()); return; }
            if (got > 0) {
                if (fwrite(buffer.data(),1,got,output) != static_cast<size_t>(got)) { fail("Cannot write extracted file. Check SD space."); return; }
                status.currentBytes += got; status.writtenBytes += got; status.processedBytes += got; return;
            }
            bool ok = fclose(output) == 0; output = nullptr;
            if (!ok) { fail("Cannot finish extracted file. Check SD space."); return; }
            if (status.currentTotal && status.currentBytes != status.currentTotal) { fail("Extracted size does not match the archive header."); return; }
            std::string error;
            if (!replaceFile(temporary,target,error)) { fail(error); return; }
            temporary.clear(); ++status.saved;
            if (!error.empty()) status.error = error;
            return;
        }
        archive_entry* entry = nullptr;
        int result = archive_read_next_header(reader,&entry);
        if (result == ARCHIVE_EOF) {
            if (status.phase == ExtractPhase::Inspecting) {
                double available = availableBytes(root);
                if (available >= 0 && static_cast<double>(needed) > available) { fail("Not enough free space to extract this archive."); return; }
                archive_read_free(reader); reader = nullptr; names.clear();
                status.phase = ExtractPhase::Extracting; status.member.clear(); open();
            } else { status.phase = ExtractPhase::Done; cleanup(); }
            return;
        }
        if (result != ARCHIVE_OK && result != ARCHIVE_WARN) { fail(libraryError()); return; }
        if (result == ARCHIVE_WARN) status.error = libraryError();
        std::string relative; bool dir = false;
        if (!validate(entry,relative,dir)) return;
        status.member = relative; target = root+relative;
        if (status.phase == ExtractPhase::Inspecting) {
            if (++inspected > 20000) { fail("Archive exceeds the 20,000-entry limit."); return; }
            if (!dir) {
                if (!names.insert(lowerAscii(relative)).second) { fail("Archive has duplicate file paths on the SD card."); return; }
                ++status.files;
                if (!archive_entry_size_is_set(entry) || archive_entry_size(entry) < 0) status.sizeKnown = false;
                else {
                    auto size = static_cast<uint64_t>(archive_entry_size(entry));
                    if (size > std::numeric_limits<uint64_t>::max()-status.totalBytes) { fail("Archive size is too large."); return; }
                    status.totalBytes += size;
                    if (replace || !pathExists(target)) needed += size;
                }
            }
            if (archive_read_data_skip(reader) < ARCHIVE_OK) fail(libraryError());
            return;
        }
        if (dir) {
            if (!relative.empty() && (!parentsSafe(target+"/",true) || !ensureDirectory(target,true))) fail("Cannot create extracted directory.");
            return;
        }
        status.currentBytes = 0;
        status.currentTotal = archive_entry_size_is_set(entry) && archive_entry_size(entry) >= 0 ? archive_entry_size(entry) : 0;
        if (!replace && pathExists(target)) {
            if (!parentsSafe(target,false)) { fail("An extraction ancestor is not a safe directory."); return; }
            if (archive_read_data_skip(reader) < ARCHIVE_OK) { fail(libraryError()); return; }
            ++status.skipped; status.processedBytes += status.currentTotal; return;
        }
        stageFile();
    }
};
ArchiveCleanup finishArchive(const std::string& path, const ExtractProgress& progress,
                             bool deleteOriginal, bool cancelled) {
    if (!deleteOriginal || cancelled || progress.phase != ExtractPhase::Done ||
        !progress.error.empty() || !progress.saved || progress.skipped || progress.saved != progress.files)
        return ArchiveCleanup::Kept;
    return std::remove(path.c_str()) == 0 ? ArchiveCleanup::Deleted : ArchiveCleanup::Failed;
}
ArchiveExtractor::ArchiveExtractor(const std::string& source,const std::string& destination,bool replace)
    : impl(new Impl(source,destination,replace)) {}
ArchiveExtractor::~ArchiveExtractor() = default;
void ArchiveExtractor::step() { if (!finished()) impl->step(); }
void ArchiveExtractor::cancel() { if (!finished()) { impl->status.phase = ExtractPhase::Cancelled; impl->cleanup(); } }
bool ArchiveExtractor::finished() const {
    auto phase = impl->status.phase;
    return phase == ExtractPhase::Done || phase == ExtractPhase::Failed || phase == ExtractPhase::Cancelled;
}
const ExtractProgress& ArchiveExtractor::progress() const { return impl->status; }
}
