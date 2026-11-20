// offline.cpp for the test-only build of the core (zap_core_test, zap_shared_test): the same
// source with ZAP_TEST_BUILD set, which makes each write primitive in it (write_whole, make_folder,
// make_dir) refuse a folder that is not a test's own: inside the temporary folder and never one of
// the real folders (restrict_to_test_folder). The release build (zap_core, zap_shared) compiles
// offline.cpp itself, without the definition. Every test program and the test builds of the helpers
// link the test-only build, so a unit test handed a real folder writes nothing there.
#define ZAP_TEST_BUILD 1
#include "offline.cpp"
