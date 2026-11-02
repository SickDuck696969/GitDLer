#pragma once
// Host-only platform shim for navigation tests. Never included by the Switch build.
#include <cstdint>
#ifdef _WIN32
#include <direct.h>
inline int mkdir(const char* path, int) { return _mkdir(path); }
#endif
using u64 = uint64_t;
enum : u64 {
    HidNpadButton_A=1ULL<<0, HidNpadButton_B=1ULL<<1,
    HidNpadButton_X=1ULL<<2, HidNpadButton_Y=1ULL<<3,
    HidNpadButton_L=1ULL<<4, HidNpadButton_R=1ULL<<5,
    HidNpadButton_ZL=1ULL<<6, HidNpadButton_ZR=1ULL<<7,
    HidNpadButton_Plus=1ULL<<8, HidNpadButton_Minus=1ULL<<9,
    HidNpadButton_Up=1ULL<<10, HidNpadButton_Down=1ULL<<11,
    HidNpadButton_StickLUp=1ULL<<12, HidNpadButton_StickLDown=1ULL<<13
};
