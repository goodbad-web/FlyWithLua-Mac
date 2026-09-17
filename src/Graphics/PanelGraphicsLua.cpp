#include "PanelGraphicsLua.h"

#include "PanelGraphicsBackend.h"
#include "../FlyWithLua.h"
#include "../FloatingWindows/FLWIntegration.h"
#include "../FloatingWindows/stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <unordered_set>
#include <vector>

namespace flywithlua::panel {
namespace {

constexpr const char* kTextureMetatable = "FlyWithLua.PanelTexture";
constexpr std::uint32_t kPanelBackendIdentity = 0x50414e4cu;

struct PanelTextureUserdata {
    void* texture = nullptr;
    LuaScriptId ownerScriptId = kSystemLuaScriptId;
    std::uint64_t generation = 0;
    std::uint32_t backend = kPanelBackendIdentity;
    std::uint64_t backendGeneration = 0;
    bool destroyed = false;
    bool stale = false;
};

std::unordered_set<PanelTextureUserdata*> gTextures;
std::uint64_t gTextureGeneration = 1;

bool finiteNumber(lua_State* state, int index) {
    return lua_isnumber(state, index) && std::isfinite(lua_tonumber(state, index));
}

void pushFalse(lua_State* state) {
    lua_pushboolean(state, 0);
}

void pushFailure(lua_State* state, const char* functionName, const std::string& message) {
    pushFalse(state);
    flywithlua::ReportLuaScriptError(flywithlua::CurrentLuaScriptId(), functionName, message);
}

bool requireDrawing(lua_State* state, const char* functionName) {
    if (!panelAvailable()) {
        pushFalse(state);
        return false;
    }
    if (panelDrawing() && flywithlua::IsLuaPanelApiAllowed()) {
        return true;
    }
    pushFailure(state, functionName, "Panel Graphics API called outside a panel draw callback");
    return false;
}

PanelTextureUserdata* checkTexture(lua_State* state, int index, const char* functionName) {
    if (!lua_isuserdata(state, index)) {
        return nullptr;
    }
    auto* texture = static_cast<PanelTextureUserdata*>(
        luaL_testudata(state, index, kTextureMetatable));
    if (texture == nullptr || texture->destroyed || texture->texture == nullptr ||
        texture->generation == 0 || texture->stale ||
        texture->backend != kPanelBackendIdentity ||
        texture->backendGeneration != sessionGeneration()) {
        return nullptr;
    }
    if (texture->ownerScriptId != kSystemLuaScriptId &&
        flywithlua::IsLuaScriptQuarantined(texture->ownerScriptId)) {
        return nullptr;
    }
    if (texture->ownerScriptId != kSystemLuaScriptId &&
        texture->ownerScriptId != flywithlua::CurrentLuaScriptId()) {
        return nullptr;
    }
    (void)functionName;
    return texture;
}

int destroyTextureUserdata(lua_State* state) {
    auto* texture = static_cast<PanelTextureUserdata*>(
        luaL_testudata(state, 1, kTextureMetatable));
    if (texture != nullptr && !texture->destroyed && texture->texture != nullptr) {
        flywithlua::panel::destroyTexture(texture->texture);
        texture->texture = nullptr;
        texture->destroyed = true;
        flwnd::invalidateImguiWindows();
    }
    if (texture != nullptr) {
        gTextures.erase(texture);
    }
    return 0;
}

int panelIsAvailable(lua_State* state) {
    lua_pushboolean(state, panelAvailable() ? 1 : 0);
    return 1;
}

int panelCapabilities(lua_State* state) {
    lua_newtable(state);
    lua_pushinteger(state, 1);
    lua_setfield(state, -2, "version");

    lua_pushboolean(state, panelAvailable() ? 1 : 0);
    lua_setfield(state, -2, "available");

    lua_pushboolean(state, hasCapability(CapabilityPrimitives) ? 1 : 0);
    lua_setfield(state, -2, "primitives");
    lua_pushboolean(state, hasCapability(CapabilityText) ? 1 : 0);
    lua_setfield(state, -2, "text");
    lua_pushboolean(state, hasCapability(CapabilityTexture) ? 1 : 0);
    lua_setfield(state, -2, "texture");
    lua_pushboolean(state, hasCapability(CapabilityMesh) ? 1 : 0);
    lua_setfield(state, -2, "mesh");
    return 1;
}

int panelSetColor(lua_State* state) {
    if (!requireDrawing(state, "panel_set_color")) return 1;
    if (!hasCapability(CapabilityPrimitives) || !finiteNumber(state, 1) ||
        !finiteNumber(state, 2) || !finiteNumber(state, 3) || !finiteNumber(state, 4)) {
        pushFailure(state, "panel_set_color", "expected four finite color components");
        return 1;
    }
    setColor(static_cast<float>(lua_tonumber(state, 1)),
             static_cast<float>(lua_tonumber(state, 2)),
             static_cast<float>(lua_tonumber(state, 3)),
             static_cast<float>(lua_tonumber(state, 4)));
    lua_pushboolean(state, 1);
    return 1;
}

int panelSetLineWidth(lua_State* state) {
    if (!requireDrawing(state, "panel_set_line_width")) return 1;
    if (!hasCapability(CapabilityPrimitives) || !finiteNumber(state, 1) ||
        lua_tonumber(state, 1) <= 0.0) {
        pushFailure(state, "panel_set_line_width", "expected a positive finite width");
        return 1;
    }
    setLineWidth(static_cast<float>(lua_tonumber(state, 1)));
    lua_pushboolean(state, 1);
    return 1;
}

bool primitiveMode(lua_State* state, int index, LegacyPrimitiveMode& mode) {
    if (!lua_isstring(state, index)) return false;
    const std::string value = lua_tostring(state, index);
    if (value == "points") mode = LegacyPrimitiveMode::Points;
    else if (value == "lines") mode = LegacyPrimitiveMode::Lines;
    else if (value == "line_strip") mode = LegacyPrimitiveMode::LineStrip;
    else if (value == "line_loop") mode = LegacyPrimitiveMode::LineLoop;
    else if (value == "polygon") mode = LegacyPrimitiveMode::Polygon;
    else if (value == "triangles") mode = LegacyPrimitiveMode::Triangles;
    else if (value == "triangle_strip") mode = LegacyPrimitiveMode::TriangleStrip;
    else if (value == "triangle_fan") mode = LegacyPrimitiveMode::TriangleFan;
    else if (value == "quads") mode = LegacyPrimitiveMode::Quads;
    else if (value == "quad_strip") mode = LegacyPrimitiveMode::QuadStrip;
    else return false;
    return true;
}

int panelBegin(lua_State* state) {
    if (!requireDrawing(state, "panel_begin")) return 1;
    LegacyPrimitiveMode mode;
    if (!hasCapability(CapabilityPrimitives) || !primitiveMode(state, 1, mode)) {
        pushFailure(state, "panel_begin", "unknown primitive mode");
        return 1;
    }
    const LuaScriptId ownerScriptId = flywithlua::CurrentLuaScriptId();
    lua_pushboolean(state, beginPrimitive(mode, PrimitiveSource::Panel, ownerScriptId) ? 1 : 0);
    if (!lua_toboolean(state, -1)) {
        flywithlua::ReportLuaScriptError(flywithlua::CurrentLuaScriptId(),
                                         "panel_begin", "cannot mix panel and legacy primitives");
    }
    return 1;
}

int panelVertex(lua_State* state) {
    if (!requireDrawing(state, "panel_vertex")) return 1;
    if (!hasCapability(CapabilityPrimitives) || !finiteNumber(state, 1) ||
        !finiteNumber(state, 2)) {
        pushFailure(state, "panel_vertex", "expected two finite coordinates");
        return 1;
    }
    const LuaScriptId ownerScriptId = flywithlua::CurrentLuaScriptId();
    lua_pushboolean(state, vertex(static_cast<float>(lua_tonumber(state, 1)),
                                  static_cast<float>(lua_tonumber(state, 2)),
                                  PrimitiveSource::Panel, ownerScriptId) ? 1 : 0);
    if (!lua_toboolean(state, -1)) {
        flywithlua::ReportLuaScriptError(flywithlua::CurrentLuaScriptId(),
                                         "panel_vertex", "cannot mix panel and legacy primitives");
    }
    return 1;
}

int panelEnd(lua_State* state) {
    if (!requireDrawing(state, "panel_end")) return 1;
    if (!hasCapability(CapabilityPrimitives)) {
        pushFailure(state, "panel_end", "primitive capability is unavailable");
        return 1;
    }
    lua_pushboolean(state, endPrimitive(PrimitiveSource::Panel,
                                        flywithlua::CurrentLuaScriptId()) ? 1 : 0);
    if (!lua_toboolean(state, -1)) {
        flywithlua::ReportLuaScriptError(flywithlua::CurrentLuaScriptId(),
                                         "panel_end", "panel primitive is not active");
    }
    return 1;
}

int panelDrawRect(lua_State* state) {
    if (!requireDrawing(state, "panel_draw_rect")) return 1;
    if (!hasCapability(CapabilityPrimitives)) {
        pushFailure(state, "panel_draw_rect", "primitive capability is unavailable");
        return 1;
    }
    for (int index = 1; index <= 4; ++index) {
        if (!finiteNumber(state, index)) {
            pushFailure(state, "panel_draw_rect", "expected four finite coordinates");
            return 1;
        }
    }
    drawFilledRect(static_cast<float>(lua_tonumber(state, 1)),
                   static_cast<float>(lua_tonumber(state, 2)),
                   static_cast<float>(lua_tonumber(state, 3)),
                   static_cast<float>(lua_tonumber(state, 4)));
    lua_pushboolean(state, 1);
    return 1;
}

int panelDrawText(lua_State* state) {
    if (!requireDrawing(state, "panel_draw_text")) return 1;
    if (!hasCapability(CapabilityText) || !finiteNumber(state, 1) ||
        !finiteNumber(state, 2) || !lua_isstring(state, 3) ||
        !lua_isstring(state, 4) || !finiteNumber(state, 5) ||
        lua_tonumber(state, 5) <= 0.0) {
        pushFailure(state, "panel_draw_text", "expected x, y, text, font, and positive size");
        return 1;
    }
    const bool drawn = drawTextCurrentColor(static_cast<float>(lua_tonumber(state, 1)),
                                            static_cast<float>(lua_tonumber(state, 2)),
                                            lua_tostring(state, 3),
                                            static_cast<float>(lua_tonumber(state, 5)),
                                            lua_tostring(state, 4), 400);
    lua_pushboolean(state, drawn ? 1 : 0);
    return 1;
}

int panelMeasureText(lua_State* state) {
    if (!requireDrawing(state, "panel_measure_text")) return 1;
    if (!hasCapability(CapabilityText) || !lua_isstring(state, 1) ||
        !lua_isstring(state, 2) || !finiteNumber(state, 3) || lua_tonumber(state, 3) <= 0.0) {
        pushFailure(state, "panel_measure_text", "expected text, font, and positive size");
        return 1;
    }
    const double width = measureText(lua_tostring(state, 1),
                                     static_cast<float>(lua_tonumber(state, 3)),
                                     lua_tostring(state, 2), 400);
    if (width < 0.0) {
        lua_pushnil(state);
    } else {
        lua_pushnumber(state, width);
    }
    return 1;
}

int panelLoadTexture(lua_State* state) {
    if (!requireDrawing(state, "panel_load_texture")) return 1;
    if (!hasCapability(CapabilityTexture) || !lua_isstring(state, 1)) {
        lua_pushnil(state);
        return 1;
    }

    int width = 0;
    int height = 0;
    int components = 0;
    const char* path = lua_tostring(state, 1);
    unsigned char* pixels = stbi_load(path, &width, &height, &components, 4);
    if (pixels == nullptr) {
        XPLMDebugString((std::string("FlyWithLua-Mac Panel Graphics: texture load failed: ") +
                         path + "\n").c_str());
        lua_pushnil(state);
        return 1;
    }

    void* nativeTexture = createTexture(pixels, width, height);
    stbi_image_free(pixels);
    if (nativeTexture == nullptr) {
        lua_pushnil(state);
        return 1;
    }

    auto* texture = static_cast<PanelTextureUserdata*>(lua_newuserdata(
        state, sizeof(PanelTextureUserdata)));
    new (texture) PanelTextureUserdata{nativeTexture, flywithlua::CurrentLuaScriptId(),
                                       gTextureGeneration++, kPanelBackendIdentity,
                                       sessionGeneration(), false, false};
    luaL_getmetatable(state, kTextureMetatable);
    lua_setmetatable(state, -2);
    gTextures.insert(texture);
    return 1;
}

int panelDestroyTexture(lua_State* state) {
    if (!requireDrawing(state, "panel_destroy_texture")) return 1;
    if (!lua_isuserdata(state, 1)) {
        pushFailure(state, "panel_destroy_texture", "expected PanelTexture userdata");
        return 1;
    }
    auto* texture = static_cast<PanelTextureUserdata*>(
        luaL_testudata(state, 1, kTextureMetatable));
    if (texture == nullptr) {
        pushFailure(state, "panel_destroy_texture", "invalid PanelTexture userdata");
        return 1;
    }
    if (texture->stale || texture->backend != kPanelBackendIdentity ||
        texture->backendGeneration != sessionGeneration()) {
        pushFalse(state);
        return 1;
    }
    if (texture->ownerScriptId != kSystemLuaScriptId &&
        texture->ownerScriptId != flywithlua::CurrentLuaScriptId()) {
        pushFailure(state, "panel_destroy_texture", "PanelTexture belongs to another script");
        return 1;
    }
    if (!texture->destroyed && texture->texture != nullptr) {
        destroyTexture(texture->texture);
        texture->texture = nullptr;
        texture->destroyed = true;
    }
    gTextures.erase(texture);
    flwnd::invalidateImguiWindows();
    lua_pushboolean(state, 1);
    return 1;
}

bool readTexture(lua_State* state, int index, PanelTextureUserdata*& texture,
                 const char* functionName) {
    texture = checkTexture(state, index, functionName);
    if (texture == nullptr) {
        pushFailure(state, functionName, "invalid, destroyed, or stale PanelTexture userdata");
        return false;
    }
    return true;
}

void writeVertex(std::vector<float>& vertices, float x, float y, float u, float v,
                 std::uint32_t color) {
    const size_t offset = vertices.size();
    vertices.resize(offset + 5u);
    vertices[offset + 0u] = x;
    vertices[offset + 1u] = y;
    vertices[offset + 2u] = u;
    vertices[offset + 3u] = v;
    std::memcpy(vertices.data() + offset + 4u, &color, sizeof(color));
}

std::uint32_t colorFromCurrentState() {
    return currentColor();
}

int panelDrawTexture(lua_State* state) {
    if (!requireDrawing(state, "panel_draw_texture")) return 1;
    if (!hasCapability(CapabilityMesh)) {
        pushFailure(state, "panel_draw_texture", "mesh capability is unavailable");
        return 1;
    }
    PanelTextureUserdata* texture = nullptr;
    if (!readTexture(state, 1, texture, "panel_draw_texture")) return 1;
    for (int index = 2; index <= 5; ++index) {
        if (!finiteNumber(state, index)) {
            pushFailure(state, "panel_draw_texture", "expected four finite coordinates");
            return 1;
        }
    }

    std::vector<float>& vertices = meshVertexScratch();
    vertices.clear();
    if (vertices.capacity() < 20u) {
        vertices.reserve(20u);
    }
    const std::uint32_t color = colorFromCurrentState();
    writeVertex(vertices, static_cast<float>(lua_tonumber(state, 2)),
                static_cast<float>(lua_tonumber(state, 3)), 0.0f, 1.0f, color);
    writeVertex(vertices, static_cast<float>(lua_tonumber(state, 4)),
                static_cast<float>(lua_tonumber(state, 3)), 1.0f, 1.0f, color);
    writeVertex(vertices, static_cast<float>(lua_tonumber(state, 4)),
                static_cast<float>(lua_tonumber(state, 5)), 1.0f, 0.0f, color);
    writeVertex(vertices, static_cast<float>(lua_tonumber(state, 2)),
                static_cast<float>(lua_tonumber(state, 5)), 0.0f, 0.0f, color);
    std::vector<std::uint16_t>& indices = meshIndexScratch();
    indices.assign({0, 1, 2, 0, 2, 3});
    const float scissors[] = {0.0f, 0.0f, 100000.0f, 100000.0f};
    XPLMMesh_t mesh{4, vertices.data(), 6, indices.data()};
    DrawCall call;
    call.texture = texture->texture;
    std::copy(std::begin(scissors), std::end(scissors), std::begin(call.scissors));
    call.elementCount = 6;
    lua_pushboolean(state, drawCalls(mesh, call, MeshCoordinateSpace::PublicYUp) ? 1 : 0);
    return 1;
}

bool readMeshVertex(lua_State* state, int index, std::vector<float>& vertices) {
    if (!lua_istable(state, index)) return false;
    lua_getfield(state, index, "x");
    lua_getfield(state, index, "y");
    lua_getfield(state, index, "u");
    lua_getfield(state, index, "v");
    lua_getfield(state, index, "color");
    const bool valid = finiteNumber(state, -5) && finiteNumber(state, -4) &&
                       finiteNumber(state, -3) && finiteNumber(state, -2);
    if (!valid) {
        lua_pop(state, 5);
        return false;
    }
    const std::uint32_t color = lua_isnumber(state, -1)
        ? static_cast<std::uint32_t>(lua_tointeger(state, -1))
        : colorFromCurrentState();
    writeVertex(vertices, static_cast<float>(lua_tonumber(state, -5)),
                static_cast<float>(lua_tonumber(state, -4)),
                static_cast<float>(lua_tonumber(state, -3)),
                static_cast<float>(lua_tonumber(state, -2)), color);
    lua_pop(state, 5);
    return true;
}

int panelDrawMesh(lua_State* state) {
    if (!requireDrawing(state, "panel_draw_mesh")) return 1;
    if (!hasCapability(CapabilityMesh) || !lua_istable(state, 1) ||
        !lua_istable(state, 2)) {
        pushFailure(state, "panel_draw_mesh", "expected vertex and index tables");
        return 1;
    }
    PanelTextureUserdata* texture = nullptr;
    if (!readTexture(state, 3, texture, "panel_draw_mesh")) return 1;

    const size_t vertexCount = lua_objlen(state, 1);
    const size_t indexCount = lua_objlen(state, 2);
    if (vertexCount == 0 || vertexCount > std::numeric_limits<int>::max() ||
        vertexCount > std::numeric_limits<size_t>::max() / 5u ||
        indexCount == 0 || indexCount % 3 != 0 || indexCount > std::numeric_limits<int>::max()) {
        pushFailure(state, "panel_draw_mesh", "invalid mesh dimensions");
        return 1;
    }

    std::vector<float>& vertices = meshVertexScratch();
    vertices.clear();
    if (vertices.capacity() < vertexCount * 5u) {
        vertices.reserve(vertexCount * 5u);
    }
    for (size_t i = 1; i <= vertexCount; ++i) {
        lua_rawgeti(state, 1, static_cast<int>(i));
        const bool valid = readMeshVertex(state, -1, vertices);
        lua_pop(state, 1);
        if (!valid) {
            pushFailure(state, "panel_draw_mesh", "invalid mesh vertex");
            return 1;
        }
    }

    std::vector<std::uint16_t>& indices = meshIndexScratch();
    indices.clear();
    if (indices.capacity() < indexCount) {
        indices.reserve(indexCount);
    }
    for (size_t i = 1; i <= indexCount; ++i) {
        lua_rawgeti(state, 2, static_cast<int>(i));
        const lua_Number rawIndex = lua_tonumber(state, -1);
        const bool valid = lua_isnumber(state, -1) && std::isfinite(rawIndex) &&
                           std::floor(rawIndex) == rawIndex && rawIndex >= 1.0 &&
                           rawIndex <= static_cast<lua_Number>(vertexCount) &&
                           rawIndex - 1.0 <=
                               static_cast<lua_Number>(std::numeric_limits<std::uint16_t>::max());
        if (valid) {
            indices.push_back(static_cast<std::uint16_t>(rawIndex - 1.0));
        }
        lua_pop(state, 1);
        if (!valid) {
            pushFailure(state, "panel_draw_mesh", "invalid mesh index");
            return 1;
        }
    }

    const float scissors[] = {0.0f, 0.0f, 100000.0f, 100000.0f};
    XPLMMesh_t mesh{static_cast<int>(vertexCount), vertices.data(),
                    static_cast<int>(indexCount), indices.data()};
    DrawCall call;
    call.texture = texture->texture;
    std::copy(std::begin(scissors), std::end(scissors), std::begin(call.scissors));
    call.elementCount = static_cast<int>(indexCount);
    lua_pushboolean(state, drawCalls(mesh, call, MeshCoordinateSpace::PublicYUp) ? 1 : 0);
    return 1;
}

} // namespace

void registerLuaFunctions(lua_State* state) {
    luaL_newmetatable(state, kTextureMetatable);
    lua_pushcfunction(state, destroyTextureUserdata);
    lua_setfield(state, -2, "__gc");
    lua_pop(state, 1);

    lua_register(state, "panel_is_available", panelIsAvailable);
    lua_register(state, "panel_capabilities", panelCapabilities);
    lua_register(state, "panel_set_color", panelSetColor);
    lua_register(state, "panel_set_line_width", panelSetLineWidth);
    lua_register(state, "panel_begin", panelBegin);
    lua_register(state, "panel_vertex", panelVertex);
    lua_register(state, "panel_end", panelEnd);
    lua_register(state, "panel_draw_rect", panelDrawRect);
    lua_register(state, "panel_draw_text", panelDrawText);
    lua_register(state, "panel_measure_text", panelMeasureText);
    lua_register(state, "panel_load_texture", panelLoadTexture);
    lua_register(state, "panel_destroy_texture", panelDestroyTexture);
    lua_register(state, "panel_draw_texture", panelDrawTexture);
    lua_register(state, "panel_draw_mesh", panelDrawMesh);

    lua_pushinteger(state, 1);
    lua_setglobal(state, "PANEL_GRAPHICS_API_VERSION");
}

void invalidateLuaResources(std::uint64_t ownerScriptId) {
    bool invalidated = false;
    for (PanelTextureUserdata* texture : gTextures) {
        if (texture == nullptr || texture->destroyed || texture->texture == nullptr) {
            continue;
        }
        if (ownerScriptId != 0 && texture->ownerScriptId != ownerScriptId) {
            continue;
        }
        destroyTexture(texture->texture);
        texture->texture = nullptr;
        texture->destroyed = true;
        texture->stale = true;
        invalidated = true;
    }
    if (invalidated) {
        flwnd::invalidateImguiWindows();
    }
}

} // namespace flywithlua::panel
