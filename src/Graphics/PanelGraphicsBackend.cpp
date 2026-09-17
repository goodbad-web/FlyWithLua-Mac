#include "PanelGraphicsBackend.h"
#include "PanelGraphicsLua.h"

#include "XPLMUtilities.h"
#include "../FlyWithLua.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unistd.h>

namespace flywithlua::panel {
namespace {

struct PanelAPI {
    decltype(&XPLMMakeColor) makeColor = nullptr;
    decltype(&XPLMPolygon) polygon = nullptr;
    decltype(&XPLMLines) lines = nullptr;
    decltype(&XPLMLinesWithWidth) linesWithWidth = nullptr;
    decltype(&XPLMLineStrip) lineStrip = nullptr;
    decltype(&XPLMLineStripWithWidth) lineStripWithWidth = nullptr;
    decltype(&XPLMLineLoop) lineLoop = nullptr;
    decltype(&XPLMLineLoopWithWidth) lineLoopWithWidth = nullptr;
    decltype(&XPLMQuadstrip) quadStrip = nullptr;
    decltype(&XPLMCreateFont) createFont = nullptr;
    decltype(&XPLMDestroyFont) destroyFont = nullptr;
    decltype(&XPLMFontAddFace) addFace = nullptr;
    decltype(&XPLMFontMeasureString) measureString = nullptr;
    decltype(&XPLMFontDrawString) drawString = nullptr;
    decltype(&XPLMCreateTexture) createTexture = nullptr;
    decltype(&XPLMDestroyTexture) destroyTexture = nullptr;
    decltype(&XPLMDrawCalls) drawCalls = nullptr;
};

struct WindowState {
    std::uint64_t generation = 0;
    std::uint64_t ownerScriptId = flywithlua::kSystemLuaScriptId;
    int width = 0;
    int height = 0;
    void* currentTexture = nullptr;
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float lineWidth = 1.0f;
    LegacyPrimitiveMode primitiveMode = LegacyPrimitiveMode::Lines;
    bool primitiveActive = false;
    PrimitiveSource primitiveSource = PrimitiveSource::Legacy;
    std::uint64_t primitiveOwnerScriptId = flywithlua::kSystemLuaScriptId;
    std::vector<XPLMVertex_t> vertices;
    std::vector<XPLMDrawCall_t> sdkDrawCalls;
};

struct State {
    BackendPreference preference = BackendPreference::Auto;
    bool initialized = false;
    bool panelEnabled = false;
    std::uint32_t capabilityMask = CapabilityNone;
    std::uint64_t sessionGeneration = 1;
    bool drawing = false;
    bool failureLogged = false;
    XPLMWindowID activeWindow = nullptr;
    std::uint64_t activeWindowGeneration = 0;
    std::uint64_t activeWindowOwnerScriptId = flywithlua::kSystemLuaScriptId;
    void* currentTexture = nullptr;
    std::uint64_t nextWindowGeneration = 1;
    std::unordered_map<XPLMWindowID, WindowState> windowStates;
    PanelAPI api;
    std::array<XPLMFontHandle, 4> fonts = {nullptr, nullptr, nullptr, nullptr};
    std::array<bool, 4> fontAttempted = {false, false, false, false};
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float lineWidth = 1.0f;
    LegacyPrimitiveMode primitiveMode = LegacyPrimitiveMode::Lines;
    bool primitiveActive = false;
    PrimitiveSource primitiveSource = PrimitiveSource::Legacy;
    std::uint64_t primitiveOwnerScriptId = flywithlua::kSystemLuaScriptId;
    std::vector<XPLMVertex_t> vertices;
    std::vector<float> meshVertexScratch;
    std::vector<float> meshNativeVertexScratch;
    std::vector<std::uint16_t> meshIndexScratch;
};

State gState;

testing::Recorder* gTestRecorder = nullptr;
int gTestWindowWidth = 0;
int gTestWindowHeight = 0;

void recordPrimitiveVertices(const XPLMVertex_t vertices[], int count) {
    if (gTestRecorder == nullptr || vertices == nullptr || count <= 0) {
        return;
    }
    gTestRecorder->lastVertices.assign(vertices, vertices + count);
}

void testPolygon(std::uint32_t, const XPLMVertex_t vertices[], int count) {
    if (gTestRecorder != nullptr) {
        ++gTestRecorder->polygonCalls;
    }
    recordPrimitiveVertices(vertices, count);
}

void testLinesWithWidth(std::uint32_t, float lineWidth,
                        const XPLMVertex_t vertices[], int count) {
    if (gTestRecorder != nullptr) {
        ++gTestRecorder->lineCalls;
        gTestRecorder->lastLineWidth = lineWidth;
    }
    recordPrimitiveVertices(vertices, count);
}

void testLines(std::uint32_t, const XPLMVertex_t vertices[], int count) {
    if (gTestRecorder != nullptr) {
        ++gTestRecorder->lineCalls;
    }
    recordPrimitiveVertices(vertices, count);
}

void testQuadStrip(std::uint32_t, const XPLMVertex_t vertices[], int count) {
    if (gTestRecorder != nullptr) {
        ++gTestRecorder->polygonCalls;
    }
    recordPrimitiveVertices(vertices, count);
}

void testDrawCalls(const XPLMMesh_t* mesh, int count,
                   const XPLMDrawCall_t calls[]) {
    if (gTestRecorder != nullptr) {
        ++gTestRecorder->drawCallBatches;
        if (mesh != nullptr && mesh->vertices != nullptr && mesh->vertex_count > 0) {
            gTestRecorder->lastMeshVertices.assign(
                mesh->vertices, mesh->vertices + static_cast<size_t>(mesh->vertex_count) * 5u);
        }
        if (calls != nullptr && count > 0) {
            std::copy(std::begin(calls[0].scissors), std::end(calls[0].scissors),
                      std::begin(gTestRecorder->lastScissors));
        }
    }
}

void saveActiveWindowState() {
    if (gState.activeWindow == nullptr) {
        return;
    }

    const auto found = gState.windowStates.find(gState.activeWindow);
    if (found == gState.windowStates.end() ||
        found->second.generation != gState.activeWindowGeneration) {
        return;
    }

    WindowState& windowState = found->second;
    windowState.currentTexture = gState.currentTexture;
    std::copy(std::begin(gState.color), std::end(gState.color), std::begin(windowState.color));
    windowState.lineWidth = gState.lineWidth;
    windowState.primitiveMode = gState.primitiveMode;
    windowState.primitiveActive = gState.primitiveActive;
    windowState.primitiveSource = gState.primitiveSource;
    windowState.primitiveOwnerScriptId = gState.primitiveOwnerScriptId;
    windowState.vertices = gState.vertices;
}

void loadWindowState(XPLMWindowID window) {
    const auto found = gState.windowStates.find(window);
    if (found == gState.windowStates.end()) {
        gState.color[0] = 1.0f;
        gState.color[1] = 1.0f;
        gState.color[2] = 1.0f;
        gState.color[3] = 1.0f;
        gState.lineWidth = 1.0f;
        gState.currentTexture = nullptr;
        gState.primitiveMode = LegacyPrimitiveMode::Lines;
        gState.primitiveActive = false;
        gState.primitiveSource = PrimitiveSource::Legacy;
        gState.primitiveOwnerScriptId = flywithlua::kSystemLuaScriptId;
        gState.vertices.clear();
        return;
    }

    const WindowState& windowState = found->second;
    gState.currentTexture = windowState.currentTexture;
    std::copy(std::begin(windowState.color), std::end(windowState.color), std::begin(gState.color));
    gState.lineWidth = windowState.lineWidth;
    gState.primitiveMode = windowState.primitiveMode;
    gState.primitiveActive = windowState.primitiveActive;
    gState.primitiveSource = windowState.primitiveSource;
    gState.primitiveOwnerScriptId = windowState.primitiveOwnerScriptId;
    gState.vertices = windowState.vertices;
}

template <typename T>
T findSymbol(const char* name) {
    return reinterpret_cast<T>(XPLMFindSymbol(name));
}

void logMessage(const std::string& message) {
    XPLMDebugString(("FlyWithLua-Mac Panel Graphics: " + message + "\n").c_str());
}

const char* preferenceName(BackendPreference preference) {
    switch (preference) {
        case BackendPreference::Panel: return "panel";
        case BackendPreference::OpenGL: return "opengl";
        case BackendPreference::Auto: return "auto";
    }
    return "auto";
}

uint32_t packedColor() {
    if (gState.api.makeColor != nullptr) {
        return gState.api.makeColor(gState.color[0], gState.color[1],
                                    gState.color[2], gState.color[3]);
    }

    const auto component = [](float value) -> uint32_t {
        return static_cast<uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    return component(gState.color[3]) << 24 |
           component(gState.color[2]) << 16 |
           component(gState.color[1]) << 8 |
           component(gState.color[0]);
}

void drawPolygon(const XPLMVertex_t* vertices, int count) {
    if (gState.api.polygon != nullptr && count >= 3) {
        gState.api.polygon(packedColor(), vertices, count);
    }
}

void drawTriangle(const XPLMVertex_t& a,
                  const XPLMVertex_t& b,
                  const XPLMVertex_t& c) {
    const XPLMVertex_t triangle[] = {a, b, c};
    drawPolygon(triangle, 3);
}

int fontIndex(const char* family, int weight) {
    const std::string name = family != nullptr ? family : "sf_pro_text";
    const bool mono = name == "sf_mono" || name == "sfmono" || name == "sf mono";
    const bool bold = weight >= 600;
    return (mono ? 2 : 0) + (bold ? 1 : 0);
}

XPLMFontHandle ensureFont(const char* family, int weight) {
    const int index = fontIndex(family, weight);
    if (gState.fontAttempted[static_cast<size_t>(index)]) {
        return gState.fonts[static_cast<size_t>(index)];
    }
    gState.fontAttempted[static_cast<size_t>(index)] = true;

    if (gState.api.createFont == nullptr || gState.api.addFace == nullptr) {
        return nullptr;
    }

    XPLMFontHandle font = gState.api.createFont(xplm_CharSetUnicode);
    if (font == nullptr) {
        return nullptr;
    }

    const bool mono = index >= 2;
    const bool bold = (index % 2) != 0;
    const std::initializer_list<const char*> candidates = mono
        ? (bold
            ? std::initializer_list<const char*>{
                  "/System/Library/Fonts/SFNSMono.ttf",
                  "/System/Library/Fonts/Menlo.ttc"}
            : std::initializer_list<const char*>{
                  "/System/Library/Fonts/SFNSMono.ttf",
                  "/System/Library/Fonts/Menlo.ttc"})
        : (bold
            ? std::initializer_list<const char*>{
                  "/Library/Fonts/SF-Pro-Text-Bold.otf",
                  "/System/Library/Fonts/SFNS.ttf",
                  "/System/Library/Fonts/Helvetica.ttc"}
            : std::initializer_list<const char*>{
                  "/Library/Fonts/SF-Pro-Text-Regular.otf",
                  "/System/Library/Fonts/SFNS.ttf",
                  "/System/Library/Fonts/Helvetica.ttc"});

    bool added = false;
    for (const char* path : candidates) {
        if (access(path, R_OK) == 0 && gState.api.addFace(font, path) != 0) {
            added = true;
            break;
        }
    }
    if (!added) {
        gState.api.destroyFont(font);
        return nullptr;
    }

    gState.fonts[static_cast<size_t>(index)] = font;
    return font;
}

void releaseFonts() {
    if (gState.api.destroyFont == nullptr) {
        return;
    }
    for (XPLMFontHandle& font : gState.fonts) {
        if (font != nullptr) {
            gState.api.destroyFont(font);
            font = nullptr;
        }
    }
    gState.fontAttempted.fill(false);
}

void drawCapturedPrimitive() {
    const auto& vertices = gState.vertices;
    if (vertices.empty()) {
        return;
    }

    switch (gState.primitiveMode) {
        case LegacyPrimitiveMode::Points:
            for (const XPLMVertex_t& point : vertices) {
                drawFilledRect(point.x, point.y, point.x + gState.lineWidth,
                               point.y + gState.lineWidth);
            }
            return;
        case LegacyPrimitiveMode::Lines:
            if (gState.api.linesWithWidth != nullptr && vertices.size() >= 2) {
                const int count = static_cast<int>(vertices.size() - vertices.size() % 2);
                gState.api.linesWithWidth(packedColor(), gState.lineWidth, vertices.data(), count);
            }
            return;
        case LegacyPrimitiveMode::LineStrip:
            if (vertices.size() >= 2) {
                if (gState.lineWidth != 1.0f && gState.api.lineStripWithWidth != nullptr) {
                    gState.api.lineStripWithWidth(packedColor(), gState.lineWidth,
                                                  vertices.data(), static_cast<int>(vertices.size()));
                } else if (gState.api.lineStrip != nullptr) {
                    gState.api.lineStrip(packedColor(), vertices.data(), static_cast<int>(vertices.size()));
                }
            }
            return;
        case LegacyPrimitiveMode::LineLoop:
            if (vertices.size() >= 2) {
                if (gState.lineWidth != 1.0f && gState.api.lineLoopWithWidth != nullptr) {
                    gState.api.lineLoopWithWidth(packedColor(), gState.lineWidth,
                                                 vertices.data(), static_cast<int>(vertices.size()));
                } else if (gState.api.lineLoop != nullptr) {
                    gState.api.lineLoop(packedColor(), vertices.data(), static_cast<int>(vertices.size()));
                }
            }
            return;
        case LegacyPrimitiveMode::Polygon:
            drawPolygon(vertices.data(), static_cast<int>(vertices.size()));
            return;
        case LegacyPrimitiveMode::Triangles:
            for (size_t i = 0; i + 2 < vertices.size(); i += 3) {
                drawTriangle(vertices[i], vertices[i + 1], vertices[i + 2]);
            }
            return;
        case LegacyPrimitiveMode::TriangleStrip:
            for (size_t i = 0; i + 2 < vertices.size(); ++i) {
                drawTriangle(vertices[i], vertices[i + 1], vertices[i + 2]);
            }
            return;
        case LegacyPrimitiveMode::TriangleFan:
            for (size_t i = 1; i + 1 < vertices.size(); ++i) {
                drawTriangle(vertices[0], vertices[i], vertices[i + 1]);
            }
            return;
        case LegacyPrimitiveMode::Quads:
            for (size_t i = 0; i + 3 < vertices.size(); i += 4) {
                drawPolygon(&vertices[i], 4);
            }
            return;
        case LegacyPrimitiveMode::QuadStrip:
            if (gState.api.quadStrip != nullptr && vertices.size() >= 4) {
                const size_t evenCount = vertices.size() - vertices.size() % 2;
                gState.api.quadStrip(packedColor(), vertices.data(), static_cast<int>(evenCount));
            }
            return;
    }
}

} // namespace

struct PanelDrawScope::StateSnapshot {
    std::uint64_t sessionGeneration = 0;
    bool drawing = false;
    XPLMWindowID activeWindow = nullptr;
    std::uint64_t activeWindowGeneration = 0;
    std::uint64_t activeWindowOwnerScriptId = flywithlua::kSystemLuaScriptId;
    void* currentTexture = nullptr;
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float lineWidth = 1.0f;
    LegacyPrimitiveMode primitiveMode = LegacyPrimitiveMode::Lines;
    bool primitiveActive = false;
    PrimitiveSource primitiveSource = PrimitiveSource::Legacy;
    std::uint64_t primitiveOwnerScriptId = flywithlua::kSystemLuaScriptId;
    std::vector<XPLMVertex_t> vertices;
    std::vector<float> meshVertexScratch;
    std::vector<float> meshNativeVertexScratch;
    std::vector<std::uint16_t> meshIndexScratch;
};

void configureBackend(const std::string& value) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (lower == "panel") {
        gState.preference = BackendPreference::Panel;
    } else if (lower == "opengl" || lower == "open_gl" || lower == "gl") {
        gState.preference = BackendPreference::OpenGL;
    } else {
        gState.preference = BackendPreference::Auto;
    }
}

void initialize() {
    if (gState.initialized) {
        return;
    }
    gState.initialized = true;

    if (gState.preference == BackendPreference::OpenGL) {
        logMessage("requested=opengl effective=opengl capabilities=0");
        return;
    }

    gState.api.makeColor = findSymbol<decltype(gState.api.makeColor)>("XPLMMakeColor");
    gState.api.polygon = findSymbol<decltype(gState.api.polygon)>("XPLMPolygon");
    gState.api.lines = findSymbol<decltype(gState.api.lines)>("XPLMLines");
    gState.api.linesWithWidth = findSymbol<decltype(gState.api.linesWithWidth)>("XPLMLinesWithWidth");
    gState.api.lineStrip = findSymbol<decltype(gState.api.lineStrip)>("XPLMLineStrip");
    gState.api.lineStripWithWidth = findSymbol<decltype(gState.api.lineStripWithWidth)>("XPLMLineStripWithWidth");
    gState.api.lineLoop = findSymbol<decltype(gState.api.lineLoop)>("XPLMLineLoop");
    gState.api.lineLoopWithWidth = findSymbol<decltype(gState.api.lineLoopWithWidth)>("XPLMLineLoopWithWidth");
    gState.api.quadStrip = findSymbol<decltype(gState.api.quadStrip)>("XPLMQuadstrip");
    gState.api.createFont = findSymbol<decltype(gState.api.createFont)>("XPLMCreateFont");
    gState.api.destroyFont = findSymbol<decltype(gState.api.destroyFont)>("XPLMDestroyFont");
    gState.api.addFace = findSymbol<decltype(gState.api.addFace)>("XPLMFontAddFace");
    gState.api.measureString = findSymbol<decltype(gState.api.measureString)>("XPLMFontMeasureString");
    gState.api.drawString = findSymbol<decltype(gState.api.drawString)>("XPLMFontDrawString");
    gState.api.createTexture = findSymbol<decltype(gState.api.createTexture)>("XPLMCreateTexture");
    gState.api.destroyTexture = findSymbol<decltype(gState.api.destroyTexture)>("XPLMDestroyTexture");
    gState.api.drawCalls = findSymbol<decltype(gState.api.drawCalls)>("XPLMDrawCalls");

    const bool primitivesAvailable = gState.api.makeColor != nullptr &&
        gState.api.polygon != nullptr && gState.api.lines != nullptr &&
        gState.api.linesWithWidth != nullptr && gState.api.lineStrip != nullptr &&
        gState.api.lineStripWithWidth != nullptr && gState.api.lineLoop != nullptr &&
        gState.api.lineLoopWithWidth != nullptr && gState.api.quadStrip != nullptr;
    const bool textAvailable = gState.api.createFont != nullptr &&
        gState.api.destroyFont != nullptr && gState.api.addFace != nullptr &&
        gState.api.measureString != nullptr && gState.api.drawString != nullptr;
    const bool textureAvailable = gState.api.createTexture != nullptr &&
        gState.api.destroyTexture != nullptr && gState.api.drawCalls != nullptr;
    gState.capabilityMask = CapabilityNone;
    if (primitivesAvailable) {
        gState.capabilityMask |= CapabilityPrimitives;
    }
    if (textAvailable) {
        gState.capabilityMask |= CapabilityText;
    }
    if (gState.api.createTexture != nullptr && gState.api.destroyTexture != nullptr) {
        gState.capabilityMask |= CapabilityTexture;
    }
    if (textureAvailable) {
        gState.capabilityMask |= CapabilityMesh;
    }
    gState.panelEnabled = gState.capabilityMask == kRequiredCapabilityMask;
    logMessage(std::string("requested=") + preferenceName(gState.preference) +
               " effective=" + (gState.panelEnabled ? "panel" : "opengl") +
               " capabilities=" + std::to_string(gState.capabilityMask) +
               (gState.panelEnabled ? "" : " reason=required symbol unavailable"));
}

void shutdown() {
    panel::invalidateLuaResources();
    releaseFonts();
    const std::uint64_t nextSessionGeneration = gState.sessionGeneration + 1;
    gState = State{};
    gState.sessionGeneration = nextSessionGeneration;
}

BackendPreference requestedBackend() {
    return gState.preference;
}

bool panelAvailable() {
    return gState.panelEnabled;
}

bool panelReady() {
    return gState.panelEnabled && hasAllCapabilities(kRequiredCapabilityMask);
}

bool enabled() {
    return gState.panelEnabled;
}

bool panelDrawing() {
    return gState.drawing;
}

std::uint32_t capabilities() {
    return gState.capabilityMask;
}

bool hasCapability(Capability capability) {
    return (gState.capabilityMask & static_cast<std::uint32_t>(capability)) != 0;
}

bool hasAllCapabilities(std::uint32_t capabilityMask) {
    return (gState.capabilityMask & capabilityMask) == capabilityMask;
}

std::uint64_t sessionGeneration() {
    return gState.sessionGeneration;
}

void registerWindow(XPLMWindowID window, std::uint64_t ownerScriptId) {
    if (window != nullptr) {
        auto result = gState.windowStates.try_emplace(window);
        if (result.second) {
            result.first->second.generation = gState.nextWindowGeneration++;
            result.first->second.ownerScriptId = ownerScriptId;
        }
    }
}

void unregisterWindow(XPLMWindowID window) {
    if (window == nullptr) {
        return;
    }
    if (gState.activeWindow == window) {
        gState.vertices.clear();
        gState.drawing = false;
        gState.activeWindow = nullptr;
        gState.activeWindowGeneration = 0;
        gState.activeWindowOwnerScriptId = flywithlua::kSystemLuaScriptId;
        gState.currentTexture = nullptr;
    }
    gState.windowStates.erase(window);
}

void disableForSession(const char* reason) {
    if (gState.panelEnabled && !gState.failureLogged) {
        gState.failureLogged = true;
        logMessage(std::string("disabled for session; affected windows will use OpenGL: ") +
                   (reason != nullptr ? reason : "unknown failure"));
    }
    panel::invalidateLuaResources();
    gState.panelEnabled = false;
    gState.drawing = false;
    gState.activeWindow = nullptr;
    gState.activeWindowGeneration = 0;
    gState.activeWindowOwnerScriptId = flywithlua::kSystemLuaScriptId;
    gState.currentTexture = nullptr;
    gState.primitiveActive = false;
    gState.primitiveOwnerScriptId = flywithlua::kSystemLuaScriptId;
    gState.windowStates.clear();
    ++gState.sessionGeneration;
}

void configurePanelWindow(XPLMCreateWindow_t& params) {
    params.structSize = sizeof(XPLMCreateWindow_t);
    params.contentType = xplm_WindowContentTypePanelGraphics;
}

void configureOpenGLWindow(XPLMCreateWindow_t& params) {
    params.structSize = static_cast<int>(offsetof(XPLMCreateWindow_t, contentType));
    params.contentType = xplm_WindowContentTypeOpenGL;
}

bool beginPanelWindow(XPLMWindowID window, std::uint64_t ownerScriptId) {
    if (!gState.panelEnabled || window == nullptr) {
        return false;
    }
    registerWindow(window, ownerScriptId);
    auto found = gState.windowStates.find(window);
    if (found == gState.windowStates.end()) {
        return false;
    }
    if (gState.drawing) {
        saveActiveWindowState();
    }
    loadWindowState(window);
    gState.activeWindow = window;
    gState.activeWindowGeneration = found->second.generation;
    gState.activeWindowOwnerScriptId = ownerScriptId;
    if (gTestRecorder != nullptr) {
        found->second.width = std::max(0, gTestWindowWidth);
        found->second.height = std::max(0, gTestWindowHeight);
    } else {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
        XPLMGetWindowGeometry(window, &left, &top, &right, &bottom);
        found->second.width = std::max(0, right - left);
        found->second.height = std::max(0, top - bottom);
    }
    gState.drawing = true;
    gState.color[0] = 1.0f;
    gState.color[1] = 1.0f;
    gState.color[2] = 1.0f;
    gState.color[3] = 1.0f;
    gState.lineWidth = 1.0f;
    gState.currentTexture = nullptr;
    gState.primitiveActive = false;
    gState.primitiveSource = PrimitiveSource::Legacy;
    gState.primitiveOwnerScriptId = flywithlua::kSystemLuaScriptId;
    gState.vertices.clear();
    gState.meshVertexScratch.clear();
    gState.meshNativeVertexScratch.clear();
    gState.meshIndexScratch.clear();
    return true;
}

void endPanelWindow() {
    if (!gState.drawing) {
        return;
    }
    if (!gState.vertices.empty()) {
        gState.vertices.clear();
    }
    gState.primitiveActive = false;
    gState.primitiveSource = PrimitiveSource::Legacy;
    gState.primitiveOwnerScriptId = flywithlua::kSystemLuaScriptId;
    saveActiveWindowState();
    gState.currentTexture = nullptr;
    gState.drawing = false;
    gState.activeWindow = nullptr;
    gState.activeWindowGeneration = 0;
    gState.activeWindowOwnerScriptId = flywithlua::kSystemLuaScriptId;
}

PanelDrawScope::PanelDrawScope(XPLMWindowID window, std::uint64_t ownerScriptId):
    snapshot_(std::make_unique<StateSnapshot>()),
    previousLuaDrawingState_(flywithlua::WeAreNotInDrawingState),
    ownerScriptId_(ownerScriptId),
    active_(false) {
    snapshot_->sessionGeneration = gState.sessionGeneration;
    snapshot_->drawing = gState.drawing;
    snapshot_->activeWindow = gState.activeWindow;
    snapshot_->activeWindowGeneration = gState.activeWindowGeneration;
    snapshot_->activeWindowOwnerScriptId = gState.activeWindowOwnerScriptId;
    snapshot_->currentTexture = gState.currentTexture;
    std::copy(std::begin(gState.color), std::end(gState.color), std::begin(snapshot_->color));
    snapshot_->lineWidth = gState.lineWidth;
    snapshot_->primitiveMode = gState.primitiveMode;
    snapshot_->primitiveActive = gState.primitiveActive;
    snapshot_->primitiveSource = gState.primitiveSource;
    snapshot_->primitiveOwnerScriptId = gState.primitiveOwnerScriptId;
    snapshot_->vertices = gState.vertices;
    snapshot_->meshVertexScratch = gState.meshVertexScratch;
    snapshot_->meshNativeVertexScratch = gState.meshNativeVertexScratch;
    snapshot_->meshIndexScratch = gState.meshIndexScratch;
    active_ = beginPanelWindow(window, ownerScriptId);
    if (active_) {
        flywithlua::WeAreNotInDrawingState = false;
    }
}

PanelDrawScope::~PanelDrawScope() {
    if (active_) {
        const bool unfinishedPrimitive = gState.primitiveActive;
        gState.vertices.clear();
        gState.primitiveActive = false;
        gState.primitiveOwnerScriptId = flywithlua::kSystemLuaScriptId;
        endPanelWindow();
        const bool sessionStillValid = gState.sessionGeneration == snapshot_->sessionGeneration;
        const auto restoredWindow = gState.windowStates.find(snapshot_->activeWindow);
        const bool restoredWindowIsCurrent = !snapshot_->drawing ||
            (restoredWindow != gState.windowStates.end() &&
             restoredWindow->second.generation == snapshot_->activeWindowGeneration);
        if (snapshot_->drawing && sessionStillValid && restoredWindowIsCurrent) {
            gState.drawing = snapshot_->drawing;
            gState.activeWindow = snapshot_->activeWindow;
            gState.activeWindowGeneration = snapshot_->activeWindowGeneration;
            gState.activeWindowOwnerScriptId = snapshot_->activeWindowOwnerScriptId;
            gState.currentTexture = snapshot_->currentTexture;
            std::copy(std::begin(snapshot_->color), std::end(snapshot_->color), std::begin(gState.color));
            gState.lineWidth = snapshot_->lineWidth;
            gState.primitiveMode = snapshot_->primitiveMode;
            gState.primitiveActive = snapshot_->primitiveActive;
            gState.primitiveSource = snapshot_->primitiveSource;
            gState.primitiveOwnerScriptId = snapshot_->primitiveOwnerScriptId;
            gState.vertices = std::move(snapshot_->vertices);
            gState.meshVertexScratch = std::move(snapshot_->meshVertexScratch);
            gState.meshNativeVertexScratch = std::move(snapshot_->meshNativeVertexScratch);
            gState.meshIndexScratch = std::move(snapshot_->meshIndexScratch);
        } else {
            gState.drawing = false;
            gState.activeWindow = nullptr;
            gState.activeWindowGeneration = 0;
            gState.activeWindowOwnerScriptId = flywithlua::kSystemLuaScriptId;
            gState.currentTexture = nullptr;
            gState.vertices.clear();
            gState.meshVertexScratch.clear();
            gState.meshNativeVertexScratch.clear();
            gState.meshIndexScratch.clear();
        }
        if (unfinishedPrimitive && ownerScriptId_ != flywithlua::kSystemLuaScriptId) {
            flywithlua::ReportLuaScriptError(ownerScriptId_, "panel_end",
                                             "primitive was not ended before the panel callback returned");
        }
    }
    flywithlua::WeAreNotInDrawingState = previousLuaDrawingState_;
}

bool PanelDrawScope::active() const {
    return active_;
}

namespace {

bool activeWindowStateIsCurrent() {
    if (!gState.drawing || gState.activeWindow == nullptr) {
        return false;
    }
    const auto found = gState.windowStates.find(gState.activeWindow);
    return found != gState.windowStates.end() &&
           found->second.generation == gState.activeWindowGeneration;
}

} // namespace

void setColor(float red, float green, float blue, float alpha) {
    if (!activeWindowStateIsCurrent()) {
        return;
    }
    gState.color[0] = red;
    gState.color[1] = green;
    gState.color[2] = blue;
    gState.color[3] = alpha;
}

std::uint32_t currentColor() {
    return packedColor();
}

void setLineWidth(float width) {
    if (!activeWindowStateIsCurrent()) {
        return;
    }
    gState.lineWidth = std::max(1.0f, width);
}

bool beginPrimitive(LegacyPrimitiveMode mode, PrimitiveSource source,
                    std::uint64_t ownerScriptId) {
    if (!activeWindowStateIsCurrent() || gState.primitiveActive) {
        return false;
    }
    gState.primitiveMode = mode;
    gState.primitiveActive = true;
    gState.primitiveSource = source;
    gState.primitiveOwnerScriptId = ownerScriptId;
    gState.vertices.clear();
    return true;
}

bool vertex(float x, float y, PrimitiveSource source, std::uint64_t ownerScriptId) {
    if (!activeWindowStateIsCurrent() || !gState.primitiveActive ||
        gState.primitiveSource != source || gState.primitiveOwnerScriptId != ownerScriptId) {
        return false;
    }
    gState.vertices.push_back({x, y});
    return true;
}

bool endPrimitive(PrimitiveSource source, std::uint64_t ownerScriptId) {
    if (!activeWindowStateIsCurrent() || !gState.primitiveActive ||
        gState.primitiveSource != source ||
        gState.primitiveOwnerScriptId != ownerScriptId) {
        return false;
    }
    drawCapturedPrimitive();
    gState.vertices.clear();
    gState.primitiveActive = false;
    gState.primitiveSource = PrimitiveSource::Legacy;
    gState.primitiveOwnerScriptId = flywithlua::kSystemLuaScriptId;
    return true;
}

void drawFilledRect(float x1, float y1, float x2, float y2) {
    if (!activeWindowStateIsCurrent()) {
        return;
    }
    const float left = std::min(x1, x2);
    const float right = std::max(x1, x2);
    const float bottom = std::min(y1, y2);
    const float top = std::max(y1, y2);
    const XPLMVertex_t vertices[] = {
        {left, bottom}, {right, bottom}, {right, top}, {left, top}
    };
    drawPolygon(vertices, 4);
}

bool drawText(float x, float y, const char* text, float fontSize, const char* family,
              int weight, float red, float green, float blue, float alpha) {
    if (!activeWindowStateIsCurrent() || text == nullptr || fontSize <= 0.0f ||
        gState.api.drawString == nullptr) {
        return false;
    }
    XPLMFontHandle font = ensureFont(family, weight);
    if (font == nullptr) {
        return false;
    }
    setColor(red, green, blue, alpha);
    gState.api.drawString(font, packedColor(), fontSize, x, y, text, xplm_JustLeft);
    return true;
}

bool drawTextCurrentColor(float x, float y, const char* text, float fontSize,
                          const char* family, int weight) {
    if (!activeWindowStateIsCurrent() || text == nullptr || fontSize <= 0.0f ||
        gState.api.drawString == nullptr) {
        return false;
    }
    XPLMFontHandle font = ensureFont(family, weight);
    if (font == nullptr) {
        return false;
    }
    gState.api.drawString(font, packedColor(), fontSize, x, y, text, xplm_JustLeft);
    return true;
}

double measureText(const char* text, float fontSize, const char* family, int weight) {
    if (!activeWindowStateIsCurrent() || text == nullptr || fontSize <= 0.0f ||
        gState.api.measureString == nullptr) {
        return -1.0;
    }
    XPLMFontHandle font = ensureFont(family, weight);
    if (font == nullptr) {
        return -1.0;
    }
    return static_cast<double>(gState.api.measureString(font, fontSize, text));
}

int drawLegacyText(int x, int y, const char* text, const char* fontName,
                   float red, float green, float blue, float alpha) {
    const char* family = (fontName != nullptr &&
                          std::strstr(fontName, "mono") != nullptr) ? "sf_mono" : "sf_pro_text";
    float size = 12.0f;
    if (fontName != nullptr) {
        if (std::strstr(fontName, "10") != nullptr) size = 10.0f;
        if (std::strstr(fontName, "18") != nullptr) size = 18.0f;
        if (std::strstr(fontName, "24") != nullptr) size = 24.0f;
    }
    return drawText(static_cast<float>(x), static_cast<float>(y), text, size,
                    family, 400, red, green, blue, alpha) ? 1 : 0;
}

double measureLegacyText(const char* text, const char* fontName) {
    const char* family = (fontName != nullptr &&
                          std::strstr(fontName, "mono") != nullptr) ? "sf_mono" : "sf_pro_text";
    float size = 12.0f;
    if (fontName != nullptr) {
        if (std::strstr(fontName, "10") != nullptr) size = 10.0f;
        if (std::strstr(fontName, "18") != nullptr) size = 18.0f;
        if (std::strstr(fontName, "24") != nullptr) size = 24.0f;
    }
    return measureText(text, size, family, 400);
}

int drawLegacyTextCurrentColor(int x, int y, const char* text, const char* fontName) {
    const char* family = (fontName != nullptr &&
                          std::strstr(fontName, "mono") != nullptr) ? "sf_mono" : "sf_pro_text";
    float size = 12.0f;
    if (fontName != nullptr) {
        if (std::strstr(fontName, "10") != nullptr) size = 10.0f;
        if (std::strstr(fontName, "18") != nullptr) size = 18.0f;
        if (std::strstr(fontName, "24") != nullptr) size = 24.0f;
    }
    return drawTextCurrentColor(static_cast<float>(x), static_cast<float>(y), text,
                                size, family, 400) ? 1 : 0;
}

void* createTexture(const unsigned char* rgbaImage, int width, int height) {
    if (!activeWindowStateIsCurrent() || !hasCapability(CapabilityTexture) ||
        gState.api.createTexture == nullptr ||
        rgbaImage == nullptr || width <= 0 || height <= 0) {
        return nullptr;
    }
    return gState.api.createTexture(rgbaImage, width, height);
}

void destroyTexture(void* texture) {
    if (texture != nullptr && gState.api.destroyTexture != nullptr) {
        gState.api.destroyTexture(texture);
    }
}

bool drawCalls(const XPLMMesh_t& mesh, const DrawCall* calls, size_t callCount,
               MeshCoordinateSpace coordinateSpace);

bool drawCalls(const XPLMMesh_t& mesh, const std::vector<DrawCall>& calls,
               MeshCoordinateSpace coordinateSpace) {
    return drawCalls(mesh, calls.data(), calls.size(), coordinateSpace);
}

bool drawCalls(const XPLMMesh_t& mesh, const DrawCall& call,
               MeshCoordinateSpace coordinateSpace) {
    return drawCalls(mesh, &call, 1u, coordinateSpace);
}

bool drawCalls(const XPLMMesh_t& mesh, const DrawCall* calls, size_t callCount,
               MeshCoordinateSpace coordinateSpace) {
    if (!activeWindowStateIsCurrent() || !hasCapability(CapabilityMesh) ||
        gState.api.drawCalls == nullptr || calls == nullptr || callCount == 0 ||
        callCount > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        mesh.vertex_count <= 0 || mesh.vertices == nullptr || mesh.index_count <= 0 ||
        mesh.indices == nullptr) {
        return false;
    }
    const auto active = gState.windowStates.find(gState.activeWindow);
    if (active == gState.windowStates.end() ||
        active->second.generation != gState.activeWindowGeneration) {
        return false;
    }

    XPLMMesh_t nativeMesh = mesh;
    if (coordinateSpace == MeshCoordinateSpace::PublicYUp) {
        if (mesh.vertex_count > std::numeric_limits<int>::max() / 5) {
            return false;
        }
        const size_t vertexFloatCount = static_cast<size_t>(mesh.vertex_count) * 5u;
        gState.meshNativeVertexScratch.assign(mesh.vertices,
                                              mesh.vertices + vertexFloatCount);
        for (int index = 0; index < mesh.vertex_count; ++index) {
            gState.meshNativeVertexScratch[static_cast<size_t>(index) * 5u + 1u] =
                static_cast<float>(active->second.height) -
                gState.meshNativeVertexScratch[static_cast<size_t>(index) * 5u + 1u];
        }
        nativeMesh.vertices = gState.meshNativeVertexScratch.data();
    }

    std::vector<XPLMDrawCall_t>& sdkCalls = active->second.sdkDrawCalls;
    sdkCalls.clear();
    if (sdkCalls.capacity() < callCount) {
        sdkCalls.reserve(callCount);
    }
    for (size_t index = 0; index < callCount; ++index) {
        const DrawCall& call = calls[index];
        if (call.texture == nullptr || call.indexOffset < 0 || call.elementCount < 0 ||
            call.vertexOffset < 0 || call.elementCount % 3 != 0 ||
            call.indexOffset > nativeMesh.index_count ||
            call.elementCount > nativeMesh.index_count - call.indexOffset ||
            (call.elementCount > 0 && call.vertexOffset >= nativeMesh.vertex_count)) {
            return false;
        }
        XPLMDrawCall_t sdkCall{};
        sdkCall.tex_ref = call.texture;
        float left = call.scissors[0];
        float bottom = call.scissors[1];
        float right = call.scissors[2];
        float top = call.scissors[3];
        if (right >= 99999.0f && top >= 99999.0f) {
            left = 0.0f;
            bottom = 0.0f;
            right = static_cast<float>(active->second.width);
            top = static_cast<float>(active->second.height);
        }
        if (coordinateSpace == MeshCoordinateSpace::PublicYUp) {
            sdkCall.scissors[0] = left;
            sdkCall.scissors[1] = static_cast<float>(active->second.height) - top;
            sdkCall.scissors[2] = right;
            sdkCall.scissors[3] = static_cast<float>(active->second.height) - bottom;
        } else {
            std::copy(std::begin(call.scissors), std::end(call.scissors), sdkCall.scissors);
        }
        sdkCall.idx_offset = call.indexOffset;
        sdkCall.element_count = call.elementCount;
        sdkCall.vtx_offset = call.vertexOffset;
        sdkCalls.push_back(sdkCall);
    }
    gState.currentTexture = sdkCalls.back().tex_ref;
    gState.api.drawCalls(&nativeMesh, static_cast<int>(sdkCalls.size()), sdkCalls.data());
    return true;
}

std::vector<float>& meshVertexScratch() {
    return gState.meshVertexScratch;
}

std::vector<std::uint16_t>& meshIndexScratch() {
    return gState.meshIndexScratch;
}

namespace testing {

void resetForTesting(std::uint32_t capabilityMask, Recorder* recorder,
                     int windowWidth, int windowHeight) {
    const std::uint64_t nextSessionGeneration = gState.sessionGeneration + 1;
    gState = State{};
    gState.sessionGeneration = nextSessionGeneration;
    gState.initialized = true;
    gState.capabilityMask = capabilityMask;
    gState.panelEnabled = capabilityMask == kRequiredCapabilityMask;
    gTestRecorder = recorder;
    gTestWindowWidth = windowWidth;
    gTestWindowHeight = windowHeight;

    gState.api.polygon = testPolygon;
    gState.api.lines = testLines;
    gState.api.linesWithWidth = testLinesWithWidth;
    gState.api.lineStrip = testLines;
    gState.api.lineStripWithWidth = testLinesWithWidth;
    gState.api.lineLoop = testLines;
    gState.api.lineLoopWithWidth = testLinesWithWidth;
    gState.api.quadStrip = testQuadStrip;
    gState.api.drawCalls = testDrawCalls;
}

std::uint64_t windowGenerationForTesting(XPLMWindowID window) {
    const auto found = gState.windowStates.find(window);
    return found == gState.windowStates.end() ? 0 : found->second.generation;
}

} // namespace testing

} // namespace flywithlua::panel

extern "C" int flywithlua_panel_draw_hidpi_text(int x, int y, const char* text,
                                                  float logicalSize, const char* family,
                                                  int weight) {
    return flywithlua::panel::drawTextCurrentColor(static_cast<float>(x), static_cast<float>(y),
                                                   text, logicalSize, family, weight)
        ? 1 : 0;
}

extern "C" int flywithlua_panel_is_drawing(void) {
    return flywithlua::panel::panelDrawing() ? 1 : 0;
}

extern "C" double flywithlua_panel_measure_hidpi_text(const char* text, float logicalSize,
                                                       const char* family, int weight) {
    return flywithlua::panel::measureText(text, logicalSize, family, weight);
}

extern "C" int flywithlua_panel_draw_legacy_text(int x, int y, const char* text,
                                                   const char* fontName, const float* color) {
    if (color == nullptr) {
        return flywithlua::panel::drawLegacyTextCurrentColor(x, y, text, fontName);
    }
    const float red = color != nullptr ? color[0] : 1.0f;
    const float green = color != nullptr ? color[1] : 1.0f;
    const float blue = color != nullptr ? color[2] : 1.0f;
    const float alpha = color != nullptr ? color[3] : 1.0f;
    return flywithlua::panel::drawLegacyText(x, y, text, fontName, red, green, blue, alpha);
}

extern "C" double flywithlua_panel_measure_legacy_text(const char* text, const char* fontName) {
    return flywithlua::panel::measureLegacyText(text, fontName);
}
