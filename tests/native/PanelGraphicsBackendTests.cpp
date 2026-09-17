#include "PanelGraphicsBackend.h"
#include "FlyWithLua.h"

#include <cassert>
#include <cstdint>
#include <string>

namespace {

int gQuarantineCount = 0;

} // namespace

namespace flywithlua {

bool WeAreNotInDrawingState = true;

void ReportLuaScriptError(LuaScriptId, const char*, const std::string&) {
    ++gQuarantineCount;
}

} // namespace flywithlua

namespace flywithlua::panel {

void invalidateLuaResources(std::uint64_t) {}

} // namespace flywithlua::panel

namespace flwnd {

void invalidateImguiWindows() {}

} // namespace flwnd

extern "C" void* XPLMFindSymbol(const char*) {
    return nullptr;
}

extern "C" void XPLMDebugString(const char*) {}

extern "C" void XPLMGetWindowGeometry(XPLMWindowID,
                                        int* left, int* top,
                                        int* right, int* bottom) {
    if (left != nullptr) *left = 0;
    if (top != nullptr) *top = 480;
    if (right != nullptr) *right = 640;
    if (bottom != nullptr) *bottom = 0;
}

int main() {
    using namespace flywithlua::panel;
    auto* windowA = reinterpret_cast<XPLMWindowID>(static_cast<uintptr_t>(0x101));
    auto* windowB = reinterpret_cast<XPLMWindowID>(static_cast<uintptr_t>(0x202));

    testing::Recorder recorder;
    testing::resetForTesting(kRequiredCapabilityMask, &recorder, 640, 480);
    assert(panelAvailable());
    assert(panelReady());
    assert(hasAllCapabilities(kRequiredCapabilityMask));

    registerWindow(windowA, 11);
    const std::uint64_t firstGeneration = testing::windowGenerationForTesting(windowA);
    assert(firstGeneration != 0);

    {
        PanelDrawScope outer(windowA, 11);
        assert(outer.active());
        setColor(1.0f, 0.0f, 0.0f, 1.0f);
        setLineWidth(3.0f);
        assert(!vertex(1.0f, 2.0f, PrimitiveSource::Legacy, 11));
        assert(beginPrimitive(LegacyPrimitiveMode::Lines, PrimitiveSource::Legacy, 11));
        assert(vertex(1.0f, 2.0f, PrimitiveSource::Legacy, 11));
        assert(!vertex(2.0f, 3.0f, PrimitiveSource::Legacy, 12));

        registerWindow(windowB, 22);
        {
            PanelDrawScope inner(windowB, 22);
            assert(inner.active());
            assert(beginPrimitive(LegacyPrimitiveMode::Polygon, PrimitiveSource::Panel, 22));
            assert(vertex(10.0f, 20.0f, PrimitiveSource::Panel, 22));
            assert(vertex(20.0f, 20.0f, PrimitiveSource::Panel, 22));
            assert(vertex(10.0f, 30.0f, PrimitiveSource::Panel, 22));
            assert(endPrimitive(PrimitiveSource::Panel, 22));
        }

        // The outer primitive and owner context are restored after the nested
        // window is closed.
        assert(vertex(2.0f, 3.0f, PrimitiveSource::Legacy, 11));
        assert(!endPrimitive(PrimitiveSource::Panel, 11));
        assert(endPrimitive(PrimitiveSource::Legacy, 11));
        assert(recorder.lineCalls == 1);
        assert(recorder.polygonCalls == 1);
        assert(recorder.lastLineWidth == 3.0f);

        const float publicVertices[] = {
            10.0f, 20.0f, 0.0f, 1.0f, 0.0f,
            30.0f, 40.0f, 1.0f, 0.0f, 0.0f,
            10.0f, 40.0f, 0.0f, 0.0f, 0.0f,
        };
        const std::uint16_t publicIndices[] = {0, 1, 2};
        XPLMMesh_t mesh{3, publicVertices, 3, publicIndices};
        DrawCall drawCall;
        drawCall.texture = reinterpret_cast<void*>(static_cast<uintptr_t>(0x303));
        drawCall.scissors[0] = 10.0f;
        drawCall.scissors[1] = 20.0f;
        drawCall.scissors[2] = 30.0f;
        drawCall.scissors[3] = 40.0f;
        drawCall.elementCount = 3;
        assert(drawCalls(mesh, drawCall, MeshCoordinateSpace::PublicYUp));
        assert(recorder.lastMeshVertices[1] == 460.0f);
        assert(recorder.lastMeshVertices[6] == 440.0f);
        assert(recorder.lastScissors[0] == 10.0f);
        assert(recorder.lastScissors[1] == 440.0f);
        assert(recorder.lastScissors[2] == 30.0f);
        assert(recorder.lastScissors[3] == 460.0f);
    }
    assert(!panelDrawing());
    assert(flywithlua::WeAreNotInDrawingState);

    unregisterWindow(windowA);
    registerWindow(windowA, 33);
    assert(testing::windowGenerationForTesting(windowA) != firstGeneration);

    // Reusing an XPLM window ID while a scope is alive must not restore the
    // old generation or leave the old drawing state active.
    testing::resetForTesting(kRequiredCapabilityMask, &recorder);
    registerWindow(windowA, 11);
    {
        PanelDrawScope oldScope(windowA, 11);
        assert(oldScope.active());
        const std::uint64_t oldGeneration = testing::windowGenerationForTesting(windowA);
        unregisterWindow(windowA);
        registerWindow(windowA, 77);
        assert(testing::windowGenerationForTesting(windowA) != oldGeneration);
        assert(!beginPrimitive(LegacyPrimitiveMode::Lines, PrimitiveSource::Legacy, 11));
        {
            PanelDrawScope replacement(windowA, 77);
            assert(replacement.active());
        }
        assert(!panelDrawing());
    }
    assert(flywithlua::WeAreNotInDrawingState);

    testing::resetForTesting(CapabilityPrimitives | CapabilityText, &recorder);
    assert(!panelAvailable());
    assert(capabilities() == (CapabilityPrimitives | CapabilityText));
    {
        PanelDrawScope unavailable(windowA, 44);
        assert(!unavailable.active());
    }

    testing::resetForTesting(kRequiredCapabilityMask, &recorder);
    const int errorsBeforeUnfinished = gQuarantineCount;
    {
        PanelDrawScope unfinished(windowA, 55);
        assert(unfinished.active());
        assert(beginPrimitive(LegacyPrimitiveMode::Lines, PrimitiveSource::Panel, 55));
        assert(vertex(1.0f, 1.0f, PrimitiveSource::Panel, 55));
        // Scope destruction discards the vertices and quarantines owner 55;
        // it must not implicitly submit the primitive.
    }
    assert(gQuarantineCount == errorsBeforeUnfinished + 1);
    assert(recorder.lineCalls == 1);
    assert(!panelDrawing());

    return 0;
}
