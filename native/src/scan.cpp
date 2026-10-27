#include "scan.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

#include "game_mem.h"
#include "text.h"  // kFontPunctuation

namespace zap {

namespace {

using Entry = std::array<BYTE, kTextEntrySize>;

// The entry as it should read holding `text`: the text, then zeros.
Entry padded(const std::string& text) {
    Entry out{};
    std::memcpy(out.data(), text.data(), (std::min)(text.size(), out.size() - 1));
    return out;
}

}  // namespace

bool scan_line_ok(const std::string& line) {
    if (line.empty() || line.size() >= static_cast<size_t>(kTextEntrySize)) return false;
    for (char c : line) {
        bool ok = std::isupper(static_cast<unsigned char>(c)) ||
                  std::isdigit(static_cast<unsigned char>(c)) ||
                  (c != 0 && std::strchr(kFontPunctuation, c) != nullptr);
        if (!ok) return false;
    }
    return true;
}

bool ScanText::apply(BYTE* table_holder, const std::string& line) {
    BYTE* table = pointer_at(table_holder);
    if (!scan_line_ok(line) || table == nullptr) return false;
    BYTE* entry = table + kScanTextEntry * kTextEntrySize;
    Entry copy;
    if (!safe_read(entry, copy.data(), copy.size())) return false;
    if (copy == padded(line)) {
        table_ = table;
        entry_ = entry;
        line_ = line;
        return true;
    }
    bool ours = entry == entry_ && table == table_ && copy == padded(line_);
    if ((copy != padded(kScanTextOriginal) && !ours) || !writable(entry, copy.size())) {
        table_ = nullptr;
        entry_ = nullptr;  // not the text traced on B1 (another language, say): left alone
        line_.clear();
        return false;
    }
    std::memcpy(entry, padded(line).data(), kTextEntrySize);
    table_ = table;
    entry_ = entry;
    line_ = line;
    return true;
}

void ScanText::restore(BYTE* table_holder) {
    if (entry_ == nullptr) return;
    Entry copy;
    if (pointer_at(table_holder) == table_ && safe_read(entry_, copy.data(), copy.size()) &&
        copy == padded(line_) && writable(entry_, copy.size())) {
        std::memcpy(entry_, padded(kScanTextOriginal).data(), kTextEntrySize);
    }
    table_ = nullptr;
    entry_ = nullptr;
    line_.clear();
}

}  // namespace zap
