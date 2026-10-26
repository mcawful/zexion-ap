#include "text.h"

#include <algorithm>
#include <cstring>
#include <sstream>

#include "apitem.h"  // kItemNameMax

namespace zap {

std::vector<std::string> wrap_words(const std::string& text, size_t width) {
    std::vector<std::string> lines;
    std::string line;
    std::istringstream words(text);
    std::string word;
    while (words >> word) {
        while (word.size() > width) {  // longer than a line: cut it
            if (!line.empty()) lines.push_back(line);
            line.clear();
            lines.push_back(word.substr(0, width));
            word.erase(0, width);
        }
        if (word.empty()) continue;
        if (!line.empty() && line.size() + 1 + word.size() > width) {
            lines.push_back(line);
            line.clear();
        }
        if (!line.empty()) line += ' ';
        line += word;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

std::string center_lines(const std::vector<std::string>& lines, int per_line) {
    // The message routine lays text out per_line characters a line and cuts wherever a line
    // ends, with no line-break code, and sizes a one-line box to its text (docs/re/memory.md,
    // "Other players' items"). So with more than one line, each is centered by spaces before it
    // and padded with spaces to the end of its line; one line stays as it is.
    if (lines.size() == 1 || per_line <= 0) return lines.empty() ? std::string() : lines[0];
    const size_t width = static_cast<size_t>(per_line);
    std::string text;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line.size() < width) text.append((width - line.size()) / 2, ' ');
        text += line;
        if (i + 1 < lines.size() && text.size() % width != 0) {
            text.append(width - text.size() % width, ' ');
        }
    }
    return text;
}

std::string popup_text(const std::string& head, const std::string& suffix, int per_line,
                       int* width) {
    std::vector<std::string> lines{head};
    if (per_line > 0) {
        for (std::string& line : wrap_words(suffix, static_cast<size_t>(per_line))) {
            lines.push_back(std::move(line));
        }
    }
    size_t widest = 0;
    for (const std::string& line : lines) widest = (std::max)(widest, line.size());
    int w = (std::min)(static_cast<int>(widest), per_line);
    if (w <= 0) w = per_line;
    if (width != nullptr) *width = w;
    return center_lines(lines, w);
}

std::vector<std::string> wrap_hyphenated(const std::string& text, size_t width) {
    std::vector<std::string> lines;
    std::string line;
    std::istringstream words(text);
    std::string word;
    while (words >> word) {
        // A word that fits a line starts a new one if it does not fit this one; a longer one
        // starts where it is and is split, a hyphen ending each line it fills.
        if (word.size() <= width && !line.empty() && line.size() + 1 + word.size() > width) {
            lines.push_back(line);
            line.clear();
        }
        while (word.size() > width && width > 1) {
            std::string head = line.empty() ? std::string() : line + ' ';
            if (head.size() + 2 > width) {  // no room for a letter and the hyphen: a new line
                lines.push_back(line);
                line.clear();
                continue;
            }
            const size_t room = width - head.size();
            lines.push_back(head + word.substr(0, room - 1) + '-');
            line.clear();
            word.erase(0, room - 1);
        }
        if (!line.empty()) line += ' ';
        line += word;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

std::string notice_text(const std::string& text, int per_line, int* width) {
    if (per_line <= 0) {
        if (width != nullptr) *width = per_line;
        return text;
    }
    std::vector<std::string> lines = wrap_hyphenated(text, static_cast<size_t>(per_line));
    size_t widest = 0;
    for (const std::string& line : lines) widest = (std::max)(widest, line.size());
    // Never wider than asked: the game is only ever given the widths it uses itself.
    const size_t w = widest == 0 ? static_cast<size_t>(per_line)
                                 : (std::min)(widest, static_cast<size_t>(per_line));
    if (width != nullptr) *width = static_cast<int>(w);
    // The game shows a popup of n lines through a window from y = 88 - 4n to 88 + 3n while its
    // text runs to 88 + 4n (measured, docs/re/memory.md, "Game functions the helper calls"), so
    // from 3 lines on the last line is cut. One blank line more (a space past a full last line)
    // makes the window reach far enough for up to 7 lines of text. Lines past what the game's
    // 256-byte message slot holds, the NUL and that space included, are left out.
    const bool blank_line = lines.size() >= 3;
    size_t fit = (kMessageMax - (blank_line ? 1 : 0)) / w;
    if (lines.size() > fit) lines.resize(fit);
    // Left-aligned: each line but the last padded with spaces to the box's width, where the
    // game starts the next line (it cuts every `w` characters, with no line-break code).
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        if (i + 1 < lines.size() || blank_line) out.append(w - lines[i].size(), ' ');
    }
    if (blank_line) out += ' ';
    return out;
}

void fit_notice_box(BYTE* message, const std::string& text, int per_line) {
    // A notice ending in its blank line (notice_text): the game sized its box for that line too,
    // a line's worth of empty box under the text. The box is the message's own: centred on its
    // y (+0x48), opening to a half-height (+0x1FC) of 4 per line past the first plus 7, set from
    // the text's length (0x182A90). Given the half-height for its real lines plus one, and its
    // centre moved 4 up onto them, it fits them with about 4 pixels above and below, the least
    // that still shows every line (docs/re/memory.md, "Game functions the helper calls"). Only a
    // message holding what the game wrote for that text is changed.
    if (message == nullptr || per_line <= 0) return;
    const size_t w = static_cast<size_t>(per_line);
    if (text.size() % w != 1 || text.size() / w < 3) return;
    const int lines = static_cast<int>(text.size() / w);
    int32_t half = 0;
    double y = 0;
    std::memcpy(&half, message + kMessageHalfHeight, sizeof(half));
    std::memcpy(&y, message + kMessageY, sizeof(y));
    if (half != 4 * lines + 7 || !(y == kPopupY - 1)) return;
    half = 4 * lines + 4;
    y -= 4;
    std::memcpy(message + kMessageHalfHeight, &half, sizeof(half));
    std::memcpy(message + kMessageY, &y, sizeof(y));
}

Label layout_label(const std::string& first, const std::string& who) {
    std::vector<std::string> shown{first};
    if (!who.empty()) shown.push_back(who);
    size_t widest = 0;
    for (const std::string& part : shown) widest = (std::max)(widest, part.size());
    Label label;
    label.width = (std::min)(static_cast<int>(widest), kGamePopupWidth);
    label.name = center_lines(shown, label.width);
    if (label.name.size() > kItemNameMax) label.name.resize(kItemNameMax);
    while (!label.name.empty() && label.name.back() == ' ') label.name.pop_back();
    return label;
}

}  // namespace zap
