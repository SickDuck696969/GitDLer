#include "app.hpp"
#include "ui.hpp"
using namespace gitdler::app;

int main(int, char**) {
    socketInitializeDefault(); plInitialize(PlServiceType_User);
    if (!initializeNetwork()) return 1;
    const AppletType type = appletGetAppletType();
    compact = type != AppletType_Application && type != AppletType_SystemApplication;
    if (SDL_Init(SDL_INIT_VIDEO) != 0 || TTF_Init() != 0) return 1;
    SDL_Window* window = SDL_CreateWindow("GitDLer",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,720,0);
    renderer = SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!window || !renderer) return 1;
    SDL_RenderSetLogicalSize(renderer,1280,720); SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    PlFontData font;
    if (R_FAILED(plGetSharedFontByType(&font,PlSharedFontType_Standard))) return 1;
    smallFont = TTF_OpenFontRW(SDL_RWFromMem(font.address,font.size),1,18);
    bodyFont = TTF_OpenFontRW(SDL_RWFromMem(font.address,font.size),1,22);
    titleFont = TTF_OpenFontRW(SDL_RWFromMem(font.address,font.size),1,32);
    if (!smallFont || !bodyFont || !titleFont) return 1;
    PadState pad; padConfigureInput(1,HidNpadStyleSet_NpadStandard); padInitializeDefault(&pad);
    loadConfig(); uint32_t repeatAt = 0, lastFrame = SDL_GetTicks(); u64 lastDirection = 0;
    constexpr u64 directions = HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_StickLUp | HidNpadButton_StickLDown;
    while (running && appletMainLoop()) {
        auto now = SDL_GetTicks();
        frameSeconds = std::max(0.001f,std::min(0.05f,(now-lastFrame)/1000.0f)); lastFrame = now;
        SDL_Event event;
        while (SDL_PollEvent(&event)) if (event.type == SDL_QUIT) running = false;
        padUpdate(&pad); u64 pressed = padGetButtonsDown(&pad), held = padGetButtons(&pad) & directions;
        if (held != lastDirection) { lastDirection = held; repeatAt = SDL_GetTicks()+350; }
        else if (held && static_cast<int32_t>(SDL_GetTicks()-repeatAt) >= 0) { pressed |= held; repeatAt = SDL_GetTicks()+110; }
        handleInput(pressed); pumpNetwork(); pumpExtraction(); render();
    }
    shutdownExtraction(); shutdownNetwork();
    clearTextCache(); TTF_CloseFont(smallFont); TTF_CloseFont(bodyFont); TTF_CloseFont(titleFont);
    TTF_Quit(); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
    plExit(); socketExit(); return 0;
}
