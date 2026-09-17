#ifndef FLOATINGWINDOWS_IMGUI_PANEL_MESH_H_
#define FLOATINGWINDOWS_IMGUI_PANEL_MESH_H_

#include "../Graphics/PanelGraphicsBackend.h"
#include "imgui/imgui.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace flwnd {

struct ImGuiPanelMesh {
    std::vector<float> vertices;
    std::vector<std::uint16_t> indices;
    std::vector<flywithlua::panel::DrawCall> drawCalls;
    bool valid = false;
};

using PanelTextureResolver = std::function<void*(void*)>;

// Converts one rendered ImGui frame into the XPLMPanelGraphics 5-float
// vertex format. The destination owns its buffers and retains capacity.
bool rebuildImGuiPanelMesh(const ImDrawData* drawData,
                           void* fontTexture,
                           ImGuiPanelMesh& destination,
                           const PanelTextureResolver& resolveTexture);

} // namespace flwnd

#endif /* FLOATINGWINDOWS_IMGUI_PANEL_MESH_H_ */
