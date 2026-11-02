#pragma once
#include <cstdio>
#include <string>
#include <sys/stat.h>

namespace gitdler {
inline bool pathExists(const std::string& path) { struct stat s; return stat(path.c_str(), &s) == 0; }
// Horizon does not replace existing files with rename. Keep a recoverable backup
// until the new file is in place; never delete the original to make room first.
inline bool replaceFile(const std::string& staged, const std::string& target, std::string& error) {
    const std::string backup = target + ".gitdler-backup";
    if (pathExists(backup)) { error = "Recover the existing .gitdler-backup file first."; return false; }
    const bool old = pathExists(target);
    if (old) {
        struct stat s;
        if (stat(target.c_str(), &s) != 0 || !S_ISREG(s.st_mode)) { error = "Destination is not a regular file."; return false; }
        if (rename(target.c_str(), backup.c_str()) != 0) { error = "Cannot back up the existing file."; return false; }
    }
    if (rename(staged.c_str(), target.c_str()) != 0) {
        error = "Cannot save the downloaded file.";
        if (old && rename(backup.c_str(), target.c_str()) != 0) error = "Restore the original from .gitdler-backup.";
        return false;
    }
    if (old && remove(backup.c_str()) != 0) error = "Saved; old copy remains in .gitdler-backup.";
    return true;
}
}
