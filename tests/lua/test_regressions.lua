local root = assert(os.getenv("FWL_TEST_ROOT"), "FWL_TEST_ROOT is required")
local tmp = assert(os.getenv("FWL_TEST_TMP"), "FWL_TEST_TMP is required")

local function new_environment()
	local env = {}
	setmetatable(env, { __index = _G })
	env._G = env
	return env
end

local function load_in_environment(path, env, ...)
	local chunk, err = loadfile(path)
	assert(chunk, err)
	setfenv(chunk, env)
	return chunk(...)
end

local function test_bravo_dial()
	local env = new_environment()
	local battery = { [0] = 0 }
	local left_dial = { [0] = nil }
	local commands = {}

	env.dataref_table = function(path)
		assert(path == "sim/cockpit2/electrical/battery_on")
		return battery
	end
	env.create_dataref_table = function(path, ref_type)
		assert(path == "FlyWithLua/BravoDial/left_dial")
		assert(ref_type == "Data")
		return left_dial
	end
	env.create_command = function() end
	env.command_once = function(command)
		commands[#commands + 1] = command
	end
	env.XPLMSpeakString = function() end

	load_in_environment(root .. "/FlyWithLua/Scripts/Bravo_Dial_Set.lua", env)
	assert(left_dial[0] == "Alt")

	env.cmdLeftDial_IAS()
	assert(left_dial[0] == "Alt")
	assert(#commands == 0)

	battery[0] = 1
	env.cmdLeftDial_IAS()
	assert(left_dial[0] == "IAS")
	env.cmdRightDial_up()
	assert(commands[#commands] == "sim/autopilot/airspeed_up")

	battery[0] = 0
	env.cmdRightDial_dn()
	assert(#commands == 1)
end

local function test_b58_defaults()
	local env = new_environment()
	local registered_paths = {}
	local often_code

	env.PLANE_ICAO = "BE58"
	env.XPLMFindDataRef = function(path)
		return path
	end
	env.dataref = function(name, path)
		registered_paths[name] = path
		env[name] = 0
	end
	env.do_often = function(code)
		often_code = code
	end
	env.logMsg = function() end

	load_in_environment(root .. "/FlyWithLua/Scripts/B58.01.logic.lua", env)
	assert(registered_paths.pfd_wind_style == "sim/cockpit2/EFIS/wind_vector_style")
	assert(env.g1000_esp_enabled == 0)
	assert(env.pfd_wind_style == 2)
	assert(env.pfd_brg1_source == 3)
	assert(env.pfd_svt_enabled == 1)
	assert(often_code == "set_be58_defaults()")

	env.g1000_esp_enabled = 1
	local callback = assert(loadstring(often_code))
	setfenv(callback, env)
	callback()
	assert(env.g1000_esp_enabled == 0)

	local unavailable = new_environment()
	local unavailable_logs = {}
	unavailable.PLANE_ICAO = "BE58"
	unavailable.XPLMFindDataRef = function()
		return nil
	end
	unavailable.dataref = function(name)
		unavailable[name] = 0
	end
	unavailable.do_often = function(code)
		unavailable.often_code = code
	end
	unavailable.logMsg = function(message)
		unavailable_logs[#unavailable_logs + 1] = message
	end
	load_in_environment(root .. "/FlyWithLua/Scripts/B58.01.logic.lua", unavailable)
	assert(unavailable.pfd_wind_style == nil)
	assert(unavailable.pfd_brg1_source == 3)
	assert(unavailable.often_code == "set_be58_defaults()")
	assert(#unavailable_logs == 1)

	local non_be58 = new_environment()
	non_be58.PLANE_ICAO = "C172"
	non_be58.do_often = function()
		error("do_often must not be registered for non-BE58 aircraft")
	end
	non_be58.dataref = function()
		error("DataRefs must not be registered for non-BE58 aircraft")
	end
	load_in_environment(root .. "/FlyWithLua/Scripts/B58.01.logic.lua", non_be58)
end

local function test_landing_rate()
	local env = new_environment()
	env.SUPPORTS_FLOATING_WINDOWS = true
	env.SCREEN_WIDTH = 1920
	env.SCREEN_HIGHT = 1080
	env.PLANE_ICAO = "TEST"
	env.graphics = setmetatable({}, { __index = function() return function() end end })
	env.require = function(name)
		if name == "graphics" then
			return env.graphics
		end
		return require(name)
	end
	env.dataref = function(name)
		env[name] = 0
	end
	env.do_every_draw = function() end
	env.do_often = function() end
	env.add_macro = function() end
	env.logMsg = function() end
	env.XPLMSetGraphicsState = function() end
	env.measure_string = function(text)
		return #tostring(text)
	end
	env.draw_string_Helvetica_10 = function() end
	env.draw_string_Helvetica_12 = function() end
	env.draw_string_Helvetica_18 = function() end
	env.float_wnd_create = function() return {} end
	env.float_wnd_destroy = function() end
	env.float_wnd_set_title = function() end
	env.float_wnd_set_imgui_builder = function() end
	env.imgui = setmetatable({}, { __index = function() return function() end end })

	load_in_environment(root .. "/FlyWithLua/Scripts/LandingRate.lua", env)
	env.lrl_popupState = env.lrl_ARMED
	env.lrl_agl = 100
	env.lrl_localtime = 0
	env.lrl_boolSimPaused = 0
	env.lrl_boolInReplay = 0
	assert(pcall(env.lrl_loopCallback))

	env.lrl_agl = 99
	env.lrl_localtime = 1
	assert(pcall(env.lrl_loopCallback))

	env.lrl_popupText[1] = "O'Reilly"
	env.lrl_logDisplayOn = true
	env.lrl_showUntil = os.clock() + 10
	assert(pcall(env.lrl_loopCallback))
end

local function install_module_environment(env, module_name)
	env.package = { seeall = function() end }
	env.module = function(name)
		local module_table = {}
		setmetatable(module_table, { __index = env })
		env[name] = module_table
		setfenv(2, module_table)
		return module_table
	end
	env.require = function(name)
		if name == "graphics" then
			return env.graphics
		end
		if name == "LuaXML_lib" then
			env.xml = env.xml or {}
			return env.xml
		end
		if name == "ffi" then
			return require("ffi")
		end
		return require(name)
	end
	return module_name
end

local function test_user_waypoint()
	local env = new_environment()
	local created = 0
	env.SUPPORTS_FLOATING_WINDOWS = true
	env.LATITUDE = 35
	env.LONGITUDE = 139
	env.add_macro = function() end
	env.create_command = function() end
	env.float_wnd_create = function()
		created = created + 1
		return { id = created }
	end
	env.float_wnd_destroy = function() end
	env.float_wnd_set_title = function() end
	env.float_wnd_set_imgui_builder = function() end
	env.float_wnd_set_onclose = function() end
	env.logMsg = function() end
	env.imgui = setmetatable({
		constant = { Col = { Text = 0 } },
	}, { __index = function() return function() end end })

	load_in_environment(root .. "/FlyWithLua/Scripts/USER_WAYPOINT_ver1.lua", env)
	env.usr_point_show_wnd()
	local first_window = env.usr_point_wnd
	env.usr_point_show_wnd()
	assert(created == 1)
	assert(env.usr_point_wnd == first_window)
	assert(pcall(env.usr_point_on_build, first_window, 0, 0))

	env.closed_usr_point_wnd(first_window)
	assert(env.usr_point_wnd == nil)
	env.usr_point_show_wnd()
	assert(created == 2)
end

local function test_hud()
	local env = new_environment()
	env.SCREEN_WIDTH = 1920
	env.SCREEN_HIGHT = 1080
	env.SCRIPT_DIRECTORY = tmp .. "/"
	env.graphics = {}
	env.require = function(name)
		if name == "graphics" then return env.graphics end
		return require(name)
	end
	env.do_every_draw = function() end
	env.do_on_mouse_click = function() end
	env.do_on_mouse_wheel = function() end
	env.dataref = function(name)
		env[name] = 0
	end
	env.dofile = function(path)
		local chunk = assert(loadfile(path))
		setfenv(chunk, env)
		return chunk()
	end
	install_module_environment(env, "HUD")
	local chunk = assert(loadfile(root .. "/FlyWithLua/Modules/HUD.lua"))
	setfenv(chunk, env)
	chunk("HUD")

	env.HUD.begin_HUD(0, 0, 100, 50, "MY_LITTLE_HUD")
	env.HUD.create_element("quoted", nil, nil, nil, nil)
	env.HUD.draw_string(nil, nil, 10, "quote \" and slash\\")
	assert(env.HUD.end_HUD())

	local file = assert(io.open(tmp .. "/HUD_module_MY_LITTLE_HUD_autogen.txt", "r"))
	local generated = file:read("*a")
	file:close()
	assert(generated:find("quote", 1, true))
end

local function test_sma()
	local env = new_environment()
	env.graphics = {}
	env.dataref = function(name)
		env[name] = 0
	end
	env.logs = {}
	env.logMsg = function(message)
		env.logs[#env.logs + 1] = message
	end
	install_module_environment(env, "SMA_smoother")
	local chunk = assert(loadfile(root .. "/FlyWithLua/Modules/SMA_smoother.lua"))
	setfenv(chunk, env)
	chunk("SMA_smoother")
	local previous_dataref = _G.dataref
	local previous_do_every_frame = _G.do_every_frame
	_G.dataref = function(name)
		_G[name] = 0
	end
	_G.do_every_frame = function(code)
		_G.last_frame_callback = code
	end
	assert(env.SMA_smoother.create_SMA(1, 3), table.concat(env.logs, "\n"))
	assert(_G.last_frame_callback == "calculate_axis_1()")
	assert(env.SMA_smoother.create_SMA(-1, 3) == false)
	assert(env.SMA_smoother.create_SMA(1, 0) == false)
	_G.dataref = previous_dataref
	_G.do_every_frame = previous_do_every_frame
	_G.last_frame_callback = nil
end

local function test_luaxml_open_failure()
	local env = new_environment()
	env.xml = {}
	install_module_environment(env, "xml")
	local chunk = assert(loadfile(root .. "/FlyWithLua/Modules/LuaXml.lua"))
	setfenv(chunk, env)
	chunk("xml")
	assert(env.xml.save({}, tmp .. "/missing-directory/output.xml") == false)
end

local function test_jjjlib_patch_safety()
	local env = new_environment()
	env.graphics = setmetatable({}, { __index = function() return function() end end })
	env.SCRIPT_DIRECTORY = tmp .. "/"
	env.AIRCRAFT_PATH = tmp .. "/aircraft/"
	env.SYSTEM_ARCHITECTURE = 64
	env.SYSTEM = "MAC"
	env.dataref = function(name)
		env[name] = 0
	end
	env.logMsg = function() end
	env.do_every_draw = function() end
	env.do_on_mouse_click = function() end
	env.do_on_mouse_wheel = function() end
	install_module_environment(env, "jjjLib1")
	env.require = function(name)
		if name == "graphics" then
			return env.graphics
		end
		if name == "ffi" then
			return {
				load = function() return {} end,
				cdef = function() end
			}
		end
		return require(name)
	end
	local chunk = assert(loadfile(root .. "/FlyWithLua/Modules/jjjLib1.lua"))
	setfenv(chunk, env)
	chunk("jjjLib1")

	local source_path = tmp .. "/patch.lua"
	local source = assert(io.open(source_path, "w"))
	source:write("VALUE = 1\n")
	source:close()
	assert(env.jjjLib1.patchLuaScript(source_path, { "VALUE = 1" }, "true", "unit", "TestPlugin") == 1)
	local patched = assert(io.open(source_path, "r")):read("*a")
	assert(patched:find("if true then VALUE = 1 end", 1, true))
	assert(io.open(source_path .. "_jjjLib_BACKUP", "r"))

	local unchanged_path = tmp .. "/unchanged.lua"
	local unchanged = assert(io.open(unchanged_path, "w"))
	unchanged:write("OTHER = 1\n")
	unchanged:close()
	assert(env.jjjLib1.patchLuaScript(unchanged_path, { "VALUE = 1" }, "true", "unit", "TestPlugin") == 0)
	local unchanged_read = assert(io.open(unchanged_path, "r")):read("*a")
	assert(unchanged_read == "OTHER = 1\n")
end

test_bravo_dial()
test_b58_defaults()
test_landing_rate()
test_user_waypoint()
test_hud()
test_sma()
test_luaxml_open_failure()
test_jjjlib_patch_safety()

print("Lua regression tests passed.")
