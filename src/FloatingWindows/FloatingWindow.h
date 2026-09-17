/*
 *   Floating Windows with imgui integration for FlyWithLua
 *   Copyright (C) 2018 Folke Will <folko@solhost.org>
 *   Released as public domain code.
 *
 */
#ifndef _FLOATINGWINDOW_H_
#define _FLOATINGWINDOW_H_

#include <XPLMGraphics.h>
#include <XPLMDisplay.h>
#include <XPLMDataAccess.h>
#include <cstdint>
#include <vector>
#include <string>
#include <functional>

namespace flwnd {

class FloatingWindow {
public:
    using DrawCallback = std::function<void(FloatingWindow &)>;
    using ClickCallback = std::function<void(FloatingWindow &, int, int, XPLMMouseStatus)>;
    using CloseCallback = std::function<void(FloatingWindow &)>;
    using KeyCallback = std::function<void(FloatingWindow &, char, char, XPLMKeyFlags)>;

    FloatingWindow(int winWidth, int winHeight, int winDecoration,
                   bool panelGraphics = false, std::uint64_t ownerScriptId = 0);
    void setDrawCallback(DrawCallback cb);
    void setClickCallback(ClickCallback cb);
    void setCloseCallback(CloseCallback cb);
    void setKeyCallback(KeyCallback cb);

    void setTitle(const char *title);
    void setPosition(int posx, int posy);
    bool isVisible() const;
    void setVisible(bool visible);
    void moveFromOrToVR();
    void requestInputFocus(bool req);
    bool hasInputFocus();
    void reportClose();
    void boxelsToNative(int x, int y, int &outX, int &outY);

    bool isPopped();
    bool isFront();
    void bringToFront();
    void setResizingLimits(int minwidth, int minheight, int maxwidth, int maxheight);
    void setPositioningMode(int pmode, int monindex);
    void setGravity(float inLeftGravity, float inTopGravity, float inRightGravity, float inBottomGravity);
    void setWindowGeometry(int mleft, int mtop, int mright, int mbot);
    void setWindowGeometryOS(int mleft, int mtop, int mright, int mbot);
    void setWindowGeometryVR(int bWidth, int bHeight);
    bool isVR();
    void setIsCmdVisible(int fCondition);
    bool getIsCmdVisible();

    XPLMWindowID getXWindow();
    bool isPanelGraphics() const;
    std::uint64_t ownerScriptId() const;
    virtual ~FloatingWindow();

protected:
    void updateMatrices();
    virtual void onDraw();
    virtual bool onClick(int x, int y, XPLMMouseStatus status);
    virtual bool onRightClick(int x, int y, XPLMMouseStatus status);
    virtual void onKey(char key, XPLMKeyFlags flags, char virtualKey, bool losingFocus);
    virtual XPLMCursorStatus getCursor(int x, int y);
    virtual bool onMouseWheel(int x, int y, int wheel, int clicks);
    
private:
    XPLMWindowID window{};
    int width, height, decoration;
    bool panelGraphicsRequested = false;
    bool panelGraphics = false;
    std::uint64_t ownerScript = 0;
    bool isInVR = false;
    bool panelSuppressedForVR = false;
    bool panelVisibilityBeforeVR = false;

    int saved2DLeft = 0;
    int saved2DTop = 0;
    int saved2DRight = 0;
    int saved2DBottom = 0;
    bool saved2DGeometryValid = false;
    int savedVRWidth = 0;
    int savedVRHeight = 0;
    bool savedVRGeometryValid = false;

    bool isCmdVisible = false;

    XPLMDataRef vrEnabledRef{};
    XPLMDataRef modeliewMatrixRef{};
    XPLMDataRef viewportRef{};
    XPLMDataRef projectionMatrixRef{};

    float mModelView[16]{}, mProjection[16]{};
    int mViewport[4]{};

    DrawCallback onDrawCB;
    ClickCallback onClickCB;
    CloseCallback onCloseCB;
    KeyCallback onKeyCB;
    
    void createWindow(bool usePanelGraphics);
    void recreateWindow(bool usePanelGraphics);
    void applyVRPositioning(bool vrEnabled);
};

} // namespace flwnd

#endif
