#pragma once
#include <functional>
#include <string>
#include <vector>
namespace gitdler {
enum class NoteKind { Body, Heading, Code };
struct NoteLine { std::string text; NoteKind kind = NoteKind::Body; };
// Width is measured by the caller so the same UTF-8 wrapping is testable on a
// desktop and uses real font metrics on Switch. All text stays locally rendered.
std::vector<NoteLine> wrapNotes(const std::string& markdown, int width,
    const std::function<int(const std::string&)>& measure);
}
