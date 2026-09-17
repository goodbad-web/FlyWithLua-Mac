/*
 *   Floating Windows with imgui integration for FlyWithLua
 *   Copyright (C) 2018 Folke Will <folko@solhost.org>
 *   Released as public domain code.
 *
 */

/*
 * portions copied with permission from https://github.com/kuroneko/xsb_public
 *
 * ImgWindow.h
 *
 * Integration for dear imgui into X-Plane.
 *
 * Copyright (C) 2018, Christopher Collins
 */
#ifndef FLOATINGWINDOWS_IMGUIINTEGRATION_H_
#define FLOATINGWINDOWS_IMGUIINTEGRATION_H_

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#include <memory>
#include <string>
#include "FloatingWindow.h"
#include "imgui/imgui.h"
#include "lua.hpp"

namespace flwnd {

class ImGUIWindow: public FloatingWindow {
public:
    using BuildCallback = std::function<void(ImGUIWindow &)>;
    using ErrorHandler = std::function<void(const std::string &)>;

    ImGUIWindow(int width, int height, int decoration, std::uint64_t ownerScriptId = 0);
    void setErrorHandler(ErrorHandler eh);
    void setBuildCallback(BuildCallback cb);
    ~ImGUIWindow();
protected:
    void onDraw() override;
    bool onClick(int x, int y, XPLMMouseStatus status) override;
    bool onMouseWheel(int x, int y, int wheel, int clicks) override;
    XPLMCursorStatus getCursor(int x, int y) override;
    void onKey(char key, XPLMKeyFlags flags, char virtualKey, bool losingFocus) override;
private:
    GLuint fontTextureId{};
    void* panelFontTexture{};
    ImGuiContext *imGuiContext{};
    int mLeft{}, mTop{}, mRight{}, mBottom{};
    ErrorHandler onError;
    BuildCallback doBuild;
    bool stopped = false;
    bool panelRenderer = false;

    void buildGUI();
    void showGUI(bool panelFrame);
    void showPanelGUI();
    bool syncFontTexture(bool usePanelGraphics);

    void translateImguiToBoxel(float inX, float inY, int &outX, int &outY);
    void translateToImguiSpace(int inX, int inY, float &outX, float &outY);
};

} /* namespace flwnd */

#endif /* FLOATINGWINDOWS_IMGUIINTEGRATION_H_ */
