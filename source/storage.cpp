#include "app.hpp"
#include "json_utils.hpp"
#include "file_ops.hpp"
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <fstream>
#include <sstream>
namespace gitdler::app {
bool exists(const std::string& p) { struct stat s; return stat(p.c_str(), &s) == 0; }
bool directory(const std::string& p) { struct stat s; return stat(p.c_str(), &s) == 0 && S_ISDIR(s.st_mode); }
void saveConfig() {
    std::string temp = std::string(CONFIG) + "repos.json.tmp";
    auto data = nlohmann::json::array();
    for (const auto& r : repos) data.push_back({{"url",r.url},{"savedVersion",r.savedVersion},{"direct",r.direct},{"name",r.name},{"favorite",r.favorite},{"lastPath",r.lastPath}});
    std::ofstream out(temp);
    out << data.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    out.close();
    std::string error;
    if (!out || !replaceFile(temp,std::string(CONFIG)+"repos.json",error)) notify("Could not save library. " + error);
}
void rememberPath() {
    repos[repoIndex].lastPath = path; saveConfig();
    history.erase(std::remove(history.begin(), history.end(), path), history.end());
    history.insert(history.begin(), path);
    if (history.size() > 15) history.resize(15);
    std::ofstream out(std::string(CONFIG) + "history.txt");
    for (const auto& p : history) out << p << '\n';
}
void loadConfig() {
    mkdir("sdmc:/switch", 0777); mkdir(CONFIG, 0777);
    const std::string config = std::string(CONFIG) + "repos.json";
    if (!exists(config) && exists(config+".gitdler-backup")) rename((config+".gitdler-backup").c_str(),config.c_str());
    bool loaded = false;
    std::ifstream jsonFile(config);
    if (jsonFile) {
        auto data = nlohmann::json::parse(jsonFile,nullptr,false);
        if (data.is_array()) {
            loaded = true;
            for (const auto& item : data) {
                if (!item.is_object()) continue;
                Repo r; r.url = stringField(item,"url"); r.name = stringField(item,"name");
                r.savedVersion = stringField(item,"savedVersion");
                r.lastPath = stringField(item,"lastPath");
                auto favorite = item.find("favorite");
                r.favorite = favorite != item.end() && favorite->is_boolean() && favorite->get<bool>();
                auto d = item.find("direct"); r.direct = d != item.end() && d->is_boolean() && d->get<bool>();
                if (!r.direct) r.url = normalizeRepo(r.url);
                if (!httpsUrl(r.url)) continue;
                if (r.name.empty()) r.name = fileName(r.url);
                repos.push_back(r);
            }
        } else notify("Library config is unreadable. Loading legacy library if available.");
    }
    std::ifstream in(std::string(CONFIG) + "repos.txt"); std::string line;
    while (!loaded && std::getline(in, line)) {
        std::istringstream row(line); Repo r; std::string direct;
        std::getline(row, r.url, '|'); std::getline(row, r.savedVersion, '|');
        std::getline(row, direct, '|'); std::getline(row, r.name);
        r.direct = direct == "1";
        if (!r.direct) r.url = normalizeRepo(r.url);
        if (!httpsUrl(r.url)) continue;
        if (r.name.empty()) r.name = fileName(r.url);
        repos.push_back(r);
    }
    std::ifstream paths(std::string(CONFIG) + "history.txt");
    while (std::getline(paths, line) && history.size() < 15)
        if (line.compare(0, 6, "sdmc:/") == 0 && directory(line)) history.push_back(line.back() == '/' ? line : line + '/');
    sortFavorites();
    std::ifstream preferences(std::string(CONFIG)+"preferences.json");
    if (preferences) {
        auto j = nlohmann::json::parse(preferences,nullptr,false);
        if (j.is_object()) {
            auto motion = j.find("reducedMotion"), notes = j.find("largeNotes");
            reducedMotion = motion != j.end() && motion->is_boolean() && motion->get<bool>();
            largeNotes = notes != j.end() && notes->is_boolean() && notes->get<bool>();
        }
    }
}
void savePreferences() {
    nlohmann::json j{{"reducedMotion",reducedMotion},{"largeNotes",largeNotes}};
    std::ofstream out(std::string(CONFIG)+"preferences.json"); out << j.dump(2);
    out.close(); if (!out) notify("Could not save preferences.");
}
void sortFavorites() {
    const std::string active = repos.empty() ? "" : repos[repoIndex].url;
    std::stable_sort(repos.begin(),repos.end(),[](const Repo& a, const Repo& b) { return a.favorite > b.favorite; });
    for (size_t i = 0; i < repos.size(); ++i) if (repos[i].url == active) repoIndex = i;
}
void updateFreeSpace() {
    struct statvfs info{};
    freeSpace = statvfs(path.c_str(),&info) == 0 ? static_cast<double>(info.f_bavail)*info.f_frsize : -1;
}
void loadFolders() {
    updateFreeSpace();
    folders.clear(); cursor = scroll = 0;
    DIR* dir = opendir(path.c_str());
    if (!dir) { notify("Cannot open this folder."); return; }
    while (auto* e = readdir(dir)) {
        std::string name = e->d_name;
        if (name != "." && name != ".." && directory(path + name)) folders.push_back(name);
    }
    closedir(dir); std::sort(folders.begin(), folders.end());
    if (path != "sdmc:/") folders.insert(folders.begin(), "..");
}
std::string keyboard(const std::string& prompt, const std::string& initial) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) { notify("Keyboard is unavailable in this mode."); return ""; }
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetGuideText(&kbd, prompt.c_str()); swkbdConfigSetInitialText(&kbd, initial.c_str());
    char value[1024] = {};
    Result result = swkbdShow(&kbd, value, sizeof(value)); swkbdClose(&kbd);
    return R_SUCCEEDED(result) ? value : "";
}


}
