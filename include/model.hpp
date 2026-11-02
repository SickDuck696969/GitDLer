#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace gitdler {
struct Asset {
    std::string name, url;
    uint64_t size = 0;
    bool selected = false;
};
struct Release {
    uint64_t id = 0;
    std::string tag, title, notes, published, author;
    bool prerelease = false;
    std::vector<Asset> assets;
};
struct Repo {
    std::string url, name, savedVersion, version, error, lastPath, latestVersion, releaseTitle;
    bool direct = false, favorite = false;
    std::vector<Asset> assets; // Editable selection for the currently opened release.
    std::vector<Release> releases; // One server page at a time bounds applet memory.
    uint64_t latestId = 0, activeReleaseId = 0;
    int releasePage = 0;
    bool hasNextPage = false, latestChecked = false;
};
bool safeName(const std::string& value);
bool httpsUrl(const std::string& value);
std::string lowerAscii(std::string value);
std::string normalizeRepo(std::string value);
std::string fileName(std::string url);
bool parseRelease(const std::string& body, Repo& repo);
bool parseReleasePage(const std::string& body, std::vector<Release>& releases, std::string& error);
bool parseLatest(const std::string& body, uint64_t& id, std::string& tag);
bool hasNextLink(const std::string& header);
std::string bytes(double value);
}
