#include "app.hpp"
#include "ui.hpp"
#include <cmath>
namespace gitdler::app {
std::string jobLabel(const Job& j) {
    switch (j.state) {
    case JobState::Waiting: return "Queued";
    case JobState::Active: return paused ? "Paused" : "Downloading";
    case JobState::Done: return !j.extractionResult.empty() ? j.extractionResult : j.error.empty() ? "Saved to SD card" : j.error;
    case JobState::Failed: return "Failed - " + j.error;
    case JobState::Skipped: return "Skipped - file already exists";
    case JobState::Cancelled: return "Cancelled";
    }
    return "";
}
void sidebar(const std::string& heading, const std::string& lead, const std::vector<std::string>& lines) {
    if (compact) return;
    card(890,168,342,438,panel);
    text(heading,914,194,accent,294,18); text(lead,914,236,ink,294,32);
    int y = 300;
    for (const auto& line : lines) { text(line,914,y,muted,294,18); y += 42; }
}
void renderTransfer() {
    if (jobIndex < 0 || jobIndex >= static_cast<int>(jobs.size())) return;
    auto& j = jobs[jobIndex]; int w = compact ? 1184 : 810;
    text("Downloading your files",48,114,ink,900,32);
    card(48,168,w,438,panel);
    text(paused ? "PAUSED" : "DOWNLOADING",80,196,paused ? warning : accent,w-64,18);
    text(j.asset.name,80,239,ink,w-64,32);
    text("File " + std::to_string(jobIndex+1) + " of " + std::to_string(jobs.size()),80,290,muted,w-64);
    double target = total > 0 ? std::min(1.0,downloaded / total) : 0;
    visualProgress += (target - visualProgress) * (reducedMotion ? 1.0 : std::min(1.0f,frameSeconds*12));
    progressBar(80,352,w-64,visualProgress,total <= 0 && !paused);
    text(total > 0 ? std::to_string(static_cast<int>(target*100)) + "%" : "Connecting / receiving",80,388,ink,500,32);
    text(bytes(downloaded) + (total > 0 ? " / " + bytes(total) : " received"),80,444,muted,w-64);
    std::string rate = paused ? "Resume when you are ready" : bytes(speed) + "/s";
    if (!paused && total > 0 && speed > 1) {
        auto seconds = static_cast<uint64_t>(std::max(0.0,total-downloaded)/speed);
        rate += "    ~" + std::to_string(seconds/60) + "m " + std::to_string(seconds%60) + "s left";
    }
    text(rate,80,488,accent,w-64); text("To " + path,80,552,muted,w-64,18);
    std::vector<std::string> upcoming;
    for (int i = jobIndex+1; i < static_cast<int>(jobs.size()) && upcoming.size() < 4; ++i)
        if (jobs[i].state == JobState::Waiting) upcoming.push_back(jobs[i].asset.name);
    if (upcoming.empty()) upcoming.push_back("Last file in this queue");
    sidebar("UP NEXT",std::to_string(jobs.size()) + " files",upcoming);
    footer("[A] " + std::string(paused ? "Resume" : "Pause") + "    [B] Cancel remaining downloads");
}
void render() {
    SDL_SetRenderDrawColor(renderer,12,19,31,255); SDL_RenderClear(renderer);
    if (!compact) for (int i = 0; i < 6; ++i) rect(870+i*62,0,1,650,SDL_Color{25,39,54,255});
    text("GitDLer",48,28,ink,600,32);
    text(compact ? "APPLET / COMPACT" : "FULL RAM / EXPANDED",920,39,accent,312,18);
    rect(48,91,1184,1,SDL_Color{43,57,75,255});
    int w = compact ? 1184 : 810;
    beginContentAnimation();
    if (screen == Screen::Library || screen == Screen::Delete) {
        text("Your library",48,114,ink,600,32);
        cursor = repoIndex; moveCursor(0,repos.size());
        for (int i = scroll; i < static_cast<int>(repos.size()) && i < scroll+6; ++i) {
            const auto& r = repos[i];
            std::string status = r.direct ? "Direct HTTPS download" : r.savedVersion.empty() ? "Browse the latest release" : "Last downloaded: " + r.savedVersion;
            if (!r.direct && !r.latestVersion.empty() && !r.savedVersion.empty() && r.latestVersion != r.savedVersion) status = "Latest differs: " + r.latestVersion + "  /  Last downloaded: " + r.savedVersion;
            row(168+(i-scroll)*73,(r.favorite ? "*  " : "")+r.name,status,i == repoIndex,w);
        }
        if (repos.empty()) {
            text("Your next download starts here.",80,260,ink,w-64,32);
            text("Press + to add a GitHub repository.",80,324,muted,w-64);
            text("Or press Y to add a direct HTTPS file link.",80,370,muted,w-64);
        }
        if (repos.empty()) sidebar("GET STARTED","Your collection",{"1  Add a repository","2  Choose release files","3  Pick a destination","4  Review and download"});
        else {
            const auto& r = repos[repoIndex];
            sidebar("SELECTED SOURCE",r.name,{r.direct ? "Direct HTTPS link" : "GitHub release",r.latestVersion.empty() ? "Open to browse releases" : "Latest: " + r.latestVersion,r.savedVersion.empty() ? "No completed download yet" : "Downloaded: " + r.savedVersion,"ZL / ZR  Page through lists"});
            text(std::to_string(repoIndex+1) + " / " + std::to_string(repos.size()) + " sources",48,616,muted,1184,18);
        }
        listPosition(repos.size());
        footer("[A] Open    [+] Add    [Y] Direct link    [L] Favorite    [R] Refresh    [X] Options    [-] Exit");
        if (screen == Screen::Delete) {
            rect(0,0,1280,720,SDL_Color{0,0,0,185}); card(280,225,720,270,panel);
            text("Remove this source?",320,260,ink,640,32); text(repos[repoIndex].name,320,322,muted,640);
            text("Downloaded files will stay on your SD card.",320,367,muted,640,18);
            text("[A] Remove source    [B] Keep",320,431,warning,640);
        }
    } else if (screen == Screen::Options) {
        text("Library options",48,114,ink,1184,32);
        const std::vector<std::pair<std::string,std::string>> options = {
            {"Rename source",repos.empty() ? "Add a source first" : repos[repoIndex].name},
            {"Remove source","Downloaded files will be kept"},
            {"Reduced motion",reducedMotion ? "On - static transitions" : "Off - animated transitions"},
            {"Release notes text",largeNotes ? "Large" : "Standard"},
            {"Exit GitDLer","Return to the Homebrew Menu"}
        };
        for (int i = 0; i < static_cast<int>(options.size()); ++i) row(168+i*73,options[i].first,options[i].second,i==cursor,w);
        sidebar("PREFERENCES","Make it yours",{"A  Change the selected option","Preferences save automatically","B  Return to your library"});
        footer("[A] Change / open    [B] Back    [Up / Down] Navigate");
    } else if (screen == Screen::Releases) {
        const auto& r = repos[repoIndex];
        text("01 / Releases",48,114,ink,700,32);
        text("Page " + std::to_string(std::max(1,r.releasePage)) + (r.hasNextPage ? "  /  more available" : ""),850,124,accent,382,18);
        if (fetching) {
            text("Loading release history...",80,278,ink,w-64,32);
            progressBar(80,350,w-64,0,true);
        } else if (r.releases.empty()) {
            text(r.error.empty() ? "No published releases on this page." : r.error,68,246,warning,w-40);
            text("Use L / R to change pages or + to refresh.",68,300,muted,w-40);
        } else {
            for (int i = scroll; i < static_cast<int>(r.releases.size()) && i < scroll+6; ++i) {
                const auto& release = r.releases[i];
                std::string badges = release.id == r.latestId ? "[LATEST]  " : "";
                if (release.prerelease) badges += "[PRE]  ";
                if (release.tag == r.savedVersion) badges += "[DOWNLOADED]  ";
                releaseRow(168+(i-scroll)*73,badges+release.title,release.tag,release.published.substr(0,10)+"  /  "+std::to_string(release.assets.size())+" files",i==cursor,w);
            }
            const auto& release = r.releases[std::min(cursor,static_cast<int>(r.releases.size())-1)];
            sidebar(r.name,release.tag,{release.title,release.id == r.latestId ? "Latest release" : release.prerelease ? "Prerelease" : "Published release",release.published.substr(0,10),"A  Read full release notes","X  Choose files"});
            listPosition(r.releases.size());
        }
        text(!r.error.empty() ? r.error : r.name + (r.latestId ? "  /  Latest: "+r.latestVersion : "  /  No latest badge available"),48,616,muted,1184,18);
        footer(fetching ? "[B] Back / cancel request" : "[A] Release notes    [X] Files    [L / R] Release pages    [+] Refresh    [B] Back");
    } else if (screen == Screen::Notes) {
        renderNotes();
    } else if (screen == Screen::Assets) {
        auto& r = repos[repoIndex]; text("02 / Choose files",48,114,ink,700,32);
        if (fetching) { text("Finding the latest release...",80,278,ink,w-64,32); progressBar(80,350,w-64,0,true); }
        else if (r.assets.empty()) text("This release has no downloadable assets. Press R to read its notes.",68,244,warning,w-40);
        else {
            for (int i = scroll; i < static_cast<int>(r.assets.size()) && i < scroll+6; ++i) {
                auto& a = r.assets[i];
                row(168+(i-scroll)*73,a.name,(a.selected ? "Selected  /  " : "A to select  /  ") + (a.size ? bytes(a.size) : "Size unknown"),i == cursor,w,a.selected);
            }
        }
        sidebar(r.name,r.version.empty() ? "Release files" : r.version,{std::to_string(selectedCount()) + " files selected",bytes(selectedBytes()) + " known size","A  Toggle a file","Y  Select or clear all"});
        if (compact) text(std::to_string(selectedCount()) + " selected  /  " + bytes(selectedBytes()) + " known size",740,124,accent,492,18);
        text(r.version + "  /  " + (r.releaseTitle.empty() ? r.name : r.releaseTitle),48,616,muted,1184,18);
        listPosition(r.assets.size());
        footer(r.direct ? "[A] Toggle    [Y] Select all / clear    [X] Destination    [B] Back" : "[A] Toggle    [Y] All / clear    [L] Select .nro    [R] Notes    [X] Destination    [B] Back");
    } else if (screen == Screen::Destination || screen == Screen::History) {
        bool recent = screen == Screen::History;
        text(recent ? "Recent folders" : choosingExtraction ? "Choose extraction destination" : "03 / Choose destination",48,114,ink,840,32);
        if (compact && !recent) text(freeSpace < 0 ? "Free space unknown" : bytes(freeSpace)+" free",900,124,accent,332,18);
        const auto& list = recent ? history : folders;
        for (int i = scroll; i < static_cast<int>(list.size()) && i < scroll+6; ++i)
            row(168+(i-scroll)*73,list[i] == ".." ? "..  Parent folder" : list[i],recent ? "A to use this folder" : "Folder",i == cursor,w);
        if (list.empty()) {
            text(recent ? "No recent destinations yet." : "This folder is empty.",80,276,ink,w-64,32);
            text(recent ? "Downloaded-to folders appear here." : "Press X to save your files here.",80,334,muted,w-64);
        }
        sidebar(choosingExtraction ? "EXTRACT TO" : "DESTINATION",choosingExtraction ? "Unpack archive" : std::to_string(selectedCount()) + " files",{path,choosingExtraction ? "Original archive will be kept" : bytes(selectedBytes()) + " known size",freeSpace < 0 ? "Free space unavailable" : bytes(freeSpace)+" free","Y  Recent folders"});
        text(path,48,616,accent,1184,18);
        listPosition(list.size());
        footer(recent ? "[A] Use folder    [B] Back" : "[A] Open    [L] Parent    [X] Review here    [Y] Recent    [+] New folder    [B] Back");
    } else if (screen == Screen::Review || screen == Screen::Results) {
        bool review = screen == Screen::Review; text(review ? "04 / Ready to download" : "Download results",48,114,ink,900,32);
        int conflicts = 0, done = 0, failed = 0;
        for (const auto& j : jobs) { conflicts += j.existed; done += j.state == JobState::Done; failed += j.state == JobState::Failed || j.state == JobState::Cancelled; }
        for (int i = scroll; i < static_cast<int>(jobs.size()) && i < scroll+6; ++i) {
            const auto& j = jobs[i];
            row(168+(i-scroll)*73,j.asset.name,review ? (j.existed ? "Already exists - X replaces / Y skips" : (j.asset.size ? bytes(j.asset.size) : "Size unknown")) : jobLabel(j),i == cursor,w);
        }
        sidebar(review ? "REVIEW" : "SUMMARY",std::to_string(review ? static_cast<int>(jobs.size()) : done) + (review ? " files" : " saved"),
            review ? std::vector<std::string>{std::to_string(conflicts) + " existing files",bytes(selectedBytes()) + " known size",freeSpace < 0 ? "Free space unavailable" : bytes(freeSpace)+" free","Y  Skip existing files"} :
                     std::vector<std::string>{std::to_string(failed) + " failed / cancelled","X  Retry unfinished files","Y  Extract selected archive","A  Return to library"});
        listPosition(jobs.size());
        text(path,48,616,accent,1184,18);
        footer(review ? "[X] Download / replace existing    [Y] Download / skip existing    [B] Back" : "[A / B] Library    [X] Retry downloads    [Y] Extract selected archive    [Up / Down] Results");
        if (compact) text(review ? std::to_string(conflicts) + " existing" + (freeSpace < 0 ? "" : " / "+bytes(freeSpace)+" free") : std::to_string(done) + " saved / " + std::to_string(failed) + " unfinished",840,124,accent,392,18);
    } else if (screen == Screen::ExtractPrompt || screen == Screen::ExtractReview || screen == Screen::Extracting || screen == Screen::ExtractCancel) {
        renderExtraction();
    } else if (screen == Screen::Transfer || screen == Screen::Cancel) {
        renderTransfer();
        if (screen == Screen::Cancel) {
            rect(0,92,1280,558,SDL_Color{0,0,0,185}); card(280,225,720,270,panel);
            text("Cancel remaining downloads?",320,260,ink,640,32);
            text("Completed files will stay on your SD card.",320,332,muted,640);
            text("[A] Cancel queue    [B] Keep downloading",320,419,warning,640);
        }
    }
    endContentAnimation();
    auto age = SDL_GetTicks() - screenSince;
    if (!reducedMotion && age < 160) rect(0,92,1280,558,SDL_Color{12,19,31,static_cast<Uint8>(100*(160-age)/160)});
    if (!toast.empty() && static_cast<int32_t>(toastUntil-SDL_GetTicks()) > 0) {
        rect(72,582,1136,55,selected); text(toast,92,596,ink,1096,18);
    }
    SDL_RenderPresent(renderer);
}

}
