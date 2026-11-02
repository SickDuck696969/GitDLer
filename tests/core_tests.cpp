#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "model.hpp"
#include "note_format.hpp"
#ifdef _WIN32
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#endif
#include "file_ops.hpp"
using namespace gitdler;

std::string read(const std::string& p) {
    std::ifstream in(p); return std::string(std::istreambuf_iterator<char>(in),{});
}
void write(const std::string& p, const std::string& s) { std::ofstream(p) << s; }
int main() {
    assert(normalizeRepo("owner/repo") == "https://github.com/owner/repo");
    assert(normalizeRepo("https://github.com/owner/repo.git/") == "https://github.com/owner/repo");
    assert(normalizeRepo("https://github.com/owner/repo/releases/latest?tab=readme") == "https://github.com/owner/repo");
    assert(normalizeRepo("https://evil.example/owner/repo").empty());
    assert(normalizeRepo("owner/").empty());
    assert(normalizeRepo("owner/..").empty());
    assert(!httpsUrl("file:///etc/passwd"));
    assert(!httpsUrl(std::string("https://host/a\0b",16)));
    assert(!safeName("../boot.nro") && !safeName("a\\b") && !safeName("a:b"));
    assert(!safeName("..") && !safeName("bad.") && !safeName("bad "));
    assert(safeName("my app.nro"));
    assert(fileName("https://host/file.zip?token=123#part") == "file.zip");
    assert(fileName("https://host/") == "download.bin");
    Repo r;
    const std::string release = R"({
        "tag_name": "v2.0", "body": "A string with a fake browser_download_url",
        "assets": [
            {"name":"app.nro", "browser_download_url":"https://host/app.nro", "size":4096},
            {"name":"bundle.zip", "browser_download_url":"https://host/bundle.zip", "size":8589934592},
            {"name":"notes\u00e9.txt", "browser_download_url":"https://host/notes", "size":0},
            {"name":"APP.NRO", "browser_download_url":"https://host/duplicate"},
            {"name":"../bad", "browser_download_url":"https://host/bad"},
            {"name":"bad.bin", "browser_download_url":"file:///bad"},
            {"name":42, "browser_download_url":"https://host/bad"}, null
        ]})";
    assert(parseRelease(release,r));
    assert(r.version == "v2.0" && r.assets.size() == 3);
    assert(r.assets[1].size == 8589934592ULL && !r.assets[0].selected);
    assert(r.assets[2].name == "notes\xc3\xa9.txt");
    assert(!parseRelease("<html>error</html>",r));
    assert(!parseRelease("{\"tag_name\":42,\"assets\":[]}",r));
    assert(!parseRelease("[]",r));
    assert(parseRelease("{\"tag_name\":\"v1\",\"assets\":[]}",r) && !r.error.empty());
    assert(parseRelease(R"({"tag_name":"v1","assets":[{"name":"only.nro","browser_download_url":"https://host/only","size":-1}]})",r));
    assert(r.assets.size() == 1 && r.assets[0].selected && r.assets[0].size == 0 && r.error.empty());
    assert(bytes(1024) == "1.0 KB");

    std::vector<Release> page;
    std::string parseError;
    const std::string list = R"([
        {"id":30,"tag_name":"v3-beta","name":"Preview","prerelease":true,"body":"# Changes\n- New UI\n\nDetailed notes","published_at":"2026-10-10T01:02:03Z","author":{"login":"maintainer"},"assets":[]},
        {"id":20,"tag_name":"v2","prerelease":false,"name":null,"body":null,"assets":[{"name":"app.nro","browser_download_url":"https://host/app.nro","size":20}]},
        {"id":10,"tag_name":"v1","assets":[]},
        {"id":5,"tag_name":"draft","draft":true,"assets":[]},
        {"id":10,"tag_name":"duplicate","assets":[]},
        {"id":"bad","tag_name":"invalid","assets":[]}
    ])";
    assert(parseReleasePage(list,page,parseError) && parseError.empty() && page.size() == 3);
    assert(page[0].prerelease && page[0].notes.find("Detailed notes") != std::string::npos);
    assert(page[0].author == "maintainer" && page[0].published == "2026-10-10T01:02:03Z");
    assert(page[1].title == "v2" && page[1].notes.empty() && page[1].assets[0].selected);
    uint64_t latestId = 0; std::string latestTag;
    assert(parseLatest(R"({"id":20,"tag_name":"v2"})",latestId,latestTag));
    assert(latestId == page[1].id && latestId != page[0].id); // newest prerelease is not the designated latest
    assert(!parseLatest(R"({"message":"Not Found"})",latestId,latestTag));
    assert(!parseReleasePage(R"({"message":"rate limited"})",page,parseError) && page.size() == 3);
    assert(hasNextLink("Link: <https://api.github.com/repos/a/b/releases?page=2>; rel=\"next\", <x>; rel=\"last\""));
    assert(!hasNextLink("Link: <x>; rel=\"prev\""));
    assert(!hasNextLink("X-Other: rel=\"next\""));
    assert(parseReleasePage("[]",page,parseError) && page.empty());

    auto measure = [](const std::string& s) { int count = 0; for (unsigned char c : s) count += (c & 0xc0) != 0x80; return count; };
    auto notes = wrapNotes("# Heading\r\n\n- A long bullet with words\n```cpp\ncode();\n```",12,measure);
    assert(notes.front().text == "Heading" && notes.front().kind == NoteKind::Heading);
    assert(notes[1].text.empty());
    assert(std::any_of(notes.begin(),notes.end(),[](const NoteLine& l) { return l.text == "code();" && l.kind == NoteKind::Code; }));
    std::string unicode = "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9";
    auto wrapped = wrapNotes(unicode,2,measure);
    assert(wrapped.size() == 3 && wrapped[0].text.size() == 4);
    std::string rebuilt; for (const auto& l : wrapped) rebuilt += l.text;
    assert(rebuilt == unicode);
    auto link = wrapNotes("https://example.com/averylongunbrokenpath",8,measure);
    rebuilt.clear(); for (const auto& l : link) { assert(measure(l.text) <= 8); rebuilt += l.text; }
    assert(rebuilt == "https://example.com/averylongunbrokenpath");
    assert(!wrapNotes("",20,measure).empty());

    // Run inside an isolated temporary directory. Check replacement and rollback,
    // including Windows/Horizon's refusal to rename over an existing target.
    const auto root = std::filesystem::temp_directory_path() / ("gitdler-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    const auto staged = (root/"staged").string(), target = (root/"target").string();
    std::string error;
    write(staged,"new"); assert(replaceFile(staged,target,error)); assert(read(target) == "new");
    write(staged,"replacement"); assert(replaceFile(staged,target,error));
    assert(read(target) == "replacement" && !pathExists(target+".gitdler-backup"));
    assert(!replaceFile(staged,target,error)); // missing staged file must roll back
    assert(read(target) == "replacement" && !pathExists(target+".gitdler-backup"));
    write(staged,"newer"); write(target+".gitdler-backup","precious backup");
    assert(!replaceFile(staged,target,error));
    assert(read(target) == "replacement" && read(target+".gitdler-backup") == "precious backup");
    const auto folder = (root/"folder").string(); std::filesystem::create_directory(folder);
    assert(!replaceFile(staged,folder,error) && pathExists(staged));
    std::filesystem::remove_all(root);
    std::cout << "PASS: release pages/latest/pagination, UTF-8 notes, URL/name validation, large assets, selection defaults, replacement and rollback\n";
}
