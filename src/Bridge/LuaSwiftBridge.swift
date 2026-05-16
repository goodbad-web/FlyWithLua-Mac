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
