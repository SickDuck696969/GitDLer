#include "archive_extract.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#undef assert
#define assert(test) do { if (!(test)) { std::cerr << "FAIL: " << #test << " at line " << __LINE__ << '\n'; std::_Exit(1); } } while (0)
namespace fs = std::filesystem;
using namespace gitdler;
std::string contents(const fs::path& path) { std::ifstream in(path,std::ios::binary); return std::string(std::istreambuf_iterator<char>(in),{}); }
ExtractProgress run(const fs::path& archive,const fs::path& destination,bool replace = true) {
    ArchiveExtractor extractor(archive.generic_string(),destination.generic_string(),replace);
    for (int i = 0; i < 100000 && !extractor.finished(); ++i) extractor.step();
    assert(extractor.finished());
    return extractor.progress();
}
int main(int argc,char** argv) {
    assert(argc == 2);
    auto fixtures = fs::absolute(argv[1]);
    auto root = fs::temp_directory_path()/ ("gitdler-extraction-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    std::string relative;
    assert(normalizeArchivePath("./switch/app/file.nro",relative) && relative == "switch/app/file.nro");
    assert(normalizeArchivePath("folder\\file.txt",relative) && relative == "folder/file.txt");
    for (auto name : {"../escape","/absolute","C:/drive","\\\\host\\share","a/../../b","a/..\\b","file:stream"}) assert(!normalizeArchivePath(name,relative));
    assert(archiveName("App.TAR.GZ") && archiveName("app.7z") && archiveName("app.rar") && !archiveName("app.nro"));
    assert(detectArchive((fixtures/"magic.dat").generic_string()));
    for (auto name : {"stored.zip","deflated.zip","test.tar","test.tar.gz","test.tar.bz2","test.tar.xz","magic.dat"}) {
        auto destination = root/name; fs::create_directory(destination);
        auto status = run(fixtures/name,destination);
        if (status.phase != ExtractPhase::Done) std::cerr << name << ": " << status.error << '\n';
        assert(status.phase == ExtractPhase::Done);
        assert(contents(destination/"folder/hello.txt") == "hello archive");
        assert(status.writtenBytes == 13 && status.processedBytes == status.totalBytes);
    }
    for (auto name : {"single.txt.gz","single.txt.bz2","single.txt.xz"}) {
        auto destination = root/name; fs::create_directory(destination);
        auto status = run(fixtures/name,destination);
        if (status.phase != ExtractPhase::Done) std::cerr << name << ": " << status.error << '\n';
        assert(status.phase == ExtractPhase::Done && contents(destination/"single.txt") == "hello archive");
    }
    for (auto name : {"test.7z","test.tar.zst","test.tar.lz4"}) if (fs::exists(fixtures/name)) {
        auto destination = root/name; fs::create_directory(destination);
        auto status = run(fixtures/name,destination);
        if (status.phase != ExtractPhase::Done) std::cerr << name << ": " << status.error << '\n';
        assert(status.phase == ExtractPhase::Done && contents(destination/"folder/hello.txt") == "hello archive");
        std::cout << "PASS: " << name << '\n';
    }
    auto compressed = root/"compressed"; fs::create_directory(compressed);
    assert(run(fixtures/"compressed.dat",compressed).phase == ExtractPhase::Done);
    assert(contents(compressed/"compressed.dat.unpacked") == "hello archive");
    for (auto name : {"traversal.zip","absolute.zip","drive.zip","backslash.zip","duplicate.zip","symlink.tar","hardlink.tar","broken.zip"}) {
        auto destination = root/name; fs::create_directory(destination);
        auto status = run(fixtures/name,destination);
        assert(status.phase == ExtractPhase::Failed && !status.error.empty());
        assert(fs::is_empty(destination));
    }
    auto replaceRoot = root/"replace"; fs::create_directories(replaceRoot/"folder");
    std::ofstream(replaceRoot/"folder/hello.txt") << "original";
    auto skipped = run(fixtures/"deflated.zip",replaceRoot,false);
    assert(skipped.phase == ExtractPhase::Done && skipped.skipped == 1 && contents(replaceRoot/"folder/hello.txt") == "original");
    auto replaced = run(fixtures/"deflated.zip",replaceRoot,true);
    assert(replaced.phase == ExtractPhase::Done && contents(replaceRoot/"folder/hello.txt") == "hello archive");
    std::ofstream(replaceRoot/"folder/hello.txt") << "original";
    auto badcrc = run(fixtures/"badcrc.zip",replaceRoot);
    if (badcrc.phase != ExtractPhase::Failed) std::cerr << "CRC result: " << static_cast<int>(badcrc.phase) << " / " << badcrc.error << '\n';
    assert(badcrc.phase == ExtractPhase::Failed && contents(replaceRoot/"folder/hello.txt") == "original");
    auto cancelRoot = root/"cancel"; fs::create_directory(cancelRoot);
    std::ofstream(cancelRoot/"big.bin") << "original";
    ArchiveExtractor cancelled((fixtures/"cancel.zip").generic_string(),cancelRoot.generic_string(),true);
    for (int i = 0; i < 100 && cancelled.progress().writtenBytes == 0 && !cancelled.finished(); ++i) cancelled.step();
    assert(cancelled.progress().writtenBytes > 0 && !cancelled.finished());
    cancelled.cancel(); assert(cancelled.progress().phase == ExtractPhase::Cancelled);
    assert(contents(cancelRoot/"big.bin") == "original");
    assert(std::distance(fs::directory_iterator(cancelRoot),fs::directory_iterator{}) == 1);
    auto selfRoot = root/"self"; fs::create_directory(selfRoot); fs::copy_file(fixtures/"self.zip",selfRoot/"self.zip");
    auto original = contents(selfRoot/"self.zip");
    auto self = run(selfRoot/"self.zip",selfRoot);
    assert(self.phase == ExtractPhase::Failed && contents(selfRoot/"self.zip") == original);
    // A parent that is a regular file must not be traversed or overwritten.
    auto blocked = root/"blocked"; fs::create_directory(blocked); std::ofstream(blocked/"folder") << "original";
    assert(run(fixtures/"deflated.zip",blocked).phase == ExtractPhase::Failed && contents(blocked/"folder") == "original");
    fs::remove_all(root);
    std::cout << "PASS: ZIP/TAR/compressed streams, signature detection, unsafe paths/links, duplicates, CRC failure, replace/skip, cancellation, source preservation\n";
}
