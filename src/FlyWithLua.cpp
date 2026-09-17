#include "FlyWithLua.h"
#include "XPLMPlugin.h"
#include "XPLMUtilities.h"
#include "XPLMProcessing.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMDataAccess.h"
#include "XPLMMenus.h"
#include "XPLMPlanes.h"
#include "FloatingWindows/FLWIntegration.h"
#include "Fmod/FmodIntegration.h"
#include "hidapi/hidapi.h"
#include "Native3jFPS12/ThreeJFPSNative.h"
#include "Graphics/PanelGraphicsBackend.h"
#include "Graphics/PanelGraphicsLua.h"
#include "third_party/iniReader/inireader.h"
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
#include <cmath>
#include <fstream>
#include <dirent.h>
#include <memory>
#include <sstream>
#include <array>
#include <sys/stat.h>
#include <cerrno>
#include <unordered_map>
#include <unordered_set>

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
static int gFlyWithLuaScriptsMenuItem = -1;
static XPLMMenuID gFlyWithLuaScriptsMenu = nullptr;
static XPLMCommandRef gFlyWithLuaCommand = nullptr;
static XPLMDataRef gAltitudeDataRef = nullptr;
static XPLMDataRef gJoystickButtonDataRef = nullptr;
static XPLMDataRef gPlaneICAODataRef = nullptr;
static XPLMDataRef gPlaneTailNumberDataRef = nullptr;
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
    std::string callback;
    std::string phase;
    std::string state;
    std::string backend;
};
struct FlyWithLuaScriptMenuEntry {
    std::string relativePath;
    bool enabled = false;
    bool conflict = false;
    int menuItemIndex = -1;
};
struct FlyWithLuaScriptMenuGroup {
    std::string key;
    XPLMMenuID menu = nullptr;
    int menuItemIndex = -1;
};
static std::vector<std::unique_ptr<FlyWithLuaScriptMenuEntry>> gFlyWithLuaScriptMenuEntries;
static std::vector<std::unique_ptr<FlyWithLuaScriptMenuGroup>> gFlyWithLuaScriptMenuGroups;
struct FlyWithLuaCommandBinding {
    std::string name;
    std::string description;
    std::string beginCode;
    std::string continueCode;
    std::string endCode;
    std::string activateCode;
    std::string deactivateCode;
    bool isLegacyMacro = false;
    bool isSwitch = false;
    bool isActive = false;
    int menuItemIndex = -1;
    XPLMCommandRef commandRef = nullptr;
};
static std::unordered_map<std::string, std::unique_ptr<FlyWithLuaCommandBinding>> gFlyWithLuaCommandBindings;
struct FlyWithLuaPositiveEdgeFlip {
    int button = 0;
    XPLMDataRef dataRef = nullptr;
    XPLMDataTypeID dataType = xplmType_Unknown;
    int index = 0;
    int offInt = 0;
    int onInt = 1;
    float offFloat = 0.0f;
    float onFloat = 1.0f;
    double offDouble = 0.0;
    double onDouble = 1.0;
    bool lastPressed = false;
};
enum class LuaCallbackKind : std::size_t {
    Draw = 0,
    CompatPanelDraw,
    PanelDraw,
    EveryFrame,
    Often,
    Sometimes,
    OnExit,
    MouseClick,
    MouseWheel,
    Count,
};

struct LuaCallbackEntries {
    std::string source;
    int chunkRef = LUA_NOREF;
    bool disabled = false;
    bool errorReported = false;

    bool empty() const { return source.empty(); }
};

struct LuaScriptCallbacks {
    std::array<LuaCallbackEntries, static_cast<std::size_t>(LuaCallbackKind::Count)> entries;

    LuaCallbackEntries& forKind(LuaCallbackKind kind) {
        return entries[static_cast<std::size_t>(kind)];
    }
};

struct LuaScriptRecord {
    flywithlua::LuaScriptId id = flywithlua::kSystemLuaScriptId;
    std::string fileName;
    bool loaded = false;
    bool quarantined = false;
    bool errorReported = false;
};

static std::vector<FlyWithLuaPositiveEdgeFlip> gPositiveEdgeFlips;
static std::vector<int> gJoystickButtonValues;
static std::vector<hid_device*> gOpenHIDDevices;
static bool gHIDInitialized = false;
static constexpr size_t kHIDReportBufferSize = 4096;
static std::string gDrawCommand;
static std::string gPanelApiDrawCommand;
static std::string gEveryFrameCommand;
static std::string gOftenCommand;
static std::string gSometimesCommand;
static std::string gOnExitCommand;
static std::string gMouseClickCommand;
static std::string gMouseWheelCommand;
static LuaCallbackEntries gDrawCallbacks;
static LuaCallbackEntries gPanelDrawCallbacks;
static LuaCallbackEntries gPanelApiDrawCallbacks;
static LuaCallbackEntries gEveryFrameCallbacks;
static LuaCallbackEntries gOftenCallbacks;
static LuaCallbackEntries gSometimesCallbacks;
static LuaCallbackEntries gOnExitCallbacks;
static LuaCallbackEntries gMouseClickCallbacks;
static LuaCallbackEntries gMouseWheelCallbacks;
static XPLMWindowID gMouseEventWindow = nullptr;
static int gMouseEventWindowLeft = 0;
static int gMouseEventWindowTop = 0;
static int gMouseEventWindowRight = 0;
static int gMouseEventWindowBottom = 0;
static bool gMouseEventWindowGeometryInitialized = false;
static bool gMouseEventWindowPanelGraphics = false;
static bool gMouseClickCaptured = false;
static float gOftenAccumulator = 0.0f;
static float gSometimesAccumulator = 0.0f;
static float gAltitudeAccumulator = 0.0f;
static constexpr float kOftenIntervalSeconds = 1.0f;
static constexpr float kSometimesIntervalSeconds = 10.0f;
static constexpr float kAltitudeIntervalSeconds = 0.1f;
static int gDiscoveredScriptCount = 0;
static int gLoadedScriptCount = 0;
static int gFailedScriptCount = 0;
static std::vector<FlyWithLuaScriptFailure> gScriptLoadFailures;
static bool gLoadingPanelScript = false;
static bool gUseIsolatedCallbackScope = true;
static flywithlua::LuaScriptId gCurrentLuaScriptId = flywithlua::kSystemLuaScriptId;
static bool gCurrentLuaPanelApiAllowed = false;
static flywithlua::LuaScriptId gNextLuaScriptId = 1;
static flywithlua::LuaScriptId gLoadingLuaScriptId = flywithlua::kSystemLuaScriptId;
static std::vector<flywithlua::LuaScriptId> gLuaScriptOrder;
static std::unordered_map<flywithlua::LuaScriptId, LuaScriptRecord> gLuaScriptRecords;
static std::unordered_map<flywithlua::LuaScriptId, LuaScriptCallbacks> gLuaScriptCallbacks;

extern "C" void flywithlua_toggle_window(void);
extern "C" void flywithlua_update_current_altitude(double altitude);
extern "C" void flywithlua_update_script_count(int count);
extern "C" void flywithlua_clear_script_load_failures(void);
extern "C" void flywithlua_update_script_load_results(const char* jsonPayload);
extern "C" void flywithlua_update_script_load_summary(int discovered, int loaded, int failed, const char* jsonPayload);
extern "C" void flywithlua_update_last_log_message(const char* message);
extern "C" void flywithlua_reload_scripts(void);
extern "C" int luaopen_LuaXML_lib(lua_State* L);
extern "C" int luaopen_socket_core(lua_State* L);
extern "C" int luaopen_mime_core(lua_State* L);
extern "C" int luaopen_socket_unix(lua_State* L);
extern "C" int luaopen_socket_serial(lua_State* L);
static bool InitializeLuaRuntime(bool registerFlightLoop);
static void UpdateLuaAircraftGlobals();
extern "C" void register_swift_bridge(lua_State* L);
float FlightLoopCallback(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void * inRefcon);
int FlyWithLuaDrawCallback(XPLMDrawingPhase inPhase, int inIsBefore, void * inRefcon);
static void RunLuaStringChunk(const std::string& code, const char* context);
static void InvalidateAllLuaCallbackChunks();
static void RunLuaCallbackEntries(LuaCallbackKind kind, LuaCallbackEntries& legacyCallbacks,
                                  const char* context);
static bool AppendLuaCallback(lua_State* state, std::string& debugCode,
                              LuaCallbackEntries& legacyCallbacks, LuaCallbackKind kind);
static bool HasEnabledLuaCallbacks(const LuaCallbackEntries& callbacks);
static bool HasEnabledLuaCallbacks(LuaCallbackKind kind, const LuaCallbackEntries& legacyCallbacks);
static LuaCallbackEntries& ScriptCallbackEntries(flywithlua::LuaScriptId scriptId,
                                                  LuaCallbackKind kind);
static void QuarantineLuaScript(flywithlua::LuaScriptId scriptId,
                                const char* context,
                                const std::string& errorMessage);
static void ResetLuaScriptRegistry();
static void ConfigureLuaCallbackScope();
static bool IsBundledPanelScript(const std::string& fileName);
static void UpdateLuaMouseGlobals();
static void UpdateMouseEventWindowGeometry();
static bool CreateMouseEventWindow();
static void DestroyMouseEventWindow();
static void FlyWithLuaMenuHandler(void*, void*);
static void FlyWithLuaMacroMenuHandler(void*, void*);
static void FlyWithLuaScriptsMenuHandler(void*, void*);
static bool HasFlyWithLuaScriptExtension(const std::string& fileName);
static bool IsExistingDirectory(const std::string& path);
static bool IsRegularFile(const std::string& path);
static bool IsSafeRelativeScriptPath(const std::string& path);
static bool EnsureDirectoryTree(const std::string& path);
static std::string GetMainDirectoryFromScripts();
static std::string FlyWithLuaLocalizedText(const char* english, const char* japanese);
static std::string FlyWithLuaScriptMenuGroupForPath(const std::string& path);
static void RefreshFlyWithLuaScriptsMenu();
static void WriteDebugFile();
static void ReturnQuarantinedScripts();
static void SetDeveloperMode(bool enabled);
static void SetVerboseLoggingMode(bool enabled);
static void RefreshFlyWithLuaMacrosMenu();
static void MarkFlyWithLuaMacrosMenuDirty();
static void CloseAllOpenHIDDevices();
static int LuaXPLMFindDataRef(lua_State* state);
static int LuaXPLMGetDataRefTypes(lua_State* state);
static int LuaXPLMGetDatai(lua_State* state);
static int LuaXPLMGetDataf(lua_State* state);
static int LuaXPLMGetDatad(lua_State* state);
static int LuaXPLMSetDatai(lua_State* state);
static int LuaXPLMSetDataf(lua_State* state);
static int LuaXPLMSetDatad(lua_State* state);
static int LuaXPLMGetDatavi(lua_State* state);
static int LuaXPLMGetDatavf(lua_State* state);
static int LuaXPLMSetDatavi(lua_State* state);
static int LuaXPLMSetDatavf(lua_State* state);
static int LuaCreateHIDTable(lua_State* state);
static int LuaHIDOpen(lua_State* state);
static int LuaHIDOpenPath(lua_State* state);
static int LuaHIDClose(lua_State* state);
static int LuaHIDWrite(lua_State* state);
static int LuaHIDRead(lua_State* state);
static int LuaHIDReadTimeout(lua_State* state);
static int LuaHIDSetNonblocking(lua_State* state);
static int LuaHIDSendFeatureReport(lua_State* state);
static int LuaHIDSendFilledFeatureReport(lua_State* state);
static int LuaHIDGetFeatureReport(lua_State* state);
static int LuaAddMacro(lua_State* state);
static int LuaActivateMacro(lua_State* state);
static int LuaDeactivateMacro(lua_State* state);
static int LuaCreatePositiveEdgeFlip(lua_State* state);
static void PollPositiveEdgeFlips();

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

static std::string ReadXPlaneStringDataRef(XPLMDataRef dataRef) {
    if (!dataRef) {
        return {};
    }

    std::vector<char> buffer(256, '\0');
    XPLMGetDatab(dataRef, buffer.data(), 0, static_cast<int>(buffer.size() - 1));
    buffer.back() = '\0';
    return std::string(buffer.data());
}

static void UpdateLuaAircraftGlobals() {
    if (!L) {
        return;
    }

    char aircraftFileName[256] = {0};
    char aircraftPath[512] = {0};
    XPLMGetNthAircraftModel(XPLM_USER_AIRCRAFT, aircraftFileName, aircraftPath);

    lua_pushstring(L, aircraftFileName);
    lua_setglobal(L, "AIRCRAFT_FILENAME");
    lua_pushstring(L, aircraftPath);
    lua_setglobal(L, "AIRCRAFT_PATH");

    const std::string planeICAO = ReadXPlaneStringDataRef(gPlaneICAODataRef);
    const std::string planeTailNumber = ReadXPlaneStringDataRef(gPlaneTailNumberDataRef);
    lua_pushstring(L, planeICAO.c_str());
    lua_setglobal(L, "PLANE_ICAO");
    lua_pushstring(L, planeTailNumber.c_str());
    lua_setglobal(L, "PLANE_TAILNUMBER");
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
        binding->menuItemIndex = XPLMAppendMenuItem(gFlyWithLuaMacrosMenu, label.c_str(), binding, 1);
        if (binding->isLegacyMacro && binding->isSwitch && binding->menuItemIndex >= 0) {
            XPLMCheckMenuItem(gFlyWithLuaMacrosMenu, binding->menuItemIndex,
                              binding->isActive ? xplm_Menu_Checked : xplm_Menu_Unchecked);
        }
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

static void DestroyFlyWithLuaScriptsMenu() {
    for (const auto& group : gFlyWithLuaScriptMenuGroups) {
        if (group && group->menu) {
            XPLMDestroyMenu(group->menu);
        }
    }
    gFlyWithLuaScriptMenuGroups.clear();

    if (gFlyWithLuaScriptsMenu) {
        XPLMDestroyMenu(gFlyWithLuaScriptsMenu);
        gFlyWithLuaScriptsMenu = nullptr;
    }
    gFlyWithLuaScriptMenuEntries.clear();
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

static void FlyWithLuaScriptsMenuHandler(void* /*inMenuRef*/, void* inItemRef) {
    auto* entry = static_cast<FlyWithLuaScriptMenuEntry*>(inItemRef);
    if (!entry) {
        return;
    }

    const bool enable = !entry->enabled;
    const std::string disabledRoot = flywithlua::JoinPath(GetMainDirectoryFromScripts(), "Scripts (disabled)");
    const std::string sourceRoot = enable ? disabledRoot : flywithlua::scriptDir;
    const std::string destinationRoot = enable ? flywithlua::scriptDir : disabledRoot;

    if (!IsSafeRelativeScriptPath(entry->relativePath) ||
        flywithlua::scriptDir.empty() ||
        !IsRegularFile(flywithlua::JoinPath(sourceRoot, entry->relativePath))) {
        XPLMDebugString(("FlyWithLua-Mac Warning: Cannot change script state for " + entry->relativePath + ".\n").c_str());
        const std::string message = FlyWithLuaLocalizedText("Could not change script state", "スクリプトの状態を変更できません");
        flywithlua_update_last_log_message(message.c_str());
        RefreshFlyWithLuaScriptsMenu();
        return;
    }

    const std::string sourcePath = flywithlua::JoinPath(sourceRoot, entry->relativePath);
    const std::string destinationPath = flywithlua::JoinPath(destinationRoot, entry->relativePath);
    if (IsRegularFile(destinationPath) || IsExistingDirectory(destinationPath)) {
        XPLMDebugString(("FlyWithLua-Mac Warning: Script destination already exists: " + destinationPath + "\n").c_str());
        const std::string message = FlyWithLuaLocalizedText("Script destination already exists", "移動先に同名のスクリプトがあります");
        flywithlua_update_last_log_message(message.c_str());
        RefreshFlyWithLuaScriptsMenu();
        return;
    }

    const size_t lastSlash = destinationPath.find_last_of('/');
    const std::string destinationDirectory = lastSlash == std::string::npos
        ? std::string()
        : destinationPath.substr(0, lastSlash);
    if (!EnsureDirectoryTree(destinationDirectory)) {
        XPLMDebugString(("FlyWithLua-Mac Warning: Could not create script directory: " + destinationDirectory + "\n").c_str());
        const std::string message = FlyWithLuaLocalizedText("Could not create script directory", "スクリプトフォルダを作成できません");
        flywithlua_update_last_log_message(message.c_str());
        RefreshFlyWithLuaScriptsMenu();
        return;
    }

    if (std::rename(sourcePath.c_str(), destinationPath.c_str()) != 0) {
        XPLMDebugString(("FlyWithLua-Mac Warning: Could not " + std::string(enable ? "enable" : "disable") +
                         " script " + entry->relativePath + ".\n").c_str());
        const std::string message = FlyWithLuaLocalizedText("Could not change script state", "スクリプトの状態を変更できません");
        flywithlua_update_last_log_message(message.c_str());
        RefreshFlyWithLuaScriptsMenu();
        return;
    }

    entry->enabled = enable;
    const std::string message = FlyWithLuaLocalizedText(
        enable ? "Enabled script: " : "Disabled script: ",
        enable ? "有効化したスクリプト: " : "無効化したスクリプト: "
    ) + entry->relativePath;
    XPLMDebugString(("FlyWithLua-Mac: " + message + "; reloading scripts.\n").c_str());
    flywithlua_update_last_log_message(message.c_str());
    flywithlua_reload_scripts();
}

static void FlyWithLuaMacroMenuHandler(void* /*inMenuRef*/, void* inItemRef) {
    auto* binding = static_cast<FlyWithLuaCommandBinding*>(inItemRef);
    if (!binding) {
        return;
    }

    if (binding->isLegacyMacro) {
        if (binding->isSwitch) {
            binding->isActive = !binding->isActive;
            XPLMCheckMenuItem(gFlyWithLuaMacrosMenu, binding->menuItemIndex,
                              binding->isActive ? xplm_Menu_Checked : xplm_Menu_Unchecked);
            RunLuaStringChunk(binding->isActive ? binding->activateCode : binding->deactivateCode,
                              (binding->name + (binding->isActive ? ":activate" : ":deactivate")).c_str());
        } else {
            RunLuaStringChunk(binding->activateCode, (binding->name + ":activate").c_str());
        }
        return;
    }

    if (binding->commandRef) {
        XPLMCommandOnce(binding->commandRef);
    }
}

static void ClearFlyWithLuaCommands() {
    DestroyFlyWithLuaMacrosMenu();
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

static int LuaAddMacro(lua_State* state) {
    std::string name;
    std::string activateCode;
    std::string deactivateCode;
    if (!LuaStringArg(state, 1, name) || !LuaStringArg(state, 2, activateCode)) {
        XPLMDebugString("FlyWithLua-Mac Warning: add_macro() expects at least two string arguments.\n");
        return 0;
    }

    const bool isSwitch = LuaStringArg(state, 3, deactivateCode);
    auto existing = gFlyWithLuaCommandBindings.find(name);
    if (existing != gFlyWithLuaCommandBindings.end()) {
        XPLMDebugString(("FlyWithLua-Mac Warning: add_macro() duplicate ignored: " + name + "\n").c_str());
        return 0;
    }

    auto binding = std::make_unique<FlyWithLuaCommandBinding>();
    binding->name = name;
    binding->description = name;
    binding->activateCode = activateCode;
    binding->deactivateCode = deactivateCode;
    binding->isLegacyMacro = true;
    binding->isSwitch = isSwitch;

    bool startActive = false;
    std::string initialState;
    if (LuaStringArg(state, 4, initialState)) {
        startActive = initialState == "activate";
    }
    binding->isActive = startActive;

    FlyWithLuaCommandBinding* bindingPtr = binding.get();
    gFlyWithLuaCommandBindings.emplace(name, std::move(binding));
    MarkFlyWithLuaMacrosMenuDirty();

    if (isSwitch) {
        if (gFlyWithLuaMacrosMenu && bindingPtr->menuItemIndex >= 0) {
            XPLMCheckMenuItem(gFlyWithLuaMacrosMenu, bindingPtr->menuItemIndex,
                              startActive ? xplm_Menu_Checked : xplm_Menu_Unchecked);
        }
        RunLuaStringChunk(startActive ? bindingPtr->activateCode : bindingPtr->deactivateCode,
                          (name + (startActive ? ":activate" : ":deactivate")).c_str());
    }
    return 0;
}

static int LuaActivateMacro(lua_State* state) {
    std::string name;
    if (!LuaStringArg(state, 1, name)) {
        XPLMDebugString("FlyWithLua-Mac Warning: activate_macro() expects a string argument.\n");
        return 0;
    }

    auto it = gFlyWithLuaCommandBindings.find(name);
    if (it == gFlyWithLuaCommandBindings.end() || !it->second->isLegacyMacro || !it->second->isSwitch) {
        return 0;
    }

    FlyWithLuaCommandBinding& binding = *it->second;
    binding.isActive = true;
    if (gFlyWithLuaMacrosMenu && binding.menuItemIndex >= 0) {
        XPLMCheckMenuItem(gFlyWithLuaMacrosMenu, binding.menuItemIndex, xplm_Menu_Checked);
    }
    RunLuaStringChunk(binding.activateCode, (name + ":activate").c_str());
    return 0;
}

static int LuaDeactivateMacro(lua_State* state) {
    std::string name;
    if (!LuaStringArg(state, 1, name)) {
        XPLMDebugString("FlyWithLua-Mac Warning: deactivate_macro() expects a string argument.\n");
        return 0;
    }

    auto it = gFlyWithLuaCommandBindings.find(name);
    if (it == gFlyWithLuaCommandBindings.end() || !it->second->isLegacyMacro || !it->second->isSwitch) {
        return 0;
    }

    FlyWithLuaCommandBinding& binding = *it->second;
    binding.isActive = false;
    if (gFlyWithLuaMacrosMenu && binding.menuItemIndex >= 0) {
        XPLMCheckMenuItem(gFlyWithLuaMacrosMenu, binding.menuItemIndex, xplm_Menu_Unchecked);
    }
    RunLuaStringChunk(binding.deactivateCode, (name + ":deactivate").c_str());
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
    debugFile << "Scripts discovered: " << gDiscoveredScriptCount << "\n";
    debugFile << "Scripts loaded: " << gLoadedScriptCount << "\n";
    debugFile << "Scripts failed: " << gFailedScriptCount << "\n\n";
    debugFile << "*** Script load failures ***\n";
    if (gScriptLoadFailures.empty()) {
        debugFile << "No script load failures.\n";
    } else {
        for (const FlyWithLuaScriptFailure& failure : gScriptLoadFailures) {
            debugFile << failure.fileName << ": " << failure.message << "\n";
        }
    }
    debugFile << "\n";
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
            debugFile << (binding.isLegacyMacro ? "Macro: " : "Command: ") << binding.name << "\n";
            debugFile << "Description: " << binding.description << "\n";
            if (binding.isLegacyMacro) {
                debugFile << "Activate: " << binding.activateCode << "\n";
                if (binding.isSwitch) {
                    debugFile << "Deactivate: " << binding.deactivateCode << "\n";
                    debugFile << "Active: " << (binding.isActive ? "true" : "false") << "\n";
                }
            } else {
                debugFile << "Begin: " << binding.beginCode << "\n";
                debugFile << "Continue: " << binding.continueCode << "\n";
                debugFile << "End: " << binding.endCode << "\n";
            }
            debugFile << "\n";
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
    RefreshFlyWithLuaScriptsMenu();
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
    const std::string scriptsMenuTitle = FlyWithLuaLocalizedText("FlyWithLua Scripts", "FlyWithLua スクリプト");
    gFlyWithLuaScriptsMenuItem = XPLMAppendMenuItem(gFlyWithLuaMenu, scriptsMenuTitle.c_str(), nullptr, 1);
    if (gFlyWithLuaScriptsMenuItem >= 0) {
        RefreshFlyWithLuaScriptsMenu();
    }
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

    DestroyFlyWithLuaScriptsMenu();
    if (gFlyWithLuaMacrosMenu) {
        XPLMDestroyMenu(gFlyWithLuaMacrosMenu);
        gFlyWithLuaMacrosMenu = nullptr;
    }

    if (gFlyWithLuaMenu) {
        XPLMDestroyMenu(gFlyWithLuaMenu);
        gFlyWithLuaMenu = nullptr;
    }

    if (gPluginsMenu && gFlyWithLuaMenuItem >= 0) {
        XPLMRemoveMenuItem(gPluginsMenu, gFlyWithLuaMenuItem);
    }

    gFlyWithLuaMenuItem = -1;
    gFlyWithLuaMenu = nullptr;
    gFlyWithLuaMacrosMenuItem = -1;
    gFlyWithLuaMacrosMenu = nullptr;
    gFlyWithLuaScriptsMenuItem = -1;
    gFlyWithLuaScriptsMenu = nullptr;
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

static std::string FlyWithLuaLocalizedText(const char* english, const char* japanese) {
    return XPLMGetLanguage() == xplm_Language_Japanese ? japanese : english;
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
            << "\",\"message\":\"" << EscapeJsonString(failures[i].message) << "\"";
        if (!failures[i].callback.empty()) {
            out << ",\"callback\":\"" << EscapeJsonString(failures[i].callback) << "\"";
        }
        if (!failures[i].phase.empty()) {
            out << ",\"phase\":\"" << EscapeJsonString(failures[i].phase) << "\"";
        }
        if (!failures[i].state.empty()) {
            out << ",\"state\":\"" << EscapeJsonString(failures[i].state) << "\"";
        }
        if (!failures[i].backend.empty()) {
            out << ",\"backend\":\"" << EscapeJsonString(failures[i].backend) << "\"";
        }
        out << "}";
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

static void ConfigureLuaCallbackScope() {
    std::string value = getOptionToString("CallbackScope");
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    gUseIsolatedCallbackScope = value != "legacy";
    XPLMDebugString((std::string("FlyWithLua-Mac: CallbackScope=") +
                     (gUseIsolatedCallbackScope ? "isolated" : "legacy") + "\n").c_str());
}

static void RunLuaStringChunk(const std::string& code, const char* context) {
    if (code.empty() || !L || !flywithlua::LuaIsRunning) {
        return;
    }

    if (luaL_dostring(L, code.c_str()) != 0) {
        const char* luaError = lua_tostring(L, -1);
        std::string errorMessage = luaError ? luaError : "unknown Lua error";
        lua_pop(L, 1);
        flywithlua::ReportLuaScriptError(flywithlua::CurrentLuaScriptId(), context, errorMessage);
    }
}

static void DisableLuaCallbackGroup(LuaCallbackEntries& callbacks, const char* context,
                                    const std::string& errorMessage,
                                    flywithlua::LuaScriptId ownerScriptId) {
    if (ownerScriptId != flywithlua::kSystemLuaScriptId) {
        QuarantineLuaScript(ownerScriptId, context, errorMessage);
        return;
    }

    callbacks.disabled = true;
    if (callbacks.errorReported) {
        return;
    }

    callbacks.errorReported = true;
    const std::string message = "FlyWithLua-Mac Lua Error (" + std::string(context) +
                                " callback group): " + errorMessage +
                                "; callback group disabled\n";
    XPLMDebugString(message.c_str());
}

static bool CompileLuaCallbackGroup(LuaCallbackEntries& callbacks, const char* context,
                                    flywithlua::LuaScriptId ownerScriptId) {
    std::string source;
    source.reserve(callbacks.source.size() + 32);
    source.append("return function()\n");
    source.append(callbacks.source);
    source.append("\nend");

    if (luaL_loadbuffer(L, source.c_str(), source.size(), context) != 0) {
        const char* luaError = lua_tostring(L, -1);
        const std::string errorMessage = luaError ? luaError : "unknown Lua error";
        lua_pop(L, 1);
        DisableLuaCallbackGroup(callbacks, context, errorMessage, ownerScriptId);
        return false;
    }

    if (lua_pcall(L, 0, 1, 0) != 0) {
        const char* luaError = lua_tostring(L, -1);
        const std::string errorMessage = luaError ? luaError : "unknown Lua error";
        lua_pop(L, 1);
        DisableLuaCallbackGroup(callbacks, context, errorMessage, ownerScriptId);
        return false;
    }

    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        DisableLuaCallbackGroup(callbacks, context, "callback group did not compile to a function", ownerScriptId);
        return false;
    }

    callbacks.chunkRef = luaL_ref(L, LUA_REGISTRYINDEX);
    return true;
}

static void InvalidateLuaCallbackEntries(LuaCallbackEntries& callbacks) {
    if (L && callbacks.chunkRef != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, callbacks.chunkRef);
    }
    callbacks.source.clear();
    callbacks.chunkRef = LUA_NOREF;
    callbacks.disabled = false;
    callbacks.errorReported = false;
}

static void InvalidateAllLuaCallbackChunks() {
    InvalidateLuaCallbackEntries(gDrawCallbacks);
    InvalidateLuaCallbackEntries(gPanelDrawCallbacks);
    InvalidateLuaCallbackEntries(gPanelApiDrawCallbacks);
    InvalidateLuaCallbackEntries(gEveryFrameCallbacks);
    InvalidateLuaCallbackEntries(gOftenCallbacks);
    InvalidateLuaCallbackEntries(gSometimesCallbacks);
    InvalidateLuaCallbackEntries(gOnExitCallbacks);
    InvalidateLuaCallbackEntries(gMouseClickCallbacks);
    InvalidateLuaCallbackEntries(gMouseWheelCallbacks);
}

static LuaCallbackEntries& ScriptCallbackEntries(flywithlua::LuaScriptId scriptId,
                                                  LuaCallbackKind kind) {
    return gLuaScriptCallbacks[scriptId].forKind(kind);
}

static void QuarantineLuaScript(flywithlua::LuaScriptId scriptId,
                                const char* context,
                                const std::string& errorMessage) {
    if (scriptId == flywithlua::kSystemLuaScriptId) {
        XPLMDebugString((std::string("FlyWithLua-Mac Lua Error (") + context + "): " +
                         errorMessage + "\n").c_str());
        return;
    }

    auto recordIt = gLuaScriptRecords.find(scriptId);
    if (recordIt == gLuaScriptRecords.end()) {
        XPLMDebugString((std::string("FlyWithLua-Mac Lua Error (") + context + "): " +
                         errorMessage + "\n").c_str());
        return;
    }
    LuaScriptRecord& record = recordIt->second;
    if (record.quarantined) {
        return;
    }

    record.quarantined = true;
    flywithlua::panel::invalidateLuaResources(scriptId);
    flwnd::quarantineWindowsOwnedBy(scriptId);
    auto callbacksIt = gLuaScriptCallbacks.find(scriptId);
    if (callbacksIt != gLuaScriptCallbacks.end()) {
        for (LuaCallbackEntries& callbacks : callbacksIt->second.entries) {
            InvalidateLuaCallbackEntries(callbacks);
        }
    }

    if (!record.errorReported) {
        record.errorReported = true;
        const std::string message = "FlyWithLua-Mac Lua Error (" + std::string(context) +
                                    ") in " + record.fileName + ": " + errorMessage +
                                    "; script quarantined until reload\n";
        XPLMDebugString(message.c_str());
        const char* phase = gLoadingLuaScriptId == scriptId ? "load" : "runtime";
        gScriptLoadFailures.push_back({record.fileName, errorMessage, context, phase,
                                       "quarantined", flywithlua::panel::enabled() ? "panel" : "opengl"});
        const std::string failuresJson = BuildScriptLoadFailuresJson(gScriptLoadFailures);
        flywithlua_update_script_load_summary(gDiscoveredScriptCount, gLoadedScriptCount,
                                              gFailedScriptCount, failuresJson.c_str());
    }
}

static void ResetLuaScriptRegistry() {
    for (auto& script : gLuaScriptCallbacks) {
        for (LuaCallbackEntries& callbacks : script.second.entries) {
            InvalidateLuaCallbackEntries(callbacks);
        }
    }
    gLuaScriptCallbacks.clear();
    gLuaScriptRecords.clear();
    gLuaScriptOrder.clear();
    gNextLuaScriptId = 1;
    gLoadingLuaScriptId = flywithlua::kSystemLuaScriptId;
    gCurrentLuaScriptId = flywithlua::kSystemLuaScriptId;
    gCurrentLuaPanelApiAllowed = false;
}

static void RunOneLuaCallbackEntries(LuaCallbackEntries& callbacks, const char* context,
                                     flywithlua::LuaScriptId ownerScriptId,
                                     bool panelApiAllowed) {
    if (callbacks.empty() || !L || !flywithlua::LuaIsRunning) {
        return;
    }

    if (callbacks.disabled) {
        return;
    }

    // Keep registrations for this owner and callback kind in one closure so
    // locals remain visible in registration order. In isolated mode the
    // owner is one script; legacy mode intentionally uses the system owner.
    if (ownerScriptId != flywithlua::kSystemLuaScriptId &&
        flywithlua::IsLuaScriptQuarantined(ownerScriptId)) {
        return;
    }

    flywithlua::LuaScriptScope scriptScope(ownerScriptId);
    flywithlua::LuaPanelApiScope panelApiScope(panelApiAllowed);
    if (callbacks.chunkRef == LUA_NOREF && !CompileLuaCallbackGroup(callbacks, context, ownerScriptId)) {
        return;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, callbacks.chunkRef);
    if (lua_pcall(L, 0, 0, 0) != 0) {
        const char* luaError = lua_tostring(L, -1);
        const std::string errorMessage = luaError ? luaError : "unknown Lua error";
        lua_pop(L, 1);
        DisableLuaCallbackGroup(callbacks, context, errorMessage, ownerScriptId);
    }
}

static void RunLuaCallbackEntries(LuaCallbackKind kind, LuaCallbackEntries& legacyCallbacks,
                                  const char* context) {
    // New panel callbacks are owner-scoped even when legacy callback scope is selected.
    const bool useOwnerScope = gUseIsolatedCallbackScope || kind == LuaCallbackKind::PanelDraw;
    if (!useOwnerScope) {
        RunOneLuaCallbackEntries(legacyCallbacks, context, flywithlua::kSystemLuaScriptId, false);
        return;
    }

    for (const flywithlua::LuaScriptId scriptId : gLuaScriptOrder) {
        const auto recordIt = gLuaScriptRecords.find(scriptId);
        if (recordIt == gLuaScriptRecords.end() || !recordIt->second.loaded ||
            recordIt->second.quarantined) {
            continue;
        }
        RunOneLuaCallbackEntries(ScriptCallbackEntries(scriptId, kind), context, scriptId,
                                 kind == LuaCallbackKind::PanelDraw);
    }

    // API registrations made outside a script load are retained as a system callback.
    if (!legacyCallbacks.empty()) {
        RunOneLuaCallbackEntries(legacyCallbacks, context, flywithlua::kSystemLuaScriptId, false);
    }
}

static bool AppendLuaCallback(lua_State* state, std::string& debugCode,
                              LuaCallbackEntries& legacyCallbacks, LuaCallbackKind kind) {
    if (!lua_isstring(state, 1)) {
        return false;
    }

    const char* code = lua_tostring(state, 1);
    if (!code) {
        return false;
    }

    debugCode.append(code).append("\n");
    LuaCallbackEntries* callbacks = &legacyCallbacks;
    const bool ownerScoped = gUseIsolatedCallbackScope || kind == LuaCallbackKind::PanelDraw;
    if (ownerScoped && gCurrentLuaScriptId != flywithlua::kSystemLuaScriptId) {
        callbacks = &ScriptCallbackEntries(gCurrentLuaScriptId, kind);
    }
    callbacks->source.append(code).append("\n");
    if (L && callbacks->chunkRef != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, callbacks->chunkRef);
        callbacks->chunkRef = LUA_NOREF;
    }
    callbacks->disabled = false;
    callbacks->errorReported = false;
    return true;
}

static bool HasEnabledLuaCallbacks(const LuaCallbackEntries& callbacks) {
    return !callbacks.empty() && !callbacks.disabled;
}

static bool HasEnabledLuaCallbacks(LuaCallbackKind kind, const LuaCallbackEntries& legacyCallbacks) {
    if (!gUseIsolatedCallbackScope && kind != LuaCallbackKind::PanelDraw) {
        return HasEnabledLuaCallbacks(legacyCallbacks);
    }
    if (HasEnabledLuaCallbacks(legacyCallbacks)) {
        return true;
    }
    for (const flywithlua::LuaScriptId scriptId : gLuaScriptOrder) {
        const auto recordIt = gLuaScriptRecords.find(scriptId);
        if (recordIt != gLuaScriptRecords.end() && recordIt->second.loaded &&
            !recordIt->second.quarantined &&
            HasEnabledLuaCallbacks(ScriptCallbackEntries(scriptId, kind))) {
            return true;
        }
    }
    return false;
}

static bool AdvanceLuaTimer(float& accumulator, float elapsedSeconds, float intervalSeconds) {
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0f) {
        return false;
    }

    accumulator += elapsedSeconds;
    if (accumulator < intervalSeconds) {
        return false;
    }

    // Run at most once per flight-loop callback. Keep only the sub-period
    // remainder so a long frame cannot cause a burst of Lua executions.
    accumulator = std::fmod(accumulator, intervalSeconds);
    return true;
}

static void UpdateLuaMouseGlobals(int globalMouseX, int globalMouseY) {
    if (!L) {
        return;
    }

    int screenLeft = 0;
    int screenTop = 0;
    int screenRight = 0;
    int screenBottom = 0;
    XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);

    // Lua drawing callbacks use the X-Plane desktop as their origin. Modern
    // window callbacks provide global desktop boxels, so normalize both paths
    // to the same coordinate space before exposing them to scripts.
    const int mouseX = globalMouseX - screenLeft;
    const int mouseY = globalMouseY - screenBottom;
    const int screenWidth = screenRight - screenLeft;
    const int screenHeight = screenTop - screenBottom;

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
}

static void UpdateLuaMouseGlobals() {
    int globalMouseX = 0;
    int globalMouseY = 0;
    XPLMGetMouseLocationGlobal(&globalMouseX, &globalMouseY);
    UpdateLuaMouseGlobals(globalMouseX, globalMouseY);
}

static bool LuaGlobalBoolean(const char* name) {
    if (!L) {
        return false;
    }

    lua_getglobal(L, name);
    const bool value = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return value;
}

static bool IsVREnabled() {
    static XPLMDataRef vrEnabledRef = XPLMFindDataRef("sim/graphics/VR/enabled");
    return vrEnabledRef != nullptr && XPLMGetDatai(vrEnabledRef) != 0;
}

static void MouseEventWindowDraw(XPLMWindowID inWindowID, void* /*inRefcon*/) {
    if (!flywithlua::LuaIsRunning) {
        return;
    }

    if (gMouseEventWindowPanelGraphics && flywithlua::panel::enabled()) {
        flywithlua::panel::PanelDrawScope panelScope(inWindowID);
        if (!panelScope.active()) {
            return;
        }
        UpdateLuaMouseGlobals();
        RunLuaCallbackEntries(LuaCallbackKind::CompatPanelDraw, gPanelDrawCallbacks,
                              "do_every_draw[panel]");
        RunLuaCallbackEntries(LuaCallbackKind::PanelDraw, gPanelApiDrawCallbacks,
                              "do_every_panel_draw");
        threejfps_draw_hud();
        return;
    }

    threejfps_draw_hud();
}

static void MouseEventWindowKey(XPLMWindowID /*inWindowID*/, char /*inKey*/, XPLMKeyFlags /*inFlags*/,
                                char /*inVirtualKey*/, void* /*inRefcon*/, int /*losingFocus*/) {
}

static int MouseEventWindowClick(XPLMWindowID /*inWindowID*/, int x, int y,
                                 XPLMMouseStatus inMouse, void* /*inRefcon*/) {
    if (threejfps_handle_click(x, y, static_cast<int>(inMouse)) != 0) {
        return 1;
    }

    if (!L || !flywithlua::LuaIsRunning) {
        gMouseClickCaptured = false;
        return 0;
    }

    UpdateLuaMouseGlobals(x, y);
    lua_pushboolean(L, 0);
    lua_setglobal(L, "RESUME_MOUSE_CLICK");

    const char* mouseStatus = "up";
    if (inMouse == xplm_MouseDown) {
        mouseStatus = "down";
    } else if (inMouse == xplm_MouseDrag) {
        mouseStatus = "drag";
    }
    lua_pushstring(L, mouseStatus);
    lua_setglobal(L, "MOUSE_STATUS");

    RunLuaCallbackEntries(LuaCallbackKind::MouseClick, gMouseClickCallbacks, "do_on_mouse_click");
    const bool resumeClick = LuaGlobalBoolean("RESUME_MOUSE_CLICK");
    if (inMouse == xplm_MouseDown) {
        gMouseClickCaptured = resumeClick;
    } else if (resumeClick) {
        gMouseClickCaptured = true;
    }

    // Once Lua consumes the mouse-down, keep the whole drag/up sequence in
    // this window. Returning it to X-Plane midway through the gesture causes
    // the tracking glitches warned about by the XPLM300 window API.
    const bool consumeClick = gMouseClickCaptured;
    if (inMouse == xplm_MouseUp) {
        gMouseClickCaptured = false;
    }
    return consumeClick ? 1 : 0;
}

static int MouseEventWindowWheel(XPLMWindowID /*inWindowID*/, int x, int y, int wheel,
                                 int clicks, void* /*inRefcon*/) {
    if (threejfps_handle_wheel(x, y, wheel, clicks) != 0) {
        return 1;
    }

    if (!L || !flywithlua::LuaIsRunning) {
        return 0;
    }

    UpdateLuaMouseGlobals(x, y);
    lua_pushboolean(L, 0);
    lua_setglobal(L, "RESUME_MOUSE_WHEEL");
    lua_pushinteger(L, wheel);
    lua_setglobal(L, "MOUSE_WHEEL_NUMBER");
    lua_pushinteger(L, clicks);
    lua_setglobal(L, "MOUSE_WHEEL_CLICKS");

    RunLuaCallbackEntries(LuaCallbackKind::MouseWheel, gMouseWheelCallbacks, "do_on_mouse_wheel");
    return LuaGlobalBoolean("RESUME_MOUSE_WHEEL") ? 1 : 0;
}

static XPLMCursorStatus MouseEventWindowCursor(XPLMWindowID /*inWindowID*/, int /*x*/, int /*y*/,
                                               void* /*inRefcon*/) {
    return xplm_CursorDefault;
}

static int MouseEventWindowRightClick(XPLMWindowID /*inWindowID*/, int /*x*/, int /*y*/,
                                      XPLMMouseStatus /*inMouse*/, void* /*inRefcon*/) {
    return 0;
}

static void UpdateMouseEventWindowGeometry() {
    if (!gMouseEventWindow) {
        return;
    }

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    XPLMGetScreenBoundsGlobal(&left, &top, &right, &bottom);
    if (gMouseEventWindowGeometryInitialized &&
        left == gMouseEventWindowLeft && top == gMouseEventWindowTop &&
        right == gMouseEventWindowRight && bottom == gMouseEventWindowBottom) {
        return;
    }

    XPLMSetWindowGeometry(gMouseEventWindow, left, top, right, bottom);
    gMouseEventWindowLeft = left;
    gMouseEventWindowTop = top;
    gMouseEventWindowRight = right;
    gMouseEventWindowBottom = bottom;
    gMouseEventWindowGeometryInitialized = true;
}

static bool CreateMouseEventWindow() {
    if (gMouseEventWindow) {
        return true;
    }

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    XPLMGetScreenBoundsGlobal(&left, &top, &right, &bottom);

    XPLMCreateWindow_t params{};
    params.structSize = sizeof(params);
    params.left = left;
    params.top = top;
    params.right = right;
    params.bottom = bottom;
    params.visible = 1;
    params.drawWindowFunc = MouseEventWindowDraw;
    params.handleMouseClickFunc = MouseEventWindowClick;
    params.handleKeyFunc = MouseEventWindowKey;
    params.handleCursorFunc = MouseEventWindowCursor;
    params.handleMouseWheelFunc = MouseEventWindowWheel;
    params.handleRightClickFunc = MouseEventWindowRightClick;
    params.decorateAsFloatingWindow = xplm_WindowDecorationNone;
    params.layer = xplm_WindowLayerFlightOverlay;
    params.refcon = nullptr;

    // Panel Graphics windows are not used for the VR path; retain the
    // existing OpenGL overlay behavior there.
    const bool requestedPanel = flywithlua::panel::enabled() && !IsVREnabled();
    if (requestedPanel) {
        flywithlua::panel::configurePanelWindow(params);
    } else {
        flywithlua::panel::configureOpenGLWindow(params);
    }

    gMouseEventWindow = XPLMCreateWindowEx(&params);
    if (!gMouseEventWindow && requestedPanel) {
        flywithlua::panel::disableForSession("Panel Graphics window creation failed");
        flywithlua::panel::configureOpenGLWindow(params);
        gMouseEventWindow = XPLMCreateWindowEx(&params);
    }
    if (!gMouseEventWindow) {
        XPLMDebugString("FlyWithLua-Mac Warning: Could not create the mouse event window.\n");
        return false;
    }
    gMouseEventWindowPanelGraphics = requestedPanel && flywithlua::panel::enabled();
    if (gMouseEventWindowPanelGraphics) {
        flywithlua::panel::registerWindow(gMouseEventWindow);
    }

    gMouseEventWindowLeft = left;
    gMouseEventWindowTop = top;
    gMouseEventWindowRight = right;
    gMouseEventWindowBottom = bottom;
    gMouseEventWindowGeometryInitialized = true;
    return true;
}

static void DestroyMouseEventWindow() {
    if (gMouseEventWindow) {
        flywithlua::panel::unregisterWindow(gMouseEventWindow);
        XPLMDestroyWindow(gMouseEventWindow);
        gMouseEventWindow = nullptr;
    }
    gMouseEventWindowGeometryInitialized = false;
    gMouseEventWindowPanelGraphics = false;
    gMouseClickCaptured = false;
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
    flywithlua::ReportLuaScriptError(
        flywithlua::CurrentLuaScriptId(), functionName,
        "graphics API called outside a drawing callback");
    return false;
}

static bool LuaGraphicsHasNumbers(lua_State* state, int firstIndex, int count, const char* functionName) {
    for (int index = firstIndex; index < firstIndex + count; ++index) {
        if (!lua_isnumber(state, index)) {
            flywithlua::logMsg(
                logToDevCon,
                std::string("FlyWithLua Error: Wrong arguments to function ") + functionName + "."
            );
            flywithlua::ReportLuaScriptError(
                flywithlua::CurrentLuaScriptId(), functionName,
                "wrong numeric arguments");
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

    if (flywithlua::panel::panelDrawing()) {
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

static flywithlua::panel::LegacyPrimitiveMode PanelPrimitiveMode(GLenum mode) {
    switch (mode) {
        case GL_POINTS: return flywithlua::panel::LegacyPrimitiveMode::Points;
        case GL_LINES: return flywithlua::panel::LegacyPrimitiveMode::Lines;
        case GL_LINE_STRIP: return flywithlua::panel::LegacyPrimitiveMode::LineStrip;
        case GL_LINE_LOOP: return flywithlua::panel::LegacyPrimitiveMode::LineLoop;
        case GL_POLYGON: return flywithlua::panel::LegacyPrimitiveMode::Polygon;
        case GL_TRIANGLES: return flywithlua::panel::LegacyPrimitiveMode::Triangles;
        case GL_TRIANGLE_STRIP: return flywithlua::panel::LegacyPrimitiveMode::TriangleStrip;
        case GL_TRIANGLE_FAN: return flywithlua::panel::LegacyPrimitiveMode::TriangleFan;
        case GL_QUADS: return flywithlua::panel::LegacyPrimitiveMode::Quads;
        case GL_QUAD_STRIP: return flywithlua::panel::LegacyPrimitiveMode::QuadStrip;
    }
    return flywithlua::panel::LegacyPrimitiveMode::Lines;
}

static int LuaGLBegin(lua_State* state, GLenum mode, const char* functionName) {
    if (!LuaGraphicsCallAllowed(functionName)) {
        return 0;
    }

    if (flywithlua::panel::panelDrawing()) {
        if (!flywithlua::panel::beginPrimitive(PanelPrimitiveMode(mode),
                                               flywithlua::panel::PrimitiveSource::Legacy)) {
            flywithlua::ReportLuaScriptError(flywithlua::CurrentLuaScriptId(),
                                             functionName,
                                             "cannot mix panel and legacy primitives");
        }
    } else {
        glBegin(mode);
    }
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

    if (flywithlua::panel::panelDrawing()) {
        if (!flywithlua::panel::endPrimitive(flywithlua::panel::PrimitiveSource::Legacy)) {
            flywithlua::ReportLuaScriptError(flywithlua::CurrentLuaScriptId(),
                                             "glEnd()", "legacy primitive is not active");
        }
    } else {
        glEnd();
    }
    return 0;
}

static int LuaGLVertex2f(lua_State* state) {
    if (!LuaGraphicsCallAllowed("glVertex2f()") ||
        !LuaGraphicsHasNumbers(state, 1, 2, "glVertex2f")) {
        return 0;
    }

    const float x = static_cast<float>(lua_tonumber(state, 1));
    const float y = static_cast<float>(lua_tonumber(state, 2));
    if (flywithlua::panel::panelDrawing()) {
        if (!flywithlua::panel::vertex(x, y, flywithlua::panel::PrimitiveSource::Legacy)) {
            flywithlua::ReportLuaScriptError(flywithlua::CurrentLuaScriptId(),
                                             "glVertex2f()",
                                             "cannot mix panel and legacy primitives");
        }
    } else {
        glVertex2f(x, y);
    }
    return 0;
}

static int LuaGLVertex3f(lua_State* state) {
    if (!LuaGraphicsCallAllowed("glVertex3f()") ||
        !LuaGraphicsHasNumbers(state, 1, 3, "glVertex3f")) {
        return 0;
    }

    const float x = static_cast<float>(lua_tonumber(state, 1));
    const float y = static_cast<float>(lua_tonumber(state, 2));
    if (flywithlua::panel::panelDrawing()) {
        if (!flywithlua::panel::vertex(x, y, flywithlua::panel::PrimitiveSource::Legacy)) {
            flywithlua::ReportLuaScriptError(flywithlua::CurrentLuaScriptId(),
                                             "glVertex3f()",
                                             "cannot mix panel and legacy primitives");
        }
    } else {
        glVertex3f(x, y, static_cast<float>(lua_tonumber(state, 3)));
    }
    return 0;
}

static int LuaGLLineWidth(lua_State* state) {
    if (!LuaGraphicsCallAllowed("glLineWidth()") ||
        !LuaGraphicsHasNumbers(state, 1, 1, "glLineWidth")) {
        return 0;
    }

    const float width = static_cast<float>(lua_tonumber(state, 1));
    if (flywithlua::panel::panelDrawing()) {
        flywithlua::panel::setLineWidth(width);
    } else {
        glLineWidth(width);
    }
    return 0;
}

static int LuaGLColor3f(lua_State* state) {
    if (!LuaGraphicsCallAllowed("glColor3f()") ||
        !LuaGraphicsHasNumbers(state, 1, 3, "glColor3f")) {
        return 0;
    }

    const float red = static_cast<float>(lua_tonumber(state, 1));
    const float green = static_cast<float>(lua_tonumber(state, 2));
    const float blue = static_cast<float>(lua_tonumber(state, 3));
    if (flywithlua::panel::panelDrawing()) {
        flywithlua::panel::setColor(red, green, blue, 1.0f);
    } else {
        glColor3f(red, green, blue);
    }
    return 0;
}

static int LuaGLColor4f(lua_State* state) {
    if (!LuaGraphicsCallAllowed("glColor4f()") ||
        !LuaGraphicsHasNumbers(state, 1, 4, "glColor4f")) {
        return 0;
    }

    const float red = static_cast<float>(lua_tonumber(state, 1));
    const float green = static_cast<float>(lua_tonumber(state, 2));
    const float blue = static_cast<float>(lua_tonumber(state, 3));
    const float alpha = static_cast<float>(lua_tonumber(state, 4));
    if (flywithlua::panel::panelDrawing()) {
        flywithlua::panel::setColor(red, green, blue, alpha);
    } else {
        glColor4f(red, green, blue, alpha);
    }
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

    if (flywithlua::panel::panelDrawing()) {
        flywithlua::panel::drawFilledRect(x1, y1, x2, y2);
    } else {
        glRectf(x1, y1, x2, y2);
    }
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

static bool IsBundledPanelScript(const std::string& fileName) {
    // This manifest is deliberately explicit.  Only scripts shipped with
    // FlyWithLua-Mac are allowed into the Panel Graphics callback group;
    // arbitrary user do_every_draw() scripts retain the legacy OpenGL path.
    static const std::unordered_set<std::string> manifest = {
        "3jFPS12.lua",
        "HUD-G1000.lua",
        "LandingRate.lua",
        "visual_trim_system.lua",
    };
    return manifest.find(fileName) != manifest.end();
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

static bool IsSafeRelativeScriptPath(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.find('\\') != std::string::npos) {
        return false;
    }

    size_t componentStart = 0;
    while (componentStart <= path.size()) {
        const size_t separator = path.find('/', componentStart);
        const size_t componentLength = separator == std::string::npos
            ? path.size() - componentStart
            : separator - componentStart;
        const std::string component = path.substr(componentStart, componentLength);
        if (component.empty() || component == "." || component == "..") {
            return false;
        }
        if (separator == std::string::npos) {
            break;
        }
        componentStart = separator + 1;
    }

    return true;
}

static bool EnsureDirectoryTree(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    std::string current;
    size_t componentStart = 0;
    if (path.front() == '/') {
        current = "/";
        componentStart = 1;
    }

    while (componentStart <= path.size()) {
        const size_t separator = path.find('/', componentStart);
        const size_t componentLength = separator == std::string::npos
            ? path.size() - componentStart
            : separator - componentStart;
        if (componentLength > 0) {
            if (!current.empty() && current.back() != '/') {
                current.push_back('/');
            }
            current.append(path, componentStart, componentLength);
            if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) {
                return false;
            }
            if (!IsExistingDirectory(current)) {
                return false;
            }
        }

        if (separator == std::string::npos) {
            break;
        }
        componentStart = separator + 1;
    }

    return IsExistingDirectory(path);
}

static std::string FlyWithLuaScriptMenuGroupForPath(const std::string& path) {
    if (path.empty()) {
        return "Other";
    }

    unsigned char first = static_cast<unsigned char>(path.front());
    if (first >= 'a' && first <= 'z') {
        first = static_cast<unsigned char>(first - ('a' - 'A'));
    }
    if (first >= 'A' && first <= 'Z') {
        return std::string(1, static_cast<char>(first));
    }
    if (first >= '0' && first <= '9') {
        return "0-9";
    }
    return "Other";
}

static std::string FlyWithLuaScriptMenuGroupLabel(const std::string& group) {
    return group == "Other" ? FlyWithLuaLocalizedText("Other", "その他") : group;
}

static void RefreshFlyWithLuaScriptsMenu() {
    if (!gFlyWithLuaMenu || gFlyWithLuaScriptsMenuItem < 0) {
        return;
    }

    DestroyFlyWithLuaScriptsMenu();
    const std::string scriptsMenuTitle = FlyWithLuaLocalizedText("FlyWithLua Scripts", "FlyWithLua スクリプト");
    gFlyWithLuaScriptsMenu = XPLMCreateMenu(scriptsMenuTitle.c_str(), gFlyWithLuaMenu,
                                            gFlyWithLuaScriptsMenuItem, FlyWithLuaScriptsMenuHandler, nullptr);
    if (!gFlyWithLuaScriptsMenu) {
        XPLMDebugString("FlyWithLua-Mac Warning: Could not create FlyWithLua Scripts submenu.\n");
        return;
    }

    std::vector<std::string> enabledFiles;
    std::vector<std::string> disabledFiles;
    CollectScriptFilesRecursive(flywithlua::scriptDir, "", enabledFiles);
    const std::string disabledDirectory = flywithlua::JoinPath(GetMainDirectoryFromScripts(), "Scripts (disabled)");
    if (IsExistingDirectory(disabledDirectory)) {
        CollectScriptFilesRecursive(disabledDirectory, "", disabledFiles);
    }

    struct ScriptPathState {
        std::string path;
        bool enabled;
        bool conflict;
    };
    std::vector<ScriptPathState> scripts;
    scripts.reserve(enabledFiles.size() + disabledFiles.size());
    std::unordered_map<std::string, size_t> scriptIndexes;
    for (const std::string& fileName : enabledFiles) {
        scriptIndexes[fileName] = scripts.size();
        scripts.push_back({fileName, true, false});
    }
    for (const std::string& fileName : disabledFiles) {
        const auto existing = scriptIndexes.find(fileName);
        if (existing != scriptIndexes.end()) {
            scripts[existing->second].conflict = true;
            continue;
        }
        scriptIndexes[fileName] = scripts.size();
        scripts.push_back({fileName, false, false});
    }

    std::sort(scripts.begin(), scripts.end(), [](const ScriptPathState& lhs, const ScriptPathState& rhs) {
        const std::string lhsGroup = FlyWithLuaScriptMenuGroupForPath(lhs.path);
        const std::string rhsGroup = FlyWithLuaScriptMenuGroupForPath(rhs.path);
        auto groupOrder = [](const std::string& group) {
            if (group == "0-9") {
                return 0;
            }
            if (group.size() == 1 && group[0] >= 'A' && group[0] <= 'Z') {
                return 1 + (group[0] - 'A');
            }
            return 27;
        };
        if (lhsGroup != rhsGroup) {
            return groupOrder(lhsGroup) < groupOrder(rhsGroup);
        }
        if (lhs.path == rhs.path) {
            return lhs.enabled > rhs.enabled;
        }
        return lhs.path < rhs.path;
    });

    if (scripts.empty()) {
        const std::string message = FlyWithLuaLocalizedText("No Lua scripts found", "Luaスクリプトが見つかりません");
        XPLMAppendMenuItem(gFlyWithLuaScriptsMenu, message.c_str(), nullptr, 0);
        return;
    }

    for (const ScriptPathState& script : scripts) {
        const std::string groupKey = FlyWithLuaScriptMenuGroupForPath(script.path);
        FlyWithLuaScriptMenuGroup* group = nullptr;
        for (const auto& existingGroup : gFlyWithLuaScriptMenuGroups) {
            if (existingGroup && existingGroup->key == groupKey) {
                group = existingGroup.get();
                break;
            }
        }

        if (!group) {
            auto newGroup = std::make_unique<FlyWithLuaScriptMenuGroup>();
            newGroup->key = groupKey;
            const std::string groupLabel = FlyWithLuaScriptMenuGroupLabel(groupKey);
            newGroup->menuItemIndex = XPLMAppendMenuItem(gFlyWithLuaScriptsMenu, groupLabel.c_str(), nullptr, 1);
            if (newGroup->menuItemIndex < 0) {
                XPLMDebugString(("FlyWithLua-Mac Warning: Could not append script group menu item: " + groupKey + "\n").c_str());
                continue;
            }
            newGroup->menu = XPLMCreateMenu((scriptsMenuTitle + " " + groupLabel).c_str(),
                                            gFlyWithLuaScriptsMenu, newGroup->menuItemIndex,
                                            FlyWithLuaScriptsMenuHandler, nullptr);
            if (!newGroup->menu) {
                XPLMDebugString(("FlyWithLua-Mac Warning: Could not create script group submenu: " + groupKey + "\n").c_str());
                XPLMRemoveMenuItem(gFlyWithLuaScriptsMenu, newGroup->menuItemIndex);
                continue;
            }
            group = newGroup.get();
            gFlyWithLuaScriptMenuGroups.push_back(std::move(newGroup));
        }

        auto entry = std::make_unique<FlyWithLuaScriptMenuEntry>();
        entry->relativePath = script.path;
        entry->enabled = script.enabled;
        entry->conflict = script.conflict;
        FlyWithLuaScriptMenuEntry* entryPointer = entry.get();
        std::string label = entry->relativePath;
        if (entry->conflict) {
            label += FlyWithLuaLocalizedText(" [conflict]", " [競合]");
        } else if (!entry->enabled) {
            label += FlyWithLuaLocalizedText(" [disabled]", " [無効]");
        }
        entry->menuItemIndex = XPLMAppendMenuItem(group->menu, label.c_str(), entryPointer,
                                                  entry->conflict ? 0 : 1);
        if (entry->menuItemIndex >= 0) {
            XPLMCheckMenuItem(group->menu, entry->menuItemIndex,
                              entry->enabled ? xplm_Menu_Checked : xplm_Menu_Unchecked);
            gFlyWithLuaScriptMenuEntries.push_back(std::move(entry));
        }
    }
}

static bool ResolveScriptsDirectory() {
    char systemPath[512] = {0};
    XPLMGetSystemPath(systemPath);
    if (systemPath[0] != '\0') {
        const std::string runtimeDirectory = flywithlua::JoinPath(
            std::string(systemPath), "Resources/plugins/FlyWithLua");
        std::string canonical = flywithlua::JoinPath(runtimeDirectory, "Scripts");
        if (IsExistingDirectory(canonical)) {
            flywithlua::scriptDir = canonical;
            flywithlua::quarantineDir = flywithlua::JoinPath(runtimeDirectory, "Scripts (Quarantine)");
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
                std::string testPath = flywithlua::JoinPath(currentDir, "Scripts");
                if (IsExistingDirectory(testPath)) {
                    flywithlua::scriptDir = testPath;
                    flywithlua::quarantineDir = flywithlua::JoinPath(currentDir, "Scripts (Quarantine)");
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

static bool LuaDataRefArgument(lua_State* state, int index, XPLMDataRef& dataRef) {
    if (!lua_islightuserdata(state, index)) {
        return false;
    }

    dataRef = lua_touserdata(state, index);
    return dataRef != nullptr;
}

static void LogLuaCompatibilityArgumentError(const char* functionName) {
    flywithlua::logMsg(logToDevCon, std::string("FlyWithLua Error: Wrong arguments to function ") + functionName + ".");
}

static int LuaXPLMFindDataRef(lua_State* state) {
    std::string name;
    if (!LuaStringArg(state, 1, name)) {
        LogLuaCompatibilityArgumentError("XPLMFindDataRef");
        lua_pushnil(state);
        return 1;
    }

    XPLMDataRef dataRef = XPLMFindDataRef(name.c_str());
    if (dataRef) {
        lua_pushlightuserdata(state, dataRef);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

static int LuaXPLMGetDataRefTypes(lua_State* state) {
    XPLMDataRef dataRef = nullptr;
    if (!LuaDataRefArgument(state, 1, dataRef)) {
        LogLuaCompatibilityArgumentError("XPLMGetDataRefTypes");
        lua_pushinteger(state, xplmType_Unknown);
        return 1;
    }

    lua_pushinteger(state, XPLMGetDataRefTypes(dataRef));
    return 1;
}

static int LuaXPLMGetDatai(lua_State* state) {
    XPLMDataRef dataRef = nullptr;
    if (!LuaDataRefArgument(state, 1, dataRef)) {
        LogLuaCompatibilityArgumentError("XPLMGetDatai");
        lua_pushinteger(state, 0);
        return 1;
    }

    lua_pushinteger(state, XPLMGetDatai(dataRef));
    return 1;
}

static int LuaXPLMGetDataf(lua_State* state) {
    XPLMDataRef dataRef = nullptr;
    if (!LuaDataRefArgument(state, 1, dataRef)) {
        LogLuaCompatibilityArgumentError("XPLMGetDataf");
        lua_pushnumber(state, 0.0);
        return 1;
    }

    lua_pushnumber(state, XPLMGetDataf(dataRef));
    return 1;
}

static int LuaXPLMGetDatad(lua_State* state) {
    XPLMDataRef dataRef = nullptr;
    if (!LuaDataRefArgument(state, 1, dataRef)) {
        LogLuaCompatibilityArgumentError("XPLMGetDatad");
        lua_pushnumber(state, 0.0);
        return 1;
    }

    lua_pushnumber(state, XPLMGetDatad(dataRef));
    return 1;
}

static int LuaXPLMSetDatai(lua_State* state) {
    XPLMDataRef dataRef = nullptr;
    if (!LuaDataRefArgument(state, 1, dataRef) || !lua_isnumber(state, 2)) {
        LogLuaCompatibilityArgumentError("XPLMSetDatai");
        return 0;
    }

    XPLMSetDatai(dataRef, static_cast<int>(lua_tointeger(state, 2)));
    return 0;
}

static int LuaXPLMSetDataf(lua_State* state) {
    XPLMDataRef dataRef = nullptr;
    if (!LuaDataRefArgument(state, 1, dataRef) || !lua_isnumber(state, 2)) {
        LogLuaCompatibilityArgumentError("XPLMSetDataf");
        return 0;
    }

    XPLMSetDataf(dataRef, static_cast<float>(lua_tonumber(state, 2)));
    return 0;
}

static int LuaXPLMSetDatad(lua_State* state) {
    XPLMDataRef dataRef = nullptr;
    if (!LuaDataRefArgument(state, 1, dataRef) || !lua_isnumber(state, 2)) {
        LogLuaCompatibilityArgumentError("XPLMSetDatad");
        return 0;
    }

    XPLMSetDatad(dataRef, lua_tonumber(state, 2));
    return 0;
}

static bool LuaArrayArguments(lua_State* state,
                              const char* functionName,
                              XPLMDataRef& dataRef,
                              int& offset,
                              int& count) {
    if (!LuaDataRefArgument(state, 1, dataRef) ||
        !lua_isnumber(state, 2) ||
        !lua_isnumber(state, 3)) {
        LogLuaCompatibilityArgumentError(functionName);
        return false;
    }

    offset = static_cast<int>(lua_tointeger(state, 2));
    count = static_cast<int>(lua_tointeger(state, 3));
    if (offset < 0 || count < 0 || count > static_cast<int>(kHIDReportBufferSize)) {
        flywithlua::logMsg(logToDevCon, std::string("FlyWithLua Error: Invalid array range for ") + functionName + ".");
        return false;
    }
    return true;
}

static bool LuaSetArrayArguments(lua_State* state,
                                 const char* functionName,
                                 XPLMDataRef& dataRef,
                                 int& offset,
                                 int& count) {
    if (!LuaDataRefArgument(state, 1, dataRef) ||
        !lua_istable(state, 2) ||
        !lua_isnumber(state, 3) ||
        !lua_isnumber(state, 4)) {
        LogLuaCompatibilityArgumentError(functionName);
        return false;
    }

    offset = static_cast<int>(lua_tointeger(state, 3));
    count = static_cast<int>(lua_tointeger(state, 4));
    if (offset < 0 || count < 0 || count > static_cast<int>(kHIDReportBufferSize)) {
        flywithlua::logMsg(logToDevCon, std::string("FlyWithLua Error: Invalid array range for ") + functionName + ".");
        return false;
    }
    return true;
}

static int LuaXPLMGetDatavi(lua_State* state) {
    XPLMDataRef dataRef = nullptr;
    int offset = 0;
    int count = 0;
    if (!LuaArrayArguments(state, "XPLMGetDatavi", dataRef, offset, count)) {
        return 0;
    }

    std::vector<int> values(static_cast<size_t>(count), 0);
    const int actualCount = XPLMGetDatavi(dataRef, count > 0 ? values.data() : nullptr, offset, count);
    const int safeCount = std::max(0, std::min(actualCount, count));
    lua_createtable(state, safeCount, 0);
    for (int index = 0; index < safeCount; ++index) {
        lua_pushinteger(state, offset + index);
        lua_pushinteger(state, values[static_cast<size_t>(index)]);
        lua_settable(state, -3);
    }
    return 1;
}

static int LuaXPLMGetDatavf(lua_State* state) {
    XPLMDataRef dataRef = nullptr;
    int offset = 0;
    int count = 0;
    if (!LuaArrayArguments(state, "XPLMGetDatavf", dataRef, offset, count)) {
        return 0;
    }

    std::vector<float> values(static_cast<size_t>(count), 0.0f);
    const int actualCount = XPLMGetDatavf(dataRef, count > 0 ? values.data() : nullptr, offset, count);
    const int safeCount = std::max(0, std::min(actualCount, count));
    lua_createtable(state, safeCount, 0);
    for (int index = 0; index < safeCount; ++index) {
        lua_pushinteger(state, offset + index);
        lua_pushnumber(state, values[static_cast<size_t>(index)]);
        lua_settable(state, -3);
    }
    return 1;
}

static bool LuaArrayValues(lua_State* state, int tableIndex, int offset, int count, int* values) {
    if (!lua_istable(state, tableIndex)) {
        return false;
    }

    for (int index = 0; index < count; ++index) {
        lua_rawgeti(state, tableIndex, offset + index);
        if (!lua_isnumber(state, -1)) {
            lua_pop(state, 1);
            return false;
        }
        values[index] = static_cast<int>(lua_tointeger(state, -1));
        lua_pop(state, 1);
    }
    return true;
}

static bool LuaArrayValues(lua_State* state, int tableIndex, int offset, int count, float* values) {
    if (!lua_istable(state, tableIndex)) {
        return false;
    }

    for (int index = 0; index < count; ++index) {
        lua_rawgeti(state, tableIndex, offset + index);
        if (!lua_isnumber(state, -1)) {
            lua_pop(state, 1);
            return false;
        }
        values[index] = static_cast<float>(lua_tonumber(state, -1));
        lua_pop(state, 1);
    }
    return true;
}

static int LuaXPLMSetDatavi(lua_State* state) {
    XPLMDataRef dataRef = nullptr;
    int offset = 0;
    int count = 0;
    if (!LuaSetArrayArguments(state, "XPLMSetDatavi", dataRef, offset, count)) {
        return 0;
    }

    std::vector<int> values(static_cast<size_t>(count), 0);
    if (!LuaArrayValues(state, 2, offset, count, values.data())) {
        LogLuaCompatibilityArgumentError("XPLMSetDatavi");
        return 0;
    }
    XPLMSetDatavi(dataRef, values.data(), offset, count);
    return 0;
}

static int LuaXPLMSetDatavf(lua_State* state) {
    XPLMDataRef dataRef = nullptr;
    int offset = 0;
    int count = 0;
    if (!LuaSetArrayArguments(state, "XPLMSetDatavf", dataRef, offset, count)) {
        return 0;
    }

    std::vector<float> values(static_cast<size_t>(count), 0.0f);
    if (!LuaArrayValues(state, 2, offset, count, values.data())) {
        LogLuaCompatibilityArgumentError("XPLMSetDatavf");
        return 0;
    }
    XPLMSetDatavf(dataRef, values.data(), offset, count);
    return 0;
}

static bool LuaHIDDeviceArgument(lua_State* state, int index, hid_device*& device) {
    if (!lua_islightuserdata(state, index)) {
        return false;
    }
    device = static_cast<hid_device*>(lua_touserdata(state, index));
    return device != nullptr &&
           std::find(gOpenHIDDevices.begin(), gOpenHIDDevices.end(), device) != gOpenHIDDevices.end();
}

static void TrackHIDDevice(hid_device* device) {
    if (device && std::find(gOpenHIDDevices.begin(), gOpenHIDDevices.end(), device) == gOpenHIDDevices.end()) {
        gOpenHIDDevices.push_back(device);
    }
}

static void UntrackHIDDevice(hid_device* device) {
    gOpenHIDDevices.erase(std::remove(gOpenHIDDevices.begin(), gOpenHIDDevices.end(), device), gOpenHIDDevices.end());
}

static void CloseAllOpenHIDDevices() {
    std::vector<hid_device*> devices;
    devices.swap(gOpenHIDDevices);
    for (hid_device* device : devices) {
        if (device) {
            hid_close(device);
        }
    }
}

static void ShutdownHID() {
    CloseAllOpenHIDDevices();
    if (gHIDInitialized) {
        hid_exit();
        gHIDInitialized = false;
    }
}

static std::string HIDWideString(const wchar_t* value) {
    if (!value) {
        return {};
    }

    std::string result;
    for (const wchar_t* character = value; *character != L'\0'; ++character) {
        const wchar_t codePoint = *character;
        result.push_back(codePoint >= 32 && codePoint <= 126 ? static_cast<char>(codePoint) : '?');
    }
    return result;
}

static void SetHIDTableField(lua_State* state, const char* key, int value) {
    lua_pushstring(state, key);
    lua_pushinteger(state, value);
    lua_settable(state, -3);
}

static void SetHIDTableField(lua_State* state, const char* key, const char* value) {
    lua_pushstring(state, key);
    lua_pushstring(state, value ? value : "");
    lua_settable(state, -3);
}

static int LuaCreateHIDTable(lua_State* state) {
    lua_newtable(state);
    if (!gHIDInitialized) {
        lua_pushinteger(state, 0);
        return 2;
    }

    int count = 0;
    hid_device_info* devices = hid_enumerate(0, 0);
    for (hid_device_info* device = devices; device; device = device->next) {
        ++count;
        lua_newtable(state);
        SetHIDTableField(state, "vendor_id", device->vendor_id);
        SetHIDTableField(state, "product_id", device->product_id);
        SetHIDTableField(state, "release_number", device->release_number);
        SetHIDTableField(state, "interface_number", device->interface_number);
        SetHIDTableField(state, "usage_page", device->usage_page);
        SetHIDTableField(state, "usage", device->usage);
        SetHIDTableField(state, "path", device->path);
        const std::string serial = HIDWideString(device->serial_number);
        const std::string manufacturer = HIDWideString(device->manufacturer_string);
        const std::string product = HIDWideString(device->product_string);
        SetHIDTableField(state, "serial_number", serial.c_str());
        SetHIDTableField(state, "manufacturer_string", manufacturer.c_str());
        SetHIDTableField(state, "product_string", product.c_str());
        lua_rawseti(state, -2, count);
    }
    if (devices) {
        hid_free_enumeration(devices);
    }

    lua_pushinteger(state, count);
    return 2;
}

static bool LuaHIDByteArguments(lua_State* state, int firstIndex, std::vector<unsigned char>& bytes) {
    const int argumentCount = lua_gettop(state);
    if (argumentCount < firstIndex) {
        return false;
    }
    const int byteCount = argumentCount - firstIndex + 1;
    if (byteCount > static_cast<int>(kHIDReportBufferSize)) {
        return false;
    }

    bytes.resize(static_cast<size_t>(byteCount));
    for (int index = firstIndex; index <= argumentCount; ++index) {
        if (!lua_isnumber(state, index)) {
            return false;
        }
        const lua_Number value = lua_tonumber(state, index);
        if (value < 0 || value > 255) {
            return false;
        }
        bytes[static_cast<size_t>(index - firstIndex)] = static_cast<unsigned char>(value);
    }
    return true;
}

static int LuaHIDOpen(lua_State* state) {
    if (!gHIDInitialized) {
        lua_pushnil(state);
        return 1;
    }

    if (!lua_isnumber(state, 1) || !lua_isnumber(state, 2)) {
        LogLuaCompatibilityArgumentError("hid_open");
        lua_pushnil(state);
        return 1;
    }

    const lua_Integer vendorID = lua_tointeger(state, 1);
    const lua_Integer productID = lua_tointeger(state, 2);
    if (vendorID < 0 || vendorID > 0xffff || productID < 0 || productID > 0xffff) {
        LogLuaCompatibilityArgumentError("hid_open");
        lua_pushnil(state);
        return 1;
    }

    hid_device* device = hid_open(static_cast<unsigned short>(vendorID), static_cast<unsigned short>(productID), nullptr);
    if (!device) {
        lua_pushnil(state);
        return 1;
    }

    TrackHIDDevice(device);
    lua_pushlightuserdata(state, device);
    return 1;
}

static int LuaHIDOpenPath(lua_State* state) {
    if (!gHIDInitialized) {
        lua_pushnil(state);
        return 1;
    }

    std::string path;
    if (!LuaStringArg(state, 1, path)) {
        LogLuaCompatibilityArgumentError("hid_open_path");
        lua_pushnil(state);
        return 1;
    }

    hid_device* device = hid_open_path(path.c_str());
    if (!device) {
        lua_pushnil(state);
        return 1;
    }

    TrackHIDDevice(device);
    lua_pushlightuserdata(state, device);
    return 1;
}

static int LuaHIDClose(lua_State* state) {
    hid_device* device = nullptr;
    if (!LuaHIDDeviceArgument(state, 1, device)) {
        LogLuaCompatibilityArgumentError("hid_close");
        return 0;
    }

    if (std::find(gOpenHIDDevices.begin(), gOpenHIDDevices.end(), device) == gOpenHIDDevices.end()) {
        return 0;
    }
    UntrackHIDDevice(device);
    hid_close(device);
    return 0;
}

static int LuaHIDWrite(lua_State* state) {
    hid_device* device = nullptr;
    std::vector<unsigned char> bytes;
    if (!LuaHIDDeviceArgument(state, 1, device) || !LuaHIDByteArguments(state, 2, bytes)) {
        LogLuaCompatibilityArgumentError("hid_write");
        return 0;
    }

    lua_pushinteger(state, hid_write(device, bytes.data(), bytes.size()));
    return 1;
}

static bool LuaHIDReadArguments(lua_State* state, hid_device*& device, int& length) {
    if (!LuaHIDDeviceArgument(state, 1, device) || !lua_isnumber(state, 2)) {
        return false;
    }
    length = static_cast<int>(lua_tointeger(state, 2));
    return length > 0 && length <= static_cast<int>(kHIDReportBufferSize);
}

static int LuaHIDRead(lua_State* state) {
    hid_device* device = nullptr;
    int length = 0;
    if (!LuaHIDReadArguments(state, device, length)) {
        LogLuaCompatibilityArgumentError("hid_read");
        return 0;
    }

    std::vector<unsigned char> bytes(static_cast<size_t>(length), 0);
    const int result = hid_read(device, bytes.data(), bytes.size());
    lua_pushinteger(state, result);
    if (result <= 0) {
        return 1;
    }
    for (int index = 0; index < result && index < length; ++index) {
        lua_pushinteger(state, bytes[static_cast<size_t>(index)]);
    }
    return 1 + std::min(result, length);
}

static int LuaHIDReadTimeout(lua_State* state) {
    hid_device* device = nullptr;
    int length = 0;
    if (!LuaHIDReadArguments(state, device, length) || !lua_isnumber(state, 3)) {
        LogLuaCompatibilityArgumentError("hid_read_timeout");
        return 0;
    }

    std::vector<unsigned char> bytes(static_cast<size_t>(length), 0);
    const int result = hid_read_timeout(device, bytes.data(), bytes.size(), static_cast<int>(lua_tointeger(state, 3)));
    lua_pushinteger(state, result);
    if (result <= 0) {
        return 1;
    }
    for (int index = 0; index < result && index < length; ++index) {
        lua_pushinteger(state, bytes[static_cast<size_t>(index)]);
    }
    return 1 + std::min(result, length);
}

static int LuaHIDSetNonblocking(lua_State* state) {
    hid_device* device = nullptr;
    if (!LuaHIDDeviceArgument(state, 1, device) || !lua_isnumber(state, 2)) {
        LogLuaCompatibilityArgumentError("hid_set_nonblocking");
        return 0;
    }

    const int nonblocking = static_cast<int>(lua_tointeger(state, 2));
    if (nonblocking != 0 && nonblocking != 1) {
        LogLuaCompatibilityArgumentError("hid_set_nonblocking");
        return 0;
    }
    lua_pushinteger(state, hid_set_nonblocking(device, nonblocking));
    return 1;
}

static int LuaHIDSendFeatureReport(lua_State* state) {
    hid_device* device = nullptr;
    std::vector<unsigned char> bytes;
    if (!LuaHIDDeviceArgument(state, 1, device) || !LuaHIDByteArguments(state, 2, bytes)) {
        LogLuaCompatibilityArgumentError("hid_send_feature_report");
        return 0;
    }

    lua_pushinteger(state, hid_send_feature_report(device, bytes.data(), bytes.size()));
    return 1;
}

static int LuaHIDSendFilledFeatureReport(lua_State* state) {
    hid_device* device = nullptr;
    if (!LuaHIDDeviceArgument(state, 1, device) || !lua_isnumber(state, 2) || !lua_isnumber(state, 3)) {
        LogLuaCompatibilityArgumentError("hid_send_filled_feature_report");
        return 0;
    }

    const int reportID = static_cast<int>(lua_tointeger(state, 2));
    const int length = static_cast<int>(lua_tointeger(state, 3));
    const int payloadArgumentCount = lua_gettop(state) - 3;
    if (reportID < 0 || reportID > 255 ||
        length <= 0 || length > static_cast<int>(kHIDReportBufferSize) ||
        payloadArgumentCount > length - 1) {
        LogLuaCompatibilityArgumentError("hid_send_filled_feature_report");
        return 0;
    }

    // HIDAPI expects the report ID in byte zero and the total report length
    // includes that byte. The remaining legacy arguments are payload bytes;
    // zero-fill the unused tail to preserve the original report size.
    std::vector<unsigned char> bytes(static_cast<size_t>(length), 0);
    bytes[0] = static_cast<unsigned char>(reportID);
    for (int index = 4; index <= lua_gettop(state) && index - 3 < length; ++index) {
        if (!lua_isnumber(state, index)) {
            LogLuaCompatibilityArgumentError("hid_send_filled_feature_report");
            return 0;
        }
        const lua_Number value = lua_tonumber(state, index);
        if (value < 0 || value > 255) {
            LogLuaCompatibilityArgumentError("hid_send_filled_feature_report");
            return 0;
        }
        bytes[static_cast<size_t>(index - 3)] = static_cast<unsigned char>(value);
    }

    lua_pushinteger(state, hid_send_feature_report(device, bytes.data(), bytes.size()));
    return 1;
}

static int LuaHIDGetFeatureReport(lua_State* state) {
    hid_device* device = nullptr;
    if (!LuaHIDDeviceArgument(state, 1, device) || !lua_isnumber(state, 2)) {
        LogLuaCompatibilityArgumentError("hid_get_feature_report");
        return 0;
    }

    const int length = static_cast<int>(lua_tointeger(state, 2));
    if (length <= 0 || length >= static_cast<int>(kHIDReportBufferSize)) {
        LogLuaCompatibilityArgumentError("hid_get_feature_report");
        return 0;
    }

    std::vector<unsigned char> bytes(static_cast<size_t>(length + 1), 0);
    const int result = hid_get_feature_report(device, bytes.data(), bytes.size());
    lua_pushinteger(state, result);
    if (result <= 0) {
        return 1;
    }

    // hid_get_feature_report() includes the report ID in both the result and
    // byte zero. Return every byte after the count, including that ID, as the
    // legacy Lua API expects.
    const int returnedByteCount = std::min(result, static_cast<int>(bytes.size()));
    for (int index = 0; index < returnedByteCount; ++index) {
        lua_pushinteger(state, bytes[static_cast<size_t>(index)]);
    }
    return 1 + returnedByteCount;
}

static void SetPositiveEdgeFlipValue(FlyWithLuaPositiveEdgeFlip& flip, bool useOnValue) {
    const bool hasScalarDouble = (flip.dataType & xplmType_Double) != 0;
    const bool hasScalarFloat = (flip.dataType & xplmType_Float) != 0;
    const bool hasScalarInt = (flip.dataType & xplmType_Int) != 0;
    const bool hasFloatArray = (flip.dataType & xplmType_FloatArray) != 0;
    const bool hasIntArray = (flip.dataType & xplmType_IntArray) != 0;

    if (hasScalarDouble) {
        const double value = useOnValue ? flip.onDouble : flip.offDouble;
        XPLMSetDatad(flip.dataRef, value);
    } else if (hasScalarFloat) {
        const float value = useOnValue ? flip.onFloat : flip.offFloat;
        XPLMSetDataf(flip.dataRef, value);
    } else if (hasScalarInt) {
        const int value = useOnValue ? flip.onInt : flip.offInt;
        XPLMSetDatai(flip.dataRef, value);
    } else if (hasFloatArray) {
        float value = useOnValue ? flip.onFloat : flip.offFloat;
        XPLMSetDatavf(flip.dataRef, &value, flip.index, 1);
    } else if (hasIntArray) {
        int value = useOnValue ? flip.onInt : flip.offInt;
        XPLMSetDatavi(flip.dataRef, &value, flip.index, 1);
    }
}

static bool PositiveEdgeFlipIsOn(const FlyWithLuaPositiveEdgeFlip& flip) {
    if ((flip.dataType & xplmType_Double) != 0) {
        return XPLMGetDatad(flip.dataRef) == flip.onDouble;
    }
    if ((flip.dataType & xplmType_Float) != 0) {
        return XPLMGetDataf(flip.dataRef) == flip.onFloat;
    }
    if ((flip.dataType & xplmType_Int) != 0) {
        return XPLMGetDatai(flip.dataRef) == flip.onInt;
    }
    if ((flip.dataType & xplmType_FloatArray) != 0) {
        float value = 0.0f;
        XPLMGetDatavf(flip.dataRef, &value, flip.index, 1);
        return value == flip.onFloat;
    }
    if ((flip.dataType & xplmType_IntArray) != 0) {
        int value = 0;
        XPLMGetDatavi(flip.dataRef, &value, flip.index, 1);
        return value == flip.onInt;
    }
    return false;
}

static int LuaCreatePositiveEdgeFlip(lua_State* state) {
    if (!lua_isnumber(state, 1) || !lua_isstring(state, 2)) {
        LogLuaCompatibilityArgumentError("create_positive_edge_flip");
        return 0;
    }

    const int button = static_cast<int>(lua_tointeger(state, 1));
    std::string path;
    LuaStringArg(state, 2, path);
    if (button < 0 || path.empty()) {
        LogLuaCompatibilityArgumentError("create_positive_edge_flip");
        return 0;
    }

    XPLMDataRef dataRef = XPLMFindDataRef(path.c_str());
    if (!dataRef) {
        XPLMDebugString(("FlyWithLua Warning: create_positive_edge_flip() DataRef not found: " + path + "\n").c_str());
        return 0;
    }

    FlyWithLuaPositiveEdgeFlip flip;
    flip.button = button;
    flip.dataRef = dataRef;
    flip.dataType = XPLMGetDataRefTypes(dataRef);
    const XPLMDataTypeID supportedTypes = xplmType_Int | xplmType_Float | xplmType_Double |
                                          xplmType_FloatArray | xplmType_IntArray;
    if ((flip.dataType & supportedTypes) == 0) {
        XPLMDebugString(("FlyWithLua Warning: create_positive_edge_flip() DataRef has no supported type: " + path + "\n").c_str());
        return 0;
    }
    flip.index = lua_isnumber(state, 3) ? static_cast<int>(lua_tointeger(state, 3)) : 0;
    if (flip.index < 0) {
        LogLuaCompatibilityArgumentError("create_positive_edge_flip");
        return 0;
    }

    const lua_Number offValue = lua_isnumber(state, 4) ? lua_tonumber(state, 4) : 0.0;
    const lua_Number onValue = lua_isnumber(state, 5) ? lua_tonumber(state, 5) : 1.0;
    flip.offInt = static_cast<int>(offValue);
    flip.onInt = static_cast<int>(onValue);
    flip.offFloat = static_cast<float>(offValue);
    flip.onFloat = static_cast<float>(onValue);
    flip.offDouble = static_cast<double>(offValue);
    flip.onDouble = static_cast<double>(onValue);
    gPositiveEdgeFlips.push_back(flip);
    return 0;
}

static void PollPositiveEdgeFlips() {
    if (gPositiveEdgeFlips.empty()) {
        return;
    }

    if (!gJoystickButtonDataRef) {
        gJoystickButtonDataRef = XPLMFindDataRef("sim/joystick/joystick_button_values");
    }
    if (!gJoystickButtonDataRef) {
        return;
    }

    const int buttonCount = XPLMGetDatavi(gJoystickButtonDataRef, nullptr, 0, 0);
    if (buttonCount <= 0 || buttonCount > static_cast<int>(kHIDReportBufferSize)) {
        return;
    }

    gJoystickButtonValues.resize(static_cast<size_t>(buttonCount));
    XPLMGetDatavi(gJoystickButtonDataRef, gJoystickButtonValues.data(), 0, buttonCount);
    for (FlyWithLuaPositiveEdgeFlip& flip : gPositiveEdgeFlips) {
        const bool pressed = flip.button < buttonCount &&
                             gJoystickButtonValues[static_cast<size_t>(flip.button)] != 0;
        if (pressed && !flip.lastPressed) {
            SetPositiveEdgeFlipValue(flip, !PositiveEdgeFlipIsOn(flip));
        }
        flip.lastPressed = pressed;
    }
}

static int LuaDoEveryDrawCallback(lua_State* state) {
    AppendLuaCallback(state, gDrawCommand,
                      gLoadingPanelScript ? gPanelDrawCallbacks : gDrawCallbacks,
                      gLoadingPanelScript ? LuaCallbackKind::CompatPanelDraw : LuaCallbackKind::Draw);
    return 0;
}

static int LuaDoEveryPanelDrawCallback(lua_State* state) {
    AppendLuaCallback(state, gPanelApiDrawCommand, gPanelApiDrawCallbacks,
                      LuaCallbackKind::PanelDraw);
    return 0;
}

static int LuaDoEveryFrameCallback(lua_State* state) {
    AppendLuaCallback(state, gEveryFrameCommand, gEveryFrameCallbacks, LuaCallbackKind::EveryFrame);
    return 0;
}

static int LuaDoOftenCallback(lua_State* state) {
    AppendLuaCallback(state, gOftenCommand, gOftenCallbacks, LuaCallbackKind::Often);
    return 0;
}

static int LuaDoSometimesCallback(lua_State* state) {
    AppendLuaCallback(state, gSometimesCommand, gSometimesCallbacks, LuaCallbackKind::Sometimes);
    return 0;
}

static int LuaDoOnExitCallback(lua_State* state) {
    AppendLuaCallback(state, gOnExitCommand, gOnExitCallbacks, LuaCallbackKind::OnExit);
    return 0;
}

static int LuaDoOnMouseClickCallback(lua_State* state) {
    AppendLuaCallback(state, gMouseClickCommand, gMouseClickCallbacks, LuaCallbackKind::MouseClick);
    return 0;
}

static int LuaDoOnMouseWheelCallback(lua_State* state) {
    AppendLuaCallback(state, gMouseWheelCommand, gMouseWheelCallbacks, LuaCallbackKind::MouseWheel);
    return 0;
}

static void RegisterFlyWithLuaCompatibilityFunctions(lua_State* state) {
    lua_register(state, "XPLMFindDataRef", LuaXPLMFindDataRef);
    lua_register(state, "XPLMGetDataRefTypes", LuaXPLMGetDataRefTypes);
    lua_register(state, "XPLMGetDatai", LuaXPLMGetDatai);
    lua_register(state, "XPLMGetDataf", LuaXPLMGetDataf);
    lua_register(state, "XPLMGetDatad", LuaXPLMGetDatad);
    lua_register(state, "XPLMSetDatai", LuaXPLMSetDatai);
    lua_register(state, "XPLMSetDataf", LuaXPLMSetDataf);
    lua_register(state, "XPLMSetDatad", LuaXPLMSetDatad);
    lua_register(state, "XPLMGetDatavi", LuaXPLMGetDatavi);
    lua_register(state, "XPLMGetDatavf", LuaXPLMGetDatavf);
    lua_register(state, "XPLMSetDatavi", LuaXPLMSetDatavi);
    lua_register(state, "XPLMSetDatavf", LuaXPLMSetDatavf);
    lua_register(state, "create_HID_table", LuaCreateHIDTable);
    lua_register(state, "hid_open", LuaHIDOpen);
    lua_register(state, "hid_open_path", LuaHIDOpenPath);
    lua_register(state, "hid_close", LuaHIDClose);
    lua_register(state, "hid_write", LuaHIDWrite);
    lua_register(state, "hid_read", LuaHIDRead);
    lua_register(state, "hid_read_timeout", LuaHIDReadTimeout);
    lua_register(state, "hid_set_nonblocking", LuaHIDSetNonblocking);
    lua_register(state, "hid_send_feature_report", LuaHIDSendFeatureReport);
    lua_register(state, "hid_send_filled_feature_report", LuaHIDSendFilledFeatureReport);
    lua_register(state, "hid_get_feature_report", LuaHIDGetFeatureReport);
    lua_register(state, "add_macro", LuaAddMacro);
    lua_register(state, "activate_macro", LuaActivateMacro);
    lua_register(state, "deactivate_macro", LuaDeactivateMacro);
    lua_register(state, "create_positive_edge_flip", LuaCreatePositiveEdgeFlip);
    lua_register(state, "do_every_draw", LuaDoEveryDrawCallback);
    lua_register(state, "do_every_panel_draw", LuaDoEveryPanelDrawCallback);
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
    ShutdownHID();
    gPositiveEdgeFlips.clear();
    gJoystickButtonValues.clear();
    flywithlua::panel::invalidateLuaResources();
    if (L) {
        InvalidateAllLuaCallbackChunks();
        ResetLuaScriptRegistry();
        ClearFlyWithLuaCommands();
        lua_close(L);
    }
    L = nullptr;
    lState = nullptr;
    flywithlua::FWLLua = nullptr;
    flywithlua::LuaIsRunning = false;
    gDrawCommand.clear();
    gPanelApiDrawCommand.clear();
    gEveryFrameCommand.clear();
    gOftenCommand.clear();
    gSometimesCommand.clear();
    gOnExitCommand.clear();
    gMouseClickCommand.clear();
    gMouseWheelCommand.clear();
    gMouseClickCaptured = false;
    gOftenAccumulator = 0.0f;
    gSometimesAccumulator = 0.0f;
    gAltitudeAccumulator = 0.0f;
    threejfps_on_lua_reset();
}

extern "C" void flywithlua_reload_scripts(void) {
    if (flywithlua::scriptDir.empty()) {
        XPLMDebugString("FlyWithLua-Mac Warning: Cannot reload scripts because the Scripts directory is unknown.\n");
        flywithlua_update_last_log_message("Scripts folder not found");
        return;
    }

    XPLMDebugString("FlyWithLua-Mac: Reloading scripts.\n");
    flywithlua_update_last_log_message("Reloading scripts...");
    flywithlua_update_script_load_summary(0, 0, 0, "[]");

    RunLuaCallbackEntries(LuaCallbackKind::OnExit, gOnExitCallbacks, "do_on_exit");
    DestroyMouseEventWindow();
    flwnd::deinitFloatingWindowSupport();
    // Keep X-Plane-owned FMOD channel groups alive across a Lua reload. Only
    // sounds created by the old Lua state need to be released here.
    fmodint::deinitFmodSupport();
    ResetLuaRuntimeState();
    flywithlua::panel::shutdown();

    if (!InitializeLuaRuntime(false)) {
        XPLMDebugString("FlyWithLua-Mac Warning: Script reload failed.\n");
        flywithlua_update_last_log_message("Script reload failed");
        return;
    }

    CreateMouseEventWindow();
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
    if (!gHIDInitialized && hid_init() != 0) {
        XPLMDebugString("FlyWithLua-Mac Warning: HIDAPI initialization failed; HID functions will remain unavailable.\n");
    } else {
        gHIDInitialized = true;
    }
    if (!RegisterLuaBuiltinModule(L, "socket.core", luaopen_socket_core)) {
        XPLMDebugString("FlyWithLua-Mac Warning: Could not register built-in socket.core module.\n");
    }
    if (!RegisterLuaBuiltinModule(L, "mime.core", luaopen_mime_core)) {
        XPLMDebugString("FlyWithLua-Mac Warning: Could not register built-in mime.core module.\n");
    }
    if (!RegisterLuaBuiltinModule(L, "LuaXML_lib", luaopen_LuaXML_lib)) {
        XPLMDebugString("FlyWithLua-Mac Warning: Could not register built-in LuaXML_lib module.\n");
    }
    if (!RegisterLuaBuiltinModule(L, "socket.unix", luaopen_socket_unix)) {
        XPLMDebugString("FlyWithLua-Mac Warning: Could not register built-in socket.unix module.\n");
    }
    if (!RegisterLuaBuiltinModule(L, "socket.serial", luaopen_socket_serial)) {
        XPLMDebugString("FlyWithLua-Mac Warning: Could not register built-in socket.serial module.\n");
    }
    // These functions must exist before the embedded init script defines its
    // dataref helpers and before any user script is loaded.
    RegisterFlyWithLuaCompatibilityFunctions(L);
    flywithlua::panel::registerLuaFunctions(L);
    flywithlua::FWLLua = L;
    flywithlua::LuaIsRunning = true;
    lState = L;

    lua_pushstring(L, "APL");
    lua_setglobal(L, "SYSTEM");

    lua_pushstring(L, "/");
    lua_setglobal(L, "DIRECTORY_SEPARATOR");

    std::string scriptDirectory = flywithlua::scriptDir;
    if (!scriptDirectory.empty() && scriptDirectory.back() != '/') {
        scriptDirectory.push_back('/');
    }
    lua_pushstring(L, scriptDirectory.c_str());
    lua_setglobal(L, "SCRIPT_DIRECTORY");

    UpdateLuaAircraftGlobals();

    std::string mainDir = GetMainDirectoryFromScripts();

    lua_pushstring(L, mainDir.c_str());
    lua_setglobal(L, "PLUGIN_MAIN_DIRECTORY");

    lua_pushstring(L, (mainDir + "/Internals/").c_str());
    lua_setglobal(L, "INTERNALS_DIRECTORY");

    lua_pushstring(L, (mainDir + "/Modules/").c_str());
    lua_setglobal(L, "MODULES_DIRECTORY");

    register_swift_bridge(L);
    threejfps_register_lua_functions(L);
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

-- Some legacy scripts call the bubble helpers from their first draw callback.
-- Load them before the legacy .ini is evaluated so an earlier .ini error cannot
-- leave those globals undefined while the rest of Lua continues to start.
local bubblesOk, bubblesError = pcall(dofile, INTERNALS_DIRECTORY .. "bubbles.lua")
if not bubblesOk then
    mac_native.log_msg("FlyWithLua Warning: Could not load bubbles.lua: " .. tostring(bubblesError))
end

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

-- Legacy FlyWithLua aliases. DataRef() is the historical capitalization and
-- set_array() receives (name, index, value), unlike the native set() order.
function set_array(n, index, v)
    return set(n, v, index)
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

DataRef = dataref
)lua";
    ReplaceAll(initScript, "__INTERNALS__", internalsDir);
    ReplaceAll(initScript, "__MODULES__", modulesDir);

	if (luaL_dostring(L, initScript.c_str())) {
		const char* luaError = lua_tostring(L, -1);
		const std::string errorMessage = luaError ? luaError : "unknown Lua error";
		XPLMDebugString(("FlyWithLua-Mac Lua Init Error: " + errorMessage + "\n").c_str());
		lua_pop(L, 1);
	}

    // Preserve the legacy globals used by HID scripts. The native functions
    // remain safe no-ops when hid_init() could not access a device backend.
	if (luaL_dostring(L, "ALL_HID_DEVICES, NUMBER_OF_HID_DEVICES = create_HID_table()")) {
		const char* luaError = lua_tostring(L, -1);
		const std::string errorMessage = luaError ? luaError : "unknown Lua error";
		XPLMDebugString(("FlyWithLua-Mac HID initialization script error: " + errorMessage + "\n").c_str());
		lua_pop(L, 1);
	}
    char xplanePath[512];
    XPLMGetSystemPath(xplanePath);
    // Keep the legacy trailing separator: SaveInitialAssignments.ini builds
    // paths directly from SYSTEM_DIRECTORY. Callers that append a path use
    // the separator-aware form below instead of adding another slash.
    lua_pushstring(L, xplanePath);
    lua_setglobal(L, "SYSTEM_DIRECTORY");

    flwnd::initFloatingWindowSupport();

    fmodint::RegisterFmodFunctionsToLua(L);

    flywithlua::process_read_ini_file();
    ConfigureLuaCallbackScope();
    flywithlua::panel::configureBackend(getOptionToString("DrawBackend"));
    flywithlua::panel::initialize();

    if (registerFlightLoop) {
        XPLMRegisterFlightLoopCallback(FlightLoopCallback, -1.0f, nullptr);
        XPLMRegisterDrawCallback(FlyWithLuaDrawCallback, xplm_Phase_Window, 0, (void*) "FlyWithLua-MacScriptDraw");
    }

    gSuppressMacroMenuRefresh = true;
    gMacroMenuNeedsRefresh = false;
    flywithlua::ReadAllScriptFiles();
    gSuppressMacroMenuRefresh = false;
    if (gMacroMenuNeedsRefresh || !gFlyWithLuaMacrosMenu) {
        RefreshFlyWithLuaMacrosMenu();
    }
    RefreshFlyWithLuaScriptsMenu();
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

    LuaScriptId CurrentLuaScriptId() {
        return gCurrentLuaScriptId;
    }

    bool IsLuaPanelApiAllowed() {
        return gCurrentLuaPanelApiAllowed;
    }

    bool IsLuaScriptQuarantined(LuaScriptId scriptId) {
        if (scriptId == kSystemLuaScriptId) {
            return false;
        }
        const auto found = gLuaScriptRecords.find(scriptId);
        return found != gLuaScriptRecords.end() && found->second.quarantined;
    }

    LuaScriptScope::LuaScriptScope(LuaScriptId scriptId):
        previousScriptId_(gCurrentLuaScriptId) {
        gCurrentLuaScriptId = scriptId;
    }

    LuaScriptScope::~LuaScriptScope() {
        gCurrentLuaScriptId = previousScriptId_;
    }

    LuaPanelApiScope::LuaPanelApiScope(bool allowed):
        previousAllowed_(gCurrentLuaPanelApiAllowed) {
        gCurrentLuaPanelApiAllowed = allowed;
    }

    LuaPanelApiScope::~LuaPanelApiScope() {
        gCurrentLuaPanelApiAllowed = previousAllowed_;
    }

    void ReportLuaScriptError(LuaScriptId scriptId, const char* context,
                              const std::string& message) {
        QuarantineLuaScript(scriptId, context != nullptr ? context : "lua", message);
    }

    void panic(const std::string& message) {
        logMsg(logToAll, "PANIC: " + message);
        ReportLuaScriptError(CurrentLuaScriptId(), "panic", message);
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
        InvalidateAllLuaCallbackChunks();
        ResetLuaScriptRegistry();
        gScriptLoadFailures.clear();
        gDiscoveredScriptCount = 0;
        gLoadedScriptCount = 0;
        gFailedScriptCount = 0;

        if (!IsExistingDirectory(scriptDir)) {
            logMsg(logToDevCon, "Failed to read directory: " + scriptDir);
            flywithlua_update_script_load_summary(0, 0, 0, "[]");
            flywithlua_update_last_log_message("Failed to read scripts folder");
            return false;
        }

        CollectScriptFilesRecursive(scriptDir, "", fileNames);
        std::sort(fileNames.begin(), fileNames.end());
        gDiscoveredScriptCount = static_cast<int>(fileNames.size());

        if (fileNames.empty()) {
            logMsg(logToDevCon, "No script files found in: " + scriptDir);
            flywithlua_update_script_load_summary(0, 0, 0, "[]");
            flywithlua_update_last_log_message("No scripts found");
            return true;
        }

        for (const std::string& fileName : fileNames) {
            std::string fullPath = JoinPath(scriptDir, fileName);
            logMsg(logToDevCon, "Loading script: " + fileName);
            const flywithlua::LuaScriptId scriptId = gNextLuaScriptId++;
            gLuaScriptOrder.push_back(scriptId);
            gLuaScriptRecords.emplace(scriptId, LuaScriptRecord{scriptId, fileName, false, false, false});
            gLoadingPanelScript = IsBundledPanelScript(fileName);
            gLoadingLuaScriptId = scriptId;
            {
                flywithlua::LuaScriptScope scriptScope(scriptId);
                if (luaL_dofile(FWLLua, fullPath.c_str())) {
                    const char* luaError = lua_tostring(FWLLua, -1);
                    std::string errorMessage = luaError ? luaError : "Unknown Lua error";
                    logMsg(logToDevCon, "Error loading " + fileName + ": " + errorMessage);
                    lua_pop(FWLLua, 1);
                    auto& record = gLuaScriptRecords.at(scriptId);
                    record.quarantined = true;
                    record.errorReported = true;
                    flywithlua::panel::invalidateLuaResources(scriptId);
                    flwnd::quarantineWindowsOwnedBy(scriptId);
                    const auto callbacksIt = gLuaScriptCallbacks.find(scriptId);
                    if (callbacksIt != gLuaScriptCallbacks.end()) {
                        for (LuaCallbackEntries& callbacks : callbacksIt->second.entries) {
                            InvalidateLuaCallbackEntries(callbacks);
                        }
                    }
                    failures.push_back({fileName, errorMessage, {}, "load", "quarantined",
                                        flywithlua::panel::enabled() ? "panel" : "opengl"});
                    ++failedScripts;
                } else {
                    auto& record = gLuaScriptRecords.at(scriptId);
                    if (record.quarantined) {
                        ++failedScripts;
                    } else {
                        record.loaded = true;
                        ++loadedScripts;
                    }
                }
            }
            gLoadingLuaScriptId = flywithlua::kSystemLuaScriptId;
            gLoadingPanelScript = false;
        }

        failures.insert(failures.end(), gScriptLoadFailures.begin(), gScriptLoadFailures.end());
        gLoadedScriptCount = loadedScripts;
        gFailedScriptCount = failedScripts;
        gScriptLoadFailures = failures;
        const std::string failuresJson = BuildScriptLoadFailuresJson(failures);
        flywithlua_update_script_load_summary(gDiscoveredScriptCount, loadedScripts, failedScripts, failuresJson.c_str());
        flywithlua_update_last_log_message((std::string("Loaded scripts: ") + std::to_string(loadedScripts) + "/" +
                                            std::to_string(gDiscoveredScriptCount) + ", failed: " +
                                            std::to_string(failedScripts)).c_str());
        return true;
    }
}

float FlightLoopCallback(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void * inRefcon) {
    UpdateMouseEventWindowGeometry();
    if (!flywithlua::LuaIsRunning) return 0.0f;

    // 3jFPS12 is deliberately stepped here in native code. The Lua adapter
    // only updates configuration and persistence; it no longer owns the
    // per-frame controller or HUD drawing when the native bridge is present.
    threejfps_step(inElapsedSinceLastCall);

    // Update FMOD and Floating Windows
    fmodint::fmod_data_update();
    flwnd::onFlightLoop();
    PollPositiveEdgeFlips();

    RunLuaCallbackEntries(LuaCallbackKind::EveryFrame, gEveryFrameCallbacks, "do_every_frame");

    if (AdvanceLuaTimer(gOftenAccumulator, inElapsedSinceLastCall, kOftenIntervalSeconds)) {
        RunLuaCallbackEntries(LuaCallbackKind::Often, gOftenCallbacks, "do_often");
    }

    if (AdvanceLuaTimer(gSometimesAccumulator, inElapsedSinceLastCall, kSometimesIntervalSeconds)) {
        RunLuaCallbackEntries(LuaCallbackKind::Sometimes, gSometimesCallbacks, "do_sometimes");
    }

    if (gAltitudeDataRef && AdvanceLuaTimer(gAltitudeAccumulator, inElapsedSinceLastCall,
                                            kAltitudeIntervalSeconds)) {
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

    const bool runLegacyGroup = HasEnabledLuaCallbacks(LuaCallbackKind::Draw, gDrawCallbacks);
    const bool runPanelFallbackGroup = !flywithlua::panel::enabled() &&
                                       HasEnabledLuaCallbacks(LuaCallbackKind::CompatPanelDraw,
                                                              gPanelDrawCallbacks);
    if (!runLegacyGroup && !runPanelFallbackGroup) {
        return 1;
    }

    UpdateLuaMouseGlobals();

    // Establish the 2D state expected by legacy FlyWithLua drawing scripts.
    XPLMSetGraphicsState(0, 0, 0, 1, 1, 0, 0);
    flywithlua::WeAreNotInDrawingState = false;
    if (runLegacyGroup) {
        RunLuaCallbackEntries(LuaCallbackKind::Draw, gDrawCallbacks, "do_every_draw");
    }
    if (runPanelFallbackGroup) {
        RunLuaCallbackEntries(LuaCallbackKind::CompatPanelDraw, gPanelDrawCallbacks,
                              "do_every_draw[panel-fallback]");
    }
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
    gJoystickButtonDataRef = XPLMFindDataRef("sim/joystick/joystick_button_values");
    gPlaneICAODataRef = XPLMFindDataRef("sim/aircraft/view/acf_ICAO");
    gPlaneTailNumberDataRef = XPLMFindDataRef("sim/aircraft/view/acf_tailnum");

    if (!InitializeLuaRuntime(true)) {
        return 0;
    }

    CreateMouseEventWindow();
    RegisterFlyWithLuaMenu();

    XPLMDebugString("FlyWithLua-Mac: Successfully started and initialized Lua.\n");

    return 1;
}

PLUGIN_API void XPluginStop(void) {
    threejfps_shutdown();
    DestroyMouseEventWindow();
    if (L) {
        XPLMUnregisterFlightLoopCallback(FlightLoopCallback, nullptr);
        XPLMUnregisterDrawCallback(FlyWithLuaDrawCallback, xplm_Phase_Window, 0, (void*) "FlyWithLua-MacScriptDraw");
        RunLuaCallbackEntries(LuaCallbackKind::OnExit, gOnExitCallbacks, "do_on_exit");
        
        flwnd::deinitFloatingWindowSupport();
        flywithlua::panel::invalidateLuaResources();
        flywithlua::panel::shutdown();
        fmodint::fmod_uninitialize();
        UnregisterFlyWithLuaMenu();
        ClearFlyWithLuaCommands();
        flywithlua_update_script_load_summary(0, 0, 0, "[]");
        ShutdownHID();
        gPositiveEdgeFlips.clear();
        gJoystickButtonValues.clear();
        gPlaneICAODataRef = nullptr;
        gPlaneTailNumberDataRef = nullptr;
        
        InvalidateAllLuaCallbackChunks();
        lua_close(L);
        L = nullptr;
        lState = nullptr;
        flywithlua::FWLLua = nullptr;
        flywithlua::LuaIsRunning = false;
        gAltitudeDataRef = nullptr;
        gJoystickButtonDataRef = nullptr;
        gOftenAccumulator = 0.0f;
        gSometimesAccumulator = 0.0f;
        gAltitudeAccumulator = 0.0f;
        XPLMDebugString("FlyWithLua-Mac: Stopped.\n");
    }
}

PLUGIN_API void XPluginDisable(void) {
    flywithlua::LuaIsRunning = false;
    gMouseClickCaptured = false;
    flywithlua_update_script_load_summary(0, 0, 0, "[]");
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
