#include "text.h"

#include <algorithm>
#include <sstream>

#include "apitem.h"  // kItemNameMax
#include "game_mem.h"

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
    // The game shows only part of a text past two or three lines; one blank line more makes it
    // show up to 7 (docs/re/memory.md, "Game functions the helper calls"). Lines past the
    // 256-byte slot, the NUL and that space included, are left out.
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
    // The game sized the box for the blank line too (half-height 4 per line past the first, plus
    // 7: 0x182A90). The half-height for the real lines plus one, the center 4 up, leaves about 4
    // pixels above and below, the least that shows every line (docs/re/memory.md, "Game
    // functions the helper calls"). Only a box still as the game set it is changed.
    if (message == nullptr || per_line <= 0) return;
    const size_t w = static_cast<size_t>(per_line);
    if (text.size() % w != 1 || text.size() / w < 3) return;
    const int lines = static_cast<int>(text.size() / w);
    const auto y = field<double>(message, kMessageY);
    if (field<int32_t>(message, kMessageHalfHeight) != 4 * lines + 7 || !(y == kPopupY - 1)) return;
    put<int32_t>(message, kMessageHalfHeight, 4 * lines + 4);
    put(message, kMessageY, y - 4);
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
