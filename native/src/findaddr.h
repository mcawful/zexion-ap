// The helper finds the game's addresses itself, by the byte patterns the client finds them with
// (client/patterns.py, generated into findaddr_patterns.h by tools/gen_native_patterns.py), so it
// works on a first launch with no client and no cache (native/README.md, "Finding its own
// addresses"). The rules are client/game.py's, locate_all: each pattern must match exactly once
// in the image's .text (overlapping matches counted), each leads to an address (a function's
// start or the instruction itself for kAt; the instruction's end plus its 32-bit field for kRip,
// both plus the addend), and every pattern of a target must lead to the same one. Addresses are
// RVAs; none is ever written here (CLAUDE.md, "No hard-coded addresses").
#pragma once

#include "platform.h"

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace zap::find {

enum class Kind : uint8_t {
    kRip,  // match + rip_end + the int32 at match + disp_at + addend
    kAt,   // the match itself + addend
};

struct Pattern {
    const char* text;  // hex bytes, "??" for a wildcard, one space apart
    int disp_at;       // kRip: the field's offset in the match
    Kind kind;
    int rip_end;       // kRip: the offset of the referring instruction's end
    int64_t addend;
};

struct Target {
    const char* name;  // client/patterns.py's, also kConfigKeys'
    const Pattern* patterns;
    int count;
};

// What a scan found: each target whose patterns all agreed, and why each other did not.
struct Found {
    std::map<std::string, uint64_t> rvas;
    std::vector<std::string> failed;  // "NAME: why", in the table's order
};

// The targets the helper takes, from the generated header: client/native.py's CONFIG_KEYS in order.
std::span<const Target> game_targets();

// The pure part, for tests: "" and `rva` (code_rva + ...) if every pattern of `target` matches
// `code` exactly once and they all lead to one address, else why not ("pattern 2 matched 0
// times", "pattern 3 matched 2 times", "patterns disagree (0x.., 0x..)", "pattern 1 is not
// readable"). `code` is the image's .text, loaded at `code_rva`.
std::string find_target(std::span<const uint8_t> code, uint64_t code_rva, const Target& target,
                        uint64_t& rva);

// Every target of `targets`: those found in `rvas`, the rest in `failed`.
Found find_all(std::span<const uint8_t> code, uint64_t code_rva,
               std::span<const Target> targets);

// The keys of `client` (a client's `config`) that `own` (a scan's) gives another address, or lacks,
// and the keys of `own` `client` lacks, comma-separated in that order, each once: "" if they
// agree. Both come from the same patterns, so a difference is a bug in one of the scans.
std::string differing_keys(const std::map<std::string, uint64_t>& client,
                           const std::map<std::string, uint64_t>& own);

// Why a scan has no address for a key, given Game::configure's refusal of its addresses
// ("missing NAME" when a required key is not in `found.rvas`): that key's own failure from
// `found.failed`, else `refusal` as it is.
std::string failure_of(const Found& found, const std::string& refusal);

// The names of the targets in `found.failed`, comma-separated: "" if none.
std::string failed_names(const Found& found);

#ifdef _WIN32
// "" if `image`, the game's loaded exe, is a build the helper knows (build.h) and its .text was
// read and scanned for game_targets(), `found` then holding the result; else why it was not (and
// `found` empty). Any thread; never under the loader lock.
std::string find_in_image(HMODULE image, Found& found);
#endif

}  // namespace zap::find
