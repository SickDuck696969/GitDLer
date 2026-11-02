#include "app.hpp"
#include <cassert>
#include <iostream>
using namespace gitdler;
using namespace gitdler::app;

// Exercise the production input/state modules with network, storage, and platform
// services replaced by deterministic doubles. No SD files or network are touched.
namespace gitdler::app {
bool fetching = false;
bool choosingExtraction = false, extractionPaused = false;
int extractionChoices = 0, extractionCancels = 0;
int requested = 0, saved = 0, cancellations = 0;
bool directory(const std::string&) { return true; }
void saveConfig() { ++saved; }
void savePreferences() { ++saved; }
void sortFavorites() {}
std::string keyboard(const std::string&,const std::string&) { return ""; }
void fetchReleases(int page, bool) { requested = page; }
void closeRequest() { fetching = false; }
void prepareNotes() {}
void scrollNotes(u64) {}
void loadFolders() { cursor = scroll = 0; folders = {"..","one","two"}; }
void buildQueue() { changeScreen(Screen::Review); }
void startQueue(bool) { changeScreen(Screen::Transfer); }
void cancelQueue() { ++cancellations; changeScreen(Screen::Results); }
void togglePause() { paused = !paused; }
void retryFailed() { changeScreen(Screen::Transfer); }
void offerExtraction(int) { changeScreen(Screen::ExtractPrompt); }
void skipExtraction(bool) { changeScreen(Screen::Results); }
void chooseExtractionDestination() { choosingExtraction = true; changeScreen(Screen::Destination); loadFolders(); }
void leaveExtractionDestination() { choosingExtraction = false; changeScreen(Screen::ExtractPrompt); }
void startExtraction(bool) { ++extractionChoices; choosingExtraction = false; changeScreen(Screen::Extracting); }
void pauseExtraction(bool pause) { extractionPaused = pause; }
void cancelExtraction() { ++extractionCancels; }
}
int main() {
    Repo repo; repo.url = "https://github.com/owner/repo";
    repo.name = "repo"; repo.releasePage = 1; repo.hasNextPage = true; repo.lastPath = "sdmc:/switch/repo/";
    for (int i = 0; i < 8; ++i) {
        Release r; r.id = i+1; r.tag = "v"+std::to_string(i+1);
        r.assets = {{"one.nro","https://host/one",1,false},{"two.zip","https://host/two",2,false}};
        repo.releases.push_back(r);
    }
    repos.push_back(repo);
    handleInput(HidNpadButton_A); assert(screen == Screen::Releases);
    handleInput(HidNpadButton_R); assert(requested == 2);
    handleInput(HidNpadButton_ZR); handleInput(HidNpadButton_Down);
    assert(cursor == 7 && scroll == 2);
    handleInput(HidNpadButton_A);
    assert(screen == Screen::Notes && releaseIndex == 7 && repos[0].version == "v8");
    handleInput(HidNpadButton_A); assert(screen == Screen::Assets);
    handleInput(HidNpadButton_X); assert(screen == Screen::Assets); // no selection: cannot advance
    handleInput(HidNpadButton_Y); assert(selectedCount() == 2);
    handleInput(HidNpadButton_A); assert(selectedCount() == 1);
    handleInput(HidNpadButton_X); assert(screen == Screen::Destination && path == repo.lastPath);
    handleInput(HidNpadButton_Down); handleInput(HidNpadButton_Down); assert(cursor == 2);
    handleInput(HidNpadButton_X); assert(screen == Screen::Review);
    handleInput(HidNpadButton_B); assert(screen == Screen::Destination && cursor == 2);
    handleInput(HidNpadButton_B); assert(screen == Screen::Assets && selectedCount() == 1);
    handleInput(HidNpadButton_R); assert(screen == Screen::Notes);
    handleInput(HidNpadButton_B); assert(screen == Screen::Assets && selectedCount() == 1);
    handleInput(HidNpadButton_L); assert(repos[0].assets[0].selected && !repos[0].assets[1].selected);
    handleInput(HidNpadButton_B); assert(screen == Screen::Releases && cursor == 7 && scroll == 2);
    handleInput(HidNpadButton_X); assert(screen == Screen::Assets && selectedCount() == 1); // same release keeps choices
    handleInput(HidNpadButton_B); handleInput(HidNpadButton_Up); handleInput(HidNpadButton_X);
    assert(repos[0].version == "v7" && selectedCount() == 0); // different release gets its own assets
    changeScreen(Screen::Transfer); paused = false;
    handleInput(HidNpadButton_B); assert(screen == Screen::Cancel && paused && cancellations == 0);
    handleInput(HidNpadButton_B); assert(screen == Screen::Transfer && !paused);
    handleInput(HidNpadButton_A); assert(paused);
    handleInput(HidNpadButton_B); handleInput(HidNpadButton_B); assert(paused); // existing pause survives modal
    handleInput(HidNpadButton_B); handleInput(HidNpadButton_A);
    assert(screen == Screen::Results && cancellations == 1);
    changeScreen(Screen::Library); handleInput(HidNpadButton_L); assert(repos[0].favorite && saved > 0);
    handleInput(HidNpadButton_X); assert(screen == Screen::Options);
    cursor = 2; handleInput(HidNpadButton_A); assert(reducedMotion);
    cursor = 3; handleInput(HidNpadButton_A); assert(largeNotes);
    changeScreen(Screen::ExtractPrompt); handleInput(HidNpadButton_A);
    assert(choosingExtraction && screen == Screen::Destination);
    handleInput(HidNpadButton_X); assert(screen == Screen::ExtractReview);
    assert(!deleteOriginalAfterExtraction);
    int preferencesBefore = saved;
    handleInput(HidNpadButton_A); assert(deleteOriginalAfterExtraction && saved > preferencesBefore);
    assert(screen == Screen::ExtractReview && extractionChoices == 0);
    handleInput(HidNpadButton_A); assert(!deleteOriginalAfterExtraction);
    handleInput(HidNpadButton_B); assert(screen == Screen::Destination && choosingExtraction);
    handleInput(HidNpadButton_B); assert(screen == Screen::ExtractPrompt && !choosingExtraction);
    handleInput(HidNpadButton_A); handleInput(HidNpadButton_X); handleInput(HidNpadButton_Y);
    assert(screen == Screen::Extracting && extractionChoices == 1 && !choosingExtraction);
    handleInput(HidNpadButton_A); assert(extractionPaused);
    handleInput(HidNpadButton_B); assert(screen == Screen::ExtractCancel);
    handleInput(HidNpadButton_B); assert(screen == Screen::Extracting && extractionPaused);
    handleInput(HidNpadButton_B); handleInput(HidNpadButton_A); assert(extractionCancels == 1);
    std::cout << "PASS: release navigation, preserved selection, back positions, remembered paths, .nro selection, cancel confirmation, preferences (mocked services)\n";
}
