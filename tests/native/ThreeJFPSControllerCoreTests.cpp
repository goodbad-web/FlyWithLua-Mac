#include "ThreeJFPSControllerCore.h"

#include <cassert>
#include <cmath>

using namespace threejfps;

namespace {

ControllerConfig makeConfig(bool processTimes) {
    ControllerConfig config;
    config.mode = Mode::Auto;
    config.targetFPS = 30.0;
    config.cpuTimeAvailable = processTimes;
    config.gpuTimeAvailable = processTimes;
    config.features[0] = {FeatureId::LOD, true, true, 4.0, 1.5};
    config.features[1] = {FeatureId::Shadows, true, true, 500.0, 1500.0};
    config.features[2] = {FeatureId::Clouds, true, true, 0.5, 1.0};
    config.features[3] = {FeatureId::FSR, true, true, 1.0, 4.0};
    return config;
}

void tick(Controller& controller, const ControllerSample& sample, double seconds) {
    const int count = static_cast<int>(std::ceil(seconds / sample.deltaSeconds));
    for (int index = 0; index < count; ++index) {
        controller.update(sample);
    }
}

} // namespace

int main() {
    {
        Controller controller;
        ControllerConfig config = makeConfig(true);
        controller.reset(config);
        ControllerSample stable;
        stable.deltaSeconds = 1.0 / 60.0;
        stable.fps = 30.0;
        stable.cpuMilliseconds = 25.0;
        stable.gpuMilliseconds = 28.0;
        stable.cpuTimeAvailable = true;
        stable.gpuTimeAvailable = true;
        tick(controller, stable, 60.0);
        assert(std::abs(controller.state().cpuQuality - 0.5) < 0.0001);
        assert(std::abs(controller.state().gpuQuality - 0.5) < 0.0001);
        assert(controller.state().reason == "stable");
    }

    {
        Controller controller;
        controller.reset(makeConfig(false));
        ControllerSample deficit;
        deficit.deltaSeconds = 1.0 / 60.0;
        deficit.fps = 20.0;
        double firstChange = -1.0;
        for (int frame = 0; frame < 5 * 60; ++frame) {
            controller.update(deficit);
            if (firstChange < 0.0 && controller.state().qualityChanged) {
                firstChange = (frame + 1) * deficit.deltaSeconds;
            }
        }
        assert(firstChange >= 0.0 && firstChange <= 5.0);
        assert(controller.state().reason == "fps-fallback");
        assert(controller.state().cpuQuality < 0.5);
        assert(controller.state().gpuQuality < 0.5);
    }

    {
        Controller controller;
        controller.reset(makeConfig(false));
        ControllerSample deficit;
        deficit.deltaSeconds = 1.0 / 60.0;
        deficit.fps = 20.0;
        tick(controller, deficit, 2.1);
        const double degradedQuality = controller.state().cpuQuality;
        assert(degradedQuality < 0.5);

        ControllerSample headroom = deficit;
        headroom.fps = 60.0;
        tick(controller, headroom, 29.0);
        assert(std::abs(controller.state().cpuQuality - degradedQuality) < 0.0001);
        tick(controller, headroom, 2.0);
        assert(controller.state().cpuQuality > degradedQuality);
    }

    {
        Controller controller;
        controller.reset(makeConfig(true));
        ControllerSample cpuLimited;
        cpuLimited.deltaSeconds = 1.0 / 60.0;
        cpuLimited.fps = 60.0;
        cpuLimited.cpuMilliseconds = 32.0;
        cpuLimited.gpuMilliseconds = 20.0;
        cpuLimited.cpuTimeAvailable = true;
        cpuLimited.gpuTimeAvailable = true;
        tick(controller, cpuLimited, 2.1);
        assert(controller.state().cpuQuality < 0.5);
        assert(std::abs(controller.state().gpuQuality - 0.5) < 0.0001);

        ControllerSample gpuLimited = cpuLimited;
        gpuLimited.cpuMilliseconds = 10.0;
        gpuLimited.gpuMilliseconds = 32.0;
        tick(controller, gpuLimited, 2.1);
        assert(controller.state().gpuQuality < 0.5);
    }

    {
        Controller controller;
        ControllerConfig config = makeConfig(false);
        config.features[2].enabled = false;
        controller.reset(config);
        ControllerSample deficit;
        deficit.deltaSeconds = 1.0 / 60.0;
        deficit.fps = 20.0;
        tick(controller, deficit, 5.0);
        const FeatureState& clouds = controller.state().features[2];
        assert(!clouds.enabled);
        assert(std::abs(clouds.currentValue - 0.75) < 0.0001);
    }

    return 0;
}
