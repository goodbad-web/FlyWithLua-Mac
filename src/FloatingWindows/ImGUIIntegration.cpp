/*
 *   Floating Windows with imgui integration for FlyWithLua
 *   Copyright (C) 2018 Folke Will <folko@solhost.org>
 *   Released as public domain code.
 *
 */

/*
 * portions copied with permission from https://github.com/kuroneko/xsb_public
 *
 * ImgWindow.cpp
 *
 * Integration for dear imgui into X-Plane.
 *
 * Copyright (C) 2018, Christopher Collins
 */
#include <XPLMGraphics.h>
#include <XPLMUtilities.h>
#include "../Graphics/PanelGraphicsBackend.h"
#include "../FlyWithLua.h"
#include <cstring>
#include <cstdint>
#include <cctype>
#include <stdexcept>
#include <vector>
#include "ImGUIIntegration.h"
#include "FLWIntegration.h"

namespace flwnd {

namespace {

class OpenGLDrawStateScope {
public:
    explicit OpenGLDrawStateScope(bool active):
        active_(active), previousLuaDrawingState_(flywithlua::WeAreNotInDrawingState) {
        if (!active_) {
            return;
        }
        XPLMSetGraphicsState(0, 0, 0, 1, 1, 0, 0);
        flywithlua::WeAreNotInDrawingState = false;
    }

    ~OpenGLDrawStateScope() {
        if (active_) {
            flywithlua::WeAreNotInDrawingState = previousLuaDrawingState_;
        }
    }

    OpenGLDrawStateScope(const OpenGLDrawStateScope&) = delete;
    OpenGLDrawStateScope& operator=(const OpenGLDrawStateScope&) = delete;

private:
    bool active_;
    bool previousLuaDrawingState_;
};

} // namespace

ImGUIWindow::ImGUIWindow(int width, int height, int decoration, std::uint64_t ownerScriptId):
    FloatingWindow(width, height, decoration, true, ownerScriptId)
{
    imGuiContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(imGuiContext);

    auto &style = ImGui::GetStyle();
    style.WindowRounding = 0;

    auto &io = ImGui::GetIO();
    // io.RenderDrawListsFn = nullptr;
    io.IniFilename = nullptr;
    // io.OptMacOSXBehaviors = false;
    // disable OSX-like keyboard behaviours always - we don't have the keymapping for it.
    io.ConfigMacOSXBehaviors = false; // This is the new version above Imgui 1.60
    io.ConfigFlags = ImGuiConfigFlags_NavNoCaptureKeyboard;

    io.KeyMap[ImGuiKey_Tab] = XPLM_VK_TAB;
    io.KeyMap[ImGuiKey_LeftArrow] = XPLM_VK_LEFT;
    io.KeyMap[ImGuiKey_RightArrow] = XPLM_VK_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow] = XPLM_VK_UP;
    io.KeyMap[ImGuiKey_DownArrow] = XPLM_VK_DOWN;
    io.KeyMap[ImGuiKey_PageUp] = XPLM_VK_PRIOR;
    io.KeyMap[ImGuiKey_PageDown] = XPLM_VK_NEXT;
    io.KeyMap[ImGuiKey_Home] = XPLM_VK_HOME;
    io.KeyMap[ImGuiKey_End] = XPLM_VK_END;
    io.KeyMap[ImGuiKey_Insert] = XPLM_VK_INSERT;
    io.KeyMap[ImGuiKey_Delete] = XPLM_VK_DELETE;
    io.KeyMap[ImGuiKey_Backspace] = XPLM_VK_BACK;
    io.KeyMap[ImGuiKey_Space] = XPLM_VK_SPACE;
    io.KeyMap[ImGuiKey_Enter] = XPLM_VK_ENTER;
    io.KeyMap[ImGuiKey_Escape] = XPLM_VK_ESCAPE;
    io.KeyMap[ImGuiKey_A] = XPLM_VK_A;
    io.KeyMap[ImGuiKey_C] = XPLM_VK_C;
    io.KeyMap[ImGuiKey_V] = XPLM_VK_V;
    io.KeyMap[ImGuiKey_X] = XPLM_VK_X;
    io.KeyMap[ImGuiKey_Y] = XPLM_VK_Y;
    io.KeyMap[ImGuiKey_Z] = XPLM_VK_Z;

    panelRenderer = isPanelGraphics();
    if (!panelRenderer && !syncFontTexture(false)) {
        throw std::runtime_error("Could not create ImGui font texture");
    }
}

bool ImGUIWindow::syncFontTexture(bool usePanelGraphics) {
    ImGui::SetCurrentContext(imGuiContext);
    auto& io = ImGui::GetIO();

    if (usePanelGraphics) {
        const bool backendChanged = !panelRenderer;
        bool fontTextureChanged = false;
        if (panelFontTexture == nullptr) {
            uint8_t* pixels = nullptr;
            int fontTexWidth = 0;
            int fontTexHeight = 0;
            io.Fonts->GetTexDataAsAlpha8(&pixels, &fontTexWidth, &fontTexHeight);
            std::vector<unsigned char> rgba(static_cast<size_t>(fontTexWidth) *
                                            static_cast<size_t>(fontTexHeight) * 4u);
            for (size_t index = 0; index < static_cast<size_t>(fontTexWidth) *
                                         static_cast<size_t>(fontTexHeight); ++index) {
                rgba[index * 4u + 0u] = 255;
                rgba[index * 4u + 1u] = 255;
                rgba[index * 4u + 2u] = 255;
                rgba[index * 4u + 3u] = pixels[index];
            }
            panelFontTexture = flywithlua::panel::createTexture(rgba.data(),
                                                                 fontTexWidth,
                                                                 fontTexHeight);
            if (panelFontTexture == nullptr) {
                return false;
            }
            fontTextureChanged = true;
        }
        if (backendChanged || fontTextureChanged) {
            requestRedraw();
            frameRendered = false;
        }
        panelRenderer = true;
        io.Fonts->SetTexID(panelFontTexture);
        return true;
    }

    if (fontTextureId == 0) {
        uint8_t* pixels = nullptr;
        int fontTexWidth = 0;
        int fontTexHeight = 0;
        io.Fonts->GetTexDataAsAlpha8(&pixels, &fontTexWidth, &fontTexHeight);
        int textureId = 0;
        XPLMGenerateTextureNumbers(&textureId, 1);
        fontTextureId = static_cast<GLuint>(textureId);

        XPLMBindTexture2d(fontTextureId, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, fontTexWidth, fontTexHeight, 0,
                     GL_ALPHA, GL_UNSIGNED_BYTE, pixels);
    }
    if (panelFontTexture != nullptr) {
        flywithlua::panel::destroyTexture(panelFontTexture);
        panelFontTexture = nullptr;
    }
    if (panelRenderer) {
        requestRedraw();
        frameRendered = false;
    }
    panelRenderer = false;
    io.Fonts->SetTexID(reinterpret_cast<void*>(static_cast<intptr_t>(fontTextureId)));
    return true;
}

void ImGUIWindow::setBuildCallback(BuildCallback cb) {
    doBuild = cb;
    requestRedraw();
}

void ImGUIWindow::setErrorHandler(ErrorHandler eh) {
    onError = eh;
}

void ImGUIWindow::requestRedraw() {
    imguiDirty = true;
    panelMesh.valid = false;
}

void ImGUIWindow::setContinuousUpdate(bool enabled) {
    continuousUpdate = enabled;
    requestRedraw();
}

ImGUIWindow::RenderStats ImGUIWindow::renderStats() const {
    return stats;
}

void ImGUIWindow::updateGeometry() {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    XPLMGetWindowGeometry(getXWindow(), &left, &top, &right, &bottom);
    if (left != mLeft || top != mTop || right != mRight || bottom != mBottom) {
        mLeft = left;
        mTop = top;
        mRight = right;
        mBottom = bottom;
        requestRedraw();
    }
}

void ImGUIWindow::onDraw() {
    if (stopped) {
        return;
    }

    const bool windowUsesPanel = isPanelGraphics();
    OpenGLDrawStateScope graphicsScope(!windowUsesPanel);
    flywithlua::panel::PanelDrawScope panelScope(windowUsesPanel ? getXWindow() : nullptr);
    if (windowUsesPanel && !panelScope.active()) {
        return;
    }
    // XPLMCreateTexture is valid only from a panel window's draw callback.
    // Enter the scope before lazily creating the ImGui font texture.
    if (!syncFontTexture(windowUsesPanel)) {
        if (windowUsesPanel) {
            flywithlua::panel::disableForSession("Panel Graphics ImGui font texture creation failed");
        }
        return;
    }
    updateMatrices();
    try {
        updateGeometry();
        if (continuousUpdate || imguiDirty || !panelMesh.valid || !panelRenderer) {
            buildGUI();
        }
        showGUI(panelScope.active());
        if (windowUsesPanel && !flywithlua::panel::enabled()) {
            // Panel Graphics failures are handled by FloatingWindow's next
            // flight-loop reconciliation. Do not run the legacy callback in
            // the failed Panel callback and accidentally mix OpenGL into it.
            return;
        }
    } catch (const std::exception &e) {
        if (onError) {
            onError(e.what());
        }
        stopped = true;
    }

    ImGui::SetCurrentContext(imGuiContext);
    auto &io = ImGui::GetIO();
    bool hasKeyboardFocus = hasInputFocus();
    if (io.WantTextInput && !hasKeyboardFocus) {
        requestInputFocus(true);
    } else if (!io.WantTextInput && hasKeyboardFocus) {
        requestInputFocus(false);
        // reset keysdown otherwise we'll think any keys used to defocus the keyboard are still down!
        std::fill(std::begin(io.KeysDown), std::end(io.KeysDown), false);
    }

    FloatingWindow::onDraw();
}

void ImGUIWindow::buildGUI() {
    ImGui::SetCurrentContext(imGuiContext);
    auto &io = ImGui::GetIO();
    // Consume the current invalidation before executing Lua. A redraw request
    // made by the builder itself remains set for the next frame.
    imguiDirty = false;

    float win_width = static_cast<float>(mRight - mLeft);
    float win_height = static_cast<float>(mTop - mBottom);

    io.DisplaySize = ImVec2(win_width, win_height);
    // in boxels, we're always scale 1, 1.
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2((float) 0.0, (float) 0.0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(win_width, win_height), ImGuiCond_Always);

    // and construct the window
    // This is the original Begin that does not have horizontal scrollbars.
    // I am keeping it here to make sure adding them does not cause and issuew
    // ImGui::Begin("FlyWithLua", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    // and construct the window with horizontal scrollbar
    // Still not sure if this will cause any issues with tables or collums.
    ImGuiWindowFlags fwl_imgui_wnd_flags = ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_HorizontalScrollbar;
    ImGui::Begin("FlyWithLua", nullptr, fwl_imgui_wnd_flags);

    if (doBuild) {
        ++stats.builderRuns;
        doBuild(*this);
    }
    ImGui::End();

    ImGui::Render();
    frameRendered = true;
}

void ImGUIWindow::showGUI(bool panelFrame) {
    ImGui::SetCurrentContext(imGuiContext);
    auto &io = ImGui::GetIO();

    ImDrawData *drawData = ImGui::GetDrawData();

    if (panelFrame) {
        bool rebuilt = false;
        if (frameRendered && drawData != nullptr) {
            // Avoid rendering when minimized, scale coordinates for retina displays
            // (screen coordinates != framebuffer coordinates).
            drawData->ScaleClipRects(io.DisplayFramebufferScale);
            rebuildPanelMesh(drawData);
            frameRendered = false;
            rebuilt = true;
        }
        if (!rebuilt && panelMesh.valid) {
            ++stats.cachedDraws;
        }
        showPanelGUI();
        return;
    }

    if (drawData == nullptr) {
        return;
    }

    // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
    drawData->ScaleClipRects(io.DisplayFramebufferScale);

    // We are using the OpenGL fixed pipeline because messing with the
    // shader-state in X-Plane is not very well documented, but using the fixed
    // function pipeline is.

    // 1TU + Alpha settings, no depth, no fog.
    XPLMSetGraphicsState(0, 1, 0, 1, 1, 0, 0);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TRANSFORM_BIT);
    glDisable(GL_CULL_FACE);
    glEnable(GL_SCISSOR_TEST);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnable(GL_TEXTURE_2D);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glScalef(1.0f, -1.0f, 1.0f);
    glTranslatef(static_cast<GLfloat>(mLeft), static_cast<GLfloat>(-mTop), 0.0f);

    // Render command lists
    for (int n = 0; n < drawData->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = drawData->CmdLists[n];
        const ImDrawVert* vtx_buffer = cmd_list->VtxBuffer.Data;
        const ImDrawIdx* idx_buffer = cmd_list->IdxBuffer.Data;
        glVertexPointer(2, GL_FLOAT, sizeof(ImDrawVert), (const GLvoid*)((const char*)vtx_buffer + IM_OFFSETOF(ImDrawVert, pos)));
        glTexCoordPointer(2, GL_FLOAT, sizeof(ImDrawVert), (const GLvoid*)((const char*)vtx_buffer + IM_OFFSETOF(ImDrawVert, uv)));
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(ImDrawVert), (const GLvoid*)((const char*)vtx_buffer + IM_OFFSETOF(ImDrawVert, col)));

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback) {
                pcmd->UserCallback(cmd_list, pcmd);
            } else {
                glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->TextureId);

                // Scissors work in viewport space - must translate the coordinates from ImGui -> Boxels, then Boxels -> Native.
                //FIXME: it must be possible to apply the scale+transform manually to the projection matrix so we don't need to doublestep.
                int bTop, bLeft, bRight, bBottom;
                translateImguiToBoxel(pcmd->ClipRect.x, pcmd->ClipRect.y, bLeft, bTop);
                translateImguiToBoxel(pcmd->ClipRect.z, pcmd->ClipRect.w, bRight, bBottom);
                int nTop, nLeft, nRight, nBottom;
                boxelsToNative(bLeft, bTop, nLeft, nTop);
                boxelsToNative(bRight, bBottom, nRight, nBottom);
                glScissor(nLeft, nBottom, nRight-nLeft, nTop-nBottom);
                glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, idx_buffer);
            }
            idx_buffer += pcmd->ElemCount;
        }
    }

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    // Restore modified state
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glBindTexture(GL_TEXTURE_2D, 0);
    glPopAttrib();
    glPopClientAttrib();
}

void ImGUIWindow::showPanelGUI() {
    if (!panelMesh.valid || panelMesh.vertices.empty() || panelMesh.indices.empty() ||
        panelMesh.drawCalls.empty() || !panelFontTexture) {
        return;
    }

    static_assert(sizeof(ImDrawIdx) == sizeof(uint16_t),
                  "Panel Graphics ImGui helper requires 16-bit ImDrawIdx");

    XPLMMesh_t mesh{};
    mesh.vertex_count = static_cast<int>(panelMesh.vertices.size() / 5u);
    mesh.vertices = panelMesh.vertices.data();
    mesh.index_count = static_cast<int>(panelMesh.indices.size());
    mesh.indices = panelMesh.indices.data();
    if (!flywithlua::panel::drawCalls(mesh, panelMesh.drawCalls)) {
        flywithlua::panel::disableForSession("Panel Graphics ImGui draw failed");
    }
}

void ImGUIWindow::rebuildPanelMesh(const ImDrawData* drawData) {
    ++stats.meshRebuilds;
    const PanelTextureResolver resolveTexture = [](void* texture) {
        return panelTextureForLegacyID(texture);
    };
    if (!rebuildImGuiPanelMesh(drawData, panelFontTexture, panelMesh, resolveTexture)) {
        throw std::runtime_error("Invalid ImGui Panel Graphics mesh");
    }
}

bool ImGUIWindow::onClick(int x, int y, XPLMMouseStatus status) {
    ImGui::SetCurrentContext(imGuiContext);
    ImGuiIO& io = ImGui::GetIO();

    updateMousePosition(x, y);

    switch (status) {
    case xplm_MouseDown:
    case xplm_MouseDrag:
        io.MouseDown[0] = true;
        break;
    case xplm_MouseUp:
        io.MouseDown[0] = false;
        break;
    }

    requestRedraw();
    return FloatingWindow::onClick(x, y, status);
}

bool ImGUIWindow::onMouseWheel(int x, int y, int wheel, int clicks) {
    ImGui::SetCurrentContext(imGuiContext);
    ImGuiIO& io = ImGui::GetIO();

    updateMousePosition(x, y);
    switch (wheel) {
    case 0:
        io.MouseWheel = static_cast<float>(clicks);
        break;
    case 1:
        io.MouseWheelH = static_cast<float>(clicks);
        break;
    }

    requestRedraw();
    return FloatingWindow::onMouseWheel(x, y, wheel, clicks);
}

XPLMCursorStatus ImGUIWindow::getCursor(int x, int y) {
    if (updateMousePosition(x, y)) {
        requestRedraw();
    }

    return xplm_CursorDefault;
}

void ImGUIWindow::onKey(char key, XPLMKeyFlags flags, char virtualKey, bool losingFocus) {
    if (losingFocus) {
        return;
    }

    ImGui::SetCurrentContext(imGuiContext);
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) {
        // If you press and hold a key, the flags will actually be down, 0, 0, ..., up
        // So the key always has to be considered as pressed unless the up flag is set
        auto vk = static_cast<unsigned char>(virtualKey);
        io.KeysDown[vk] = (flags & xplm_UpFlag) != xplm_UpFlag;
        io.KeyShift = (flags & xplm_ShiftFlag) == xplm_ShiftFlag;
        io.KeyAlt = (flags & xplm_OptionAltFlag) == xplm_OptionAltFlag;
        io.KeyCtrl = (flags & xplm_ControlFlag) == xplm_ControlFlag;

        if ((flags & xplm_UpFlag) != xplm_UpFlag
            && !io.KeyCtrl
            && !io.KeyAlt
            && std::isprint(key)) {
            char smallStr[] = { key, 0 };
            io.AddInputCharactersUTF8(smallStr);
        }
    }

    requestRedraw();

    FloatingWindow::onKey(key, flags, virtualKey, losingFocus);
}

void ImGUIWindow::translateImguiToBoxel(float inX, float inY, int& outX, int& outY) {
    outX = (int)(mLeft + inX);
    outY = (int)(mTop - inY);
}

bool ImGUIWindow::updateMousePosition(int x, int y) {
    ImGui::SetCurrentContext(imGuiContext);
    ImGuiIO& io = ImGui::GetIO();

    float outX, outY;
    translateToImguiSpace(x, y, outX, outY);
    const bool changed = std::memcmp(&io.MousePos.x, &outX, sizeof(outX)) != 0 ||
                         std::memcmp(&io.MousePos.y, &outY, sizeof(outY)) != 0;
    io.MousePos = ImVec2(outX, outY);
    return changed;
}

void ImGUIWindow::translateToImguiSpace(int inX, int inY, float& outX, float& outY) {
    outX = static_cast<float>(inX - mLeft);
    if (outX < 0.0f || outX > (float)(mRight - mLeft)) {
        outX = -FLT_MAX;
        outY = -FLT_MAX;
        return;
    }
    outY = static_cast<float>(mTop - inY);
    if (outY < 0.0f || outY > (float)(mTop - mBottom)) {
        outX = -FLT_MAX;
        outY = -FLT_MAX;
        return;
    }
}

ImGUIWindow::~ImGUIWindow() {
    ImGui::SetCurrentContext(imGuiContext);
    if (panelFontTexture != nullptr) {
        flywithlua::panel::destroyTexture(panelFontTexture);
        panelFontTexture = nullptr;
    }
    panelMesh.vertices.clear();
    panelMesh.indices.clear();
    panelMesh.drawCalls.clear();
    panelMesh.valid = false;
    ImGui::DestroyContext(imGuiContext);
    if (fontTextureId != 0) {
        glDeleteTextures(1, &fontTextureId);
    }
}

} /* namespace flwnd */
