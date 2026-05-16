import Foundation

/// Registers the Swift-based module into the Lua state.
@_cdecl("register_swift_bridge")
public func register_swift_bridge(L: OpaquePointer?) {
    // Create 'mac_native' table
    lua_newtable(L)
    
    // Register get_dataref
    lua_pushcclosure(L, { l_get_dataref(L: $0) }, 0)
    lua_setfield(L, -2, "get_dataref")
    
    // Register set_dataref
    lua_pushcclosure(L, { l_set_dataref(L: $0) }, 0)
    lua_setfield(L, -2, "set_dataref")
    
    // Set as global 'mac_native'
    lua_setglobal(L, "mac_native")
    
    print("FlyWithLua-Mac: 'mac_native' module registered in Lua.")
}

// Note: Some luaL_ functions are macros and not available in Swift.
// We use the underlying lua_ functions instead.
