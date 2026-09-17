#ifndef FLYWITHLUA_PANEL_GRAPHICS_LUA_H
#define FLYWITHLUA_PANEL_GRAPHICS_LUA_H

#include <cstdint>

#include "lua.hpp"

namespace flywithlua::panel {

/** Register the explicit panel_* Lua API. */
void registerLuaFunctions(lua_State* state);

/** Invalidate texture userdata owned by one script, or all scripts for zero. */
void invalidateLuaResources(std::uint64_t ownerScriptId = 0);

} // namespace flywithlua::panel

#endif
