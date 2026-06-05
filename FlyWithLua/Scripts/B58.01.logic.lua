-- Baron 58 G1000 ESP & PFD Setup (FlyWithLua)
if PLANE_ICAO == "BE58" then
    -- データレフの定義
    dataref("g1000_esp_enabled", "sim/cockpit2/autopilot/g1000_esp_enabled", "writable")
    dataref("pfd_wind_style", "sim/cockpit2/EFIS/wind_vctor_style", "writable")
    dataref("pfd_brg1_source", "sim/cockpit2/EFIS/bearing_1_source", "writable")
    dataref("pfd_svt_enabled", "sim/cockpit2/EFIS/pfd_synthetic_vision_enabled", "writable")

    function set_be58_defaults()
        -- ESPをオフにする
        if g1000_esp_enabled ~= 0 then g1000_esp_enabled = 0 end
        
        -- PFDの一括設定
        if pfd_wind_style ~= 2 then pfd_wind_style = 2 end   -- 風表示オプション2
        if pfd_brg1_source ~= 3 then pfd_brg1_source = 3 end -- ベアリング1: GPS
        if pfd_svt_enabled ~= 1 then pfd_svt_enabled = 1 end -- シンセティックビジョン: ON
    end

    -- 毎秒チェックして設定を適用・維持する
    -- do_often("set_be58_defaults()")
end
