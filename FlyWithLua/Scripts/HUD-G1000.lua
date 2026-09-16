require("graphics")

-- HUD-G1000 Overlay
-- Production version: v2.0.0
-- Requires FlyWithLua NG / FlyWithLua-Mac compatibility APIs
--
-- The values in hud_g1000.config are the user-editable defaults.  A dragged
-- position is stored separately in HUD-G1000.position.cfg and takes
-- precedence over the default anchor and offsets.

hud_g1000 = {}
local hud = hud_g1000

local BASE_WIDTH = 560
local BASE_HEIGHT = 184
local ALERT_HEIGHT = 26
local SYSTEM_HEIGHT = 44
local FLIGHT_HEIGHT = 62
local AIRCRAFT_HEIGHT = 52

local MIN_SCALE = 0.65
local MAX_SCALE = 1.50
local POSITION_LIMIT = 100000
local UPDATE_INTERVAL_SECONDS = 0.20
local TEXT_WIDTH_CACHE_LIMIT = 256

local COLORS = {
    background = {0.02, 0.03, 0.05},
    border = {0.75, 0.80, 0.88},
    info = {1.00, 1.00, 1.00},
    muted = {0.62, 0.68, 0.76},
    normal = {0.15, 1.00, 0.25},
    caution = {1.00, 0.72, 0.12},
    warning = {1.00, 0.18, 0.18},
}

local LABELS = {
    en = {
        status_ok = "STATUS OK",
        edit_mode = "EDIT MODE",
        unavailable = "Unavailable",
        on = "ON",
        off = "OFF",
        fuel = "FUEL",
        pump = "PUMP",
        beacon = "BCN",
        landing = "LAND",
        taxi = "TAXI",
        nav = "NAV",
        strobe = "STRB",
        pitot = "PITOT",
        heat = "HEAT",
        txp = "TXP",
        alt = "ALT",
        gnd = "GND",
        ta = "TA",
        ta_ra = "TA/RA",
        stby = "STBY",
        flaps = "FLAPS",
        fps = "FPS",
        gear = "GEAR",
        up = "UP",
        down = "DOWN",
        transit = "TRANSIT",
        trim = "TRIM",
        agl = "AGL",
        hdg = "HDG",
        wind = "WIND",
        ap = "AP",
        park = "PARK",
        brake = "BRAKE",
        set = "SET",
        released = "RELEASED",
        ias = "IAS",
        vs = "V/S",
        data_unavailable = "DATA UNAVAILABLE",
        engine_fire = "ENGINE FIRE",
        master_warning = "MASTER WARNING",
        low_fuel = "LOW FUEL",
        gear_warning = "GEAR WARNING",
        low_alt_gear = "LOW ALT: GEAR UP",
        master_caution = "MASTER CAUTION",
        low_voltage = "LOW VOLTAGE",
        oil_pressure = "OIL PRESSURE",
        oil_temperature = "OIL TEMPERATURE",
        generator = "GENERATOR OFF",
        pitot_warning = "PITOT HEAT OFF",
        autopilot = "AUTOPILOT DISCONNECT",
        drag_hint = "DRAG HUD TO MOVE",
    },
    ja = {
        status_ok = "正常",
        edit_mode = "編集モード",
        unavailable = "利用不可",
        on = "ON",
        off = "OFF",
        fuel = "燃料",
        pump = "ポンプ",
        beacon = "ビーコン",
        landing = "着陸灯",
        taxi = "タキシー",
        nav = "航法灯",
        strobe = "ストロボ",
        pitot = "ピトー",
        heat = "ヒート",
        txp = "TXP",
        alt = "ALT",
        gnd = "GND",
        ta = "TA",
        ta_ra = "TA/RA",
        stby = "STBY",
        flaps = "フラップ",
        fps = "FPS",
        gear = "ギア",
        up = "UP",
        down = "DOWN",
        transit = "移動中",
        trim = "トリム",
        agl = "対地高",
        hdg = "方位",
        wind = "風",
        ap = "AP",
        park = "駐車",
        brake = "ブレーキ",
        set = "SET",
        released = "解除",
        ias = "速度",
        vs = "昇降率",
        data_unavailable = "データ利用不可",
        engine_fire = "エンジン火災",
        master_warning = "重大警告",
        low_fuel = "燃料低下",
        gear_warning = "ギア警告",
        low_alt_gear = "低高度: ギア上",
        master_caution = "注意",
        low_voltage = "電圧低下",
        oil_pressure = "油圧低下",
        oil_temperature = "油温上昇",
        generator = "発電機OFF",
        pitot_warning = "ピトーヒートOFF",
        autopilot = "AP解除",
        drag_hint = "ドラッグで移動",
    },
}

hud.config = {
    -- User-editable defaults.
    anchor = "bottom_left", -- top_left, top_right, bottom_left, bottom_right
    offset_x = 18,
    offset_y = 28,
    scale = 1.0,
    opacity = 0.75,
    language = "en", -- en or ja; English is the safe default for XPLM fonts.
    theme = "dark_high_contrast",
    smoothing_seconds = 0.3,
    alert_clear_seconds = 0.5,
    low_altitude_ft = 500,
}

hud.state = {
    visible = true,
    edit_mode = false,
    dragging = false,
    snapshot = nil,
    clock = 0,
    last_update_time = nil,
    text_width_cache = {
        values = {},
        keys = {},
        next_slot = 1,
    },
    filtered = {
        vvi = nil,
        wind_heading = nil,
        wind_speed = nil,
    },
    alert = {
        active = nil,
        clear_started = nil,
    },
    text_backend = {
        disabled = false,
    },
    logged = {},
}

-- Keep the historical global visible flag available to existing macros.
hud_g1000_visible = true

local function is_finite_number(value)
    return type(value) == "number"
        and value == value
        and value ~= math.huge
        and value ~= -math.huge
end

local function clamp(value, low, high)
    if not is_finite_number(value) then return low end
    if value < low then return low end
    if value > high then return high end
    return value
end

local function configured_number(key, default, low, high)
    local value = tonumber(hud.config[key])
    if not is_finite_number(value) then value = default end
    if low ~= nil and high ~= nil then
        value = clamp(value, low, high)
    end
    return value
end

local function normalize_heading(value)
    if not is_finite_number(value) then return nil end
    local heading = value % 360
    if heading < 0 then heading = heading + 360 end
    return heading
end

local function trim_string(value)
    return tostring(value or ""):match("^%s*(.-)%s*$")
end

local function labels()
    if hud.config.language == "ja" then
        return LABELS.ja
    end
    return LABELS.en
end

local function log_once(key, message)
    if hud.state.logged[key] then return end
    hud.state.logged[key] = true
    if type(logMsg) == "function" then
        pcall(logMsg, "HUD-G1000: " .. tostring(message))
    end
end

local function position_config_path()
    local base = PLUGIN_MAIN_DIRECTORY or SCRIPT_DIRECTORY or "."
    base = tostring(base):gsub("/+$", "")
    return base .. "/HUD-G1000.position.cfg"
end

local function valid_anchor(anchor)
    return anchor == "top_left"
        or anchor == "top_right"
        or anchor == "bottom_left"
        or anchor == "bottom_right"
end

local function default_position()
    return {
        anchor = valid_anchor(hud.config.anchor) and hud.config.anchor or "bottom_left",
        offset_x = configured_number("offset_x", 18, 0, POSITION_LIMIT),
        offset_y = configured_number("offset_y", 28, 0, POSITION_LIMIT),
    }
end

hud.position = default_position()

local function load_position_config()
    local path = position_config_path()
    local file, err = io.open(path, "r")
    if not file then
        if err and not tostring(err):find("No such file", 1, true) then
            log_once("position_read", "Could not read " .. path .. ": " .. tostring(err))
        end
        return
    end

    local values = {}
    local seen = {}
    for line in file:lines() do
        local key, value = line:match("^%s*([%w_]+)%s*=%s*(.-)%s*$")
        if key and value and value:sub(1, 1) ~= "#" then
            seen[key] = true
            values[key] = trim_string(value)
        end
    end
    file:close()

    local offset_x = tonumber(values.offset_x)
    local offset_y = tonumber(values.offset_y)
    local anchor = values.anchor
    local valid = seen.anchor
        and seen.offset_x
        and seen.offset_y
        and valid_anchor(anchor)
        and is_finite_number(offset_x)
        and is_finite_number(offset_y)
        and offset_x >= 0
        and offset_y >= 0
        and offset_x <= POSITION_LIMIT
        and offset_y <= POSITION_LIMIT

    if not valid then
        log_once("position_invalid", "Invalid position config; using defaults: " .. path)
        hud.position = default_position()
        return
    end

    hud.position = {
        anchor = anchor,
        offset_x = offset_x,
        offset_y = offset_y,
    }
end

local function save_position_config()
    local path = position_config_path()
    local temporary_path = path .. ".tmp"
    local defaults = default_position()
    local position = hud.position or defaults
    local anchor = valid_anchor(position.anchor) and position.anchor or defaults.anchor
    local offset_x = tonumber(position.offset_x)
    local offset_y = tonumber(position.offset_y)
    if not is_finite_number(offset_x) then offset_x = defaults.offset_x end
    if not is_finite_number(offset_y) then offset_y = defaults.offset_y end
    offset_x = clamp(offset_x, 0, POSITION_LIMIT)
    offset_y = clamp(offset_y, 0, POSITION_LIMIT)

    local file, err = io.open(temporary_path, "w")
    if not file then
        log_once("position_write", "Could not open temporary position config: " .. tostring(err))
        return false
    end

    local written, write_error = pcall(function()
        assert(file:write("# HUD-G1000 position configuration v1\n"))
        assert(file:write("version=1\n"))
        assert(file:write("anchor=" .. anchor .. "\n"))
        assert(file:write("offset_x=" .. string.format("%.0f", offset_x) .. "\n"))
        assert(file:write("offset_y=" .. string.format("%.0f", offset_y) .. "\n"))
        local flushed, flush_error = file:flush()
        if not flushed then
            error(flush_error or "flush failed")
        end
    end)
    if not written then
        pcall(function() file:close() end)
        pcall(os.remove, temporary_path)
        log_once("position_write", "Could not write position config: " .. tostring(write_error))
        return false
    end

    local close_ok, closed, close_error = pcall(function() return file:close() end)
    if not close_ok or not closed then
        pcall(os.remove, temporary_path)
        log_once("position_write", "Could not close position config: " .. tostring(close_error or closed))
        return false
    end

    local renamed, rename_error = os.rename(temporary_path, path)
    if not renamed then
        pcall(os.remove, temporary_path)
        log_once("position_write", "Could not replace position config: " .. tostring(rename_error))
        return false
    end
    return true
end

local function read_number(path, index)
    if type(get) ~= "function" then return nil end
    local ok, value
    if index == nil then
        ok, value = pcall(get, path)
    else
        ok, value = pcall(get, path, index)
    end
    if not ok then
        log_once("dataref_error_" .. path, "DataRef read failed for " .. path .. ": " .. tostring(value))
        return nil
    end
    if not is_finite_number(value) then return nil end
    return value
end

local function read_boolean(path, index)
    local value = read_number(path, index)
    if value == nil then return nil end
    if value == 0 then return false end
    if value == 1 then return true end
    return nil
end

local function read_any(path, count)
    local saw_value = false
    local saw_missing = false
    local saw_invalid = false
    for index = 0, count - 1 do
        local value = read_number(path, index)
        if value ~= nil then
            saw_value = true
            if value == 1 then return true end
            if value ~= 0 then saw_invalid = true end
        else
            saw_missing = true
        end
    end
    if saw_value and not saw_missing and not saw_invalid then return false end
    return nil
end

local function read_first_number(paths)
    for index = 1, #paths do
        local value = read_number(paths[index])
        if value ~= nil then return value end
    end
    return nil
end

local function read_first_boolean(paths)
    for index = 1, #paths do
        local value = read_boolean(paths[index])
        if value ~= nil then return value end
    end
    return nil
end

local function read_any_boolean(paths)
    local saw_known = false
    for index = 1, #paths do
        local value = read_boolean(paths[index])
        if value == true then return true end
        if value == false then saw_known = true end
    end
    if saw_known then return false end
    return nil
end

local function read_first_any(paths, count)
    for index = 1, #paths do
        local value = read_any(paths[index], count)
        if value ~= nil then return value end
    end
    return nil
end

local function read_range(path, low, high, index)
    local value = read_number(path, index)
    if value == nil or value < low or value > high then return nil end
    return value
end

local function read_first_range(paths, low, high)
    for index = 1, #paths do
        local value = read_range(paths[index], low, high)
        if value ~= nil then return value end
    end
    return nil
end

local function read_annunciator_boolean(name)
    return read_first_boolean({
        "sim/cockpit2/annunciators/" .. name,
        "sim/cockpit/warnings/annunciators/" .. name,
    })
end

local function read_annunciator_any(name, count)
    return read_first_any({
        "sim/cockpit2/annunciators/" .. name,
        "sim/cockpit/warnings/annunciators/" .. name,
    }, count)
end

local function smooth_value(previous, current, dt, seconds)
    if current == nil then return nil end
    if previous == nil then return current end
    local alpha = 1 - math.exp(-clamp(dt, 0.001, 1.0) / math.max(seconds or 0.3, 0.001))
    return previous + (current - previous) * alpha
end

local function smooth_heading(previous, current, dt, seconds)
    if current == nil then return nil end
    if previous == nil then return current end
    local delta = ((current - previous + 540) % 360) - 180
    local alpha = 1 - math.exp(-clamp(dt, 0.001, 1.0) / math.max(seconds or 0.3, 0.001))
    return normalize_heading(previous + delta * alpha)
end

local function profile_is_be58()
    return tostring(PLANE_ICAO or ""):upper() == "BE58"
end

local function read_gear_state()
    local result = {
        profile = profile_is_be58() and "BE58" or "generic",
        ratios = {},
        warning = read_first_boolean({
            "sim/cockpit2/annunciators/gear_warning",
            "sim/cockpit2/annunciators/gear_warning_aural",
            "sim/cockpit/warnings/annunciators/gear_warning",
        }),
        handle_down = read_first_boolean({
            "sim/cockpit2/controls/gear_handle_down",
            "sim/cockpit/switches/gear_handle_status",
        }),
    }

    if result.profile == "BE58" then
        local all_available = true
        for index = 0, 2 do
            result.ratios[index] = read_range("sim/flightmodel2/gear/deploy_ratio", 0, 1, index)
            if result.ratios[index] == nil then all_available = false end
        end
        if not all_available then
            result.status = "UNAVAILABLE"
            result.level = "unavailable"
            result.missing = true
            return result
        end

        if result.warning then
            result.status = "WARNING"
            result.level = "warning"
            return result
        end

        local minimum = math.min(result.ratios[0], result.ratios[1], result.ratios[2])
        local maximum = math.max(result.ratios[0], result.ratios[1], result.ratios[2])
        if maximum < 0.1 then
            result.status = "UP"
            result.level = "info"
        elseif minimum > 0.9 then
            result.status = "DOWN"
            result.level = "normal"
        else
            result.status = "TRANSIT"
            result.level = "caution"
        end
        return result
    end

    if result.warning then
        result.status = "WARNING"
        result.level = "warning"
    elseif result.handle_down == nil then
        result.status = "UNAVAILABLE"
        result.level = "unavailable"
        result.missing = true
    elseif result.handle_down then
        result.status = "DOWN"
        result.level = "normal"
    else
        result.status = "UP"
        result.level = "info"
    end
    return result
end

local function transponder_state(mode)
    local l = labels()
    if mode == nil then
        return {text = "--", level = "unavailable", missing = true}
    end
    if mode < 0 or mode > 7 or mode ~= math.floor(mode) then
        return {text = "--", level = "unavailable", missing = true}
    end
    local map = {
        [0] = l.off,
        [1] = l.stby,
        [2] = "ON",
        [3] = "ALT",
        [4] = "TEST",
        [5] = l.gnd,
        [6] = l.ta,
        [7] = l.ta_ra,
    }
    local text = map[mode]
    if not text then
        return {text = "--", level = "unavailable", missing = true}
    end
    if mode == 3 or mode == 5 or mode == 6 or mode == 7 then
        return {text = text, level = "normal"}
    end
    if mode == 0 then
        return {text = text, level = "info"}
    end
    return {text = text, level = "caution"}
end

local AUTOPILOT_LATERAL_MODES = {
    {bit = 2, text = "HDG"},
    {bit = 4, text = "ROLL"},
    {bit = 512, text = "NAV"},
    {bit = 32768, text = "TOGA"},
    {bit = 524288, text = "GPSS"},
    {bit = 1048576, text = "HDG HOLD"},
    {bit = 2097152, text = "TRK RATE"},
    {bit = 4194304, text = "TRK"},
}

local AUTOPILOT_AUTOTHROTTLE_MODE = {bit = 1, text = "AT"}

local AUTOPILOT_VERTICAL_MODES = {
    {bit = 8, text = "SPD"},
    {bit = 16, text = "V/S"},
    {bit = 64, text = "FLC"},
    {bit = 128, text = "PITCH"},
    {bit = 2048, text = "GS"},
    {bit = 8192, text = "VNAV SPD"},
    {bit = 16384, text = "ALT"},
    {bit = 65536, text = "TOGA"},
    {bit = 262144, text = "VNAV PATH"},
    {bit = 8388608, text = "FPA"},
}

local AUTOPILOT_ARMED_MODES = {
    {bit = 32, text = "ALT"},
    {bit = 256, text = "NAV"},
    {bit = 1024, text = "GS"},
    {bit = 4096, text = "VNAV SPD"},
    {bit = 131072, text = "VNAV PATH"},
}

local function autopilot_bit_set(value, bit)
    return math.floor(value / bit) % 2 == 1
end

local function first_autopilot_mode(value, modes)
    for index = 1, #modes do
        if autopilot_bit_set(value, modes[index].bit) then
            return modes[index].text
        end
    end
    return nil
end

local function autopilot_state(value)
    if value == nil or value < 0 or value ~= math.floor(value) then
        return {text = "--", level = "unavailable", missing = true}
    end

    local lateral = first_autopilot_mode(value, AUTOPILOT_LATERAL_MODES)
    local vertical = first_autopilot_mode(value, AUTOPILOT_VERTICAL_MODES)
    local autothrottle = autopilot_bit_set(value, AUTOPILOT_AUTOTHROTTLE_MODE.bit)
    local armed = {}
    for index = 1, #AUTOPILOT_ARMED_MODES do
        local mode = AUTOPILOT_ARMED_MODES[index]
        if autopilot_bit_set(value, mode.bit) then armed[#armed + 1] = mode.text end
    end

    local parts = {}
    if autothrottle then parts[#parts + 1] = AUTOPILOT_AUTOTHROTTLE_MODE.text end
    if lateral ~= nil then parts[#parts + 1] = lateral end
    if vertical ~= nil then parts[#parts + 1] = vertical end
    local text
    if #parts == 0 and #armed > 0 then
        text = "ARM " .. table.concat(armed, "/")
    elseif #parts > 0 and #armed > 0 then
        text = table.concat(parts, "/") .. " ARM " .. table.concat(armed, "/")
    elseif #parts > 0 then
        text = table.concat(parts, "/")
    elseif value == 0 then
        text = "OFF"
    else
        text = "ON"
    end

    return {
        raw = value,
        text = text,
        level = value == 0 and "info" or "normal",
        lateral = lateral or "OFF",
        vertical = vertical or "OFF",
        autothrottle = autothrottle,
        armed = armed,
    }
end

local function switch_state(value)
    local l = labels()
    if value == nil then
        return {text = "--", level = "unavailable", missing = true}
    end
    if value then
        return {text = l.on, level = "normal"}
    end
    return {text = l.off, level = "info"}
end

local function brake_state(value)
    local l = labels()
    if value == nil then
        return {text = "--", level = "unavailable", missing = true}
    end
    if value > 0.1 then
        return {text = l.set, level = "warning"}
    end
    return {text = l.released, level = "info"}
end

local function update_alert(snapshot)
    local l = labels()
    local candidates = {}

    local function add(priority, code, level, message)
        candidates[#candidates + 1] = {
            priority = priority,
            code = code,
            level = level,
            message = message,
        }
    end

    if snapshot.engine_fire then add(10, "engine_fire", "warning", l.engine_fire) end
    if snapshot.master_warning then add(20, "master_warning", "warning", l.master_warning) end
    if snapshot.low_fuel then add(30, "low_fuel", "warning", l.low_fuel) end
    if snapshot.gear.warning then add(40, "gear_warning", "warning", l.gear_warning) end

    local descending = snapshot.vvi_raw ~= nil and snapshot.vvi_raw < 0
    local low_altitude = snapshot.agl_ft ~= nil
        and snapshot.agl_ft <= configured_number("low_altitude_ft", 500, 0, POSITION_LIMIT)
    local gear_not_down = snapshot.gear.status ~= "DOWN"
        and snapshot.gear.status ~= "UNAVAILABLE"
        and snapshot.gear.status ~= "WARNING"
    if low_altitude and descending and gear_not_down then
        add(45, "low_alt_gear", "warning", l.low_alt_gear)
    end

    if snapshot.master_caution then add(50, "master_caution", "caution", l.master_caution) end
    if snapshot.low_voltage then add(60, "low_voltage", "caution", l.low_voltage) end
    if snapshot.oil_pressure_low then add(65, "oil_pressure", "caution", l.oil_pressure) end
    if snapshot.oil_temperature_high then add(66, "oil_temperature", "caution", l.oil_temperature) end
    if snapshot.generator_off then add(67, "generator", "caution", l.generator) end
    if snapshot.pitot_heat_off then add(68, "pitot_warning", "caution", l.pitot_warning) end
    if snapshot.autopilot_disconnect then add(69, "autopilot", "caution", l.autopilot) end

    if #candidates == 0 and snapshot.missing_count > 0 then
        add(90, "data_unavailable", "caution", l.data_unavailable)
    end

    local best = nil
    for index = 1, #candidates do
        if best == nil or candidates[index].priority < best.priority then
            best = candidates[index]
        end
    end

    if best ~= nil then
        hud.state.alert.active = best
        hud.state.alert.clear_started = nil
    elseif hud.state.alert.active ~= nil then
        if hud.state.alert.clear_started == nil then
            hud.state.alert.clear_started = snapshot.time
        elseif snapshot.time - hud.state.alert.clear_started >= configured_number("alert_clear_seconds", 0.5, 0.05, 10.0) then
            hud.state.alert.active = nil
            hud.state.alert.clear_started = nil
        end
    end

    snapshot.alert = hud.state.alert.active
end

local function update_snapshot(sim_time_hint)
    local period = read_number("sim/time/framerate_period")
    local dt = period and period > 0 and clamp(period, 0.001, 0.25) or (1 / 60)
    local sim_time = sim_time_hint or read_number("sim/time/total_running_time_sec")
    if sim_time == nil then
        hud.state.clock = hud.state.clock + dt
        sim_time = hud.state.clock
    else
        hud.state.clock = sim_time
    end

    local vvi_raw = read_number("sim/cockpit2/gauges/indicators/vvi_fpm_pilot")
    local wind_heading_raw = normalize_heading(read_number("sim/cockpit2/gauges/indicators/wind_heading_deg_mag"))
    local wind_speed = read_range("sim/cockpit2/gauges/indicators/wind_speed_kts", 0, POSITION_LIMIT)

    hud.state.filtered.vvi = smooth_value(
        hud.state.filtered.vvi,
        vvi_raw,
        dt,
        configured_number("smoothing_seconds", 0.3, 0.05, 2.0)
    )
    hud.state.filtered.wind_heading = smooth_heading(
        hud.state.filtered.wind_heading,
        wind_heading_raw,
        dt,
        configured_number("smoothing_seconds", 0.3, 0.05, 2.0)
    )
    hud.state.filtered.wind_speed = smooth_value(
        hud.state.filtered.wind_speed,
        wind_speed,
        dt,
        configured_number("smoothing_seconds", 0.3, 0.05, 2.0)
    )

    local snapshot = {
        time = sim_time,
        dt = dt,
        fps = period and period > 0 and 1 / period or nil,
        ias = read_range("sim/cockpit2/gauges/indicators/airspeed_kts_pilot", 0, POSITION_LIMIT),
        altitude_ft = read_number("sim/cockpit2/gauges/indicators/altitude_ft_pilot"),
        vvi_raw = vvi_raw,
        vvi_fpm = hud.state.filtered.vvi,
        agl_ft = nil,
        heading = normalize_heading(read_number("sim/flightmodel/position/mag_psi")),
        wind_heading = hud.state.filtered.wind_heading,
        wind_speed = hud.state.filtered.wind_speed,
        flaps = read_first_range({
            "sim/flightmodel2/controls/flap1_deploy_ratio",
            "sim/cockpit2/controls/flap_ratio",
        }, 0, 1),
        trim = read_first_range({
            "sim/cockpit2/controls/elevator_trim",
            "sim/flightmodel/controls/elv_trim",
        }, -1, 1),
        parking_brake = read_range("sim/cockpit2/controls/parking_brake_ratio", 0, 1),
        systems = {},
        gear = read_gear_state(),
        transponder = transponder_state(read_number("sim/cockpit2/radios/actuators/transponder_mode")),
        engine_fire = nil,
        autopilot = autopilot_state(read_number("sim/cockpit/autopilot/autopilot_state")),
        master_warning = read_annunciator_boolean("master_warning"),
        low_fuel = read_annunciator_boolean("fuel_quantity"),
        gear_warning = nil,
        master_caution = read_annunciator_boolean("master_caution"),
        low_voltage = read_annunciator_boolean("low_voltage"),
        oil_pressure_low = read_annunciator_any("oil_pressure_low", 8),
        oil_temperature_high = read_annunciator_any("oil_temperature_high", 8),
        generator_off = read_annunciator_any("generator_off", 8),
        pitot_heat_off = read_first_boolean({
            "sim/cockpit2/annunciators/pitot_heat",
            "sim/cockpit/warnings/annunciators/pitot_heat_off",
            "sim/cockpit2/annunciators/pitot_heat_off",
        }),
        autopilot_disconnect = read_any_boolean({
            "sim/cockpit2/annunciators/autopilot_disconnect",
            "sim/cockpit2/annunciators/autopilot",
            "sim/cockpit/warnings/annunciators/autopilot_disconnect",
            "sim/cockpit/warnings/annunciators/autopilot",
        }),
        missing_count = 0,
    }

    local agl_m = read_range("sim/flightmodel/position/y_agl", 0, POSITION_LIMIT)
    if agl_m ~= nil then snapshot.agl_ft = agl_m * 3.28084 end

    local engine_count = read_number("sim/aircraft/engine/acf_num_engines")
    engine_count = math.floor(clamp(engine_count or 8, 1, 8))
    snapshot.engine_fire = read_annunciator_any("engine_fires", engine_count)
    snapshot.gear_warning = snapshot.gear.warning

    snapshot.systems.fuel_pump = switch_state(read_any("sim/cockpit2/engine/actuators/fuel_pump_on", 8))
    snapshot.systems.beacon = switch_state(read_boolean("sim/cockpit/electrical/beacon_lights_on"))
    snapshot.systems.landing = switch_state(read_any("sim/cockpit2/switches/landing_lights_switch", 8))
    snapshot.systems.taxi = switch_state(read_boolean("sim/cockpit2/switches/taxi_light_on"))
    snapshot.systems.nav = switch_state(read_boolean("sim/cockpit2/switches/navigation_lights_on"))
    snapshot.systems.strobe = switch_state(read_boolean("sim/cockpit2/switches/strobe_lights_on"))
    snapshot.systems.pitot = switch_state(read_first_boolean({
        "sim/cockpit2/ice/ice_pitot_heat_on_pilot",
        "sim/cockpit/switches/pitot_heat_on",
    }))
    snapshot.systems.transponder = snapshot.transponder
    snapshot.brake = brake_state(snapshot.parking_brake)

    local displayed_values = {
        snapshot.ias,
        snapshot.altitude_ft,
        snapshot.vvi_fpm,
        snapshot.agl_ft,
        snapshot.heading,
        snapshot.wind_heading,
        snapshot.wind_speed,
        snapshot.flaps,
        snapshot.fps,
        snapshot.trim,
        snapshot.parking_brake,
    }
    for index = 1, #displayed_values do
        if displayed_values[index] == nil then snapshot.missing_count = snapshot.missing_count + 1 end
    end
    if snapshot.gear.missing then snapshot.missing_count = snapshot.missing_count + 1 end
    for _, value in pairs(snapshot.systems) do
        if value.missing then snapshot.missing_count = snapshot.missing_count + 1 end
    end
    local safety_values = {
        snapshot.autopilot,
        snapshot.engine_fire,
        snapshot.master_warning,
        snapshot.low_fuel,
        snapshot.gear.warning,
        snapshot.master_caution,
        snapshot.low_voltage,
        snapshot.pitot_heat_off,
        snapshot.autopilot_disconnect,
    }
    for index = 1, #safety_values do
        if safety_values[index] == nil
            or (type(safety_values[index]) == "table" and safety_values[index].missing) then
            snapshot.missing_count = snapshot.missing_count + 1
        end
    end
    update_alert(snapshot)
    hud.state.snapshot = snapshot
end

function hud.get_layout(screen_width, screen_height)
    screen_width = tonumber(screen_width) or tonumber(SCREEN_WIDTH) or BASE_WIDTH
    screen_height = tonumber(screen_height) or tonumber(SCREEN_HIGHT) or BASE_HEIGHT
    screen_width = math.max(1, screen_width)
    screen_height = math.max(1, screen_height)

    local scale = configured_number("scale", 1.0, MIN_SCALE, MAX_SCALE)
    scale = math.min(scale, screen_width / BASE_WIDTH, screen_height / BASE_HEIGHT)

    local width = BASE_WIDTH * scale
    local height = BASE_HEIGHT * scale
    local position = hud.position or default_position()
    local offset_x = clamp(tonumber(position.offset_x), 0, POSITION_LIMIT)
    local offset_y = clamp(tonumber(position.offset_y), 0, POSITION_LIMIT)
    local max_x = math.max(0, screen_width - width)
    local max_y = math.max(0, screen_height - height)
    local x
    local y

    if position.anchor == "top_right" or position.anchor == "bottom_right" then
        x = max_x - offset_x
    else
        x = offset_x
    end
    if position.anchor == "top_left" or position.anchor == "top_right" then
        y = max_y - offset_y
    else
        y = offset_y
    end

    return {
        x = clamp(x, 0, max_x),
        y = clamp(y, 0, max_y),
        width = width,
        height = height,
        scale = scale,
        anchor = position.anchor,
        offset_x = offset_x,
        offset_y = offset_y,
        screen_width = screen_width,
        screen_height = screen_height,
    }
end

local function set_color(color, alpha)
    if type(graphics.set_color) ~= "function" then return end
    local opacity = configured_number("opacity", 0.75, 0, 1)
    graphics.set_color(color[1], color[2], color[3], clamp((alpha or 1) * opacity, 0, 1))
end

local function font_for(font_size, scale)
    if font_size == 18 and scale < 0.85 then return 12 end
    if font_size == 12 and scale < 0.85 then return 10 end
    return font_size
end

local function native_text_backend()
    if hud.state.text_backend.disabled then return nil end
    if type(mac_native) ~= "table"
        or type(mac_native.draw_hidpi_string) ~= "function"
        or type(mac_native.measure_hidpi_string) ~= "function" then
        return nil
    end
    return mac_native
end

local function native_elapsed_time()
    if type(mac_native) ~= "table" or type(mac_native.elapsed_time) ~= "function" then
        return nil
    end
    local ok, value = pcall(mac_native.elapsed_time)
    if ok and is_finite_number(value) then return value end
    return nil
end

local function disable_native_text(reason)
    if hud.state.text_backend.disabled then return end
    hud.state.text_backend.disabled = true
    log_once("hidpi_text", "High-DPI text renderer unavailable: " .. tostring(reason or "unknown error"))
end

local function native_text_style(style)
    if style == "value" or style == "numeric" then
        return "sf_mono", 600
    end
    if style == "emphasis" or style == "status" then
        return "sf_pro_text", 600
    end
    return "sf_pro_text", 400
end

local function legacy_text_width(text, font_size, layout)
    if type(measure_string) == "function" then
        local actual_font = font_for(font_size, layout and layout.scale or 1)
        local font_name = actual_font == 10 and "Helvetica_10"
            or actual_font == 18 and "Helvetica_18"
            or "Helvetica_12"
        local ok, width = pcall(measure_string, tostring(text), font_name)
        if ok and is_finite_number(width) then return width end
    end
    return #tostring(text) * font_size * 0.55
end

local function cache_text_width(cache_key, width)
    local cache = hud.state.text_width_cache
    if cache.values[cache_key] ~= nil then return end

    if #cache.keys < TEXT_WIDTH_CACHE_LIMIT then
        cache.keys[#cache.keys + 1] = cache_key
    else
        local slot = cache.next_slot
        cache.values[cache.keys[slot]] = nil
        cache.keys[slot] = cache_key
        cache.next_slot = (slot % TEXT_WIDTH_CACHE_LIMIT) + 1
    end
    cache.values[cache_key] = width
end

local function text_width(text, font_size, layout, style)
    text = tostring(text)
    local cache_key = table.concat({
        tostring(font_size),
        tostring(layout and layout.scale or 1),
        tostring(style or ""),
        text,
    }, "\31")
    local cached_width = hud.state.text_width_cache.values[cache_key]
    if cached_width ~= nil then
        return cached_width
    end

    local native = native_text_backend()
    if native ~= nil and layout ~= nil then
        local family, weight = native_text_style(style)
        local requested_size = font_size * layout.scale
        local ok, width = pcall(
            native.measure_hidpi_string,
            text,
            requested_size,
            family,
            weight
        )
        if ok and is_finite_number(width) then
            cache_text_width(cache_key, width)
            return width
        end
        if not ok then
            disable_native_text(width)
        else
            disable_native_text("measure returned no width")
        end
    end
    local width = legacy_text_width(text, font_size, layout)
    cache_text_width(cache_key, width)
    return width
end

local function draw_legacy_text(actual_font, x, y, text)
    if actual_font == 10 then
        draw_string_Helvetica_10(x, y, text)
    elseif actual_font == 18 then
        draw_string_Helvetica_18(x, y, text)
    else
        draw_string_Helvetica_12(x, y, text)
    end
end

local function draw_text(font_size, x, y, text, color, layout, style)
    set_color(color or COLORS.info, 1)
    local draw_x = math.floor(layout.x + x * layout.scale)
    local draw_y = math.floor(layout.y + y * layout.scale)
    text = tostring(text or "")

    local native = native_text_backend()
    if native ~= nil then
        local family, weight = native_text_style(style)
        local ok, rendered, deferred = pcall(
            native.draw_hidpi_string,
            draw_x,
            draw_y,
            text,
            font_size * layout.scale,
            family,
            weight
        )
        if ok and rendered == true then
            return
        end
        if not ok then
            disable_native_text(rendered)
        elseif rendered ~= true and deferred ~= true then
            disable_native_text("draw returned false")
        end
    end

    draw_legacy_text(font_for(font_size, layout.scale), draw_x, draw_y, text)
end

local function draw_centered(font_size, x, y, width, text, color, layout, style)
    local measured = text_width(text, font_size, layout, style)
    local centered_x = x + (width - measured / layout.scale) * 0.5
    draw_text(font_size, centered_x, y, text, color, layout, style)
end

local function draw_text_group(draw_group)
    local native = native_text_backend()
    local batch_started = false
    if native ~= nil and type(native.begin_hidpi_frame) == "function" then
        local batch_ok, started = pcall(native.begin_hidpi_frame)
        batch_started = batch_ok and started == true
    end

    local ok, err = pcall(draw_group)

    if batch_started and type(native.end_hidpi_frame) == "function" then
        local end_ok, ended = pcall(native.end_hidpi_frame)
        if not end_ok then
            disable_native_text(ended)
        elseif ended == false then
            disable_native_text("batched draw returned false")
        end
    end
    if not ok then error(err) end
end

local function level_color(level)
    if level == "normal" then return COLORS.normal end
    if level == "caution" then return COLORS.caution end
    if level == "warning" then return COLORS.warning end
    if level == "unavailable" then return COLORS.muted end
    return COLORS.info
end

local function draw_status_cell(x, y, width, top, bottom, status, layout)
    status = status or {text = "--", level = "unavailable"}
    draw_centered(10, x, y + 27, width, top, COLORS.info, layout, "label")
    if bottom ~= nil and bottom ~= "" then
        draw_centered(10, x, y + 15, width, bottom, COLORS.info, layout, "label")
    end
    draw_centered(12, x, y + 2, width, status.text, level_color(status.level), layout, "status")
end

local function localized_status(status)
    local l = labels()
    local map = {
        UP = l.up,
        DOWN = l.down,
        TRANSIT = l.transit,
        WARNING = l.gear_warning,
        UNAVAILABLE = l.unavailable,
    }
    return map[status] or status
end

local function draw_metric(x, y, width, label, value, format, level, layout)
    draw_centered(12, x, y + 40, width, label, COLORS.muted, layout, "label")
    local text = "--"
    if value ~= nil and format ~= nil then
        local ok, formatted = pcall(string.format, format, value)
        if ok then text = formatted end
    elseif type(value) == "string" then
        text = value
    end
    draw_centered(18, x, y + 13, width, text, level_color(level or "info"), layout, "value")
end

local function draw_alert_bar(snapshot, layout)
    local alert = snapshot and snapshot.alert or nil
    local l = labels()
    local level = alert and alert.level or "normal"
    local message = alert and alert.message or l.status_ok
    local fill = alert and level_color(level) or COLORS.normal
    set_color(fill, alert and 0.16 or 0.08)
    graphics.draw_rectangle(
        layout.x + 1,
        layout.y + (BASE_HEIGHT - ALERT_HEIGHT) * layout.scale,
        layout.x + layout.width - 1,
        layout.y + layout.height - 1
    )
    draw_text_group(function()
        draw_text(12, 10, BASE_HEIGHT - ALERT_HEIGHT + 7, message, fill, layout, "emphasis")
        if snapshot and snapshot.autopilot then
            draw_text(10, 370, BASE_HEIGHT - ALERT_HEIGHT + 8,
                l.ap .. " " .. snapshot.autopilot.text,
                level_color(snapshot.autopilot.level), layout, "status")
        end
        if hud.state.edit_mode then
            draw_text(10, BASE_WIDTH - 102, BASE_HEIGHT - ALERT_HEIGHT + 8,
                l.edit_mode, COLORS.caution, layout, "status")
        end
    end)
end

local function draw_system_row(snapshot, layout)
    local l = labels()
    local y = AIRCRAFT_HEIGHT + FLIGHT_HEIGHT + 4
    local cells = {
        {"fuel", "pump", snapshot.systems.fuel_pump},
        {"beacon", "", snapshot.systems.beacon},
        {"landing", "", snapshot.systems.landing},
        {"taxi", "", snapshot.systems.taxi},
        {"nav", "", snapshot.systems.nav},
        {"strobe", "", snapshot.systems.strobe},
        {"pitot", "heat", snapshot.systems.pitot},
        {"txp", "alt", snapshot.systems.transponder},
    }
    local x = 8
    local width = 67
    for index = 1, #cells do
        local cell = cells[index]
        draw_status_cell(x, y, width, l[cell[1]], l[cell[2]], cell[3], layout)
        x = x + 68
    end
end

local function draw_flight_row(snapshot, layout)
    local l = labels()
    local y = AIRCRAFT_HEIGHT + 4
    draw_metric(10, y, 80, l.ias, snapshot.ias, "%03.0f", "info", layout)
    draw_metric(94, y, 90, l.alt, snapshot.altitude_ft, "%05.0f", "info", layout)
    draw_metric(188, y, 100, l.vs, snapshot.vvi_fpm, "%+05.0f", "info", layout)
    draw_metric(292, y, 80, l.agl, snapshot.agl_ft, "%04.0f", "info", layout)
    draw_metric(376, y, 78, l.hdg, snapshot.heading, "%03.0f", "info", layout)

    local wind_text = "--/--"
    if snapshot.wind_heading ~= nil and snapshot.wind_speed ~= nil then
        wind_text = string.format("%03.0f/%02.0f", snapshot.wind_heading, snapshot.wind_speed)
    end
    draw_metric(458, y, 92, l.wind, wind_text, nil, "info", layout)
end

local function draw_aircraft_row(snapshot, layout)
    local l = labels()
    local y = 4
    local flap_text = nil
    if snapshot.flaps ~= nil then
        flap_text = string.format("%02d%%", math.floor(clamp(snapshot.flaps, 0, 1) * 100 + 0.5))
    end
    draw_metric(10, y, 95, l.flaps, flap_text, nil, "info", layout)
    draw_metric(110, y, 90, l.gear, snapshot.gear.status == "UNAVAILABLE" and nil or localized_status(snapshot.gear.status), nil, snapshot.gear.level, layout)
    draw_metric(210, y, 95, l.trim, snapshot.trim, "%+.2f", "info", layout)

    local fps_level = "info"
    if snapshot.fps ~= nil then
        if snapshot.fps < 30 then fps_level = "warning"
        elseif snapshot.fps < 45 then fps_level = "caution"
        else fps_level = "normal" end
    end
    draw_metric(315, y, 80, l.fps, snapshot.fps, "%02.0f", fps_level, layout)
    draw_status_cell(405, y + 4, 145, l.park, l.brake, snapshot.brake, layout)
end

local function draw_overlay()
    if not hud.state.visible then return end
    local layout = hud.get_layout()
    local snapshot = hud.state.snapshot or {
        systems = {},
        gear = {status = "UNAVAILABLE", level = "unavailable"},
        transponder = {text = "--", level = "unavailable"},
        brake = {text = "--", level = "unavailable"},
        missing_count = 1,
    }
    local l = labels()

    set_color(COLORS.background, 0.92)
    graphics.draw_rectangle(layout.x, layout.y, layout.x + layout.width, layout.y + layout.height)
    set_color(COLORS.border, 0.20)
    graphics.draw_line(layout.x, layout.y, layout.x + layout.width, layout.y, 1)
    graphics.draw_line(layout.x, layout.y + layout.height, layout.x + layout.width, layout.y + layout.height, 1)
    graphics.draw_line(layout.x, layout.y, layout.x, layout.y + layout.height, 1)
    graphics.draw_line(layout.x + layout.width, layout.y, layout.x + layout.width, layout.y + layout.height, 1)

    local separator_color = {0.55, 0.60, 0.68}
    set_color(separator_color, 0.18)
    graphics.draw_line(layout.x, layout.y + (AIRCRAFT_HEIGHT) * layout.scale, layout.x + layout.width, layout.y + AIRCRAFT_HEIGHT * layout.scale, 1)
    graphics.draw_line(layout.x, layout.y + (AIRCRAFT_HEIGHT + FLIGHT_HEIGHT) * layout.scale, layout.x + layout.width, layout.y + (AIRCRAFT_HEIGHT + FLIGHT_HEIGHT) * layout.scale, 1)
    graphics.draw_line(layout.x, layout.y + (BASE_HEIGHT - ALERT_HEIGHT) * layout.scale, layout.x + layout.width, layout.y + (BASE_HEIGHT - ALERT_HEIGHT) * layout.scale, 1)

    draw_alert_bar(snapshot, layout)
    draw_text_group(function()
        draw_system_row(snapshot, layout)
        draw_flight_row(snapshot, layout)
        draw_aircraft_row(snapshot, layout)
    end)

    if hud.state.edit_mode then
        set_color(COLORS.caution, 0.85)
        graphics.draw_line(layout.x - 2, layout.y - 2, layout.x + layout.width + 2, layout.y - 2, 2)
        graphics.draw_line(layout.x - 2, layout.y + layout.height + 2, layout.x + layout.width + 2, layout.y + layout.height + 2, 2)
        draw_text_group(function()
            draw_text(10, 10, 1, l.drag_hint, COLORS.caution, layout)
        end)
    end
end

local function sync_legacy_visibility()
    if type(hud_g1000_visible) == "boolean" and hud_g1000_visible ~= hud.state.visible then
        hud.state.visible = hud_g1000_visible
    end
end

local function origin_to_position(origin_x, origin_y, layout)
    local max_x = math.max(0, layout.screen_width - layout.width)
    local max_y = math.max(0, layout.screen_height - layout.height)
    origin_x = clamp(origin_x, 0, max_x)
    origin_y = clamp(origin_y, 0, max_y)
    local offset_x
    local offset_y

    if layout.anchor == "top_right" or layout.anchor == "bottom_right" then
        offset_x = layout.screen_width - (origin_x + layout.width)
    else
        offset_x = origin_x
    end
    if layout.anchor == "top_left" or layout.anchor == "top_right" then
        offset_y = layout.screen_height - (origin_y + layout.height)
    else
        offset_y = origin_y
    end

    hud.position.offset_x = clamp(offset_x, 0, POSITION_LIMIT)
    hud.position.offset_y = clamp(offset_y, 0, POSITION_LIMIT)
end

local function mouse_inside(layout, x, y)
    return x >= layout.x
        and x <= layout.x + layout.width
        and y >= layout.y
        and y <= layout.y + layout.height
end

function hud.handle_mouse_click()
    if not hud.state.edit_mode then return end
    local layout = hud.get_layout()
    local x = tonumber(MOUSE_X) or 0
    local y = tonumber(MOUSE_Y) or 0
    local status = tostring(MOUSE_STATUS or "up")

    if status == "down" and mouse_inside(layout, x, y) then
        hud.state.dragging = true
        hud.state.drag_start_x = x
        hud.state.drag_start_y = y
        hud.state.drag_start_origin_x = layout.x
        hud.state.drag_start_origin_y = layout.y
        RESUME_MOUSE_CLICK = true
        return
    end

    if not hud.state.dragging then return end

    if status == "drag" or status == "down" then
        local next_x = hud.state.drag_start_origin_x + (x - hud.state.drag_start_x)
        local next_y = hud.state.drag_start_origin_y + (y - hud.state.drag_start_y)
        origin_to_position(next_x, next_y, layout)
        RESUME_MOUSE_CLICK = true
    elseif status == "up" then
        local next_x = hud.state.drag_start_origin_x + (x - hud.state.drag_start_x)
        local next_y = hud.state.drag_start_origin_y + (y - hud.state.drag_start_y)
        origin_to_position(next_x, next_y, layout)
        hud.state.dragging = false
        save_position_config()
        RESUME_MOUSE_CLICK = true
    end
end

function hud.update()
    sync_legacy_visibility()
    if not hud.state.visible then
        -- Do not keep polling dozens of DataRefs for a hidden overlay.
        hud.state.last_update_time = nil
        return
    end

    -- Keep the display readable and avoid coupling telemetry polling to the
    -- simulator frame rate. One clock DataRef is read per frame; the full
    -- snapshot and its numeric values are refreshed at 5 Hz.
    local sim_time = read_number("sim/time/total_running_time_sec")
    -- XPLMGetElapsedTime is a wall timer, so updates continue while the sim
    -- is paused. If neither clock is available, do not throttle and risk a
    -- permanently stale HUD on compatibility hosts.
    local clock = native_elapsed_time() or sim_time
    local last_update_time = hud.state.last_update_time
    if clock ~= nil
        and last_update_time ~= nil
        and clock >= last_update_time
        and clock - last_update_time < UPDATE_INTERVAL_SECONDS then
        return
    end
    hud.state.last_update_time = clock

    local ok, err = pcall(update_snapshot, sim_time)
    if not ok then
        log_once("update_error", "update skipped: " .. tostring(err))
    end
end

function hud.draw()
    sync_legacy_visibility()
    if not hud.state.visible then return end
    local ok, err = pcall(draw_overlay)
    if not ok then
        log_once("draw_error", "draw skipped: " .. tostring(err))
    end
end

function hud.toggle()
    hud.state.visible = not hud.state.visible
    hud_g1000_visible = hud.state.visible
end

function hud.edit_position()
    hud.state.edit_mode = not hud.state.edit_mode
    if not hud.state.edit_mode then
        hud.state.dragging = false
    end
end

function hud.reset_position()
    hud.position = default_position()
    hud.state.dragging = false
    save_position_config()
end

-- Historical entry points remain available for existing commands/macros.
function hud_g1000_toggle()
    hud.toggle()
end

function hud_g1000_edit_position()
    hud.edit_position()
end

function hud_g1000_reset_position()
    hud.reset_position()
end

load_position_config()

create_command("FlyWithLua/HUD-G1000/toggle", "Show or hide HUD-G1000", "hud_g1000_toggle()", "", "")
create_command("FlyWithLua/HUD-G1000/edit_position", "Enter or leave HUD-G1000 position edit mode", "hud_g1000_edit_position()", "", "")
create_command("FlyWithLua/HUD-G1000/reset_position", "Reset HUD-G1000 position", "hud_g1000_reset_position()", "", "")

do_every_frame("hud_g1000.update()")
do_every_draw("hud_g1000.draw()")
do_on_mouse_click("hud_g1000.handle_mouse_click()")
