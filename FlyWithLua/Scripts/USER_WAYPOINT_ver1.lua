--User waypoint window for X-plane G1000
--Made by Saturns, a X-plane forum user
--Heavily inspired by two scripts "Imugi Get Latitude and Longitude", and "fmc_goto" made by William R. Good(sparker256). Almost half of script is based on his.
--Let me know if you can make any improvement.

if not SUPPORTS_FLOATING_WINDOWS then
    -- to make sure the script doesn't stop old FlyWithLua versions
    logMsg("imgui not supported by your FlyWithLua version")
    return
end

add_macro("User waypoint window", "usr_point_show_wnd()")

local fms_max_entries = 100
local new_entry = 1

local new_lat = 0.0
local new_lon = 0.0

local user_waypoint_window_on = 0
--1=on 0=off
local mfd_pan_clicked = 0
--1=on 0=off
local lat_lon_digits_place = 1
--1 to 16

--local latlon_digits_mod_1 = Reserved for entry no.
--local latlon_digits_mod_2 = Reserved for lat -+ value
local latlon_digits_mod_3 = 10
local latlon_digits_mod_4 = 1
local latlon_digits_mod_5 = 0.166670
local latlon_digits_mod_6 = 0.016667
local latlon_digits_mod_7 = 0.001667
local latlon_digits_mod_8 = 0.000167

--local latlon_digits_mod_9 = Reserved for lon -+ value
local latlon_digits_mod_10 = 100
local latlon_digits_mod_11 = 10 
local latlon_digits_mod_12 = 1
local latlon_digits_mod_13 = 0.166670
local latlon_digits_mod_14 = 0.016667
local latlon_digits_mod_15 = 0.001667
local latlon_digits_mod_16 = 0.000167

local function clamp(value, minimum, maximum)
	return math.max(minimum, math.min(maximum, value))
end

local function clamp_coordinates()
	new_lat = clamp(new_lat, -90.0, 90.0)
	new_lon = clamp(new_lon, -180.0, 180.0)
end

local function get_fms_entry_count()
	if type(XPLMCountFMSEntries) ~= "function" then
		return nil
	end

	local ok, count = pcall(XPLMCountFMSEntries)
	if not ok or type(count) ~= "number" then
		return nil
	end

	return clamp(math.floor(count), 0, fms_max_entries)
end

local function send_waypoint_to_fms()
	if type(XPLMSetFMSEntryLatLon) ~= "function" or
		type(XPLMSetDisplayedFMSEntry) ~= "function" or
		type(XPLMSetDestinationFMSEntry) ~= "function" then
		logMsg("User waypoint: FMS navigation API is unavailable in this FlyWithLua build")
		return
	end

	clamp_coordinates()

	-- The XPLM FMS API is zero-based; the UI intentionally shows 1..9.
	local fms_index = new_entry - 1
	if fms_index < 0 or fms_index >= fms_max_entries then
		logMsg("User waypoint: invalid FMS entry number")
		return
	end

	local entry_count = get_fms_entry_count()
	if entry_count ~= nil and fms_index > entry_count then
		logMsg("User waypoint: FMS entries must be contiguous; select an existing entry or the next entry")
		return
	end

	local ok, error_message = pcall(XPLMSetFMSEntryLatLon, fms_index, new_lat, new_lon, 0)
	if not ok then
		logMsg("User waypoint: unable to write FMS entry: " .. tostring(error_message))
		return
	end

	local displayed_ok, displayed_error = pcall(XPLMSetDisplayedFMSEntry, fms_index)
	local destination_ok, destination_error = pcall(XPLMSetDestinationFMSEntry, fms_index)
	if not displayed_ok or not destination_ok then
		logMsg("User waypoint: unable to select FMS entry: " ..
			tostring(displayed_error or destination_error))
		return
	end

	logMsg("FlyWithLua Info: Sending Lat/Lon to FMS entry " .. fms_index)
end

function MFD_FMS_outerL()
if user_waypoint_window_on == 1 and lat_lon_digits_place >= 2 and lat_lon_digits_place <= 16 then
	lat_lon_digits_place = lat_lon_digits_place - 1
end 

if user_waypoint_window_on == 0 then
	command_once("sim/GPS/g1000n3_fms_outer_down")
end
end

function MFD_FMS_outerR()
if user_waypoint_window_on == 1 and lat_lon_digits_place >= 1 and lat_lon_digits_place <= 15 then
	lat_lon_digits_place = lat_lon_digits_place + 1
end 

if user_waypoint_window_on == 0 then
	command_once("sim/GPS/g1000n3_fms_outer_up")
end
end


function MFD_FMS_innerR()

if user_waypoint_window_on == 1 and lat_lon_digits_place == 1 and new_entry < 9 then
	new_entry = new_entry + 1
end

if user_waypoint_window_on == 1 and lat_lon_digits_place == 2 then
	new_lat = new_lat*-1
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 3 then
	new_lat = new_lat + latlon_digits_mod_3
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 4 then
	new_lat = new_lat + latlon_digits_mod_4
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 5 then
	new_lat = new_lat + latlon_digits_mod_5
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 6 then
	new_lat = new_lat + latlon_digits_mod_6
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 7 then
	new_lat = new_lat + latlon_digits_mod_7
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 8 then
	new_lat = new_lat + latlon_digits_mod_8
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 9 then
	new_lon = new_lon*-1
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 10 then
	new_lon = new_lon + latlon_digits_mod_10
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 11 then
	new_lon = new_lon + latlon_digits_mod_11
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 12 then
	new_lon = new_lon + latlon_digits_mod_12
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 13 then
	new_lon = new_lon + latlon_digits_mod_13
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 14 then
	new_lon = new_lon + latlon_digits_mod_14
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 15 then
	new_lon = new_lon + latlon_digits_mod_15
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 16 then
	new_lon = new_lon + latlon_digits_mod_16
end 
	clamp_coordinates()

if user_waypoint_window_on == 0 then
	command_once("sim/GPS/g1000n3_fms_inner_down")
end
end

function MFD_FMS_innerL()

if user_waypoint_window_on == 1 and lat_lon_digits_place == 1 and new_entry > 1 then
	new_entry = new_entry - 1
end

if user_waypoint_window_on == 1 and lat_lon_digits_place == 2 then
	new_lat = new_lat*-1
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 3 then
	new_lat = new_lat - latlon_digits_mod_3
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 4 then
	new_lat = new_lat - latlon_digits_mod_4
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 5 then
	new_lat = new_lat - latlon_digits_mod_5
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 6 then
	new_lat = new_lat - latlon_digits_mod_6
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 7 then
	new_lat = new_lat - latlon_digits_mod_7
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 8 then
	new_lat = new_lat - latlon_digits_mod_8
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 9 then
	new_lon = new_lon*-1
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 10 then
	new_lon = new_lon - latlon_digits_mod_10
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 11 then
	new_lon = new_lon - latlon_digits_mod_11
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 12 then
	new_lon = new_lon - latlon_digits_mod_12
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 13 then
	new_lon = new_lon - latlon_digits_mod_13
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 14 then
	new_lon = new_lon - latlon_digits_mod_14
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 15 then
	new_lon = new_lon - latlon_digits_mod_15
elseif user_waypoint_window_on == 1 and lat_lon_digits_place == 16 then
	new_lon = new_lon - latlon_digits_mod_16
end 
	clamp_coordinates()

if user_waypoint_window_on == 0 then
	command_once("sim/GPS/g1000n3_fms_inner_up")
end
end

function MFD_ENT()
	if user_waypoint_window_on == 0 and mfd_pan_clicked == 1 then
	usr_point_show_wnd()
	get_location()
	elseif mfd_pan_clicked == 0 then
	command_once("sim/GPS/g1000n3_ent")
end
end

function MFD_PAN()
	if mfd_pan_clicked == 0 and user_waypoint_window_on == 0 then
	mfd_pan_clicked = 1
	command_once("sim/GPS/g1000n3_pan_push")
	elseif mfd_pan_clicked == 1 then
	mfd_pan_clicked = 0
	command_once("sim/GPS/g1000n3_pan_push")
end
end

function MFD_CLR()
	if user_waypoint_window_on == 0 then
	command_once("sim/GPS/g1000n3_clr")
	elseif user_waypoint_window_on == 1 then
        float_wnd_destroy(usr_point_wnd)
        usr_point_wnd = nil
        user_waypoint_window_on = 0
    end
end

function usr_point_on_build(usr_point_wnd, x, y)
-- -----------------------------------added 4/02 begin -------------------------------------------------
    clamp_coordinates()
local floor_latitude = math.floor(new_lat)
local floor_longitude = math.floor(new_lon)

local sim_mod_latitude = math.abs(new_lat)
local sim_mod_longitude = math.abs(new_lon)

local	lat = math.floor(sim_mod_latitude)
local	lat_deg_decimal = (sim_mod_latitude - lat)*60
local	lat_deg_decimal2 = math.floor(lat_deg_decimal)
local	lat_deg_decimal3 = (lat_deg_decimal - lat_deg_decimal2)*100
--This is for DDM(Degree Decimal Minute) lat coordinate

local	lon = math.floor(sim_mod_longitude)
local	lon_deg_decimal = (sim_mod_longitude - lon)*60
local	lon_deg_decimal2 = math.floor(lon_deg_decimal)
local	lon_deg_decimal3 = (lon_deg_decimal - lon_deg_decimal2)*100

--This is for DDM(Degree Decimal Minute) lon coordinate
				imgui.Separator()
	imgui.TextUnformatted(" USER WAYPOINT")
			imgui.PushStyleColor(imgui.constant.Col.Text, 0xFFFFD700)
	imgui.TextUnformatted("----")
	imgui.SameLine()
	imgui.TextUnformatted("TEMPORARY [V]")
	        imgui.PopStyleColor()
	imgui.TextUnformatted("WAYPOINT TYPE")
	imgui.SameLine()
			imgui.PushStyleColor(imgui.constant.Col.Text, 0xFFFFD700)	
	imgui.TextUnformatted("LAT/LON")
	        imgui.PopStyleColor()
				imgui.Separator()
	imgui.TextUnformatted("")
				imgui.Separator()
	imgui.TextUnformatted("COMMENT")
			imgui.PushStyleColor(imgui.constant.Col.Text, 0xFFFFD700)	
	imgui.TextUnformatted("----")
	        imgui.PopStyleColor()
				imgui.Separator()
	imgui.TextUnformatted("")
				imgui.Separator()
	imgui.TextUnformatted("INFORMATION")
			imgui.PushStyleColor(imgui.constant.Col.Text, 0xFFFFD700)		
	imgui.TextUnformatted("FPL Entry No: " .. new_entry)
	        imgui.PopStyleColor()
	imgui.TextUnformatted("")
		
	if floor_latitude < 0 then
			imgui.PushStyleColor(imgui.constant.Col.Text, 0xFFFFD700)	
	imgui.TextUnformatted(string.format("S"));
	imgui.SameLine()
	imgui.TextUnformatted(string.format(" %.2d°", lat));
	imgui.SameLine()
	imgui.TextUnformatted(string.format("%.2d.", lat_deg_decimal2));
	imgui.SameLine()
	imgui.TextUnformatted(string.format("%.2d'", lat_deg_decimal3));
		    imgui.PopStyleColor()

	elseif floor_latitude > 0 then
			imgui.PushStyleColor(imgui.constant.Col.Text, 0xFFFFD700)	
	imgui.TextUnformatted(string.format("N"));
	imgui.SameLine()
	imgui.TextUnformatted(string.format(" %.2d°", lat));
	imgui.SameLine()
	imgui.TextUnformatted(string.format("%.2d.", lat_deg_decimal2));
	imgui.SameLine()
	imgui.TextUnformatted(string.format("%.2d'", lat_deg_decimal3));
		    imgui.PopStyleColor()
--Drawing N/S
	end
	if floor_longitude < 0 then
			imgui.PushStyleColor(imgui.constant.Col.Text, 0xFFFFD700)	
	imgui.TextUnformatted(string.format("W"));
	imgui.SameLine()
	imgui.TextUnformatted(string.format("%.3d°", lon));
	imgui.SameLine()
	imgui.TextUnformatted(string.format("%.2d.", lon_deg_decimal2));
	imgui.SameLine()
	imgui.TextUnformatted(string.format("%.2d'", lon_deg_decimal3));
		    imgui.PopStyleColor()
	elseif floor_longitude > 0 then
			imgui.PushStyleColor(imgui.constant.Col.Text, 0xFFFFD700)	
	imgui.TextUnformatted(string.format("E"));
	imgui.SameLine()
	imgui.TextUnformatted(string.format("%.2d°", lon));
	imgui.SameLine()
	imgui.TextUnformatted(string.format("%.2d.", lon_deg_decimal2));
	imgui.SameLine()
	imgui.TextUnformatted(string.format("%.2d'", lon_deg_decimal3));
		    imgui.PopStyleColor()
--Drawing E/W
	end	

				imgui.Separator()
-- ---------------------------------------added 4/02 end ----------------------------------------------
-- -----------------------------test only script-------------------------------------------------------
--		imgui.TextUnformatted("(lat_lon_digits_place): " .. lat_lon_digits_place)
--		imgui.TextUnformatted("(user_waypoint_window_on): " .. user_waypoint_window_on)
--		imgui.TextUnformatted("(mfd_pan_clicked): " .. mfd_pan_clicked)	
-- upper 3 lines are only for the test purpose. Activate it if you need to see whats going on
-- --------------------------added 4/3 begin------------------------
	if lat_lon_digits_place == 1 then
	imgui.DrawList_AddRect(105, 160, 113, 180, 0xFFFFD700, 1, 1, 1);
	elseif lat_lon_digits_place == 2 then
	imgui.DrawList_AddRect(7, 192, 17, 212, 0xFFFFD700, 1, 1, 1);
	elseif lat_lon_digits_place == 3 then
	imgui.DrawList_AddRect(29, 192, 37, 212, 0xFFFFD700, 1, 1, 1);
	elseif lat_lon_digits_place == 4 then
	imgui.DrawList_AddRect(37, 192, 45, 212, 0xFFFFD700, 1, 1, 1);
	elseif lat_lon_digits_place == 5 then
	imgui.DrawList_AddRect(58, 192, 66, 212, 0xFFFFD700, 1, 1, 1);
	elseif lat_lon_digits_place == 6 then
	imgui.DrawList_AddRect(66, 192, 74, 212, 0xFFFFD700, 1, 1, 1);
	elseif lat_lon_digits_place == 7 then
	imgui.DrawList_AddRect(88, 192, 96, 212, 0xFFFFD700, 1, 1, 1);
	elseif lat_lon_digits_place == 8 then
	imgui.DrawList_AddRect(95, 192, 103, 212, 0xFFFFD700, 1, 1, 1);
	elseif lat_lon_digits_place == 9 then
	--line change
	imgui.DrawList_AddRect(7, 212, 17, 232, 0xFFFFD700, 1, 1, 1);
	elseif lat_lon_digits_place == 10 then
	imgui.DrawList_AddRect(21, 212, 29, 232, 0xFFFFD700, 1, 1, 1);
	elseif lat_lon_digits_place == 11 then
	imgui.DrawList_AddRect(29, 212, 37, 232, 0xFFFFD700, 1, 1, 1);
	elseif lat_lon_digits_place == 12 then
	imgui.DrawList_AddRect(37, 212, 45, 232, 0xFFFFD700, 1, 1, 1);
	elseif lat_lon_digits_place == 13 then
	imgui.DrawList_AddRect(58, 212, 66, 232, 0xFFFFD700, 1, 1, 1);
	elseif lat_lon_digits_place == 14 then
	imgui.DrawList_AddRect(66, 212, 74, 232, 0xFFFFD700, 1, 1, 1);
	elseif lat_lon_digits_place == 15 then
	imgui.DrawList_AddRect(88, 212, 96, 232, 0xFFFFD700, 1, 1, 1);
	elseif lat_lon_digits_place == 16 then
	imgui.DrawList_AddRect(95, 212, 103, 232, 0xFFFFD700, 1, 1, 1);
	end

-- --------------------------aaded 4/3 end------------------------

  if imgui.Button("MFD FMS OUTER KNOB <<") then
        MFD_FMS_outerL()
  end
     imgui.SameLine()
  if imgui.Button(">> MFD FMS OUTER KNOB") then
        MFD_FMS_outerR()
  end
  
  if imgui.Button("MFD FMS INNER KNOB  <") then
        MFD_FMS_innerL()
  end
     imgui.SameLine()
  if imgui.Button(">  MFD FMS INNER KNOB") then
        MFD_FMS_innerR()
  end

  if imgui.Button("Send to FPL entry") then
        send_waypoint_to_fms()
    end

end

function get_location()
	new_lat = tonumber(LATITUDE) or 0.0
	new_lon = tonumber(LONGITUDE) or 0.0
	clamp_coordinates()
end


usr_point_wnd = nil

function usr_point_show_wnd()
    if usr_point_wnd then
        return
    end

    local window = float_wnd_create(340, 400, 1, true)
    if not window then
        logMsg("User waypoint: unable to create floating window")
        return
    end

    usr_point_wnd = window
    float_wnd_set_title(usr_point_wnd, " ")
    float_wnd_set_imgui_builder(usr_point_wnd, "usr_point_on_build")
	float_wnd_set_onclose(usr_point_wnd, "closed_usr_point_wnd")
	user_waypoint_window_on = 1
	get_location()
end

function usr_point_hide_wnd()
    if usr_point_wnd then
        float_wnd_destroy(usr_point_wnd)
        usr_point_wnd = nil
        user_waypoint_window_on = 0
    end
end


function closed_usr_point_wnd(usr_point_wnd)
    if _G.usr_point_wnd == usr_point_wnd then
        _G.usr_point_wnd = nil
    end
    user_waypoint_window_on = 0
--this should make the user_waypoint_window_on = 0
    -- This function is called when the user closes the window. Drawing or calling imgui
end


create_command("User_way_point/MFD_CLR", "MFD_CLR", "MFD_CLR()", "", "")
create_command("User_way_point/MFD_ENT", "MFD_ENT", "MFD_ENT()", "", "")
create_command("User_way_point/MFD_PAN", "MFD_PAN", "MFD_PAN()", "", "")
create_command("User_way_point/MFD_FMS_outer_R", "MFD_FMS_outer_R", "MFD_FMS_outerR()", "", "")
create_command("User_way_point/MFD_FMS_outer_L", "MFD_FMS_outer_L", "MFD_FMS_outerL()", "", "")
create_command("User_way_point/MFD_FMS_inner_R", "MFD_FMS_inner_R", "MFD_FMS_innerR()", "", "")
create_command("User_way_point/MFD_FMS_inner_L", "MFD_FMS_inner_L", "MFD_FMS_innerL()", "", "")
