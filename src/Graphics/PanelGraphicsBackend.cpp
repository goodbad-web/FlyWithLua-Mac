#include "PanelGraphicsBackend.h"

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
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float lineWidth = 1.0f;
    LegacyPrimitiveMode primitiveMode = LegacyPrimitiveMode::Lines;
    bool primitiveActive = false;
    PrimitiveSource primitiveSource = PrimitiveSource::Legacy;
    std::vector<XPLMVertex_t> vertices;
    std::vector<XPLMDrawCall_t> sdkDrawCalls;
};

struct State {
    BackendPreference preference = BackendPreference::Auto;
    bool initialized = false;
    bool panelEnabled = false;
    std::uint32_t capabilityMask = CapabilityNone;
    bool drawing = false;
    bool failureLogged = false;
    XPLMWindowID activeWindow = nullptr;
    std::uint64_t activeWindowGeneration = 0;
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
    std::vector<XPLMVertex_t> vertices;
    std::vector<float> meshVertexScratch;
    std::vector<std::uint16_t> meshIndexScratch;
};

State gState;

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
    std::copy(std::begin(gState.color), std::end(gState.color), std::begin(windowState.color));
    windowState.lineWidth = gState.lineWidth;
    windowState.primitiveMode = gState.primitiveMode;
    windowState.primitiveActive = gState.primitiveActive;
    windowState.primitiveSource = gState.primitiveSource;
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
        gState.primitiveMode = LegacyPrimitiveMode::Lines;
        gState.primitiveActive = false;
        gState.primitiveSource = PrimitiveSource::Legacy;
        gState.vertices.clear();
        return;
    }

    const WindowState& windowState = found->second;
    std::copy(std::begin(windowState.color), std::end(windowState.color), std::begin(gState.color));
    gState.lineWidth = windowState.lineWidth;
    gState.primitiveMode = windowState.primitiveMode;
    gState.primitiveActive = windowState.primitiveActive;
    gState.primitiveSource = windowState.primitiveSource;
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
        logMessage("requested=opengl ignored=panel-required");
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
               " effective=" + (gState.panelEnabled ? "panel" : "disabled") +
               " capabilities=" + std::to_string(gState.capabilityMask) +
               (gState.panelEnabled ? "" : " reason=required symbol unavailable"));
}

void shutdown() {
    releaseFonts();
    gState = State{};
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

void registerWindow(XPLMWindowID window) {
    if (window != nullptr) {
        auto result = gState.windowStates.try_emplace(window);
        if (result.second) {
            result.first->second.generation = gState.nextWindowGeneration++;
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
    }
    gState.windowStates.erase(window);
}

void disableForSession(const char* reason) {
    if (gState.panelEnabled && !gState.failureLogged) {
        gState.failureLogged = true;
        logMessage(std::string("disabled for session; no OpenGL fallback: ") +
                   (reason != nullptr ? reason : "unknown failure"));
    }
    gState.panelEnabled = false;
    gState.capabilityMask = CapabilityNone;
    gState.drawing = false;
    gState.activeWindow = nullptr;
    gState.activeWindowGeneration = 0;
    gState.primitiveActive = false;
    gState.windowStates.clear();
}

void configurePanelWindow(XPLMCreateWindow_t& params) {
    params.structSize = sizeof(XPLMCreateWindow_t);
    params.contentType = xplm_WindowContentTypePanelGraphics;
}

void configureOpenGLWindow(XPLMCreateWindow_t& params) {
    params.structSize = static_cast<int>(offsetof(XPLMCreateWindow_t, contentType));
    params.contentType = xplm_WindowContentTypeOpenGL;
}

bool beginPanelWindow(XPLMWindowID window) {
    if (!gState.panelEnabled || window == nullptr || gState.drawing) {
        return false;
    }
    registerWindow(window);
    loadWindowState(window);
    gState.activeWindow = window;
    gState.activeWindowGeneration = gState.windowStates.at(window).generation;
    gState.drawing = true;
    gState.color[0] = 1.0f;
    gState.color[1] = 1.0f;
    gState.color[2] = 1.0f;
    gState.color[3] = 1.0f;
    gState.lineWidth = 1.0f;
    gState.primitiveActive = false;
    gState.primitiveSource = PrimitiveSource::Legacy;
    gState.vertices.clear();
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
    saveActiveWindowState();
    gState.drawing = false;
    gState.activeWindow = nullptr;
}

PanelDrawScope::PanelDrawScope(XPLMWindowID window):
    previousLuaDrawingState_(flywithlua::WeAreNotInDrawingState),
    active_(beginPanelWindow(window)) {
    if (active_) {
        flywithlua::WeAreNotInDrawingState = false;
    }
}

PanelDrawScope::~PanelDrawScope() {
    if (active_) {
        endPanelWindow();
    }
    flywithlua::WeAreNotInDrawingState = previousLuaDrawingState_;
}

bool PanelDrawScope::active() const {
    return active_;
}

void setColor(float red, float green, float blue, float alpha) {
    gState.color[0] = red;
    gState.color[1] = green;
    gState.color[2] = blue;
    gState.color[3] = alpha;
}

std::uint32_t currentColor() {
    return packedColor();
}

void setLineWidth(float width) {
    gState.lineWidth = std::max(1.0f, width);
}

bool beginPrimitive(LegacyPrimitiveMode mode, PrimitiveSource source) {
    if (!gState.drawing || (gState.primitiveActive && gState.primitiveSource != source)) {
        return false;
    }
    gState.primitiveMode = mode;
    gState.primitiveActive = true;
    gState.primitiveSource = source;
    gState.vertices.clear();
    return true;
}

bool vertex(float x, float y, PrimitiveSource source) {
    if (!gState.drawing || (gState.primitiveActive && gState.primitiveSource != source)) {
        return false;
    }
    gState.vertices.push_back({x, y});
    return true;
}

bool endPrimitive(PrimitiveSource source) {
    if (!gState.drawing || !gState.primitiveActive || gState.primitiveSource != source) {
        return false;
    }
    drawCapturedPrimitive();
    gState.vertices.clear();
    gState.primitiveActive = false;
    gState.primitiveSource = PrimitiveSource::Legacy;
    return true;
}

void drawFilledRect(float x1, float y1, float x2, float y2) {
    if (!gState.drawing) {
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
    if (!gState.drawing || text == nullptr || fontSize <= 0.0f ||
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
    if (!gState.drawing || text == nullptr || fontSize <= 0.0f ||
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
    if (!gState.drawing || text == nullptr || fontSize <= 0.0f ||
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
    if (!gState.drawing || !hasCapability(CapabilityTexture) ||
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

bool drawCalls(const XPLMMesh_t& mesh, const DrawCall* calls, size_t callCount);

bool drawCalls(const XPLMMesh_t& mesh, const std::vector<DrawCall>& calls) {
    return drawCalls(mesh, calls.data(), calls.size());
}

bool drawCalls(const XPLMMesh_t& mesh, const DrawCall& call) {
    return drawCalls(mesh, &call, 1u);
}

bool drawCalls(const XPLMMesh_t& mesh, const DrawCall* calls, size_t callCount) {
    if (!gState.drawing || !hasCapability(CapabilityMesh) ||
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

    std::vector<XPLMDrawCall_t>& sdkCalls = active->second.sdkDrawCalls;
    sdkCalls.clear();
    if (sdkCalls.capacity() < callCount) {
        sdkCalls.reserve(callCount);
    }
    for (size_t index = 0; index < callCount; ++index) {
        const DrawCall& call = calls[index];
        if (call.texture == nullptr || call.indexOffset < 0 || call.elementCount < 0 ||
            call.vertexOffset < 0 || call.elementCount % 3 != 0 ||
            call.indexOffset > mesh.index_count ||
            call.elementCount > mesh.index_count - call.indexOffset ||
            (call.elementCount > 0 && call.vertexOffset >= mesh.vertex_count)) {
            return false;
        }
        XPLMDrawCall_t sdkCall{};
        sdkCall.tex_ref = call.texture;
        std::copy(std::begin(call.scissors), std::end(call.scissors), sdkCall.scissors);
        sdkCall.idx_offset = call.indexOffset;
        sdkCall.element_count = call.elementCount;
        sdkCall.vtx_offset = call.vertexOffset;
        sdkCalls.push_back(sdkCall);
    }
    gState.api.drawCalls(&mesh, static_cast<int>(sdkCalls.size()), sdkCalls.data());
    return true;
}

std::vector<float>& meshVertexScratch() {
    return gState.meshVertexScratch;
}

std::vector<std::uint16_t>& meshIndexScratch() {
    return gState.meshIndexScratch;
}

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
