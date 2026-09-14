#include "ThreeJFPSNative.h"

#include "ThreeJFPSControllerCore.h"
#include "XPLMDataAccess.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using threejfps::Controller;
using threejfps::ControllerConfig;
using threejfps::ControllerSample;
using threejfps::FeatureConfig;
using threejfps::FeatureId;
using threejfps::Mode;

extern "C" void flywithlua_update_3jfps_snapshot(const char* jsonPayload);
extern "C" void flywithlua_show_3jfps_settings(void);
extern "C" int flywithlua_draw_hidpi_text(int x,
                                          int y,
                                          const char* text,
                                          float logicalSize,
                                          const char* family,
                                          int weight);
extern "C" double flywithlua_measure_hidpi_text(const char* text,
                                                float logicalSize,
                                                const char* family,
                                                int weight);

constexpr double kSnapshotIntervalSeconds = 0.2;
constexpr double kHUDDisplayIntervalSeconds = 0.5;
constexpr double kMinimumDeltaSeconds = 1.0 / 240.0;
constexpr double kMaximumDeltaSeconds = 1.0;
constexpr int kHUDMinimumWidth = 190;
constexpr int kHUDHorizontalPadding = 24;
constexpr int kHUDGraphMinimumWidth = 120;
constexpr int kHUDGraphMaximumWidth = 240;
constexpr int kHUDGraphGap = 14;
constexpr double kHUDGraphEnvelopeSpeedFPS = 4.0;
constexpr double kHUDStutterHoldSeconds = 0.5;

struct NumericDataRef {
    XPLMDataRef ref = nullptr;
    double originalValue = 0.0;
    bool originalCaptured = false;
};

class HUDPrimitiveStateGuard {
public:
    HUDPrimitiveStateGuard() {
        // X-Plane and plug-ins share the fixed-function OpenGL state.  The
        // SDK helper does not manage face culling or scissoring, so an
        // inherited state can make otherwise valid screen-space quads
        // disappear (especially on the OpenGL/Metal bridge).
        glPushAttrib(GLbitfield(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT |
                                GL_CURRENT_BIT | GL_SCISSOR_BIT));
        XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    ~HUDPrimitiveStateGuard() {
        glPopAttrib();

        // Keep X-Plane's cached state consistent with the state that the HUD
        // historically left for the next drawing path.  The attribute stack
        // restores unmanaged state such as culling and scissoring above.
        XPLMSetGraphicsState(0, 0, 0, 1, 1, 0, 0);
    }

    HUDPrimitiveStateGuard(const HUDPrimitiveStateGuard&) = delete;
    HUDPrimitiveStateGuard& operator=(const HUDPrimitiveStateGuard&) = delete;
};

bool readBoolField(lua_State* state, int tableIndex, const char* name, bool fallback) {
    lua_getfield(state, tableIndex, name);
    const bool value = lua_isboolean(state, -1) ? lua_toboolean(state, -1) != 0 : fallback;
    lua_pop(state, 1);
    return value;
}

double readNumberField(lua_State* state, int tableIndex, const char* name, double fallback) {
    lua_getfield(state, tableIndex, name);
    const bool valid = lua_isnumber(state, -1) != 0;
    const double value = valid ? lua_tonumber(state, -1) : fallback;
    lua_pop(state, 1);
    return std::isfinite(value) ? value : fallback;
}

std::string readStringField(lua_State* state, int tableIndex, const char* name,
                            const std::string& fallback) {
    lua_getfield(state, tableIndex, name);
    size_t length = 0;
    const char* value = lua_tolstring(state, -1, &length);
    std::string result = value != nullptr ? std::string(value, length) : fallback;
    lua_pop(state, 1);
    return result;
}

FeatureConfig readFeature(lua_State* state, int tableIndex, FeatureId id,
                          const char* enabledName, const char* availableName,
                          const char* lowName, const char* highName) {
    FeatureConfig feature;
    feature.id = id;
    feature.enabled = readBoolField(state, tableIndex, enabledName, false);
    feature.available = readBoolField(state, tableIndex, availableName, false);
    feature.lowValue = readNumberField(state, tableIndex, lowName, 0.0);
    feature.highValue = readNumberField(state, tableIndex, highName, 1.0);
    return feature;
}

std::string jsonStringField(const std::string& json, const std::string& key) {
    const std::string marker = "\"" + key + "\"";
    const size_t markerPosition = json.find(marker);
    if (markerPosition == std::string::npos) {
        return std::string();
    }
    const size_t colon = json.find(':', markerPosition + marker.size());
    if (colon == std::string::npos) {
        return std::string();
    }
    size_t start = json.find_first_not_of(" \t\r\n", colon + 1);
    if (start == std::string::npos || json[start] != '"') {
        return std::string();
    }
    ++start;
    std::string result;
    for (size_t index = start; index < json.size(); ++index) {
        const char character = json[index];
        if (character == '"') {
            return result;
        }
        if (character == '\\' && index + 1 < json.size()) {
            ++index;
            switch (json[index]) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                default: result.push_back(json[index]); break;
            }
        } else {
            result.push_back(character);
        }
    }
    return std::string();
}

std::string jsonRawField(const std::string& json, const std::string& key) {
    const std::string marker = "\"" + key + "\"";
    const size_t markerPosition = json.find(marker);
    if (markerPosition == std::string::npos) {
        return std::string();
    }
    const size_t colon = json.find(':', markerPosition + marker.size());
    if (colon == std::string::npos) {
        return std::string();
    }
    const size_t start = json.find_first_not_of(" \t\r\n", colon + 1);
    if (start == std::string::npos) {
        return std::string();
    }
    if (json[start] == '"') {
        return jsonStringField(json, key);
    }
    size_t end = json.find_first_of(",}", start);
    if (end == std::string::npos) {
        end = json.size();
    }
    while (end > start && std::isspace(static_cast<unsigned char>(json[end - 1]))) {
        --end;
    }
    return json.substr(start, end - start);
}

double jsonNumberField(const std::string& json, const std::string& key, double fallback) {
    const std::string raw = jsonRawField(json, key);
    if (raw.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const double value = std::strtod(raw.c_str(), &end);
    return end != raw.c_str() && std::isfinite(value) ? value : fallback;
}

bool jsonBoolField(const std::string& json, const std::string& key, bool fallback) {
    const std::string raw = jsonRawField(json, key);
    if (raw == "true") return true;
    if (raw == "false") return false;
    return fallback;
}

std::string jsonEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const char character : value) {
        switch (character) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped.push_back(character); break;
        }
    }
    return escaped;
}

std::string numberString(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    return stream.str();
}

std::string hudNumberString(double value, int precision, bool trimTrailingZeros) {
    if (!std::isfinite(value)) {
        return "--";
    }

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(std::max(0, precision)) << value;
    std::string result = stream.str();
    if (trimTrailingZeros && precision > 0) {
        while (!result.empty() && result.back() == '0') {
            result.pop_back();
        }
        if (!result.empty() && result.back() == '.') {
            result.pop_back();
        }
    }
    return result;
}

double readDataRef(NumericDataRef& dataRef) {
    if (dataRef.ref == nullptr) {
        return 0.0;
    }
    const XPLMDataTypeID type = XPLMGetDataRefTypes(dataRef.ref);
    if ((type & xplmType_Double) != 0) return XPLMGetDatad(dataRef.ref);
    if ((type & xplmType_Float) != 0) return XPLMGetDataf(dataRef.ref);
    if ((type & xplmType_Int) != 0) return XPLMGetDatai(dataRef.ref);
    return 0.0;
}

bool writeDataRef(NumericDataRef& dataRef, double value) {
    if (dataRef.ref == nullptr || XPLMCanWriteDataRef(dataRef.ref) == 0) {
        return false;
    }
    const XPLMDataTypeID type = XPLMGetDataRefTypes(dataRef.ref);
    if ((type & xplmType_Double) != 0) {
        XPLMSetDatad(dataRef.ref, value);
        return true;
    }
    if ((type & xplmType_Float) != 0) {
        XPLMSetDataf(dataRef.ref, static_cast<float>(value));
        return true;
    }
    if ((type & xplmType_Int) != 0) {
        XPLMSetDatai(dataRef.ref, static_cast<int>(std::lround(value)));
        return true;
    }
    return false;
}

const char* featureName(FeatureId id) {
    switch (id) {
        case FeatureId::LOD: return "lod";
        case FeatureId::Shadows: return "shadows";
        case FeatureId::Clouds: return "clouds";
        case FeatureId::FSR: return "fsr";
    }
    return "unknown";
}

class Runtime final {
public:
    static Runtime& instance() {
        static Runtime runtime;
        return runtime;
    }

    void registerLuaFunctions(lua_State* state) {
        lua_register(state, "threejfps_register", &Runtime::luaRegister);
        lua_register(state, "threejfps_set_config", &Runtime::luaSetConfig);
        lua_register(state, "threejfps_unregister", &Runtime::luaUnregister);
        lua_register(state, "threejfps_open_settings", &Runtime::luaOpenSettings);
        lua_register(state, "threejfps_screenshot", &Runtime::luaScreenshot);
    }

    void registerFromLua(lua_State* state, int tableIndex) {
        luaState_ = state;
        dirty_ = false;
        snapshotTimer_ = 0.0;
        snapshotDirty_ = true;
        screenshotOverride_ = false;
        screenshotFrames_ = 0;
        configureFromLua(state, tableIndex, true);
        resetHUDDisplay();
        active_ = true;
        publishSnapshot();
    }

    void setConfigFromLua(lua_State* state, int tableIndex) {
        if (state == nullptr || !lua_istable(state, tableIndex)) {
            return;
        }
        luaState_ = state;
        configureFromLua(state, tableIndex, false);
        publishSnapshot();
    }

    void unregisterFromLua() {
        restoreAllDataRefs();
        resetOriginalDataRefCapture();
        active_ = false;
        luaState_ = nullptr;
        {
            std::lock_guard<std::mutex> lock(commandMutex_);
            queuedCommands_.clear();
        }
        dragging_ = false;
        inputCaptured_ = false;
        hudEditing_ = false;
        dirty_ = false;
        snapshotTimer_ = 0.0;
        snapshotDirty_ = true;
        screenshotOverride_ = false;
        screenshotFrames_ = 0;
        resetHUDDisplay();
    }

    void step(float deltaSeconds) {
        drainCommands();
        if (!active_) {
            return;
        }

        const double delta = std::max(kMinimumDeltaSeconds,
                                      std::min(kMaximumDeltaSeconds,
                                               static_cast<double>(deltaSeconds)));
        snapshotTimer_ += delta;

        if (screenshotOverride_) {
            ++screenshotFrames_;
            applyAllQuality(1.0);
            if (screenshotFrames_ == 3) {
                if (XPLMCommandRef command = XPLMFindCommand("sim/operation/screenshot")) {
                    XPLMCommandOnce(command);
                }
            }
            if (screenshotFrames_ >= 4) {
                screenshotOverride_ = false;
                screenshotFrames_ = 0;
            }
        }

        const double fps = 1.0 / delta;
        const bool hasGPU = config_.gpuTimeAvailable && gpuTime_.ref != nullptr;
        const bool hasCPU = config_.cpuTimeAvailable && swapTimeTotal_.ref != nullptr;
        double cpuMilliseconds = delta * 1000.0;
        if (hasCPU) {
            const double swapTotal = readDataRef(swapTimeTotal_);
            if (!swapTimeInitialized_) {
                previousSwapTime_ = swapTotal;
                swapTimeInitialized_ = true;
            }
            const double swapDelta = std::max(0.0, swapTotal - previousSwapTime_);
            previousSwapTime_ = swapTotal;
            cpuMilliseconds = std::max(0.0, delta * 1000.0 - swapDelta * 1000.0);
        }

        ControllerSample sample;
        sample.deltaSeconds = delta;
        sample.fps = fps;
        sample.cpuMilliseconds = cpuMilliseconds;
        sample.gpuMilliseconds = hasGPU ? std::max(0.0, readDataRef(gpuTime_) * 1000.0) : 0.0;
        sample.cpuTimeAvailable = hasCPU;
        sample.gpuTimeAvailable = hasGPU;
        controller_.update(sample);
        updateHUDDisplay(delta);

        applyControllerOutputs();
        applyIndependentRules();

        if (snapshotTimer_ >= kSnapshotIntervalSeconds || snapshotDirty_) {
            publishSnapshot();
        }
    }

    void drawHUD() {
        if (!active_) {
            return;
        }

        int screenLeft = 0;
        int screenTop = 0;
        int screenRight = 0;
        int screenBottom = 0;
        XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);
        const int screenWidth = std::max(1, screenRight - screenLeft);
        const int screenHeight = std::max(1, screenTop - screenBottom);

        int mouseX = 0;
        int mouseY = 0;
        XPLMGetMouseLocationGlobal(&mouseX, &mouseY);
        const int localMouseX = mouseX - screenLeft;
        const int localMouseY = mouseY - screenBottom;

        const int lineHeight = std::max(12, hudLineHeight_);
        const bool hover = isInsideHUD(localMouseX, localMouseY, screenWidth, screenHeight);
        const bool showDetails = hover || hudEditing_ || showDetailsPreference_;
        if (displayMode_ == "hov" && !showDetails && !hudEditing_) {
            return;
        }
        if (displayMode_ == "bad" && controller_.state().fps >= controller_.state().targetFPS &&
            !hudEditing_) {
            return;
        }

        const int boxHeight = hudBoxHeight(showDetails);
        const int boxWidth = hudBoxWidth(showDetails);
        const int x = resolveHUDX(screenWidth, boxWidth);
        const int y = resolveHUDY(screenHeight, boxHeight);
        const float alpha = static_cast<float>(std::max(0.15, std::min(1.0, hudAlpha_)));

        XPLMSetGraphicsState(0, 0, 0, 1, 1, 0, 0);
        XPLMDrawTranslucentDarkBox(x - 5, y + boxHeight + 5, x + boxWidth + 5, y - 5);

        float indicatorColor[4] = {0.25f, 0.85f, 0.35f, alpha};
        if (controller_.state().reason.find("overload") != std::string::npos ||
            controller_.state().reason == "fps-fallback") {
            indicatorColor[0] = 0.95f;
            indicatorColor[1] = 0.25f;
            indicatorColor[2] = 0.18f;
        } else if (controller_.state().reason == "recovering") {
            indicatorColor[0] = 0.95f;
            indicatorColor[1] = 0.75f;
            indicatorColor[2] = 0.15f;
        } else if (controller_.state().mode == Mode::Off) {
            indicatorColor[0] = 0.60f;
            indicatorColor[1] = 0.60f;
            indicatorColor[2] = 0.60f;
        }

        {
            HUDPrimitiveStateGuard primitiveState;
            glColor4f(indicatorColor[0], indicatorColor[1], indicatorColor[2], alpha);
            glBegin(GL_QUADS);
            glVertex2f(static_cast<float>(x), static_cast<float>(y));
            glVertex2f(static_cast<float>(x + 4), static_cast<float>(y));
            glVertex2f(static_cast<float>(x + 4), static_cast<float>(y + boxHeight));
            glVertex2f(static_cast<float>(x), static_cast<float>(y + boxHeight));
            glEnd();
        }

        const float textColor[3] = {1.0f, 1.0f, 1.0f};
        const int textX = x + 12;
        const int topY = y + boxHeight - lineHeight;
        drawText(textColor, textX, topY, hudModeLine(), alpha,
                 "sf_pro_text", 600);
        drawText(textColor, textX, topY - lineHeight, hudFPSLine(),
                 alpha, "sf_mono", 600);

        if (showDetails) {
            drawText(textColor, textX, topY - lineHeight * 2, hudMetricsLine(),
                     alpha, "sf_mono", 400);
            drawText(textColor, textX, topY - lineHeight * 3, hudQualityLine(),
                     alpha, "sf_mono", 400);
        }

        if (showGraph_) {
            const int graphX = textX + hudTextColumnWidth(showDetails) + kHUDGraphGap;
            HUDPrimitiveStateGuard primitiveState;
            drawGraph(graphX, topY, hudGraphWidth(), lineHeight, alpha);
        }

        if (hudEditing_) {
            drawText(indicatorColor, textX, y - 2, label("EDIT HUD", "HUD編集"),
                     indicatorColor[3], "sf_pro_text", 600);
        }
    }

    int handleClick(int x, int y, int mouseStatus) {
        if (!active_) {
            return 0;
        }
        int screenLeft = 0;
        int screenTop = 0;
        int screenRight = 0;
        int screenBottom = 0;
        XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);
        const int width = std::max(1, screenRight - screenLeft);
        const int height = std::max(1, screenTop - screenBottom);
        const int localX = x - screenLeft;
        const int localY = y - screenBottom;
        const bool showDetails = hudEditing_ || showDetailsPreference_;
        const int boxHeight = hudBoxHeight(showDetails);
        const int boxWidth = hudBoxWidth(showDetails);
        const bool inside = isInsideHUD(localX, localY, width, height, boxWidth, boxHeight);

        if (mouseStatus == xplm_MouseDown) {
            if (!inside) return 0;
            inputCaptured_ = true;
            if (hudEditing_) {
                dragging_ = true;
                dragOffsetX_ = localX - resolveHUDX(width, boxWidth);
                dragOffsetY_ = localY - resolveHUDY(height, boxHeight);
            } else {
                flywithlua_show_3jfps_settings();
            }
            return 1;
        }

        if (!inputCaptured_) {
            return 0;
        }
        if (mouseStatus == xplm_MouseDrag && dragging_) {
            hudX_ = std::max(0, localX - dragOffsetX_);
            hudY_ = std::max(0, localY - dragOffsetY_);
            hudPositionIsAbsolute_ = true;
            dirty_ = true;
            snapshotDirty_ = true;
            return 1;
        }
        if (mouseStatus == xplm_MouseUp) {
            if (dragging_) {
                dragging_ = false;
                callLuaAdapter("hudPosition", std::to_string(hudX_) + "," + std::to_string(hudY_));
            }
            inputCaptured_ = false;
            publishSnapshot();
            return 1;
        }
        return 1;
    }

    int handleWheel(int x, int y, int wheel, int clicks) {
        if (!active_ || !hudEditing_) {
            return 0;
        }
        int screenLeft = 0;
        int screenTop = 0;
        int screenRight = 0;
        int screenBottom = 0;
        XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);
        const int width = std::max(1, screenRight - screenLeft);
        const int height = std::max(1, screenTop - screenBottom);
        const int localX = x - screenLeft;
        const int localY = y - screenBottom;
        const int boxHeight = hudBoxHeight(true);
        if (!isInsideHUD(localX, localY, width, height, hudBoxWidth(true), boxHeight)) {
            return 0;
        }
        const int direction = (wheel == 0 ? 1 : (wheel > 0 ? 1 : -1));
        const int amount = std::max(1, std::abs(clicks)) * direction;
        hudLineHeight_ = std::max(12, std::min(30, hudLineHeight_ + amount));
        hudWidth_ = std::max(kHUDMinimumWidth, std::min(420, hudWidth_ + amount * 8));
        invalidateHUDLayout();
        dirty_ = true;
        snapshotDirty_ = true;
        callLuaAdapter("hudSize", std::to_string(hudWidth_) + "," + std::to_string(hudLineHeight_));
        publishSnapshot();
        return 1;
    }

    void enqueueCommand(const char* jsonCommand) {
        if (jsonCommand == nullptr || jsonCommand[0] == '\0') return;
        std::lock_guard<std::mutex> lock(commandMutex_);
        queuedCommands_.emplace_back(jsonCommand);
    }

    void onLuaReset() {
        unregisterFromLua();
        config_ = ControllerConfig{};
        controller_.reset(config_);
        profile_.clear();
    }

    void shutdown() {
        onLuaReset();
    }

    bool active() const { return active_; }

private:
    static int luaRegister(lua_State* state) {
        if (lua_istable(state, 1)) instance().registerFromLua(state, 1);
        return 0;
    }

    static int luaSetConfig(lua_State* state) {
        if (lua_istable(state, 1)) instance().setConfigFromLua(state, 1);
        return 0;
    }

    static int luaUnregister(lua_State* /*state*/) {
        instance().unregisterFromLua();
        return 0;
    }

    static int luaOpenSettings(lua_State* /*state*/) {
        flywithlua_show_3jfps_settings();
        return 0;
    }

    static int luaScreenshot(lua_State* /*state*/) {
        instance().beginScreenshot();
        return 0;
    }

    void beginScreenshot() {
        if (!active_) return;
        screenshotOverride_ = true;
        screenshotFrames_ = 0;
        snapshotDirty_ = true;
        publishSnapshot();
    }

    void configureFromLua(lua_State* state, int tableIndex, bool firstRegistration) {
        const std::string newProfile = readStringField(state, tableIndex, "profile", profile_);
        const bool profileChanged = !firstRegistration && !profile_.empty() && newProfile != profile_;
        const bool oldActive = active_;
        const ControllerConfig oldConfig = controller_.config();
        const bool requestedCPUTime = readBoolField(state, tableIndex, "cpuTimeRequested", false);
        const bool requestedGPUTime = readBoolField(state, tableIndex, "gpuTimeRequested", false);

        ControllerConfig newConfig;
        newConfig.mode = threejfps::modeFromString(readStringField(state, tableIndex, "mode", "auto"));
        newConfig.targetFPS = readNumberField(state, tableIndex, "targetFPS", 30.0);
        newConfig.cpuHeadroomPercent = readNumberField(state, tableIndex, "cpuHeadroom", 20.0);
        newConfig.gpuHeadroomPercent = readNumberField(state, tableIndex, "gpuHeadroom", 10.0);
        newConfig.detectionWindowSeconds = readNumberField(state, tableIndex, "detectionWindow", 0.5);
        newConfig.qualityChangeIntervalSeconds = readNumberField(state, tableIndex, "changeInterval", 2.0);
        newConfig.qualityRecoveryDelaySeconds = readNumberField(state, tableIndex, "recoveryDelay", 30.0);
        newConfig.degradationStep = readNumberField(state, tableIndex, "degradationStep", 0.20);
        newConfig.recoveryStep = readNumberField(state, tableIndex, "recoveryStep", 0.05);
        newConfig.cpuTimeAvailable = requestedCPUTime;
        newConfig.gpuTimeAvailable = requestedGPUTime;

        newConfig.features[0] = readFeature(state, tableIndex, FeatureId::LOD,
                                            "lodEnabled", "lodAvailable", "lodLow", "lodHigh");
        newConfig.features[1] = readFeature(state, tableIndex, FeatureId::Shadows,
                                            "shadowEnabled", "shadowAvailable", "shadowLow", "shadowHigh");
        newConfig.features[2] = readFeature(state, tableIndex, FeatureId::Clouds,
                                            "cloudEnabled", "cloudAvailable", "cloudLow", "cloudHigh");
        newConfig.features[3] = readFeature(state, tableIndex, FeatureId::FSR,
                                            "fsrEnabled", "fsrAvailable", "fsrLow", "fsrHigh");

        const std::string oldLanguage = language_;
        language_ = readStringField(state, tableIndex, "language", language_.empty() ? "auto" : language_);
        if (language_ != "auto" && language_ != "en" && language_ != "ja") language_ = "auto";

        displayMode_ = readStringField(state, tableIndex, "displayMode", displayMode_);
        showDetailsPreference_ = readBoolField(state, tableIndex, "showDetails", showDetailsPreference_);
        showGraph_ = readBoolField(state, tableIndex, "showGraph", showGraph_);
        showUtilisation_ = readBoolField(state, tableIndex, "showUtilisation", showUtilisation_);
        fpsMeterFrom_ = readNumberField(state, tableIndex, "fpsMeterFrom", fpsMeterFrom_);
        fpsMeterTo_ = readNumberField(state, tableIndex, "fpsMeterTo", fpsMeterTo_);
        fpsMeterBad_ = readNumberField(state, tableIndex, "fpsMeterBad", fpsMeterBad_);
        fpsMeterGood_ = readNumberField(state, tableIndex, "fpsMeterGood", fpsMeterGood_);
        normaliseFPSMeterScale();
        hudAlpha_ = readNumberField(state, tableIndex, "hudAlpha", hudAlpha_);
        hudX_ = static_cast<int>(std::lround(readNumberField(state, tableIndex, "hudX", hudX_)));
        hudY_ = static_cast<int>(std::lround(readNumberField(state, tableIndex, "hudY", hudY_)));
        hudWidth_ = static_cast<int>(std::lround(readNumberField(state, tableIndex, "hudWidth", hudWidth_)));
        hudLineHeight_ = static_cast<int>(std::lround(readNumberField(state, tableIndex, "hudLineHeight", hudLineHeight_)));
        aglEnabled_ = readBoolField(state, tableIndex, "aglEnabled", aglEnabled_);
        aglHeight_ = readNumberField(state, tableIndex, "aglHeight", aglHeight_);
        shadowKillEnabled_ = readBoolField(state, tableIndex, "shadowKillEnabled", shadowKillEnabled_);
        shadowKillInteriorDegrees_ = readNumberField(state, tableIndex, "shadowKillInteriorDegrees", shadowKillInteriorDegrees_);
        shadowKillExternalDegrees_ = readNumberField(state, tableIndex, "shadowKillExternalDegrees", shadowKillExternalDegrees_);
        if (firstRegistration || !hudPositionIsAbsolute_) {
            hudPositionIsAbsolute_ = readBoolField(state, tableIndex, "hudPositionAbsolute", false);
        }

        if (firstRegistration) {
            bindDataRefs();
            swapTimeInitialized_ = false;
        } else {
            bindDataRefs();
        }

        newConfig.cpuTimeAvailable = requestedCPUTime && swapTimeTotal_.ref != nullptr;
        newConfig.gpuTimeAvailable = requestedGPUTime && gpuTime_.ref != nullptr;
        newConfig.features[0].available = newConfig.features[0].available && lodBias_.ref != nullptr;
        newConfig.features[1].available = newConfig.features[1].available &&
            shadowInterior_.ref != nullptr && shadowExterior_.ref != nullptr &&
            shadowBillboards_.ref != nullptr;
        newConfig.features[2].available = newConfig.features[2].available &&
            cloudSegmentSteps_.ref != nullptr && cloudStepStart_.ref != nullptr;
        newConfig.features[3].available = newConfig.features[3].available &&
            fsrEnabled_.ref != nullptr && fsrQuality_.ref != nullptr;

        if (oldActive) {
            restoreFeaturesNoLongerControlled(oldConfig, newConfig);
        }

        for (size_t index = 0; index < threejfps::kFeatureCount; ++index) {
            if (oldActive && !profileChanged) {
                newConfig.features[index].manualOverride = oldConfig.features[index].manualOverride;
                newConfig.features[index].manualValue = oldConfig.features[index].manualValue;
            }
        }

        config_ = newConfig;
        profile_ = newProfile;
        invalidateHUDLayout();
        if (firstRegistration || !oldActive || profileChanged) {
            controller_.reset(config_);
            clearAppliedCache();
        } else {
            controller_.reconfigure(config_, true);
        }
        if (oldLanguage != language_) snapshotDirty_ = true;
        if (config_.mode == Mode::Off) {
            restoreAllDataRefs();
        }
    }

    void bindDataRefs() {
        findDataRef(gpuTime_, "sim/time/gpu_time_per_frame_sec_approx");
        findDataRef(swapTimeTotal_, "sim/private/stats/ogl/swap_time_total");
        findDataRef(lodBias_, "sim/private/controls/reno/LOD_bias_rat");
        findDataRef(cloudSegmentSteps_, "sim/private/controls/new_clouds/march/seg_steps");
        findDataRef(cloudStepStart_, "sim/private/controls/new_clouds/march/step_len_start");
        findDataRef(shadowInterior_, "sim/private/controls/shadow/csm/far_limit_interior");
        findDataRef(shadowExterior_, "sim/private/controls/shadow/csm/far_limit_exterior");
        findDataRef(shadowBillboards_, "sim/private/controls/vegetation/billboard_shadows");
        findDataRef(shadowPreparationDisabled_, "sim/private/controls/perf/disable_shadow_prep");
        findDataRef(sunPitch_, "sim/graphics/scenery/sun_pitch_degrees");
        findDataRef(viewExternal_, "sim/graphics/view/view_is_external");
        findDataRef(aircraftY_, "sim/flightmodel/position/local_y");
        findDataRef(aircraftAGL_, "sim/flightmodel/position/y_agl");
        findDataRef(viewY_, "sim/graphics/view/view_y");
        findDataRef(fsrEnabled_, "sim/private/controls/fsr/enable");
        findDataRef(fsrQuality_, "sim/private/controls/fsr/quality");
    }

    static void findDataRef(NumericDataRef& target, const char* name) {
        if (target.ref == nullptr) {
            target.ref = XPLMFindDataRef(name);
        }
        // X-Plane is the source of truth for restoration. Lua may still hold
        // fallback values when a private DataRef was unavailable at startup.
        if (target.ref != nullptr && !target.originalCaptured) {
            target.originalValue = readDataRef(target);
            target.originalCaptured = true;
        }
    }

    void clearAppliedCache() {
        appliedValues_.clear();
    }

    void writeCached(NumericDataRef& dataRef, double value) {
        if (dataRef.ref == nullptr || !std::isfinite(value)) return;
        const auto found = appliedValues_.find(dataRef.ref);
        if (found != appliedValues_.end() && std::abs(found->second - value) < 0.0005) return;
        if (writeDataRef(dataRef, value)) {
            appliedValues_[dataRef.ref] = value;
        }
    }

    void restoreDataRef(NumericDataRef& dataRef) {
        if (!dataRef.originalCaptured) return;
        writeDataRef(dataRef, dataRef.originalValue);
    }

    void restoreAllDataRefs() {
        restoreDataRef(lodBias_);
        restoreDataRef(cloudSegmentSteps_);
        restoreDataRef(cloudStepStart_);
        restoreDataRef(shadowInterior_);
        restoreDataRef(shadowExterior_);
        restoreDataRef(shadowBillboards_);
        restoreDataRef(shadowPreparationDisabled_);
        restoreDataRef(fsrEnabled_);
        restoreDataRef(fsrQuality_);
        clearAppliedCache();
    }

    void resetOriginalDataRefCapture() {
        gpuTime_.originalValue = 0.0;
        gpuTime_.originalCaptured = false;
        swapTimeTotal_.originalValue = 0.0;
        swapTimeTotal_.originalCaptured = false;
        lodBias_.originalValue = 0.0;
        lodBias_.originalCaptured = false;
        cloudSegmentSteps_.originalValue = 0.0;
        cloudSegmentSteps_.originalCaptured = false;
        cloudStepStart_.originalValue = 0.0;
        cloudStepStart_.originalCaptured = false;
        shadowInterior_.originalValue = 0.0;
        shadowInterior_.originalCaptured = false;
        shadowExterior_.originalValue = 0.0;
        shadowExterior_.originalCaptured = false;
        shadowBillboards_.originalValue = 0.0;
        shadowBillboards_.originalCaptured = false;
        shadowPreparationDisabled_.originalValue = 0.0;
        shadowPreparationDisabled_.originalCaptured = false;
        sunPitch_.originalValue = 0.0;
        sunPitch_.originalCaptured = false;
        viewExternal_.originalValue = 0.0;
        viewExternal_.originalCaptured = false;
        aircraftY_.originalValue = 0.0;
        aircraftY_.originalCaptured = false;
        aircraftAGL_.originalValue = 0.0;
        aircraftAGL_.originalCaptured = false;
        viewY_.originalValue = 0.0;
        viewY_.originalCaptured = false;
        fsrEnabled_.originalValue = 0.0;
        fsrEnabled_.originalCaptured = false;
        fsrQuality_.originalValue = 0.0;
        fsrQuality_.originalCaptured = false;
    }

    void restoreFeaturesNoLongerControlled(const ControllerConfig& oldConfig,
                                           const ControllerConfig& newConfig) {
        const auto wasControlled = [&oldConfig](FeatureId id) {
            const FeatureConfig& feature = oldConfig.features[static_cast<size_t>(id)];
            return feature.enabled && feature.available;
        };
        const auto isControlled = [&newConfig](FeatureId id) {
            const FeatureConfig& feature = newConfig.features[static_cast<size_t>(id)];
            return feature.enabled && feature.available;
        };

        if (wasControlled(FeatureId::LOD) && !isControlled(FeatureId::LOD)) {
            restoreDataRef(lodBias_);
        }
        if (wasControlled(FeatureId::Shadows) && !isControlled(FeatureId::Shadows)) {
            restoreDataRef(shadowInterior_);
            restoreDataRef(shadowExterior_);
            restoreDataRef(shadowBillboards_);
        }
        if (wasControlled(FeatureId::Clouds) && !isControlled(FeatureId::Clouds)) {
            restoreDataRef(cloudSegmentSteps_);
            restoreDataRef(cloudStepStart_);
        }
        if (wasControlled(FeatureId::FSR) && !isControlled(FeatureId::FSR)) {
            restoreDataRef(fsrEnabled_);
            restoreDataRef(fsrQuality_);
        }
        clearAppliedCache();
    }

    double featureValue(FeatureId id) const {
        return controller_.state().features[static_cast<size_t>(id)].currentValue;
    }

    void resetHUDDisplay() {
        hudDisplayElapsed_ = 0.0;
        hudDisplayFrameCount_ = 0.0;
        hudDisplayCPUTotal_ = 0.0;
        hudDisplayGPUTotal_ = 0.0;
        hudFPS_ = 0.0;
        hudCPUMilliseconds_ = 0.0;
        hudGPUMilliseconds_ = 0.0;
        hudFeatureValues_.fill(0.0);
        graphFPSMin_ = 0.0;
        graphFPSMax_ = 0.0;
        graphCPUUsage_ = 0.0;
        graphGPUUsage_ = 0.0;
        graphInitialized_ = false;
        stutterHoldSeconds_ = 0.0;
        hudDisplayInitialized_ = false;
        invalidateHUDLayout();
    }

    void updateHUDDisplay(double deltaSeconds) {
        const auto& state = controller_.state();
        stutterHoldSeconds_ = std::max(0.0, stutterHoldSeconds_ - deltaSeconds);
        const double targetFrameSeconds = 1.0 / std::max(1.0, state.targetFPS);
        if (deltaSeconds > std::max(0.05, targetFrameSeconds * 1.5) &&
            state.fps < state.targetFPS * 0.9) {
            stutterHoldSeconds_ = kHUDStutterHoldSeconds;
        }

        if (showGraph_) {
            if (!graphInitialized_) {
                const double initialFPS = std::max(0.0, state.fps);
                graphFPSMin_ = initialFPS;
                graphFPSMax_ = initialFPS;
                graphInitialized_ = true;
            } else {
                updateGraphEnvelope(std::max(0.0, state.fps), deltaSeconds);
            }
        } else {
            graphInitialized_ = false;
        }

        if (!hudDisplayInitialized_) {
            hudFPS_ = state.fps;
            hudCPUMilliseconds_ = state.cpuMilliseconds;
            hudGPUMilliseconds_ = state.gpuMilliseconds;
            const double frameBudgetMilliseconds = 1000.0 / std::max(1.0, state.targetFPS);
            graphCPUUsage_ = state.cpuTimeAvailable
                ? std::max(0.0, std::min(2.0, state.cpuMilliseconds / frameBudgetMilliseconds))
                : 0.0;
            graphGPUUsage_ = state.gpuTimeAvailable
                ? std::max(0.0, std::min(2.0, state.gpuMilliseconds / frameBudgetMilliseconds))
                : 0.0;
            for (size_t index = 0; index < threejfps::kFeatureCount; ++index) {
                hudFeatureValues_[index] = state.features[index].currentValue;
            }
            hudDisplayInitialized_ = true;
        }

        hudDisplayElapsed_ += deltaSeconds;
        hudDisplayFrameCount_ += 1.0;
        hudDisplayCPUTotal_ += state.cpuMilliseconds;
        hudDisplayGPUTotal_ += state.gpuMilliseconds;
        if (hudDisplayElapsed_ < kHUDDisplayIntervalSeconds) {
            return;
        }

        const double elapsed = std::max(kMinimumDeltaSeconds, hudDisplayElapsed_);
        const double frameCount = std::max(1.0, hudDisplayFrameCount_);
        hudFPS_ = frameCount / elapsed;
        hudCPUMilliseconds_ = hudDisplayCPUTotal_ / frameCount;
        hudGPUMilliseconds_ = hudDisplayGPUTotal_ / frameCount;
        const double frameBudgetMilliseconds = 1000.0 / std::max(1.0, state.targetFPS);
        graphCPUUsage_ = state.cpuTimeAvailable
            ? smoothGraphUsage(graphCPUUsage_, hudCPUMilliseconds_ / frameBudgetMilliseconds)
            : 0.0;
        graphGPUUsage_ = state.gpuTimeAvailable
            ? smoothGraphUsage(graphGPUUsage_, hudGPUMilliseconds_ / frameBudgetMilliseconds)
            : 0.0;
        for (size_t index = 0; index < threejfps::kFeatureCount; ++index) {
            hudFeatureValues_[index] = state.features[index].currentValue;
        }
        invalidateHUDLayout();

        hudDisplayElapsed_ = 0.0;
        hudDisplayFrameCount_ = 0.0;
        hudDisplayCPUTotal_ = 0.0;
        hudDisplayGPUTotal_ = 0.0;
    }

    double featureQuality(FeatureId id) const {
        const auto& config = controller_.config().features[static_cast<size_t>(id)];
        const auto& state = controller_.state();
        const auto& featureState = state.features[static_cast<size_t>(id)];
        if (featureState.manualOverride || config.highValue == config.lowValue) {
            return 0.5;
        }
        const double quality = (featureState.currentValue - config.lowValue) /
                               (config.highValue - config.lowValue);
        return std::max(0.0, std::min(1.0, quality));
    }

    void applyControllerOutputs() {
        if (config_.mode == Mode::Off) return;
        if (screenshotOverride_) return;

        const auto& state = controller_.state();
        if (state.mode == Mode::MaxFPS) {
            applyAllQuality(0.0);
            return;
        }
        if (state.mode == Mode::MaxQuality) {
            applyAllQuality(1.0);
            return;
        }

        if (featureCanControl(FeatureId::LOD)) {
            const double lodValue = featureValue(FeatureId::LOD);
            writeCached(lodBias_, lodValue + 1.0 - aglVisibilityFactor());
        }

        if (featureCanControl(FeatureId::Shadows)) {
            const double shadowValue = featureValue(FeatureId::Shadows);
            writeCached(shadowInterior_, shadowValue);
            writeCached(shadowExterior_, shadowValue);
            writeCached(shadowBillboards_, 1.0);
        }

        if (featureCanControl(FeatureId::Clouds)) {
            const double cloudQuality = std::max(0.1, featureValue(FeatureId::Clouds));
            writeCached(cloudSegmentSteps_, cloudSegmentSteps_.originalValue);
            writeCached(cloudStepStart_, cloudStepStart_.originalValue / cloudQuality);
        }

        if (featureCanControl(FeatureId::FSR)) {
            const double fsrValue = featureValue(FeatureId::FSR);
            const int fsrMode = static_cast<int>(std::lround(fsrValue));
            if (fsrMode >= 4) {
                writeCached(fsrEnabled_, 0.0);
                writeCached(fsrQuality_, 3.0);
            } else {
                writeCached(fsrEnabled_, 1.0);
                writeCached(fsrQuality_, std::max(0, std::min(3, fsrMode)));
            }
        }
    }

    void applyAllQuality(double quality) {
        if (config_.mode == Mode::Off) return;
        const auto apply = [quality](const FeatureConfig& feature) {
            return feature.lowValue + (feature.highValue - feature.lowValue) * quality;
        };
        if (featureCanControl(FeatureId::LOD)) {
            writeCached(lodBias_, apply(controller_.config().features[0]) + 1.0 - aglVisibilityFactor());
        }
        if (featureCanControl(FeatureId::Shadows)) {
            writeCached(shadowInterior_, apply(controller_.config().features[1]));
            writeCached(shadowExterior_, apply(controller_.config().features[1]));
            writeCached(shadowBillboards_, 1.0);
        }
        if (featureCanControl(FeatureId::Clouds)) {
            const double cloudQuality = std::max(0.1, apply(controller_.config().features[2]));
            writeCached(cloudSegmentSteps_, cloudSegmentSteps_.originalValue);
            writeCached(cloudStepStart_, cloudStepStart_.originalValue / cloudQuality);
        }
        if (featureCanControl(FeatureId::FSR)) {
            const int fsrMode = static_cast<int>(std::lround(apply(controller_.config().features[3])));
            writeCached(fsrEnabled_, fsrMode >= 4 ? 0.0 : 1.0);
            writeCached(fsrQuality_, fsrMode >= 4 ? 3.0 : std::max(0, std::min(3, fsrMode)));
        }
    }

    bool featureCanControl(FeatureId id) const {
        const auto& feature = controller_.config().features[static_cast<size_t>(id)];
        return feature.enabled && feature.available;
    }

    double aglVisibilityFactor() const {
        if (!readBoolFromConfig("aglEnabled", true) || aircraftAGL_.ref == nullptr ||
            viewY_.ref == nullptr || aircraftY_.ref == nullptr) {
            return 1.0;
        }
        const double viewAGL = std::max(0.0, readDataRef(const_cast<NumericDataRef&>(aircraftAGL_)) +
                                             readDataRef(const_cast<NumericDataRef&>(viewY_)) -
                                             readDataRef(const_cast<NumericDataRef&>(aircraftY_)));
        const double height = std::max(1.0, aglHeight_);
        return viewAGL < height ? viewAGL / height : 1.0;
    }

    bool readBoolFromConfig(const char* name, bool fallback) const {
        if (std::string(name) == "aglEnabled") return aglEnabled_;
        return fallback;
    }

    void applyIndependentRules() {
        if (config_.mode == Mode::Off || shadowPreparationDisabled_.ref == nullptr) return;
        if (!shadowKillEnabled_ || sunPitch_.ref == nullptr || viewExternal_.ref == nullptr) {
            writeCached(shadowPreparationDisabled_, shadowPreparationDisabled_.originalValue);
            return;
        }
        const double external = readDataRef(const_cast<NumericDataRef&>(viewExternal_));
        const double sunPitch = readDataRef(const_cast<NumericDataRef&>(sunPitch_));
        const double threshold = shadowKillExternalDegrees_ * external +
                                 shadowKillInteriorDegrees_ * (1.0 - external);
        writeCached(shadowPreparationDisabled_, sunPitch < threshold ? 1.0 : 0.0);
    }

    void drainCommands() {
        std::deque<std::string> commands;
        {
            std::lock_guard<std::mutex> lock(commandMutex_);
            commands.swap(queuedCommands_);
        }
        for (const std::string& command : commands) {
            processCommand(command);
        }
    }

    void processCommand(const std::string& command) {
        const std::string type = jsonStringField(command, "type");
        if (type == "setMode") {
            callLuaAdapter("mode", jsonStringField(command, "value"));
            dirty_ = true;
            invalidateHUDLayout();
            return;
        }
        if (type == "setProfile") {
            callLuaAdapter("profile", jsonStringField(command, "value"));
            dirty_ = true;
            invalidateHUDLayout();
            return;
        }
        if (type == "setParameter") {
            callLuaParameter(jsonStringField(command, "key"), jsonRawField(command, "value"));
            dirty_ = true;
            invalidateHUDLayout();
            return;
        }
        if (type == "setGraph") {
            callLuaAdapter("graph", jsonBoolField(command, "value", showGraph_) ? "true" : "false");
            dirty_ = true;
            invalidateHUDLayout();
            snapshotDirty_ = true;
            return;
        }
        if (type == "save") {
            callLuaAdapter("save", std::string());
            dirty_ = false;
            snapshotDirty_ = true;
            return;
        }
        if (type == "cancel") {
            callLuaAdapter("cancel", std::string());
            dirty_ = false;
            snapshotDirty_ = true;
            return;
        }
        if (type == "defaults") {
            callLuaAdapter("defaults", std::string());
            dirty_ = true;
            invalidateHUDLayout();
            return;
        }
        if (type == "setLanguage") {
            callLuaAdapter("language", jsonStringField(command, "value"));
            dirty_ = true;
            invalidateHUDLayout();
            return;
        }
        if (type == "setHUDEditing") {
            hudEditing_ = jsonBoolField(command, "value", false);
            invalidateHUDLayout();
            snapshotDirty_ = true;
            return;
        }
        if (type == "setFeatureOverride") {
            const std::string id = jsonStringField(command, "id");
            const double value = jsonNumberField(command, "value", 0.0);
            const FeatureId feature = featureFromString(id);
            controller_.setManualOverride(feature, value);
            applyControllerOutputs();
            invalidateHUDLayout();
            snapshotDirty_ = true;
            return;
        }
        if (type == "clearFeatureOverride") {
            controller_.clearManualOverrides();
            applyControllerOutputs();
            invalidateHUDLayout();
            snapshotDirty_ = true;
            return;
        }
        if (type == "requestSnapshot") {
            publishSnapshot();
        }
    }

    static FeatureId featureFromString(const std::string& value) {
        if (value == "shadows") return FeatureId::Shadows;
        if (value == "clouds") return FeatureId::Clouds;
        if (value == "fsr") return FeatureId::FSR;
        return FeatureId::LOD;
    }

    void callLuaParameter(const std::string& key, const std::string& rawValue) {
        if (key.empty() || luaState_ == nullptr) return;
        lua_getglobal(luaState_, "jjjFPS_nativeApplyCommand");
        if (!lua_isfunction(luaState_, -1)) {
            lua_pop(luaState_, 1);
            return;
        }
        lua_pushstring(luaState_, "param");
        lua_pushlstring(luaState_, key.data(), key.size());
        pushRawJSONValue(luaState_, rawValue);
        if (lua_pcall(luaState_, 3, 0, 0) != 0) logLuaError("parameter");
    }

    void callLuaAdapter(const std::string& kind, const std::string& value) {
        if (luaState_ == nullptr) return;
        lua_getglobal(luaState_, "jjjFPS_nativeApplyCommand");
        if (!lua_isfunction(luaState_, -1)) {
            lua_pop(luaState_, 1);
            return;
        }
        lua_pushlstring(luaState_, kind.data(), kind.size());
        if (value.empty()) {
            lua_pushnil(luaState_);
        } else {
            lua_pushlstring(luaState_, value.data(), value.size());
        }
        if (lua_pcall(luaState_, 2, 0, 0) != 0) logLuaError(kind.c_str());
    }

    void pushRawJSONValue(lua_State* state, const std::string& rawValue) {
        if (rawValue == "true" || rawValue == "false") {
            lua_pushboolean(state, rawValue == "true");
            return;
        }
        if (!rawValue.empty() && rawValue.front() == '"') {
            lua_pushlstring(state, rawValue.data(), rawValue.size());
            return;
        }
        char* end = nullptr;
        const double number = std::strtod(rawValue.c_str(), &end);
        if (end != rawValue.c_str() && end != nullptr && *end == '\0') {
            lua_pushnumber(state, number);
            return;
        }
        lua_pushlstring(state, rawValue.data(), rawValue.size());
    }

    void logLuaError(const char* context) {
        const char* error = lua_tostring(luaState_, -1);
        std::string message = "FlyWithLua-Mac 3jFPS12 command error (";
        message += context != nullptr ? context : "unknown";
        message += "): ";
        message += error != nullptr ? error : "unknown Lua error";
        message += "\n";
        XPLMDebugString(message.c_str());
        lua_pop(luaState_, 1);
    }

    void publishSnapshot() {
        if (!active_) return;
        snapshotTimer_ = 0.0;
        snapshotDirty_ = false;
        const auto& state = controller_.state();
        std::ostringstream json;
        json << "{\"schemaVersion\":1"
             << ",\"mode\":\"" << threejfps::modeName(state.mode) << "\""
             << ",\"profile\":\"" << jsonEscape(profile_) << "\""
             << ",\"fps\":" << numberString(state.fps)
             << ",\"targetFPS\":" << numberString(state.targetFPS)
             << ",\"cpuMs\":" << numberString(state.cpuMilliseconds)
             << ",\"gpuMs\":" << numberString(state.gpuMilliseconds)
             << ",\"cpuHeadroom\":" << numberString(config_.cpuHeadroomPercent)
             << ",\"gpuHeadroom\":" << numberString(config_.gpuHeadroomPercent)
             << ",\"detectionWindow\":" << numberString(config_.detectionWindowSeconds)
             << ",\"changeInterval\":" << numberString(config_.qualityChangeIntervalSeconds)
             << ",\"recoveryDelay\":" << numberString(config_.qualityRecoveryDelaySeconds)
             << ",\"limiter\":\"" << jsonEscape(state.limiter) << "\""
             << ",\"controllerReason\":\"" << jsonEscape(state.reason) << "\""
             << ",\"cpuDataRefAvailable\":" << (state.cpuTimeAvailable ? "true" : "false")
             << ",\"gpuDataRefAvailable\":" << (state.gpuTimeAvailable ? "true" : "false")
             << ",\"dirty\":" << (dirty_ ? "true" : "false")
             << ",\"language\":\"" << jsonEscape(language_) << "\""
             << ",\"resolvedLanguage\":\"" << resolvedLanguage() << "\""
             << ",\"hudEditing\":" << (hudEditing_ ? "true" : "false")
             << ",\"showGraph\":" << (showGraph_ ? "true" : "false")
             << ",\"showUtilisation\":" << (showUtilisation_ ? "true" : "false")
             << ",\"dataRefAvailability\":{"
             << "\"lod\":" << (lodBias_.ref != nullptr ? "true" : "false")
             << ",\"shadows\":" << (shadowInterior_.ref != nullptr && shadowExterior_.ref != nullptr &&
                                          shadowBillboards_.ref != nullptr ? "true" : "false")
             << ",\"clouds\":" << (cloudSegmentSteps_.ref != nullptr && cloudStepStart_.ref != nullptr ? "true" : "false")
             << ",\"fsr\":" << (fsrEnabled_.ref != nullptr && fsrQuality_.ref != nullptr ? "true" : "false")
             << "}"
             << ",\"features\":[";
        for (size_t index = 0; index < threejfps::kFeatureCount; ++index) {
            const auto& feature = state.features[index];
            if (index != 0) json << ',';
            json << "{\"id\":\"" << featureName(feature.id) << "\""
                 << ",\"enabled\":" << (feature.enabled ? "true" : "false")
                 << ",\"available\":" << (feature.available ? "true" : "false")
                 << ",\"current\":" << numberString(feature.currentValue)
                 << ",\"min\":" << numberString(feature.lowValue)
                 << ",\"max\":" << numberString(feature.highValue)
                 << ",\"manualOverride\":" << (feature.manualOverride ? "true" : "false")
                 << '}';
        }
        json << "]}";
        const std::string payload = json.str();
        flywithlua_update_3jfps_snapshot(payload.c_str());
    }

    const char* resolvedLanguage() const {
        if (language_ == "ja") return "ja";
        if (language_ == "en") return "en";
        return XPLMGetLanguage() == xplm_Language_Japanese ? "ja" : "en";
    }

    std::string label(const char* english, const char* japanese) const {
        return std::string(resolvedLanguage() == std::string("ja") ? japanese : english);
    }

    std::string modeLabel() const {
        switch (controller_.state().mode) {
            case Mode::Auto: return label("AUTO", "自動");
            case Mode::MaxFPS: return label("MAX FPS", "最大FPS");
            case Mode::MaxQuality: return label("MAX QUAL", "最大品質");
            case Mode::Off: return "OFF";
        }
        return "AUTO";
    }

    std::string profileLabel() const {
        return profile_.empty() ? "-" : profile_;
    }

    std::string hudModeLine() const {
        return modeLabel() + "  " + profileLabel();
    }

    std::string hudFPSLine() const {
        return label("FPS", "FPS") + " " + hudNumberString(hudFPS_, 1, false) +
               "  " + label("Target", "目標") + " " +
               hudNumberString(controller_.state().targetFPS, 0, true);
    }

    std::string hudMetricsLine() const {
        const auto& state = controller_.state();
        const std::string cpu = state.cpuTimeAvailable
            ? hudNumberString(hudCPUMilliseconds_, 1, false)
            : "--";
        const std::string gpu = state.gpuTimeAvailable
            ? hudNumberString(hudGPUMilliseconds_, 1, false)
            : "--";
        return label("CPU", "CPU") + " " + cpu +
               (state.cpuTimeAvailable ? "ms  " : "    ") +
               label("GPU", "GPU") + " " + gpu +
               (state.gpuTimeAvailable ? "ms" : "");
    }

    std::string hudQualityLine() const {
        return label("Quality", "品質") + " " + qualitySummary();
    }

    double measureHUDText(const std::string& text,
                          const char* family,
                          int weight) const {
        const double measured = flywithlua_measure_hidpi_text(
            text.c_str(),
            static_cast<float>(std::max(12, hudLineHeight_)),
            family,
            weight);
        if (std::isfinite(measured) && measured >= 0.0) {
            return measured;
        }

        const XPLMFontID font = std::string(family) == "sf_mono"
            ? xplmFont_Basic
            : xplmFont_Proportional;
        const size_t maximumLength = static_cast<size_t>(std::numeric_limits<int>::max());
        const int length = static_cast<int>(std::min(text.size(), maximumLength));
        return static_cast<double>(XPLMMeasureString(font, text.c_str(), length));
    }

    int hudGraphWidth() const {
        const int derivedWidth = static_cast<int>(std::lround(
            static_cast<double>(std::max(12, hudLineHeight_)) * 9.0));
        return std::max(kHUDGraphMinimumWidth,
                        std::min(kHUDGraphMaximumWidth, derivedWidth));
    }

    int hudBoxHeight(bool showDetails) const {
        const int lineHeight = std::max(12, hudLineHeight_);
        const int detailLines = showDetails ? 4 : 2;
        const int graphLines = showGraph_ ? graphRowCount() : 0;
        return std::max(2, std::max(detailLines, graphLines) * lineHeight + 8);
    }

    bool qualityMarkerVisible() const {
        if (controller_.state().mode == Mode::Off) return false;
        for (const auto& feature : controller_.state().features) {
            if (feature.enabled && feature.available) return true;
        }
        return false;
    }

    int graphRowCount() const {
        int rows = 1; // FPS meter
        if (showUtilisation_ && controller_.state().cpuTimeAvailable) ++rows;
        if (showUtilisation_ && controller_.state().gpuTimeAvailable) ++rows;
        if (qualityMarkerVisible()) ++rows;
        return rows;
    }

    void measureHUDTextIfNeeded(bool showDetails) const {
        bool& valid = showDetails ? hudDetailsWidthValid_ : hudCompactWidthValid_;
        int& measuredWidth = showDetails ? hudMeasuredDetailsWidth_ : hudMeasuredCompactWidth_;
        if (valid) return;

        double widestLine = measureHUDText(hudModeLine(), "sf_pro_text", 600);
        widestLine = std::max(widestLine, measureHUDText(hudFPSLine(), "sf_mono", 600));
        if (showDetails) {
            widestLine = std::max(widestLine, measureHUDText(hudMetricsLine(), "sf_mono", 400));
            widestLine = std::max(widestLine, measureHUDText(hudQualityLine(), "sf_mono", 400));
        }
        if (hudEditing_) {
            widestLine = std::max(
                widestLine,
                measureHUDText(label("EDIT HUD", "HUD編集"), "sf_pro_text", 600));
        }
        measuredWidth = static_cast<int>(std::ceil(widestLine)) + kHUDHorizontalPadding;
        valid = true;
    }

    int hudTextColumnWidth(bool showDetails) const {
        measureHUDTextIfNeeded(showDetails);
        const int measuredWidth = showDetails ? hudMeasuredDetailsWidth_ : hudMeasuredCompactWidth_;
        return std::max(0, measuredWidth - kHUDHorizontalPadding);
    }

    int hudBoxWidth(bool showDetails) const {
        measureHUDTextIfNeeded(showDetails);
        const int measuredWidth = showDetails ? hudMeasuredDetailsWidth_ : hudMeasuredCompactWidth_;
        int contentWidth = measuredWidth;
        if (showGraph_) {
            contentWidth = std::max(
                contentWidth,
                hudTextColumnWidth(showDetails) + kHUDGraphGap + hudGraphWidth() +
                    kHUDHorizontalPadding);
        }
        return std::max(kHUDMinimumWidth, std::max(hudWidth_, contentWidth));
    }

    void normaliseFPSMeterScale() {
        fpsMeterFrom_ = std::max(1.0, std::min(240.0, fpsMeterFrom_));
        fpsMeterTo_ = std::max(fpsMeterFrom_ + 1.0, std::min(300.0, fpsMeterTo_));
        fpsMeterBad_ = std::max(fpsMeterFrom_, std::min(fpsMeterTo_, fpsMeterBad_));
        fpsMeterGood_ = std::max(fpsMeterBad_, std::min(fpsMeterTo_, fpsMeterGood_));
    }

    void updateGraphEnvelope(double fps, double deltaSeconds) {
        const double heldFPS = hudFPS_ > 0.0 ? hudFPS_ : fps;
        graphFPSMin_ += kHUDGraphEnvelopeSpeedFPS * deltaSeconds;
        if (fps < graphFPSMin_) {
            graphFPSMin_ = fps;
        } else if (graphFPSMin_ > heldFPS) {
            graphFPSMin_ = heldFPS;
        }
        graphFPSMin_ = std::max(5.0, graphFPSMin_);

        graphFPSMax_ -= kHUDGraphEnvelopeSpeedFPS * deltaSeconds;
        if (fps > graphFPSMax_) {
            graphFPSMax_ = fps;
        } else if (graphFPSMax_ < heldFPS) {
            graphFPSMax_ = heldFPS;
        }
        graphFPSMax_ = std::min(fpsMeterTo_ + 20.0, graphFPSMax_);
        if (graphFPSMax_ < graphFPSMin_) graphFPSMax_ = graphFPSMin_;
    }

    static double smoothGraphUsage(double previous, double current) {
        current = std::max(0.0, std::min(2.0, current));
        if (previous <= 0.0) return current;
        return previous * 0.7 + current * 0.3;
    }

    static void drawGraphRect(float x,
                              float y,
                              float width,
                              float height,
                              float red,
                              float green,
                              float blue,
                              float alpha) {
        if (width <= 0.0f || height <= 0.0f) return;
        glColor4f(red, green, blue, alpha);
        glBegin(GL_QUADS);
        glVertex2f(x, y);
        glVertex2f(x + width, y);
        glVertex2f(x + width, y + height);
        glVertex2f(x, y + height);
        glEnd();
    }

    void drawUsageGraph(int x,
                        int rowBottom,
                        int width,
                        int lineHeight,
                        double usage,
                        double headroom,
                        float alpha) const {
        const int barY = rowBottom + 2;
        const int barHeight = std::max(4, lineHeight - 5);
        drawGraphRect(static_cast<float>(x), static_cast<float>(barY),
                      static_cast<float>(width), static_cast<float>(barHeight),
                      0.08f, 0.08f, 0.08f, alpha * 0.8f);

        const double clampedUsage = std::max(0.0, std::min(1.0, usage));
        const double threshold = std::max(0.0, std::min(1.0, 1.0 - headroom / 100.0));
        const float fillRed = usage > 1.0 ? 0.9f : (usage > threshold ? 0.95f : 0.8f);
        const float fillGreen = usage > 1.0 ? 0.2f : (usage > threshold ? 0.7f : 0.85f);
        const float fillBlue = usage > 1.0 ? 0.15f : 0.25f;
        drawGraphRect(static_cast<float>(x), static_cast<float>(barY),
                      static_cast<float>(width * clampedUsage), static_cast<float>(barHeight),
                      fillRed, fillGreen, fillBlue, alpha);

        const float thresholdX = static_cast<float>(x) +
            static_cast<float>(width * threshold);
        drawGraphRect(thresholdX, static_cast<float>(barY),
                      std::max(1.0f, static_cast<float>(std::max(1, lineHeight / 8))),
                      static_cast<float>(barHeight), 1.0f, 1.0f, 1.0f, alpha * 0.45f);
    }

    void drawFPSGraph(int x,
                      int rowBottom,
                      int width,
                      int lineHeight,
                      float alpha) const {
        const int barY = rowBottom + 2;
        const int barHeight = std::max(4, lineHeight - 5);
        const double span = std::max(1.0, fpsMeterTo_ - fpsMeterFrom_);
        const auto position = [this, x, width, span](double value) {
            const double normalized = std::max(0.0, std::min(1.0,
                (value - fpsMeterFrom_) / span));
            return static_cast<float>(x + normalized * width);
        };

        const float badX = position(fpsMeterBad_);
        const float goodX = position(fpsMeterGood_);
        const float rightX = static_cast<float>(x + width);
        drawGraphRect(static_cast<float>(x), static_cast<float>(barY),
                      std::max(0.0f, badX - x), static_cast<float>(barHeight),
                      0.72f, 0.05f, 0.05f, alpha);
        drawGraphRect(badX, static_cast<float>(barY),
                      std::max(0.0f, goodX - badX), static_cast<float>(barHeight),
                      0.82f, 0.68f, 0.02f, alpha);
        drawGraphRect(goodX, static_cast<float>(barY),
                      std::max(0.0f, rightX - goodX), static_cast<float>(barHeight),
                      0.05f, 0.62f, 0.08f, alpha);

        const float minX = position(graphFPSMin_);
        const float maxX = position(graphFPSMax_);
        const float rangeLeft = std::min(minX, maxX);
        const float rangeWidth = std::max(1.0f, std::abs(maxX - minX));
        drawGraphRect(rangeLeft, static_cast<float>(barY + barHeight / 2),
                      rangeWidth, std::max(1.0f, static_cast<float>(lineHeight / 8)),
                      1.0f, 1.0f, 1.0f, alpha * 0.75f);

        const float fpsX = position(hudFPS_);
        const float markerWidth = std::max(2.0f, static_cast<float>(lineHeight / 8));
        drawGraphRect(fpsX - markerWidth * 0.5f, static_cast<float>(rowBottom),
                      markerWidth, static_cast<float>(lineHeight - 1),
                      1.0f, 1.0f, 1.0f, alpha);

        if (stutterHoldSeconds_ > 0.0) {
            drawGraphRect(static_cast<float>(x), static_cast<float>(barY),
                          static_cast<float>(width), std::max(1.0f, markerWidth * 0.65f),
                          0.95f, 0.08f, 0.05f, alpha);
            drawGraphRect(static_cast<float>(x),
                          static_cast<float>(barY + barHeight) - std::max(1.0f, markerWidth * 0.65f),
                          static_cast<float>(width), std::max(1.0f, markerWidth * 0.65f),
                          0.95f, 0.08f, 0.05f, alpha);
        }
    }

    double aggregateQuality() const {
        double quality = 1.0;
        bool hasQuality = false;
        for (const auto& feature : controller_.state().features) {
            if (!feature.enabled || !feature.available) continue;
            quality = std::min(quality, featureQuality(feature.id));
            hasQuality = true;
        }
        return hasQuality ? quality : 0.5;
    }

    void drawQualityGraph(int x,
                          int rowBottom,
                          int width,
                          int lineHeight,
                          float alpha) const {
        const int barY = rowBottom + 2;
        const int barHeight = std::max(4, lineHeight - 5);
        drawGraphRect(static_cast<float>(x), static_cast<float>(barY),
                      static_cast<float>(width), static_cast<float>(barHeight),
                      0.10f, 0.10f, 0.16f, alpha * 0.9f);

        const float markerWidth = std::max(2.0f, static_cast<float>(lineHeight / 8));
        const float markerX = static_cast<float>(x) +
            static_cast<float>(aggregateQuality() * width);
        const float red = controller_.state().mode == Mode::MaxFPS ? 0.95f : 1.0f;
        const float green = controller_.state().mode == Mode::MaxFPS ? 0.25f : 1.0f;
        drawGraphRect(markerX - markerWidth * 0.5f, static_cast<float>(rowBottom),
                      markerWidth, static_cast<float>(lineHeight - 1),
                      red, green, 0.95f, alpha);
    }

    void drawGraph(int x,
                   int topRowBottom,
                   int width,
                   int lineHeight,
                   float alpha) const {
        int rowBottom = topRowBottom;
        const auto& state = controller_.state();
        if (showUtilisation_ && state.cpuTimeAvailable) {
            drawUsageGraph(x, rowBottom, width, lineHeight,
                           graphCPUUsage_, config_.cpuHeadroomPercent, alpha);
            rowBottom -= lineHeight;
        }
        if (showUtilisation_ && state.gpuTimeAvailable) {
            drawUsageGraph(x, rowBottom, width, lineHeight,
                           graphGPUUsage_, config_.gpuHeadroomPercent, alpha);
            rowBottom -= lineHeight;
        }
        drawFPSGraph(x, rowBottom, width, lineHeight, alpha);
        rowBottom -= lineHeight;
        if (qualityMarkerVisible()) {
            drawQualityGraph(x, rowBottom, width, lineHeight, alpha);
        }
    }

    void invalidateHUDLayout() {
        hudCompactWidthValid_ = false;
        hudDetailsWidthValid_ = false;
    }

    std::string qualitySummary() const {
        const auto& state = controller_.state();
        std::ostringstream result;
        result << label("LOD", "LOD") << ' '
               << hudNumberString(hudFeatureValues_[static_cast<size_t>(FeatureId::LOD)], 1, true)
               << "  " << label("SHD", "影") << ' '
               << hudNumberString(hudFeatureValues_[static_cast<size_t>(FeatureId::Shadows)], 0, true);
        if (state.features[static_cast<size_t>(FeatureId::Clouds)].enabled) {
            result << "  " << label("CLD", "雲") << ' '
                   << hudNumberString(hudFeatureValues_[static_cast<size_t>(FeatureId::Clouds)], 1, true);
        }
        if (state.features[static_cast<size_t>(FeatureId::FSR)].enabled) {
            result << "  FSR "
                   << hudNumberString(hudFeatureValues_[static_cast<size_t>(FeatureId::FSR)], 0, true);
        }
        return result.str();
    }

    void drawText(const float* color,
                  int x,
                  int y,
                  const std::string& text,
                  float alpha,
                  const char* family,
                  int weight) const {
        std::string mutableText = text;
        float rgb[3] = {color[0], color[1], color[2]};
        const float drawAlpha = std::max(0.0f, std::min(1.0f, alpha));
        const float logicalSize = static_cast<float>(std::max(12, hudLineHeight_));
        glColor4f(rgb[0], rgb[1], rgb[2], drawAlpha);
        if (flywithlua_draw_hidpi_text(x, y, text.c_str(), logicalSize, family, weight) != 0) {
            return;
        }
        XPLMDrawString(rgb, x, y, mutableText.data(), nullptr, xplmFont_Basic);
    }

    int resolveHUDX(int screenWidth, int boxWidth) const {
        if (hudPositionIsAbsolute_ || hudX_ >= 0) return std::max(4, hudX_);
        return std::max(4, screenWidth + hudX_ - boxWidth);
    }

    int resolveHUDY(int screenHeight, int boxHeight) const {
        if (hudPositionIsAbsolute_ || hudY_ >= 0) return std::max(4, hudY_);
        return std::max(4, screenHeight + hudY_ - boxHeight);
    }

    bool isInsideHUD(int localX, int localY, int screenWidth, int screenHeight) const {
        const bool showDetails = hudEditing_ || showDetailsPreference_;
        const int boxHeight = hudBoxHeight(showDetails);
        return isInsideHUD(localX, localY, screenWidth, screenHeight,
                           hudBoxWidth(showDetails), boxHeight);
    }

    bool isInsideHUD(int localX, int localY, int screenWidth, int screenHeight,
                     int boxWidth, int boxHeight) const {
        const int x = resolveHUDX(screenWidth, boxWidth);
        const int y = resolveHUDY(screenHeight, boxHeight);
        return localX >= x - 5 && localX <= x + boxWidth + 5 &&
               localY >= y - 5 && localY <= y + boxHeight + 5;
    }

    Controller controller_;
    ControllerConfig config_;
    lua_State* luaState_ = nullptr;
    bool active_ = false;
    bool dirty_ = false;
    bool snapshotDirty_ = true;
    double snapshotTimer_ = 0.0;
    std::string profile_ = "-";
    std::string language_ = "auto";
    std::string displayMode_ = "alw";
    bool showDetailsPreference_ = false;
    bool showGraph_ = true;
    bool showUtilisation_ = true;
    double fpsMeterFrom_ = 15.0;
    double fpsMeterTo_ = 40.0;
    double fpsMeterBad_ = 20.0;
    double fpsMeterGood_ = 30.0;
    double hudAlpha_ = 0.8;
    int hudX_ = 8;
    int hudY_ = -24;
    int hudWidth_ = 240;
    int hudLineHeight_ = 16;
    bool hudPositionIsAbsolute_ = false;
    bool hudEditing_ = false;
    bool hudDisplayInitialized_ = false;
    double hudDisplayElapsed_ = 0.0;
    double hudDisplayFrameCount_ = 0.0;
    double hudDisplayCPUTotal_ = 0.0;
    double hudDisplayGPUTotal_ = 0.0;
    double hudFPS_ = 0.0;
    double hudCPUMilliseconds_ = 0.0;
    double hudGPUMilliseconds_ = 0.0;
    std::array<double, threejfps::kFeatureCount> hudFeatureValues_{};
    double graphFPSMin_ = 0.0;
    double graphFPSMax_ = 0.0;
    double graphCPUUsage_ = 0.0;
    double graphGPUUsage_ = 0.0;
    bool graphInitialized_ = false;
    double stutterHoldSeconds_ = 0.0;
    mutable bool hudCompactWidthValid_ = false;
    mutable bool hudDetailsWidthValid_ = false;
    mutable int hudMeasuredCompactWidth_ = 0;
    mutable int hudMeasuredDetailsWidth_ = 0;
    bool dragging_ = false;
    bool inputCaptured_ = false;
    int dragOffsetX_ = 0;
    int dragOffsetY_ = 0;
    bool aglEnabled_ = true;
    double aglHeight_ = 150.0;
    bool shadowKillEnabled_ = true;
    double shadowKillInteriorDegrees_ = -1.0;
    double shadowKillExternalDegrees_ = 1.6;
    bool screenshotOverride_ = false;
    int screenshotFrames_ = 0;
    bool swapTimeInitialized_ = false;
    double previousSwapTime_ = 0.0;
    NumericDataRef gpuTime_;
    NumericDataRef swapTimeTotal_;
    NumericDataRef lodBias_;
    NumericDataRef cloudSegmentSteps_;
    NumericDataRef cloudStepStart_;
    NumericDataRef shadowInterior_;
    NumericDataRef shadowExterior_;
    NumericDataRef shadowBillboards_;
    NumericDataRef shadowPreparationDisabled_;
    NumericDataRef sunPitch_;
    NumericDataRef viewExternal_;
    NumericDataRef aircraftY_;
    NumericDataRef aircraftAGL_;
    NumericDataRef viewY_;
    NumericDataRef fsrEnabled_;
    NumericDataRef fsrQuality_;
    std::unordered_map<XPLMDataRef, double> appliedValues_;
    std::mutex commandMutex_;
    std::deque<std::string> queuedCommands_;
};

} // namespace

extern "C" void threejfps_register_lua_functions(lua_State* state) {
    if (state != nullptr) Runtime::instance().registerLuaFunctions(state);
}

extern "C" void threejfps_step(float deltaSeconds) {
    Runtime::instance().step(deltaSeconds);
}

extern "C" void threejfps_draw_hud(void) {
    Runtime::instance().drawHUD();
}

extern "C" int threejfps_handle_click(int x, int y, int mouseStatus) {
    return Runtime::instance().handleClick(x, y, mouseStatus);
}

extern "C" int threejfps_handle_wheel(int x, int y, int wheel, int clicks) {
    return Runtime::instance().handleWheel(x, y, wheel, clicks);
}

extern "C" void threejfps_on_lua_reset(void) {
    Runtime::instance().onLuaReset();
}

extern "C" void threejfps_shutdown(void) {
    Runtime::instance().shutdown();
}

extern "C" int threejfps_is_active(void) {
    return Runtime::instance().active() ? 1 : 0;
}

extern "C" void threejfps_enqueue_command(const char* jsonCommand) {
    Runtime::instance().enqueueCommand(jsonCommand);
}
