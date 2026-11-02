#include "model.hpp"
#include "json_utils.hpp"
#include <algorithm>
#include <cstdio>
#include <cctype>
namespace gitdler {
bool safeName(const std::string& s) {
    return !s.empty() && s != "." && s != ".." && s.size() <= 240 &&
        s.find_first_of("/\\:*?\"<>|") == std::string::npos &&
        s.back() != '.' && s.back() != ' ' &&
        std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
bool httpsUrl(const std::string& s) {
    return s.compare(0, 8, "https://") == 0 && s.size() > 8 &&
        s.find_first_of(" |") == std::string::npos &&
        std::none_of(s.begin(),s.end(),[](unsigned char c) { return c < 32 || c == 127; });
}
std::string lowerAscii(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}
std::string normalizeRepo(std::string s) {
    const std::string prefix = "https://github.com/";
    if (s.compare(0, prefix.size(), prefix) == 0) s.erase(0, prefix.size());
    auto end = s.find_first_of("?#");
    if (end != std::string::npos) s.resize(end);
    while (!s.empty() && s.back() == '/') s.pop_back();
    auto slash = s.find('/');
    if (slash == std::string::npos) return "";
    auto next = s.find('/', slash + 1);
    if (next != std::string::npos) s.resize(next);
    if (s.size() > 4 && s.substr(s.size() - 4) == ".git") s.resize(s.size() - 4);
    if (slash == 0 || slash + 1 >= s.size()) return "";
    for (unsigned char c : s) if (!std::isalnum(c) && c != '/' && c != '-' && c != '_' && c != '.') return "";
    if (s.substr(slash + 1) == "." || s.substr(slash + 1) == "..") return "";
    return prefix + s;
}
std::string fileName(std::string url) {
    auto end = url.find_first_of("?#");
    if (end != std::string::npos) url.resize(end);
    auto slash = url.find_last_of('/');
    std::string name = slash == std::string::npos ? url : url.substr(slash + 1);
    return safeName(name) ? name : "download.bin";
}
bool parseRelease(const std::string& body, Repo& repo) {
    auto j = nlohmann::json::parse(body, nullptr, false);
    if (!j.is_object()) { repo.error = "Invalid release response. Press R to retry."; return false; }
    auto version = stringField(j, "tag_name");
    auto list = j.find("assets");
    if (version.empty() || list == j.end() || !list->is_array()) {
        repo.error = "No published release found. Press R to retry."; return false;
    }
    std::vector<Asset> assets;
    for (const auto& item : *list) {
        if (!item.is_object()) continue;
        Asset a;
        a.name = stringField(item, "name"); a.url = stringField(item, "browser_download_url");
        if (!safeName(a.name) || !httpsUrl(a.url)) continue;
        // SD card filenames are case insensitive: avoid overwriting another queued asset.
        if (std::any_of(assets.begin(), assets.end(), [&](const Asset& old) { return lowerAscii(old.name) == lowerAscii(a.name); })) continue;
        auto size = item.find("size");
        if (size != item.end() && size->is_number_unsigned()) a.size = size->get<uint64_t>();
        assets.push_back(a);
    }
    if (assets.size() == 1) assets[0].selected = true;
    repo.version = version; repo.assets = std::move(assets);
    repo.error = repo.assets.empty() ? "This release has no downloadable files." : "";
    return true;
}
std::string bytes(double n) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    while (n >= 1024 && unit < 4) { n /= 1024; ++unit; }
    char out[64];
    snprintf(out, sizeof(out), unit ? "%.1f %s" : "%.0f %s", n, units[unit]);
    return out;
}

bool parseReleasePage(const std::string& body, std::vector<Release>& releases, std::string& error) {
    auto list = nlohmann::json::parse(body,nullptr,false);
    if (!list.is_array()) { error = "Invalid release list. Press + to retry."; return false; }
    std::vector<Release> parsed;
    for (const auto& item : list) {
        if (!item.is_object()) continue;
        auto id = item.find("id"), draft = item.find("draft");
        if (id == item.end() || !id->is_number_unsigned() || id->get<uint64_t>() == 0) continue;
        if (draft != item.end() && draft->is_boolean() && draft->get<bool>()) continue;
        Repo assetSource;
        if (!parseRelease(item.dump(),assetSource)) continue;
        Release release;
        release.id = id->get<uint64_t>();
        if (std::any_of(parsed.begin(),parsed.end(),[&](const Release& r) { return r.id == release.id; })) continue;
        release.tag = assetSource.version;
        release.title = stringField(item,"name");
        if (release.title.empty()) release.title = release.tag;
        release.notes = stringField(item,"body");
        release.published = stringField(item,"published_at");
        auto author = item.find("author"), pre = item.find("prerelease");
        if (author != item.end() && author->is_object()) release.author = stringField(*author,"login");
        release.prerelease = pre != item.end() && pre->is_boolean() && pre->get<bool>();
        release.assets = std::move(assetSource.assets);
        parsed.push_back(std::move(release));
    }
    releases = std::move(parsed); error.clear(); return true;
}
bool parseLatest(const std::string& body, uint64_t& id, std::string& tag) {
    auto item = nlohmann::json::parse(body,nullptr,false);
    if (!item.is_object()) return false;
    auto value = item.find("id");
    if (value == item.end() || !value->is_number_unsigned() || value->get<uint64_t>() == 0) return false;
    auto version = stringField(item,"tag_name");
    if (version.empty()) return false;
    id = value->get<uint64_t>(); tag = version; return true;
}
bool hasNextLink(const std::string& header) {
    return lowerAscii(header).compare(0,5,"link:") == 0 && header.find("rel=\"next\"") != std::string::npos;
}

}
