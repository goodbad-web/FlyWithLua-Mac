#ifndef FLYWITHLUA_PANEL_GRAPHICS_BACKEND_H
#define FLYWITHLUA_PANEL_GRAPHICS_BACKEND_H

#include <XPLMDisplay.h>
#include <XPLMPanelGraphics.h>

#include <cstdint>
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

struct DrawCall {
    void* texture = nullptr;
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
bool enabled();
bool panelDrawing();
void disableForSession(const char* reason);

void configurePanelWindow(XPLMCreateWindow_t& params);
void configureOpenGLWindow(XPLMCreateWindow_t& params);
bool beginPanelWindow(XPLMWindowID window);
void endPanelWindow();

void setColor(float red, float green, float blue, float alpha);
void setLineWidth(float width);
void beginPrimitive(LegacyPrimitiveMode mode);
void vertex(float x, float y);
void endPrimitive();
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
bool drawCalls(const XPLMMesh_t& mesh, const std::vector<DrawCall>& calls);

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
