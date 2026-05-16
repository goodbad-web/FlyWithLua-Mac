#ifndef FlyWithLua_Bridging_Header_h
#define FlyWithLua_Bridging_Header_h

// X-Plane SDK Headers
#include "XPLM/XPLMDefs.h"
#include "XPLM/XPLMDataAccess.h"
#include "XPLM/XPLMUtilities.h"
#include "XPLM/XPLMPlugin.h"
#include "XPLM/XPLMDisplay.h"
#include "XPLM/XPLMGraphics.h"
#include "XPLM/XPLMProcessing.h"
#include "XPLM/XPLMCamera.h"
#include "XPLM/XPLMNavigation.h"

// OpenGL Headers (for texture upload)
#ifdef __cplusplus
extern "C" {
#endif
#include <OpenGL/gl.h>

// Lua Headers
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#ifdef __cplusplus
}
#endif

#endif /* FlyWithLua_Bridging_Header_h */
