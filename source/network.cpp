#include "app.hpp"
#include "file_ops.hpp"
#include <curl/curl.h>
#include <unistd.h>
namespace gitdler::app {
bool fetching = false;
namespace {
CURLM* multi = nullptr;
CURL* easy = nullptr;
std::string response, temporary;
FILE* output = nullptr;
char curlError[CURL_ERROR_SIZE] = {};
uint32_t sampleTime = 0;
double sampleBytes = 0;

size_t receiveBody(char* ptr, size_t size, size_t count, void*) {
    const size_t bytes = size * count;
    if (response.size() + bytes > 4 * 1024 * 1024) return 0;
    response.append(ptr, bytes); return bytes;
}
size_t receiveFile(char* ptr, size_t size, size_t count, void*) { return fwrite(ptr, 1, size * count, output); }
int transferProgress(void*, curl_off_t size, curl_off_t now, curl_off_t, curl_off_t) {
    total = static_cast<double>(size); downloaded = static_cast<double>(now); return 0;
}
}
void closeRequest() {
    if (easy) { curl_multi_remove_handle(multi,easy); curl_easy_cleanup(easy); easy = nullptr; }
    if (output) { fclose(output); output = nullptr; }
    if (!temporary.empty()) { remove(temporary.c_str()); temporary.clear(); }
    fetching = false; paused = false;
}
bool prepareRequest(const std::string& url) {
    easy = curl_easy_init();
    if (!easy) return false;
    curlError[0] = 0;
    curl_easy_setopt(easy,CURLOPT_URL,url.c_str());
    curl_easy_setopt(easy,CURLOPT_USERAGENT,"GitDLer-Switch/2.2");
    curl_easy_setopt(easy,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(easy,CURLOPT_MAXREDIRS,8L);
    curl_easy_setopt(easy,CURLOPT_PROTOCOLS,CURLPROTO_HTTPS);
    curl_easy_setopt(easy,CURLOPT_REDIR_PROTOCOLS,CURLPROTO_HTTPS);
    curl_easy_setopt(easy,CURLOPT_FAILONERROR,1L);
    curl_easy_setopt(easy,CURLOPT_CONNECTTIMEOUT,20L);
    curl_easy_setopt(easy,CURLOPT_ERRORBUFFER,curlError);
    curl_easy_setopt(easy,CURLOPT_BUFFERSIZE,compact ? 16384L : 65536L);
    // TLS verification stays enabled; support a CA bundle for builds that need one.
    static const std::string ca = std::string(CONFIG) + "cacert.pem";
    if (exists(ca)) curl_easy_setopt(easy,CURLOPT_CAINFO,ca.c_str());
    return true;
}
namespace {
enum class Request { Latest, Releases, Download };
Request request = Request::Download;
int requestedPage = 1;
bool responseHasNext = false;
size_t receiveHeader(char* data, size_t size, size_t count, void*) {
    std::string line(data,size*count);
    if (line.compare(0,5,"HTTP/") == 0) responseHasNext = false;
    if (hasNextLink(line)) responseHasNext = true;
    return size*count;
}
}
bool initializeNetwork() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return false;
    multi = curl_multi_init(); return multi != nullptr;
}
void shutdownNetwork() {
    closeRequest(); appletSetMediaPlaybackState(false);
    if (multi) curl_multi_cleanup(multi);
    multi = nullptr; curl_global_cleanup();
}
void fetchReleases(int page, bool refreshLatest) {
    if (easy) return;
    Repo& r = repos[repoIndex]; r.error.clear();
    if (r.direct) {
        r.version = "Direct link";
        if (r.assets.empty()) r.assets.push_back(Asset{fileName(r.url),r.url,0,true});
        return;
    }
    requestedPage = std::max(1,page);
    request = refreshLatest || !r.latestChecked ? Request::Latest : Request::Releases;
    response.clear(); responseHasNext = false;
    const auto base = "https://api.github.com/repos/" + r.url.substr(std::string("https://github.com/").size()) + "/releases";
    const auto url = request == Request::Latest ? base+"/latest" : base+"?per_page=20&page="+std::to_string(requestedPage);
    if (!prepareRequest(url)) { r.error = "Could not start network request."; return; }
    fetching = true;
    curl_easy_setopt(easy,CURLOPT_TIMEOUT,45L);
    curl_easy_setopt(easy,CURLOPT_WRITEFUNCTION,receiveBody);
    curl_easy_setopt(easy,CURLOPT_HEADERFUNCTION,receiveHeader);
    if (curl_multi_add_handle(multi,easy) != CURLM_OK) { closeRequest(); r.error = "Could not start network request."; }
}
void finishQueue() {
    bool allDone = !jobs.empty() && std::all_of(jobs.begin(),jobs.end(),[](const Job& j) { return j.state == JobState::Done; });
    if (allDone) { repos[repoIndex].savedVersion = repos[repoIndex].version; saveConfig(); }
    appletSetMediaPlaybackState(false); offerExtraction();
}
void nextJob() {
    closeRequest();
    for (++jobIndex; jobIndex < static_cast<int>(jobs.size()); ++jobIndex) {
        auto& j = jobs[jobIndex];
        if (j.state != JobState::Waiting) continue;
        request = Request::Download;
        j.state = JobState::Active; j.error.clear();
        downloaded = total = speed = visualProgress = sampleBytes = 0; sampleTime = SDL_GetTicks();
        // Unique staging files protect existing downloads until the replacement is complete.
        std::string pattern = path + ".gitdler-XXXXXX";
        std::vector<char> name(pattern.begin(),pattern.end()); name.push_back(0);
        int fd = mkstemp(name.data());
        if (fd < 0) { j.state = JobState::Failed; j.error = "Cannot create file. Check SD card space / permissions."; continue; }
        temporary = name.data(); output = fdopen(fd,"wb");
        if (!output) { close(fd); closeRequest(); j.state = JobState::Failed; j.error = "Cannot open staging file."; continue; }
        if (!prepareRequest(j.asset.url)) { closeRequest(); j.state = JobState::Failed; j.error = "Network initialization failed."; continue; }
        curl_easy_setopt(easy,CURLOPT_WRITEFUNCTION,receiveFile);
        curl_easy_setopt(easy,CURLOPT_NOPROGRESS,0L); curl_easy_setopt(easy,CURLOPT_XFERINFOFUNCTION,transferProgress);
        if (curl_multi_add_handle(multi,easy) == CURLM_OK) return;
        closeRequest(); j.state = JobState::Failed; j.error = "Cannot start download.";
    }
    finishQueue();
}
void pumpNetwork() {
    // Keep the queue stationary while a cancellation decision is pending.
    if (!easy || screen == Screen::Cancel) return;
    int active = 0; CURLMcode state = curl_multi_perform(multi,&active);
    if (state != CURLM_OK) {
        if (fetching) { repos[repoIndex].error = "Network error. Press + to retry."; closeRequest(); }
        else { jobs[jobIndex].state = JobState::Failed; jobs[jobIndex].error = curl_multi_strerror(state); nextJob(); }
        return;
    }
    if (!fetching && !paused && SDL_GetTicks() - sampleTime >= 500) {
        const auto elapsed = SDL_GetTicks() - sampleTime;
        speed = std::max(0.0,downloaded - sampleBytes) * 1000.0 / elapsed;
        sampleBytes = downloaded; sampleTime = SDL_GetTicks();
    }
    int count;
    while (CURLMsg* msg = curl_multi_info_read(multi,&count)) {
        if (msg->msg != CURLMSG_DONE) continue;
        CURLcode result = msg->data.result;
        long status = 0; curl_easy_getinfo(easy,CURLINFO_RESPONSE_CODE,&status);
        if (fetching) {
            auto& r = repos[repoIndex];
            if (request == Request::Latest) {
                r.latestId = 0; r.latestVersion.clear(); r.latestChecked = true;
                if (result == CURLE_OK) parseLatest(response,r.latestId,r.latestVersion);
                closeRequest(); response.clear();
                // A repo with only prereleases has no /latest; still list its history.
                fetchReleases(requestedPage,false);
            } else {
                std::vector<Release> page;
                if (result == CURLE_OK && parseReleasePage(response,page,r.error)) {
                    r.releases = std::move(page); r.releasePage = requestedPage;
                    r.hasNextPage = responseHasNext;
                    r.activeReleaseId = 0; r.assets.clear(); r.version.clear();
                    cursor = scroll = releaseIndex = 0;
                    screenSince = SDL_GetTicks();
                } else if (status == 403 || status == 429) r.error = "GitHub rate limit / access denied. Try again later.";
                else if (status == 404) r.error = "Repository not found or not public.";
                else if (result != CURLE_OK) r.error = std::string("Could not load releases: ") + curl_easy_strerror(result);
                closeRequest(); response.clear();
            }
        } else {
            auto& j = jobs[jobIndex]; bool ok = result == CURLE_OK;
            if (output) { if (fclose(output) != 0) ok = false; output = nullptr; }
            if (ok && replaceFile(temporary,j.target,j.error)) { temporary.clear(); j.state = JobState::Done; }
            else {
                j.state = JobState::Failed;
                if (j.error.empty()) j.error = result == CURLE_OK ? "Cannot save file. Check SD space / destination." : curl_easy_strerror(result);
            }
            nextJob();
        }
        break; // nextJob may have added a new handle.
    }
}
void cancelQueue() {
    closeRequest();
    for (auto& j : jobs) if (j.state == JobState::Active || j.state == JobState::Waiting) j.state = JobState::Cancelled;
    appletSetMediaPlaybackState(false); changeScreen(Screen::Results);
}
void buildQueue() {
    updateFreeSpace();
    jobs.clear();
    for (const auto& a : repos[repoIndex].assets) if (a.selected) jobs.push_back(Job{a,path+a.name,"",JobState::Waiting,exists(path+a.name)});
    changeScreen(Screen::Review);
}
void startQueue(bool skipExisting) {
    updateFreeSpace();
    double needed = 0;
    for (const auto& j : jobs) if (!skipExisting || !exists(j.target)) needed += j.asset.size;
    if (freeSpace >= 0 && needed > freeSpace) { notify("Not enough SD space for the known file sizes. Choose another destination."); return; }
    for (auto& j : jobs) { j.state = skipExisting && exists(j.target) ? JobState::Skipped : JobState::Waiting; j.error.clear(); j.extractionOffered = false; j.extractionResult.clear(); }
    rememberPath(); jobIndex = -1; changeScreen(Screen::Transfer);
    appletSetMediaPlaybackState(true); nextJob();
}

void togglePause() {
    if (!easy || fetching) return;
    const bool wasPaused = paused;
    const auto result = curl_easy_pause(easy,wasPaused ? CURLPAUSE_CONT : CURLPAUSE_ALL);
    if (result == CURLE_OK) {
        paused = !wasPaused; sampleTime = SDL_GetTicks(); sampleBytes = downloaded; speed = 0;
    } else notify("Could not change pause state.");
}
void retryFailed() {
    bool retry = false;
    for (auto& j : jobs) if (j.state == JobState::Failed || j.state == JobState::Cancelled) { j.state = JobState::Waiting; retry = true; }
    if (!retry) { notify("No unfinished downloads to retry."); return; }
    jobIndex = -1; changeScreen(Screen::Transfer); appletSetMediaPlaybackState(true); nextJob();
}
}
