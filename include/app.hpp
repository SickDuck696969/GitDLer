#pragma once
#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "model.hpp"
#include "archive_extract.hpp"

// Shared application state. Platform lifecycle lives in main.cpp; each module
// below owns its implementation and exposes only the operations used elsewhere.
namespace gitdler::app {
inline constexpr const char* CONFIG = "sdmc:/switch/GitDLer/";
enum class Screen { Library, Releases, Notes, Assets, Destination, History, Review, Transfer, Results, Delete, Options, Cancel, ExtractPrompt, ExtractReview, Extracting, ExtractCancel };
enum class JobState { Waiting, Active, Done, Failed, Skipped, Cancelled };
struct Job {
    Asset asset; std::string target, error; JobState state = JobState::Waiting; bool existed = false;
    bool extractionOffered = false;
    std::string extractionResult;
};
extern SDL_Renderer* renderer;
extern TTF_Font *smallFont, *bodyFont, *titleFont;
extern bool compact, running, paused, fetching, reducedMotion, largeNotes;
extern Screen screen, notesReturn;
extern uint32_t screenSince, toastUntil;
extern std::vector<Repo> repos;
extern std::vector<std::string> history, folders;
extern std::vector<Job> jobs;
extern int repoIndex, cursor, scroll, jobIndex, releaseIndex;
extern std::string path, toast;
extern double downloaded, total, speed, visualProgress, freeSpace;
extern float frameSeconds;

// state.cpp: transitions and selection context
void notify(const std::string& message);
void changeScreen(Screen next);
void goBack(Screen next);
void selectRelease(int index);
int selectedCount();
uint64_t selectedBytes();

// storage.cpp: configuration, directories and platform keyboard
bool exists(const std::string& path);
bool directory(const std::string& path);
void saveConfig();
void loadConfig();
void rememberPath();
void loadFolders();
void updateFreeSpace();
void savePreferences();
void sortFavorites();
std::string keyboard(const std::string& prompt, const std::string& initial = "");

// network.cpp: all CURL handles and sequential queue lifecycle
bool initializeNetwork();
void shutdownNetwork();
void closeRequest();
void fetchReleases(int page = 1, bool refreshLatest = false);
void pumpNetwork();
void buildQueue();
void startQueue(bool skipExisting);
void cancelQueue();
void togglePause();
void retryFailed();

// input.cpp / screens.cpp / notes.cpp
void moveCursor(u64 pressed, int count);
void handleInput(u64 pressed);
void render();
void prepareNotes();
void renderNotes();
void scrollNotes(u64 pressed);

// extraction.cpp: archive prompts, destination mode, worker lifecycle and progress
extern bool choosingExtraction, extractionPaused;
extern bool deleteOriginalAfterExtraction;
extern int extractionJob;
extern ExtractProgress extractionProgress;
extern double extractionSpeed;
void offerExtraction(int selectedJob = -1);
void skipExtraction(bool all);
void chooseExtractionDestination();
void leaveExtractionDestination();
void startExtraction(bool replace);
void pumpExtraction();
void pauseExtraction(bool pause);
void cancelExtraction();
void shutdownExtraction();
void renderExtraction();
}
