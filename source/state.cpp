#include "app.hpp"
#include <array>
namespace gitdler::app {
bool deleteOriginalAfterExtraction = false;
SDL_Renderer* renderer = nullptr;
TTF_Font *smallFont = nullptr, *bodyFont = nullptr, *titleFont = nullptr;
bool compact = true, running = true;
Screen screen = Screen::Library;
uint32_t screenSince = 0;
std::vector<Repo> repos;
std::vector<std::string> history, folders;
std::vector<Job> jobs;
int repoIndex = 0, cursor = 0, scroll = 0, jobIndex = -1;
std::string path = "sdmc:/", toast;
uint32_t toastUntil = 0;
bool paused = false;
double downloaded = 0, total = 0, speed = 0, visualProgress = 0;
double freeSpace = -1;
float frameSeconds = 1.0f/60.0f;
bool reducedMotion = false, largeNotes = false;
Screen notesReturn = Screen::Releases;
int releaseIndex = 0;
namespace {
struct Position { int cursor = 0, scroll = 0; };
std::array<Position,static_cast<size_t>(Screen::ExtractCancel)+1> positions;
}

void notify(const std::string& s) { toast = s; toastUntil = SDL_GetTicks() + 4500; }
void changeScreen(Screen s) {
    positions[static_cast<size_t>(screen)] = {cursor,scroll};
    screen = s; cursor = scroll = 0; screenSince = SDL_GetTicks();
}
void goBack(Screen s) {
    const auto saved = positions[static_cast<size_t>(s)];
    changeScreen(s); cursor = saved.cursor; scroll = saved.scroll;
}
void selectRelease(int index) {
    auto& repo = repos[repoIndex];
    if (index < 0 || index >= static_cast<int>(repo.releases.size())) return;
    releaseIndex = index;
    const auto& release = repo.releases[index];
    if (repo.activeReleaseId != release.id) {
        repo.activeReleaseId = release.id; repo.version = release.tag; repo.releaseTitle = release.title;
        repo.assets = release.assets;
    }
}
int selectedCount() { int n = 0; for (const auto& a : repos[repoIndex].assets) n += a.selected; return n; }
uint64_t selectedBytes() { uint64_t n = 0; for (const auto& a : repos[repoIndex].assets) if (a.selected) n += a.size; return n; }

}
