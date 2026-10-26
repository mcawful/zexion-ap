#include "scan.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "game_mem.h"

namespace zap {

namespace {

// The entry as it should read holding `text`: the text, then zeros.
void padded(const std::string& text, BYTE (&out)[kTextEntrySize]) {
    std::memset(out, 0, sizeof(out));
    std::memcpy(out, text.data(), (std::min)(text.size(), sizeof(out) - 1));
}

bool reads(const BYTE (&entry)[kTextEntrySize], const std::string& text) {
    BYTE want[kTextEntrySize];
    padded(text, want);
    return std::memcmp(entry, want, sizeof(want)) == 0;
}

void put_text(BYTE* entry, const std::string& text) {
    BYTE bytes[kTextEntrySize];
    padded(text, bytes);
    std::memcpy(entry, bytes, sizeof(bytes));
}

}  // namespace

bool scan_line_ok(const std::string& line) {
    if (line.empty() || line.size() >= static_cast<size_t>(kTextEntrySize)) return false;
    for (char c : line) {
        bool ok = std::isupper(static_cast<unsigned char>(c)) ||
                  std::isdigit(static_cast<unsigned char>(c)) ||
                  (c != 0 && std::strchr(" .,'!?-:()/&", c) != nullptr);
        if (!ok) return false;
    }
    return true;
}

bool ScanText::apply(BYTE* table_holder, const std::string& line) {
    BYTE* table = pointer_at(table_holder);
    if (!scan_line_ok(line) || table == nullptr) return false;
    BYTE* entry = table + kScanTextEntry * kTextEntrySize;
    BYTE copy[kTextEntrySize];
    if (!safe_read(entry, copy, sizeof(copy))) return false;
    if (reads(copy, line)) {
        table_ = table;
        entry_ = entry;
        line_ = line;
        return true;
    }
    bool ours = entry == entry_ && table == table_ && reads(copy, line_);
    if ((!reads(copy, kScanTextOriginal) && !ours) || !writable(entry, sizeof(copy))) {
        table_ = nullptr;
        entry_ = nullptr;  // not the text traced on B1 (another language, say): left alone
        line_.clear();
        return false;
    }
    put_text(entry, line);
    table_ = table;
    entry_ = entry;
    line_ = line;
    return true;
}

void ScanText::restore(BYTE* table_holder) {
    if (entry_ == nullptr) return;
    BYTE copy[kTextEntrySize];
    if (pointer_at(table_holder) == table_ && safe_read(entry_, copy, sizeof(copy)) &&
        reads(copy, line_) &&
        writable(entry_, sizeof(copy))) {
        put_text(entry_, kScanTextOriginal);
    }
    table_ = nullptr;
    entry_ = nullptr;
    line_.clear();
}

}  // namespace zap
