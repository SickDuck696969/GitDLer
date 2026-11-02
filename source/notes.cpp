#include "app.hpp"
#include "ui.hpp"
#include "note_format.hpp"
namespace gitdler::app {
namespace {
std::vector<NoteLine> lines;
float visualLine = 0;
int visibleLines() { return largeNotes ? 13 : 16; }
}
void prepareNotes() {
    const auto& repo = repos[repoIndex];
    if (releaseIndex < 0 || releaseIndex >= static_cast<int>(repo.releases.size())) { lines.clear(); return; }
    auto font = largeNotes ? bodyFont : smallFont;
    lines = wrapNotes(repo.releases[releaseIndex].notes,compact ? 1100 : 726,[font](const std::string& s) {
        int w = 0; TTF_SizeUTF8(font,s.c_str(),&w,nullptr); return w;
    });
    cursor = std::min(cursor,std::max(0,static_cast<int>(lines.size())-visibleLines()));
    visualLine = static_cast<float>(cursor);
}
void scrollNotes(u64 pressed) {
    if (pressed & (HidNpadButton_Up | HidNpadButton_StickLUp)) --cursor;
    if (pressed & (HidNpadButton_Down | HidNpadButton_StickLDown)) ++cursor;
    if (pressed & HidNpadButton_ZL) cursor -= visibleLines()-1;
    if (pressed & HidNpadButton_ZR) cursor += visibleLines()-1;
    if (pressed & HidNpadButton_X) cursor = 0;
    cursor = std::max(0,std::min(cursor,std::max(0,static_cast<int>(lines.size())-visibleLines())));
}
void renderNotes() {
    const auto& releases = repos[repoIndex].releases;
    if (releaseIndex < 0 || releaseIndex >= static_cast<int>(releases.size())) return;
    const auto& release = releases[releaseIndex];
    int w = compact ? 1184 : 810, lineHeight = largeNotes ? 30 : 24;
    text(release.title,48,114,ink,830,32);
    text(release.tag,920,122,accent,312,22);
    card(48,168,w,438,panel);
    visualLine += (cursor-visualLine)*(reducedMotion ? 1.0f : std::min(1.0f,frameSeconds*18));
    SDL_Rect clip{68,188,w-40,390}; SDL_RenderSetClipRect(renderer,&clip);
    int first = static_cast<int>(visualLine);
    for (int i = first; i < static_cast<int>(lines.size()) && i < first+visibleLines()+2; ++i) {
        const auto& line = lines[i];
        text(line.text,80,188+static_cast<int>((i-visualLine)*lineHeight),
            line.kind == NoteKind::Heading ? accent : line.kind == NoteKind::Code ? muted : ink,w-84,largeNotes ? 22 : 18);
    }
    SDL_RenderSetClipRect(renderer,nullptr);
    int count = std::max(1,static_cast<int>(lines.size()));
    if (count > visibleLines()) {
        rect(48+w-14,188,3,390,selected);
        int thumb = std::max(18,390*visibleLines()/count);
        rect(48+w-14,188+static_cast<int>((390-thumb)*visualLine/(count-visibleLines())),3,thumb,accent);
    }
    sidebar(release.id == repos[repoIndex].latestId ? "LATEST RELEASE" : release.prerelease ? "PRERELEASE" : "RELEASE",release.tag,
        {release.published.substr(0,10),release.author.empty() ? "Author unavailable" : "By " + release.author,std::to_string(release.assets.size())+" downloadable files","A  Choose files"});
    const auto position = std::to_string(cursor+1)+"-"+std::to_string(std::min(count,cursor+visibleLines()))+" of "+std::to_string(count)+" lines";
    if (compact) {
        text(release.tag+"  /  "+release.published.substr(0,10)+(release.author.empty() ? "" : "  /  By "+release.author),48,616,muted,820,18);
        text(position,900,616,accent,332,18);
    } else text("Notes  /  "+position,48,616,muted,1184,18);
    footer("[A] Choose files    [B] Back    [Y] Text size    [X] Top    [ZL / ZR] Page    [Up / Down] Scroll");
}
}
