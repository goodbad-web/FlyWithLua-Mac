// CMake builds do not link the SwiftUI bridge that is part of the XcodeGen
// target. Keep the plugin's C ABI available so the portable CMake build can
// validate and package the native core without depending on Swift runtime
// objects.

struct lua_State;

extern "C" void flywithlua_toggle_window(void) {}
extern "C" void flywithlua_update_current_altitude(double) {}
extern "C" void flywithlua_update_script_load_summary(int, int, int, const char*) {}
extern "C" void flywithlua_update_last_log_message(const char*) {}
extern "C" void register_swift_bridge(lua_State*) {}
