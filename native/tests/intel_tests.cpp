// Host tests for the Area Map as an Intel Chip (intel.h): a stand-in item record table with the
// Area Map's record as B1's Objects file holds it.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "check.h"
#include "intel.h"

namespace {

struct Records {
    std::vector<BYTE> table = std::vector<BYTE>(51 * 100);
    BYTE* holder_value = nullptr;
    Records() {
        holder_value = table.data();
        BYTE* r = area_map();
        auto word = [&](ptrdiff_t o, int16_t v) { std::memcpy(r + o, &v, 2); };
        word(0x00, 34);
        word(0x02, 25);
        word(0x04, 6);
        word(0x06, 2);
        r[0x08] = 0;
        r[0x09] = 3;
        std::memcpy(r + 0x0A, "AREA MAP\0NK", 11);  // what follows the name, as in the file
        word(0x3C, 46);
        word(0x40, 3551);
        word(0x58, 1);
    }
    BYTE* holder() { return reinterpret_cast<BYTE*>(&holder_value); }
    BYTE* area_map() { return table.data() + 49 * 100; }
    const char* name() { return reinterpret_cast<const char*>(area_map() + 0x0A); }
    int16_t event() {
        int16_t v;
        std::memcpy(&v, area_map() + 0x40, 2);
        return v;
    }
};

void test_on_renames_it_and_turns_off_the_reveal() {
    Records t;
    zap::IntelChip chip;
    chip.watch(t.holder());
    check(std::strcmp(t.name(), "AREA MAP") == 0 && !chip.restyled(), "off: the game's");
    chip.set(true);
    chip.watch(t.holder());
    check(std::strcmp(t.name(), "INTEL CHIP") == 0 && t.event() == -1 && chip.restyled(),
          "on: INTEL CHIP, no area event");
    chip.watch(t.holder());
    check(std::strcmp(t.name(), "INTEL CHIP") == 0 && chip.restyled(), "and stays so");
    chip.set(false);
    chip.watch(t.holder());
    check(std::strcmp(t.name(), "AREA MAP") == 0 && t.event() == 3551 && !chip.restyled(),
          "off again: AREA MAP and event 3551 back");
}

void test_restore_puts_it_back() {
    Records t;
    zap::IntelChip chip;
    chip.set(true);
    chip.watch(t.holder());
    chip.restore(t.holder());
    check(std::strcmp(t.name(), "AREA MAP") == 0 && t.event() == 3551 && !chip.restyled(),
          "restore: the game's");
}

void test_a_record_not_as_traced_is_left_alone() {
    Records t;
    t.area_map()[0x09] = 5;  // another palette
    zap::IntelChip chip;
    chip.set(true);
    chip.watch(t.holder());
    check(std::strcmp(t.name(), "AREA MAP") == 0 && t.event() == 3551 && !chip.restyled(),
          "another record: untouched");

    Records u;
    std::memcpy(u.area_map() + 0x0A, "MAPA\0", 5);  // a translation's name, say
    chip.watch(u.holder());
    check(std::strcmp(u.name(), "MAPA") == 0 && u.event() == 3551, "another name: untouched");

    BYTE* none = nullptr;
    chip.watch(reinterpret_cast<BYTE*>(&none));
    check(!chip.restyled(), "no records: nothing");
}

void test_an_earlier_helpers_rename_is_adopted_and_put_back() {
    Records t;
    zap::IntelChip earlier;
    earlier.set(true);
    earlier.watch(t.holder());  // then unloaded with no frame to put it back
    zap::IntelChip chip;
    chip.set(true);
    chip.watch(t.holder());
    check(chip.restyled(), "adopted");
    zap::IntelChip later;
    later.restore(t.holder());
    check(std::strcmp(t.name(), "AREA MAP") == 0 && t.event() == 3551,
          "put back by a helper that never renamed it");
}

}  // namespace

int main() {
    test_on_renames_it_and_turns_off_the_reveal();
    test_restore_puts_it_back();
    test_a_record_not_as_traced_is_left_alone();
    test_an_earlier_helpers_rename_is_adopted_and_put_back();
    return report("intel tests");
}
