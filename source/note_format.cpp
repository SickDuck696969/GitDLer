#include "note_format.hpp"
#include <sstream>
namespace gitdler {
std::vector<NoteLine> wrapNotes(const std::string& markdown, int width,
    const std::function<int(const std::string&)>& measure) {
    std::vector<NoteLine> result;
    std::istringstream stream(markdown.empty() ? "No release notes were provided." : markdown);
    std::string line; bool code = false;
    while (std::getline(stream,line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t tab;
        while ((tab = line.find('\t')) != std::string::npos) line.replace(tab,1,"    ");
        if (line.compare(0,3,"```") == 0) {
            code = !code;
            result.push_back({code ? "CODE  " + line.substr(3) : "",NoteKind::Code});
            continue;
        }
        NoteKind kind = code ? NoteKind::Code : NoteKind::Body;
        if (!code && !line.empty() && line[0] == '#') {
            auto first = line.find_first_not_of('#');
            if (first != std::string::npos && line[first] == ' ') { line.erase(0,first+1); kind = NoteKind::Heading; }
        }
        if (line.empty()) { result.push_back({"",kind}); continue; }
        while (!line.empty()) {
            size_t fit = 0, boundary = 0, space = std::string::npos;
            while (boundary < line.size()) {
                size_t end = boundary + 1;
                while (end < line.size() && (static_cast<unsigned char>(line[end]) & 0xc0) == 0x80) ++end;
                if (fit && measure(line.substr(0,end)) > width) break;
                fit = end;
                if (line[boundary] == ' ') space = boundary;
                boundary = end;
            }
            size_t cut = fit;
            if (fit < line.size() && space != std::string::npos && space > 0) cut = space;
            result.push_back({line.substr(0,cut),kind});
            size_t consume = cut;
            if (consume < line.size() && line[consume] == ' ') ++consume;
            line.erase(0,consume);
        }
    }
    return result;
}
}
