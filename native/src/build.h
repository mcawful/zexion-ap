// Which game build is running. The helper hooks nothing in a build it does not know, the same
// rule client/game.py follows (docs/re/builds.md).
#pragma once

#include <windows.h>

namespace zap {

// The build's name ("B1"), or nullptr for a build not listed here. Reads the PE header's
// TimeDateStamp and SizeOfImage, as client/patterns.py's KNOWN_BUILDS does.
const char* identify_build(HMODULE exe);

// The same check on raw header values, for tests.
const char* identify_build(DWORD time_date_stamp, DWORD size_of_image);

}  // namespace zap
