--------------------------------
-- Script:  3jFPS12 for X-Plane 12
-- Version: 1.0
-- Date:    2022-12-23
-- By:      Jörn-Jören Jörensön, email: j@3j.wtf
-- basic idea was inspired by Auto_LOD_1.3 by: oe3gsu@x-plane.at
--------------------------------

-----------------------------------------------------------------------------------------------
-- for other scripts to detect that this plugin is installed and running
jjjFPS_3jFPS12Running = true
-----------------------------------------------------------------------------------------------

-----------------------------------------------------------------------------------------------
-- for other scripts to tell 3jFPS to ignore stutters for a moment
--   (can be set to true by any other script and 3jFPS will ignore changes of FPS for a second)
jjjFPS_3jFPSignoreStutters = false
-----------------------------------------------------------------------------------------------


local jjjFPS_pluginName = "3jFPS12"
require("graphics")
require("jjjLib1")
if jjjLib1.version == nil or jjjLib1.version() < 1.7 then
	do_every_draw('draw_string(20, SCREEN_HIGHT - 104, "Plugin ' .. jjjFPS_pluginName .. ': Requires library \'3jLib1\' version 1.7 or higher! Please search for \'3jLib1\' on x-plane.org, download and install current version of library.")')
	jjjFPS_3jFPS12Running = false
	return
end

local jjjFPS_plId = jjjLib1.getPlId(jjjFPS_pluginName)

if XPLANE_VERSION < 12000 then
	jjjLib1.warning(jjjFPS_plId, jjjFPS_pluginName .. ': This plugin is for X-Plane 12 only!')
	return
end

local jjjFPS_UHD = 0
if SCREEN_HIGHT >= 1440 and SCREEN_WIDTH >= 3000 then
	jjjFPS_UHD = 1
end

local jjjFPS_saveMode = true -- Set this to true, if you want the plugin to keep the selected mode (Auto, Max FPS, Max Qual., OFF) after restart of X-Plane or new flight

local jjjFPS_profile = "-"

jjjLib1.addParam(jjjFPS_plId, "xpver",   {["save"]="global", ["autosave"]=true, ["dflt"]=""}, "")
jjjLib1.addParam(jjjFPS_plId, "profile", {["save"]="global", ["autosave"]=true, ["dflt"]="A"}, "")
jjjLib1.addParam(jjjFPS_plId, "profNVR", {["save"]="global", ["autosave"]=true, ["dflt"]="A"}, "")
jjjLib1.addParam(jjjFPS_plId, "profVR",  {["save"]="global", ["autosave"]=true, ["dflt"]="D"}, "")
jjjLib1.addParam(jjjFPS_plId, "mode",    {["save"]="global", ["autosave"]=true, ["dflt"]="on"}, "")


jjjLib1.addParam(jjjFPS_plId, "disL", {["save"]="global-profile", ["autosave"]=true, ["dflt"]=false, ["info"]="display: show labels"}, "")
jjjLib1.addParam(jjjFPS_plId, "disN", {["save"]="global-profile", ["autosave"]=true, ["dflt"]=true,  ["info"]="display: show numbers"}, "jjjFPS_setShowNumbers()")
jjjLib1.addParam(jjjFPS_plId, "disG", {["save"]="global-profile", ["autosave"]=true, ["dflt"]=true,  ["info"]="display: show graphic"}, "jjjFPS_setShowGraphic()")
jjjLib1.addParam(jjjFPS_plId, "disU", {["save"]="global-profile", ["autosave"]=true, ["dflt"]=true,  ["info"]="display: show utilisation of CPU and GPU"}, "")
jjjLib1.addParam(jjjFPS_plId, "disD", {["save"]="global-profile", ["autosave"]=true, ["dflt"]=false, ["info"]="display: show details"}, "")
jjjLib1.addParam(jjjFPS_plId, "disM", {["save"]="global-profile", ["autosave"]=true, ["dflt"]="alw", ["info"]="display mode: show always, mouse-over, bad-FPS"}, "")

jjjLib1.addParam(jjjFPS_plId, "Fmn", {["save"]="global-profile", ["dflt"]=28, ["mn"]=10, ["mx"]=90, ["stp"]=1, ["slMn"]=10, ["slMx"]=120, ["gdMn"]=23, ["gdMx"]=55, ["fmt"]="%2.0f", ["parMx"]="Fmx", ["info"]="minimum FPS"}, "")
jjjLib1.addParam(jjjFPS_plId, "Fmx", {["save"]="global-profile", ["dflt"]=31, ["mn"]=20, ["mx"]=120, ["stp"]=1, ["slMn"]=10, ["slMx"]=120, ["gdMn"]=25, ["gdMx"]=60, ["fmt"]="%2.0f", ["parMn"]="Fmn", ["info"]="maximum FPS"}, "")
jjjLib1.addParam(jjjFPS_plId, "Ftg", {["save"]="global-profile", ["dflt"]=30, ["mn"]=20, ["mx"]=120, ["stp"]=1, ["slMn"]=10, ["slMx"]=120, ["gdMn"]=30, ["gdMx"]=90, ["fmt"]="%2.0f", ["info"]="target FPS"}, "")

jjjLib1.addParam(jjjFPS_plId, "smartM", {["save"]="global-profile", ["dflt"]=true, ["info"]="smart mode on/off"}, "jjjFPS_setSmartMode()")
jjjLib1.addParam(jjjFPS_plId, "CPUhdrm", {["save"]="global-profile", ["dflt"]=20, ["mn"]=0, ["mx"]=70, ["stp"]=1, ["slMn"]=0, ["slMx"]=100, ["gdMn"]=10, ["gdMx"]=25, ["fmt"]="%2.0f", ["info"]="CPU headroom in %"}, "")
jjjLib1.addParam(jjjFPS_plId, "GPUhdrm", {["save"]="global-profile", ["dflt"]=10, ["mn"]=0, ["mx"]=70, ["stp"]=1, ["slMn"]=0, ["slMx"]=100, ["gdMn"]=5, ["gdMx"]=15, ["fmt"]="%2.0f", ["info"]="GPU headroom in %"}, "")

jjjLib1.addParam(jjjFPS_plId, "wizDisp", {["save"]="global-profile", ["dflt"]='sync'}, "jjjFPS_calcWizardSettings()")
jjjLib1.addParam(jjjFPS_plId, "wizFps", {["save"]="global-profile", ["dflt"]=30, ["mn"]=20, ["mx"]=120, ["stp"]=1, ["slMn"]=10, ["slMx"]=120, ["gdMn"]=30, ["gdMx"]=60, ["fmt"]="%2.0f"}, "jjjFPS_calcWizardSettings()")


jjjLib1.addParam(jjjFPS_plId, "Qinc", {["save"]="global-profile", ["dflt"]=0.3, ["mn"]=0.1, ["mx"]=4.0, ["stp"]=0.1, ["slMn"]=0.2, ["slMx"]=4.0, ["gdMn"]=0.1, ["gdMx"]=1.0, ["fmt"]="%1.1f"}, "")
jjjLib1.addParam(jjjFPS_plId, "Qdec", {["save"]="global-profile", ["dflt"]=1.5, ["mn"]=1.0, ["mx"]=4.0, ["stp"]=0.1, ["slMn"]=0.2, ["slMx"]=4.0, ["gdMn"]=1.0, ["gdMx"]=3.0, ["fmt"]="%1.1f"}, "")

jjjLib1.addParam(jjjFPS_plId, "LDa", {["save"]="global-profile", ["dflt"]=true}, "jjjFPS_setAutoLDOnOff()")
jjjLib1.addParam(jjjFPS_plId, "LDmn", {["save"]="global-profile", ["dflt"]=4.0, ["mn"]=0.2, ["mx"]=8.0, ["stp"]=0.1, ["slMn"]=8.0, ["slMx"]=0.1, ["gdMn"]=1.0, ["gdMx"]=5.0, ["fmt"]="%1.1f", ["parMn"]="LDmx"}, "")
jjjLib1.addParam(jjjFPS_plId, "LDmx", {["save"]="global-profile", ["dflt"]=1.5, ["mn"]=0.1, ["mx"]=7.9, ["stp"]=0.1, ["slMn"]=8.0, ["slMx"]=0.1, ["gdMn"]=1.0, ["gdMx"]=2.0, ["fmt"]="%1.1f", ["parMx"]="LDmn"}, "")
jjjLib1.addParam(jjjFPS_plId, "LDmnQ", {["save"]="global-profile", ["dflt"]=-10, ["mn"]=-10, ["mx"]=9, ["stp"]=1, ["slMn"]=-10, ["slMx"]=10, ["fmt"]="%3.0f", ["parMx"]="LDmxQ"}, "")
jjjLib1.addParam(jjjFPS_plId, "LDmxQ", {["save"]="global-profile", ["dflt"]=10, ["mn"]=-9, ["mx"]=10, ["stp"]=1, ["slMn"]=-10, ["slMx"]=10, ["fmt"]="%3.0f", ["parMn"]="LDmnQ"}, "")

jjjLib1.addParam(jjjFPS_plId, "CLDa", {["save"]="global-profile", ["dflt"]=true}, "jjjFPS_setAutoCLDOnOff()")
jjjLib1.addParam(jjjFPS_plId, "CLDmn", {["save"]="global-profile", ["dflt"]=0.5, ["mn"]=0.1, ["mx"]=1.0, ["stp"]=0.05, ["slMn"]=0.0, ["slMx"]=1.5, ["gdMn"]=0.4, ["gdMx"]=0.6, ["fmt"]="%1.2f", ["parMx"]="CLDmx"}, "")
jjjLib1.addParam(jjjFPS_plId, "CLDmx", {["save"]="global-profile", ["dflt"]=1.0, ["mn"]=0.5, ["mx"]=1.5, ["stp"]=0.05, ["slMn"]=0.0, ["slMx"]=1.5, ["gdMn"]=0.8, ["gdMx"]=1.0, ["fmt"]="%1.2f", ["parMn"]="CLDmn"}, "")
jjjLib1.addParam(jjjFPS_plId, "CLDmnQ", {["save"]="global-profile", ["dflt"]=-10, ["mn"]=-10, ["mx"]=9, ["stp"]=1, ["slMn"]=-10, ["slMx"]=10, ["fmt"]="%3.0f", ["parMx"]="CLDmxQ"}, "")
jjjLib1.addParam(jjjFPS_plId, "CLDmxQ", {["save"]="global-profile", ["dflt"]=10, ["mn"]=-9, ["mx"]=10, ["stp"]=1, ["slMn"]=-10, ["slMx"]=10, ["fmt"]="%3.0f", ["parMn"]="CLDmnQ"}, "")

jjjLib1.addParam(jjjFPS_plId, "ShDa", {["save"]="global-profile", ["dflt"]=true}, "jjjFPS_setAutoShDOnOff()")
jjjLib1.addParam(jjjFPS_plId, "ShDmn", {["save"]="global-profile", ["dflt"]=500, ["mn"]=300, ["mx"]=3000, ["stp"]=100, ["slMn"]=0, ["slMx"]=5000, ["gdMn"]=500, ["gdMx"]=1000, ["fmt"]="%4.0f", ["parMx"]="ShDmx"}, "")
jjjLib1.addParam(jjjFPS_plId, "ShDmx", {["save"]="global-profile", ["dflt"]=1500, ["mn"]=500, ["mx"]=5000, ["stp"]=100, ["slMn"]=0, ["slMx"]=5000, ["gdMn"]=1000, ["gdMx"]=3000, ["fmt"]="%4.0f", ["parMn"]="ShDmn"}, "")
jjjLib1.addParam(jjjFPS_plId, "ShDmnQ", {["save"]="global-profile", ["dflt"]=0, ["mn"]=-10, ["mx"]=9, ["stp"]=1, ["slMn"]=-10, ["slMx"]=10, ["fmt"]="%3.0f", ["parMx"]="ShDmxQ"}, "")
jjjLib1.addParam(jjjFPS_plId, "ShDmxQ", {["save"]="global-profile", ["dflt"]=10, ["mn"]=-9, ["mx"]=10, ["stp"]=1, ["slMn"]=-10, ["slMx"]=10, ["fmt"]="%3.0f", ["parMn"]="ShDmnQ"}, "")
jjjLib1.addParam(jjjFPS_plId, "ShDbil", {["save"]="global-profile", ["dflt"]=true}, "jjjFPS_setAutoShDOnOff()")

jjjLib1.addParam(jjjFPS_plId, "ShKa",     {["save"]="global-profile", ["dflt"]=true}, "jjjFPS_setAutoShKOnOff()")
jjjLib1.addParam(jjjFPS_plId, "ShKintDg", {["save"]="global-profile", ["dflt"]=-1.0, ["mn"]=-5.0, ["mx"]=10.0, ["stp"]=0.2, ["slMn"]=-5.0, ["slMx"]=10.0, ["gdMn"]=-2.0, ["gdMx"]=2.0, ["fmt"]="%2.1f"}, "")
jjjLib1.addParam(jjjFPS_plId, "ShKextDg", {["save"]="global-profile", ["dflt"]=1.6,  ["mn"]=-5.0, ["mx"]=10.0, ["stp"]=0.2, ["slMn"]=-5.0, ["slMx"]=10.0, ["gdMn"]=1.0, ["gdMx"]=2.0, ["fmt"]="%2.1f"}, "")

jjjLib1.addParam(jjjFPS_plId, "AGLa",  {["save"]="global-profile", ["dflt"]=true}, "")
jjjLib1.addParam(jjjFPS_plId, "AGLmn", {["save"]="global-profile", ["dflt"]=40, ["mn"]=5, ["mx"]=100, ["stp"]=1, ["slMn"]=0, ["slMx"]=100, ["gdMn"]=20, ["gdMx"]=50, ["fmt"]="%3.0f", ["parMx"]="AGLmx"}, "")
jjjLib1.addParam(jjjFPS_plId, "AGLmx", {["save"]="global-profile", ["dflt"]=100, ["mn"]=20, ["mx"]=100, ["stp"]=1, ["slMn"]=0, ["slMx"]=100, ["gdMn"]=80, ["gdMx"]=100, ["fmt"]="%3.0f", ["parMn"]="AGLmn"}, "")
jjjLib1.addParam(jjjFPS_plId, "AGLh",  {["save"]="global-profile", ["dflt"]=150, ["mn"]=20, ["mx"]=1000, ["stp"]=10, ["slMn"]=0, ["slMx"]=1000, ["fmt"]="%4.0f"}, "")

jjjLib1.addParam(jjjFPS_plId, "FSRa", {["save"]="global-profile", ["dflt"]=true}, "jjjFPS_setAutoFSROnOff()")
jjjLib1.addParam(jjjFPS_plId, "FSRmn", {["save"]="global-profile", ["dflt"]=1, ["mn"]=0, ["mx"]=4, ["stp"]=1, ["slMn"]=0, ["slMx"]=4, ["gdMn"]=1, ["gdMx"]=3, ["fmt"]="%1i"}, "jjjFPS_setAutoFSRmn()")
jjjLib1.addParam(jjjFPS_plId, "FSRmx", {["save"]="global-profile", ["dflt"]=4, ["mn"]=0, ["mx"]=4, ["stp"]=1, ["slMn"]=0, ["slMx"]=4, ["gdMn"]=2, ["gdMx"]=4, ["fmt"]="%1i"}, "jjjFPS_setAutoFSRmx()")
jjjLib1.addParam(jjjFPS_plId, "FSRmnQ", {["save"]="global-profile", ["dflt"]=-5, ["mn"]=-10, ["mx"]=5, ["stp"]=1, ["slMn"]=-10, ["slMx"]=10, ["fmt"]="%2.0f", ["parMx"]="FSRmxQ"}, "")
jjjLib1.addParam(jjjFPS_plId, "FSRmxQ", {["save"]="global-profile", ["dflt"]=5,  ["mn"]=-5, ["mx"]=10, ["stp"]=1, ["slMn"]=-10, ["slMx"]=10, ["fmt"]="%2.0f", ["parMn"]="FSRmnQ"}, "")
jjjLib1.addParam(jjjFPS_plId, "FSRtmInc", {["save"]="global-profile", ["dflt"]=30,  ["mn"]=5, ["mx"]=120, ["stp"]=1, ["slMn"]=0, ["slMx"]=120, ["fmt"]="%3.0f"}, "")

jjjLib1.addParam(jjjFPS_plId, "Stutt", {["save"]="global-profile", ["dflt"]='off'}, "jjjFPS_setStutterMode()")


jjjLib1.addParam(jjjFPS_plId, "MTfr", {["save"]="global-profile", ["dflt"]=15, ["mn"]=5, ["mx"]=30, ["stp"]=1, ["slMn"]=5, ["slMx"]=80, ["gdMn"]=10, ["gdMx"]=30, ["fmt"]="%2.0f", ["parMx"]="MTto", ["parMxDiff"]=20}, "")
jjjLib1.addParam(jjjFPS_plId, "MTto", {["save"]="global-profile", ["dflt"]=40, ["mn"]=25, ["mx"]=80, ["stp"]=1, ["slMn"]=5, ["slMx"]=80, ["gdMn"]=40, ["gdMx"]=70, ["fmt"]="%2.0f", ["parMn"]="MTfr", ["parMnDiff"]=20}, "")
jjjLib1.addParam(jjjFPS_plId, "MTbd", {["save"]="global-profile", ["dflt"]=20, ["mn"]=10, ["mx"]=40, ["stp"]=1, ["slMn"]=5, ["slMx"]=80, ["gdMn"]=18, ["gdMx"]=40, ["fmt"]="%2.0f", ["parMx"]="MTgd", ["parMxDiff"]=5}, "")
jjjLib1.addParam(jjjFPS_plId, "MTgd", {["save"]="global-profile", ["dflt"]=30, ["mn"]=20, ["mx"]=60, ["stp"]=1, ["slMn"]=5, ["slMx"]=80, ["gdMn"]=25, ["gdMx"]=60, ["fmt"]="%2.0f", ["parMn"]="MTbd", ["parMnDiff"]=5}, "")
jjjLib1.addParam(jjjFPS_plId, "MTwd", {["save"]="global-profile", ["dflt"]=84 + jjjFPS_UHD*84, ["mn"]=24, ["mx"]=256, ["stp"]=4, ["slMn"]=24, ["slMx"]=256, ["fmt"]="%3i"}, "")
jjjLib1.addParam(jjjFPS_plId, "MTht", {["save"]="global-profile", ["dflt"]=11 + jjjFPS_UHD*7, ["mn"]=8, ["mx"]=24, ["stp"]=1, ["slMn"]=8, ["slMx"]=24, ["fmt"]="%2i"}, "")

jjjLib1.addParam(jjjFPS_plId, "disA", {["save"]="global-profile", ["dflt"]=0.8, ["mn"]=0.1, ["mx"]=1, ["stp"]=0.1, ["slMn"]=0, ["slMx"]=1, ["fmt"]="%1.1f", ["info"]="display alpha"}, "")
jjjLib1.addParam(jjjFPS_plId, "disBgA", {["save"]="global-profile", ["dflt"]=0.1, ["mn"]=0.0, ["mx"]=1, ["stp"]=0.1, ["slMn"]=0, ["slMx"]=1, ["fmt"]="%1.1f", ["info"]="display background alpha"}, "")

jjjLib1.addParam(jjjFPS_plId, "disX", {["save"]="global-profile", ["dflt"]=8*(jjjFPS_UHD + 1), ["mn"]=1, ["mx"]=300, ["stp"]=1, ["slMn"]=1, ["slMx"]=300, ["fmt"]="%3i"}, "")
jjjLib1.addParam(jjjFPS_plId, "disY", {["save"]="global-profile", ["dflt"]=-24*(jjjFPS_UHD + 1), ["mn"]=1, ["mx"]=300, ["stp"]=1, ["slMn"]=1, ["slMx"]=300, ["fmt"]="%3i"}, "")

function jjjFPS_param(param)
	return jjjLib1.getParam(jjjFPS_plId, param)
end
function jjjFPS_setParam(param, value)
	jjjLib1.setParam(jjjFPS_plId, param, value)
end
function jjjFPS_loadParams()
	local ok = jjjLib1.loadParams(jjjFPS_plId)
end
function jjjFPS_saveParams()
	jjjLib1.saveParams(jjjFPS_plId)
end
function jjjFPS_setParamsDefault()
	jjjLib1.setParamsDefault(jjjFPS_plId)
end

--------------------------------
-- QUICK-START SETTINGS:
--------------------------------
local jjjFPS_autoMode       = "on"  -- mode on plugin start: "off", "on" (automatic mode), "max" (max. FPS), "min" (max. quality)

--------------------------------
-- DETAILED SETTINGS:
--------------------------------
local jjjFPS_useProfiles  = true
local jjjFPS_useVRmode    = true

local jjjFPS_useProcTimes = true    -- use (estimated) CPU and GPU times for SMART mode

local jjjFPS_useAutoLod = true      -- autom. set LOD
local jjjFPS_useAutoAGL = true      -- reduce LOD when view is close to ground
local jjjFPS_useAutoCLD = false     -- autom. change clouds
local jjjFPS_useAutoShD = true      -- autom. shadow distance
local jjjFPS_useAutoShK = true      -- autom. kill shadows, when sun is below a certain angle
local jjjFPS_useAutoFSR = false     -- autom. change FSR

local jjjFPS_dispAlpha = 0.8        -- opacity of display (0.0: invisble, 1.0: fully opaque)
local jjjFPS_dispX     = 12         -- horizontal position in pixels
--                                      (if positive: pixels from left side of screen, if negative: from right side of screen)
local jjjFPS_dispY     = -16        -- vertical position in pixels
--                                      (if positive: pixels from bottom of screen, if negative: from top of screen)
local jjjFPS_meterFpsSpd    = 4     -- graphic FPS display: Speed to move variation display up/down (min/max values) in FPS/sec
local jjjFPS_meterShowOrig  = true  -- graphic+numeric FPS display: show original values in graph below FPS meter


--------------------------------
-- check needed datarefs:
--------------------------------


-- lights/do_spill_fog = 1


if jjjFPS_useProcTimes then
	jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/private/stats/ogl/swap_time_total", true)
end
if jjjFPS_useAutoLod then
	jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/private/controls/reno/LOD_bias_rat", true)
end
if jjjFPS_useAutoCLD then
	jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/private/controls/new_clouds/march/seg_steps", true)
	jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/private/controls/new_clouds/march/step_len_start", true)
end
if jjjFPS_useAutoFSR then
	jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/private/controls/fsr/enable", true)
	jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/private/controls/fsr/quality", true)
end
if jjjFPS_useAutoShK then
	jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/private/controls/perf/disable_shadow_prep", true)
	jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/graphics/scenery/sun_pitch_degrees", true)
	jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/graphics/view/view_is_external", true)
end
if jjjFPS_useAutoShD then
	jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/private/controls/vegetation/billboard_shadows", true)
	jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/private/controls/shadow/csm/far_limit_interior", true)
	jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/private/controls/shadow/csm/far_limit_exterior", true)
end
if jjjLib1.getNumMissingDataRefs(jjjFPS_plId) > 0 then
	jjjLib1.warning(jjjFPS_plId, jjjLib1.getNumMissingDataRefs(jjjFPS_plId) .. ' DataRef(s) missing, some functions of the plugin are disabled!||The plugin is not fully compatible with your version of X-Plane. As the plugin has to use some unoffical control parameters (so called "DataRefs"), this is NOT a bug of X-Plane, FlyWithLua or the plugin itself! It is something that -unfortunately- simply can happen, please do not blame anyone for it.')
end


--------------------------------
-- internal variables:
--------------------------------
local jjjFPS_totalSec       = jjjLib1.getClock()
local jjjFPS_frameSec       = 0
local jjjFPS_didDraw        = true
local jjjFPS_QF             = 0
local jjjFPS_destQF         = jjjFPS_QF
local jjjFPS_gpuQF          = 0
local jjjFPS_gpuDestQF      = jjjFPS_gpuQF

local jjjFPS_ignoreFPStimer = 0

local jjjFPS_cpuTime        = 0
local jjjFPS_gpuTime        = 0
local jjjFPS_cpuTimeSmooth  = 0
local jjjFPS_gpuTimeSmooth  = 0
local jjjFPS_procTimesDecr  = 0.0005  -- for cpu/gpu time smoothing: rate (secs per sec) to decrease smoothed time

local jjjFPS_tTargetMax     = 1.0/30
local jjjFPS_tTargetCPUHdrm = jjjFPS_tTargetMax*0.9
local jjjFPS_tTargetGPUHdrm = jjjFPS_tTargetMax*0.9


local jjjFPS_dispMark    = ""

local jjjFPS_x           = 0
local jjjFPS_y           = 0

local jjjFPS_width       = jjjFPS_param("MTwd")
local jjjFPS_meterWidth  = jjjFPS_param("MTwd")
local jjjFPS_lineHeight  = jjjFPS_param("MTht")
local jjjFPS_borderWidth = math.floor(jjjFPS_lineHeight*0.15)
local jjjFPS_height      = jjjFPS_lineHeight*4
local jjjFPS_fontSize    = 1
local jjjFPS_fontSize2   = 1
local jjjFPS_curX        = 0
local jjjFPS_curY        = 0

local jjjFPS_fps           = 20
local jjjFPS_fpsSmooth     = 20
local jjjFPS_meterFps      = 20
local jjjFPS_meterFpsMin   = 20
local jjjFPS_meterFpsMax   = 20

local jjjFPS_lastShowTime  = jjjFPS_totalSec + 7
local jjjFPS_fadeAlpha     = 0.0

local jjjFPS_moveMode      = 0
local jjjFPS_moveModeTimer = 0

local jjjFPS_meterY1 = 0
local jjjFPS_meterY2 = 0
local jjjFPS_meterY3 = 0
local jjjFPS_meterY4 = 0
local jjjFPS_meterXB = 0
local jjjFPS_meterXG = 0
local jjjFPS_meterXR = 0
local jjjFPS_meterXMin = 0
local jjjFPS_meterXMax = 0
local jjjFPS_meterXFps = 0
local jjjFPS_meterScaleX = 1
local jjjFPS_meterX1 = 0
local jjjFPS_meterX2 = 0
local jjjFPS_meterXorigLD = false
local jjjFPS_meterXorigCD = false
local jjjFPS_meterXorigCS = false

local jjjFPS_stuttersDetected = false
local jjjFPS_chkStutters    = false
local jjjFPS_chkStuttThrs   = 0.75
local jjjFPS_chkStuttTime   = 60
local jjjFPS_prevFrameStutt = false
local jjjFPS_stuttTimes     = {[0]=0, [1]=0, [2]=0, [3]=0, [4]=0}
local jjjFPS_currStuttIndex = 0
local jjjFPS_lastStuttIndex = 0

local jjjFPS_screenShot     = 0  -- phase of screen shot
local jjjFPS_screenShotOldQ = 0

-- DataRefs etc.
local jjjFPS_DR_gpuTime_ptr  = nil
local jjjFPS_DR_gpuTime      = 0
local jjjFPS_DR_swapTime_ptr = nil
local jjjFPS_DR_swapTime     = 0
local jjjFPS_DR_swapTimeOld  = 0
local jjjFPS_swapTime_last   = 0
if jjjFPS_useProcTimes then
	jjjFPS_useProcTimes = jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/time/gpu_time_per_frame_sec_approx") and jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/private/stats/ogl/swap_time_total")
end
if jjjFPS_useProcTimes then
	jjjFPS_DR_gpuTime_ptr = jjjLib1.findDataRef("sim/time/gpu_time_per_frame_sec_approx")
	jjjFPS_DR_gpuTime = jjjLib1.getDataRef_f(jjjFPS_DR_gpuTime_ptr)

	jjjFPS_DR_swapTime_ptr = jjjLib1.findDataRef("sim/private/stats/ogl/swap_time_total")
	jjjFPS_DR_swapTime = jjjLib1.getDataRef_f(jjjFPS_DR_swapTime_ptr)
	jjjFPS_DR_swapTimeOld = jjjFPS_DR_swapTime
end


local jjjFPS_DR_VR_ptr = nil
local jjjFPS_DR_VR     = 0
if jjjFPS_useVRmode then
	jjjFPS_useVRmode = jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/graphics/VR/enabled")
end
if jjjFPS_useVRmode then
	jjjFPS_DR_VR_ptr = jjjLib1.findDataRef("sim/graphics/VR/enabled")
end

local jjjFPS_DR_lodBias_ptr = nil
local jjjFPS_DR_lodBias = 1.0
if jjjFPS_useAutoLod then
	jjjFPS_useAutoLod = jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/private/controls/reno/LOD_bias_rat")
end
if jjjFPS_useAutoLod then
	jjjFPS_DR_lodBias_ptr = jjjLib1.findDataRef("sim/private/controls/reno/LOD_bias_rat")
	jjjFPS_DR_lodBias = jjjLib1.getDataRef_f(jjjFPS_DR_lodBias_ptr)
end


local jjjFPS_DR_cloudsSegSteps_ptr = nil
local jjjFPS_DR_cloudsSegSteps = 100.0
local jjjFPS_DR_cloudsStepStart_ptr = nil
local jjjFPS_DR_cloudsStepStart = 15.0
local jjjFPS_curCloudQ = 1.0
if jjjFPS_useAutoCLD then
	jjjFPS_useAutoCLD = jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/private/controls/new_clouds/march/seg_steps") and jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/private/controls/new_clouds/march/step_len_start")
end
if jjjFPS_useAutoCLD then
	jjjFPS_DR_cloudsSegSteps_ptr = jjjLib1.findDataRef("sim/private/controls/new_clouds/march/seg_steps")
	jjjFPS_DR_cloudsSegSteps = jjjLib1.getDataRef_f(jjjFPS_DR_cloudsSegSteps_ptr)
	jjjFPS_DR_cloudsStepStart_ptr = jjjLib1.findDataRef("sim/private/controls/new_clouds/march/step_len_start")
	jjjFPS_DR_cloudsStepStart = jjjLib1.getDataRef_f(jjjFPS_DR_cloudsStepStart_ptr)
end


local jjjFPS_DR_shdLimitInt_ptr = nil
local jjjFPS_DR_shdLimitInt = 500.0
local jjjFPS_DR_shdLimitExt_ptr = nil
local jjjFPS_DR_shdLimitExt = 500.0
local jjjFPS_DR_shdBillboards_ptr = nil
local jjjFPS_DR_shdBillboards = 0
local jjjFPS_curShdLimit = 500.0
if jjjFPS_useAutoShD then
	jjjFPS_useAutoShD = jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/private/controls/vegetation/billboard_shadows") and jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/private/controls/shadow/csm/far_limit_interior") and jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/private/controls/shadow/csm/far_limit_exterior")
end
if jjjFPS_useAutoShD then
	jjjFPS_DR_shdLimitInt_ptr = jjjLib1.findDataRef("sim/private/controls/shadow/csm/far_limit_interior")
	jjjFPS_DR_shdLimitInt = jjjLib1.getDataRef_f(jjjFPS_DR_shdLimitInt_ptr)
	jjjFPS_DR_shdLimitExt_ptr = jjjLib1.findDataRef("sim/private/controls/shadow/csm/far_limit_exterior")
	jjjFPS_DR_shdLimitExt = jjjLib1.getDataRef_f(jjjFPS_DR_shdLimitExt_ptr)
	jjjFPS_DR_shdBillboards_ptr = jjjLib1.findDataRef("sim/private/controls/vegetation/billboard_shadows")
	jjjFPS_DR_shdBillboards = jjjLib1.getDataRef_f(jjjFPS_DR_shdBillboards_ptr)
end

local jjjFPS_DR_disShdPrep_ptr = nil
local jjjFPS_DR_disShdPrep = 0
local jjjFPS_DR_sunPitch_ptr = nil
local jjjFPS_DR_viewExt_ptr  = nil
if jjjFPS_useAutoShK then
	jjjFPS_useAutoShK = jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/private/controls/perf/disable_shadow_prep") and jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/graphics/scenery/sun_pitch_degrees") and jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/graphics/view/view_is_external")
end
if jjjFPS_useAutoShK then
	jjjFPS_DR_disShdPrep_ptr = jjjLib1.findDataRef("sim/private/controls/perf/disable_shadow_prep")
	jjjFPS_DR_disShdPrep = jjjLib1.getDataRef_f(jjjFPS_DR_disShdPrep_ptr)
	jjjFPS_DR_sunPitch_ptr = jjjLib1.findDataRef("sim/graphics/scenery/sun_pitch_degrees")
	jjjFPS_DR_viewExt_ptr = jjjLib1.findDataRef("sim/graphics/view/view_is_external")
end


local jjjFPS_DR_FSRon_ptr = nil
local jjjFPS_DR_FSRon = 1
local jjjFPS_DR_FSRq_ptr = nil
local jjjFPS_DR_FSRq = 2
local jjjFPS_curFSRmode = 0
local jjjFPS_lastChangeFSRtime = 0
local jjjFPS_cantIncFSRtime     = 0

if jjjFPS_useAutoFSR then
	jjjFPS_useAutoFSR = jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/private/controls/fsr/enable") and jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/private/controls/fsr/quality")
end
if jjjFPS_useAutoFSR then
	jjjFPS_DR_FSRon_ptr = jjjLib1.findDataRef("sim/private/controls/fsr/enable")
	jjjFPS_DR_FSRon = jjjLib1.getDataRef_f(jjjFPS_DR_FSRon_ptr)
	jjjFPS_DR_FSRq_ptr = jjjLib1.findDataRef("sim/private/controls/fsr/quality")
	jjjFPS_DR_FSRq = jjjLib1.getDataRef_f(jjjFPS_DR_FSRq_ptr)

	-- set default to current settings
	if jjjFPS_DR_FSRon == 1 then
		jjjFPS_curFSRmode = jjjFPS_DR_FSRq
	else
		jjjFPS_curFSRmode = 4
	end
end


local jjjFPS_drAcfY_ptr      = nil
local jjjFPS_drAcfAGL_ptr    = nil
local jjjFPS_drViewY_ptr     = nil
local jjjFPS_useAutoAGLlod   = jjjFPS_useAutoLod
if jjjFPS_useAutoAGL then
	jjjFPS_useAutoAGL = jjjFPS_useAutoAGLlod and jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/flightmodel/position/local_y", true) and jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/flightmodel/position/y_agl", true) and jjjLib1.dataRefAvailable(jjjFPS_plId, "sim/graphics/view/view_y", true)
end
if jjjFPS_useAutoAGL then
	jjjFPS_drAcfY_ptr   = jjjLib1.findDataRef("sim/flightmodel/position/local_y")
	jjjFPS_drAcfAGL_ptr = jjjLib1.findDataRef("sim/flightmodel/position/y_agl")
	jjjFPS_drViewY_ptr  = jjjLib1.findDataRef("sim/graphics/view/view_y")
else
	jjjFPS_useAutoAGLlod = false
end

local jjjFPS_DR_lodBiasOrig         = jjjFPS_DR_lodBias
local jjjFPS_DR_FSRonOrig           = jjjFPS_DR_FSRon
local jjjFPS_DR_FSRqOrig            = jjjFPS_DR_FSRq
local jjjFPS_DR_cloudsSegStepsOrig  = jjjFPS_DR_cloudsSegSteps
local jjjFPS_DR_cloudsStepStartOrig = jjjFPS_DR_cloudsStepStart
local jjjFPS_DR_shdLimitIntOrig     = jjjFPS_DR_shdLimitInt
local jjjFPS_DR_shdLimitExtOrig     = jjjFPS_DR_shdLimitExt
local jjjFPS_DR_shdBillboardsOrig   = jjjFPS_DR_shdBillboards


-- Panel variables
local jjjFPS_panelMode = ""  -- ""=no panel, "main"=main panel, "wiz"=wizard config, "adv"=advanced config
local jjjFPS_panelPage = 0  -- counter for the current page of current mode of panel (if mode has more than 1 page)
local jjjFPS_panelPageOld = 0
local jjjFPS_panelPageStep = 0
local jjjFPS_panelW = 378
local jjjFPS_panelFpsDispId = false

local jjjFPS_pnId = jjjLib1.createPanel(jjjFPS_plId, "", jjjFPS_panelW, 'jjjFPS_refreshPanel()', "blue")

function jjjFPS_getMeterX(value, valueL, valueR)
	if valueL ~= valueR and valueL and valueR then
		local x = (value - valueL)/(valueR - valueL)
		x = ( x < 0 and 0.0 or ( x > 1 and 1.0 or x ) )
		return x*jjjFPS_meterWidth
	end
	return false
end
function jjjFPS_calcMeter()
	jjjFPS_meterScaleX = jjjFPS_meterWidth/( jjjFPS_param("MTto") - jjjFPS_param("MTfr") )
	jjjFPS_meterXB = math.floor(( jjjFPS_param("MTbd") - jjjFPS_param("MTfr") )*jjjFPS_meterScaleX + 0.5 )
	jjjFPS_meterXR = math.floor(( jjjFPS_param("MTto") - jjjFPS_param("MTfr") )*jjjFPS_meterScaleX + 0.5 )
	jjjFPS_meterXG = math.min(jjjFPS_meterXR + 1, math.floor(( jjjFPS_param("MTgd") - jjjFPS_param("MTfr") )*jjjFPS_meterScaleX + 0.5 ))
	jjjFPS_meterXorigLD = jjjFPS_getMeterX(jjjFPS_DR_lodBiasOrig,  jjjFPS_param("LDmn"), jjjFPS_param("LDmx"))
end

function jjjFPS_roundMeterSetting( fpsVal, minVal, maxVal )
	if fpsVal < 40 then
		fpsVal = math.floor(fpsVal/5)*5
	elseif fpsVal < 60 then
		fpsVal = math.floor(fpsVal/10)*10
	elseif fpsVal < 90 then
		fpsVal = math.floor(fpsVal/20)*20
	else
		fpsVal = math.floor(fpsVal/30)*30
	end
	return math.max(minVal, math.min(maxVal, fpsVal))
end

function jjjFPS_calcWizardSettings()
	if jjjFPS_param("wizDisp") == 'nosync' or jjjFPS_param("wizDisp") == 'free' then
		-- monitor without vsync or freesync:
		if jjjFPS_useProcTimes then
			jjjFPS_setParam("smartM", true)
			jjjFPS_setParam("Ftg", jjjFPS_param("wizFps"))
			jjjFPS_setParam("CPUhdrm", 20)
			jjjFPS_setParam("GPUhdrm", 10)
		else
			jjjFPS_setParam("smartM", false)
			jjjFPS_setParam("Fmn", jjjFPS_param("wizFps") - 2)
			jjjFPS_setParam("Fmx", jjjFPS_param("wizFps") + 2)
		end
	elseif jjjFPS_param("wizDisp") == 'vr' then
		-- VR:
		if jjjFPS_useProcTimes then
			jjjFPS_setParam("smartM", true)
			jjjFPS_setParam("Ftg", jjjFPS_param("wizFps"))
			jjjFPS_setParam("CPUhdrm", 20)
			jjjFPS_setParam("GPUhdrm", 10)
		else
			jjjFPS_setParam("smartM", false)
			jjjFPS_setParam("Fmn", jjjFPS_param("wizFps"))
			jjjFPS_setParam("Fmx", jjjFPS_param("wizFps") + 1)
		end
	else
		-- monitor with vsync:
		if jjjFPS_useProcTimes then
			jjjFPS_setParam("smartM", true)
			jjjFPS_setParam("Ftg", jjjFPS_param("wizFps"))
			jjjFPS_setParam("CPUhdrm", 20)
			jjjFPS_setParam("GPUhdrm", 10)
		else
			jjjFPS_setParam("smartM", false)
			jjjFPS_setParam("Fmn", jjjFPS_param("wizFps"))
			jjjFPS_setParam("Fmx", jjjFPS_param("wizFps") + 1)
		end
	end

	jjjFPS_setParam("MTbd", jjjFPS_roundMeterSetting( jjjFPS_param("wizFps")*0.75, 20, 30 ) )
	jjjFPS_setParam("MTgd", jjjFPS_param("wizFps"), 20, 90 )
	jjjFPS_setParam("MTfr", jjjFPS_roundMeterSetting( jjjFPS_param("wizFps")/2, 10, jjjFPS_param("MTbd") - 5 ) )
	if jjjFPS_param("wizDisp") == 'nosync' or jjjFPS_param("wizDisp") == 'free' then
		jjjFPS_setParam("MTto", jjjFPS_roundMeterSetting( jjjFPS_param("wizFps")*1.6, 30, 120 ) )
	else
		jjjFPS_setParam("MTto", jjjFPS_roundMeterSetting( jjjFPS_param("wizFps")*1.5, 30, 120 ) )
	end
end

function jjjFPS_setShowNumbers()
	if not jjjFPS_param("disN") then
		jjjFPS_setParam("disG", true)
	end
end
function jjjFPS_setShowGraphic()
	if not jjjFPS_param("disG") then
		jjjFPS_setParam("disN", true)
	end
end

function jjjFPS_setMoveMode(mode)
	jjjFPS_moveMode = mode
	if mode > 0 then
		jjjFPS_moveModeTimer = jjjFPS_totalSec
	else
		jjjFPS_moveModeTimer = 0
	end
end
	
function jjjFPS_setSmartMode()
	jjjFPS_setStutterMode()
end

function jjjFPS_setAutoLDOnOff()
	if jjjFPS_useAutoLod then
		if not jjjFPS_param("LDa") then
			jjjFPS_DR_lodBias = jjjFPS_DR_lodBiasOrig
			jjjLib1.setDataRef_f(jjjFPS_DR_lodBias_ptr, jjjFPS_DR_lodBias)
		end
	end
end
function jjjFPS_setAutoCLDOnOff()
	if jjjFPS_useAutoCLD then
		if not jjjFPS_param("CLDa") then
			jjjFPS_DR_cloudsSegSteps  = jjjFPS_DR_cloudsSegStepsOrig
			jjjFPS_DR_cloudsStepStart = jjjFPS_DR_cloudsStepStartOrig
			jjjLib1.setDataRef_f(jjjFPS_DR_cloudsSegSteps_ptr, jjjFPS_DR_cloudsSegSteps)
			jjjLib1.setDataRef_f(jjjFPS_DR_cloudsStepStart_ptr, jjjFPS_DR_cloudsStepStart)
		end
	end
end
function jjjFPS_setAutoShDOnOff()
	if jjjFPS_useAutoShD then
		if not jjjFPS_param("ShDa") then
			jjjFPS_DR_shdLimitInt   = jjjFPS_DR_shdLimitIntOrig
			jjjFPS_DR_shdLimitExt   = jjjFPS_DR_shdLimitExtOrig
			jjjFPS_DR_shdBillboards = jjjFPS_DR_shdBillboardsOrig
			jjjLib1.setDataRef_f(jjjFPS_DR_shdLimitInt_ptr, jjjFPS_DR_shdLimitInt)
			jjjLib1.setDataRef_f(jjjFPS_DR_shdLimitExt_ptr, jjjFPS_DR_shdLimitExt)
			jjjLib1.setDataRef_f(jjjFPS_DR_shdBillboards_ptr, jjjFPS_DR_shdBillboards)
		else
			jjjFPS_DR_shdBillboards = 1
			jjjLib1.setDataRef_f(jjjFPS_DR_shdBillboards_ptr, jjjFPS_DR_shdBillboards)
		end
	end
end
function jjjFPS_setAutoShKOnOff()
	if jjjFPS_useAutoShK then
		if not jjjFPS_param("ShKa") then
			jjjFPS_DR_disShdPrep = 0
			jjjLib1.setDataRef_f(jjjFPS_DR_disShdPrep_ptr, jjjFPS_DR_disShdPrep)
		end
	end
end
function jjjFPS_setAutoFSROnOff()
	if jjjFPS_useAutoFSR then
		if not jjjFPS_param("FSRa") then
			jjjFPS_DR_FSRon    = jjjFPS_DR_FSRonOrig
			jjjFPS_DR_FSRq     = jjjFPS_DR_FSRqOrig
			jjjLib1.setDataRef_f(jjjFPS_DR_FSRon_ptr, jjjFPS_DR_FSRon)
			jjjLib1.setDataRef_f(jjjFPS_DR_FSRq_ptr, jjjFPS_DR_FSRq)
		end
		jjjFPS_lastChangeFSRtime = 0
	end
end
function jjjFPS_setAutoFSRmn()
	if jjjFPS_param("FSRmx") < jjjFPS_param("FSRmn") then
		jjjLib1.setParam(jjjFPS_plId, "FSRmx", jjjFPS_param("FSRmn"))
	end
	jjjFPS_lastChangeFSRtime = 0
end
function jjjFPS_setAutoFSRmx()
	if jjjFPS_param("FSRmn") > jjjFPS_param("FSRmx") then
		jjjLib1.setParam(jjjFPS_plId, "FSRmn", jjjFPS_param("FSRmx"))
	end
	jjjFPS_lastChangeFSRtime = 0
end


function jjjFPS_setOrigDatarefs()
	if jjjFPS_useAutoLod then
		jjjFPS_DR_lodBias = jjjFPS_DR_lodBiasOrig
		jjjLib1.setDataRef_f(jjjFPS_DR_lodBias_ptr, jjjFPS_DR_lodBias)
	end
	if jjjFPS_useAutoCLD then
		jjjFPS_DR_cloudsSegSteps  = jjjFPS_DR_cloudsSegStepsOrig
		jjjFPS_DR_cloudsStepStart = jjjFPS_DR_cloudsStepStartOrig
		jjjLib1.setDataRef_f(jjjFPS_DR_cloudsSegSteps_ptr, jjjFPS_DR_cloudsSegSteps)
		jjjLib1.setDataRef_f(jjjFPS_DR_cloudsStepStart_ptr, jjjFPS_DR_cloudsStepStart)
	end
	if jjjFPS_useAutoShD then
		jjjFPS_DR_shdLimitInt   = jjjFPS_DR_shdLimitIntOrig
		jjjFPS_DR_shdLimitExt   = jjjFPS_DR_shdLimitExtOrig
		jjjFPS_DR_shdBillboards = jjjFPS_DR_shdBillboardsOrig
		jjjLib1.setDataRef_f(jjjFPS_DR_shdLimitInt_ptr, jjjFPS_DR_shdLimitInt)
		jjjLib1.setDataRef_f(jjjFPS_DR_shdLimitExt_ptr, jjjFPS_DR_shdLimitExt)
		jjjLib1.setDataRef_f(jjjFPS_DR_shdBillboards_ptr, jjjFPS_DR_shdBillboards)
	end
	if jjjFPS_useAutoShK then
		jjjFPS_DR_disShdPrep = 0
		jjjLib1.setDataRef_f(jjjFPS_DR_disShdPrep_ptr, jjjFPS_DR_disShdPrep)
	end
	if jjjFPS_useAutoFSR then
		jjjFPS_DR_FSRon    = jjjFPS_DR_FSRonOrig
		jjjFPS_DR_FSRq     = jjjFPS_DR_FSRqOrig
		jjjLib1.setDataRef_f(jjjFPS_DR_FSRon_ptr, jjjFPS_DR_FSRon)
		jjjLib1.setDataRef_f(jjjFPS_DR_FSRq_ptr, jjjFPS_DR_FSRq)
	end
end


function jjjFPS_setAutoModeManual(mode)
	if jjjFPS_saveMode then
		jjjFPS_setParam("mode", mode)
		jjjLib1.saveParams(jjjFPS_plId, "global")
	end
	jjjFPS_setAutoMode(mode)
end

function jjjFPS_setAutoMode(mode)
	if jjjFPS_autoMode ~= mode then
		jjjFPS_autoMode = mode
		if mode == "on" then
			-- jjjFPS_destQF = 0
			-- jjjFPS_QF = 0
			-- jjjFPS_gpuDestQF = 0
			-- jjjFPS_gpuQF = 0
			jjjFPS_setAutoShDOnOff()
		end
		if mode == "off" then
			jjjFPS_setOrigDatarefs()
			jjjFPS_destQF = 0
			jjjFPS_QF = 0
			jjjFPS_gpuDestQF = 0
			jjjFPS_gpuQF = 0
		end
		if mode == "max" then
			jjjFPS_destQF = -10
			jjjFPS_QF = -10
			jjjFPS_gpuDestQF = -10
			jjjFPS_gpuQF = -10
			jjjFPS_setAutoShDOnOff()
		end
		if mode == "min" then
			jjjFPS_destQF = 10
			jjjFPS_QF = 10
			jjjFPS_gpuDestQF = 10
			jjjFPS_gpuQF = 10
			jjjFPS_setAutoShDOnOff()
		end
		jjjFPS_lastChangeFSRtime = 0
	end
	jjjFPS_setMoveMode(0)
end

function jjjFPS_toggleAutoMode()
	if jjjFPS_autoMode == "off" then
		jjjFPS_setAutoModeManual("on")
	else
		jjjFPS_setAutoModeManual("off")
	end
	jjjFPS_setMoveMode(0)
end

function jjjFPS_setPosition()
	jjjFPS_dispX = jjjFPS_param("disX")
	jjjFPS_dispY = jjjFPS_param("disY")
end

function jjjFPS_setMeterHeight()
	jjjFPS_meterWidth = jjjFPS_lineHeight*9
	jjjFPS_borderWidth = math.floor(jjjFPS_lineHeight*0.15)
	if jjjFPS_lineHeight < 17 then
		jjjFPS_fontSize  = 1.0
		jjjFPS_fontSize2 = 1.0
	else
		jjjFPS_fontSize  = 1.8
		jjjFPS_fontSize2 = 1.4
	end
end

function jjjFPS_setFSRmode(mode, force)
	if jjjFPS_useAutoFSR and jjjFPS_curFSRmode ~= mode and mode >= 0 and mode <= 4 then
		jjjFPS_curFSRmode = mode
		if not force then
			jjjFPS_lastChangeFSRtime = jjjLib1.getClock()
			jjjFPS_cantIncFSRtime    = jjjFPS_lastChangeFSRtime
		end
		if jjjFPS_curFSRmode < 4 then
			-- modes 0 - 3 => FSR on
			jjjFPS_DR_FSRon = 1
			jjjFPS_DR_FSRq = jjjFPS_curFSRmode
		else
			-- mode 4 => FSR off
			jjjFPS_DR_FSRon = 0
			jjjFPS_DR_FSRq = 3
		end
		jjjLib1.setDataRef_f(jjjFPS_DR_FSRon_ptr, jjjFPS_DR_FSRon)
		jjjLib1.setDataRef_f(jjjFPS_DR_FSRq_ptr, jjjFPS_DR_FSRq)
	end
end

function jjjFPS_setStutterMode()
	jjjFPS_prevFrameStutt = false
	jjjFPS_stuttTimes     = {[0]=0, [1]=0, [2]=0, [3]=0, [4]=0}
	jjjFPS_stuttersDetected = false
	if jjjFPS_param("smartM") or jjjFPS_param("Stutt") == 'off' then
		jjjFPS_chkStutters  = false
	elseif jjjFPS_param("Stutt") == "lo" then
		jjjFPS_chkStutters  = true
		jjjFPS_chkStuttThrs = 0.7
		jjjFPS_chkStuttTime = 5
	elseif jjjFPS_param("Stutt") == "mid" then
		jjjFPS_chkStutters  = true
		jjjFPS_chkStuttThrs = 0.75
		jjjFPS_chkStuttTime = 10
	elseif jjjFPS_param("Stutt") == "hi" then
		jjjFPS_chkStutters  = true
		jjjFPS_chkStuttThrs = 0.8
		jjjFPS_chkStuttTime = 30
	end
end

function jjjFPS_setProfile(profile)
	if jjjFPS_profile ~= profile then
		jjjFPS_profile = profile
		jjjFPS_setParam("profile", profile)
		jjjLib1.saveParams(jjjFPS_plId, "global")
		jjjLib1.setParamProfile(jjjFPS_plId, jjjFPS_profile)
		jjjLib1.loadParams(jjjFPS_plId, "global-profile")
		jjjFPS_setPosition()
		jjjFPS_lineHeight  = jjjFPS_param("MTht")
		jjjFPS_setMeterHeight()
		jjjFPS_calcMeter()
		jjjFPS_setMoveMode(0)
		if jjjFPS_panelMode and jjjFPS_refreshPanel then
			jjjFPS_refreshPanel()
		end
	end
end

function jjjFPS_checkAutoProfile()
	if jjjFPS_useProfiles and jjjFPS_useVRmode and ( jjjFPS_param("profVR") or jjjFPS_param("profNVR") ) then
		local vr = jjjLib1.getDataRef_i(jjjFPS_DR_VR_ptr)
		if vr ~= jjjFPS_DR_VR then
			jjjFPS_DR_VR = vr
			if jjjFPS_panelMode == "wiz" or jjjFPS_panelMode == "adv" then
				return
			end
			if vr == 1 and jjjFPS_param("profVR") then
				-- XPLMSpeakString("VR on. profile " .. jjjFPS_param("profVR"))
				jjjFPS_setProfile(jjjFPS_param("profVR"))
				return
			end
			if vr == 0 and jjjFPS_param("profNVR") then
				-- XPLMSpeakString("VR off. profile " .. jjjFPS_param("profNVR"))
				jjjFPS_setProfile(jjjFPS_param("profNVR"))
				return
			end
		end
	end
end

-- load profile independent parameters first
jjjLib1.loadParams(jjjFPS_plId, "global")
-- initialise profile
if jjjFPS_useProfiles and jjjFPS_param("profNVR") and jjjFPS_param("profNVR") ~= "-" then
	jjjFPS_setProfile(jjjFPS_param("profNVR"))
elseif jjjFPS_param("profile") and jjjFPS_param("profile") ~= "-" then
	jjjFPS_setProfile(jjjFPS_param("profile"))
else
	jjjFPS_setProfile("A")
end
jjjFPS_checkAutoProfile()

if jjjFPS_saveMode then
	jjjFPS_setAutoMode(jjjFPS_param("mode"))
end

jjjFPS_width       = jjjFPS_param("MTwd")
jjjFPS_meterWidth  = jjjFPS_param("MTwd")
jjjFPS_lineHeight  = jjjFPS_param("MTht")

jjjFPS_setPosition()
jjjFPS_setMeterHeight()
jjjFPS_calcMeter()
jjjLib1.setParamsActionOnSet(jjjFPS_plId, "jjjFPS_calcMeter()")

jjjFPS_setStutterMode()

function jjjFPS_exit()
	jjjFPS_setMoveMode(0)
	jjjFPS_setOrigDatarefs()
end

-- check conflict with 3jFPS-control and AUTO_LOD:
local jjjFPS_checkWarn3jFps = false
local jjjFPS_checkWarn3jFpsWizard = false
local jjjFPS_checkWarn3jFpsWizard11 = false
local jjjFPS_checkWarnAutoLod = false
function jjjFPS_check()
	if LOD_Mode_auto ~= nil and not jjjFPS_checkWarnAutoLod then
		-- AUTO_LOD.lua seems to be running too, not good...
		jjjFPS_checkWarnAutoLod = true
		jjjLib1.warning(jjjFPS_plId, 'There is a conflicting plugin running:|FlyWithLua script "Auto_LOD 1.3.lua".||That plugin conflicts with ' .. jjjFPS_pluginName .. ', because it controls LOD (level of detail) too.||You should remove one of these 2 plugins!', true)
	end
	if jjjFPSC_3jFPScontrolRunning and not jjjFPS_checkWarn3jFps then
		jjjFPS_checkWarn3jFps = true
		jjjLib1.warning(jjjFPS_plId, 'There is a conflicting plugin running:|FlyWithLua script "3jFPS-control.lua".||This plugin is a previous version of ' .. jjjFPS_pluginName .. ' and must be removed or disabled when using ' .. jjjFPS_pluginName .. '!', true)
	end
	if jjjFPS_3jFPSwizardRunning and not jjjFPS_checkWarn3jFpsWizard then
		jjjFPS_checkWarn3jFpsWizard = true
		jjjLib1.warning(jjjFPS_plId, 'There is a conflicting plugin running:|FlyWithLua script "3jFPS-wizard.lua".||This plugin is a previous version of ' .. jjjFPS_pluginName .. ' and must be removed or disabled when using ' .. jjjFPS_pluginName .. '!', true)
	end
	if jjjFPS_3jFPSwizard11Running and not jjjFPS_checkWarn3jFpsWizard11 then
		jjjFPS_checkWarn3jFpsWizard11 = true
		jjjLib1.warning(jjjFPS_plId, 'There is a conflicting plugin running:|FlyWithLua script "3jFPS-wizard11.lua".||This plugin is a previous version of ' .. jjjFPS_pluginName .. ' and must be removed or disabled when using ' .. jjjFPS_pluginName .. '!', true)
	end
end


function jjjFPS_checkShadowKill()
	local viewExt = jjjLib1.getDataRef_i(jjjFPS_DR_viewExt_ptr)
	local sunPitch = jjjLib1.getDataRef_f(jjjFPS_DR_sunPitch_ptr)
	local shadowsOff = (sunPitch < (jjjFPS_param("ShKextDg")*viewExt + jjjFPS_param("ShKintDg")*(1 - viewExt))) and 1 or 0
	if shadowsOff ~= jjjFPS_DR_disShdPrep then
		jjjFPS_DR_disShdPrep = shadowsOff
		jjjLib1.setDataRef_f(jjjFPS_DR_disShdPrep_ptr, jjjFPS_DR_disShdPrep)
	end
end

function jjjFPS_main()
	local time = jjjLib1.getClock()
	jjjFPS_frameSec = time - jjjFPS_totalSec
	jjjFPS_totalSec = time
	
	-- if jjjFPS_3jFPSignoreStutters is set (by this or another script), set ignore timer to now + 0.5 sec
	jjjFPS_ignoreFPStimer = (jjjFPS_3jFPSignoreStutters and (jjjFPS_totalSec + 0.5) or jjjFPS_ignoreFPStimer)
	jjjFPS_3jFPSignoreStutters = false

	-- only do calculations, if there was a draw since last call (otherwise sim is not running, e.g. a menu is opened)
	if jjjFPS_didDraw then
		jjjFPS_didDraw = false

		local smartMode = jjjFPS_param("smartM")

		-- calculate fps
		if jjjFPS_totalSec > jjjFPS_ignoreFPStimer then
			if jjjFPS_frameSec < 0.004 then jjjFPS_frameSec = 0.004 end
			if jjjFPS_frameSec > 0.2 then jjjFPS_frameSec = 0.2 end
			jjjFPS_fps = 1/jjjFPS_frameSec
			jjjFPS_fpsSmooth = jjjFPS_fpsSmooth*0.95+jjjFPS_fps*0.05
			if jjjFPS_fps <= 20 then
				jjjFPS_fpsSmooth = jjjFPS_fpsSmooth*0.95 + jjjFPS_fps*0.05
			end

			jjjFPS_tTargetMax     = 1.0/jjjFPS_param("Ftg")
			jjjFPS_tTargetCPUHdrm = jjjFPS_tTargetMax - jjjFPS_tTargetMax*jjjFPS_param("CPUhdrm")*0.01
			jjjFPS_tTargetGPUHdrm = jjjFPS_tTargetMax - jjjFPS_tTargetMax*jjjFPS_param("GPUhdrm")*0.01

			-- calculate (estimate) CPU time and GPU time
			if jjjFPS_useProcTimes then
				-- jjjFPS_swapTime_last = jjjFPS_DR_swapTime
				local swapTimeNew = jjjLib1.getDataRef_f(jjjFPS_DR_swapTime_ptr)
				jjjFPS_DR_swapTime = swapTimeNew - jjjFPS_DR_swapTimeOld
				jjjFPS_DR_swapTimeOld = swapTimeNew

				jjjFPS_cpuTime = jjjFPS_frameSec - jjjFPS_DR_swapTime -- - 0.002

				jjjFPS_gpuTime = jjjLib1.getDataRef_f(jjjFPS_DR_gpuTime_ptr)

				jjjFPS_cpuTimeSmooth = math.min(jjjFPS_tTargetMax*2.0, math.max(jjjFPS_cpuTime, jjjFPS_cpuTimeSmooth*0.98 + jjjFPS_cpuTime*0.02))
				jjjFPS_gpuTimeSmooth = math.min(jjjFPS_tTargetMax*2.0, math.max(jjjFPS_gpuTime, jjjFPS_gpuTimeSmooth*0.98 + jjjFPS_gpuTime*0.02))
			end
		elseif jjjFPS_useProcTimes then
			-- jjjFPS_swapTime_last = jjjFPS_DR_swapTime
			local swapTimeNew = jjjLib1.getDataRef_f(jjjFPS_DR_swapTime_ptr)
			jjjFPS_DR_swapTime = swapTimeNew - jjjFPS_DR_swapTimeOld
			jjjFPS_DR_swapTimeOld = swapTimeNew	
		end
		
		-- change profile if VR switched on or off
		jjjFPS_checkAutoProfile()

		if jjjFPS_autoMode ~= "off" then
			-- check for stutters?
--			if jjjFPS_chkStutters then
--				local nowStutter = ( jjjFPS_fps <= math.min(jjjFPS_fpsSmooth, jjjFPS_param("Fmx"))*jjjFPS_chkStuttThrs )
--				if nowStutter and not jjjFPS_prevFrameStutt then
--					-- current frame might be a stutter frame
--					jjjFPS_prevFrameStutt = true
--				elseif not nowStutter and jjjFPS_prevFrameStutt then
--					-- previous frame was a stutter frame, put it in array with stutter times
--					if (time - jjjFPS_stuttTimes[jjjFPS_currStuttIndex]) > 0.5 then
--						jjjFPS_lastStuttIndex = jjjFPS_currStuttIndex
--						jjjFPS_currStuttIndex = ( jjjFPS_currStuttIndex + 1 ) % 5
--						jjjFPS_stuttTimes[jjjFPS_currStuttIndex] = time
--					end
--					jjjFPS_prevFrameStutt = false
--				end
--				-- check if the last of 5 stutters was in detection time window
--				jjjFPS_stuttersDetected = (time - jjjFPS_stuttTimes[jjjFPS_lastStuttIndex] < jjjFPS_chkStuttTime)
--			else
--				jjjFPS_stuttersDetected = false
--			end

			local aglVisF = 1
			local cpuOverload = false
			local gpuOverload = false

			if jjjFPS_screenShot > 0 then
				-- screen shot: set quality to max (temporarily)
				jjjFPS_screenShot = jjjFPS_screenShot + 1
				if jjjFPS_screenShot == 2 then
					jjjFPS_screenShotOldQ = jjjFPS_QF
					jjjFPS_QF = 10
				elseif jjjFPS_screenShot == 3 then
					jjjFPS_QF = 10
				elseif jjjFPS_screenShot == 4 then
					jjjFPS_QF = 10
					command_once("sim/operation/screenshot")
				else
					jjjFPS_QF = jjjFPS_screenShotOldQ
					jjjFPS_screenShot = 0
				end

			else
				-- no screen shot
				if jjjFPS_useAutoAGL and jjjFPS_param("AGLa") then
					local viewAGL = math.max(0, jjjLib1.getDataRef_f(jjjFPS_drAcfAGL_ptr) + jjjLib1.getDataRef_f(jjjFPS_drViewY_ptr) - jjjLib1.getDataRef_f(jjjFPS_drAcfY_ptr))
					aglVisF = ( viewAGL < jjjFPS_param("AGLh") and viewAGL/jjjFPS_param("AGLh") or 1.0 )
				end

				if jjjFPS_useAutoShK and jjjFPS_param("ShKa") then
					jjjFPS_checkShadowKill()
				end

				if jjjFPS_autoMode == "on" then
					if smartMode then
						-- SMART mode:
						if jjjFPS_fpsSmooth < jjjFPS_param("Ftg") - 1 or jjjFPS_cpuTime > jjjFPS_tTargetMax then
							jjjFPS_destQF = math.max(-10.0, jjjFPS_destQF - jjjFPS_param("Qdec")*jjjFPS_frameSec*2.0)
							cpuOverload = true
						elseif jjjFPS_cpuTimeSmooth > jjjFPS_tTargetCPUHdrm then
							jjjFPS_destQF = math.max(-10.0, jjjFPS_destQF - jjjFPS_param("Qdec")*jjjFPS_frameSec)
							cpuOverload = true
						elseif jjjFPS_cpuTimeSmooth < jjjFPS_tTargetCPUHdrm*0.95 and not jjjFPS_stuttersDetected then
							jjjFPS_destQF = math.min(10.0, jjjFPS_destQF + jjjFPS_param("Qinc")*jjjFPS_frameSec)
						end
						-- move current quality factor diretly to destination value
						jjjFPS_QF = jjjFPS_destQF

						if jjjFPS_gpuTime > jjjFPS_tTargetMax then
							jjjFPS_gpuDestQF = math.max(-10.0, jjjFPS_gpuDestQF - jjjFPS_param("Qdec")*jjjFPS_frameSec*2.0)
							gpuOverload = true
						elseif jjjFPS_gpuTimeSmooth > jjjFPS_tTargetGPUHdrm then
							jjjFPS_gpuDestQF = math.max(-10.0, jjjFPS_gpuDestQF - jjjFPS_param("Qdec")*jjjFPS_frameSec)
							gpuOverload = true
						elseif jjjFPS_gpuTimeSmooth < jjjFPS_tTargetGPUHdrm*0.95 then
							jjjFPS_gpuDestQF = math.min(10.0, jjjFPS_gpuDestQF + jjjFPS_param("Qinc")*jjjFPS_frameSec)
						end
						-- move current quality factor diretly to destination value
						jjjFPS_gpuQF = jjjFPS_gpuDestQF
					else
						-- BASIC mode:
						-- inc/dec destination quality factor if FPS exceed min/max values
						if jjjFPS_fpsSmooth > jjjFPS_param("Fmx") - 1.0 and not jjjFPS_stuttersDetected then
							jjjFPS_destQF = jjjFPS_destQF + jjjFPS_param("Qinc")*jjjFPS_frameSec*1.01
							if jjjFPS_destQF > 10.05 then jjjFPS_destQF = 10.05 end
						elseif jjjFPS_fpsSmooth < jjjFPS_param("Fmn") - 0.25 or jjjFPS_stuttersDetected then
							jjjFPS_destQF = jjjFPS_destQF - jjjFPS_param("Qdec")*jjjFPS_frameSec*1.01
							if jjjFPS_destQF < -10.05 then jjjFPS_destQF = -10.05 end
						else
							jjjFPS_destQF = jjjFPS_destQF + jjjFPS_param("Qinc")*0.2*jjjFPS_frameSec
							if jjjFPS_destQF > 10.05 then jjjFPS_destQF = 10.05 end
						end
						-- move current quality factor smoothly to destination value
						if jjjFPS_QF - jjjFPS_destQF >= 0.05 then
							jjjFPS_QF = math.max(-10.0, jjjFPS_QF - jjjFPS_param("Qdec")*jjjFPS_frameSec)
						elseif jjjFPS_QF - jjjFPS_destQF <= -0.05 then
							jjjFPS_QF = math.min(10.0, jjjFPS_QF + jjjFPS_param("Qinc")*jjjFPS_frameSec)
						end

						jjjFPS_gpuQF = jjjFPS_QF

					end
				elseif jjjFPS_autoMode == "max" then
					jjjFPS_QF = -10
					jjjFPS_gpuQF = -10
					jjjFPS_setFSRmode(jjjFPS_param("FSRmn"), false)
				elseif jjjFPS_autoMode == "min" then
					jjjFPS_QF = 10
					jjjFPS_gpuQF = 10
					jjjFPS_setFSRmode(jjjFPS_param("FSRmx"), false)
				end
			end

			local genQF = jjjFPS_QF  -- for features that affect CPU and GPU
			local cpuQF = jjjFPS_QF  -- for features that affect CPU only
			local gpuQF = jjjFPS_QF  -- for features that affect GPU only
			if smartMode then
				genQF = math.min(jjjFPS_QF, jjjFPS_gpuQF)
				cpuQF = jjjFPS_QF
				gpuQF = jjjFPS_gpuQF
			end

			-- LOD (affects CPU and GPU)
			if jjjFPS_param("LDa") then
				if genQF <= jjjFPS_param("LDmnQ") then
					jjjFPS_DR_lodBias = jjjFPS_param("LDmn")
				elseif genQF >= jjjFPS_param("LDmxQ") then
					jjjFPS_DR_lodBias = jjjFPS_param("LDmx")
				else
					jjjFPS_DR_lodBias = jjjFPS_param("LDmn") + ( jjjFPS_param("LDmx") - jjjFPS_param("LDmn") )*( genQF - jjjFPS_param("LDmnQ") )/( jjjFPS_param("LDmxQ") - jjjFPS_param("LDmnQ") )
				end
				jjjLib1.setDataRef_f(jjjFPS_DR_lodBias_ptr, jjjFPS_DR_lodBias + 1 - aglVisF)
			end

			-- shadow distance (affects CPU and GPU)
			if jjjFPS_useAutoShD and jjjFPS_param("ShDa") then
				if genQF <= jjjFPS_param("ShDmnQ") then
					jjjFPS_DR_shdLimitInt = jjjFPS_param("ShDmn")
				elseif genQF >= jjjFPS_param("ShDmxQ") then
					jjjFPS_DR_shdLimitInt = jjjFPS_param("ShDmx")
				else
					jjjFPS_DR_shdLimitInt = jjjFPS_param("ShDmn") + ( jjjFPS_param("ShDmx") - jjjFPS_param("ShDmn") )*( genQF - jjjFPS_param("ShDmnQ") )/( jjjFPS_param("ShDmxQ") - jjjFPS_param("ShDmnQ") )
				end
				jjjFPS_DR_shdLimitExt = jjjFPS_DR_shdLimitInt
				jjjLib1.setDataRef_f(jjjFPS_DR_shdLimitInt_ptr, jjjFPS_DR_shdLimitInt)
				jjjLib1.setDataRef_f(jjjFPS_DR_shdLimitExt_ptr, jjjFPS_DR_shdLimitExt)
			end

			-- cloud quality (affects only GPU)
			if jjjFPS_useAutoCLD and jjjFPS_param("CLDa") then
				if gpuQF <= jjjFPS_param("CLDmnQ") then
					jjjFPS_curCloudQ = jjjFPS_param("CLDmn")
				elseif gpuQF >= jjjFPS_param("CLDmxQ") then
					jjjFPS_curCloudQ = jjjFPS_param("CLDmx")
				else
					jjjFPS_curCloudQ = jjjFPS_param("CLDmn") + ( jjjFPS_param("CLDmx") - jjjFPS_param("CLDmn") )*( gpuQF - jjjFPS_param("CLDmnQ") )/( jjjFPS_param("CLDmxQ") - jjjFPS_param("CLDmnQ") )
				end
				-- jjjFPS_DR_cloudsSegSteps  = math.max(jjjFPS_DR_cloudsSegStepsOrig/math.sqrt(jjjFPS_curCloudQ/2 + 0.5), jjjFPS_DR_cloudsSegStepsOrig)
				jjjFPS_DR_cloudsSegSteps  = jjjFPS_DR_cloudsSegStepsOrig
				jjjFPS_DR_cloudsStepStart = jjjFPS_DR_cloudsStepStartOrig/jjjFPS_curCloudQ
				jjjLib1.setDataRef_f(jjjFPS_DR_cloudsSegSteps_ptr, jjjFPS_DR_cloudsSegSteps)
				jjjLib1.setDataRef_f(jjjFPS_DR_cloudsStepStart_ptr, jjjFPS_DR_cloudsStepStart)
			end

			-- FSR (affects only GPU)
			if jjjFPS_useAutoFSR and jjjFPS_param("FSRa") and jjjFPS_autoMode == "on" then
				if jjjFPS_screenShot > 0 then
					jjjFPS_setFSRmode(4, true)
				elseif smartMode then
					if gpuOverload then
						if jjjFPS_curFSRmode > jjjFPS_param("FSRmn") and time - jjjFPS_lastChangeFSRtime > 1.0 then
							jjjFPS_setFSRmode(jjjFPS_curFSRmode - 1, false)
						end
						jjjFPS_cantIncFSRtime = time
					elseif jjjFPS_curFSRmode < jjjFPS_param("FSRmx") then
						local incFSRfactor = 1.25
						if jjjFPS_gpuTimeSmooth*incFSRfactor > jjjFPS_tTargetGPUHdrm*0.95 then
							jjjFPS_cantIncFSRtime = time
						elseif time - jjjFPS_cantIncFSRtime > jjjFPS_param("FSRtmInc") then
							jjjFPS_setFSRmode(jjjFPS_curFSRmode + 1, false)
						end
					end
				else
					if gpuQF <= jjjFPS_param("FSRmnQ") then
						jjjFPS_setFSRmode(jjjFPS_param("FSRmn"), false)
					elseif gpuQF >= jjjFPS_param("FSRmxQ") then
						if time - jjjFPS_lastChangeFSRtime > jjjFPS_param("FSRtmInc") then
							jjjFPS_setFSRmode(jjjFPS_param("FSRmx"), false)
						end
					else
						local FSRmode = math.floor( jjjFPS_param("FSRmn") + ( jjjFPS_param("FSRmx") - jjjFPS_param("FSRmn") )*( gpuQF - jjjFPS_param("FSRmnQ") )/( jjjFPS_param("FSRmxQ") - jjjFPS_param("FSRmnQ") ) + 0.5 )
						if FSRmode < jjjFPS_curFSRmode or ( FSRmode > jjjFPS_curFSRmode and time - jjjFPS_lastChangeFSRtime > jjjFPS_param("FSRtmInc") ) then
							jjjFPS_setFSRmode(FSRmode, false)
						end
					end
				end
			end

		end
		jjjFPS_calcFPSmeter()

		if jjjFPS_moveMode == 1 then
			if jjjFPS_totalSec - jjjFPS_moveModeTimer > 10 then
				jjjFPS_setMoveMode(0)
				jjjFPS_refreshPanel()
			end
		elseif jjjFPS_moveMode == 2 then
			if jjjFPS_totalSec - jjjFPS_moveModeTimer > 30 then
				jjjFPS_setMoveMode(0)
				jjjFPS_setPosition()
				jjjFPS_refreshPanel()
			end
		end

	end
end

-- calculate fps meter (graphical display)
function jjjFPS_calcFPSmeter()
	if jjjFPS_param("disG") then
		jjjFPS_meterFps = jjjFPS_fpsSmooth

		jjjFPS_meterFpsMin = jjjFPS_meterFpsMin + jjjFPS_meterFpsSpd*jjjFPS_frameSec
		if jjjFPS_fps < jjjFPS_meterFpsMin then
			jjjFPS_meterFpsMin = jjjFPS_fps
		elseif jjjFPS_meterFpsMin > jjjFPS_meterFps then
			jjjFPS_meterFpsMin = jjjFPS_meterFps
		end
		if jjjFPS_meterFpsMin < 5 then jjjFPS_meterFpsMin = 5 end

		jjjFPS_meterFpsMax = jjjFPS_meterFpsMax - jjjFPS_meterFpsSpd*jjjFPS_frameSec
		if jjjFPS_fps > jjjFPS_meterFpsMax then
			jjjFPS_meterFpsMax = jjjFPS_fps
		elseif jjjFPS_meterFpsMax < jjjFPS_meterFps then
			jjjFPS_meterFpsMax = jjjFPS_meterFps
		end
		if jjjFPS_meterFpsMax > jjjFPS_param("MTto") + 20 then jjjFPS_meterFpsMax = jjjFPS_param("MTto") + 20 end
	end
end


function jjjFPS_draw()
	jjjFPS_didDraw = true

	if jjjFPS_screenShot > 0 then
		return
	end

	local panelX = 0
	local panelY = 0
	local lineHeight = jjjFPS_lineHeight

	local tx = ""
	local LDa  = jjjFPS_param("LDa") -- and jjjFPS_useAutoLod
	local CLDa = jjjFPS_param("CLDa") and jjjFPS_useAutoCLD
	local FSRa = jjjFPS_param("FSRa") and jjjFPS_useAutoFSR
	local disM = jjjFPS_param("disM")
	local disL = jjjFPS_param("disL")
	local disN = jjjFPS_param("disN")
	local disG = jjjFPS_param("disG")
	local disU = ( jjjFPS_useProcTimes and jjjFPS_param("disU") )
	local disD = jjjFPS_param("disD")

	if jjjFPS_panelMode == "wiz" or jjjFPS_panelMode == "adv" then
		disL = true
		disN = true
		disG = true
		disU = jjjFPS_useProcTimes
		disD = true
	end
	disD = (jjjFPS_autoMode == "off" and false or disD)
	if not disN and not disG then
		disU = false
		disD = false
		disL = false
	end

	local meterW = jjjFPS_meterWidth
	jjjFPS_width = 0
	if disD then
		if disL then jjjFPS_width = jjjFPS_width + 23.5*jjjFPS_fontSize end
		if disN then jjjFPS_width = jjjFPS_width + 28*jjjFPS_fontSize2 end
	else
		if disL then jjjFPS_width = jjjFPS_width + 23.5*jjjFPS_fontSize end
		if disN then jjjFPS_width = jjjFPS_width + 28*jjjFPS_fontSize2 end
	end
	if disG then jjjFPS_width = jjjFPS_width + meterW end

	if disD then
		jjjFPS_height = 0
		if LDa then jjjFPS_height = jjjFPS_height + lineHeight end
		if FSRa then jjjFPS_height = jjjFPS_height + lineHeight end
	else
		jjjFPS_height = ( disG and lineHeight or 0.0 )
	end
	if jjjFPS_autoMode ~= "off" then
		jjjFPS_height = jjjFPS_height + lineHeight
	end
	if disU then
		jjjFPS_height = jjjFPS_height + lineHeight*2
	end

	local show = true
	if jjjFPS_moveMode == 2 then
		if MOUSE_X < SCREEN_WIDTH*0.5 then
			jjjFPS_dispX = math.max(4, MOUSE_X - jjjFPS_width*0.5)
		else
			jjjFPS_dispX = math.min(-8, MOUSE_X + jjjFPS_width*0.5 - SCREEN_WIDTH)
		end
		if MOUSE_Y < SCREEN_HIGHT*0.5 then
			jjjFPS_dispY = math.max(4, MOUSE_Y - jjjFPS_height*0.5)
		else
			jjjFPS_dispY = math.min(-20, MOUSE_Y + jjjFPS_height*0.5 - SCREEN_HIGHT)
		end
	elseif jjjFPS_moveMode == 0 and disM == "hov" and jjjFPS_panelMode == "" and ( MOUSE_X < jjjFPS_x or MOUSE_X > jjjFPS_x + jjjFPS_width or MOUSE_Y < jjjFPS_y + lineHeight - jjjFPS_height or MOUSE_Y > jjjFPS_y + lineHeight) then
		show = false
	elseif jjjFPS_moveMode == 0 and disM == "bad" and jjjFPS_panelMode == "" and jjjFPS_fps > jjjFPS_param("MTbd") then
		show = false
	end

	jjjFPS_lastShowTime = ( show and jjjFPS_totalSec or jjjFPS_lastShowTime )
	if jjjFPS_totalSec - jjjFPS_lastShowTime > 3 then
		jjjFPS_fadeAlpha = jjjFPS_fadeAlpha - jjjFPS_frameSec*2
		jjjFPS_fadeAlpha = ( jjjFPS_fadeAlpha < 0 and 0.0 or jjjFPS_fadeAlpha )
	else
		jjjFPS_fadeAlpha = jjjFPS_fadeAlpha + jjjFPS_frameSec*2
		jjjFPS_fadeAlpha = ( jjjFPS_fadeAlpha > 1.0 and 1.0 or jjjFPS_fadeAlpha )
	end

	jjjFPS_dispAlpha = jjjFPS_param("disA")*jjjFPS_fadeAlpha

	if jjjFPS_fadeAlpha > 0 and ( disN or disG or jjjFPS_panelMode ~= "" ) then
		XPLMSetGraphicsState(0,0,0,1,1,0,0)

		if jjjFPS_dispX > 0 then
			jjjFPS_x = jjjFPS_dispX
			panelX = jjjFPS_dispX
		else
			jjjFPS_x = SCREEN_WIDTH + jjjFPS_dispX - jjjFPS_width
			panelX = jjjFPS_dispX
		end
		if jjjFPS_dispY > 0 then
			jjjFPS_y = jjjFPS_dispY + jjjFPS_height - lineHeight*0.75
			panelY = jjjFPS_y + lineHeight*1.5
		else
			jjjFPS_y = SCREEN_HIGHT - lineHeight*0.75 + jjjFPS_dispY
			panelY = jjjFPS_dispY - jjjFPS_height - lineHeight*1.5
		end

		jjjLib1.setPanelPos(jjjFPS_plId, jjjFPS_pnId, panelX, panelY)

		if jjjFPS_param("disBgA") > 0 then
			graphics.set_color(0.0, 0.0, 0.0, jjjFPS_dispAlpha*jjjFPS_param("disBgA"))
			graphics.draw_rectangle(jjjFPS_x - jjjFPS_borderWidth*2 + 0.5, jjjFPS_y + jjjFPS_borderWidth + lineHeight - 2, jjjFPS_x + jjjFPS_width + jjjFPS_borderWidth*2 + 0.5, jjjFPS_y + lineHeight - jjjFPS_height - jjjFPS_borderWidth*2 - 0.5)
		end

		jjjFPS_curX = jjjFPS_x
		jjjFPS_curY = jjjFPS_y
		if disL then
			-- display labels ("FPS", "LOD", ...)
			if disU then
				jjjFPS_drawString(jjjFPS_curX, jjjFPS_curY, "CPU", jjjFPS_dispMark == "cpu" or jjjFPS_dispMark == "cpugpu")
				jjjFPS_curY = jjjFPS_curY - lineHeight
				jjjFPS_drawString(jjjFPS_curX, jjjFPS_curY, "GPU", jjjFPS_dispMark == "gpu" or jjjFPS_dispMark == "cpugpu")
				jjjFPS_curY = jjjFPS_curY - lineHeight
			end
			jjjFPS_drawString(jjjFPS_curX, jjjFPS_curY, "FPS", jjjFPS_dispMark == "fps")
			jjjFPS_curY = jjjFPS_curY - lineHeight
			if disD then
				if LDa  then
					jjjFPS_drawString(jjjFPS_curX, jjjFPS_curY, "LOD", jjjFPS_dispMark == "lod")
					jjjFPS_curY = jjjFPS_curY - lineHeight
				end
--				if CLDa  then
--					jjjFPS_drawString(jjjFPS_curX, jjjFPS_curY, "Clds", jjjFPS_dispMark == "cld")
--					jjjFPS_curY = jjjFPS_curY - lineHeight
--				end
--				if FSRa then
--					jjjFPS_drawString(jjjFPS_curX, jjjFPS_curY, "FSR", jjjFPS_dispMark == "fsr")
--					jjjFPS_curY = jjjFPS_curY - lineHeight
--				end
				jjjFPS_curX = jjjFPS_curX + 24*jjjFPS_fontSize
			else
				if disG and jjjFPS_autoMode ~= "off" then
					jjjFPS_drawString(jjjFPS_curX, jjjFPS_curY, "Q")
					jjjFPS_curY = jjjFPS_curY - lineHeight
				end
				jjjFPS_curX = jjjFPS_curX + 24*jjjFPS_fontSize
			end
		end

		jjjFPS_curY = jjjFPS_y

		if disN then
			if disU then
				jjjFPS_drawString(jjjFPS_curX, jjjFPS_curY, string.format("%04.1f", jjjFPS_cpuTime*1000), false)
				jjjFPS_curY = jjjFPS_curY - lineHeight
				jjjFPS_drawString(jjjFPS_curX, jjjFPS_curY, string.format("%04.1f", jjjFPS_gpuTime*1000), false)
				jjjFPS_curY = jjjFPS_curY - lineHeight
			end
			-- draw FPS
			jjjFPS_drawString(jjjFPS_curX, jjjFPS_curY, string.format("%2d", jjjFPS_fpsSmooth), jjjFPS_fps < 20)
			jjjFPS_curY = jjjFPS_curY - lineHeight

			if disD then
				-- draw LOD bias
				if LDa then
					jjjFPS_drawString(jjjFPS_curX, jjjFPS_curY, string.format("%1.2f", jjjFPS_DR_lodBias))
					jjjFPS_curY = jjjFPS_curY - lineHeight
				end
--				-- draw cloud quality
--				if CLDa then
--					jjjFPS_drawString(jjjFPS_curX, jjjFPS_curY, string.format("%1.2f", jjjFPS_curCloudQ))
--					jjjFPS_curY = jjjFPS_curY - lineHeight
--				end
--				-- draw FSR mode?
--				if FSRa then
--					jjjFPS_drawString(jjjFPS_curX, jjjFPS_curY, jjjFPS_curFSRmode)
--					jjjFPS_curY = jjjFPS_curY - lineHeight
--				end
			elseif FSRa then
				jjjFPS_drawString(jjjFPS_curX, jjjFPS_curY, "fsr " .. (4 - jjjFPS_curFSRmode))
			end
			jjjFPS_curX = jjjFPS_curX + 28*jjjFPS_fontSize2
		end

		if disG then
			-- graphical display
			jjjFPS_curY = jjjFPS_y

			if disU then
				-- CPU time
				jjjFPS_meterY1 = jjjFPS_curY
				jjjFPS_meterY2 = jjjFPS_curY + lineHeight - jjjFPS_borderWidth*2
				jjjFPS_drawProcLoadbar(jjjFPS_param("CPUhdrm"), jjjFPS_cpuTime*jjjFPS_param("Ftg"), jjjFPS_cpuTimeSmooth*jjjFPS_param("Ftg"), meterW, jjjFPS_dispAlpha)
				jjjFPS_curY = jjjFPS_curY - lineHeight

				-- GPU time
				jjjFPS_meterY1 = jjjFPS_curY
				jjjFPS_meterY2 = jjjFPS_curY + lineHeight - jjjFPS_borderWidth*2
				jjjFPS_drawProcLoadbar(jjjFPS_param("GPUhdrm"), jjjFPS_gpuTime*jjjFPS_param("Ftg"), jjjFPS_gpuTimeSmooth*jjjFPS_param("Ftg"), meterW, jjjFPS_dispAlpha)
				jjjFPS_curY = jjjFPS_curY - lineHeight
			end

			-- FPS
			jjjFPS_meterY1 = jjjFPS_curY - jjjFPS_borderWidth
			jjjFPS_meterY4 = jjjFPS_curY + lineHeight - jjjFPS_borderWidth
			jjjFPS_meterY2 = jjjFPS_meterY1 + jjjFPS_borderWidth
			jjjFPS_meterY3 = jjjFPS_meterY4 - jjjFPS_borderWidth

			jjjFPS_meterXFps = jjjFPS_curX + math.min(jjjFPS_meterXR, math.max(0.0, ( jjjFPS_meterFps - jjjFPS_param("MTfr") )*jjjFPS_meterScaleX))
			jjjFPS_meterXMin = jjjFPS_curX + math.min(jjjFPS_meterXR, math.max(0.0, ( jjjFPS_meterFpsMin - jjjFPS_param("MTfr") )*jjjFPS_meterScaleX))
			jjjFPS_meterXMax = jjjFPS_curX + math.min(jjjFPS_meterXR, math.max(0.0, ( jjjFPS_meterFpsMax - jjjFPS_param("MTfr") )*jjjFPS_meterScaleX))

			graphics.set_color(0.7, 0.0, 0.0, jjjFPS_dispAlpha)
			graphics.draw_rectangle(jjjFPS_curX, jjjFPS_meterY2, jjjFPS_curX + jjjFPS_meterXB, jjjFPS_meterY3)

			if jjjFPS_stuttersDetected then
				graphics.draw_rectangle(jjjFPS_curX, jjjFPS_meterY1 + jjjFPS_borderWidth - 1, jjjFPS_meterXFps + 1, jjjFPS_meterY1)
				graphics.draw_rectangle(jjjFPS_curX, jjjFPS_meterY4 - jjjFPS_borderWidth + 1, jjjFPS_meterXFps + 1, jjjFPS_meterY4)
			end

			graphics.set_color(0.8, 0.7, 0.0, jjjFPS_dispAlpha)
			graphics.draw_rectangle(jjjFPS_curX + jjjFPS_meterXB, jjjFPS_meterY2, jjjFPS_curX + jjjFPS_meterXG, jjjFPS_meterY3)
			if jjjFPS_meterXG <= jjjFPS_meterXR then
				graphics.set_color(0.0, 0.6, 0.0, jjjFPS_dispAlpha)
				graphics.draw_rectangle(jjjFPS_curX + jjjFPS_meterXG, jjjFPS_meterY2, jjjFPS_curX + jjjFPS_meterXR + 1, jjjFPS_meterY3)
			end
			graphics.set_color(1.0, 1.0, 1.0, jjjFPS_dispAlpha*0.75)
			graphics.draw_rectangle(jjjFPS_meterXMin, jjjFPS_meterY2, jjjFPS_meterXMax, jjjFPS_meterY3)

			graphics.set_color(1.0, 1.0, 1.0, jjjFPS_dispAlpha)
			graphics.draw_rectangle(jjjFPS_meterXFps - jjjFPS_borderWidth*0.5, jjjFPS_meterY1, jjjFPS_meterXFps + jjjFPS_borderWidth*0.5, jjjFPS_meterY4)

			if disD then
				jjjFPS_curY = jjjFPS_curY - lineHeight
				jjjFPS_meterY1 = jjjFPS_curY - 4
				jjjFPS_meterY2 = jjjFPS_curY
				-- draw indicator for auto LOD
				if LDa then
					jjjFPS_drawQbar(jjjFPS_param("LDmnQ"), jjjFPS_param("LDmxQ"), jjjFPS_meterXorigLD)
					jjjFPS_curY = jjjFPS_curY - lineHeight
				end
--				-- draw indicator for cloud quality
--				if CLDa then
--					jjjFPS_drawQbar(jjjFPS_param("CLDmnQ"), jjjFPS_param("CLDmxQ"), 1.0)
--					jjjFPS_curY = jjjFPS_curY - lineHeight
--				end
--				if not disN then
--					if FSRa then
--						-- draw AA mode?
--						if jjjFPS_useAutoFSR then
--							jjjFPS_drawString(jjjFPS_curX, jjjFPS_curY, jjjFPS_curFSRmode)
--						end
--						jjjFPS_curY = jjjFPS_curY - lineHeight
--					end
--				end
				jjjFPS_meterY1 = math.min( jjjFPS_curY + lineHeight*2 - 4, jjjFPS_meterY2 )
				jjjFPS_curY = jjjFPS_curY + lineHeight
			else
				jjjFPS_curY = jjjFPS_curY - lineHeight*0.6
				jjjFPS_meterY1 = jjjFPS_curY - 4
				jjjFPS_meterY2 = jjjFPS_curY
				jjjFPS_curY = jjjFPS_curY - lineHeight
			end
			-- indicate quality factor?
			if jjjFPS_autoMode ~= "off" and ( LDa or CDa or CSa ) then
				if jjjFPS_autoMode == "on" then
					graphics.set_color(1.0, 1.0, 1.0, jjjFPS_dispAlpha)
				else
					graphics.set_color(0.7, 0.0, 0.0, jjjFPS_dispAlpha)
				end
				if disD then
					jjjFPS_meterX1 = jjjFPS_curX + jjjFPS_meterXR*0.5
					-- draw marker for quality factor
					if jjjFPS_param("smartM") then
						if LDa then
							jjjFPS_meterX2 = jjjFPS_meterX1 + math.min(jjjFPS_QF, jjjFPS_gpuQF)*meterW*0.05
							graphics.draw_rectangle(jjjFPS_meterX2 - jjjFPS_borderWidth*0.5, jjjFPS_meterY2, jjjFPS_meterX2 + jjjFPS_borderWidth*0.5, jjjFPS_meterY2 + lineHeight*0.8 - 0.5)
						end
						if CDa or CSa then
							jjjFPS_meterX2 = jjjFPS_meterX1 + jjjFPS_gpuQF*meterW*0.05
							if CDa and CSa then
								graphics.draw_rectangle(jjjFPS_meterX2 - jjjFPS_borderWidth*0.5, jjjFPS_meterY2 - lineHeight*2, jjjFPS_meterX2 + jjjFPS_borderWidth*0.5, jjjFPS_meterY2 - lineHeight + lineHeight*0.8 - 0.5)
							else
								graphics.draw_rectangle(jjjFPS_meterX2 - jjjFPS_borderWidth*0.5, jjjFPS_meterY2 - lineHeight, jjjFPS_meterX2 + jjjFPS_borderWidth*0.5, jjjFPS_meterY2 - lineHeight + lineHeight*0.8 - 0.5)
							end
						end
					else
						jjjFPS_meterX2 = jjjFPS_meterX1 + jjjFPS_QF*meterW*0.05
						graphics.draw_rectangle(jjjFPS_meterX2 - jjjFPS_borderWidth*0.5, jjjFPS_meterY1 - lineHeight*0.4, jjjFPS_meterX2 + jjjFPS_borderWidth*0.5, jjjFPS_meterY2 + lineHeight*0.4)
					end
				else
					jjjFPS_meterX1 = jjjFPS_curX + jjjFPS_meterXR*0.5
					jjjFPS_meterX2 = jjjFPS_meterX1 + jjjFPS_QF*meterW*0.05
					-- draw bar for quality factor
					graphics.draw_rectangle(jjjFPS_meterX1, jjjFPS_meterY1, jjjFPS_meterX2, jjjFPS_meterY2)
					-- draw marker for quality factor
					graphics.draw_rectangle(jjjFPS_meterX2 - jjjFPS_borderWidth*0.5, jjjFPS_meterY1 - jjjFPS_borderWidth*2, jjjFPS_meterX2 + jjjFPS_borderWidth*0.5, jjjFPS_meterY2 + jjjFPS_borderWidth*2)
					-- if jjjFPS_gpuQF ~= jjjFPS_QF then
					-- 	jjjFPS_meterX2 = jjjFPS_meterX1 + jjjFPS_gpuQF*meterW*0.05
					-- 	graphics.draw_rectangle(jjjFPS_meterX2 - jjjFPS_borderWidth*0.5, jjjFPS_meterY1 - jjjFPS_borderWidth*2, jjjFPS_meterX2 + jjjFPS_borderWidth*0.5, jjjFPS_meterY2 + jjjFPS_borderWidth*2)
					-- end
				end
			end
		end

		if jjjFPS_panelMode ~= "" then
			local x1
			local y1
			local x2
			local y2
			x1, y1, x2, y2 = jjjLib1.getPanelElementPos(jjjFPS_plId, jjjFPS_pnId, jjjFPS_panelFpsDispId)
			if x1 and y1 and x2 and y2 then
				graphics.set_color(0.73, 0.73, 0.86, 1)
				tx = string.format( "%1.0f", jjjFPS_fpsSmooth )
				draw_string_Helvetica_18(x1 + 80 - measure_string(tx, "Helvetica_18"), y1 + 2, tx)
				graphics.set_color(0.0, 0.6, 0.0, 1)
				tx = string.format( "+%1.0f", math.max(0, jjjFPS_meterFpsMax - jjjFPS_fpsSmooth) )
				draw_string_Helvetica_12(x1 + 102 - measure_string(tx, "Helvetica_12"), y1 + 2, tx)
				graphics.set_color(0.7, 0.0, 0.0, 1)
				tx = string.format( "-%1.0f", math.max(0, jjjFPS_fpsSmooth - jjjFPS_meterFpsMin) )
				draw_string_Helvetica_12(x1 + 49 - measure_string(tx, "Helvetica_12"), y1 + 2, tx)
			end
		end
		
		if jjjFPS_moveMode > 0 then
			local x1 = jjjFPS_x - jjjFPS_borderWidth*2 + 0.5
			local x2 = jjjFPS_x + jjjFPS_width + jjjFPS_borderWidth*2 + 0.5
			local y1 = jjjFPS_y + jjjFPS_borderWidth + lineHeight - 2
			local y2 = jjjFPS_y + lineHeight - jjjFPS_height - jjjFPS_borderWidth*2 - 0.5
			local x = x1*0.5 + x2*0.5
			local y = y1 + 15
			graphics.set_color(1.0, 1.0, 1.0, 1.0)
			graphics.draw_line(x1, y1, x2, y1)
			graphics.draw_line(x2, y1, x2, y2)
			graphics.draw_line(x1, y2, x2, y2)
			graphics.draw_line(x1, y1, x1, y2)
			if jjjFPS_moveMode == 1 then
				if jjjFPS_totalSec*2 % 2 < 1 then
					tx = "Click to move/resize"
					local w = measure_string(tx, "Helvetica_12")
					graphics.set_color(0.0, 0.0, 0.0, 0.8)
					local xt = math.max(0, x - w*0.5 - 4)
					graphics.draw_rectangle(xt, y - 10, xt + w + 4, y + 10)
					graphics.set_color(1.0, 1.0, 1.0, 1.0)
					draw_string_Helvetica_12(xt, y - 4, tx)
				end
			elseif jjjFPS_moveMode == 2 then
				local w = 120
				graphics.set_color(0.0, 0.0, 0.0, 0.8)
				local xt = math.max(0, x - w*0.5 - 4)
				graphics.draw_rectangle(xt, y - 15, xt + w + 4, y + 15)
				graphics.set_color(1.0, 1.0, 1.0, 1.0)
				draw_string_Helvetica_12(xt, y + 3 , "Left click: set position")
				draw_string_Helvetica_12(xt, y - 11, "Mouse wheel: resize")
			end
		end	

	end
end

function jjjFPS_drawString(x, y, str, red)
	if jjjFPS_fontSize == 1 then
		if red then
			draw_string(x, y, str, 0.8, 0, 0 )
		else
			draw_string(x, y, str)
		end
	else
		if red then
			graphics.set_color(0.8, 0, 0, jjjFPS_dispAlpha)
		else
			graphics.set_color(1, 1, 1, jjjFPS_dispAlpha)
		end
			draw_string_Helvetica_18(x, y, str)
	end
end
function jjjFPS_drawQbar(minQ, maxQ, origValX)
	jjjFPS_meterX1 = jjjFPS_curX + ( minQ + 10 )*jjjFPS_meterWidth*0.05 - 1
	jjjFPS_meterX2 = jjjFPS_curX + ( maxQ + 10 )*jjjFPS_meterWidth*0.05
	graphics.set_color(1.0, 1.0, 1.0, jjjFPS_dispAlpha*0.6)
	graphics.draw_triangle(jjjFPS_meterX1, jjjFPS_curY, jjjFPS_meterX2, jjjFPS_curY + jjjFPS_lineHeight - jjjFPS_borderWidth*2, jjjFPS_meterX2, jjjFPS_curY)
	graphics.draw_rectangle(jjjFPS_meterX2, jjjFPS_curY + jjjFPS_lineHeight - jjjFPS_borderWidth*2, jjjFPS_curX + jjjFPS_meterXR + 1, jjjFPS_curY)
	if jjjFPS_meterShowOrig and origValX then
		graphics.draw_rectangle(jjjFPS_curX + origValX, jjjFPS_curY + jjjFPS_lineHeight - jjjFPS_borderWidth*2, jjjFPS_curX + origValX + 1, jjjFPS_curY)
	end
end
function jjjFPS_drawProcLoadbar(headroom, usage, usageSmooth, meterW, alpha)
	if headroom ~= nil then
		graphics.set_color(1.0, 1.0, 1.0, alpha*0.4)
		graphics.draw_rectangle(jjjFPS_curX + (100.0 - headroom)*meterW/100, jjjFPS_meterY1, jjjFPS_curX + meterW + 0.5, jjjFPS_meterY2)
	end

	graphics.set_color(1.0, 1.0, 1.0, alpha)
	if usage > 1 then
		usage = 2.0 - usage
		usage = ( usage < 0 and 0 or usage )
		graphics.draw_rectangle(jjjFPS_curX, jjjFPS_meterY1 + jjjFPS_borderWidth, jjjFPS_curX + usage*meterW + 0.5, jjjFPS_meterY2 - jjjFPS_borderWidth)
		graphics.set_color(0.7, 0.0, 0.0, alpha)
		graphics.draw_rectangle(jjjFPS_curX + usage*meterW, jjjFPS_meterY1 + jjjFPS_borderWidth, jjjFPS_curX + meterW + 0.5, jjjFPS_meterY2 - jjjFPS_borderWidth)
	else
		graphics.draw_rectangle(jjjFPS_curX, jjjFPS_meterY1 + jjjFPS_borderWidth, jjjFPS_curX + usage*meterW + 0.5, jjjFPS_meterY2 - jjjFPS_borderWidth)
	end

	if usageSmooth ~= nil then
		if usageSmooth > 1 then
			usageSmooth = 2.0 - usageSmooth
			usageSmooth = ( usageSmooth < 0 and 0 or usageSmooth )
			graphics.set_color(1.0, 1.0, 1.0, alpha)
			-- graphics.draw_rectangle(jjjFPS_curX, jjjFPS_meterY1 + jjjFPS_borderWidth, jjjFPS_curX + usageSmooth*meterW + 0.5, jjjFPS_meterY2 - jjjFPS_borderWidth)
			graphics.draw_line(jjjFPS_curX, jjjFPS_meterY1 + jjjFPS_borderWidth, jjjFPS_curX + usageSmooth*meterW + 0.5, jjjFPS_meterY1 + jjjFPS_borderWidth)
			-- graphics.draw_line(jjjFPS_curX + usageSmooth*meterW + 0.5, jjjFPS_meterY1 + jjjFPS_borderWidth, jjjFPS_curX + usageSmooth*meterW + 0.5, jjjFPS_meterY2 - jjjFPS_borderWidth)
			graphics.draw_line(jjjFPS_curX, jjjFPS_meterY2 - jjjFPS_borderWidth, jjjFPS_curX + usageSmooth*meterW + 0.5, jjjFPS_meterY2 - jjjFPS_borderWidth)
			graphics.set_color(0.7, 0.0, 0.0, alpha)
			-- graphics.draw_rectangle(jjjFPS_curX + usageSmooth*meterW, jjjFPS_meterY1 + jjjFPS_borderWidth, jjjFPS_curX + meterW + 0.5, jjjFPS_meterY2 - jjjFPS_borderWidth)
			graphics.draw_line(jjjFPS_curX + usageSmooth*meterW, jjjFPS_meterY1 + jjjFPS_borderWidth, jjjFPS_curX + meterW + 0.5, jjjFPS_meterY1 + jjjFPS_borderWidth)
			graphics.draw_line(jjjFPS_curX + usageSmooth*meterW, jjjFPS_meterY1 + jjjFPS_borderWidth, jjjFPS_curX + usageSmooth*meterW, jjjFPS_meterY2 - jjjFPS_borderWidth)
			graphics.draw_line(jjjFPS_curX + usageSmooth*meterW, jjjFPS_meterY2 - jjjFPS_borderWidth, jjjFPS_curX + meterW + 0.5, jjjFPS_meterY2 - jjjFPS_borderWidth)
		else
			graphics.set_color(1.0, 1.0, 1.0, alpha)
			graphics.draw_line(jjjFPS_curX, jjjFPS_meterY1 + jjjFPS_borderWidth, jjjFPS_curX + usageSmooth*meterW + 0.5, jjjFPS_meterY1 + jjjFPS_borderWidth)
			graphics.draw_line(jjjFPS_curX + usageSmooth*meterW + 0.5, jjjFPS_meterY1 + jjjFPS_borderWidth, jjjFPS_curX + usageSmooth*meterW + 0.5, jjjFPS_meterY2 - jjjFPS_borderWidth)
			graphics.draw_line(jjjFPS_curX, jjjFPS_meterY2 - jjjFPS_borderWidth, jjjFPS_curX + usageSmooth*meterW + 0.5, jjjFPS_meterY2 - jjjFPS_borderWidth)
		end
	end

end

function jjjFPS_setPanelMode(mode)
	jjjFPS_panelMode = mode
	jjjFPS_panelPage = 0
	jjjFPS_panelPageOld = -1
	jjjFPS_dispMark = ""
	jjjFPS_setMoveMode(0)
	if mode ~= "" then
		jjjFPS_refreshPanel()
	end
end

function jjjFPS_setPanelPage(page)
	jjjFPS_panelPage = page
	jjjFPS_dispMark = ""
	jjjFPS_setMoveMode(0)
end
function jjjFPS_panelPrevPage()
	jjjFPS_panelPage = jjjFPS_panelPage - 1
	jjjFPS_panelPageStep = -1
	jjjFPS_dispMark = ""
	jjjFPS_setMoveMode(0)
end
function jjjFPS_panelNextPage()
	jjjFPS_panelPage = jjjFPS_panelPage + 1
	jjjFPS_panelPageStep = 1
	jjjFPS_dispMark = ""
	jjjFPS_setMoveMode(0)
end

function jjjFPS_addProfileButton(profile)
	local text = profile
	if profile == jjjFPS_param("profNVR") then
		text = "  " .. text .. "  (default)"
	end
	if profile == jjjFPS_param("profVR") then
		text = "  " .. text .. "  (default VR)"
	end
	local cur = ( profile == jjjFPS_profile)
	jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, text, "m", cur, jjjFPS_panelMode == "main" or ( cur and jjjFPS_panelMode ~= "prof" ), 'jjjFPS_setProfile("' .. profile .. '")', "cyan")
end

function jjjFPS_addProfilesPanel()
	if jjjFPS_panelMode == "main" then
		jjjLib1.addPanelTextLine(jjjFPS_plId, jjjFPS_pnId, "PROFILE")
	else
		jjjLib1.addPanelTextLine(jjjFPS_plId, jjjFPS_pnId, "PROFILE (edit settings of current profile)")
	end
	jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)

	jjjFPS_addProfileButton("A")
	jjjFPS_addProfileButton("B")
	jjjFPS_addProfileButton("C")
	jjjFPS_addProfileButton("D")
	jjjLib1.addPanelHR(jjjFPS_plId, jjjFPS_pnId)
end

function jjjFPS_addInfoPanel()
	jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "AUTO", "s", jjjFPS_autoMode == "on", true, 'jjjFPS_setAutoModeManual("on")', "red")
	jjjLib1.addPanelSpace(jjjFPS_plId, jjjFPS_pnId)
	jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "MAX FPS", "s", jjjFPS_autoMode == "max", true, 'jjjFPS_setAutoModeManual("max")', "red")
	jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "MAX QUAL", "s", jjjFPS_autoMode == "min", true, 'jjjFPS_setAutoModeManual("min")', "red")
	jjjLib1.addPanelSpace(jjjFPS_plId, jjjFPS_pnId)
	jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "OFF", "s", jjjFPS_autoMode == "off", true, 'jjjFPS_setAutoModeManual("off")', "orange")
	jjjFPS_panelFpsDispId = jjjLib1.addPanelFpsDisplay(jjjFPS_plId, jjjFPS_pnId)
	jjjLib1.addPanelHR(jjjFPS_plId, jjjFPS_pnId)
end

function jjjFPS_wizardPrevPage()
	if jjjFPS_panelMode == "wiz" then
		jjjFPS_panelPage = jjjFPS_panelPage - 1
		if ( jjjFPS_panelPage == 5 or jjjFPS_panelPage == 6 ) and not jjjFPS_useAutoLod then
			jjjFPS_wizardPrevPage()
			return
		end
		if jjjFPS_panelPage == 6 and not jjjFPS_param("LDa") then
			jjjFPS_wizardPrevPage()
			return
		end
		if ( jjjFPS_panelPage == 7 or jjjFPS_panelPage == 8 ) and not jjjFPS_useAutoCLD then
			jjjFPS_wizardPrevPage()
			return
		end
		if jjjFPS_panelPage == 8 and not jjjFPS_param("CLDa") then
			jjjFPS_wizardPrevPage()
			return
		end
		if ( jjjFPS_panelPage == 9 or jjjFPS_panelPage == 10 ) and true then
			jjjFPS_wizardPrevPage()
			return
		end
--		if jjjFPS_panelPage == 10 and not jjjFPS_param("AAa") then
--			jjjFPS_wizardPrevPage()
--			return
--		end
		if ( jjjFPS_panelPage == 11 or jjjFPS_panelPage == 12 ) and not jjjFPS_useAutoShD then
			jjjFPS_wizardPrevPage()
			return
		end
		if jjjFPS_panelPage == 12 and not jjjFPS_param("ShDa") then
			jjjFPS_wizardPrevPage()
			return
		end

		if jjjFPS_panelPage < 0 then
			jjjFPS_panelPage = 0
			return
		end
	end
end
function jjjFPS_wizardNextPage()
	if jjjFPS_panelMode == "wiz" then
		jjjFPS_panelPage = jjjFPS_panelPage + 1
		if ( jjjFPS_panelPage == 5 or jjjFPS_panelPage == 6 ) and not jjjFPS_useAutoLod then
			jjjFPS_wizardNextPage()
			return
		end
		if jjjFPS_panelPage == 6 and not jjjFPS_param("LDa") then
			jjjFPS_wizardNextPage()
			return
		end
		if ( jjjFPS_panelPage == 7 or jjjFPS_panelPage == 8 ) and not jjjFPS_useAutoCLD then
			jjjFPS_wizardNextPage()
			return
		end
		if jjjFPS_panelPage == 8 and not jjjFPS_param("CLDa") then
			jjjFPS_wizardNextPage()
			return
		end
		if ( jjjFPS_panelPage == 9 or jjjFPS_panelPage == 10 ) and true then
			jjjFPS_wizardNextPage()
			return
		end
--		if jjjFPS_panelPage == 10 and not jjjFPS_param("AAa") then
--			jjjFPS_wizardNextPage()
--			return
--		end
		if ( jjjFPS_panelPage == 11 or jjjFPS_panelPage == 12 ) and not jjjFPS_useAutoShD then
			jjjFPS_wizardNextPage()
			return
		end
		if jjjFPS_panelPage == 12 and not jjjFPS_param("ShDa") then
			jjjFPS_wizardNextPage()
			return
		end

		if jjjFPS_panelPage > 13 then
			jjjFPS_panelPage = 13
			return
		end
	end
end

function jjjFPS_refreshPanel()
	jjjLib1.clearPanel(jjjFPS_plId, jjjFPS_pnId)
	jjjFPS_dispMark = ""

	if jjjFPS_panelPage ~= jjjFPS_panelPageOld then

		if jjjFPS_panelMode == "wiz" then
			if jjjFPS_panelPage == 1 then
				jjjFPS_setAutoMode("on")
			elseif jjjFPS_panelPage == 4 or jjjFPS_panelPage == 6 or jjjFPS_panelPage == 8 or jjjFPS_panelPage == 10 then
				jjjFPS_setAutoMode("max")
			elseif jjjFPS_panelPage == 0 or jjjFPS_panelPage == 3 or jjjFPS_panelPage == 5 or jjjFPS_panelPage == 7 or jjjFPS_panelPage == 9 or jjjFPS_panelPage == 11 then
				jjjFPS_setAutoMode("on")
			end
		end
	end

	jjjFPS_addInfoPanel()
	if jjjFPS_useProfiles then
		jjjFPS_addProfilesPanel()
	end

	if jjjFPS_panelMode == "main" then
		jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "SETTINGS")
		jjjLib1.addPanelClose(jjjFPS_plId, jjjFPS_pnId, "jjjFPS_closePanel()")
		jjjLib1.addPanelTextLine(jjjFPS_plId, jjjFPS_pnId, "FPS DISPLAY")
		jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "show what")
		jjjLib1.addPanelToggleButton(jjjFPS_plId, jjjFPS_pnId, "LABELS",  "s", "disL", jjjFPS_param("disN") or jjjFPS_param("disG") )
		jjjLib1.addPanelSpace(jjjFPS_plId, jjjFPS_pnId)
		jjjLib1.addPanelToggleButton(jjjFPS_plId, jjjFPS_pnId, "NUMBERS", "s", "disN", true )
		jjjLib1.addPanelSpace(jjjFPS_plId, jjjFPS_pnId)
		jjjLib1.addPanelToggleButton(jjjFPS_plId, jjjFPS_pnId, "GRAPHIC", "s", "disG", true )
		jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 2)
		jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "")
		if jjjFPS_useProcTimes then
			jjjLib1.addPanelToggleButton(jjjFPS_plId, jjjFPS_pnId, "CPU / GPU", "s", "disU", jjjFPS_param("disN") or jjjFPS_param("disG") )
			jjjLib1.addPanelSpace(jjjFPS_plId, jjjFPS_pnId)
		end
		jjjLib1.addPanelToggleButton(jjjFPS_plId, jjjFPS_pnId, "DETAILS", "s", "disD", jjjFPS_param("disN") or jjjFPS_param("disG") )
		jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 3)
		jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "show when")
		jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "ALWAYS", "s", "disM", "alw", true )
		jjjLib1.addPanelSpace(jjjFPS_plId, jjjFPS_pnId)
		jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "mouse over", "s", "disM", "hov", true )
		jjjLib1.addPanelSpace(jjjFPS_plId, jjjFPS_pnId)
		jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "bad FPS", "s", "disM", "bad", true )
		jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 3)
		jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "display position")
		if jjjFPS_moveMode > 0 then
			jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "MOVE / RESIZE", "m", true, true, "jjjFPS_setMoveMode(0); jjjFPS_refreshPanel()", "grey" )
		else
			jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "MOVE / RESIZE", "m", false, true, "jjjFPS_setMoveMode(1); jjjFPS_refreshPanel()", "grey" )
		end
		

		jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
		jjjLib1.addPanelHR(jjjFPS_plId, jjjFPS_pnId)
		jjjLib1.addPanelTextLine(jjjFPS_plId, jjjFPS_pnId, "CONFIGURATION")
		jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
		if not jjjFPS_useProfiles then
			jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "", "m", false, false, "")
		end
		jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "", "m", false, false, "")
		jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "WIZARD...", "m", false, true, 'jjjFPS_setPanelMode("wiz")', "grey")
		jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "ADVANCED...", "m", false, true, 'jjjFPS_setPanelMode("adv")', "grey")
		if jjjFPS_useProfiles then
			jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "PROFILES...", "m", false, true, 'jjjFPS_setPanelMode("prof")', "cyan")
		end
	end

	if jjjFPS_panelMode == "wiz" then
		if jjjFPS_panelPage == 0 then
			jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "CONFIGURATION WIZARD")
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "This plugin can make X-Plane run very smooth and gain a few FPS, but it needs some configuration to work as good as possible. Also, first you have to chose reasonable graphics settings that match your hardware.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "This wizard guides you through the basic configuration step by step. Please take the time to read and follow the informations and hints.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Now, set up a typical scenario for your flights. In case X-Plane reloads, please open the wizard again.||Then pause X-Plane (press 'P') and click 'NEXT PAGE'.")
		elseif jjjFPS_panelPage == 1 then
			jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "WIZARD - Plugin modes")
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "This plugin has 4 modes:||AUTO: Recommended! Continuously adopts the settings to the current situation.||MAX FPS: Set the lowest settings for highest FPS.||MAX QUAL: Set highest settings for best look (and low FPS).||OFF: Sets everything to X-Plane default, like the plugin was not installed.||You can select the mode with the red buttons on top of this window")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Please notice that the mode will be set to 'AUTO', 'MAX FPS' or 'MAX QUAL' depending on the value you are adjusting.||In AUTO mode the plugin does all changes slowly, to make them as less noticeable as possible. But during configuration, you will see the effect of every setting you adjust immediately.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Please click 'NEXT PAGE'.")
		elseif jjjFPS_panelPage == 2 then
			jjjFPS_dispMark = ""
			jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "WIZARD - Display type")
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "First select the type of display you are using.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1.5)
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "Monitor Vsync ON", "w", "wizDisp", "sync", true )
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "Monitor Vsync OFF", "w", "wizDisp", "nosync", true )
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 2.5)
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "Monitor freeSync", "w", "wizDisp", "free", true )
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "(or G-Sync)")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1.5)
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "VR Headset", "w", "wizDisp", "vr", true )
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1.5)
		elseif jjjFPS_panelPage == 3 then
			jjjFPS_dispMark = "fps"
			jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "WIZARD - FPS")
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Now select your desired FPS (frames per second).")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "The plugin will try to keep the selected FPS by reducing or increasing visual quality.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			if jjjFPS_param("wizDisp") == 'sync' then
				jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "When using Vsync, your desired FPS should be equal to the refresh rate of the monitor or 1/2, 1/3, 1/4 etc. of that refresh rate.")
				jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
				jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "If the refresh rate of the monitor is 60 for example, select 60 or 30. If it is 144, select 72, 48 or 36.")
				jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
				jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Please keep in mind, that more than 30 or 40 FPS need a very powerful computer and/or reasonable graphics settings. If you are not sure what to select, start with 30.")
			elseif jjjFPS_param("wizDisp") == 'nosync' then
				jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Please keep in mind, that more than 30 or 40 FPS need a very powerful computer and/or reasonable graphics settings. If you are not sure what to select, start with 30.")
			elseif jjjFPS_param("wizDisp") == 'free' then
				jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "When using freeSync (or G-Sync), your desired FPS should be somewhere between the minimum and maximum refresh rate of the monitor.")
				jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
				jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "If for example the native refresh rate of the monitor is 60 and it can go down to 40 with freeSync/G-Sync, 45 would be a good value.")
				jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
				jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Please keep in mind, that more than 30 or 40 FPS need a very powerful computer and/or reasonable graphics settings.")
			elseif jjjFPS_param("wizDisp") == 'vr' then
				jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Your desired FPS should be equal to the refresh rate of your VR headset.")
				jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
				jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "If you are using ASW (Oculus) or motion smoothing (WMR), select the effective refresh rate. (Usually 1/2 of the native refresh rate.)")
				jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
				jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Please keep in mind, that VR needs an extremely powerful computer and very reasonable graphics settings.")
			end
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 2)

			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "aimed FPS")
			jjjLib1.addPanelSliderLegend(jjjFPS_plId, jjjFPS_pnId, "choppy", "smooth")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "wizFps", true)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Buttons <<  <  >  >> change value by small/big steps.")
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Click on number resets to default.")
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Click on slider sets value directly.")
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Click on number resets to default.")
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Or use mouse wheel when mouse is over number or slider.")
		elseif jjjFPS_panelPage == 4 then
			jjjFPS_dispMark = ""
			jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "WIZARD - CPU and GPU usage")
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "At the top of the FPS display of this plugin you see the usage of your CPU and GPU. The upper number and bar show how much time your CPU (processor) needs for every frame.|The lower show how much time your GPU (graphics card) needs for every frame.||This is a very important information to find the best settings for your hardware. Please keep in mind though, that usage will vary a lot with different locations, weather, aircraft, add-ons etc.||The higher the number (time per frame in milli seconds), the more the CPU/GPU is loaded with work. If the bar gets red, it is too much work to keep the desired FPS you selected on the previous page.||In a perfect world, usage of CPU and GPU would be about the same. In that case, both CPU and GPU can unfold their full potential.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1.5)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "CPU has noticably higher load or red bars?|FPS are limited by your CPU. If the bar is very red all the time, you probably have to reduce some sliders in the X-Plane graphics settings. Try shadows, distance, world objects, vegetation.|When CPU bar is only a little red, 3jFPS hopefully can help you.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1.5)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "GPU has noticably higher load or red bars?|Your graphics card limits your FPS. If the bar is very red all the time, you probably have to reduce sliders in the X-Plane graphics settings. Try FSR, Antialiasing, SSAO, cloud quality.|When GPU bar is only a little red, 3jFPS hopefully can help you.")
		elseif jjjFPS_panelPage == 5 then
			jjjFPS_dispMark = "lod"
			jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "WIZARD - LOD (far objects)")
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "The LOD (level of detail) controls how far objects can be seen. It affects buildings, vegetation and other objects.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "LOD is the most effective setting to keep good FPS. It can reduce load of CPU and GPU a lot.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "It is recommended to set automatic LOD to 'ON'.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "automatic LOD")
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "ON", "xs", "LDa", true, true )
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "OFF", "xs", "LDa", false, true )
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
		elseif jjjFPS_panelPage == 6 then
			jjjFPS_dispMark = "lod"
			jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "WIZARD - LOD (far objects) min/max")
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Set minimum and maximum LOD.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "When the slider is at the very left, you will only see very near objects. (Good for FPS, bad for the look.)|When it is on the very right, you will see very far objects. (Looks great, but will produce bad FPS when scenery has many buildings or vegetation.)")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "+ Fly to a big city with lots of buildings, roads etc.|PLEASE NOTICE: When changing to another location, X-Plane reloads the scenery. If 'SAVE' button is green, click it to keep the settings you have done already before reload!||+ Move 'min' slider as much to the left as you can accept for good FPS.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
			jjjLib1.addPanelSliderLegend(jjjFPS_plId, jjjFPS_pnId, "FPS", "Quality")
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "LOD min")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "LDmn", jjjFPS_param("LDa"), 'jjjFPS_setAutoMode("max")')
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelSliderLegend(jjjFPS_plId, jjjFPS_pnId, "FPS", "Quality")
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "+ Move 'max' slider as much to the right as you want far objects to be seen when your FPS are good.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "LOD max")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "LDmx", jjjFPS_param("LDa"), 'jjjFPS_setAutoMode("min")')
		elseif jjjFPS_panelPage == 7 then
			jjjFPS_dispMark = "cld"
			jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "WIZARD - Clouds quality")
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Automatic CLOUDS QUALITY reduces the rendering of clouds, when FPS are too low.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Less clouds quality can reduce load of GPU, when weather is cloudy.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Because X-Plane's clouds cost a lot of performance, it is recommended to set automatic clouds quality to 'ON'.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "PLEASE NOTICE: If you are using 3rd-party cloud rendering plugins (like SkyMaxxPro) set automatic clouds quality to 'OFF'.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "auto clouds quality")
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "ON", "xs", "CLDa", true, true )
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "OFF", "xs", "CLDa", false, true )
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
		elseif jjjFPS_panelPage == 8 then
			jjjFPS_dispMark = "cld"
			jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "WIZARD - Clouds quality min/max")
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Set minimum and maximum CLOUDS QUALITY.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "When the slider is at the very left, clouds quality is very low, at the right, quality is very high.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "+ Turn off 'real weather'.||+ Then set different cloudy conditions in X-Plane's weather settings.||+ Move 'min' slider as much to the left as you can accept for good FPS.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
			jjjLib1.addPanelSliderLegend(jjjFPS_plId, jjjFPS_pnId, "FPS", "Quality")
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "clouds quality min")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "CLDmn", jjjFPS_param("CLDa"), 'jjjFPS_setAutoMode("max")')
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "+ Move 'max' slider as far to the right as you need to let clouds look ok.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "clouds quality max")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "CLDmx", jjjFPS_param("CLDa"), 'jjjFPS_setAutoMode("min")')
		elseif jjjFPS_panelPage == 9 then
			jjjFPS_dispMark = "cls"
			jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "WIZARD - Clouds puff size")
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Automatic CLOUDS PUFF SIZE reduces the size of cloud puffs, when FPS are too low.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Smaller clouds puffs reduce load of GPU, when weather is cloudy.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Because X-Plane's clouds cost a lot of performance, it is recommended to set automatic clouds puff size to 'ON'.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "PLEASE NOTICE: If you are using 3rd-party cloud rendering plugins (like SkyMaxxPro) set automatic clouds puff size to 'OFF'.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "auto clouds puff size")
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "ON", "xs", "CSa", true, true )
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "OFF", "xs", "CSa", false, true )
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
		elseif jjjFPS_panelPage == 10 then
			jjjFPS_dispMark = "cls"
			jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "WIZARD - Clouds puff size min/max")
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Set minimum and maximum CLOUDS PUFF SIZE.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "When the slider is at the very left, cloud puffs are small, at the right puffs are big.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "+ Turn off 'real weather', 'NOAA weather' or any other weather plugin if you have.||+ Then set 1 or 2 cloud layers to 'cumulus sct' in X-Plane's weather settings.||+ Move 'min' slider as much to the left as you can accept for good FPS.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
			jjjLib1.addPanelSliderLegend(jjjFPS_plId, jjjFPS_pnId, "FPS", "Quality")
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "clouds puff size min")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "CSmn", jjjFPS_param("CSa"), 'jjjFPS_setAutoMode("max")')
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Move 'max' slider as far to the right as you need to let clouds look ok. (Don't go too high, it will kill your performance!)")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "clouds puff size max")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "CSmx", jjjFPS_param("CSa"), 'jjjFPS_setAutoMode("min")')
		elseif jjjFPS_panelPage == 11 then
			jjjFPS_dispMark = "shd"
			jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "WIZARD - Shadow distance")
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Automatic shadow distance.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Shadows improve the look of the scenery a lot. Unfortunately, they can cause very high load for the CPU and the GPU.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "If you have scenery shadows enabled in the X-Plane settings, it is recommended to set automatic shadow distance to 'ON'.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "auto shadow distance")
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "ON", "xs", "ShDa", true, true )
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "OFF", "xs", "ShDa", false, true )
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
		elseif jjjFPS_panelPage == 12 then
			jjjFPS_dispMark = "shd"
			jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "WIZARD - Shadow distance min/max")
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Set minimum and maximum shadow distance.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "When the slider is at the very left, you will only see very near shadows. (Good for FPS, not so good for visual quality.)|When it is on the very right, you will see shadows far away. (Looks great, but very likely gives bad FPS.)")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "+ Move 'min' slider as much to the left as you can accept for good FPS.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
			jjjLib1.addPanelSliderLegend(jjjFPS_plId, jjjFPS_pnId, "FPS", "Quality")
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "Shadows min")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "ShDmn", jjjFPS_param("ShDa"), 'jjjFPS_setAutoMode("max")')
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelSliderLegend(jjjFPS_plId, jjjFPS_pnId, "FPS", "Quality")
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "+ Move 'max' slider as much to the right as you want far shadows to be seen when your FPS are good.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "Shadows max")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "ShDmx", jjjFPS_param("ShDa"), 'jjjFPS_setAutoMode("min")')
		elseif jjjFPS_panelPage == 13 then
			jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "WIZARD finished")
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Now you are done!")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Toggle plugin mode between 'MAX FPS' and 'MAX QUAL' to see how the look of the simulator changes and how this affects FPS.|When FPS at 'MAX FPS' are too high and at 'MAX QUAL' too low, you probably did it right.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Of course, performance depends very much on scenery, time of day, aircraft and weather. When you are in a situation where FPS still are too low, pause X-Plane and open the configuration wizard again to refine your settings. In that case, move 1 or more 'min' sliders to the left. (Observe current FPS while doing it, to find out which setting is best for your needs.)")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Now, don't forget to SAVE YOUR CHANGES and enjoy smooth flying!")
		end

		jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 2)
		if jjjFPS_panelPage > 0 then
			jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "< PREV STEP", "m", false, true, 'jjjFPS_wizardPrevPage()')
		else
			jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "", "m", false, false, "")
		end
		jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "", "m", false, false, "")
		jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "", "m", false, false, "")
		if jjjFPS_panelPage < 13 then
			jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "NEXT STEP >", "m", false, jjjFPS_panelPage < 13, 'jjjFPS_wizardNextPage()')
		elseif not jjjLib1.isAnyParamUnsaved(jjjFPS_plId) then
			jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "END WIZARD", "m", false, true, 'jjjFPS_setAutoMode("on"); jjjFPS_setPanelMode("main")')
		end

		jjjLib1.addPanelHR(jjjFPS_plId, jjjFPS_pnId)
		jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
		jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "", "m", false, false, "")
		jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "SAVE", "m", false, jjjLib1.isAnyParamUnsaved(jjjFPS_plId), 'jjjFPS_saveParams()', "green")
		if jjjLib1.isAnyParamUnsaved(jjjFPS_plId) then
			jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "CANCEL", "m", false, true, 'jjjFPS_loadParams(); jjjFPS_setAutoMode("on"); jjjFPS_setPanelMode("main")', "grey")
		else
			jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "BACK", "m", false, true, 'jjjFPS_setAutoMode("on"); jjjFPS_setPanelMode("main")', "grey")
		end
	end
	if jjjFPS_panelMode == "adv" then
		jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "FPS control", "m", jjjFPS_panelPage == 0, jjjFPS_panelPage ~= 0, 'jjjFPS_setPanelPage(0)')
		if jjjFPS_useAutoLod then
			jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "LOD / far objects", "m", jjjFPS_panelPage == 1, jjjFPS_panelPage ~= 1, 'jjjFPS_setPanelPage(1)')
		end
		if jjjFPS_useAutoCLD then
			jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "clouds quality", "m", jjjFPS_panelPage == 3, jjjFPS_panelPage ~= 3, 'jjjFPS_setPanelPage(3)')
		end
		if jjjFPS_useAutoFSR then
			jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "FSR", "m", jjjFPS_panelPage == 5, jjjFPS_panelPage ~= 5, 'jjjFPS_setPanelPage(5)')
		end
		if jjjFPS_useAutoAGL then
			jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "AGL visibility", "m", jjjFPS_panelPage == 7, jjjFPS_panelPage ~= 7, 'jjjFPS_setPanelPage(7)')
		end
		if jjjFPS_useAutoShK or jjjFPS_useAutoShD then
			jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "Shadows", "m", jjjFPS_panelPage == 4, jjjFPS_panelPage ~= 4, 'jjjFPS_setPanelPage(4)')
		end
		jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "Display", "m", jjjFPS_panelPage == 8, jjjFPS_panelPage ~= 8, 'jjjFPS_setPanelPage(8)')
		jjjLib1.addPanelHR(jjjFPS_plId, jjjFPS_pnId)

		if jjjFPS_panelPage == 0 then
			jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "ADVANCED - FPS/Quality")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)

			if jjjFPS_useProcTimes then
				jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "FPS-control mode")
				jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "BASIC", "m", "smartM", false, true )
				jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "SMART", "m", "smartM", true, true )
				jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 2)
			else
				jjjFPS_setParam("smartM", false)
			end

			if jjjFPS_param("smartM") then
				jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "In SMART mode, the plugin observes how much time the CPU and the GPU each need to render the frame.")
				jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
				jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Recommended, especially for displays with fixed frame rates (VR or vsync on).")
				jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
				jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Please notice: The calculations of CPU and GPU times may not be accurate in some cases and BASIC mode works better then.")
				jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)

				jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "FPS target")
				jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "Ftg", true)
				jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "CPU headroom %")
				jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "CPUhdrm", true)
				jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "GPU headroom %")
				jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "GPUhdrm", true)
			else
				jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "In BASIC mode, the plugin only observes the actual frame rate to decide if quality must be reduced or can be increased.")
				jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
				jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Option for displays with variable frame rates (monitor with freeSync/G-Sync or vsync off), if you think that SMART mode does not work for you.")
				jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)

				jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "FPS min")
				jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "Fmn", true)
				jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "FPS max")
				jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "Fmx", true)
			end


			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "Q decrease spd.")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "Qdec", true)
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "Q increase spd.")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "Qinc", true)
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "Stutter alarm (exp.)")
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "OFF", "s", "Stutt", "off", true )
			jjjLib1.addPanelSpace(jjjFPS_plId, jjjFPS_pnId)
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "LOW", "s", "Stutt", "lo", true )
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "MEDIUM", "s", "Stutt", "mid", true )
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "HIGH", "s", "Stutt", "hi", true )
		elseif jjjFPS_panelPage == 1 then
			jjjFPS_dispMark = "lod"
			jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "ADVANCED - LOD (far objects)")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "automatic LOD")
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "ON", "xs", "LDa", true, true )
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "OFF", "xs", "LDa", false, true )
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 2)
			jjjLib1.addPanelSliderLegend(jjjFPS_plId, jjjFPS_pnId, "FPS", "Quality")
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "LOD min")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "LDmn", jjjFPS_param("LDa"))
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "LOD max")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "LDmx", jjjFPS_param("LDa"))
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "LOD min Q")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "LDmnQ", jjjFPS_param("LDa"))
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "LOD max Q")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "LDmxQ", jjjFPS_param("LDa"))
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 2)
		elseif jjjFPS_panelPage == 2 then
			jjjFPS_dispMark = "vis"
		elseif jjjFPS_panelPage == 3 then
			jjjFPS_dispMark = "cld"
			jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "ADVANCED - cloud rendering quality")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "automatic cloud quality")
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "ON", "xs", "CLDa", true, true )
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "OFF", "xs", "CLDa", false, true )
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 2)
			jjjLib1.addPanelSliderLegend(jjjFPS_plId, jjjFPS_pnId, "FPS", "Quality")
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "cloud quality min")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "CLDmn", jjjFPS_param("CLDa"))
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "cloud quality max")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "CLDmx", jjjFPS_param("CLDa"))
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "cloud quality min Q")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "CLDmnQ", jjjFPS_param("CLDa"))
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "cloud quality max Q")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "CLDmxQ", jjjFPS_param("CLDa"))
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 2)
		elseif jjjFPS_panelPage == 4 then
			jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "ADVANCED - Shadows")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
			jjjFPS_dispMark = "ShD"
			if jjjFPS_useAutoShD then
				jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Control distance for shadows of buildings and trees:")
				jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1.0)
				jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "Auto shadow dist.")
				jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "ON", "xs", "ShDa", true, true )
				jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "OFF", "xs", "ShDa", false, true )
				jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 2.0)
				jjjLib1.addPanelSliderLegend(jjjFPS_plId, jjjFPS_pnId, "FPS", "Quality")
				jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "min shadow dist.")
				jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "ShDmn", jjjFPS_param("ShDa"))
				jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "max shadow dist.")
				jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "ShDmx", jjjFPS_param("ShDa"))
				jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "shadow dist. min Q")
				jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "ShDmnQ", jjjFPS_param("ShDa"))
				jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "shadow dist. max Q")
				jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "ShDmxQ", jjjFPS_param("ShDa"))
			end
			if jjjFPS_useAutoShD and jjjFPS_useAutoShK then
				jjjLib1.addPanelHR(jjjFPS_plId, jjjFPS_pnId)
			end
			if jjjFPS_useAutoShD then
				jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Always disable shadows when sun is at a very low angle:")
				jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1.0)
				jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "auto disable")
				jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "ON", "xs", "ShKa", true, true )
				jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "OFF", "xs", "ShKa", false, true )
				jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 2.0)
				jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "min. sun angle external view")
				jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "ShKextDg", jjjFPS_param("ShKa"))
				jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "min. sun angle internal view")
				jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "ShKintDg", jjjFPS_param("ShKa"))
			end
		elseif jjjFPS_panelPage == 5 then
			jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "ADVANCED - FSR")
			if jjjFPS_useAutoFSR then
				jjjFPS_dispMark = "fsr"
				jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
				jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "auto FSR")
				jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "ON", "xs", "FSRa", true, true )
				jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "OFF", "xs", "FSRa", false, true )
				jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 2)
				jjjLib1.addPanelSliderLegend(jjjFPS_plId, jjjFPS_pnId, "FPS", "Quality")
				jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "FSR mode min")
				jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "FSRmn", jjjFPS_param("FSRa"), 'jjjFPS_setAutoMode("max")')
				jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "FSR mode max")
				jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "FSRmx", jjjFPS_param("FSRa"), 'jjjFPS_setAutoMode("min")')
				jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "time")
				jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "FSRtmInc", jjjFPS_param("FSRa"))
				jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "FSR mode min Q")
				jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "FSRmnQ", jjjFPS_param("FSRa"))
				jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "FSR mode max Q")
				jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "FSRmxQ", jjjFPS_param("FSRa"))
			else
				jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
				jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Auto FSR could not be loaded.")
			end
		elseif jjjFPS_panelPage == 6 then
			jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "ADVANCED - Extras")
			jjjFPS_dispMark = "SSh"
			if jjjFPS_useAutoSSh then
				jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Scenery shadows")
			else
				jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Scenery shadows (disabled in X-Plane settings)")
			end
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "auto off/on")
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "ON", "xs", "SSa", true, jjjFPS_useAutoSSh )
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "OFF", "xs", "SSa", false, jjjFPS_useAutoSSh )
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 2)
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "secs bad before off")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "SStmOff", jjjFPS_useAutoSSh and jjjFPS_param("SSa"), 'jjjFPS_setAutoMode("max")')
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "secs good before on")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "SStmOn", jjjFPS_useAutoSSh and jjjFPS_param("SSa"), 'jjjFPS_setAutoMode("min")')
			jjjLib1.addPanelHR(jjjFPS_plId, jjjFPS_pnId)

			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 3.5)
		elseif jjjFPS_panelPage == 7 then
			jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "ADVANCED - AGL view distance")
			jjjFPS_dispMark = "AGL"
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Reduce visibility of objects (buildings and vegetation) when view is near to the ground (independent of current FPS).")
			jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Use free camera mode (press c) and move up and down between ground and 'max vis. height'.")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "auto off/on")
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "ON", "xs", "AGLa", true, true )
			jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "OFF", "xs", "AGLa", false, true )
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 2)
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "max vis. height (m)")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "AGLh", jjjFPS_param("AGLa"))
		elseif jjjFPS_panelPage == 8 then
			jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "ADVANCED - FPS meter")
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
			jjjLib1.addPanelSliderLegend(jjjFPS_plId, jjjFPS_pnId, "5", "80")
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "meter FPS min")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "MTfr", true)
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "meter FPS max")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "MTto", true)
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "meter bad FPS")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "MTbd", true)
			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "meter good FPS")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "MTgd", true)
			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)

--			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "meter width (pixels)")
--			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "MTwd", true)
--			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "meter height (pixels)")
--			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "MTht", true)

--			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "horizontal padding")
--			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "disX", true)
--			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "vertical padding")
--			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "disY", true)
--			jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)

			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "transparency")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "disA", true)

			jjjLib1.addPanelLabel(jjjFPS_plId, jjjFPS_pnId, "background")
			jjjLib1.addPanelNumericInput(jjjFPS_plId, jjjFPS_pnId, "disBgA", true)
		end

--		jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1)
--		jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "< PREV PAGE", "m", false, jjjFPS_panelPage > 0, 'jjjFPS_panelPrevPage()')
--		jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "", "m", false, false, "")
--		jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "", "m", false, false, "")
--		jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "NEXT PAGE >", "m", false, jjjFPS_panelPage < 7, 'jjjFPS_panelNextPage()')

		jjjLib1.addPanelHR(jjjFPS_plId, jjjFPS_pnId)
		jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
		jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "", "m", false, false, "")
		jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "SAVE", "m", false, jjjLib1.isAnyParamUnsaved(jjjFPS_plId), 'jjjFPS_saveParams(); jjjFPS_setPanelMode("main")', "green")
		if jjjLib1.isAnyParamUnsaved(jjjFPS_plId) then
			jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "CANCEL", "m", false, true, 'jjjFPS_loadParams(); jjjFPS_setPanelMode("main")', "grey")
		else
			jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "BACK", "m", false, true, 'jjjFPS_setPanelMode("main")', "grey")
		end
		jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "DEFAULT", "m", false, jjjLib1.isAnyParamNotDefault(jjjFPS_plId), 'jjjFPS_setParamsDefault()', "grey")
	end

	if jjjFPS_panelMode == "prof" then
		jjjLib1.setPanelName(jjjFPS_plId, jjjFPS_pnId, "PROFILES")
		jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)

		jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "Select one of the 4 profiles (A, B, C and D) to set up and test different configurations. All changes to the settings are saved to the currently active profile.")
		jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1.0)
		jjjLib1.addPanelText(jjjFPS_plId, jjjFPS_pnId, "You can set a default profile for non-VR and for VR, which gets automatically activated.")
		jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1.5)

		jjjLib1.addPanelTextLine(jjjFPS_plId, jjjFPS_pnId, "Default profile for non-VR (monitor):")
		jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
		jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "A", "s", "profNVR", "A", jjjFPS_param("profVR") ~= "A")
		jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "B", "s", "profNVR", "B", jjjFPS_param("profVR") ~= "B")
		jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "C", "s", "profNVR", "C", jjjFPS_param("profVR") ~= "C")
		jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "D", "s", "profNVR", "D", jjjFPS_param("profVR") ~= "D")
		jjjLib1.addPanelSpace(jjjFPS_plId, jjjFPS_pnId)
		jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "no default", "s", "profNVR", "-", true )

		jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 2.5)

		jjjLib1.addPanelTextLine(jjjFPS_plId, jjjFPS_pnId, "Default profile for VR: (ignore, if you don't use VR)")
		jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
		jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "A", "s", "profVR", "A", jjjFPS_param("profNVR") ~= "A")
		jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "B", "s", "profVR", "B", jjjFPS_param("profNVR") ~= "B")
		jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "C", "s", "profVR", "C", jjjFPS_param("profNVR") ~= "C")
		jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "D", "s", "profVR", "D", jjjFPS_param("profNVR") ~= "D")
		jjjLib1.addPanelSpace(jjjFPS_plId, jjjFPS_pnId)
		jjjLib1.addPanelParamButton(jjjFPS_plId, jjjFPS_pnId, "no default", "s", "profVR", "-", true )

		jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 1.5)

		jjjLib1.addPanelHR(jjjFPS_plId, jjjFPS_pnId)
		jjjLib1.addPanelBR(jjjFPS_plId, jjjFPS_pnId, 0.5)
		jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "", "m", false, false, "")
		jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "", "m", false, false, "")
		jjjLib1.addPanelButton(jjjFPS_plId, jjjFPS_pnId, "BACK", "m", false, true, 'jjjFPS_setPanelMode("main")', "grey")
	end

	jjjFPS_panelPageOld = jjjFPS_panelPage
end


function jjjFPS_onMouseClick()
	if MOUSE_STATUS == "down" then
		if MOUSE_X >= jjjFPS_x and MOUSE_X < jjjFPS_x + jjjFPS_width and MOUSE_Y >= jjjFPS_y + jjjFPS_lineHeight - jjjFPS_height and MOUSE_Y <= jjjFPS_y + jjjFPS_lineHeight then
			if jjjFPS_moveMode == 0 then
				jjjFPS_togglePanel()
				RESUME_MOUSE_CLICK = true
			elseif jjjFPS_moveMode == 1 then
				jjjFPS_setMoveMode(2)
				RESUME_MOUSE_CLICK = true
			elseif jjjFPS_moveMode == 2 then
				jjjFPS_setParam("disX", jjjFPS_dispX)
				jjjFPS_setParam("disY", jjjFPS_dispY)
				jjjFPS_setParam("MTwd", jjjFPS_meterWidth)
				jjjFPS_setParam("MTht", jjjFPS_lineHeight)
				jjjFPS_saveParams()
				jjjFPS_setMoveMode(0)
				jjjFPS_refreshPanel()
				RESUME_MOUSE_CLICK = true
			end
		end
	end
end
do_on_mouse_click("jjjFPS_onMouseClick()")

function jjjFPS_onMouseWheel()
	if jjjFPS_moveMode == 2 then
		jjjFPS_lineHeight = math.max(8, math.min(24, jjjFPS_lineHeight + MOUSE_WHEEL_CLICKS))
		jjjFPS_setMeterHeight()
		RESUME_MOUSE_WHEEL = true
	end
end
do_on_mouse_wheel("jjjFPS_onMouseWheel()")

function jjjFPS_closePanel()
	jjjFPS_setPanelMode("")
	jjjFPS_setMoveMode(0)
	jjjLib1.closePanel(jjjFPS_plId, jjjFPS_pnId)
end
function jjjFPS_togglePanel()
	if jjjFPS_panelMode ~= "" then
		if jjjFPS_panelMode == "main" then
			jjjFPS_setPanelMode("")
			jjjLib1.closePanel(jjjFPS_plId, jjjFPS_pnId)
		end
	else
		jjjFPS_setPanelMode("main")
		jjjLib1.openPanel(jjjFPS_plId, jjjFPS_pnId)
	end
	jjjFPS_setMoveMode(0)
end

function jjjFPS_doScreenShot()
	if jjjFPS_screenShot < 1 then
		jjjFPS_screenShot = 1
	end
end

-- calculate position
jjjFPS_setPosition()
if jjjFPS_dispX > 0 then
	jjjFPS_x = jjjFPS_dispX
else
	jjjFPS_x = SCREEN_WIDTH + jjjFPS_dispX - jjjFPS_width
end
if jjjFPS_dispY > 0 then
	jjjFPS_y = jjjFPS_dispY + jjjFPS_height - jjjFPS_param("MTht")*0.75
else
	jjjFPS_y = SCREEN_HIGHT - jjjFPS_param("MTht")*0.75 + jjjFPS_dispY
end

jjjFPS_setAutoShDOnOff()

if jjjFPS_useProcTimes ~= true then
	jjjFPS_setParam("smartM", false)
end

function jjjFPS_alertNewInstall()
	jjjLib1.alert(jjjFPS_plId, "PLEASE NOTICE", 'You have the plugin ' .. jjjFPS_pluginName .. ' installed as a new FlyWithLua script.||PLEASE, read this carefully:||This plugin uses some unofficial control parameters (so called "private DataRefs"/"Art controls"). This can cause bugs or unwanted effects, especially after updates of X-Plane.||If something does not work, it is NOT a bug of X-Plane or FlyWithLua. It is something that -unfortunately- simply can happen, when a plugin "hacks" internal settings.||You can always totally uninstall the plugin by removing the file "' .. jjjFPS_pluginName .. '.lua" from your "resources/plugins/FlyWithLua/scripts" folder.', "blue", "Understood", "jjjFPS_alertInfoWizard()")
end
function jjjFPS_alertInfoWizard()
	jjjLib1.alert(jjjFPS_plId, "ONE MORE THING...", 'To make the plugin work as good as possible, it has to be configured for your hardware, settings and personal preferences.||To do so, please open the plugin settings by clicking on the FPS-display on the top left of the screen.|You can also open the settings via the X-Plane menu: Plugins -> FlyWithLua -> FlyWithLua Macros -> ' .. jjjFPS_pluginName .. ': open/close settings.||At the bottom left of the settings window click on the "WIZARD..." button and follow the instructions carefully.', "blue", "Yes, I will do so!", "")
	jjjFPS_setParam("xpver", XPLANE_VERSION)
	jjjLib1.saveParams(jjjFPS_plId, "global")
end
function jjjFPS_alertUpdateXP()
	jjjLib1.alert(jjjFPS_plId, "PLEASE NOTICE", 'You have the plugin ' .. jjjFPS_pluginName .. ' running as a FlyWithLua script.||PLEASE, read this carefully:||It seems that your X-Plane installation has been updated. This plugin uses some unofficial control parameters (so called "private DataRefs"/"Art controls"). This can cause bugs or unwanted effects, especially after X-Plane was updated.||If something does not work, it is NOT a bug of X-Plane or FlyWithLua. It is something that -unfortunately- simply can happen, when a plugin "hacks" internal settings.||You can always totally uninstall the plugin by removing the ' .. jjjFPS_pluginName .. '.lua file from your "resources/plugins/FlyWithLua/scripts" folder.', "blue", "Understood", "jjjFPS_alertUpdateXPinfo()")
end
function jjjFPS_alertUpdateXPinfo()
	jjjLib1.alert(jjjFPS_plId, "ONE MORE THING...", 'The X-Plane update may have changed a lot of things. Please check, if your configuration of this plugin is still useful.||You should turn 3jFPS12 off at first (click "OFF" button at the top right of the settings window), to see what the X-Plane update did to visuals and performance.||Then, set the plugin to "AUTO" and observe change of visuals and performance. It is recommended to go through the "wizard" mode again to refine your configuration of 3jFPS12.||To do so, please open the plugin settings by clicking on the FPS-display (default on the top left of the screen).|You can also open the settings via the X-Plane menu: Plugins -> FlyWithLua -> FlyWithLua Macros -> ' .. jjjFPS_pluginName .. ': open/close settings.||At the bottom left of the settings window click on the "WIZARD..." button and follow the instructions carefully.', "blue", "Yes, I will do so!", "")
	jjjFPS_setParam("xpver", XPLANE_VERSION)
	jjjLib1.saveParams(jjjFPS_plId, "global")
end

-- check xp version
if jjjFPS_param("xpver") ~= XPLANE_VERSION then
	if jjjFPS_param("xpver") == "" then
		-- new install
		jjjFPS_alertNewInstall()
	else 
		-- xp update
		jjjFPS_alertUpdateXP()
	end
end

add_macro("3jFPS12: open/close settings", "jjjFPS_togglePanel()")

create_command("FlyWithLua/3jFPS12/01_open_settings", "3jFPS12: open/close settings", "jjjFPS_togglePanel()", "", "")
create_command("FlyWithLua/3jFPS12/02_mode_auto", "3jFPS12: mode auto", 'jjjFPS_setAutoModeManual("on")', "", "")
create_command("FlyWithLua/3jFPS12/03_mode_off", "3jFPS12: off", 'jjjFPS_setAutoModeManual("off")', "", "")
create_command("FlyWithLua/3jFPS12/04_mode_toggle", "3jFPS12: toggle on/off", 'jjjFPS_toggleAutoMode()', "", "")
create_command("FlyWithLua/3jFPS12/05_mode_max", "3jFPS12: max FPS (lowest quality)", 'jjjFPS_setAutoModeManual("max")', "", "")
create_command("FlyWithLua/3jFPS12/06_mode_min", "3jFPS12: max quality (lowest FPS)", 'jjjFPS_setAutoModeManual("min")', "", "")
create_command("FlyWithLua/3jFPS12/07_profile_A", "3jFPS12: Profile A", 'jjjFPS_setProfile("A")', "", "")
create_command("FlyWithLua/3jFPS12/08_profile_B", "3jFPS12: Profile B", 'jjjFPS_setProfile("B")', "", "")
create_command("FlyWithLua/3jFPS12/09_profile_C", "3jFPS12: Profile C", 'jjjFPS_setProfile("C")', "", "")
create_command("FlyWithLua/3jFPS12/10_profile_D", "3jFPS12: Profile D", 'jjjFPS_setProfile("D")', "", "")

create_command("FlyWithLua/3jFPS12/11_screen_shot", "3jFPS12: screen shot (max quality)", "jjjFPS_doScreenShot()", "", "")

do_on_exit("jjjFPS_exit()")

do_every_frame("jjjFPS_main()")
do_every_draw("jjjFPS_draw()")
do_sometimes("jjjFPS_check()")

jjjFPS_refreshPanel()

dbg = ""
function jjjFPS_drawDbg()
	draw_string(20, 20, "DEBUG: " ..  dbg)
end
-- do_every_draw("jjjFPS_drawDbg()")
