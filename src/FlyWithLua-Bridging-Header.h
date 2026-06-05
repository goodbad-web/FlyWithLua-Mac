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

void flywithlua_reload_scripts(void);
void flywithlua_update_script_count(int count);
void flywithlua_clear_script_load_failures(void);
void flywithlua_update_script_load_results(const char* jsonPayload);
void flywithlua_update_current_altitude(double altitude);
void flywithlua_update_last_log_message(const char* message);
void flywithlua_toggle_window(void);

#include <OpenGL/gl.h>

// Lua Headers
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#ifdef __cplusplus
}
#endif

#endif /* FlyWithLua_Bridging_Header_h */
