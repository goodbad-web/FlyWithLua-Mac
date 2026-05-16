#include "FlyWithLua.hpp"
#include <iostream>

lua_State* L = nullptr;

PLUGIN_API int XPluginStart(
						char *		outName,
						char *		outSig,
						char *		outDesc)
{
	strcpy(outName, "FlyWithLua-Mac");
	strcpy(outSig, "com.goodbad-web.flywithlua_mac");
	strcpy(outDesc, "A modernized, macOS-optimized FlyWithLua plugin.");

	// Initialize Lua State
	L = luaL_newstate();
	if (L) {
		luaL_openlibs(L);
		XPLMDebugString("FlyWithLua-Mac: Lua state initialized.\n");
	} else {
		XPLMDebugString("FlyWithLua-Mac: Failed to initialize Lua state.\n");
		return 0;
	}

	return 1;
}

PLUGIN_API void	XPluginStop(void)
{
	if (L) {
		lua_close(L);
		L = nullptr;
		XPLMDebugString("FlyWithLua-Mac: Lua state closed.\n");
	}
}

PLUGIN_API void XPluginDisable(void) { }
PLUGIN_API int XPluginEnable(void) { return 1; }
PLUGIN_API void XPluginReceiveMessage(XPLMPluginID inFrom, int inMsg, void * inParam) { }
