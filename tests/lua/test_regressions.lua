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
	env.do_every_panel_draw = function() end
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
	env.do_every_panel_draw = function() end
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

local function test_hud_g1000()
	local hud_path = root .. "/FlyWithLua/Scripts/HUD-G1000.lua"
	local position_path = tmp .. "/HUD-G1000.position.cfg"
	os.remove(position_path)

	local function make_data()
		return {
			["sim/time/framerate_period"] = 1 / 60,
			["sim/time/total_running_time_sec"] = 10,
			["sim/cockpit2/gauges/indicators/airspeed_kts_pilot"] = 120,
			["sim/cockpit2/gauges/indicators/altitude_ft_pilot"] = 4500,
			["sim/cockpit2/gauges/indicators/vvi_fpm_pilot"] = 100,
			["sim/flightmodel/position/y_agl"] = 1200,
			["sim/flightmodel/position/mag_psi"] = 361,
			["sim/cockpit2/gauges/indicators/wind_heading_deg_mag"] = 359,
			["sim/cockpit2/gauges/indicators/wind_speed_kts"] = 12,
			["sim/flightmodel2/controls/flap1_deploy_ratio"] = 0.5,
			["sim/cockpit2/controls/elevator_trim"] = 0.2,
			["sim/cockpit2/controls/parking_brake_ratio"] = 0,
			["sim/flightmodel2/gear/deploy_ratio"] = {[0] = 1, [1] = 1, [2] = 1},
			["sim/cockpit2/radios/actuators/transponder_mode"] = 3,
			["sim/cockpit/autopilot/autopilot_state"] = 16386,
			["sim/aircraft/engine/acf_num_engines"] = 2,
			["sim/cockpit2/engine/actuators/fuel_pump_on"] = {[0] = 1, [1] = 1},
			["sim/cockpit/electrical/beacon_lights_on"] = 1,
			["sim/cockpit2/switches/landing_lights_switch"] = {[0] = 1},
			["sim/cockpit2/switches/taxi_light_on"] = 0,
			["sim/cockpit2/switches/navigation_lights_on"] = 1,
			["sim/cockpit2/switches/strobe_lights_on"] = 0,
			["sim/cockpit2/ice/ice_pitot_heat_on_pilot"] = 1,
			["sim/cockpit2/controls/gear_handle_down"] = 1,
			["sim/cockpit2/annunciators/gear_warning"] = 0,
			["sim/cockpit/warnings/annunciators/master_warning"] = 0,
			["sim/cockpit/warnings/annunciators/fuel_quantity"] = 0,
			["sim/cockpit/warnings/annunciators/master_caution"] = 0,
			["sim/cockpit/warnings/annunciators/low_voltage"] = 0,
			["sim/cockpit/warnings/annunciators/pitot_heat_off"] = 0,
			["sim/cockpit/warnings/annunciators/autopilot_disconnect"] = 0,
			["sim/cockpit/warnings/annunciators/engine_fires"] = {[0] = 0, [1] = 0},
			["sim/cockpit/warnings/annunciators/oil_pressure_low"] = {[0] = 0, [1] = 0},
			["sim/cockpit/warnings/annunciators/oil_temperature_high"] = {[0] = 0, [1] = 0},
			["sim/cockpit/warnings/annunciators/generator_off"] = {[0] = 0, [1] = 0},
		}
	end

	local function make_environment(data, draws, logs, native)
		local env = new_environment()
		local commands = {}
		local callbacks = {}
		local legacy_draws = native ~= nil and {} or draws
		env.SCREEN_WIDTH = 1920
		env.SCREEN_HIGHT = 1080
		env.SCRIPT_DIRECTORY = tmp .. "/"
		env.PLUGIN_MAIN_DIRECTORY = tmp
		env.PLANE_ICAO = "BE58"
		env.MOUSE_X = 0
		env.MOUSE_Y = 0
		env.MOUSE_STATUS = "up"
		env.RESUME_MOUSE_CLICK = false
		env.get = function(path, index)
			local value = data[path]
			if type(value) == "table" then return value[index] end
			if index ~= nil then return nil end
			return value
		end
		env.graphics = setmetatable({
			set_color = function() end,
			draw_rectangle = function() end,
			draw_line = function() end,
			draw_circle = function() end,
			draw_filled_circle = function() end,
		}, {__index = function() return function() end end})
		env.require = function(name)
			if name == "graphics" then return env.graphics end
			return require(name)
		end
		env.create_command = function(name, description, press, begin, end_command)
			commands[name] = {description, press, begin, end_command}
		end
		env.do_every_frame = function(code) callbacks.every_frame = code end
		env.do_every_draw = function(code) callbacks.every_draw = code end
		env.do_every_panel_draw = function(code) callbacks.every_panel_draw = code end
		env.do_on_mouse_click = function(code) callbacks.mouse_click = code end
		env.logMsg = function(message) logs[#logs + 1] = message end
		env.measure_string = function(text) return #tostring(text) * 7 end
		local function record(text) legacy_draws[#legacy_draws + 1] = tostring(text) end
		env.draw_string_Helvetica_10 = function(_, _, text) record(text) end
		env.draw_string_Helvetica_12 = function(_, _, text) record(text) end
		env.draw_string_Helvetica_18 = function(_, _, text) record(text) end
		if native ~= nil then
			native.draws = native.draws or {}
			native.measures = native.measures or {}
			native.batch_begins = native.batch_begins or 0
			native.batch_ends = native.batch_ends or 0
			env.mac_native = {
				draw_hidpi_string = function(x, y, text, size, family, weight)
				native.draws[#native.draws + 1] = {
					x = x,
					y = y,
					text = tostring(text),
					size = size,
					family = family,
					weight = weight,
				}
				draws[#draws + 1] = tostring(text)
				if native.draw_deferred then return false, true end
				if native.draw_result == nil then return true end
				return native.draw_result
			end,
				measure_hidpi_string = function(text, size, family, weight)
				native.measures[#native.measures + 1] = {
					text = tostring(text),
					size = size,
					family = family,
					weight = weight,
				}
				if native.measure_result == nil then return #tostring(text) * 7 end
				return native.measure_result
			end,
				begin_hidpi_frame = function()
					native.batch_begins = native.batch_begins + 1
					return true
				end,
				end_hidpi_frame = function()
					native.batch_ends = native.batch_ends + 1
					return true
				end,
			}
		end
		env._hud_commands = commands
		env._hud_callbacks = callbacks
		env._legacy_draws = legacy_draws
		return env
	end

	local data = make_data()
	local draws = {}
	local logs = {}
	local env = make_environment(data, draws, logs)
	local function update_hud(at_time)
		if at_time ~= nil then
			data["sim/time/total_running_time_sec"] = at_time
		else
			-- Stay just above the 0.20 s gate; decimal floating-point values
			-- can otherwise land a few ulps below the threshold.
			data["sim/time/total_running_time_sec"] = data["sim/time/total_running_time_sec"] + 0.21
		end
		env.hud_g1000.update()
	end
	local function drew(text)
		for index = 1, #draws do
			if draws[index] == text then return true end
		end
		return false
	end
	load_in_environment(hud_path, env)

	assert(env._hud_commands["FlyWithLua/HUD-G1000/toggle"])
	assert(env._hud_commands["FlyWithLua/HUD-G1000/edit_position"])
	assert(env._hud_commands["FlyWithLua/HUD-G1000/reset_position"])
	assert(env._hud_callbacks.every_frame == "hud_g1000.update()")
	assert(env._hud_callbacks.every_draw == "hud_g1000.draw()")
	assert(env._hud_callbacks.every_panel_draw == "hud_g1000.draw()")
	assert(env._hud_callbacks.mouse_click == "hud_g1000.handle_mouse_click()")

	update_hud()
	local snapshot = env.hud_g1000.state.snapshot
	assert(snapshot.ias == 120)
	assert(snapshot.altitude_ft == 4500)
	assert(snapshot.vvi_raw == 100)
	assert(math.abs(snapshot.agl_ft - 3937.008) < 0.01)
	assert(snapshot.heading == 1)
	assert(snapshot.gear.status == "DOWN")
	assert(snapshot.transponder.text == "ALT")
	assert(snapshot.autopilot.text == "HDG/ALT")
	assert(snapshot.autopilot.lateral == "HDG")
	assert(snapshot.autopilot.vertical == "ALT")
	assert(snapshot.missing_count == 0)
	data["sim/cockpit2/gauges/indicators/airspeed_kts_pilot"] = 121
	env.hud_g1000.update()
	assert(env.hud_g1000.state.snapshot.ias == 120)
	update_hud()
	assert(env.hud_g1000.state.snapshot.ias == 121)
	data["sim/cockpit2/gauges/indicators/airspeed_kts_pilot"] = 120
	update_hud()
	env.hud_g1000.draw()
	assert(drew("120"))
	assert(drew("04500"))
	assert(drew("+0100"))
	assert(drew("3937"))
	assert(drew("001"))

	local native_draws = {}
	local native_measures = {}
	local native_backend = {
		draws = native_draws,
		measures = native_measures,
	}
	local native_env = make_environment(make_data(), {}, {}, native_backend)
	load_in_environment(hud_path, native_env)
	native_env.hud_g1000.update()
	native_env.hud_g1000.draw()
	local saw_mono_value = false
	local saw_pro_label = false
	for index = 1, #native_draws do
		local call = native_draws[index]
		if call.text == "120" and call.family == "sf_mono" and call.weight == 600 and call.size == 18 then
			saw_mono_value = true
		end
		if call.text == "IAS" and call.family == "sf_pro_text" and call.weight == 400 and call.size == 12 then
			saw_pro_label = true
		end
	end
	assert(saw_mono_value)
	assert(saw_pro_label)
	assert(#native_measures > 0)
	assert(#native_env._legacy_draws == 0)
	assert(native_backend.batch_begins > 0)
	assert(native_backend.batch_begins == native_backend.batch_ends)
	local native_measure_count = #native_measures
	native_env.hud_g1000.draw()
	assert(#native_measures == native_measure_count)

	local failing_native = make_environment(make_data(), {}, {}, {draw_result = false})
	load_in_environment(hud_path, failing_native)
	failing_native.hud_g1000.update()
	failing_native.hud_g1000.draw()
	assert(#failing_native._legacy_draws > 0)
	assert(failing_native.hud_g1000.state.text_backend.disabled)

	local deferred_native = make_environment(make_data(), {}, {}, {draw_deferred = true})
	load_in_environment(hud_path, deferred_native)
	deferred_native.hud_g1000.update()
	deferred_native.hud_g1000.draw()
	assert(#deferred_native._legacy_draws > 0)
	assert(not deferred_native.hud_g1000.state.text_backend.disabled)

	local bottom_left = env.hud_g1000.get_layout(1920, 1080)
	assert(bottom_left.x == 18)
	assert(bottom_left.y == 28)
	env.hud_g1000.position = {anchor = "top_left", offset_x = 10, offset_y = 20}
	local top_left = env.hud_g1000.get_layout(1920, 1080)
	assert(top_left.x == 10)
	assert(top_left.y == 876)
	env.hud_g1000.position = {anchor = "top_right", offset_x = 10, offset_y = 20}
	local top_right = env.hud_g1000.get_layout(1920, 1080)
	assert(top_right.x == 1350)
	assert(top_right.y == 876)
	env.hud_g1000.position = {anchor = "bottom_right", offset_x = 10, offset_y = 20}
	local bottom_right = env.hud_g1000.get_layout(1920, 1080)
	assert(bottom_right.x == 1350)
	assert(bottom_right.y == 20)

	data["sim/cockpit2/gauges/indicators/airspeed_kts_pilot"] = nil
	update_hud()
	assert(env.hud_g1000.state.snapshot.ias == nil)
	assert(env.hud_g1000.state.snapshot.alert.code == "data_unavailable")
	env.hud_g1000.draw()
	local saw_dash = false
	local saw_unavailable = false
	for index = 1, #draws do
		if draws[index] == "--" then saw_dash = true end
		if draws[index] == "DATA UNAVAILABLE" then saw_unavailable = true end
	end
	assert(saw_dash)
	assert(saw_unavailable)

	data["sim/cockpit2/gauges/indicators/airspeed_kts_pilot"] = 120
	local transponder_labels = {[0] = "OFF", [1] = "STBY", [2] = "ON", [3] = "ALT", [4] = "TEST", [5] = "GND", [6] = "TA", [7] = "TA/RA"}
	for mode = 0, 7 do
		data["sim/cockpit2/radios/actuators/transponder_mode"] = mode
		update_hud()
		assert(env.hud_g1000.state.snapshot.transponder.text == transponder_labels[mode])
	end
	data["sim/cockpit2/radios/actuators/transponder_mode"] = 8
	update_hud()
	assert(env.hud_g1000.state.snapshot.transponder.text == "--")
	data["sim/cockpit2/radios/actuators/transponder_mode"] = 3

	data["sim/flightmodel2/gear/deploy_ratio"] = {[0] = 1, [1] = 0.5, [2] = 1}
	data["sim/cockpit2/radios/actuators/transponder_mode"] = 3
	update_hud()
	assert(env.hud_g1000.state.snapshot.gear.status == "TRANSIT")

	env.PLANE_ICAO = "C172"
	data["sim/flightmodel2/gear/deploy_ratio"] = nil
	data["sim/cockpit2/controls/gear_handle_down"] = 1
	update_hud()
	assert(env.hud_g1000.state.snapshot.gear.profile == "generic")
	assert(env.hud_g1000.state.snapshot.gear.status == "DOWN")

	data["sim/cockpit/warnings/annunciators/engine_fires"][0] = 1
	data["sim/cockpit/warnings/annunciators/fuel_quantity"] = 1
	update_hud()
	assert(env.hud_g1000.state.snapshot.alert.code == "engine_fire")
	data["sim/cockpit/warnings/annunciators/engine_fires"][0] = 0
	update_hud()
	assert(env.hud_g1000.state.snapshot.alert.code == "low_fuel")
	data["sim/cockpit2/annunciators/master_warning"] = 1
	update_hud()
	assert(env.hud_g1000.state.snapshot.alert.code == "master_warning")
	data["sim/cockpit2/annunciators/master_warning"] = 0
	data["sim/cockpit/warnings/annunciators/fuel_quantity"] = 0

	data["sim/cockpit/warnings/annunciators/fuel_quantity"] = 0
	data["sim/cockpit2/controls/gear_handle_down"] = 0
	data["sim/flightmodel/position/y_agl"] = 100
	data["sim/cockpit2/gauges/indicators/vvi_fpm_pilot"] = -500
	data["sim/time/total_running_time_sec"] = 20
	update_hud(20)
	assert(env.hud_g1000.state.snapshot.alert.code == "low_alt_gear")

	data["sim/cockpit2/controls/gear_handle_down"] = 1
	data["sim/flightmodel/position/y_agl"] = 1200
	data["sim/cockpit2/gauges/indicators/vvi_fpm_pilot"] = 100
	data["sim/time/total_running_time_sec"] = 30
	update_hud(30)
	assert(env.hud_g1000.state.snapshot.alert ~= nil)
	data["sim/time/total_running_time_sec"] = 30.3
	update_hud(30.3)
	assert(env.hud_g1000.state.snapshot.alert ~= nil)
	data["sim/time/total_running_time_sec"] = 30.6
	update_hud(30.6)
	assert(env.hud_g1000.state.snapshot.alert == nil)

	env.hud_g1000.position = {anchor = "bottom_left", offset_x = 18, offset_y = 28}
	env.hud_g1000.edit_position()
	local drag_layout = env.hud_g1000.get_layout(1920, 1080)
	env.MOUSE_X = drag_layout.x + 20
	env.MOUSE_Y = drag_layout.y + 20
	env.MOUSE_STATUS = "down"
	env.hud_g1000.handle_mouse_click()
	env.MOUSE_X = env.MOUSE_X + 100
	env.MOUSE_Y = env.MOUSE_Y + 50
	env.MOUSE_STATUS = "drag"
	env.hud_g1000.handle_mouse_click()
	env.MOUSE_STATUS = "up"
	env.hud_g1000.handle_mouse_click()
	assert(env.hud_g1000.position.offset_x == 118)
	assert(env.hud_g1000.position.offset_y == 78)
	local first_saved_position = assert(io.open(position_path, "r")):read("*a")
	assert(first_saved_position:find("offset_x=118", 1, true))
	assert(first_saved_position:find("offset_y=78", 1, true))
	drag_layout = env.hud_g1000.get_layout(1920, 1080)
	env.MOUSE_X = drag_layout.x + 20
	env.MOUSE_Y = drag_layout.y + 20
	env.MOUSE_STATUS = "down"
	env.hud_g1000.handle_mouse_click()
	env.MOUSE_X = 9999
	env.MOUSE_Y = 9999
	env.MOUSE_STATUS = "drag"
	env.hud_g1000.handle_mouse_click()
	env.MOUSE_STATUS = "up"
	env.hud_g1000.handle_mouse_click()
	local constrained_layout = env.hud_g1000.get_layout(1920, 1080)
	assert(constrained_layout.x + constrained_layout.width <= 1920)
	assert(constrained_layout.y + constrained_layout.height <= 1080)
	assert(constrained_layout.x == 1360)
	assert(constrained_layout.y == 896)
	local saved_position = assert(io.open(position_path, "r")):read("*a")
	assert(saved_position:find("offset_x=1360", 1, true))
	assert(saved_position:find("offset_y=896", 1, true))
	env.hud_g1000.config.language = "ja"
	update_hud()
	env.hud_g1000.draw()
	assert(drew("正常"))

	local corrupt = assert(io.open(position_path, "w"))
	corrupt:write("anchor=middle\noffset_x=not-a-number\noffset_y=10\n")
	corrupt:close()
	local reload_draws = {}
	local reload_logs = {}
	local reloaded = make_environment(make_data(), reload_draws, reload_logs)
	load_in_environment(hud_path, reloaded)
	assert(reloaded.hud_g1000.position.anchor == "bottom_left")
	assert(reloaded.hud_g1000.position.offset_x == 18)
	assert(reloaded.hud_g1000.position.offset_y == 28)
	assert(#reload_logs == 1)
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
	env.do_every_panel_draw = function() end
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
test_hud_g1000()
test_sma()
test_luaxml_open_failure()
test_jjjlib_patch_safety()

print("Lua regression tests passed.")
