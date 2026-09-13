-- Baron 58 G1000 ESP & PFD Setup (FlyWithLua)
if PLANE_ICAO == "BE58" then
    -- データレフの定義
    dataref("g1000_esp_enabled", "sim/cockpit2/autopilot/g1000_esp_enabled", "writable")

    -- X-Plane versions and aircraft packages have used different spellings
    -- for this DataRef. Prefer the correctly spelled path, while retaining
    -- compatibility with installations that still expose the legacy path.
    local wind_style_paths = {
        "sim/cockpit2/EFIS/wind_vector_style",
        "sim/cockpit2/EFIS/wind_vctor_style",
    }
    local wind_style_path
    for _, candidate in ipairs(wind_style_paths) do
        if XPLMFindDataRef(candidate) ~= nil then
            wind_style_path = candidate
            break
        end
    end
    if wind_style_path ~= nil then
        dataref("pfd_wind_style", wind_style_path, "writable")
    else
        logMsg("B58.01.logic: wind style DataRef is unavailable; skipping wind style configuration")
    end

    dataref("pfd_brg1_source", "sim/cockpit2/EFIS/bearing_1_source", "writable")
    dataref("pfd_svt_enabled", "sim/cockpit2/EFIS/pfd_synthetic_vision_enabled", "writable")

    function set_be58_defaults()
        -- ESPをオフにする
        if g1000_esp_enabled ~= 0 then g1000_esp_enabled = 0 end
        
        -- PFDの一括設定
        if pfd_wind_style ~= nil and pfd_wind_style ~= 2 then pfd_wind_style = 2 end   -- 風表示オプション2
        if pfd_brg1_source ~= 3 then pfd_brg1_source = 3 end -- ベアリング1: GPS
        if pfd_svt_enabled ~= 1 then pfd_svt_enabled = 1 end -- シンセティックビジョン: ON
    end

    -- Apply immediately and keep the settings in place while the aircraft is loaded.
    set_be58_defaults()
    do_often("set_be58_defaults()")
end
