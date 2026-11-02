#include "app.hpp"
#include "ui.hpp"
#include <cmath>
#include <map>
namespace gitdler::app {
namespace { float focusY = 168; Screen lastScreen = Screen::Library; }
void beginContentAnimation() {
    const auto age = SDL_GetTicks()-screenSince;
    float t = std::min(1.0f,age/200.0f);
    float eased = 1.0f-std::pow(1.0f-t,3.0f);
    int slide = reducedMotion ? 0 : static_cast<int>(18*(1-eased));
    SDL_Rect viewport{slide,0,1280-slide,720}; SDL_RenderSetViewport(renderer,&viewport);
    const float target = 168+(cursor-scroll)*73;
    if (lastScreen != screen || reducedMotion) focusY = target;
    else focusY += (target-focusY)*std::min(1.0f,frameSeconds*22);
    lastScreen = screen;
}
void endContentAnimation() { SDL_RenderSetViewport(renderer,nullptr); }
void listPosition(int count) {
    if (count <= 6) return;
    int w = compact ? 1184 : 810;
    rect(48+w+8,168,3,431,selected);
    int thumb = std::max(20,431*6/count);
    rect(48+w+8,168+(431-thumb)*std::min(scroll,count-6)/(count-6),3,thumb,accent);
}
// Applet mode keeps a smaller bounded text cache and never loads background artwork.
struct TextTexture { SDL_Texture* texture; int w, h; uint32_t used; };
std::map<std::string, TextTexture> textCache;
void clearTextCache() { for (auto& item : textCache) SDL_DestroyTexture(item.second.texture); textCache.clear(); }
void rect(int x, int y, int w, int h, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_Rect r{x,y,w,h}; SDL_RenderFillRect(renderer, &r);
}
void card(int x, int y, int w, int h, SDL_Color color) {
    constexpr int radius = 10;
    rect(x,y+radius,w,h-radius*2,color);
    for (int i = 0; i < radius; ++i) {
        const int inset = radius-static_cast<int>(std::sqrt(radius*radius-(radius-i-0.5)*(radius-i-0.5)));
        rect(x+inset,y+i,w-inset*2,1,color);
        rect(x+inset,y+h-1-i,w-inset*2,1,color);
    }
}
void text(const std::string& value, int x, int y, SDL_Color color, int width, int fontSize) {
    if (value.empty() || width <= 0) return;
    TTF_Font* font = fontSize == 32 ? titleFont : fontSize == 18 ? smallFont : bodyFont;
    std::string label = value;
    std::replace(label.begin(), label.end(), '\n', ' '); std::replace(label.begin(), label.end(), '\r', ' ');
    int w = 0, h = 0; TTF_SizeUTF8(font, label.c_str(), &w, &h);
    if (w > width) {
        do {
            if (label.empty()) break;
            auto last = label.size() - 1;
            while (last > 0 && (static_cast<unsigned char>(label[last]) & 0xc0) == 0x80) --last;
            label.resize(last);
            TTF_SizeUTF8(font, (label + "...").c_str(), &w, &h);
        } while (w > width);
        label += "...";
    }
    std::string key = std::to_string(fontSize) + ":" + label;
    auto found = textCache.find(key);
    if (found == textCache.end()) {
        if (textCache.size() >= (compact ? 64u : 192u)) {
            auto oldest = std::min_element(textCache.begin(), textCache.end(), [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
            SDL_DestroyTexture(oldest->second.texture); textCache.erase(oldest);
        }
        SDL_Surface* surface = TTF_RenderUTF8_Blended(font, label.c_str(), SDL_Color{255,255,255,255});
        if (!surface) return;
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        TextTexture entry{texture,surface->w,surface->h,SDL_GetTicks()}; SDL_FreeSurface(surface);
        if (!texture) return;
        found = textCache.emplace(key, entry).first;
    }
    auto& t = found->second; t.used = SDL_GetTicks();
    SDL_SetTextureColorMod(t.texture, color.r, color.g, color.b);
    SDL_Rect dest{x,y,t.w,t.h}; SDL_RenderCopy(renderer, t.texture, nullptr, &dest);
}
void row(int y, const std::string& title, const std::string& subtitle, bool focused, int width, bool checked) {
    card(48,y,width,66,focused ? selected : panel);
    if (focused) {
        const auto a = static_cast<Uint8>(reducedMotion ? 255 : 190 + 65 * (0.5 + 0.5 * std::sin(SDL_GetTicks() / 260.0)));
        rect(48,static_cast<int>(focusY)+12,4,42,SDL_Color{accent.r,accent.g,accent.b,a});
    }
    text(title, checked ? 102 : 68,y+7,ink,width-(checked ? 75 : 40));
    text(subtitle,checked ? 102 : 68,y+37,muted,width-(checked ? 75 : 40),18);
    if (checked) { rect(68,y+22,20,20,accent); text("+",70,y+17,panel,20); }
}
void footer(const std::string& s) {
    SDL_Rect viewport; SDL_RenderGetViewport(renderer,&viewport); SDL_RenderSetViewport(renderer,nullptr);
    rect(0,650,1280,70,panel); text(s,48,674,muted,1184,18);
    SDL_RenderSetViewport(renderer,&viewport);
}
void releaseRow(int y,const std::string& title,const std::string& tag,const std::string& subtitle,bool focused,int width) {
    row(y,"",subtitle,focused,width);
    int measured = 0; TTF_SizeUTF8(bodyFont,tag.c_str(),&measured,nullptr);
    int tagWidth = std::min(260,std::max(80,measured));
    text(title,68,y+7,ink,width-tagWidth-72);
    text(tag,48+width-tagWidth-20,y+7,accent,tagWidth);
}
void progressBar(int x, int y, int w, double fraction, bool indeterminate) {
    rect(x,y,w,12,SDL_Color{42,56,73,255});
    if (indeterminate) {
        int span = w / 5, offset = reducedMotion ? w/2-span/2 : static_cast<int>((SDL_GetTicks() / 5) % (w + span)) - span;
        int left = std::max(0,offset), right = std::min(w,offset+span);
        if (right > left) rect(x+left,y,right-left,12,accent);
    } else rect(x,y,static_cast<int>(w * std::max(0.0,std::min(1.0,fraction))),12,accent);
}


}
