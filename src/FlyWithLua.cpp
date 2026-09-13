#include "FlyWithLua.h"
#include "XPLMPlugin.h"
#include "XPLMUtilities.h"
#include "XPLMProcessing.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMDataAccess.h"
#include "XPLMMenus.h"
#include "FloatingWindows/FLWIntegration.h"
#include "Fmod/FmodIntegration.h"
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <dirent.h>
#include <memory>
#include <sstream>
#include <sys/stat.h>
#include <unordered_map>

lua_State* L = nullptr;
// lState is defined in imgui_lua_bindings.cpp

namespace flywithlua {
    extern bool LuaIsRunning;
}

static XPLMMenuID gPluginsMenu = nullptr;
static int gFlyWithLuaMenuItem = -1;
static XPLMMenuID gFlyWithLuaMenu = nullptr;
static int gFlyWithLuaMacrosMenuItem = -1;
static XPLMMenuID gFlyWithLuaMacrosMenu = nullptr;
static XPLMCommandRef gFlyWithLuaCommand = nullptr;
static XPLMDataRef gAltitudeDataRef = nullptr;
static int gReloadMenuItem = -1;
static int gWriteDebugMenuItem = -1;
static int gReturnQuarantineMenuItem = -1;
static int gDeveloperModeMenuItem = -1;
static int gVerboseModeMenuItem = -1;
static bool gSuppressMacroMenuRefresh = false;
static bool gMacroMenuNeedsRefresh = false;
struct FlyWithLuaScriptFailure {
    std::string fileName;
    std::string message;
};
struct FlyWithLuaCommandBinding {
    std::string name;
    std::string description;
    std::string beginCode;
    std::string continueCode;
    std::string endCode;
    XPLMCommandRef commandRef = nullptr;
};
static std::unordered_map<std::string, std::unique_ptr<FlyWithLuaCommandBinding>> gFlyWithLuaCommandBindings;
static std::string gDrawCommand;
static std::string gEveryFrameCommand;
static std::string gOftenCommand;
static std::string gSometimesCommand;
static std::string gOnExitCommand;
static std::string gMouseClickCommand;
static std::string gMouseWheelCommand;
static float gSometimesAccumulator = 0.0f;

extern "C" void flywithlua_toggle_window(void);
extern "C" void flywithlua_update_current_altitude(double altitude);
extern "C" void flywithlua_update_script_count(int count);
extern "C" void flywithlua_clear_script_load_failures(void);
extern "C" void flywithlua_update_script_load_results(const char* jsonPayload);
extern "C" void flywithlua_update_last_log_message(const char* message);
extern "C" void flywithlua_reload_scripts(void);
extern "C" int luaopen_socket_core(lua_State* L);
extern "C" int luaopen_mime_core(lua_State* L);
static bool InitializeLuaRuntime(bool registerFlightLoop);
extern "C" void register_swift_bridge(lua_State* L);
float FlightLoopCallback(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void * inRefcon);
int FlyWithLuaDrawCallback(XPLMDrawingPhase inPhase, int inIsBefore, void * inRefcon);
static void RunLuaStringChunk(const std::string& code, const char* context);
static void FlyWithLuaMenuHandler(void*, void*);
static void FlyWithLuaMacroMenuHandler(void*, void*);
static bool HasFlyWithLuaScriptExtension(const std::string& fileName);
static bool IsRegularFile(const std::string& path);
static void WriteDebugFile();
static void ReturnQuarantinedScripts();
static void SetDeveloperMode(bool enabled);
static void SetVerboseLoggingMode(bool enabled);
static void RefreshFlyWithLuaMacrosMenu();
static void MarkFlyWithLuaMacrosMenuDirty();

// LuaJIT 2.1 exposes the Lua 5.1 API and does not provide luaL_requiref.
// Registering the statically linked modules in package.preload gives require()
// the same resolution path without depending on a platform-specific .so file.
static bool RegisterLuaBuiltinModule(lua_State* state, const char* moduleName, lua_CFunction openFunction) {
    lua_getglobal(state, "package");
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        return false;
    }

    lua_getfield(state, -1, "preload");
    if (!lua_istable(state, -1)) {
        lua_pop(state, 2);
        return false;
    }

    lua_pushcfunction(state, openFunction);
    lua_setfield(state, -2, moduleName);
    lua_pop(state, 2);
    return true;
}

static int FlyWithLuaMenuCommandHandler(XPLMCommandRef /*inCommand*/, XPLMCommandPhase inPhase, void * /*inRefcon*/) {
    if (inPhase == xplm_CommandBegin && flywithlua::LuaIsRunning) {
        flywithlua_toggle_window();
    }
    return 1;
}

static bool LuaStringArg(lua_State* state, int index, std::string& out) {
    if (!state || !lua_isstring(state, index)) {
        return false;
    }

    size_t len = 0;
    const char* value = lua_tolstring(state, index, &len);
    if (!value) {
        return false;
    }

    out.assign(value, len);
    return true;
}

static int FlyWithLuaScriptCommandHandler(XPLMCommandRef /*inCommand*/, XPLMCommandPhase inPhase, void * inRefcon) {
    auto* binding = static_cast<FlyWithLuaCommandBinding*>(inRefcon);
    if (!binding) {
        return 1;
    }

    switch (inPhase) {
        case xplm_CommandBegin:
            RunLuaStringChunk(binding->beginCode, (binding->name + ":begin").c_str());
            break;
        case xplm_CommandContinue:
            RunLuaStringChunk(binding->continueCode, (binding->name + ":continue").c_str());
            break;
        case xplm_CommandEnd:
            RunLuaStringChunk(binding->endCode, (binding->name + ":end").c_str());
            break;
        default:
            break;
    }

    return 1;
}

static std::string FlyWithLuaMacroDisplayName(const FlyWithLuaCommandBinding& binding) {
    if (!binding.description.empty()) {
        return binding.description;
    }
    return binding.name;
}

static void DestroyFlyWithLuaMacrosMenu() {
    if (gFlyWithLuaMacrosMenu) {
        XPLMDestroyMenu(gFlyWithLuaMacrosMenu);
        gFlyWithLuaMacrosMenu = nullptr;
    }
}

static void RefreshFlyWithLuaMacrosMenu() {
    if (!gFlyWithLuaMenu || gFlyWithLuaMacrosMenuItem < 0) {
        gMacroMenuNeedsRefresh = true;
        return;
    }

    DestroyFlyWithLuaMacrosMenu();
    gFlyWithLuaMacrosMenu = XPLMCreateMenu("FlyWithLua Macros", gFlyWithLuaMenu, gFlyWithLuaMacrosMenuItem, FlyWithLuaMacroMenuHandler, nullptr);
    if (!gFlyWithLuaMacrosMenu) {
        XPLMDebugString("FlyWithLua-Mac Warning: Could not create FlyWithLua Macros submenu.\n");
        gMacroMenuNeedsRefresh = true;
        return;
    }

    std::vector<FlyWithLuaCommandBinding*> bindings;
    bindings.reserve(gFlyWithLuaCommandBindings.size());
    for (const auto& entry : gFlyWithLuaCommandBindings) {
        if (entry.second) {
            bindings.push_back(entry.second.get());
        }
    }

    std::sort(bindings.begin(), bindings.end(), [](const FlyWithLuaCommandBinding* lhs, const FlyWithLuaCommandBinding* rhs) {
        return lhs->name < rhs->name;
    });

    for (FlyWithLuaCommandBinding* binding : bindings) {
        std::string label = FlyWithLuaMacroDisplayName(*binding);
        XPLMAppendMenuItem(gFlyWithLuaMacrosMenu, label.c_str(), binding, 1);
    }

    gMacroMenuNeedsRefresh = false;
}

static void MarkFlyWithLuaMacrosMenuDirty() {
    if (gSuppressMacroMenuRefresh) {
        gMacroMenuNeedsRefresh = true;
        return;
    }
    RefreshFlyWithLuaMacrosMenu();
}

static void FlyWithLuaMenuHandler(void* /*inMenuRef*/, void* inItemRef) {
    const char* action = static_cast<const char*>(inItemRef);
    if (!action) {
        return;
    }

    if (std::strcmp(action, "Reload") == 0) {
        flywithlua::logMsg(logToDevCon, "FlyWithLua-Mac: User forced a script reload.");
        flywithlua_reload_scripts();
        return;
    }

    if (std::strcmp(action, "Debug") == 0) {
        WriteDebugFile();
        return;
    }

    if (std::strcmp(action, "ReturnQt") == 0) {
        ReturnQuarantinedScripts();
        return;
    }

    if (std::strcmp(action, "DevMode") == 0) {
        SetDeveloperMode(flywithlua::developer_mode == 0);
        return;
    }

    if (std::strcmp(action, "VerboseMode") == 0) {
        SetVerboseLoggingMode(flywithlua::verbose_logging_mode == 0);
        return;
    }
}

static void FlyWithLuaMacroMenuHandler(void* /*inMenuRef*/, void* inItemRef) {
    auto* binding = static_cast<FlyWithLuaCommandBinding*>(inItemRef);
    if (!binding || !binding->commandRef) {
        return;
    }

    XPLMCommandOnce(binding->commandRef);
}

static void ClearFlyWithLuaCommands() {
    for (auto& entry : gFlyWithLuaCommandBindings) {
        FlyWithLuaCommandBinding* binding = entry.second.get();
        if (binding && binding->commandRef) {
            XPLMUnregisterCommandHandler(binding->commandRef, FlyWithLuaScriptCommandHandler, 0, binding);
            binding->commandRef = nullptr;
        }
    }
    gFlyWithLuaCommandBindings.clear();
}

static int LuaCreateCommandCallback(lua_State* state) {
    std::string name;
    std::string description;
    std::string beginCode;
    std::string continueCode;
    std::string endCode;

    if (!LuaStringArg(state, 1, name) ||
        !LuaStringArg(state, 2, description) ||
        !LuaStringArg(state, 3, beginCode) ||
        !LuaStringArg(state, 4, continueCode) ||
        !LuaStringArg(state, 5, endCode)) {
        XPLMDebugString("FlyWithLua-Mac Warning: create_command() expects five string arguments.\n");
        return 0;
    }

    if (name.rfind("sim/", 0) == 0) {
        XPLMDebugString(("FlyWithLua-Mac Warning: create_command() rejected custom command name: " + name + "\n").c_str());
        return 0;
    }

    auto existing = gFlyWithLuaCommandBindings.find(name);
    if (existing != gFlyWithLuaCommandBindings.end()) {
        XPLMDebugString(("FlyWithLua-Mac Warning: create_command() duplicate ignored: " + name + "\n").c_str());
        return 0;
    }

    auto binding = std::make_unique<FlyWithLuaCommandBinding>();
    binding->name = name;
    binding->description = description;
    binding->beginCode = beginCode;
    binding->continueCode = continueCode;
    binding->endCode = endCode;
    binding->commandRef = XPLMCreateCommand(name.c_str(), description.c_str());
    if (!binding->commandRef) {
        XPLMDebugString(("FlyWithLua-Mac Warning: create_command() failed to create command: " + name + "\n").c_str());
        return 0;
    }

    FlyWithLuaCommandBinding* bindingPtr = binding.get();
    XPLMRegisterCommandHandler(binding->commandRef, FlyWithLuaScriptCommandHandler, 0, bindingPtr);
    gFlyWithLuaCommandBindings.emplace(name, std::move(binding));
    MarkFlyWithLuaMacrosMenuDirty();
    return 0;
}

static void UpdateFlyWithLuaMenuEnabled(bool enabled) {
    if (gPluginsMenu && gFlyWithLuaMenuItem >= 0) {
        XPLMEnableMenuItem(gPluginsMenu, gFlyWithLuaMenuItem, enabled ? 1 : 0);
    }
}

static void UpdateFlyWithLuaMenuChecks() {
    if (!gFlyWithLuaMenu) {
        return;
    }

    if (gDeveloperModeMenuItem >= 0) {
        XPLMCheckMenuItem(gFlyWithLuaMenu, gDeveloperModeMenuItem,
                          flywithlua::developer_mode ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    }

    if (gVerboseModeMenuItem >= 0) {
        XPLMCheckMenuItem(gFlyWithLuaMenu, gVerboseModeMenuItem,
                          flywithlua::verbose_logging_mode ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    }
}

namespace flywithlua {
std::string JoinPath(const std::string& base, const std::string& child) {
    if (base.empty()) {
        return child;
    }
    if (child.empty()) {
        return base;
    }
    if (base.back() == '/') {
        return base + child;
    }
    return base + "/" + child;
}
}

static std::string GetSystemPathString() {
    char systemPath[512] = {0};
    XPLMGetSystemPath(systemPath);
    return systemPath;
}

static std::string GetDebugFilePath() {
    return flywithlua::JoinPath(GetSystemPathString(), "FlyWithLua_Debug.txt");
}

static void WriteDebugFile() {
    const std::string debugPath = GetDebugFilePath();
    std::ofstream debugFile(debugPath, std::ios::out | std::ios::trunc);
    if (!debugFile.is_open()) {
        XPLMDebugString(("FlyWithLua-Mac Warning: Could not open debug file for writing: " + debugPath + "\n").c_str());
        return;
    }

    const std::time_t now = std::time(nullptr);
    debugFile << "-- FlyWithLua-Mac Debug File\n";
    debugFile << "-- Generated: " << std::asctime(std::localtime(&now));
    debugFile << "\n";
    debugFile << "System path: " << GetSystemPathString() << "\n";
    debugFile << "Script directory: " << flywithlua::scriptDir << "\n";
    debugFile << "Quarantine directory: " << flywithlua::quarantineDir << "\n";
    debugFile << "Lua running: " << (flywithlua::LuaIsRunning ? "true" : "false") << "\n";
    debugFile << "Developer mode: " << (flywithlua::developer_mode ? "true" : "false") << "\n";
    debugFile << "Verbose logging: " << (flywithlua::verbose_logging_mode ? "true" : "false") << "\n";
    debugFile << "Bad function script: " << (flywithlua::found_bad_function_script ? "true" : "false") << "\n\n";
    debugFile << "*** Draw callback ***\n" << gDrawCommand << "\n";
    debugFile << "*** Every frame callback ***\n" << gEveryFrameCommand << "\n";
    debugFile << "*** Often callback ***\n" << gOftenCommand << "\n";
    debugFile << "*** Sometimes callback ***\n" << gSometimesCommand << "\n";
    debugFile << "*** On exit callback ***\n" << gOnExitCommand << "\n";
    debugFile << "*** Mouse click callback ***\n" << gMouseClickCommand << "\n";
    debugFile << "*** Mouse wheel callback ***\n" << gMouseWheelCommand << "\n";
    debugFile << "\n*** Custom commands ***\n";

    if (gFlyWithLuaCommandBindings.empty()) {
        debugFile << "No custom commands defined.\n";
    } else {
        for (const auto& entry : gFlyWithLuaCommandBindings) {
            const auto& binding = *entry.second;
            debugFile << "Command: " << binding.name << "\n";
            debugFile << "Description: " << binding.description << "\n";
            debugFile << "Begin: " << binding.beginCode << "\n";
            debugFile << "Continue: " << binding.continueCode << "\n";
            debugFile << "End: " << binding.endCode << "\n\n";
        }
    }

    debugFile.close();
    XPLMDebugString(("FlyWithLua-Mac: Debug file written to " + debugPath + "\n").c_str());
    flywithlua_update_last_log_message("Debug file written");
}

static void ReturnQuarantinedScripts() {
    if (flywithlua::scriptDir.empty() || flywithlua::quarantineDir.empty()) {
        XPLMDebugString("FlyWithLua-Mac Warning: Cannot return quarantined scripts because the script folders are unknown.\n");
        return;
    }

    DIR* dir = opendir(flywithlua::quarantineDir.c_str());
    if (!dir) {
        XPLMDebugString(("FlyWithLua-Mac Warning: Could not open quarantine folder: " + flywithlua::quarantineDir + "\n").c_str());
        return;
    }

    std::vector<std::string> files;
    while (dirent* entry = readdir(dir)) {
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        std::string name = entry->d_name;
        std::string sourcePath = flywithlua::JoinPath(flywithlua::quarantineDir, name);
        if (IsRegularFile(sourcePath) && HasFlyWithLuaScriptExtension(name)) {
            files.push_back(name);
        }
    }

    closedir(dir);

    if (files.empty()) {
        XPLMDebugString("FlyWithLua-Mac: No quarantined scripts found.\n");
        flywithlua_update_last_log_message("No quarantined scripts found");
        return;
    }

    std::sort(files.begin(), files.end());
    int movedCount = 0;
    for (const std::string& fileName : files) {
        std::string sourcePath = flywithlua::JoinPath(flywithlua::quarantineDir, fileName);
        std::string destinationPath = flywithlua::JoinPath(flywithlua::scriptDir, fileName);
        if (std::rename(sourcePath.c_str(), destinationPath.c_str()) == 0) {
            ++movedCount;
            XPLMDebugString(("FlyWithLua-Mac: Returned quarantined script " + destinationPath + "\n").c_str());
        } else {
            XPLMDebugString(("FlyWithLua-Mac Warning: Could not return quarantined script " + destinationPath + "\n").c_str());
        }
    }

    flywithlua_update_last_log_message((std::string("Returned ") + std::to_string(movedCount) + " quarantined scripts").c_str());
}

static void SetDeveloperMode(bool enabled) {
    flywithlua::developer_mode = enabled ? 1 : 0;
    UpdateFlyWithLuaMenuChecks();
    XPLMDebugString((std::string("FlyWithLua-Mac: Developer mode ") + (enabled ? "enabled" : "disabled") + "\n").c_str());
}

static void SetVerboseLoggingMode(bool enabled) {
    flywithlua::verbose_logging_mode = enabled ? 1 : 0;
    UpdateFlyWithLuaMenuChecks();
    XPLMDebugString((std::string("FlyWithLua-Mac: Verbose logging ") + (enabled ? "enabled" : "disabled") + "\n").c_str());
}

static void RegisterFlyWithLuaMenu() {
    if (gFlyWithLuaMenuItem >= 0) {
        return;
    }

    gPluginsMenu = XPLMFindPluginsMenu();
    if (!gPluginsMenu) {
        XPLMDebugString("FlyWithLua-Mac Warning: Could not find X-Plane Plugins menu.\n");
        return;
    }

    gFlyWithLuaCommand = XPLMCreateCommand("com.goodbad-web.flywithlua_mac/toggle_window", "Toggle the FlyWithLua-Mac window");
    if (!gFlyWithLuaCommand) {
        XPLMDebugString("FlyWithLua-Mac Warning: Could not create FlyWithLua-Mac command.\n");
        return;
    }

    XPLMRegisterCommandHandler(gFlyWithLuaCommand, FlyWithLuaMenuCommandHandler, 1, nullptr);

    gFlyWithLuaMenuItem = XPLMAppendMenuItem(gPluginsMenu, "FlyWithLua-Mac", nullptr, 1);
    if (gFlyWithLuaMenuItem < 0) {
        XPLMDebugString("FlyWithLua-Mac Warning: Could not append menu item to Plugins menu.\n");
        XPLMUnregisterCommandHandler(gFlyWithLuaCommand, FlyWithLuaMenuCommandHandler, 1, nullptr);
        gFlyWithLuaCommand = nullptr;
        gFlyWithLuaMenuItem = -1;
        return;
    }

    gFlyWithLuaMenu = XPLMCreateMenu("FlyWithLua-Mac", gPluginsMenu, gFlyWithLuaMenuItem, FlyWithLuaMenuHandler, nullptr);
    if (!gFlyWithLuaMenu) {
        XPLMDebugString("FlyWithLua-Mac Warning: Could not create FlyWithLua-Mac submenu.\n");
        XPLMRemoveMenuItem(gPluginsMenu, gFlyWithLuaMenuItem);
        XPLMUnregisterCommandHandler(gFlyWithLuaCommand, FlyWithLuaMenuCommandHandler, 1, nullptr);
        gFlyWithLuaCommand = nullptr;
        gFlyWithLuaMenuItem = -1;
        return;
    }

    gReloadMenuItem = XPLMAppendMenuItem(gFlyWithLuaMenu, "Reload all Lua script files", (void*) "Reload", 1);
    XPLMAppendMenuSeparator(gFlyWithLuaMenu);
    gFlyWithLuaMacrosMenuItem = XPLMAppendMenuItem(gFlyWithLuaMenu, "FlyWithLua Macros", nullptr, 1);
    if (gFlyWithLuaMacrosMenuItem >= 0) {
        RefreshFlyWithLuaMacrosMenu();
    }
    gWriteDebugMenuItem = XPLMAppendMenuItem(gFlyWithLuaMenu, "Write Debug file", (void*) "Debug", 1);
    gReturnQuarantineMenuItem = XPLMAppendMenuItem(gFlyWithLuaMenu, "Return all quarantined Lua scripts", (void*) "ReturnQt", 1);
    XPLMAppendMenuSeparator(gFlyWithLuaMenu);
    gDeveloperModeMenuItem = XPLMAppendMenuItem(gFlyWithLuaMenu, "Disable moving bad scripts to Quarantine", (void*) "DevMode", 1);
    gVerboseModeMenuItem = XPLMAppendMenuItem(gFlyWithLuaMenu, "Enable Verbose Logging Mode", (void*) "VerboseMode", 1);

    UpdateFlyWithLuaMenuChecks();
    UpdateFlyWithLuaMenuEnabled(true);
}

static void UnregisterFlyWithLuaMenu() {
    if (gFlyWithLuaCommand) {
        XPLMUnregisterCommandHandler(gFlyWithLuaCommand, FlyWithLuaMenuCommandHandler, 1, nullptr);
        gFlyWithLuaCommand = nullptr;
    }

    if (gFlyWithLuaMenu) {
        XPLMDestroyMenu(gFlyWithLuaMenu);
        gFlyWithLuaMenu = nullptr;
    }

    if (gFlyWithLuaMacrosMenu) {
        XPLMDestroyMenu(gFlyWithLuaMacrosMenu);
        gFlyWithLuaMacrosMenu = nullptr;
    }

    if (gPluginsMenu && gFlyWithLuaMenuItem >= 0) {
        XPLMRemoveMenuItem(gPluginsMenu, gFlyWithLuaMenuItem);
    }

    gFlyWithLuaMenuItem = -1;
    gFlyWithLuaMenu = nullptr;
    gFlyWithLuaMacrosMenuItem = -1;
    gFlyWithLuaMacrosMenu = nullptr;
    gReloadMenuItem = -1;
    gWriteDebugMenuItem = -1;
    gReturnQuarantineMenuItem = -1;
    gDeveloperModeMenuItem = -1;
    gVerboseModeMenuItem = -1;
    gSuppressMacroMenuRefresh = false;
    gMacroMenuNeedsRefresh = false;
    gPluginsMenu = nullptr;
}

static std::string GetMainDirectoryFromScripts() {
    std::string scriptsStr = flywithlua::scriptDir;
    size_t lastS = scriptsStr.find_last_of("/");
    return (lastS != std::string::npos) ? scriptsStr.substr(0, lastS) : ".";
}

static std::string EscapeLuaSingleQuotedString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() * 2);
    for (char c : value) {
        if (c == '\\' || c == '\'') {
            escaped.push_back('\\');
        }
        escaped.push_back(c);
    }
    return escaped;
}

static std::string EscapeJsonString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (unsigned char c : value) {
        switch (c) {
            case '\\': escaped.append("\\\\"); break;
            case '\"': escaped.append("\\\""); break;
            case '\b': escaped.append("\\b"); break;
            case '\f': escaped.append("\\f"); break;
            case '\n': escaped.append("\\n"); break;
            case '\r': escaped.append("\\r"); break;
            case '\t': escaped.append("\\t"); break;
            default:
                if (c < 0x20) {
                    char buffer[7];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    escaped.append(buffer);
                } else {
                    escaped.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return escaped;
}

static std::string BuildScriptLoadFailuresJson(const std::vector<FlyWithLuaScriptFailure>& failures) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < failures.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << "{\"fileName\":\"" << EscapeJsonString(failures[i].fileName)
            << "\",\"message\":\"" << EscapeJsonString(failures[i].message) << "\"}";
    }
    out << "]";
    return out.str();
}

static void ReplaceAll(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return;
    }

    size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos) {
        text.replace(position, from.length(), to);
        position += to.length();
    }
}

static void RunLuaStringChunk(const std::string& code, const char* context) {
    if (code.empty() || !L || !flywithlua::LuaIsRunning) {
        return;
    }

    if (luaL_dostring(L, code.c_str()) != 0) {
        std::string errorMessage = lua_tostring(L, -1);
        lua_pop(L, 1);
        XPLMDebugString((std::string("FlyWithLua-Mac Lua Error (") + context + "): " + errorMessage + "\n").c_str());
    }
}

static bool LuaGraphicsCallAllowed(const char* functionName) {
    if (!flywithlua::WeAreNotInDrawingState) {
        return true;
    }

    flywithlua::logMsg(
        logToDevCon,
        std::string("FlyWithLua Error: ") + functionName +
            " cannot be executed outside a drawing loopback. Put the function call inside the do_every_draw() string argument to solve this issue."
    );
    flywithlua::LuaIsRunning = false;
    return false;
}

static bool LuaGraphicsHasNumbers(lua_State* state, int firstIndex, int count, const char* functionName) {
    for (int index = firstIndex; index < firstIndex + count; ++index) {
        if (!lua_isnumber(state, index)) {
            flywithlua::logMsg(
                logToDevCon,
                std::string("FlyWithLua Error: Wrong arguments to function ") + functionName + "."
            );
            flywithlua::LuaIsRunning = false;
            return false;
        }
    }
    return true;
}

static int LuaXPLMSetGraphicsState(lua_State* state) {
    if (!LuaGraphicsCallAllowed("XPLMSetGraphicsState()") ||
        !LuaGraphicsHasNumbers(state, 1, 7, "XPLMSetGraphicsState")) {
        return 0;
    }

    XPLMSetGraphicsState(
        static_cast<int>(lua_tointeger(state, 1)),
        static_cast<int>(lua_tointeger(state, 2)),
        static_cast<int>(lua_tointeger(state, 3)),
        static_cast<int>(lua_tointeger(state, 4)),
        static_cast<int>(lua_tointeger(state, 5)),
        static_cast<int>(lua_tointeger(state, 6)),
        static_cast<int>(lua_tointeger(state, 7))
    );
    return 0;
}

static int LuaGLBegin(lua_State* state, GLenum mode, const char* functionName) {
    if (!LuaGraphicsCallAllowed(functionName)) {
        return 0;
    }

    glBegin(mode);
    return 0;
}

static int LuaGLBegin_POINTS(lua_State* state) {
    return LuaGLBegin(state, GL_POINTS, "glBegin_POINTS()");
}

static int LuaGLBegin_LINES(lua_State* state) {
    return LuaGLBegin(state, GL_LINES, "glBegin_LINES()");
}

static int LuaGLBegin_LINE_STRIP(lua_State* state) {
    return LuaGLBegin(state, GL_LINE_STRIP, "glBegin_LINE_STRIP()");
}

static int LuaGLBegin_LINE_LOOP(lua_State* state) {
    return LuaGLBegin(state, GL_LINE_LOOP, "glBegin_LINE_LOOP()");
}

static int LuaGLBegin_POLYGON(lua_State* state) {
    return LuaGLBegin(state, GL_POLYGON, "glBegin_POLYGON()");
}

static int LuaGLBegin_TRIANGLES(lua_State* state) {
    return LuaGLBegin(state, GL_TRIANGLES, "glBegin_TRIANGLES()");
}

static int LuaGLBegin_TRIANGLE_STRIP(lua_State* state) {
    return LuaGLBegin(state, GL_TRIANGLE_STRIP, "glBegin_TRIANGLE_STRIP()");
}

static int LuaGLBegin_TRIANGLE_FAN(lua_State* state) {
    return LuaGLBegin(state, GL_TRIANGLE_FAN, "glBegin_TRIANGLE_FAN()");
}

static int LuaGLBegin_QUADS(lua_State* state) {
    return LuaGLBegin(state, GL_QUADS, "glBegin_QUADS()");
}

static int LuaGLBegin_QUAD_STRIP(lua_State* state) {
    return LuaGLBegin(state, GL_QUAD_STRIP, "glBegin_QUAD_STRIP()");
}

static int LuaGLEnd(lua_State* state) {
    if (!LuaGraphicsCallAllowed("glEnd()")) {
        return 0;
    }

    glEnd();
    return 0;
}

static int LuaGLVertex2f(lua_State* state) {
    if (!LuaGraphicsCallAllowed("glVertex2f()") ||
        !LuaGraphicsHasNumbers(state, 1, 2, "glVertex2f")) {
        return 0;
    }

    glVertex2f(
        static_cast<float>(lua_tonumber(state, 1)),
        static_cast<float>(lua_tonumber(state, 2))
    );
    return 0;
}

static int LuaGLVertex3f(lua_State* state) {
    if (!LuaGraphicsCallAllowed("glVertex3f()") ||
        !LuaGraphicsHasNumbers(state, 1, 3, "glVertex3f")) {
        return 0;
    }

    glVertex3f(
        static_cast<float>(lua_tonumber(state, 1)),
        static_cast<float>(lua_tonumber(state, 2)),
        static_cast<float>(lua_tonumber(state, 3))
    );
    return 0;
}

static int LuaGLLineWidth(lua_State* state) {
    if (!LuaGraphicsCallAllowed("glLineWidth()") ||
        !LuaGraphicsHasNumbers(state, 1, 1, "glLineWidth")) {
        return 0;
    }

    glLineWidth(static_cast<float>(lua_tonumber(state, 1)));
    return 0;
}

static int LuaGLColor3f(lua_State* state) {
    if (!LuaGraphicsCallAllowed("glColor3f()") ||
        !LuaGraphicsHasNumbers(state, 1, 3, "glColor3f")) {
        return 0;
    }

    glColor3f(
        static_cast<float>(lua_tonumber(state, 1)),
        static_cast<float>(lua_tonumber(state, 2)),
        static_cast<float>(lua_tonumber(state, 3))
    );
    return 0;
}

static int LuaGLColor4f(lua_State* state) {
    if (!LuaGraphicsCallAllowed("glColor4f()") ||
        !LuaGraphicsHasNumbers(state, 1, 4, "glColor4f")) {
        return 0;
    }

    glColor4f(
        static_cast<float>(lua_tonumber(state, 1)),
        static_cast<float>(lua_tonumber(state, 2)),
        static_cast<float>(lua_tonumber(state, 3)),
        static_cast<float>(lua_tonumber(state, 4))
    );
    return 0;
}

static int LuaGLRectf(lua_State* state) {
    if (!LuaGraphicsCallAllowed("glRectf()") ||
        !LuaGraphicsHasNumbers(state, 1, 4, "glRectf")) {
        return 0;
    }

    float x1 = static_cast<float>(lua_tonumber(state, 1));
    float y1 = static_cast<float>(lua_tonumber(state, 2));
    float x2 = static_cast<float>(lua_tonumber(state, 3));
    float y2 = static_cast<float>(lua_tonumber(state, 4));

    if (x1 < x2) {
        std::swap(x1, x2);
    }
    if (y1 > y2) {
        std::swap(y1, y2);
    }

    glRectf(x1, y1, x2, y2);
    return 0;
}

static bool HasFlyWithLuaScriptExtension(const std::string& fileName) {
    std::string lowerName;
    lowerName.reserve(fileName.size());
    for (unsigned char c : fileName) {
        lowerName.push_back(static_cast<char>(std::tolower(c)));
    }

    auto hasSuffix = [&](const char* suffix) {
        const std::string suffixStr = suffix;
        return lowerName.size() >= suffixStr.size() &&
            lowerName.compare(lowerName.size() - suffixStr.size(), suffixStr.size(), suffixStr) == 0;
    };

    return hasSuffix(".lua") || hasSuffix(".fwl") || hasSuffix(".lua64") || hasSuffix(".lua32");
}

static bool IsExistingDirectory(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool IsRegularFile(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static bool ShouldSkipScriptDirectory(const std::string& directoryName) {
    std::string lowerName;
    lowerName.reserve(directoryName.size());
    for (unsigned char c : directoryName) {
        lowerName.push_back(static_cast<char>(std::tolower(c)));
    }

    return !directoryName.empty() &&
        (directoryName[0] == '.' || lowerName.find("disabled") != std::string::npos);
}

static void CollectScriptFilesRecursive(const std::string& directory, const std::string& relativePrefix, std::vector<std::string>& files) {
    DIR* dir = opendir(directory.c_str());
    if (!dir) {
        XPLMDebugString(("FlyWithLua-Mac Warning: Failed to read directory: " + directory + "\n").c_str());
        return;
    }

    std::vector<std::string> subdirectories;
    while (dirent* entry = readdir(dir)) {
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        std::string name = entry->d_name;
        std::string fullPath = directory + "/" + name;
        if (IsRegularFile(fullPath)) {
            if (HasFlyWithLuaScriptExtension(name)) {
                if (relativePrefix.empty()) {
                    files.push_back(name);
                } else {
                    files.push_back(relativePrefix + "/" + name);
                }
            }
        } else if (IsExistingDirectory(fullPath) && !ShouldSkipScriptDirectory(name)) {
            subdirectories.push_back(name);
        }
    }

    closedir(dir);

    std::sort(subdirectories.begin(), subdirectories.end());
    for (const std::string& subdirectory : subdirectories) {
        std::string nextPrefix = relativePrefix.empty() ? subdirectory : relativePrefix + "/" + subdirectory;
        CollectScriptFilesRecursive(directory + "/" + subdirectory, nextPrefix, files);
    }
}

static bool ResolveScriptsDirectory() {
    char systemPath[512] = {0};
    XPLMGetSystemPath(systemPath);
    if (systemPath[0] != '\0') {
        std::string canonical = std::string(systemPath) + "/Resources/plugins/FlyWithLua/Scripts";
        if (IsExistingDirectory(canonical)) {
            flywithlua::scriptDir = canonical;
            flywithlua::quarantineDir = std::string(systemPath) + "/Resources/plugins/FlyWithLua/Scripts (Quarantine)";
            return true;
        }
    }

    char pluginPath[512] = {0};
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, pluginPath, nullptr, nullptr);
    if (pluginPath[0] != '\0') {
        std::string path(pluginPath);
        size_t slash = path.find_last_of("/");
        if (slash != std::string::npos) {
            std::string currentDir = path.substr(0, slash);

            for (int i = 0; i < 5; ++i) {
                std::string testPath = currentDir + "/Scripts";
                if (IsExistingDirectory(testPath)) {
                    flywithlua::scriptDir = testPath;
                    flywithlua::quarantineDir = currentDir + "/Scripts (Quarantine)";
                    return true;
                }
                size_t last = currentDir.find_last_of("/");
                if (last == std::string::npos) {
                    break;
                }
                currentDir = currentDir.substr(0, last);
            }
        }
    }

    return false;
}

static int LuaDoEveryDrawCallback(lua_State* state) {
    if (!lua_isstring(state, 1)) {
        return 0;
    }
    gDrawCommand.append(lua_tostring(state, 1)).append("\n");
    return 0;
}

static int LuaDoEveryFrameCallback(lua_State* state) {
    if (!lua_isstring(state, 1)) {
        return 0;
    }
    gEveryFrameCommand.append(lua_tostring(state, 1)).append("\n");
    return 0;
}

static int LuaDoOftenCallback(lua_State* state) {
    if (!lua_isstring(state, 1)) {
        return 0;
    }
    gOftenCommand.append(lua_tostring(state, 1)).append("\n");
    return 0;
}

static int LuaDoSometimesCallback(lua_State* state) {
    if (!lua_isstring(state, 1)) {
        return 0;
    }
    gSometimesCommand.append(lua_tostring(state, 1)).append("\n");
    return 0;
}

static int LuaDoOnExitCallback(lua_State* state) {
    if (!lua_isstring(state, 1)) {
        return 0;
    }
    gOnExitCommand.append(lua_tostring(state, 1)).append("\n");
    return 0;
}

static int LuaDoOnMouseClickCallback(lua_State* state) {
    if (!lua_isstring(state, 1)) {
        return 0;
    }
    gMouseClickCommand.append(lua_tostring(state, 1)).append("\n");
    return 0;
}

static int LuaDoOnMouseWheelCallback(lua_State* state) {
    if (!lua_isstring(state, 1)) {
        return 0;
    }
    gMouseWheelCommand.append(lua_tostring(state, 1)).append("\n");
    return 0;
}

static void RegisterFlyWithLuaCompatibilityFunctions(lua_State* state) {
    lua_register(state, "do_every_draw", LuaDoEveryDrawCallback);
    lua_register(state, "do_every_frame", LuaDoEveryFrameCallback);
    lua_register(state, "do_often", LuaDoOftenCallback);
    lua_register(state, "do_sometimes", LuaDoSometimesCallback);
    lua_register(state, "do_on_exit", LuaDoOnExitCallback);
    lua_register(state, "do_on_mouse_click", LuaDoOnMouseClickCallback);
    lua_register(state, "do_on_mouse_wheel", LuaDoOnMouseWheelCallback);
    lua_register(state, "XPLMSetGraphicsState", LuaXPLMSetGraphicsState);
    lua_register(state, "glBegin_POINTS", LuaGLBegin_POINTS);
    lua_register(state, "glBegin_LINES", LuaGLBegin_LINES);
    lua_register(state, "glBegin_LINE_STRIP", LuaGLBegin_LINE_STRIP);
    lua_register(state, "glBegin_LINE_LOOP", LuaGLBegin_LINE_LOOP);
    lua_register(state, "glBegin_POLYGON", LuaGLBegin_POLYGON);
    lua_register(state, "glBegin_TRIANGLES", LuaGLBegin_TRIANGLES);
    lua_register(state, "glBegin_TRIANGLE_STRIP", LuaGLBegin_TRIANGLE_STRIP);
    lua_register(state, "glBegin_TRIANGLE_FAN", LuaGLBegin_TRIANGLE_FAN);
    lua_register(state, "glBegin_QUADS", LuaGLBegin_QUADS);
    lua_register(state, "glBegin_QUAD_STRIP", LuaGLBegin_QUAD_STRIP);
    lua_register(state, "glEnd", LuaGLEnd);
    lua_register(state, "glVertex2f", LuaGLVertex2f);
    lua_register(state, "glVertex3f", LuaGLVertex3f);
    lua_register(state, "glLineWidth", LuaGLLineWidth);
    lua_register(state, "glColor3f", LuaGLColor3f);
    lua_register(state, "glColor4f", LuaGLColor4f);
    lua_register(state, "glRectf", LuaGLRectf);
}

static void ResetLuaRuntimeState() {
    if (L) {
        ClearFlyWithLuaCommands();
        lua_close(L);
    }
    L = nullptr;
    lState = nullptr;
    flywithlua::FWLLua = nullptr;
    flywithlua::LuaIsRunning = false;
    gDrawCommand.clear();
    gEveryFrameCommand.clear();
    gOftenCommand.clear();
    gSometimesCommand.clear();
    gOnExitCommand.clear();
    gMouseClickCommand.clear();
    gMouseWheelCommand.clear();
    gSometimesAccumulator = 0.0f;
}

extern "C" void flywithlua_reload_scripts(void) {
    if (flywithlua::scriptDir.empty()) {
        XPLMDebugString("FlyWithLua-Mac Warning: Cannot reload scripts because the Scripts directory is unknown.\n");
        flywithlua_update_last_log_message("Scripts folder not found");
        return;
    }

    XPLMDebugString("FlyWithLua-Mac: Reloading scripts.\n");
    flywithlua_update_last_log_message("Reloading scripts...");
    flywithlua_update_script_count(0);
    flywithlua_clear_script_load_failures();

    RunLuaStringChunk(gOnExitCommand, "do_on_exit");
    flwnd::deinitFloatingWindowSupport();
    fmodint::deinitFmodSupport();
    ResetLuaRuntimeState();

    if (!InitializeLuaRuntime(false)) {
        XPLMDebugString("FlyWithLua-Mac Warning: Script reload failed.\n");
        flywithlua_update_last_log_message("Script reload failed");
        return;
    }

    flywithlua::LuaIsRunning = true;
    XPLMDebugString("FlyWithLua-Mac: Scripts reloaded.\n");
    flywithlua_update_last_log_message("Scripts reloaded");
}

static bool InitializeLuaRuntime(bool registerFlightLoop) {
    L = luaL_newstate();
    if (!L) {
        XPLMDebugString("FlyWithLua-Mac: Failed to initialize Lua state.\n");
        return false;
    }

    luaL_openlibs(L);
    if (!RegisterLuaBuiltinModule(L, "socket.core", luaopen_socket_core)) {
        XPLMDebugString("FlyWithLua-Mac Warning: Could not register built-in socket.core module.\n");
    }
    if (!RegisterLuaBuiltinModule(L, "mime.core", luaopen_mime_core)) {
        XPLMDebugString("FlyWithLua-Mac Warning: Could not register built-in mime.core module.\n");
    }
    flywithlua::FWLLua = L;
    flywithlua::LuaIsRunning = true;
    lState = L;

    lua_pushstring(L, "APL");
    lua_setglobal(L, "SYSTEM");

    lua_pushstring(L, "/");
    lua_setglobal(L, "DIRECTORY_SEPARATOR");

    lua_pushstring(L, flywithlua::scriptDir.c_str());
    lua_setglobal(L, "SCRIPT_DIRECTORY");

    std::string mainDir = GetMainDirectoryFromScripts();

    lua_pushstring(L, mainDir.c_str());
    lua_setglobal(L, "PLUGIN_MAIN_DIRECTORY");

    lua_pushstring(L, (mainDir + "/Internals/").c_str());
    lua_setglobal(L, "INTERNALS_DIRECTORY");

    lua_pushstring(L, (mainDir + "/Modules/").c_str());
    lua_setglobal(L, "MODULES_DIRECTORY");

    register_swift_bridge(L);
    lua_register(L, "create_command", LuaCreateCommandCallback);

    int xplaneVersion = 0;
    int sdkVersion = 0;
    int hostId = 0;
    XPLMGetVersions(&xplaneVersion, &sdkVersion, &hostId);
    lua_pushinteger(L, xplaneVersion);
    lua_setglobal(L, "XPLANE_VERSION");

    int screenLeft = 0;
    int screenTop = 0;
    int screenRight = 0;
    int screenBottom = 0;
    XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);
    const int screenWidth = screenRight - screenLeft;
    const int screenHeight = screenTop - screenBottom;
    lua_pushinteger(L, screenWidth);
    lua_setglobal(L, "SCREEN_WIDTH");
    lua_pushinteger(L, screenHeight);
    lua_setglobal(L, "SCREEN_HEIGHT");
    lua_pushinteger(L, screenHeight);
    lua_setglobal(L, "SCREEN_HIGHT");

    const std::string internalsDir = EscapeLuaSingleQuotedString(mainDir + "/Internals");
    const std::string modulesDir = EscapeLuaSingleQuotedString(mainDir + "/Modules");

    XPLMDebugString(("FlyWithLua-Mac: Setting package.path to include " + mainDir + "/Internals and " + mainDir + "/Modules\n").c_str());
    std::string initScript = R"lua(
package.path = package.path .. ';__INTERNALS__/?.lua;__INTERNALS__/?/init.lua;__MODULES__/?.lua;__MODULES__/?/init.lua'

graphics = require("graphics")

function logMsg(s)
    mac_native.log_msg(tostring(s))
end

function XSBSpeakString(...)
    local parts = {...}
    for i = 1, #parts do
        parts[i] = tostring(parts[i])
    end
    mac_native.log_msg(table.concat(parts, " "))
end

function XPLMSpeakString(...)
    XSBSpeakString(...)
end

function print(...)
    XSBSpeakString(...)
end

function hid_open()
    return nil
end

function add_macro()
end

function create_positive_edge_flip()
end

function create_HID_table()
    return {}, 0
end

local band = bit.band

local fwl_shared_datarefs = {}

local function fwl_make_shared_proxy(entry)
    local t = {
        refname = entry.refname,
        reference = entry,
        reftype = entry.reftype,
    }
    setmetatable(t, DATAREF_META_TABLE)
    return t
end

function define_shared_dataref(ref_name, ref_type)
    local entry = fwl_shared_datarefs[ref_name]
    if entry == nil then
        entry = {
            refname = ref_name,
            reftype = ref_type,
            _shared_storage = {},
        }
        fwl_shared_datarefs[ref_name] = entry
    else
        entry.reftype = ref_type or entry.reftype
    end

    return entry
end

function define_shared_DataRef(ref_name, ref_type)
    return define_shared_dataref(ref_name, ref_type)
end

local function fwl_color_from_name(name)
    local color = tostring(name or ""):lower()
    if color == "black" then return 0, 0, 0, 1 end
    if color == "blue" then return 0, 0, 1, 1 end
    if color == "cyan" then return 0, 1, 1, 1 end
    if color == "gray" or color == "grey" then return 0.6, 0.6, 0.6, 1 end
    if color == "green" then return 0, 1, 0, 1 end
    if color == "magenta" then return 1, 0, 1, 1 end
    if color == "orange" then return 1, 0.5, 0, 1 end
    if color == "red" then return 1, 0, 0, 1 end
    if color == "white" then return 1, 1, 1, 1 end
    if color == "yellow" then return 1, 1, 0, 1 end
    return 1, 1, 1, 1
end

local function fwl_set_color_from_name(name)
    local r, g, b, a = fwl_color_from_name(name)
    graphics.set_color(r, g, b, a)
end

local function fwl_draw_font(fontName, x, y, text, r, g, b, a)
    if r ~= nil and g ~= nil and b ~= nil and a ~= nil then
        graphics.set_color(r, g, b, a)
    end
    mac_native.draw_string(x, y, tostring(text), fontName)
end

function peek(reference, reftype, key)
    if type(reference) == "table" then
        local storage = rawget(reference, "_shared_storage")
        if storage ~= nil then
            return storage[key]
        end

        local path = rawget(reference, "refname")
        if path ~= nil then
            return get(path, key)
        end
    elseif type(reference) == "string" then
        return get(reference, key)
    end

    return nil
end

function poke(reference, reftype, key, value)
    if type(reference) == "table" then
        local storage = rawget(reference, "_shared_storage")
        if storage ~= nil then
            storage[key] = value
            return
        end

        local path = rawget(reference, "refname")
        if path ~= nil then
            set(path, value, key)
            return
        end
    elseif type(reference) == "string" then
        set(reference, value, key)
    end
end

DATAREF_META_TABLE = {}
DATAREF_META_TABLE.__index = function(t, key)
    return peek(t.reference, t.reftype, key)
end
DATAREF_META_TABLE.__newindex = function(t, key, value)
    poke(t.reference, t.reftype, key, value)
end

function dataref_table(ref_name)
    local shared = fwl_shared_datarefs[ref_name]
    if shared ~= nil then
        return fwl_make_shared_proxy(shared)
    end

    local t = {
        refname = ref_name,
        reference = ref_name,
        reftype = nil,
    }
    setmetatable(t, DATAREF_META_TABLE)
    return t
end

function create_dataref_table(ref_name, ref_type)
    define_shared_dataref(ref_name, ref_type)
    return dataref_table(ref_name)
end

local fwl_datarefs = {}

function get_DataRef_binding(name)
    local binding = fwl_datarefs[name]
    if not binding then
        return nil, nil, nil, nil
    end

    if (binding.type or 0) == 0 and binding.path ~= nil then
        local ref = XPLMFindDataRef(binding.path)
        if ref ~= nil then
            binding.type = XPLMGetDataRefTypes(ref)
        end
    end

    return binding.path, binding.index, not binding.writable, binding.type or 0
end

local function fwl_dataref_type_label(typeMask)
    local labels = {}
    if band(typeMask, 1) ~= 0 then labels[#labels + 1] = "integer" end
    if band(typeMask, 2) ~= 0 then labels[#labels + 1] = "float" end
    if band(typeMask, 4) ~= 0 then labels[#labels + 1] = "double" end
    if band(typeMask, 8) ~= 0 then labels[#labels + 1] = "float array" end
    if band(typeMask, 16) ~= 0 then labels[#labels + 1] = "integer array" end
    if band(typeMask, 32) ~= 0 then labels[#labels + 1] = "data (string)" end

    if #labels == 0 then
        return nil
    end

    return table.concat(labels, ", ")
end

function measure_string(text, fontName)
    return mac_native.measure_string(tostring(text), fontName or "Helvetica_12")
end

function draw_string_Helvetica_10(x, y, text, r, g, b, a)
    return fwl_draw_font("Helvetica_10", x, y, text, r, g, b, a)
end

function draw_string_Helvetica_12(x, y, text, r, g, b, a)
    return fwl_draw_font("Helvetica_12", x, y, text, r, g, b, a)
end

function draw_string_Helvetica_18(x, y, text, r, g, b, a)
    return fwl_draw_font("Helvetica_18", x, y, text, r, g, b, a)
end

function draw_string_Times_Roman_10(x, y, text, r, g, b, a)
    return fwl_draw_font("Times_Roman_10", x, y, text, r, g, b, a)
end

function draw_string_Times_Roman_24(x, y, text, r, g, b, a)
    return fwl_draw_font("Times_Roman_24", x, y, text, r, g, b, a)
end

function draw_string(x, y, arg3, arg4, arg5, arg6, arg7, arg8)
    if type(arg3) == "number" then
        local fontsize = arg3
        local text = arg4
        if arg5 ~= nil and arg6 ~= nil and arg7 ~= nil and arg8 ~= nil then
            graphics.set_color(arg5, arg6, arg7, arg8)
        end

        if fontsize == 10 then
            return draw_string_Helvetica_10(x, y, text)
        elseif fontsize == 12 then
            return draw_string_Helvetica_12(x, y, text)
        elseif fontsize == 18 then
            return draw_string_Helvetica_18(x, y, text)
        else
            return draw_string_Helvetica_12(x, y, text)
        end
    end

    if type(arg4) == "string" then
        fwl_set_color_from_name(arg4)
        return draw_string_Helvetica_12(x, y, arg3)
    end

    if arg4 ~= nil and arg5 ~= nil and arg6 ~= nil and arg7 ~= nil then
        graphics.set_color(arg4, arg5, arg6, arg7)
    else
        graphics.set_color(1, 1, 1, 1)
    end

    return draw_string_Helvetica_12(x, y, arg3)
end

function command_once(commandName)
    return mac_native.command_once(commandName)
end

function get(n, index)
    return mac_native.get_dataref(n, index)
end

function set(n, v, index)
    return mac_native.set_dataref(n, v, index)
end
local globalMeta = getmetatable(_G) or {}
local previousIndex = globalMeta.__index
local previousNewIndex = globalMeta.__newindex

globalMeta.__index = function(t, k)
    local binding = fwl_datarefs[k]
    if binding then
        return get(binding.path, binding.index)
    end

    if type(previousIndex) == "function" then
        return previousIndex(t, k)
    end

    if type(previousIndex) == "table" then
        return previousIndex[k]
    end

    return nil
end

globalMeta.__newindex = function(t, k, v)
    local binding = fwl_datarefs[k]
    if binding then
        if binding.writable then
            set(binding.path, v, binding.index)
        else
            logMsg("DataRef is readonly: " .. tostring(k))
        end
        return
    end

    if type(previousNewIndex) == "function" then
        previousNewIndex(t, k, v)
        return
    end

    if type(previousNewIndex) == "table" then
        previousNewIndex[k] = v
        return
    end

    rawset(t, k, v)
end

setmetatable(_G, globalMeta)

function dataref(name, path, mode, index)
    local ref = XPLMFindDataRef(path)
    local refType = 0
    if ref ~= nil then
        refType = XPLMGetDataRefTypes(ref)
    end

    fwl_datarefs[name] = {
        path = path,
        writable = mode == "writable",
        index = index,
        type = refType
    }
end
)lua";
    ReplaceAll(initScript, "__INTERNALS__", internalsDir);
    ReplaceAll(initScript, "__MODULES__", modulesDir);

    if (luaL_dostring(L, initScript.c_str())) {
        XPLMDebugString(("FlyWithLua-Mac Lua Init Error: " + std::string(lua_tostring(L, -1)) + "\n").c_str());
        lua_pop(L, 1);
    }
    RegisterFlyWithLuaCompatibilityFunctions(L);

    char xplanePath[512];
    XPLMGetSystemPath(xplanePath);
    lua_pushstring(L, xplanePath);
    lua_setglobal(L, "SYSTEM_DIRECTORY");

    flwnd::initFloatingWindowSupport();

    fmodint::RegisterFmodFunctionsToLua(L);

    flywithlua::process_read_ini_file();

    if (registerFlightLoop) {
        XPLMRegisterFlightLoopCallback(FlightLoopCallback, -1.0f, nullptr);
        XPLMRegisterDrawCallback(FlyWithLuaDrawCallback, xplm_Phase_Window, 0, (void*) "FlyWithLua-MacScriptDraw");
    }

    gSuppressMacroMenuRefresh = true;
    gMacroMenuNeedsRefresh = false;
    flywithlua::ReadAllScriptFiles();
    gSuppressMacroMenuRefresh = false;
    if (gMacroMenuNeedsRefresh) {
        RefreshFlyWithLuaMacrosMenu();
    }
    return true;
}

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
        logMsg(logToDevCon, "Loading scripts from: " + scriptDir);
        int loadedScripts = 0;
        int failedScripts = 0;
        std::vector<FlyWithLuaScriptFailure> failures;

        std::vector<std::string> fileNames;
        flywithlua_clear_script_load_failures();

        if (!IsExistingDirectory(scriptDir)) {
            logMsg(logToDevCon, "Failed to read directory: " + scriptDir);
            flywithlua_update_script_count(0);
            flywithlua_clear_script_load_failures();
            flywithlua_update_last_log_message("Failed to read scripts folder");
            return false;
        }

        CollectScriptFilesRecursive(scriptDir, "", fileNames);
        std::sort(fileNames.begin(), fileNames.end());

        if (fileNames.empty()) {
            logMsg(logToDevCon, "No script files found in: " + scriptDir);
            flywithlua_update_script_count(0);
            flywithlua_clear_script_load_failures();
            flywithlua_update_last_log_message("No scripts found");
            return true;
        }

        for (const std::string& fileName : fileNames) {
            std::string fullPath = scriptDir + "/" + fileName;
            logMsg(logToDevCon, "Loading script: " + fileName);
            if (luaL_dofile(FWLLua, fullPath.c_str())) {
                const char* luaError = lua_tostring(FWLLua, -1);
                std::string errorMessage = luaError ? luaError : "Unknown Lua error";
                logMsg(logToDevCon, "Error loading " + fileName + ": " + errorMessage);
                lua_pop(FWLLua, 1);
                failures.push_back({fileName, errorMessage});
                ++failedScripts;
            } else {
                ++loadedScripts;
            }
        }

        flywithlua_update_script_count(loadedScripts);
        flywithlua_update_script_load_results(BuildScriptLoadFailuresJson(failures).c_str());
        flywithlua_update_last_log_message((std::string("Loaded scripts: ") + std::to_string(loadedScripts) + ", failed: " + std::to_string(failedScripts)).c_str());
        return true;
    }
}

float FlightLoopCallback(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void * inRefcon) {
    if (!flywithlua::LuaIsRunning) return 0.0f;

    // Update FMOD and Floating Windows
    fmodint::fmod_data_update();
    flwnd::onFlightLoop();

    RunLuaStringChunk(gEveryFrameCommand, "do_every_frame");
    RunLuaStringChunk(gOftenCommand, "do_often");
    gSometimesAccumulator += inElapsedSinceLastCall;
    if (gSometimesAccumulator >= 2.0f) {
        gSometimesAccumulator = 0.0f;
        RunLuaStringChunk(gSometimesCommand, "do_sometimes");
    }

    if (gAltitudeDataRef) {
        flywithlua_update_current_altitude(XPLMGetDatad(gAltitudeDataRef));
    }

    // Placeholder for other periodic tasks
    flywithlua::CopyDataRefsToLua();
    flywithlua::CopyDataRefsToXPlane();

    return -1.0f; // Run every frame
}

int FlyWithLuaDrawCallback(XPLMDrawingPhase /*inPhase*/, int /*inIsBefore*/, void * /*inRefcon*/) {
    if (!flywithlua::LuaIsRunning) {
        return 1;
    }

    int mouseX = 0;
    int mouseY = 0;
    int screenWidth = 0;
    int screenHeight = 0;
    XPLMGetMouseLocation(&mouseX, &mouseY);
    XPLMGetScreenSize(&screenWidth, &screenHeight);

    lua_pushinteger(L, mouseX);
    lua_setglobal(L, "MOUSE_X");
    lua_pushinteger(L, mouseY);
    lua_setglobal(L, "MOUSE_Y");
    lua_pushinteger(L, screenWidth);
    lua_setglobal(L, "SCREEN_WIDTH");
    lua_pushinteger(L, screenHeight);
    lua_setglobal(L, "SCREEN_HIGHT");
    lua_pushinteger(L, screenHeight);
    lua_setglobal(L, "SCREEN_HEIGHT");

    // Establish the 2D state expected by legacy FlyWithLua drawing scripts.
    XPLMSetGraphicsState(0, 0, 0, 1, 1, 0, 0);
    flywithlua::WeAreNotInDrawingState = false;
    RunLuaStringChunk(gDrawCommand, "do_every_draw");
    flywithlua::WeAreNotInDrawingState = true;
    return 1;
}

extern "C" void register_swift_bridge(lua_State* L);

PLUGIN_API int XPluginStart(char * outName, char * outSig, char * outDesc) {
    strcpy(outName, "FlyWithLua-Mac");
    strcpy(outSig, "com.goodbad-web.flywithlua_mac");
    strcpy(outDesc, "A modernized, macOS-optimized FlyWithLua plugin.");

    // Enable native paths (UTF-8 /)
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);

    if (!ResolveScriptsDirectory()) {
        XPLMDebugString("FlyWithLua-Mac Warning: Could not find Scripts directory relative to plugin.\n");
    }

    gAltitudeDataRef = XPLMFindDataRef("sim/flightmodel/position/elevation");
    if (!gAltitudeDataRef) {
        XPLMDebugString("FlyWithLua-Mac Warning: Could not find altitude DataRef.\n");
    }

    if (!InitializeLuaRuntime(true)) {
        return 0;
    }

    RegisterFlyWithLuaMenu();

    XPLMDebugString("FlyWithLua-Mac: Successfully started and initialized Lua.\n");

    return 1;
}

PLUGIN_API void XPluginStop(void) {
    if (L) {
        XPLMUnregisterFlightLoopCallback(FlightLoopCallback, nullptr);
        XPLMUnregisterDrawCallback(FlyWithLuaDrawCallback, xplm_Phase_Window, 0, (void*) "FlyWithLua-MacScriptDraw");
        RunLuaStringChunk(gOnExitCommand, "do_on_exit");
        
        flwnd::deinitFloatingWindowSupport();
        fmodint::fmod_uninitialize();
        UnregisterFlyWithLuaMenu();
        ClearFlyWithLuaCommands();
        flywithlua_update_script_count(0);
        flywithlua_clear_script_load_failures();
        
        lua_close(L);
        L = nullptr;
        lState = nullptr;
        flywithlua::FWLLua = nullptr;
        flywithlua::LuaIsRunning = false;
        gAltitudeDataRef = nullptr;
        XPLMDebugString("FlyWithLua-Mac: Stopped.\n");
    }
}

PLUGIN_API void XPluginDisable(void) {
    flywithlua::LuaIsRunning = false;
    flywithlua_update_script_count(0);
    flywithlua_clear_script_load_failures();
    UpdateFlyWithLuaMenuEnabled(false);
}

PLUGIN_API int XPluginEnable(void) {
    flywithlua::LuaIsRunning = true;
    UpdateFlyWithLuaMenuEnabled(true);
    return 1;
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID /*inFrom*/, int inMsg, void * /*inParam*/) {
    switch (inMsg) {
        case XPLM_MSG_FMOD_BANK_LOADED:
            // X-Plane's FMOD buses are valid only after the corresponding bank
            // has been loaded. Initialization is intentionally deferred until
            // this notification instead of XPluginStart.
            fmodint::fmod_initialization();
            break;
        case XPLM_MSG_FMOD_BANK_UNLOADING:
            // X-Plane may rebuild the FMOD system during a bank reload. Drop
            // every handle before that happens so the next BANK_LOADED message
            // can rebuild the FlyWithLua groups against the new system.
            fmodint::fmod_uninitialize();
            break;
        default:
            break;
    }
}
