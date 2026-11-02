#include "app.hpp"
#include "ui.hpp"
#include <atomic>

namespace gitdler::app {
bool choosingExtraction = false, extractionPaused = false;
int extractionJob = -1;
ExtractProgress extractionProgress;
double extractionSpeed = 0;
namespace {
Thread worker{};
Mutex progressMutex{};
bool workerActive = false;
std::atomic<bool> stop{false}, hold{false}, done{false};
ExtractProgress workerProgress;
std::string source, destination, downloadPath;
bool overwrite = false;
uint32_t sampleTime = 0;
uint64_t sampleBytes = 0;
double visual = 0;

void runWorker(void*) {
    ArchiveExtractor extractor(source,destination,overwrite);
    while (!extractor.finished()) {
        if (stop.load()) { extractor.cancel(); break; }
        if (hold.load()) { svcSleepThread(16000000); continue; }
        extractor.step();
        mutexLock(&progressMutex); workerProgress = extractor.progress(); mutexUnlock(&progressMutex);
    }
    mutexLock(&progressMutex); workerProgress = extractor.progress(); mutexUnlock(&progressMutex);
    done.store(true,std::memory_order_release);
}
void closeWorker() {
    if (!workerActive) return;
    threadWaitForExit(&worker); threadClose(&worker); workerActive = false;
}
}
void offerExtraction(int selectedJob) {
    if (workerActive) return;
    choosingExtraction = false;
    extractionJob = -1;
    if (selectedJob >= 0 && selectedJob < static_cast<int>(jobs.size())) {
        auto& job = jobs[selectedJob];
        if (job.state != JobState::Done || !detectArchive(job.target)) { notify("Select a successfully downloaded archive to extract."); return; }
        extractionJob = selectedJob;
    } else {
        for (int i = 0; i < static_cast<int>(jobs.size()); ++i) {
            auto& job = jobs[i];
            if (job.state == JobState::Done && !job.extractionOffered && detectArchive(job.target)) { extractionJob = i; break; }
        }
    }
    if (extractionJob < 0) { changeScreen(Screen::Results); return; }
    jobs[extractionJob].extractionOffered = true;
    changeScreen(Screen::ExtractPrompt);
}
void skipExtraction(bool all) {
    if (extractionJob >= 0) jobs[extractionJob].extractionResult = "Archive kept; extraction skipped";
    if (all) for (auto& job : jobs) if (job.state == JobState::Done && !job.extractionOffered) job.extractionOffered = true;
    offerExtraction();
}
void chooseExtractionDestination() {
    downloadPath = path;
    path = jobs[extractionJob].target;
    path.resize(path.find_last_of('/')+1);
    choosingExtraction = true; changeScreen(Screen::Destination); loadFolders();
}
void leaveExtractionDestination() {
    choosingExtraction = false; path = downloadPath;
    changeScreen(Screen::ExtractPrompt);
}
void startExtraction(bool replace) {
    source = jobs[extractionJob].target; destination = path; overwrite = replace;
    choosingExtraction = false; extractionPaused = false; stop = false; hold = false; done = false;
    extractionProgress = {}; workerProgress = {}; extractionSpeed = visual = 0;
    sampleBytes = 0; sampleTime = SDL_GetTicks(); mutexInit(&progressMutex);
    Result created = threadCreate(&worker,runWorker,nullptr,nullptr,256*1024,0x3B,-2);
    if (R_FAILED(created)) { choosingExtraction = true; notify("Cannot start extraction worker. Try full RAM mode."); return; }
    if (R_FAILED(threadStart(&worker))) {
        threadClose(&worker); choosingExtraction = true; notify("Could not start extraction."); return;
    }
    workerActive = true; jobs[extractionJob].extractionResult.clear();
    appletSetMediaPlaybackState(true); changeScreen(Screen::Extracting);
}
void pauseExtraction(bool pause) {
    extractionPaused = pause; hold.store(pause);
    sampleTime = SDL_GetTicks(); sampleBytes = extractionProgress.writtenBytes; extractionSpeed = 0;
}
void cancelExtraction() { stop.store(true); hold.store(false); extractionPaused = false; }
void pumpExtraction() {
    if (!workerActive) return;
    mutexLock(&progressMutex); extractionProgress = workerProgress; mutexUnlock(&progressMutex);
    if (!extractionPaused && SDL_GetTicks()-sampleTime >= 500) {
        extractionSpeed = (extractionProgress.writtenBytes-sampleBytes)*1000.0/(SDL_GetTicks()-sampleTime);
        sampleBytes = extractionProgress.writtenBytes; sampleTime = SDL_GetTicks();
    }
    if (!done.load(std::memory_order_acquire)) return;
    closeWorker();
    mutexLock(&progressMutex); extractionProgress = workerProgress; mutexUnlock(&progressMutex);
    auto& job = jobs[extractionJob];
    if (extractionProgress.phase == ExtractPhase::Done)
        job.extractionResult = "Extracted " + std::to_string(extractionProgress.saved)+" files; skipped "+std::to_string(extractionProgress.skipped);
    else if (extractionProgress.phase == ExtractPhase::Cancelled) job.extractionResult = "Extraction cancelled; completed files kept";
    else job.extractionResult = "Extraction failed: " + extractionProgress.error;
    if (extractionProgress.phase == ExtractPhase::Done && !extractionProgress.error.empty()) job.extractionResult += " / "+extractionProgress.error;
    path = downloadPath; appletSetMediaPlaybackState(false); offerExtraction();
}
void shutdownExtraction() {
    stop.store(true); hold.store(false); closeWorker(); appletSetMediaPlaybackState(false);
}
void renderExtraction() {
    if (extractionJob < 0 || extractionJob >= static_cast<int>(jobs.size())) return;
    const auto& job = jobs[extractionJob]; int w = compact ? 1184 : 810;
    if (screen == Screen::ExtractPrompt || screen == Screen::ExtractReview) {
        bool review = screen == Screen::ExtractReview;
        text(review ? "Ready to extract" : "Archive downloaded",48,114,ink,1184,32);
        card(48,168,w,438,panel);
        text(job.asset.name,80,206,ink,w-64,32);
        text(review ? "How should existing files be handled?" : "Extract this archive now?",80,282,accent,w-64,22);
        text("The downloaded archive will be kept.",80,338,muted,w-64);
        text(review ? "[X] Extract and replace existing files" : "[A] Choose extraction destination",80,408,ink,w-64);
        text(review ? "[Y] Extract and skip existing files" : "[B] Keep this archive without extracting",80,454,ink,w-64);
        text(review ? "[B] Choose a different destination" : "[Y] Skip extraction for all remaining archives",80,500,muted,w-64,18);
        sidebar(review ? "EXTRACT TO" : "OPTIONAL EXTRACTION",review ? "Your destination" : "Keep or unpack",
            {review ? path : "ZIP / 7z / RAR / TAR",review ? (freeSpace < 0 ? "Free space unknown" : bytes(freeSpace)+" free") : "GZ / BZ2 / XZ / ZST / LZ4","Folder structure is preserved","Original archives are kept"});
        if (review) text(path,48,616,accent,1184,18);
        footer(review ? "[X] Replace existing    [Y] Skip existing    [B] Back" : "[A] Choose location    [B] Keep archive    [Y] Skip all");
        return;
    }
    const auto& p = extractionProgress;
    const bool scanning = p.phase == ExtractPhase::Inspecting;
    text("Extracting your archive",48,114,ink,1184,32);
    card(48,168,w,438,panel);
    text(stop.load() ? "CANCELLING..." : extractionPaused ? "PAUSED" : scanning ? "INSPECTING ARCHIVE" : "EXTRACTING",80,196,accent,w-64,18);
    text(job.asset.name,80,236,ink,w-64,32);
    text(p.member.empty() ? "Preparing archive reader..." : p.member,80,290,muted,w-64);
    bool known = !scanning && p.sizeKnown && p.totalBytes > 0;
    double fraction = known ? std::min(1.0,static_cast<double>(p.processedBytes)/p.totalBytes) : 0;
    visual += (fraction-visual)*(reducedMotion ? 1.0 : std::min(1.0f,frameSeconds*12));
    progressBar(80,352,w-64,visual,!known && !extractionPaused);
    text(known ? std::to_string(static_cast<int>(fraction*100))+"%" : scanning ? "Checking entries..." : bytes(p.writtenBytes)+" extracted",80,388,ink,w-64,32);
    text(scanning ? std::to_string(p.files)+" files found" : std::to_string(p.saved)+" saved / "+std::to_string(p.skipped)+" skipped / "+std::to_string(p.files)+" files",80,444,muted,w-64);
    std::string detail = scanning ? "Checking paths and required space" : bytes(extractionSpeed)+"/s  /  "+bytes(p.writtenBytes)+" written";
    if (known && !extractionPaused && extractionSpeed > 1 && p.processedBytes < p.totalBytes)
        detail += "  /  ~"+std::to_string(static_cast<uint64_t>((p.totalBytes-p.processedBytes)/extractionSpeed))+"s left";
    text(detail,80,490,accent,w-64,18); text("To "+destination,80,550,muted,w-64,18);
    sidebar("EXTRACTION",scanning ? "Inspecting" : "Progress",{bytes(p.writtenBytes)+" written",p.sizeKnown ? bytes(p.totalBytes)+" total" : "Unpacked size unknown","A  Pause / resume","Original archive is kept"});
    footer("[A] Pause / resume    [B] Cancel extraction");
    if (screen == Screen::ExtractCancel) {
        rect(0,92,1280,558,SDL_Color{0,0,0,185}); card(280,225,720,270,panel);
        text("Cancel extraction?",320,260,ink,640,32);
        text("Completed files and the archive will be kept.",320,332,muted,640);
        text("[A] Cancel extraction    [B] Continue",320,419,warning,640);
    }
}
}
