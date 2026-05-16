import Foundation

/// C-compatible Swift functions to be called by Lua.
/// Uses the signature: int (*lua_CFunction) (lua_State *L)

@_cdecl("l_get_dataref")
public func l_get_dataref(L: OpaquePointer?) -> Int32 {
    // Get DataRef name from Lua stack
    guard let cName = lua_tolstring(L, 1, nil) else {
        return 0
    }
    let name = String(cString: cName)
    
    // Find DataRef using our Swift wrapper
    guard let dr = DataRef<Double>(name) else {
        lua_pushnil(L)
        return 1
    }
    
    // Push value back to Lua
    if let value = dr.value {
        lua_pushnumber(L, value)
    } else {
        lua_pushnil(L)
    }
    
    return 1
}

@_cdecl("l_set_dataref")
public func l_set_dataref(L: OpaquePointer?) -> Int32 {
    guard let cName = lua_tolstring(L, 1, nil) else { return 0 }
    let name = String(cString: cName)
    let value = lua_tonumber(L, 2)
    
    if let dr = DataRef<Double>(name) {
        dr.value = value
    }
    
    return 0
}

/// Registers the Swift-based module into the Lua state.
@_cdecl("register_swift_bridge")
public func register_swift_bridge(L: OpaquePointer?) {
    // Create 'mac_native' table
    lua_createtable(L, 0, 0) // Equivalent to lua_newtable
    
    // Register get_dataref
    lua_pushcclosure(L, l_get_dataref, 0)
    lua_setfield(L, -2, "get_dataref")
    
    // Register set_dataref
    lua_pushcclosure(L, l_set_dataref, 0)
    lua_setfield(L, -2, "set_dataref")
    
    // Set as global 'mac_native'
    // LUA_GLOBALSINDEX is -10002 in Lua 5.1/LuaJIT
    lua_setfield(L, -10002, "mac_native")
    
    print("FlyWithLua-Mac: 'mac_native' module registered in Lua.")
}
