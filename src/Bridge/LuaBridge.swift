import Foundation

/// C-compatible Swift functions to be called by Lua.
/// Uses the signature: int (*lua_CFunction) (lua_State *L)

private struct ScriptLoadFailurePayload: Decodable {
    let fileName: String
    let message: String
}

private func flywithluaFontID(for name: String?) -> XPLMFontID {
    switch name?.lowercased() {
    case "helvetica_10", "helvetica_12", "times_roman_10":
        return XPLMFontID(xplmFont_Basic)
    case "helvetica_18", "times_roman_24":
        return XPLMFontID(xplmFont_Proportional)
    case "times_roman_12", "times_roman_18":
        return XPLMFontID(xplmFont_Proportional)
    default:
        return XPLMFontID(xplmFont_Basic)
    }
}

private func flywithluaCurrentColor() -> [GLfloat] {
    var color: [GLfloat] = [1, 1, 1, 1]
    color.withUnsafeMutableBufferPointer { buffer in
        guard let baseAddress = buffer.baseAddress else { return }
        glGetFloatv(UInt32(GL_CURRENT_COLOR), baseAddress)
    }
    return color
}

private func flywithluaCommandName(from L: OpaquePointer?, index: Int32) -> String? {
    guard let cName = lua_tolstring(L, index, nil) else {
        return nil
    }
    return String(cString: cName)
}

@_cdecl("flywithlua_toggle_window")
public func flywithlua_toggle_window() {
    XPWindowManager.shared.toggleWindow()
}

@_cdecl("flywithlua_show_3jfps_settings")
public func flywithlua_show_3jfps_settings() {
    XPWindowManager.shared.show3jFPSSettings()
}

@_cdecl("flywithlua_update_3jfps_snapshot")
public func flywithlua_update_3jfps_snapshot(_ jsonPayload: UnsafePointer<CChar>?) {
    guard let jsonPayload else { return }
    ThreeJFPSUIState.shared.updateSnapshot(String(cString: jsonPayload))
}

@_cdecl("flywithlua_update_current_altitude")
public func flywithlua_update_current_altitude(_ altitude: Double) {
    guard XPWindowManager.shared.isWindowVisible else { return }
    XPUIState.shared.updateCurrentAltitude(altitude)
}

@_cdecl("flywithlua_update_script_count")
public func flywithlua_update_script_count(_ count: Int32) {
    XPUIState.shared.updateScriptCount(Int(count))
}

@_cdecl("flywithlua_clear_script_load_failures")
public func flywithlua_clear_script_load_failures() {
    XPUIState.shared.clearScriptLoadFailures()
}

@_cdecl("flywithlua_update_script_load_results")
public func flywithlua_update_script_load_results(_ jsonPayload: UnsafePointer<CChar>?) {
    guard let jsonPayload else {
        XPUIState.shared.clearScriptLoadFailures()
        return
    }

    let payload = String(cString: jsonPayload)
    guard let data = payload.data(using: .utf8) else {
        XPLMDebugString("FlyWithLua-Mac Warning: Failed to decode script load results payload.\n")
        XPUIState.shared.clearScriptLoadFailures()
        return
    }

    do {
        let decoded = try JSONDecoder().decode([ScriptLoadFailurePayload].self, from: data)
        let failures = decoded.map { XPUIState.ScriptLoadFailure(fileName: $0.fileName, message: $0.message) }
        XPUIState.shared.updateScriptLoadFailures(failures)
    } catch {
        XPLMDebugString("FlyWithLua-Mac Warning: Failed to parse script load results: " + error.localizedDescription + "\n")
        XPUIState.shared.clearScriptLoadFailures()
    }
}

@_cdecl("flywithlua_update_script_load_summary")
public func flywithlua_update_script_load_summary(_ discovered: Int32,
                                                  _ loaded: Int32,
                                                  _ failed: Int32,
                                                  _ jsonPayload: UnsafePointer<CChar>?) {
    var failures: [XPUIState.ScriptLoadFailure] = []
    if let jsonPayload {
        let payload = String(cString: jsonPayload)
        if let data = payload.data(using: .utf8) {
            do {
                let decoded = try JSONDecoder().decode([ScriptLoadFailurePayload].self, from: data)
                failures = decoded.map { XPUIState.ScriptLoadFailure(fileName: $0.fileName, message: $0.message) }
            } catch {
                XPLMDebugString("FlyWithLua-Mac Warning: Failed to parse script load summary: " + error.localizedDescription + "\n")
            }
        }
    }

    XPUIState.shared.updateScriptLoadSummary(discovered: Int(discovered),
                                             loaded: Int(loaded),
                                             failed: Int(failed),
                                             failures: failures)
}

@_cdecl("flywithlua_update_last_log_message")
public func flywithlua_update_last_log_message(_ message: UnsafePointer<CChar>?) {
    guard let message else { return }
    XPUIState.shared.updateLastLogMessage(String(cString: message))
}

@_cdecl("l_get_dataref")
public func l_get_dataref(L: OpaquePointer?) -> Int32 {
    guard let cName = lua_tolstring(L, 1, nil) else {
        return 0
    }
    let name = String(cString: cName)

    let index: Int?
    if lua_gettop(L) >= 2, lua_isnumber(L, 2) != 0 {
        index = Int(lua_tointeger(L, 2))
    } else {
        index = nil
    }

    guard let value = flywithluaReadDataRef(path: name, index: index) else {
        lua_pushnil(L)
        return 1
    }

    value.push(to: L)
    return 1
}

@_cdecl("l_set_dataref")
public func l_set_dataref(L: OpaquePointer?) -> Int32 {
    guard let cName = lua_tolstring(L, 1, nil) else { return 0 }
    let name = String(cString: cName)

    let index: Int?
    if lua_gettop(L) >= 3, lua_isnumber(L, 3) != 0 {
        index = Int(lua_tointeger(L, 3))
    } else {
        index = nil
    }

    let inputValue: LuaDataValue?
    if lua_type(L, 2) == LUA_TSTRING {
        let byteCount = Int(lua_objlen(L, 2))
        guard let cString = lua_tolstring(L, 2, nil) else {
            flywithluaLogDataRefIssue("Unsupported Lua string value for DataRef: " + name)
            return 0
        }
        inputValue = .string(Data(bytes: UnsafeRawPointer(cString), count: byteCount))
    } else if lua_type(L, 2) == LUA_TBOOLEAN {
        inputValue = .boolean(lua_toboolean(L, 2) != 0)
    } else if lua_isnumber(L, 2) != 0 {
        inputValue = .number(lua_tonumber(L, 2))
    } else {
        inputValue = nil
    }

    guard let value = inputValue else {
        flywithluaLogDataRefIssue("Unsupported Lua value for DataRef: " + name)
        return 0
    }

    _ = flywithluaWriteDataRef(path: name, value: value, index: index)
    return 0
}

@_cdecl("l_log_msg")
public func l_log_msg(L: OpaquePointer?) -> Int32 {
    guard let cMsg = lua_tolstring(L, 1, nil) else { return 0 }
    let msg = String(cString: cMsg)
    XPLMDebugString("FlyWithLua-Mac: " + msg + "\n")
    return 0
}

@_cdecl("l_command_once")
public func l_command_once(L: OpaquePointer?) -> Int32 {
    guard let name = flywithluaCommandName(from: L, index: 1) else {
        return 0
    }

    guard let command = XPLMFindCommand(name) else {
        flywithluaLogDataRefIssue("Command not found: " + name)
        return 0
    }

    XPLMCommandOnce(command)
    return 0
}

@_cdecl("l_draw_string")
public func l_draw_string(L: OpaquePointer?) -> Int32 {
    guard let cText = lua_tolstring(L, 3, nil) else {
        return 0
    }

    let fontName: String?
    if lua_gettop(L) >= 4, let cFont = lua_tolstring(L, 4, nil) {
        fontName = String(cString: cFont)
    } else {
        fontName = "Helvetica_12"
    }

    let fontID = flywithluaFontID(for: fontName)
    var color: [GLfloat]
    if lua_gettop(L) >= 8,
       lua_isnumber(L, 5) != 0,
       lua_isnumber(L, 6) != 0,
       lua_isnumber(L, 7) != 0,
       lua_isnumber(L, 8) != 0 {
        color = [
            GLfloat(lua_tonumber(L, 5)),
            GLfloat(lua_tonumber(L, 6)),
            GLfloat(lua_tonumber(L, 7)),
            GLfloat(lua_tonumber(L, 8))
        ]
    } else {
        color = flywithluaCurrentColor()
    }

    let x = Int32(lua_tointeger(L, 1))
    let y = Int32(lua_tointeger(L, 2))
    let drawableText = UnsafeMutablePointer(mutating: cText)

    color.withUnsafeMutableBufferPointer { buffer in
        guard let baseAddress = buffer.baseAddress else { return }
        XPLMDrawString(baseAddress, Int32(x), Int32(y), drawableText, nil, fontID)
    }

    return 0
}

@_cdecl("l_measure_string")
public func l_measure_string(L: OpaquePointer?) -> Int32 {
    guard let cText = lua_tolstring(L, 1, nil) else {
        lua_pushnumber(L, 0)
        return 1
    }

    let fontName: String?
    if lua_gettop(L) >= 2, let cFont = lua_tolstring(L, 2, nil) {
        fontName = String(cString: cFont)
    } else {
        fontName = "Helvetica_12"
    }

    let fontID = flywithluaFontID(for: fontName)
    let width = XPLMMeasureString(fontID, cText, Int32(strlen(cText)))
    lua_pushnumber(L, Double(width))
    return 1
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
    
    // Register log_msg
    lua_pushcclosure(L, l_log_msg, 0)
    lua_setfield(L, -2, "log_msg")

    // Register command_once
    lua_pushcclosure(L, l_command_once, 0)
    lua_setfield(L, -2, "command_once")

    // Register draw_string
    lua_pushcclosure(L, l_draw_string, 0)
    lua_setfield(L, -2, "draw_string")

    // Register measure_string
    lua_pushcclosure(L, l_measure_string, 0)
    lua_setfield(L, -2, "measure_string")
    
    // Set as global 'mac_native'
    // LUA_GLOBALSINDEX is -10002 in Lua 5.1/LuaJIT
    lua_setfield(L, -10002, "mac_native")
    
    print("FlyWithLua-Mac: 'mac_native' module registered in Lua.")
}
