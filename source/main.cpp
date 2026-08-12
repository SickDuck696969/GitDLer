#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <curl/curl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <cctype>

// Application States
enum AppState { 
    STATE_MAIN, 
    STATE_CONFIRM_DELETE, 
    STATE_FILE_PICKER, 
    STATE_PATH_HISTORY,
    STATE_CONFIRM_OVERWRITE_DL,
    STATE_DOWNLOADING
};

// Repository Structure
struct Repo {
    std::string url;
    std::string name;
    std::string savedVersion;
    std::string onlineVersion;
    std::string downloadUrl;
    std::string fileName;
    bool needsUpdate = false;
    bool isDirect = false;
};

// Global Variables
std::vector<Repo> repos;
int selectedRepoIdx = 0;
AppState currentState = STATE_MAIN;
std::string currentPath = "sdmc:/";
std::vector<std::string> dirList;
int selectedDirIdx = 0;
int dirScrollOffset = 0;

// Path History Variables
std::vector<std::string> pathHistory;
int selectedHistoryIdx = 0;
int historyScrollOffset = 0;

std::string downloadStatus = "";
int downloadPercent = 0;
std::string pendingDownloadPath = "";

// Pause / Cancel Control Variables
bool isDownloadPaused = false;
bool shouldCancelDownload = false;
PadState* downloadPadRef = nullptr;

TTF_Font* font24 = nullptr;
TTF_Font* font18 = nullptr;
SDL_Texture* bgTexture = nullptr;
SDL_Renderer* globalRenderer = nullptr;

// --- STRING & FILE HELPERS ---
std::string extractRepoName(const std::string& url) {
    std::string cleanUrl = url;
    size_t qPos = cleanUrl.find('?');
    if (qPos != std::string::npos) {
        cleanUrl = cleanUrl.substr(0, qPos);
    }

    size_t pos = cleanUrl.find_last_of('/');
    if (pos != std::string::npos && pos + 1 < cleanUrl.length()) {
        return cleanUrl.substr(pos + 1);
    }
    return "downloaded_file";
}

void createDir(const std::string& path) {
    std::string temp;
    for (char c : path) {
        temp += c;
        if (c == '/') mkdir(temp.c_str(), 0777);
    }
    mkdir(temp.c_str(), 0777);
}

std::string getJsonValue(const std::string& json, const std::string& key) {
    size_t keyPos = json.find("\"" + key + "\":");
    if (keyPos == std::string::npos) return "";
    size_t startQuote = json.find("\"", keyPos + key.length() + 2);
    if (startQuote == std::string::npos) return "";
    size_t endQuote = json.find("\"", startQuote + 1);
    if (endQuote == std::string::npos) return "";
    return json.substr(startQuote + 1, endQuote - startQuote - 1);
}

// --- NETWORK/CURL FUNCTIONS ---
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

size_t WriteFileCallback(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    return fwrite(ptr, size, nmemb, stream);
}

void renderBackgroundAndProgress(int percent, const std::string& statusText) {
    if (!globalRenderer) return;

    SDL_SetRenderDrawColor(globalRenderer, 30, 30, 40, 255);
    SDL_RenderClear(globalRenderer);
    if (bgTexture) SDL_RenderCopy(globalRenderer, bgTexture, NULL, NULL);

    SDL_Rect box = { 340, 240, 600, 240 };
    SDL_SetRenderDrawColor(globalRenderer, 20, 20, 30, 220);
    SDL_SetRenderDrawBlendMode(globalRenderer, SDL_BLENDMODE_BLEND);
    SDL_RenderFillRect(globalRenderer, &box);
    SDL_SetRenderDrawBlendMode(globalRenderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(globalRenderer, 80, 80, 120, 255);
    SDL_RenderDrawRect(globalRenderer, &box);

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color yellow = {255, 230, 50, 255};

    auto renderQuickText = [](TTF_Font* font, const std::string& text, int x, int y, SDL_Color color) {
        if (text.empty() || !font) return;
        SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
        if (surface) {
            SDL_Texture* texture = SDL_CreateTextureFromSurface(globalRenderer, surface);
            SDL_Rect dest = { x, y, surface->w, surface->h };
            SDL_RenderCopy(globalRenderer, texture, NULL, &dest);
            SDL_DestroyTexture(texture);
            SDL_FreeSurface(surface);
        }
    };

    renderQuickText(font24, statusText, 380, 260, yellow);
    renderQuickText(font18, "[A] Pause/Resume   [B] Cancel & Delete", 380, 300, white);

    SDL_Rect barBg = { 380, 350, 520, 30 };
    SDL_SetRenderDrawColor(globalRenderer, 60, 60, 80, 255);
    SDL_RenderFillRect(globalRenderer, &barBg);

    int fillWidth = (520 * percent) / 100;
    SDL_Rect barFill = { 380, 350, fillWidth, 30 };
    SDL_SetRenderDrawColor(globalRenderer, 50, 220, 50, 255);
    SDL_RenderFillRect(globalRenderer, &barFill);

    renderQuickText(font24, std::to_string(percent) + "%", 600, 400, white);
    SDL_RenderPresent(globalRenderer);
}

int curlProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    if (downloadPadRef) {
        padUpdate(downloadPadRef);
        u64 kDown = padGetButtonsDown(downloadPadRef);
        if (kDown & HidNpadButton_A) {
            isDownloadPaused = !isDownloadPaused;
        }
    }

    while (isDownloadPaused && !shouldCancelDownload) {
        renderBackgroundAndProgress(downloadPercent, "Paused (Press [A] to Resume)");
        if (downloadPadRef) {
            padUpdate(downloadPadRef);
            u64 kDown = padGetButtonsDown(downloadPadRef);
            if (kDown & HidNpadButton_A) {
                isDownloadPaused = false;
                break;
            }
            if (kDown & HidNpadButton_B) {
                shouldCancelDownload = true;
                return 1; 
            }
        }
        svcSleepThread(50000000); 
    }

    if (shouldCancelDownload) return 1;

    downloadPercent = (dltotal > 0) ? (int)((dlnow * 100) / dltotal) : 0;
    renderBackgroundAndProgress(downloadPercent, "Downloading... (Press [A] to Pause)");
    return 0;
}

void checkRepoUpdate(Repo& r) {
    if (r.isDirect) {
        r.onlineVersion = "Direct";
        r.downloadUrl = r.url;
        r.fileName = extractRepoName(r.url);
        r.needsUpdate = false;
        return;
    }

    std::string apiUrl = r.url;
    size_t githubPos = apiUrl.find("github.com/");
    if (githubPos != std::string::npos) {
        apiUrl.replace(githubPos, 11, "api.github.com/repos/");
        apiUrl += "/releases/latest";
    } else {
        return;
    }

    CURL* curl = curl_easy_init();
    if (curl) {
        std::string readBuffer;
        curl_easy_setopt(curl, CURLOPT_URL, apiUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "GitDLer-Switch/1.0");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_AUTOREFERER, 1L);
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        r.onlineVersion = getJsonValue(readBuffer, "tag_name");
        r.downloadUrl = getJsonValue(readBuffer, "browser_download_url");
        r.fileName = extractRepoName(r.downloadUrl);
        
        r.needsUpdate = r.savedVersion.empty() ? true : (!r.onlineVersion.empty() && r.onlineVersion != r.savedVersion);
    }
}

void downloadFile(const std::string& url, const std::string& savePath, PadState& pad) {
    CURL* curl = curl_easy_init();
    if (curl) {
        FILE* fp = fopen(savePath.c_str(), "wb");
        if (fp) {
            downloadPercent = 0;
            isDownloadPaused = false;
            shouldCancelDownload = false;
            downloadPadRef = &pad;

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "GitDLer-Switch/1.0");
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgressCallback);

            CURLcode res = curl_easy_perform(curl);
            fclose(fp);

            if (shouldCancelDownload || res == CURLE_ABORTED_BY_CALLBACK) {
                remove(savePath.c_str()); 
                downloadStatus = "Download cancelled!";
            } else {
                downloadStatus = (res == CURLE_OK) ? "Download successful!" : "Download failed!";
                if (res != CURLE_OK) {
                    remove(savePath.c_str());
                }
            }
        } else {
            downloadStatus = "Failed to create file!";
        }
        curl_easy_cleanup(curl);
    }
}

// --- CONFIG & HISTORY MANAGEMENT ---
void saveConfig() {
    mkdir("sdmc:/switch/GitDLer", 0777);
    std::ofstream out("sdmc:/switch/GitDLer/repos.txt");
    for (const auto& r : repos) {
        out << r.url << "|" << r.savedVersion << "|" << (r.isDirect ? "1" : "0") << "|" << r.name << "\n";
    }
}

void loadConfig() {
    repos.clear();
    std::ifstream in("sdmc:/switch/GitDLer/repos.txt");
    std::string line;
    while (std::getline(in, line)) {
        std::vector<std::string> parts;
        size_t pos = 0, found;
        while ((found = line.find('|', pos)) != std::string::npos) {
            parts.push_back(line.substr(pos, found - pos));
            pos = found + 1;
        }
        parts.push_back(line.substr(pos));

        if (parts.size() >= 2) {
            Repo r;
            r.url = parts[0];
            r.savedVersion = parts[1];
            r.isDirect = (parts.size() >= 3 && parts[2] == "1");
            r.name = (parts.size() >= 4) ? parts[3] : extractRepoName(r.url);
            
            if (r.isDirect) r.fileName = extractRepoName(r.url);
            repos.push_back(r);
        }
    }
    for (auto& r : repos) checkRepoUpdate(r);
}

void saveHistory() {
    mkdir("sdmc:/switch/GitDLer", 0777);
    std::ofstream out("sdmc:/switch/GitDLer/history.txt");
    for (const auto& p : pathHistory) {
        out << p << "\n";
    }
}

void loadHistory() {
    pathHistory.clear();
    std::ifstream in("sdmc:/switch/GitDLer/history.txt");
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) pathHistory.push_back(line);
    }
}

void addPathToHistory(const std::string& path) {
    auto it = std::find(pathHistory.begin(), pathHistory.end(), path);
    if (it != pathHistory.end()) {
        pathHistory.erase(it);
    }
    pathHistory.insert(pathHistory.begin(), path);
    if (pathHistory.size() > 15) pathHistory.pop_back(); // Giới hạn 15 đường dẫn
    saveHistory();
}

std::string showKeyboard(const std::string& guideText, const std::string& initialText) {
    SwkbdConfig kbd;
    swkbdCreate(&kbd, 0);
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetGuideText(&kbd, guideText.c_str());
    swkbdConfigSetInitialText(&kbd, initialText.c_str());
    
    char out_string[512] = {0};
    if (R_SUCCEEDED(swkbdShow(&kbd, out_string, sizeof(out_string)))) {
        swkbdClose(&kbd);
        return std::string(out_string);
    }
    swkbdClose(&kbd);
    return "";
}

void loadDir(const std::string& path) {
    dirList.clear();
    DIR* dir = opendir(path.c_str());
    if (dir) {
        struct dirent* ent;
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_type == DT_DIR) {
                std::string name = ent->d_name;
                if (name != "." && name != "..") dirList.push_back(name);
            }
        }
        closedir(dir);
    }
    std::sort(dirList.begin(), dirList.end(), [](const std::string& a, const std::string& b) {
        std::string lowerA = a, lowerB = b;
        std::transform(lowerA.begin(), lowerA.end(), lowerA.begin(), ::tolower);
        std::transform(lowerB.begin(), lowerB.end(), lowerB.begin(), ::tolower);
        return lowerA < lowerB;
    });

    if (path != "sdmc:/") {
        dirList.insert(dirList.begin(), "..");
    }

    selectedDirIdx = 0;
    dirScrollOffset = 0;
}

void renderText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, int x, int y, SDL_Color color) {
    if (text.empty()) return;
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (surface) {
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_Rect dest = { x, y, surface->w, surface->h };
        SDL_RenderCopy(renderer, texture, NULL, &dest);
        SDL_DestroyTexture(texture);
        SDL_FreeSurface(surface);
    }
}

int main(int argc, char* argv[]) {
    romfsInit();
    socketInitializeDefault();
    plInitialize(PlServiceType_User);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    SDL_Init(SDL_INIT_VIDEO);
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
    TTF_Init();

    SDL_Window* window = SDL_CreateWindow("GitDLer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    globalRenderer = renderer;

    SDL_Surface* bgSurf = IMG_Load("romfs:/bg.png");
    if (bgSurf) {
        bgTexture = SDL_CreateTextureFromSurface(renderer, bgSurf);
        SDL_FreeSurface(bgSurf);
        SDL_SetTextureBlendMode(bgTexture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(bgTexture, 50);
    }

    PlFontData fontData;
    plGetSharedFontByType(&fontData, PlSharedFontType_Standard);
    SDL_RWops* rw = SDL_RWFromMem(fontData.address, fontData.size);
    font24 = TTF_OpenFontRW(rw, 1, 24);
    
    rw = SDL_RWFromMem(fontData.address, fontData.size);
    font18 = TTF_OpenFontRW(rw, 1, 18);

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color green = {50, 220, 50, 255};
    SDL_Color yellow = {255, 230, 50, 255};
    SDL_Color gray = {150, 150, 150, 255};
    SDL_Color red = {255, 100, 100, 255};

    loadConfig();
    loadHistory();

    bool running = true;
    while (appletMainLoop() && running) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        // --- BACKGROUND RENDER ---
        SDL_SetRenderDrawColor(renderer, 30, 30, 40, 255);
        SDL_RenderClear(renderer);
        if (bgTexture) SDL_RenderCopy(renderer, bgTexture, NULL, NULL);

        SDL_Rect topBar = {0, 0, 1280, 60};
        SDL_SetRenderDrawColor(renderer, 50, 50, 70, 200);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_RenderFillRect(renderer, &topBar);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        renderText(renderer, font24, "GitDLer - Universal Downloader", 40, 15, white);

        // --- STATE HANDLING ---
        if (currentState == STATE_MAIN) {
            if (kDown & HidNpadButton_Plus) {
                std::string input = showKeyboard("Enter GitHub Repo URL", "https://github.com/");
                if (!input.empty()) {
                    Repo r;
                    r.url = input;
                    r.name = extractRepoName(r.url);
                    r.isDirect = false;
                    checkRepoUpdate(r);
                    repos.push_back(r);
                    saveConfig();
                }
            }
            if (kDown & HidNpadButton_Y) {
                std::string input = showKeyboard("Enter Direct Link (Format: Name||https://...)", "Name||https://");
                size_t sep = input.find("||");
                if (sep != std::string::npos && sep > 0) {
                    Repo r;
                    r.name = input.substr(0, sep);
                    r.url = input.substr(sep + 2);
                    r.isDirect = true;
                    r.fileName = extractRepoName(r.url);
                    checkRepoUpdate(r);
                    repos.push_back(r);
                    saveConfig();
                }
            }
            if (kDown & HidNpadButton_Minus) running = false;

            if (!repos.empty()) {
                if (kDown & (HidNpadButton_Up | HidNpadButton_StickLUp)) selectedRepoIdx = std::max(0, selectedRepoIdx - 1);
                if (kDown & (HidNpadButton_Down | HidNpadButton_StickLDown)) selectedRepoIdx = std::min((int)repos.size() - 1, selectedRepoIdx + 1);

                if (kDown & HidNpadButton_B) currentState = STATE_CONFIRM_DELETE;
                if (kDown & HidNpadButton_A) {
                    if (!repos[selectedRepoIdx].downloadUrl.empty()) {
                        currentPath = "sdmc:/";
                        loadDir(currentPath);
                        currentState = STATE_FILE_PICKER;
                    }
                }
            }
        } 
        else if (currentState == STATE_CONFIRM_DELETE) {
            if (kDown & HidNpadButton_A) {
                repos.erase(repos.begin() + selectedRepoIdx);
                if (selectedRepoIdx >= (int)repos.size() && selectedRepoIdx > 0) selectedRepoIdx--;
                saveConfig();
                currentState = STATE_MAIN;
            }
            if (kDown & HidNpadButton_B) currentState = STATE_MAIN;
        } 
        else if (currentState == STATE_FILE_PICKER) {
            if (kDown & HidNpadButton_B) currentState = STATE_MAIN;
            if (kDown & HidNpadButton_Minus) {
                currentState = STATE_PATH_HISTORY;
                selectedHistoryIdx = 0;
                historyScrollOffset = 0;
            }
            
            if (!dirList.empty()) {
                if (kDown & (HidNpadButton_Up | HidNpadButton_StickLUp)) selectedDirIdx = std::max(0, selectedDirIdx - 1);
                if (kDown & (HidNpadButton_Down | HidNpadButton_StickLDown)) selectedDirIdx = std::min((int)dirList.size() - 1, selectedDirIdx + 1);

                if (kDown & HidNpadButton_A) {
                    std::string chosen = dirList[selectedDirIdx];
                    if (chosen == "..") {
                        size_t lastSlash = currentPath.find_last_of('/');
                        if (lastSlash != std::string::npos && lastSlash > 5) {
                            currentPath = currentPath.substr(0, lastSlash);
                            if(currentPath == "sdmc:") currentPath = "sdmc:/";
                        }
                    } else {
                        if (currentPath.back() != '/') currentPath += "/";
                        currentPath += chosen;
                    }
                    loadDir(currentPath);
                }
            }
            if (kDown & HidNpadButton_X) {
                addPathToHistory(currentPath); // Lưu lại đường dẫn đích vào lịch sử
                
                pendingDownloadPath = currentPath;
                if (pendingDownloadPath.back() != '/') pendingDownloadPath += "/";
                pendingDownloadPath += repos[selectedRepoIdx].fileName;

                if (access(pendingDownloadPath.c_str(), F_OK) != -1) {
                    currentState = STATE_CONFIRM_OVERWRITE_DL;
                } else {
                    currentState = STATE_DOWNLOADING;
                    downloadStatus = "Downloading...";
                }
            }
        }
        else if (currentState == STATE_PATH_HISTORY) {
            if (kDown & HidNpadButton_B) currentState = STATE_FILE_PICKER;
            if (!pathHistory.empty()) {
                if (kDown & (HidNpadButton_Up | HidNpadButton_StickLUp)) selectedHistoryIdx = std::max(0, selectedHistoryIdx - 1);
                if (kDown & (HidNpadButton_Down | HidNpadButton_StickLDown)) selectedHistoryIdx = std::min((int)pathHistory.size() - 1, selectedHistoryIdx + 1);

                if (kDown & HidNpadButton_A) {
                    currentPath = pathHistory[selectedHistoryIdx];
                    loadDir(currentPath);
                    currentState = STATE_FILE_PICKER;
                }
            }
        }
        else if (currentState == STATE_CONFIRM_OVERWRITE_DL) {
            if (kDown & HidNpadButton_A) {
                currentState = STATE_DOWNLOADING;
                downloadStatus = "Downloading...";
            }
            if (kDown & HidNpadButton_B) currentState = STATE_FILE_PICKER;
        }
        else if (currentState == STATE_DOWNLOADING) {
            if (downloadStatus == "Downloading...") {
                downloadFile(repos[selectedRepoIdx].downloadUrl, pendingDownloadPath, pad);
                
                if (downloadStatus == "Download successful!") {
                    repos[selectedRepoIdx].savedVersion = repos[selectedRepoIdx].onlineVersion;
                    repos[selectedRepoIdx].needsUpdate = false;
                    saveConfig();
                }
            } else {
                SDL_Rect box = { 340, 240, 600, 240 };
                SDL_SetRenderDrawColor(renderer, 20, 20, 30, 230);
                SDL_RenderFillRect(renderer, &box);
                
                renderText(renderer, font24, downloadStatus, 460, 300, downloadStatus == "Download successful!" ? green : yellow);
                renderText(renderer, font18, "Press [A] to continue", 550, 400, gray);
                
                if (kDown & HidNpadButton_A) {
                    currentState = STATE_MAIN;
                }
            }
        }

        // --- RENDER LOGIC ---
        if (currentState == STATE_MAIN || currentState == STATE_CONFIRM_DELETE) {
            renderText(renderer, font18, "[+] Add Repo   [Y] Add Direct Link   [-] Exit   [A] Download   [B] Delete", 40, 670, gray);

            int yPos = 80;
            for (size_t i = 0; i < repos.size(); ++i) {
                SDL_Rect rowRect = {40, yPos, 1200, 50};
                if ((int)i == selectedRepoIdx) {
                    SDL_SetRenderDrawColor(renderer, 80, 80, 100, 180);
                    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                    SDL_RenderFillRect(renderer, &rowRect);
                    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                }

                renderText(renderer, font24, repos[i].name, 60, yPos + 10, white);
                
                std::string savedDisplay = repos[i].savedVersion.empty() ? "Not Downloaded" : repos[i].savedVersion;
                std::string verText = "Saved: " + savedDisplay + " | Online: " + repos[i].onlineVersion;
                if (repos[i].isDirect) verText = "Type: Direct Download | File: " + repos[i].fileName;
                
                renderText(renderer, font18, verText, 400, yPos + 15, gray);

                if (repos[i].savedVersion.empty() && !repos[i].isDirect) {
                    renderText(renderer, font24, "DOWNLOAD", 1060, yPos + 10, yellow);
                } else if (repos[i].needsUpdate) {
                    renderText(renderer, font24, "UPDATE", 1100, yPos + 10, yellow);
                } else if (repos[i].isDirect) {
                    renderText(renderer, font24, "READY", 1110, yPos + 10, yellow);
                } else {
                    renderText(renderer, font24, "Latest", 1100, yPos + 10, green);
                }
                yPos += 60;
            }

            if (repos.empty()) {
                renderText(renderer, font24, "List is empty. Press [+] for Github or [Y] for Direct Link.", 350, 320, gray);
            }

            if (currentState == STATE_CONFIRM_DELETE) {
                SDL_Rect box = {400, 260, 480, 200};
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 230);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_RenderFillRect(renderer, &box);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(renderer, 255, 50, 50, 255);
                SDL_RenderDrawRect(renderer, &box);
                renderText(renderer, font24, "Delete this item?", 550, 290, white);
                renderText(renderer, font24, "[A] Yes        [B] No", 525, 380, gray);
            }
        } 
        else if (currentState == STATE_FILE_PICKER || currentState == STATE_PATH_HISTORY) {
            renderText(renderer, font18, "Select directory | [X] Download Here | [B] Cancel | [A] Enter Folder | [-] History", 40, 670, gray);
            renderText(renderer, font24, "Current Path: " + currentPath, 40, 80, yellow);

            int maxVisibleItems = 10;
            if (selectedDirIdx < dirScrollOffset) dirScrollOffset = selectedDirIdx;
            else if (selectedDirIdx >= dirScrollOffset + maxVisibleItems) dirScrollOffset = selectedDirIdx - maxVisibleItems + 1;

            int yPos = 130;
            for (size_t i = dirScrollOffset; i < dirList.size() && i < (size_t)(dirScrollOffset + maxVisibleItems); ++i) {
                SDL_Rect rowRect = {40, yPos, 1200, 40};
                if ((int)i == selectedDirIdx && currentState == STATE_FILE_PICKER) {
                    SDL_SetRenderDrawColor(renderer, 80, 80, 100, 180);
                    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                    SDL_RenderFillRect(renderer, &rowRect);
                    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                }
                renderText(renderer, font24, (dirList[i] == ".." ? "<- Back" : "[DIR] " + dirList[i]), 60, yPos + 5, white);
                yPos += 45;
            }

            // Draw History Overlay
            if (currentState == STATE_PATH_HISTORY) {
                SDL_Rect box = { 240, 100, 800, 520 };
                SDL_SetRenderDrawColor(renderer, 20, 20, 30, 245);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_RenderFillRect(renderer, &box);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(renderer, 80, 80, 120, 255);
                SDL_RenderDrawRect(renderer, &box);

                renderText(renderer, font24, "Path History", 560, 120, yellow);
                renderText(renderer, font18, "[A] Jump to Path   [B] Close", 530, 580, gray);

                if (pathHistory.empty()) {
                    renderText(renderer, font24, "History is empty.", 540, 320, gray);
                } else {
                    int maxHistItems = 10;
                    if (selectedHistoryIdx < historyScrollOffset) historyScrollOffset = selectedHistoryIdx;
                    else if (selectedHistoryIdx >= historyScrollOffset + maxHistItems) historyScrollOffset = selectedHistoryIdx - maxHistItems + 1;

                    int hY = 170;
                    for (size_t i = historyScrollOffset; i < pathHistory.size() && i < (size_t)(historyScrollOffset + maxHistItems); ++i) {
                        SDL_Rect rowRect = { 250, hY, 780, 40 };
                        if ((int)i == selectedHistoryIdx) {
                            SDL_SetRenderDrawColor(renderer, 80, 80, 100, 255);
                            SDL_RenderFillRect(renderer, &rowRect);
                        }
                        renderText(renderer, font24, pathHistory[i], 270, hY + 5, white);
                        hY += 40;
                    }
                }
            }
        }
        else if (currentState == STATE_CONFIRM_OVERWRITE_DL) {
            SDL_Rect box = { 340, 240, 600, 240 };
            SDL_SetRenderDrawColor(renderer, 20, 20, 30, 240);
            SDL_RenderFillRect(renderer, &box);
            SDL_SetRenderDrawColor(renderer, 255, 50, 50, 255);
            SDL_RenderDrawRect(renderer, &box);

            renderText(renderer, font24, "File already exists!", 520, 280, red);
            renderText(renderer, font24, "Overwrite existing file?", 490, 330, white);
            renderText(renderer, font18, "[A] Yes, Overwrite       [B] No, Cancel", 480, 410, gray);
        }

        if (currentState != STATE_DOWNLOADING || downloadStatus != "Downloading...") {
            SDL_RenderPresent(renderer);
        }
    }

    if (bgTexture) SDL_DestroyTexture(bgTexture);
    TTF_CloseFont(font24);
    TTF_CloseFont(font18);
    TTF_Quit();
    IMG_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    curl_global_cleanup();
    plExit();
    socketExit();
    romfsExit();
    return 0;
}