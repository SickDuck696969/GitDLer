#include "app.hpp"
#include <sys/stat.h>
namespace gitdler::app {
namespace { bool cancelWasPaused = false, extractWasPaused = false; }
void moveCursor(u64 pressed, int count) {
    if (count <= 0) { cursor = scroll = 0; return; }
    if (pressed & (HidNpadButton_Up | HidNpadButton_StickLUp)) --cursor;
    if (pressed & (HidNpadButton_Down | HidNpadButton_StickLDown)) ++cursor;
    if (pressed & HidNpadButton_ZL) cursor -= 6;
    if (pressed & HidNpadButton_ZR) cursor += 6;
    cursor = std::max(0,std::min(count-1,cursor));
    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + 6) scroll = cursor - 5;
}
void addRepo(bool direct) {
    std::string input = keyboard(direct ? "HTTPS file URL" : "GitHub URL or owner/repository",direct ? "https://" : "https://github.com/");
    if (input.empty()) return;
    Repo r; r.direct = direct; r.url = direct ? input : normalizeRepo(input);
    if (!httpsUrl(r.url)) { notify("Enter a valid HTTPS link or GitHub owner/repository."); return; }
    if (std::any_of(repos.begin(),repos.end(),[&](const Repo& old) { return old.url == r.url; })) { notify("This source is already in your library."); return; }
    r.name = fileName(r.url);
    repos.push_back(r); repoIndex = static_cast<int>(repos.size()) - 1;
    cursor = repoIndex; saveConfig(); notify("Added to your library.");
}
void handleInput(u64 pressed) {
    switch (screen) {
    case Screen::Library:
        cursor = repoIndex; moveCursor(pressed,repos.size()); repoIndex = cursor;
        if (pressed & HidNpadButton_Minus) { running = false; return; }
        if (pressed & HidNpadButton_Plus) { addRepo(false); return; }
        if (pressed & HidNpadButton_Y) { addRepo(true); return; }
        if (pressed & HidNpadButton_X) { changeScreen(Screen::Options); break; }
        if (repos.empty()) break;
        if (pressed & HidNpadButton_L) {
            repos[repoIndex].favorite = !repos[repoIndex].favorite;
            sortFavorites(); cursor = repoIndex; saveConfig(); break;
        }
        if (pressed & (HidNpadButton_A | HidNpadButton_R)) {
            // Applet mode retains only the active source's release page.
            if (compact) for (int i = 0; i < static_cast<int>(repos.size()); ++i) if (i != repoIndex && !repos[i].direct) {
                std::vector<Release>().swap(repos[i].releases);
                std::vector<Asset>().swap(repos[i].assets);
                repos[i].releasePage = 0; repos[i].activeReleaseId = 0;
                repos[i].hasNextPage = false;
            }
            auto& r = repos[repoIndex];
            changeScreen(r.direct ? Screen::Assets : Screen::Releases);
            if (r.direct || r.releasePage == 0 || (pressed & HidNpadButton_R)) fetchReleases(1,true);
        }
        break;
    case Screen::Options:
        moveCursor(pressed,5);
        if (pressed & HidNpadButton_B) { goBack(Screen::Library); break; }
        if (pressed & HidNpadButton_A) {
            if (cursor == 0 && !repos.empty()) {
                auto name = keyboard("Display name",repos[repoIndex].name);
                if (!name.empty()) { repos[repoIndex].name = name; saveConfig(); }
            } else if (cursor == 1 && !repos.empty()) changeScreen(Screen::Delete);
            else if (cursor == 2) { reducedMotion = !reducedMotion; savePreferences(); }
            else if (cursor == 3) { largeNotes = !largeNotes; savePreferences(); }
            else if (cursor == 4) running = false;
        }
        break;
    case Screen::Releases: {
        if (pressed & HidNpadButton_B) { closeRequest(); goBack(Screen::Library); break; }
        if (fetching) break;
        auto& r = repos[repoIndex]; moveCursor(pressed,r.releases.size());
        if (pressed & HidNpadButton_Plus) { fetchReleases(std::max(1,r.releasePage),true); break; }
        if ((pressed & HidNpadButton_L) && r.releasePage > 1) { fetchReleases(r.releasePage-1); break; }
        if ((pressed & HidNpadButton_R) && r.hasNextPage) { fetchReleases(r.releasePage+1); break; }
        if (r.releases.empty()) break;
        if (pressed & (HidNpadButton_A | HidNpadButton_X)) {
            selectRelease(cursor);
            if (pressed & HidNpadButton_A) { notesReturn = Screen::Releases; changeScreen(Screen::Notes); prepareNotes(); }
            else changeScreen(Screen::Assets);
        }
        break;
    }
    case Screen::Notes:
        scrollNotes(pressed);
        if (pressed & HidNpadButton_B) { goBack(notesReturn); break; }
        if (pressed & HidNpadButton_Y) { largeNotes = !largeNotes; savePreferences(); prepareNotes(); }
        if (pressed & HidNpadButton_A) {
            if (repos[repoIndex].assets.empty()) notify("This release has no downloadable assets.");
            else if (notesReturn == Screen::Assets) goBack(Screen::Assets);
            else changeScreen(Screen::Assets);
        }
        break;
    case Screen::Delete:
        if (pressed & HidNpadButton_B) goBack(Screen::Options);
        else if (pressed & HidNpadButton_A) {
            repos.erase(repos.begin()+repoIndex); repoIndex = std::max(0,std::min(repoIndex,static_cast<int>(repos.size())-1));
            saveConfig(); changeScreen(Screen::Library); notify("Source removed. Downloaded files were kept.");
        }
        break;
    case Screen::Assets: {
        auto& repo = repos[repoIndex];
        if (pressed & HidNpadButton_B) { goBack(repo.direct ? Screen::Library : Screen::Releases); break; }
        auto& assets = repo.assets; moveCursor(pressed,assets.size());
        if ((pressed & HidNpadButton_R) && !repo.direct) { notesReturn = Screen::Assets; changeScreen(Screen::Notes); prepareNotes(); break; }
        if (pressed & HidNpadButton_L) {
            for (auto& asset : assets) {
                auto name = lowerAscii(asset.name);
                asset.selected = name.size() >= 4 && name.substr(name.size()-4) == ".nro";
            }
            if (!selectedCount()) notify("No .nro files in this release.");
        }
        if (assets.empty()) break;
        if (pressed & HidNpadButton_A) assets[cursor].selected = !assets[cursor].selected;
        if (pressed & HidNpadButton_Y) { bool select = selectedCount() != static_cast<int>(assets.size()); for (auto& a : assets) a.selected = select; }
        if (pressed & HidNpadButton_X) {
            if (!selectedCount()) { notify("Select at least one file with A."); break; }
            path = repo.lastPath.empty() ? (history.empty() ? "sdmc:/" : history.front()) : repo.lastPath;
            if (path.compare(0,6,"sdmc:/") != 0) path = "sdmc:/";
            if (path.back() != '/') path += '/';
            if (!directory(path)) path = "sdmc:/";
            changeScreen(Screen::Destination); loadFolders();
        }
        break;
    }
    case Screen::Destination:
        moveCursor(pressed,folders.size());
        if (pressed & HidNpadButton_B) { if (choosingExtraction) leaveExtractionDestination(); else goBack(Screen::Assets); break; }
        if (pressed & HidNpadButton_Y) { changeScreen(Screen::History); break; }
        if (pressed & HidNpadButton_X) { if (choosingExtraction) changeScreen(Screen::ExtractReview); else buildQueue(); break; }
        if (pressed & HidNpadButton_L) {
            if (path != "sdmc:/") { path.pop_back(); path.resize(path.find_last_of('/')+1); loadFolders(); }
            break;
        }
        if (pressed & HidNpadButton_Plus) {
            auto name = keyboard("New folder name");
            if (name.empty()) break;
            if (!safeName(name)) { notify("Use a folder name without slashes or special characters."); break; }
            if (mkdir((path+name).c_str(),0777) != 0) notify("Cannot create folder. It may already exist.");
            loadFolders(); break;
        }
        if ((pressed & HidNpadButton_A) && !folders.empty()) {
            if (folders[cursor] == "..") { path.pop_back(); path.resize(path.find_last_of('/')+1); }
            else path += folders[cursor] + '/';
            loadFolders();
        }
        break;
    case Screen::History:
        moveCursor(pressed,history.size());
        if (pressed & HidNpadButton_B) { goBack(Screen::Destination); }
        else if ((pressed & HidNpadButton_A) && !history.empty()) {
            if (!directory(history[cursor])) { notify("This folder no longer exists."); break; }
            path = history[cursor]; changeScreen(Screen::Destination); loadFolders();
        }
        break;
    case Screen::Review:
        moveCursor(pressed,jobs.size());
        if (pressed & HidNpadButton_B) { goBack(Screen::Destination); }
        else if (pressed & HidNpadButton_X) startQueue(false);
        else if (pressed & HidNpadButton_Y) startQueue(true);
        break;
    case Screen::Transfer:
        if (pressed & HidNpadButton_B) {
            cancelWasPaused = paused;
            if (!paused) togglePause();
            changeScreen(Screen::Cancel); break;
        }
        if (pressed & HidNpadButton_A) togglePause();
        break;
    case Screen::Cancel:
        if (pressed & HidNpadButton_A) cancelQueue();
        else if (pressed & HidNpadButton_B) {
            if (!cancelWasPaused && paused) togglePause();
            goBack(Screen::Transfer);
        }
        break;
    case Screen::Results:
        moveCursor(pressed,jobs.size());
        if (pressed & (HidNpadButton_A | HidNpadButton_B)) { changeScreen(Screen::Library); break; }
        if (pressed & HidNpadButton_X) retryFailed();
        else if ((pressed & HidNpadButton_Y) && !jobs.empty()) offerExtraction(cursor);
        break;
    case Screen::ExtractPrompt:
        if (pressed & HidNpadButton_A) chooseExtractionDestination();
        else if (pressed & HidNpadButton_B) skipExtraction(false);
        else if (pressed & HidNpadButton_Y) skipExtraction(true);
        break;
    case Screen::ExtractReview:
        if (pressed & HidNpadButton_B) goBack(Screen::Destination);
        else if (pressed & HidNpadButton_X) startExtraction(true);
        else if (pressed & HidNpadButton_Y) startExtraction(false);
        break;
    case Screen::Extracting:
        if (pressed & HidNpadButton_B) {
            extractWasPaused = extractionPaused; pauseExtraction(true); changeScreen(Screen::ExtractCancel);
        } else if (pressed & HidNpadButton_A) pauseExtraction(!extractionPaused);
        break;
    case Screen::ExtractCancel:
        if (pressed & HidNpadButton_A) { cancelExtraction(); changeScreen(Screen::Extracting); }
        else if (pressed & HidNpadButton_B) { pauseExtraction(extractWasPaused); goBack(Screen::Extracting); }
        break;
    }
}


}
