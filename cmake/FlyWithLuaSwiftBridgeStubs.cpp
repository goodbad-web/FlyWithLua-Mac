// Portable CMake implementation of the C ABI normally provided by the
// SwiftUI bridge. CMake builds do not compile the Swift sources, but the Lua
// runtime still needs the mac_native module used by the compatibility layer.

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "XPLMDataAccess.h"
#include "XPLMGraphics.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"
#include "../src/Graphics/PanelGraphicsBackend.h"
#include "lua.hpp"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

namespace {

struct LuaDataValue {
    enum class Kind {
        Number,
        Boolean,
        String,
    };

    Kind kind = Kind::Number;
    lua_Number number = 0.0;
    bool boolean = false;
    std::string bytes;

    lua_Number numberValue() const {
        if (kind == Kind::Boolean) {
            return boolean ? 1.0 : 0.0;
        }
        return kind == Kind::Number ? number : 0.0;
    }
};

static std::unordered_map<std::string, XPLMDataRef> dataRefCache;

static XPLMDataRef cachedDataRef(const std::string& path) {
    const auto cached = dataRefCache.find(path);
    if (cached != dataRefCache.end()) {
        return cached->second;
    }

    XPLMDataRef dataRef = XPLMFindDataRef(path.c_str());
    if (dataRef != nullptr) {
        dataRefCache.emplace(path, dataRef);
    }
    return dataRef;
}

static void logBridgeMessage(const std::string& message) {
    const std::string line = "FlyWithLua-Mac: " + message + "\n";
    XPLMDebugString(line.c_str());
}

static bool hasDataType(XPLMDataTypeID typeMask, XPLMDataTypeID type) {
    return (typeMask & type) != 0;
}

static bool readDataBytes(XPLMDataRef dataRef, int offset, std::string& value) {
    if (offset < 0) {
        return false;
    }

    const int totalBytes = XPLMGetDatab(dataRef, nullptr, 0, 0);
    if (totalBytes < 0 || offset > totalBytes) {
        return false;
    }

    const int bytesToRead = totalBytes - offset;
    if (bytesToRead == 0) {
        value.clear();
        return true;
    }

    std::vector<char> buffer(static_cast<size_t>(bytesToRead));
    const int copied = XPLMGetDatab(dataRef, buffer.data(), offset, bytesToRead);
    if (copied < 0) {
        return false;
    }

    const size_t safeCopied = std::min(static_cast<size_t>(copied), buffer.size());
    value.assign(buffer.data(), safeCopied);
    return true;
}

static bool readScalar(lua_State* state, XPLMDataRef dataRef, XPLMDataTypeID typeMask) {
    if (hasDataType(typeMask, xplmType_Double)) {
        lua_pushnumber(state, XPLMGetDatad(dataRef));
        return true;
    }
    if (hasDataType(typeMask, xplmType_Float)) {
        lua_pushnumber(state, XPLMGetDataf(dataRef));
        return true;
    }
    if (hasDataType(typeMask, xplmType_Int)) {
        lua_pushinteger(state, static_cast<lua_Integer>(XPLMGetDatai(dataRef)));
        return true;
    }
    return false;
}

static bool readArray(lua_State* state,
                      XPLMDataRef dataRef,
                      XPLMDataTypeID typeMask,
                      int index) {
    if (index < 0) {
        return false;
    }

    if (hasDataType(typeMask, xplmType_IntArray)) {
        int value = 0;
        if (XPLMGetDatavi(dataRef, &value, index, 1) > 0) {
            lua_pushinteger(state, static_cast<lua_Integer>(value));
            return true;
        }
    }
    if (hasDataType(typeMask, xplmType_FloatArray)) {
        float value = 0.0f;
        if (XPLMGetDatavf(dataRef, &value, index, 1) > 0) {
            lua_pushnumber(state, value);
            return true;
        }
    }
    return false;
}

static bool readDataRef(lua_State* state,
                        XPLMDataRef dataRef,
                        int index,
                        bool hasIndex) {
    const XPLMDataTypeID typeMask = XPLMGetDataRefTypes(dataRef);

    // Match the Swift bridge: byte-array DataRefs take precedence over all
    // other advertised types and use the optional index as an offset.
    if (hasDataType(typeMask, xplmType_Data)) {
        std::string value;
        if (!readDataBytes(dataRef, hasIndex ? index : 0, value)) {
            return false;
        }
        lua_pushlstring(state, value.data(), value.size());
        return true;
    }

    if (hasIndex) {
        if (readArray(state, dataRef, typeMask, index)) {
            return true;
        }
        return index == 0 && readScalar(state, dataRef, typeMask);
    }

    if (readScalar(state, dataRef, typeMask)) {
        return true;
    }
    return readArray(state, dataRef, typeMask, 0);
}

static bool writeScalar(XPLMDataRef dataRef,
                        XPLMDataTypeID typeMask,
                        const LuaDataValue& value) {
    if (hasDataType(typeMask, xplmType_Double)) {
        XPLMSetDatad(dataRef, value.numberValue());
        return true;
    }
    if (hasDataType(typeMask, xplmType_Float)) {
        XPLMSetDataf(dataRef, static_cast<float>(value.numberValue()));
        return true;
    }
    if (hasDataType(typeMask, xplmType_Int)) {
        XPLMSetDatai(dataRef, static_cast<int>(value.numberValue()));
        return true;
    }
    return false;
}

static bool writeArray(XPLMDataRef dataRef,
                       XPLMDataTypeID typeMask,
                       const LuaDataValue& value,
                       int index) {
    if (index < 0) {
        return false;
    }

    if (hasDataType(typeMask, xplmType_IntArray)) {
        int intValue = static_cast<int>(value.numberValue());
        XPLMSetDatavi(dataRef, &intValue, index, 1);
        return true;
    }
    if (hasDataType(typeMask, xplmType_FloatArray)) {
        float floatValue = static_cast<float>(value.numberValue());
        XPLMSetDatavf(dataRef, &floatValue, index, 1);
        return true;
    }
    return false;
}

static bool writeDataBytes(XPLMDataRef dataRef, const std::string& value, int offset) {
    if (offset < 0 || value.size() >= static_cast<size_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    std::vector<char> terminated(value.begin(), value.end());
    terminated.push_back('\0');
    XPLMSetDatab(dataRef,
                 terminated.data(),
                 offset,
                 static_cast<int>(terminated.size()));
    return true;
}

static bool writeDataRef(XPLMDataRef dataRef,
                         const LuaDataValue& value,
                         int index,
                         bool hasIndex) {
    if (XPLMCanWriteDataRef(dataRef) == 0) {
        return false;
    }

    const XPLMDataTypeID typeMask = XPLMGetDataRefTypes(dataRef);
    if (value.kind == LuaDataValue::Kind::String) {
        return writeDataBytes(dataRef, value.bytes, hasIndex ? index : 0);
    }

    if (hasIndex) {
        if (writeArray(dataRef, typeMask, value, index)) {
            return true;
        }
        return index == 0 && writeScalar(dataRef, typeMask, value);
    }

    if (writeScalar(dataRef, typeMask, value)) {
        return true;
    }
    return writeArray(dataRef, typeMask, value, 0);
}

static bool readOptionalIndex(lua_State* state, int argument, int& index) {
    if (lua_gettop(state) < argument || !lua_isnumber(state, argument)) {
        return false;
    }
    index = static_cast<int>(lua_tointeger(state, argument));
    return true;
}

static int luaGetDataRef(lua_State* state) {
    size_t nameLength = 0;
    const char* name = lua_tolstring(state, 1, &nameLength);
    if (name == nullptr) {
        return 0;
    }

    std::string path(name, nameLength);
    XPLMDataRef dataRef = cachedDataRef(path);
    if (dataRef == nullptr) {
        lua_pushnil(state);
        return 1;
    }

    int index = 0;
    const bool hasIndex = readOptionalIndex(state, 2, index);
    if (!readDataRef(state, dataRef, index, hasIndex)) {
        lua_pushnil(state);
    }
    return 1;
}

static int luaSetDataRef(lua_State* state) {
    size_t nameLength = 0;
    const char* name = lua_tolstring(state, 1, &nameLength);
    if (name == nullptr) {
        return 0;
    }

    LuaDataValue value;
    const int valueType = lua_type(state, 2);
    if (valueType == LUA_TSTRING) {
        size_t valueLength = 0;
        const char* bytes = lua_tolstring(state, 2, &valueLength);
        if (bytes == nullptr) {
            logBridgeMessage("Unsupported Lua string value for DataRef");
            return 0;
        }
        value.kind = LuaDataValue::Kind::String;
        value.bytes.assign(bytes, valueLength);
    } else if (valueType == LUA_TBOOLEAN) {
        value.kind = LuaDataValue::Kind::Boolean;
        value.boolean = lua_toboolean(state, 2) != 0;
    } else if (lua_isnumber(state, 2)) {
        value.kind = LuaDataValue::Kind::Number;
        value.number = lua_tonumber(state, 2);
    } else {
        logBridgeMessage("Unsupported Lua value for DataRef");
        return 0;
    }

    std::string path(name, nameLength);
    XPLMDataRef dataRef = cachedDataRef(path);
    if (dataRef == nullptr) {
        logBridgeMessage("DataRef not found: " + path);
        return 0;
    }

    int index = 0;
    const bool hasIndex = readOptionalIndex(state, 3, index);
    if (XPLMCanWriteDataRef(dataRef) == 0) {
        logBridgeMessage("DataRef is readonly: " + path);
        return 0;
    }
    if (!writeDataRef(dataRef, value, index, hasIndex)) {
        logBridgeMessage("Unsupported DataRef type for write: " + path);
    }
    return 0;
}

static int luaLogMessage(lua_State* state) {
    const char* message = lua_tolstring(state, 1, nullptr);
    if (message != nullptr) {
        logBridgeMessage(message);
    }
    return 0;
}

static int luaCommandOnce(lua_State* state) {
    const char* name = lua_tolstring(state, 1, nullptr);
    if (name == nullptr) {
        return 0;
    }

    XPLMCommandRef command = XPLMFindCommand(name);
    if (command == nullptr) {
        logBridgeMessage(std::string("Command not found: ") + name);
        return 0;
    }
    XPLMCommandOnce(command);
    return 0;
}

static std::string lowerString(const char* value) {
    if (value == nullptr) {
        return std::string();
    }

    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

static XPLMFontID fontIDForName(const char* name) {
    const std::string fontName = lowerString(name);
    if (fontName == "helvetica_18" || fontName == "times_roman_24" ||
        fontName == "times_roman_12" || fontName == "times_roman_18") {
        return static_cast<XPLMFontID>(xplmFont_Proportional);
    }
    return static_cast<XPLMFontID>(xplmFont_Basic);
}

static XPLMFontID fontIDForHiDPIFamily(const char* name) {
    const std::string family = lowerString(name);
    if (family == "sf_mono" || family == "sfmono" || family == "sf mono") {
        return static_cast<XPLMFontID>(xplmFont_Basic);
    }
    return static_cast<XPLMFontID>(xplmFont_Proportional);
}

static int luaDrawString(lua_State* state) {
    const char* text = lua_tolstring(state, 3, nullptr);
    if (text == nullptr) {
        return 0;
    }

    const char* fontName = nullptr;
    if (lua_gettop(state) >= 4) {
        fontName = lua_tolstring(state, 4, nullptr);
    }

    if (flywithlua::panel::panelDrawing()) {
        const int x = static_cast<int>(lua_tointeger(state, 1));
        const int y = static_cast<int>(lua_tointeger(state, 2));
        flywithlua_panel_draw_legacy_text(x, y, text, fontName, nullptr);
        return 0;
    }

    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    if (lua_gettop(state) >= 8 && lua_isnumber(state, 5) &&
        lua_isnumber(state, 6) && lua_isnumber(state, 7) &&
        lua_isnumber(state, 8)) {
        color[0] = static_cast<float>(lua_tonumber(state, 5));
        color[1] = static_cast<float>(lua_tonumber(state, 6));
        color[2] = static_cast<float>(lua_tonumber(state, 7));
        color[3] = static_cast<float>(lua_tonumber(state, 8));
    } else {
        glGetFloatv(GL_CURRENT_COLOR, color);
    }

    const int x = static_cast<int>(lua_tointeger(state, 1));
    const int y = static_cast<int>(lua_tointeger(state, 2));
    XPLMDrawString(color,
                   x,
                   y,
                   const_cast<char*>(text),
                   nullptr,
                   fontIDForName(fontName != nullptr ? fontName : "Helvetica_12"));
    return 0;
}

static int luaMeasureString(lua_State* state) {
    size_t textLength = 0;
    const char* text = lua_tolstring(state, 1, &textLength);
    if (text == nullptr) {
        lua_pushnumber(state, 0.0);
        return 1;
    }

    const char* fontName = nullptr;
    if (lua_gettop(state) >= 2) {
        fontName = lua_tolstring(state, 2, nullptr);
    }
    if (textLength > static_cast<size_t>(std::numeric_limits<int>::max())) {
        lua_pushnumber(state, 0.0);
        return 1;
    }
    const double panelWidth = flywithlua_panel_measure_legacy_text(
        text, fontName != nullptr ? fontName : "Helvetica_12");
    if (panelWidth >= 0.0) {
        lua_pushnumber(state, panelWidth);
        return 1;
    }
    const float width = XPLMMeasureString(
        fontIDForName(fontName != nullptr ? fontName : "Helvetica_12"),
        text,
        static_cast<int>(textLength));
    lua_pushnumber(state, width);
    return 1;
}

// The portable CMake runtime has no CoreText renderer.  Keep the new HUD API
// available there and deliberately fall back to the existing XPLM fonts.
static int luaDrawHiDPIString(lua_State* state) {
    const char* text = lua_tolstring(state, 3, nullptr);
    if (text == nullptr || !lua_isnumber(state, 1) || !lua_isnumber(state, 2)) {
        lua_pushboolean(state, 0);
        return 1;
    }

    const char* family = nullptr;
    if (lua_gettop(state) >= 5) {
        family = lua_tolstring(state, 5, nullptr);
    }

    if (flywithlua::panel::panelDrawing()) {
        const int x = static_cast<int>(lua_tonumber(state, 1));
        const int y = static_cast<int>(lua_tonumber(state, 2));
        const float size = static_cast<float>(lua_tonumber(state, 4));
        const int rendered = flywithlua_panel_draw_hidpi_text(x, y, text, size,
                                                               family != nullptr ? family : "sf_pro_text",
                                                               lua_gettop(state) >= 6
                                                                   ? static_cast<int>(lua_tointeger(state, 6))
                                                                   : 400);
        lua_pushboolean(state, rendered != 0);
        return 1;
    }

    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glGetFloatv(GL_CURRENT_COLOR, color);
    XPLMDrawString(color,
                   static_cast<int>(lua_tonumber(state, 1)),
                   static_cast<int>(lua_tonumber(state, 2)),
                   const_cast<char*>(text),
                   nullptr,
                   fontIDForHiDPIFamily(family));
    lua_pushboolean(state, 1);
    return 1;
}

static int luaMeasureHiDPIString(lua_State* state) {
    size_t textLength = 0;
    const char* text = lua_tolstring(state, 1, &textLength);
    if (text == nullptr || !lua_isnumber(state, 2) ||
        textLength > static_cast<size_t>(std::numeric_limits<int>::max())) {
        lua_pushnil(state);
        return 1;
    }

    const char* family = nullptr;
    if (lua_gettop(state) >= 3) {
        family = lua_tolstring(state, 3, nullptr);
    }
    const double panelWidth = flywithlua_panel_measure_hidpi_text(
        text, static_cast<float>(lua_tonumber(state, 2)),
        family != nullptr ? family : "sf_pro_text",
        lua_gettop(state) >= 4 ? static_cast<int>(lua_tointeger(state, 4)) : 400);
    if (panelWidth >= 0.0) {
        lua_pushnumber(state, panelWidth);
        return 1;
    }
    const float width = XPLMMeasureString(
        fontIDForHiDPIFamily(family),
        text,
        static_cast<int>(textLength));
    lua_pushnumber(state, width);
    return 1;
}

static int luaElapsedTime(lua_State* state) {
    lua_pushnumber(state, static_cast<lua_Number>(XPLMGetElapsedTime()));
    return 1;
}

static void registerBridgeFunction(lua_State* state,
                                   const char* name,
                                   lua_CFunction function) {
    lua_pushcclosure(state, function, 0);
    lua_setfield(state, -2, name);
}

} // namespace

// The portable CMake runtime has no CoreText renderer. Native overlays still
// link against this ABI and deliberately fall back to the existing XPLM font
// IDs when Swift is not part of the build.
extern "C" int flywithlua_draw_hidpi_text(int x,
                                          int y,
                                          const char* text,
                                          float logicalSize,
                                          const char* family,
                                          int weight) {
    (void)logicalSize;
    (void)weight;
    if (text == nullptr) {
        return 0;
    }

    if (flywithlua_panel_draw_hidpi_text(x, y, text, logicalSize, family, weight) != 0) {
        return 1;
    }

    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glGetFloatv(GL_CURRENT_COLOR, color);
    XPLMDrawString(color,
                   x,
                   y,
                   const_cast<char*>(text),
                   nullptr,
                   fontIDForHiDPIFamily(family));
    return 1;
}

extern "C" double flywithlua_measure_hidpi_text(const char* text,
                                                float logicalSize,
                                                const char* family,
                                                int weight) {
    (void)logicalSize;
    (void)weight;
    if (text == nullptr) {
        return -1.0;
    }

    const double panelWidth = flywithlua_panel_measure_hidpi_text(text, logicalSize, family, weight);
    if (panelWidth >= 0.0) {
        return panelWidth;
    }

    const std::string value(text);
    const size_t maximumLength = static_cast<size_t>(std::numeric_limits<int>::max());
    const int length = static_cast<int>(std::min(value.size(), maximumLength));
    return static_cast<double>(XPLMMeasureString(
        fontIDForHiDPIFamily(family),
        value.c_str(),
        length));
}

// These callbacks update the SwiftUI state in the XcodeGen build. CMake has
// no SwiftUI surface, so keeping them as no-ops preserves the ABI without
// making the native runtime depend on a UI implementation.
extern "C" void flywithlua_toggle_window(void) {}
extern "C" void flywithlua_update_current_altitude(double) {}
extern "C" void flywithlua_update_script_count(int) {}
extern "C" void flywithlua_clear_script_load_failures(void) {}
extern "C" void flywithlua_update_script_load_results(const char*) {}
extern "C" void flywithlua_update_script_load_summary(int, int, int, const char*) {}
extern "C" void flywithlua_update_last_log_message(const char*) {}
extern "C" void flywithlua_update_3jfps_snapshot(const char*) {}
extern "C" void flywithlua_show_3jfps_settings(void) {}

extern "C" void register_swift_bridge(lua_State* state) {
    if (state == nullptr) {
        return;
    }

    lua_createtable(state, 0, 9);
    registerBridgeFunction(state, "get_dataref", luaGetDataRef);
    registerBridgeFunction(state, "set_dataref", luaSetDataRef);
    registerBridgeFunction(state, "log_msg", luaLogMessage);
    registerBridgeFunction(state, "command_once", luaCommandOnce);
    registerBridgeFunction(state, "draw_string", luaDrawString);
    registerBridgeFunction(state, "measure_string", luaMeasureString);
    registerBridgeFunction(state, "draw_hidpi_string", luaDrawHiDPIString);
    registerBridgeFunction(state, "measure_hidpi_string", luaMeasureHiDPIString);
    registerBridgeFunction(state, "elapsed_time", luaElapsedTime);
    lua_setfield(state, LUA_GLOBALSINDEX, "mac_native");

    XPLMDebugString("FlyWithLua-Mac: portable CMake 'mac_native' module registered in Lua.\n");
}
