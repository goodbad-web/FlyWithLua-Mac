#ifndef THREE_JFPS_CONTROLLER_CORE_H
#define THREE_JFPS_CONTROLLER_CORE_H

#include <array>
#include <string>

namespace threejfps {

enum class Mode {
    Auto,
    MaxFPS,
    MaxQuality,
    Off,
};

enum class FeatureId {
    LOD = 0,
    Shadows = 1,
    Clouds = 2,
    FSR = 3,
};

constexpr size_t kFeatureCount = 4;

struct FeatureConfig {
    FeatureId id = FeatureId::LOD;
    bool enabled = false;
    bool available = false;
    double lowValue = 0.0;
    double highValue = 1.0;
    bool manualOverride = false;
    double manualValue = 0.0;
};

struct ControllerConfig {
    Mode mode = Mode::Auto;
    double targetFPS = 30.0;
    double cpuHeadroomPercent = 20.0;
    double gpuHeadroomPercent = 10.0;
    double detectionWindowSeconds = 0.5;
    double qualityChangeIntervalSeconds = 2.0;
    double qualityRecoveryDelaySeconds = 30.0;
    double degradationStep = 0.20;
    double recoveryStep = 0.05;
    bool cpuTimeAvailable = false;
    bool gpuTimeAvailable = false;
    std::array<FeatureConfig, kFeatureCount> features{};
};

struct ControllerSample {
    double deltaSeconds = 0.0;
    double fps = 0.0;
    double cpuMilliseconds = 0.0;
    double gpuMilliseconds = 0.0;
    bool cpuTimeAvailable = false;
    bool gpuTimeAvailable = false;
};

struct FeatureState {
    FeatureId id = FeatureId::LOD;
    bool enabled = false;
    bool available = false;
    double currentValue = 0.0;
    double lowValue = 0.0;
    double highValue = 1.0;
    bool manualOverride = false;
};

struct ControllerState {
    Mode mode = Mode::Auto;
    double fps = 0.0;
    double targetFPS = 30.0;
    double cpuMilliseconds = 0.0;
    double gpuMilliseconds = 0.0;
    double cpuQuality = 0.5;
    double gpuQuality = 0.5;
    bool cpuTimeAvailable = false;
    bool gpuTimeAvailable = false;
    bool qualityChanged = false;
    std::string limiter = "none";
    std::string reason = "starting";
    std::array<FeatureState, kFeatureCount> features{};
};

class Controller final {
public:
    Controller();

    void reset(const ControllerConfig& config);
    void reconfigure(const ControllerConfig& config, bool preserveQuality);
    void update(const ControllerSample& sample);

    void setManualOverride(FeatureId id, double value);
    void clearManualOverrides();

    const ControllerConfig& config() const { return config_; }
    const ControllerState& state() const { return state_; }

private:
    static size_t featureIndex(FeatureId id);
    static double clamp01(double value);
    static Mode normalizedMode(Mode mode);

    void refreshFeatureStates();
    void applyModeTargets();
    void updateReason(bool cpuOverload, bool gpuOverload, bool fpsFallback,
                      bool recovering, bool hasHeadroom);

    ControllerConfig config_{};
    ControllerState state_{};
    double cpuOverloadSeconds_ = 0.0;
    double gpuOverloadSeconds_ = 0.0;
    double cpuRecoverySeconds_ = 0.0;
    double gpuRecoverySeconds_ = 0.0;
    double cpuChangeTimerSeconds_ = 0.0;
    double gpuChangeTimerSeconds_ = 0.0;
};

const char* modeName(Mode mode);
Mode modeFromString(const std::string& value);

} // namespace threejfps

#endif
