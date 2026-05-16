#include "FlyWithLua.hpp"
#include "FlyWithLua.h"
#include <iostream>
#include <vector>
#include <string>
#include <sys/stat.h>

lua_State* L = nullptr;

namespace flywithlua {
    lua_State* FWLLua = nullptr;
    bool LuaIsRunning = false;
    bool WeAreNotInDrawingState = true;
    std::string scriptDir;
    std::string quarantineDir;
    int found_bad_function_script = 0;
    int developer_mode = 0;
    int verbose_logging_mode = 0;

    void logMsg(ELogType logType, std::string message) {
        XPLMDebugString(("FlyWithLua: " + message + "\n").c_str());
    }

    void panic(const std::string& message) {
        logMsg(logToAll, "PANIC: " + message);
        LuaIsRunning = false;
    }

    void CopyDataRefsToLua(void) {
        // Placeholder for dataref synchronization logic
    }

    void CopyDataRefsToXPlane(void) {
        // Placeholder for dataref synchronization logic
    }

    void DebugLua() {
        // Placeholder for debug logic
    }

    bool ReadAllScriptFiles() {
        logMsg(logToDevCon, "Scanning for scripts in: " + scriptDir);
        // In a real implementation, this would iterate files in scriptDir
        // and call luaL_dofile() on each.
        return true;
    }
}

extern "C" void register_swift_bridge(lua_State* L);

PLUGIN_API int XPluginStart(char * outName, char * outSig, char * outDesc) {
    strcpy(outName, "FlyWithLua-Mac");
    strcpy(outSig, "com.goodbad-web.flywithlua_mac");
    strcpy(outDesc, "A modernized, macOS-optimized FlyWithLua plugin.");

    // Enable native paths (UTF-8 /)
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);

    // Get our plugin path to locate the Scripts folder
    char pluginPath[512];
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, pluginPath, nullptr, nullptr);
    
    // Extract path and set scriptDir
    // Expected structure: Resources/plugins/FlyWithLua-Mac/mac_arm64/FlyWithLua.xpl
    // We want: Resources/plugins/FlyWithLua/Scripts
    std::string path(pluginPath);
    size_t lastSep = path.find_last_of("/");
    if (lastSep != std::string::npos) {
        std::string baseDir = path.substr(0, lastSep); // mac_arm64
        lastSep = baseDir.find_last_of("/");
        if (lastSep != std::string::npos) {
            std::string pluginDir = baseDir.substr(0, lastSep); // FlyWithLua-Mac
            lastSep = pluginDir.find_last_of("/");
             if (lastSep != std::string::npos) {
                 std::string parentDir = pluginDir.substr(0, lastSep); // plugins
                 flywithlua::scriptDir = parentDir + "/FlyWithLua/Scripts";
                 flywithlua::quarantineDir = parentDir + "/FlyWithLua/Scripts (Quarantine)";
             }
        }
    }

    L = luaL_newstate();
    if (L) {
        luaL_openlibs(L);
        flywithlua::FWLLua = L;
        flywithlua::LuaIsRunning = true;
        
        // Register Swift-based native modules
        register_swift_bridge(L);
        
        // Initial load
        flywithlua::ReadAllScriptFiles();
        
        XPLMDebugString("FlyWithLua-Mac: Successfully started and initialized Lua.\n");
    } else {
        XPLMDebugString("FlyWithLua-Mac: Failed to initialize Lua state.\n");
        return 0;
    }

    return 1;
}

PLUGIN_API void XPluginStop(void) {
    if (L) {
        lua_close(L);
        L = nullptr;
        flywithlua::FWLLua = nullptr;
        flywithlua::LuaIsRunning = false;
        XPLMDebugString("FlyWithLua-Mac: Stopped.\n");
    }
}

PLUGIN_API void XPluginDisable(void) {
    flywithlua::LuaIsRunning = false;
}

PLUGIN_API int XPluginEnable(void) {
    flywithlua::LuaIsRunning = true;
    return 1;
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID inFrom, int inMsg, void * inParam) {
    // Handle X-Plane messages
}
