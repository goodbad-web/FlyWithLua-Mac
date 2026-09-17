#ifndef FLYWITHLUA_PANEL_GRAPHICS_BACKEND_H
#define FLYWITHLUA_PANEL_GRAPHICS_BACKEND_H

#include <XPLMDisplay.h>
#include <XPLMPanelGraphics.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace flywithlua::panel {

enum class BackendPreference {
    Auto,
    Panel,
    OpenGL,
};

enum class LegacyPrimitiveMode {
    Points,
    Lines,
    LineStrip,
    LineLoop,
    Polygon,
    Triangles,
    TriangleStrip,
    TriangleFan,
    Quads,
    QuadStrip,
};

enum class PrimitiveSource {
    Legacy,
    Panel,
};

enum class MeshCoordinateSpace {
    PublicYUp,
    NativeTopLeft,
};

enum Capability : std::uint32_t {
    CapabilityNone = 0,
    CapabilityPrimitives = 1u << 0,
    CapabilityText = 1u << 1,
    CapabilityTexture = 1u << 2,
    CapabilityMesh = 1u << 3,
};

constexpr std::uint32_t kRequiredCapabilityMask =
    CapabilityPrimitives | CapabilityText | CapabilityTexture | CapabilityMesh;

struct DrawCall {
    void* texture = nullptr;
    // Public API order is {left, bottom, right, top}; the backend converts
    // this to XPLMDrawCall_t's top-left order for PublicYUp meshes.
    float scissors[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int indexOffset = 0;
    int elementCount = 0;
    int vertexOffset = 0;
};

void configureBackend(const std::string& value);
void initialize();
void shutdown();

BackendPreference requestedBackend();
bool panelAvailable();
bool panelReady();
bool enabled();
bool panelDrawing();
std::uint32_t capabilities();
bool hasCapability(Capability capability);
bool hasAllCapabilities(std::uint32_t capabilityMask);
void disableForSession(const char* reason);
std::uint64_t sessionGeneration();

void registerWindow(XPLMWindowID window,
                    std::uint64_t ownerScriptId = 0);
void unregisterWindow(XPLMWindowID window);

void configurePanelWindow(XPLMCreateWindow_t& params);
void configureOpenGLWindow(XPLMCreateWindow_t& params);
bool beginPanelWindow(XPLMWindowID window,
                      std::uint64_t ownerScriptId = 0);
void endPanelWindow();

class PanelDrawScope {
public:
    explicit PanelDrawScope(XPLMWindowID window,
                            std::uint64_t ownerScriptId = 0);
    ~PanelDrawScope();

    PanelDrawScope(const PanelDrawScope&) = delete;
    PanelDrawScope& operator=(const PanelDrawScope&) = delete;

    bool active() const;

private:
    struct StateSnapshot;
    std::unique_ptr<StateSnapshot> snapshot_;
    bool previousLuaDrawingState_ = true;
    std::uint64_t ownerScriptId_ = 0;
    bool active_ = false;
};

void setColor(float red, float green, float blue, float alpha);
std::uint32_t currentColor();
void setLineWidth(float width);
bool beginPrimitive(LegacyPrimitiveMode mode,
                    PrimitiveSource source,
                    std::uint64_t ownerScriptId = 0);
bool vertex(float x,
            float y,
            PrimitiveSource source,
            std::uint64_t ownerScriptId = 0);
bool endPrimitive(PrimitiveSource source,
                  std::uint64_t ownerScriptId = 0);
void drawFilledRect(float x1, float y1, float x2, float y2);

bool drawText(float x,
              float y,
              const char* text,
              float fontSize,
              const char* family,
              int weight,
              float red,
              float green,
              float blue,
              float alpha);
bool drawTextCurrentColor(float x,
                          float y,
                          const char* text,
                          float fontSize,
                          const char* family,
                          int weight);
double measureText(const char* text, float fontSize, const char* family, int weight);

int drawLegacyText(int x,
                   int y,
                   const char* text,
                   const char* fontName,
                   float red,
                   float green,
                   float blue,
                   float alpha);
int drawLegacyTextCurrentColor(int x,
                               int y,
                               const char* text,
                               const char* fontName);
double measureLegacyText(const char* text, const char* fontName);

void* createTexture(const unsigned char* rgbaImage, int width, int height);
void destroyTexture(void* texture);
bool drawCalls(const XPLMMesh_t& mesh,
               const std::vector<DrawCall>& calls,
               MeshCoordinateSpace coordinateSpace = MeshCoordinateSpace::PublicYUp);
bool drawCalls(const XPLMMesh_t& mesh,
               const DrawCall& call,
               MeshCoordinateSpace coordinateSpace = MeshCoordinateSpace::PublicYUp);

// These buffers are valid only during the active Panel Graphics callback.
// They are owned by the backend so Lua mesh helpers can reuse their capacity.
std::vector<float>& meshVertexScratch();
std::vector<std::uint16_t>& meshIndexScratch();

namespace testing {

/** Test-only recorder used by the native Panel Graphics state tests. */
struct Recorder {
    int polygonCalls = 0;
    int lineCalls = 0;
    int drawCallBatches = 0;
    float lastLineWidth = 0.0f;
    std::vector<XPLMVertex_t> lastVertices;
    std::vector<float> lastMeshVertices;
    float lastScissors[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

void resetForTesting(std::uint32_t capabilityMask, Recorder* recorder,
                     int windowWidth = 640, int windowHeight = 480);
std::uint64_t windowGenerationForTesting(XPLMWindowID window);

} // namespace testing

} // namespace flywithlua::panel

extern "C" {
int flywithlua_panel_draw_hidpi_text(int x,
                                     int y,
                                     const char* text,
                                     float logicalSize,
                                     const char* family,
                                     int weight);
int flywithlua_panel_is_drawing(void);
double flywithlua_panel_measure_hidpi_text(const char* text,
                                           float logicalSize,
                                           const char* family,
                                           int weight);
int flywithlua_panel_draw_legacy_text(int x,
                                      int y,
                                      const char* text,
                                      const char* fontName,
                                      const float* color);
double flywithlua_panel_measure_legacy_text(const char* text, const char* fontName);
}

#endif
