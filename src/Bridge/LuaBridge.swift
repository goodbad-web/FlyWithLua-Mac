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

/// C ABI used by native in-sim overlays that need the same CoreText/OpenGL
/// renderer as the Lua HUD path.
@_cdecl("flywithlua_draw_hidpi_text")
public func flywithlua_draw_hidpi_text(_ x: Int32,
                                       _ y: Int32,
                                       _ text: UnsafePointer<CChar>?,
                                       _ logicalSize: Float,
                                       _ family: UnsafePointer<CChar>?,
                                       _ weight: Int32) -> Int32 {
    guard let text,
          logicalSize.isFinite,
          logicalSize > 0 else {
        return 0
    }

    if flywithlua_panel_draw_hidpi_text(x, y, text, logicalSize, family, weight) != 0 {
        return 1
    }
    if flywithlua_panel_is_drawing() != 0 {
        return 0
    }

    let familyName = family.map { String(cString: $0) } ?? "sf_pro_text"
    let rendered = HUDTextRenderer.shared.draw(
        text: String(cString: text),
        x: CGFloat(x),
        y: CGFloat(y),
        logicalSize: CGFloat(logicalSize),
        family: familyName,
        weight: Int(weight)
    )
    return rendered ? 1 : 0
}

@_cdecl("flywithlua_measure_hidpi_text")
public func flywithlua_measure_hidpi_text(_ text: UnsafePointer<CChar>?,
                                          _ logicalSize: Float,
                                          _ family: UnsafePointer<CChar>?,
                                          _ weight: Int32) -> Double {
    guard let text,
          logicalSize.isFinite,
          logicalSize > 0 else {
        return -1
    }

    let panelWidth = flywithlua_panel_measure_hidpi_text(text, logicalSize, family, weight)
    if panelWidth.isFinite, panelWidth >= 0 {
        return panelWidth
    }

    let familyName = family.map { String(cString: $0) } ?? "sf_pro_text"
    guard let width = HUDTextRenderer.shared.measure(
        text: String(cString: text),
        logicalSize: CGFloat(logicalSize),
        family: familyName,
        weight: Int(weight)
    ) else {
        return -1
    }
    return Double(width)
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

    let fontName: String
    if lua_gettop(L) >= 4, let cFont = lua_tolstring(L, 4, nil) {
        fontName = String(cString: cFont)
    } else {
        fontName = "Helvetica_12"
    }

    let explicitColor = lua_gettop(L) >= 8 &&
        lua_isnumber(L, 5) != 0 && lua_isnumber(L, 6) != 0 &&
        lua_isnumber(L, 7) != 0 && lua_isnumber(L, 8) != 0
    let fontID = flywithluaFontID(for: fontName)
    var color: [GLfloat]
    if explicitColor {
        color = [
            GLfloat(lua_tonumber(L, 5)),
            GLfloat(lua_tonumber(L, 6)),
            GLfloat(lua_tonumber(L, 7)),
            GLfloat(lua_tonumber(L, 8))
        ]
    } else {
        color = flywithlua_panel_is_drawing() != 0
            ? [1, 1, 1, 1]
            : flywithluaCurrentColor()
    }

    let x = Int32(lua_tointeger(L, 1))
    let y = Int32(lua_tointeger(L, 2))
    let drawableText = UnsafeMutablePointer(mutating: cText)

    if flywithlua_panel_is_drawing() != 0 {
        let rendered = fontName.withCString { fontPointer in
            if explicitColor {
                return color.withUnsafeBufferPointer { buffer in
                    flywithlua_panel_draw_legacy_text(x, y, cText, fontPointer,
                                                      buffer.baseAddress)
                }
            }
            return flywithlua_panel_draw_legacy_text(x, y, cText, fontPointer, nil)
        }
        if rendered != 0 {
            return 0
        }
        return 0
    }

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

    let fontName: String
    if lua_gettop(L) >= 2, let cFont = lua_tolstring(L, 2, nil) {
        fontName = String(cString: cFont)
    } else {
        fontName = "Helvetica_12"
    }

    let panelWidth = fontName.withCString { fontPointer in
        flywithlua_panel_measure_legacy_text(cText, fontPointer)
    }
    if panelWidth.isFinite, panelWidth >= 0 {
        lua_pushnumber(L, panelWidth)
        return 1
    }

    let fontID = flywithluaFontID(for: fontName)
    let width = XPLMMeasureString(fontID, cText, Int32(strlen(cText)))
    lua_pushnumber(L, Double(width))
    return 1
}

@_cdecl("l_elapsed_time")
public func l_elapsed_time(L: OpaquePointer?) -> Int32 {
    lua_pushnumber(L, Double(XPLMGetElapsedTime()))
    return 1
}

private func flywithluaTextArgument(from L: OpaquePointer?, index: Int32) -> String? {
    guard let cString = lua_tolstring(L, index, nil) else {
        return nil
    }
    return String(cString: cString)
}

private func flywithluaTextSize(from L: OpaquePointer?, index: Int32) -> CGFloat? {
    guard lua_gettop(L) >= index, lua_isnumber(L, index) != 0 else {
        return nil
    }
    let value = CGFloat(lua_tonumber(L, index))
    return value.isFinite && value > 0 ? value : nil
}

private func flywithluaTextCoordinate(from L: OpaquePointer?, index: Int32) -> CGFloat? {
    guard lua_gettop(L) >= index, lua_isnumber(L, index) != 0 else {
        return nil
    }
    let value = CGFloat(lua_tonumber(L, index))
    return value.isFinite ? value : nil
}

private func flywithluaTextWeight(from L: OpaquePointer?, index: Int32) -> Int {
    guard lua_gettop(L) >= index, lua_isnumber(L, index) != 0 else {
        return 400
    }
    let value = Int(lua_tointeger(L, index))
    return min(max(value, 100), 900)
}

@_cdecl("l_draw_hidpi_string")
public func l_draw_hidpi_string(L: OpaquePointer?) -> Int32 {
    guard let x = flywithluaTextCoordinate(from: L, index: 1),
          let y = flywithluaTextCoordinate(from: L, index: 2),
          let text = flywithluaTextArgument(from: L, index: 3),
          let logicalSize = flywithluaTextSize(from: L, index: 4) else {
        lua_pushboolean(L, 0)
        lua_pushboolean(L, 0)
        return 2
    }

    let family = flywithluaTextArgument(from: L, index: 5) ?? "sf_pro_text"
    let weight = flywithluaTextWeight(from: L, index: 6)
    let panelDrawing = flywithlua_panel_is_drawing() != 0
    if panelDrawing {
        let panelRendered = text.withCString { textPointer in
            family.withCString { familyPointer in
                flywithlua_panel_draw_hidpi_text(
                    Int32(x),
                    Int32(y),
                    textPointer,
                    Float(logicalSize),
                    familyPointer,
                    Int32(weight)
                )
            }
        }
        lua_pushboolean(L, panelRendered != 0 ? 1 : 0)
        lua_pushboolean(L, 0)
        return 2
    }
    let result = HUDTextRenderer.shared.drawResult(
        text: text,
        x: x,
        y: y,
        logicalSize: logicalSize,
        family: family,
        weight: weight
    )
    lua_pushboolean(L, result == .rendered ? 1 : 0)
    lua_pushboolean(L, result == .deferred ? 1 : 0)
    return 2
}

@_cdecl("l_measure_hidpi_string")
public func l_measure_hidpi_string(L: OpaquePointer?) -> Int32 {
    guard let text = flywithluaTextArgument(from: L, index: 1),
          let logicalSize = flywithluaTextSize(from: L, index: 2) else {
        lua_pushnil(L)
        return 1
    }

    let family = flywithluaTextArgument(from: L, index: 3) ?? "sf_pro_text"
    let weight = flywithluaTextWeight(from: L, index: 4)
    let panelWidth = text.withCString { textPointer in
        family.withCString { familyPointer in
            flywithlua_panel_measure_hidpi_text(textPointer, Float(logicalSize), familyPointer, Int32(weight))
        }
    }
    if panelWidth.isFinite, panelWidth >= 0 {
        lua_pushnumber(L, panelWidth)
        return 1
    }
    guard let width = HUDTextRenderer.shared.measure(
        text: text,
        logicalSize: logicalSize,
        family: family,
        weight: weight
    ), width.isFinite else {
        lua_pushnil(L)
        return 1
    }

    lua_pushnumber(L, Double(width))
    return 1
}

@_cdecl("l_begin_hidpi_frame")
public func l_begin_hidpi_frame(L: OpaquePointer?) -> Int32 {
    if flywithlua_panel_is_drawing() != 0 {
        lua_pushboolean(L, 1)
        return 1
    }
    lua_pushboolean(L, HUDTextRenderer.shared.beginFrame() ? 1 : 0)
    return 1
}

@_cdecl("l_end_hidpi_frame")
public func l_end_hidpi_frame(L: OpaquePointer?) -> Int32 {
    if flywithlua_panel_is_drawing() != 0 {
        lua_pushboolean(L, 1)
        return 1
    }
    lua_pushboolean(L, HUDTextRenderer.shared.endFrame() ? 1 : 0)
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

    lua_pushcclosure(L, l_elapsed_time, 0)
    lua_setfield(L, -2, "elapsed_time")

    // Register high-DPI text functions. The legacy functions above remain
    // unchanged for existing Lua scripts.
    lua_pushcclosure(L, l_draw_hidpi_string, 0)
    lua_setfield(L, -2, "draw_hidpi_string")

    lua_pushcclosure(L, l_measure_hidpi_string, 0)
    lua_setfield(L, -2, "measure_hidpi_string")

    lua_pushcclosure(L, l_begin_hidpi_frame, 0)
    lua_setfield(L, -2, "begin_hidpi_frame")

    lua_pushcclosure(L, l_end_hidpi_frame, 0)
    lua_setfield(L, -2, "end_hidpi_frame")
    
    // Set as global 'mac_native'
    // LUA_GLOBALSINDEX is -10002 in Lua 5.1/LuaJIT
    lua_setfield(L, -10002, "mac_native")
    
    print("FlyWithLua-Mac: 'mac_native' module registered in Lua.")
}
