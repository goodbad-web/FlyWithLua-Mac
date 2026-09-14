#include "ThreeJFPSControllerCore.h"

#include <algorithm>
#include <cmath>

namespace threejfps {

namespace {

double positiveOr(double value, double fallback) {
    return std::isfinite(value) && value > 0.0 ? value : fallback;
}

} // namespace

Controller::Controller() {
    reset(config_);
}

size_t Controller::featureIndex(FeatureId id) {
    const auto index = static_cast<size_t>(id);
    return index < kFeatureCount ? index : 0;
}

double Controller::clamp01(double value) {
    if (!std::isfinite(value)) {
        return 0.5;
    }
    return std::max(0.0, std::min(1.0, value));
}

Mode Controller::normalizedMode(Mode mode) {
    return mode;
}

void Controller::reset(const ControllerConfig& config) {
    config_ = config;
    config_.targetFPS = positiveOr(config_.targetFPS, 30.0);
    config_.cpuHeadroomPercent = std::max(0.0, std::min(95.0, config_.cpuHeadroomPercent));
    config_.gpuHeadroomPercent = std::max(0.0, std::min(95.0, config_.gpuHeadroomPercent));
    config_.detectionWindowSeconds = positiveOr(config_.detectionWindowSeconds, 0.5);
    config_.qualityChangeIntervalSeconds = positiveOr(config_.qualityChangeIntervalSeconds, 2.0);
    config_.qualityRecoveryDelaySeconds = positiveOr(config_.qualityRecoveryDelaySeconds, 30.0);
    config_.degradationStep = clamp01(config_.degradationStep);
    config_.recoveryStep = clamp01(config_.recoveryStep);

    state_ = ControllerState{};
    state_.mode = normalizedMode(config_.mode);
    state_.targetFPS = config_.targetFPS;
    state_.cpuQuality = 0.5;
    state_.gpuQuality = 0.5;
    state_.cpuTimeAvailable = config_.cpuTimeAvailable;
    state_.gpuTimeAvailable = config_.gpuTimeAvailable;
    state_.reason = modeName(state_.mode) == std::string("off") ? "off" : "starting";

    cpuOverloadSeconds_ = 0.0;
    gpuOverloadSeconds_ = 0.0;
    cpuRecoverySeconds_ = 0.0;
    gpuRecoverySeconds_ = 0.0;
    cpuChangeTimerSeconds_ = config_.qualityChangeIntervalSeconds;
    gpuChangeTimerSeconds_ = config_.qualityChangeIntervalSeconds;

    applyModeTargets();
    refreshFeatureStates();
}

void Controller::reconfigure(const ControllerConfig& config, bool preserveQuality) {
    const double oldCPUQuality = state_.cpuQuality;
    const double oldGPUQuality = state_.gpuQuality;
    const double oldCPUOverload = cpuOverloadSeconds_;
    const double oldGPUOverload = gpuOverloadSeconds_;
    const double oldCPURecovery = cpuRecoverySeconds_;
    const double oldGPURecovery = gpuRecoverySeconds_;

    reset(config);
    if (!preserveQuality) {
        return;
    }

    state_.cpuQuality = oldCPUQuality;
    state_.gpuQuality = oldGPUQuality;
    cpuOverloadSeconds_ = oldCPUOverload;
    gpuOverloadSeconds_ = oldGPUOverload;
    cpuRecoverySeconds_ = oldCPURecovery;
    gpuRecoverySeconds_ = oldGPURecovery;
    applyModeTargets();
    refreshFeatureStates();
}

void Controller::applyModeTargets() {
    switch (state_.mode) {
        case Mode::MaxFPS:
            state_.cpuQuality = 0.0;
            state_.gpuQuality = 0.0;
            break;
        case Mode::MaxQuality:
            state_.cpuQuality = 1.0;
            state_.gpuQuality = 1.0;
            break;
        case Mode::Auto:
            state_.cpuQuality = clamp01(state_.cpuQuality);
            state_.gpuQuality = clamp01(state_.gpuQuality);
            break;
        case Mode::Off:
            // The native runtime restores DataRefs in OFF mode. Keeping the
            // midpoint here gives the UI a deterministic preview without
            // causing the controller to write any feature values.
            state_.cpuQuality = 0.5;
            state_.gpuQuality = 0.5;
            break;
    }
}

void Controller::refreshFeatureStates() {
    for (size_t index = 0; index < kFeatureCount; ++index) {
        const FeatureConfig& config = config_.features[index];
        FeatureState& state = state_.features[index];
        state.id = config.id;
        state.enabled = config.enabled;
        state.available = config.available;
        state.lowValue = config.lowValue;
        state.highValue = config.highValue;
        state.manualOverride = config.manualOverride;

        if (!config.enabled || !config.available) {
            state.currentValue = config.lowValue + (config.highValue - config.lowValue) * 0.5;
            continue;
        }

        double quality = 0.5;
        switch (config.id) {
            case FeatureId::LOD:
            case FeatureId::Shadows:
                quality = std::min(state_.cpuQuality, state_.gpuQuality);
                break;
            case FeatureId::Clouds:
            case FeatureId::FSR:
                quality = state_.gpuQuality;
                break;
        }

        state.currentValue = config.manualOverride
            ? config.manualValue
            : config.lowValue + (config.highValue - config.lowValue) * clamp01(quality);
    }
}

void Controller::updateReason(bool cpuOverload, bool gpuOverload, bool fpsFallback,
                              bool recovering, bool hasHeadroom) {
    if (state_.mode == Mode::Off) {
        state_.limiter = "none";
        state_.reason = "off";
    } else if (fpsFallback) {
        state_.limiter = "FPS";
        state_.reason = "fps-fallback";
    } else if (cpuOverload && gpuOverload) {
        state_.limiter = "CPU+GPU";
        state_.reason = "cpu-gpu-overload";
    } else if (cpuOverload) {
        state_.limiter = "CPU";
        state_.reason = "cpu-overload";
    } else if (gpuOverload) {
        state_.limiter = "GPU";
        state_.reason = "gpu-overload";
    } else if (recovering || hasHeadroom) {
        state_.limiter = "none";
        state_.reason = "recovering";
    } else {
        state_.limiter = "none";
        state_.reason = "stable";
    }
}

void Controller::update(const ControllerSample& sample) {
    const double dt = std::max(0.0, std::min(1.0, sample.deltaSeconds));
    state_.fps = std::max(0.0, sample.fps);
    state_.targetFPS = config_.targetFPS;
    state_.cpuMilliseconds = std::max(0.0, sample.cpuMilliseconds);
    state_.gpuMilliseconds = std::max(0.0, sample.gpuMilliseconds);
    state_.cpuTimeAvailable = sample.cpuTimeAvailable && config_.cpuTimeAvailable;
    state_.gpuTimeAvailable = sample.gpuTimeAvailable && config_.gpuTimeAvailable;
    state_.qualityChanged = false;

    if (state_.mode != Mode::Auto) {
        const double oldCPU = state_.cpuQuality;
        const double oldGPU = state_.gpuQuality;
        applyModeTargets();
        state_.qualityChanged = oldCPU != state_.cpuQuality || oldGPU != state_.gpuQuality;
        refreshFeatureStates();
        updateReason(false, false, false, false, false);
        return;
    }

    cpuChangeTimerSeconds_ += dt;
    gpuChangeTimerSeconds_ += dt;

    const double targetMilliseconds = 1000.0 / positiveOr(config_.targetFPS, 30.0);
    const double cpuBudget = targetMilliseconds * (1.0 - config_.cpuHeadroomPercent * 0.01);
    const double gpuBudget = targetMilliseconds * (1.0 - config_.gpuHeadroomPercent * 0.01);
    const bool fpsBelowTarget = state_.fps > 0.0 && state_.fps < config_.targetFPS * 0.98;
    const bool fpsHasHeadroom = state_.fps >= config_.targetFPS * 1.05;
    const bool cpuAvailable = state_.cpuTimeAvailable;
    const bool gpuAvailable = state_.gpuTimeAvailable;
    const bool fpsFallback = !cpuAvailable && !gpuAvailable;

    bool cpuOverload = cpuAvailable && state_.cpuMilliseconds > cpuBudget;
    bool gpuOverload = gpuAvailable && state_.gpuMilliseconds > gpuBudget;
    if (fpsFallback && fpsBelowTarget) {
        cpuOverload = true;
        gpuOverload = true;
    }
    if (!cpuAvailable && !fpsFallback && fpsBelowTarget) {
        cpuOverload = true;
    }
    if (!gpuAvailable && !fpsFallback && fpsBelowTarget) {
        gpuOverload = true;
    }

    const bool cpuHeadroom = cpuAvailable
        ? state_.cpuMilliseconds < cpuBudget * 0.90
        : fpsHasHeadroom;
    const bool gpuHeadroom = gpuAvailable
        ? state_.gpuMilliseconds < gpuBudget * 0.90
        : fpsHasHeadroom;

    if (cpuOverload) {
        cpuOverloadSeconds_ += dt;
        cpuRecoverySeconds_ = 0.0;
    } else if (cpuHeadroom) {
        cpuOverloadSeconds_ = 0.0;
        cpuRecoverySeconds_ += dt;
    } else {
        cpuOverloadSeconds_ = 0.0;
        cpuRecoverySeconds_ = 0.0;
    }

    if (gpuOverload) {
        gpuOverloadSeconds_ += dt;
        gpuRecoverySeconds_ = 0.0;
    } else if (gpuHeadroom) {
        gpuOverloadSeconds_ = 0.0;
        gpuRecoverySeconds_ += dt;
    } else {
        gpuOverloadSeconds_ = 0.0;
        gpuRecoverySeconds_ = 0.0;
    }

    const double oldCPU = state_.cpuQuality;
    const double oldGPU = state_.gpuQuality;
    if (cpuOverloadSeconds_ >= config_.detectionWindowSeconds &&
        cpuChangeTimerSeconds_ >= config_.qualityChangeIntervalSeconds) {
        state_.cpuQuality = clamp01(state_.cpuQuality - config_.degradationStep);
        cpuChangeTimerSeconds_ = 0.0;
    } else if (!cpuOverload && cpuRecoverySeconds_ >= config_.qualityRecoveryDelaySeconds &&
               cpuChangeTimerSeconds_ >= config_.qualityChangeIntervalSeconds) {
        state_.cpuQuality = clamp01(state_.cpuQuality + config_.recoveryStep);
        cpuChangeTimerSeconds_ = 0.0;
    }

    if (gpuOverloadSeconds_ >= config_.detectionWindowSeconds &&
        gpuChangeTimerSeconds_ >= config_.qualityChangeIntervalSeconds) {
        state_.gpuQuality = clamp01(state_.gpuQuality - config_.degradationStep);
        gpuChangeTimerSeconds_ = 0.0;
    } else if (!gpuOverload && gpuRecoverySeconds_ >= config_.qualityRecoveryDelaySeconds &&
               gpuChangeTimerSeconds_ >= config_.qualityChangeIntervalSeconds) {
        state_.gpuQuality = clamp01(state_.gpuQuality + config_.recoveryStep);
        gpuChangeTimerSeconds_ = 0.0;
    }

    state_.qualityChanged = oldCPU != state_.cpuQuality || oldGPU != state_.gpuQuality;
    const bool recovering = (cpuRecoverySeconds_ >= config_.qualityRecoveryDelaySeconds) ||
                            (gpuRecoverySeconds_ >= config_.qualityRecoveryDelaySeconds);
    const bool hasHeadroom = cpuHeadroom || gpuHeadroom;
    updateReason(cpuOverload, gpuOverload, fpsFallback, recovering, hasHeadroom);
    refreshFeatureStates();
}

void Controller::setManualOverride(FeatureId id, double value) {
    FeatureConfig& feature = config_.features[featureIndex(id)];
    feature.manualOverride = true;
    if (!std::isfinite(value)) {
        value = feature.lowValue + (feature.highValue - feature.lowValue) * 0.5;
    }
    const double lower = std::min(feature.lowValue, feature.highValue);
    const double upper = std::max(feature.lowValue, feature.highValue);
    feature.manualValue = std::max(lower, std::min(upper, value));
    refreshFeatureStates();
}

void Controller::clearManualOverrides() {
    for (FeatureConfig& feature : config_.features) {
        feature.manualOverride = false;
    }
    refreshFeatureStates();
}

const char* modeName(Mode mode) {
    switch (mode) {
        case Mode::Auto: return "auto";
        case Mode::MaxFPS: return "max-fps";
        case Mode::MaxQuality: return "max-quality";
        case Mode::Off: return "off";
    }
    return "auto";
}

Mode modeFromString(const std::string& value) {
    if (value == "max" || value == "max-fps") {
        return Mode::MaxFPS;
    }
    if (value == "min" || value == "max-quality") {
        return Mode::MaxQuality;
    }
    if (value == "off") {
        return Mode::Off;
    }
    return Mode::Auto;
}

} // namespace threejfps
