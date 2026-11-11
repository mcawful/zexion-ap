#include "requests.h"

#include <cctype>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "game_layout.h"  // kMapSide, record::kCount
#include "scan.h"         // scan_line_ok
#include "trap.h"         // trap::is_trap

namespace zap {

namespace {

constexpr int kItems = 50;  // inventory bytes the helper may write: 0..49
constexpr size_t kSuffixMax = 40;
// A notice's text as sent, at most; notice_text cuts it further to the game's message slot.
constexpr size_t kNoticeMax = 200;

bool parse_number(const std::string& text, uint64_t& out) {
    // A digit first: strtoull also takes leading spaces and a sign, and reads "-1" as 2^64 - 1.
    if (text.empty() || !std::isdigit(static_cast<unsigned char>(text[0]))) return false;
    char* end = nullptr;
    out = std::strtoull(text.c_str(), &end, text.rfind("0x", 0) == 0 ? 16 : 10);
    return end != nullptr && *end == '\0';
}

// Adds `c` to `out` if the game's font draws it: a letter (in either case), a digit, or `kept`.
void keep_font_char(std::string& out, unsigned char c, const char* kept) {
    if (std::isalnum(c) || (c != 0 && std::strchr(kept, c) != nullptr)) out += static_cast<char>(c);
}

// The first line `wrap_words` makes of `text`, or "".
std::string first_wrapped(const std::string& text, size_t width) {
    std::vector<std::string> wrapped = wrap_words(text, width);
    return wrapped.empty() ? std::string() : wrapped.front();
}

}  // namespace

bool parse_hex(const std::string& text, std::vector<uint8_t>& out) {
    if (text.empty() || text.size() % 2 != 0) return false;
    for (size_t i = 0; i < text.size(); i += 2) {
        // Both characters a hex digit: for an unsigned type from_chars takes no sign, no 0x and
        // no space.
        uint8_t value = 0;
        const char* end = text.data() + i + 2;
        auto [stop, error] = std::from_chars(text.data() + i, end, value, 16);
        if (error != std::errc() || stop != end) return false;
        out.push_back(value);
    }
    return true;
}

bool parse_number_list(const std::string& text, size_t max_digits, int lo, int hi,
                       std::vector<int>& out) {
    std::istringstream items(text);
    std::string item;
    while (std::getline(items, item, ',')) {
        if (item.empty() || item.size() > max_digits ||
            item.find_first_not_of("0123456789") != std::string::npos) {
            return false;
        }
        int n = std::stoi(item);
        if (n < lo || n > hi) return false;
        out.push_back(n);
    }
    return true;
}

std::string parse_config(const std::string& request, std::map<std::string, uint64_t>& out) {
    std::istringstream words(request);
    std::string word;
    words >> word;
    if (word != "config") return "not a config request";
    while (words >> word) {
        size_t eq = word.find('=');
        uint64_t rva = 0;
        if (eq == std::string::npos || !parse_number(word.substr(eq + 1), rva)) {
            return "bad entry " + word;
        }
        out[word.substr(0, eq)] = rva;
    }
    return "";
}

std::string parse_batch(const std::string& request, Batch& out) {
    std::istringstream lines(request);
    std::string line;
    if (!std::getline(lines, line) || line != "apply") return "not an apply request";
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string op;
        words >> op;
        if (op == "cas") {
            unsigned item = 0, expected = 0, value = 0;
            if (!(words >> item >> expected >> value) || item >= kItems || expected > 255 ||
                value > 255) {
                return "bad line " + line;
            }
            out.cas.push_back({static_cast<uint8_t>(item), static_cast<uint8_t>(expected),
                               static_cast<uint8_t>(value)});
        } else if (op == "refresh") {
            out.refresh = true;
        } else if (op == "intel") {
            out.intel = true;
        } else if (op == "popup" || op == "notice") {
            const bool bare = op == "notice";
            const char* kept = bare ? " .,'!?-:" : " .,'!?-";
            const size_t most = bare ? kNoticeMax : kSuffixMax;
            unsigned item = 0;
            if (!(words >> item)) return "bad line " + line;
            // A trap's popup has no record: its name (trap.h). Never a notice.
            if (item >= record::kCount && (bare || !trap::is_trap(static_cast<int>(item)))) {
                return "bad line " + line;
            }
            std::string rest;
            std::getline(words >> std::ws, rest);
            std::string text;
            for (char c : rest) {
                keep_font_char(text, static_cast<unsigned char>(c), kept);
                if (text.size() >= most) break;
            }
            while (!text.empty() && text.back() == ' ') text.pop_back();
            if (bare && text.empty()) return "bad line " + line;
            out.popups.push_back({static_cast<uint8_t>(item), std::move(text), bare, true});
        } else if (op == "trap") {
            unsigned item = 0;
            std::string extra;
            if (!(words >> item) || !trap::is_trap(static_cast<int>(item)) || (words >> extra)) {
                return "bad line " + line;
            }
            out.traps.push_back(static_cast<uint8_t>(item));
        } else if (!op.empty()) {
            return "unknown line " + line;
        }
    }
    return "";
}

std::string parse_newgame(const std::string& request, NewGamePlan& out) {
    std::istringstream lines(request);
    std::string line;
    if (!std::getline(lines, line) || line != "ap_newgame") return "not an ap_newgame request";
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string op, offset_text, hex;
        if (!(words >> op)) continue;
        if (op == "start_table") {
            std::vector<uint8_t> raw;
            if (!(words >> hex) || !parse_hex(hex, raw) || raw.size() % 5 != 0 ||
                raw.size() / 5 > kStartTableMax || !out.start_table.empty()) {
                return "bad line " + line;
            }
            for (size_t i = 0; i < raw.size(); i += 5) {
                StartRoom room;
                std::memcpy(&room.x, raw.data() + i, 2);
                std::memcpy(&room.y, raw.data() + i + 2, 2);
                room.flag = raw[i + 4];
                if (room.x < 0 || room.x >= kMapSide || room.y < 0 || room.y >= kMapSide ||
                    room.flag > 1) {
                    return "bad line " + line;
                }
                out.start_table.push_back(room);
            }
            continue;
        }
        if (op == "choice") {
            std::string extra;
            if (words >> extra) return "bad line " + line;
            out.choice = true;
            continue;
        }
        if (op != "identify" && op != "expect" && op != "write" && op != "menu") {
            return "unknown line " + line;
        }
        uint64_t offset = 0;
        SectionBytes edit;
        size_t room = op == "menu" ? kMenuSize : kSectionSize;
        if (!(words >> offset_text >> hex) || !parse_number(offset_text, offset) ||
            !parse_hex(hex, edit.bytes) || offset > room || edit.bytes.size() > room - offset) {
            return "bad line " + line;
        }
        edit.offset = static_cast<uint16_t>(offset);
        (op == "identify" ? out.identify
         : op == "expect" ? out.expect
         : op == "write"  ? out.write
                          : out.menu)
            .push_back(std::move(edit));
    }
    if (out.write.empty()) return "nothing to write";
    // The menu lines place the seed; over a RANDOMIZER game only over one that is this seed.
    if (out.menu.empty()) return "no menu lines";
    if (out.identify.empty()) return "no identify lines";
    return "";
}

std::string parse_discs(const std::string& request, int& count, int& required, std::string& line,
                        bool& told) {
    count = required = 0;
    told = false;
    line.clear();
    if (request == "discs") return "";
    if (request.rfind("discs ", 0) != 0) return "not a discs request";
    // Kept only once the whole request checks out.
    std::istringstream words(request.substr(6));
    std::string hex, extra;
    int n = 0, needed = 0, told_word = 1;
    std::vector<uint8_t> raw;
    if (!(words >> n >> needed >> hex) || n < 0 || n > 255 || needed < 1 || needed > 255 ||
        !parse_hex(hex, raw) || (words >> told_word && (told_word < 0 || told_word > 1)) ||
        (words >> extra)) {
        return "bad discs request";
    }
    std::string text(raw.begin(), raw.end());
    if (!scan_line_ok(text)) return "the scanner's line is not one line of the game's font";
    count = n;
    required = needed;
    told = told_word == 1;
    line = std::move(text);
    return "";
}

std::string parse_mission(const std::string& request, int& column, int& row) {
    column = row = -1;
    if (request.rfind("mission_prompt ", 0) != 0) return "bad mission_prompt request";
    std::istringstream words(request.substr(15));
    std::string extra;
    int c = -1, r = -1;
    if (!(words >> c >> r) || (words >> extra) || c < 0 || c >= kMapSide * kCellColumns ||
        r < 0 || r >= kMapSide * kCellRows) {
        return "bad mission_prompt request";
    }
    column = c;
    row = r;
    return "";
}

std::string parse_disk_slots(const std::string& request, std::map<int, int>& counts) {
    counts.clear();
    if (request == "disk_slots") return "";
    if (request.rfind("disk_slots ", 0) != 0) return "not a disk_slots request";
    std::istringstream words(request.substr(11));
    std::string list, extra;
    int count = 0;
    std::vector<int> slots;
    if (!(words >> count >> list) || (words >> extra) || count < 0 || count > 255 ||
        (list != "-" && !parse_number_list(list, 2, 1, slot::kCount, slots))) {
        return "bad disk_slots request";
    }
    for (int slot : slots) counts[slot] = count;
    return "";
}

std::string parse_labels(const std::string& request, std::map<int, Label>& out) {
    std::istringstream lines(request);
    std::string line;
    if (!std::getline(lines, line) || line != "ap_labels") return "not an ap_labels request";
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string op, hex;
        unsigned x = 0, y = 0;
        if (!(words >> op)) continue;
        std::vector<uint8_t> raw;
        if (op != "label" || !(words >> x >> y >> hex) || x >= kMapSide || y >= kMapSide ||
            !parse_hex(hex, raw)) {
            return "bad line " + line;
        }
        // Up to two lines (the item, then whom it is for), each kept to the font's characters
        // and to its line: the first after the logo glyph. A first line "@N" names item N of
        // another Zexion player, made from its record later (Game::set_labels); "@N NAME" gives
        // it this name instead of the record's (this player's own OS Install Disk).
        std::vector<std::string> parts{std::string()};
        Label label;
        size_t from = 0;
        if (!raw.empty() && raw[0] == '@') {
            int n = 0;
            size_t i = 1;
            while (i < raw.size() && i < 4 && std::isdigit(raw[i])) n = n * 10 + (raw[i++] - '0');
            size_t end = i;
            std::string item_name;
            if (i > 1 && i < raw.size() && raw[i] == ' ') {
                for (end = i + 1; end < raw.size() && raw[end] != '\n'; ++end) {
                    keep_font_char(item_name, raw[end], kFontPunctuation);
                }
            }
            if (i > 1 && n < record::kCount && (end == raw.size() || raw[end] == '\n')) {
                label.item = n;
                label.item_name = std::move(item_name);
                from = end;
            }
        }
        for (size_t k = from; k < raw.size(); ++k) {
            if (raw[k] == '\n') {
                if (parts.size() == 2) break;
                parts.emplace_back();
                continue;
            }
            keep_font_char(parts.back(), raw[k], kFontPunctuation);
        }
        // An @N label's first line waits for its record (Game::resolve_item_labels).
        if (label.item < 0) {
            label.first = first_wrapped(parts[0], static_cast<size_t>(kGamePopupWidth) - 1);
        }
        if (parts.size() > 1) label.who = first_wrapped(parts[1], kGamePopupWidth);
        label.lay_out();
        out[static_cast<int>(y * kMapSide + x)] = label;
    }
    return "";
}

}  // namespace zap
