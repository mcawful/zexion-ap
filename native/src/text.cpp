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
    // The game sized the box for the blank line too (0x182A90); half-height 4 a line plus 4,
    // centered 4 up, shows every real line (docs/re/memory.md, "Game functions the helper
    // calls"). Only a box still as the game set it is changed.
    if (message == nullptr || per_line <= 0) return;
    const size_t w = static_cast<size_t>(per_line);
    if (text.size() % w != 1 || text.size() / w < 3) return;
    const int lines = static_cast<int>(text.size() / w);
    const auto y = field<double>(message, kMessageY);
    if (field<int32_t>(message, kMessageHalfHeight) != 4 * lines + 7 || !(y == kPopupY - 1)) return;
    put<int32_t>(message, kMessageHalfHeight, 4 * lines + 4);
    put(message, kMessageY, y - 4);
}

void show_text_when_open(BYTE* message) {
    // The window (0xA8210) tests glyph row r at 10r and cuts a glyph's top at the window's top
    // edge mod 8, leaving out the row's own offset, so a row past the first cut there shows the
    // wrong part; past three rows it comes back whole when the edge reaches a multiple of 8, so
    // lines flash while the box opens and closes (docs/re/memory.md, "Game functions the helper
    // calls"). The message's update moves the window's half-width by its step each frame the box
    // opens or closes, and leaves it while the box is open. Started at 4w - step x half-height,
    // it holds no glyph until the opening's last frame, all of them whole from then on, and none
    // from the closing's first; the half-height is started so that it then holds every row whole.
    // The text then shows for the frames left less twice the half-height, plus one.
    int32_t glyphs = 0, w = 0, half = 0, step_x = 0, step_y = 0, wx = -1, wy = -1, frames = 0;
    double bottom = -1;
    if (message == nullptr || !read(message, kMessageGlyphs, glyphs) ||
        !read(message, kMessageLineLength, w) || !read(message, kMessageHalfHeight, half) ||
        !read(message, kTextWindowStepX, step_x) || !read(message, kTextWindowStepY, step_y) ||
        !read(message, kTextWindowX, wx) || !read(message, kTextWindowY, wy) ||
        !read(message, kMessageBoxBottom, bottom) || !read(message, kMessageFramesLeft, frames)) {
        return;
    }
    // A line's half-width in pixels, 8 a character: what the window must reach to hold a row.
    const int32_t reach = 4 * w;
    // As the message routine left it (0x182A4E to 0x182C89): steps of 100 and 1, the window
    // empty and the box not yet opened; within the game's widths and the slot's 256 glyphs; and
    // up long enough for the box to open fully before it closes, else the text would never show.
    if (w <= 0 || w > kGamePopupWidth || glyphs <= 0 ||
        glyphs > static_cast<int32_t>(kMessageMax) + 1 || half <= 0 ||
        half > 4 * static_cast<int32_t>(kMessageMax) || step_x != 100 || step_y != 1 || wx != 0 ||
        wy != 0 || !(bottom == 0.0) || reach > step_x - 4 || frames <= 2 * half) {
        return;
    }
    const int32_t rows = (glyphs - 1) / w + 1;
    if (rows < kMiscutRows || !writable(message + kTextWindowX, 8)) return;
    // Half-width after the opening's half-height of steps: reach (every column), and reach - step
    // (at most -4: no column) at the closing's first.
    put<int32_t>(message, kTextWindowX, reach - step_x * half);
    // Half-height after the opening: 6 a row or more, so the window, centered on the rows' 8
    // pixels each, takes in every row's 10 whole.
    put<int32_t>(message, kTextWindowY, (std::max)(0, 6 * rows - step_y * half));
}

std::string in_capitals(std::string text) {
    for (char& c : text) c = capital(c);
    return text;
}

std::string Label::first_text() const { return first.empty() ? std::string(kApItemName) : first; }

void Label::lay_out() {
    std::vector<std::string> shown{static_cast<char>(glyph) + in_capitals(first_text())};
    if (!who.empty()) shown.push_back(in_capitals(who));
    size_t widest = 0;
    for (const std::string& part : shown) widest = (std::max)(widest, part.size());
    width = (std::min)(static_cast<int>(widest), kGamePopupWidth);
    name = center_lines(shown, width);
    if (name.size() > kItemNameMax) name.resize(kItemNameMax);
    while (!name.empty() && name.back() == ' ') name.pop_back();
}

}  // namespace zap
