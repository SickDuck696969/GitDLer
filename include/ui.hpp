#pragma once
#include "app.hpp"
namespace gitdler::app {
inline constexpr SDL_Color ink{234,240,249,255}, muted{143,161,185,255}, accent{91,226,195,255},
    panel{23,33,49,255}, selected{35,61,76,255}, warning{255,195,112,255};
void clearTextCache();
void rect(int x, int y, int w, int h, SDL_Color color);
void card(int x, int y, int w, int h, SDL_Color color);
void text(const std::string& value, int x, int y, SDL_Color color = ink, int width = 1184, int fontSize = 22);
void row(int y, const std::string& title, const std::string& subtitle, bool focused, int width, bool checked = false);
void releaseRow(int y, const std::string& title, const std::string& tag, const std::string& subtitle, bool focused, int width);
void footer(const std::string& value);
void progressBar(int x, int y, int w, double fraction, bool indeterminate = false);
void sidebar(const std::string& heading, const std::string& lead, const std::vector<std::string>& lines);
void beginContentAnimation();
void endContentAnimation();
void listPosition(int count);
}
