#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace gitdler {
enum class ExtractPhase { Inspecting, Extracting, Done, Failed, Cancelled };
struct ExtractProgress {
    ExtractPhase phase = ExtractPhase::Inspecting;
    std::string member, error;
    uint64_t totalBytes = 0, processedBytes = 0, writtenBytes = 0;
    uint64_t currentBytes = 0, currentTotal = 0, files = 0, saved = 0, skipped = 0;
    bool sizeKnown = true;
};
enum class ArchiveCleanup { Kept, Deleted, Failed };
// Invoke only after the extractor has closed the source archive.
ArchiveCleanup finishArchive(const std::string& path, const ExtractProgress& progress,
                             bool deleteOriginal, bool cancelled);
bool archiveName(const std::string& name);
bool detectArchive(const std::string& path);
bool normalizeArchivePath(const std::string& input, std::string& relative);

// Portable streaming engine. The Switch controller runs step() on a worker;
// tests can call it directly. No UI, platform services, or networking live here.
class ArchiveExtractor {
public:
    ArchiveExtractor(const std::string& source, const std::string& destination, bool replace);
    ~ArchiveExtractor();
    ArchiveExtractor(const ArchiveExtractor&) = delete;
    ArchiveExtractor& operator=(const ArchiveExtractor&) = delete;
    void step();
    void cancel();
    bool finished() const;
    const ExtractProgress& progress() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
