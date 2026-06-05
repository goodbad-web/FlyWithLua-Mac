-- Honeycomb Bravo dial button script.
--
-- This script will set the commands for the dial buttons
-- The left button to select between Alt, VS, HDG, CRS and IAS
-- and the right button to tune the values up and down. 
--
-- Author: Axel Seedig December 2022
--

-- First we read the status of batterie 1. If batterie 1 is off, Nothing happens
--dataref("battery_switch1", "sim/cockpit2/electrical/battery_on")
local battery_switch1 = get("sim/cockpit2/electrical/battery_on")

-- Here we define the left dial and set it to Alt, since when we start dark,
-- we don't know what setting is actually set.
left_dial = create_dataref_table("FlyWithLua/BravoDial/left_dial", "Data")

left_dial[0] = "Alt"


-- Functions to select the different registers (Alt, VS, HDG, CRS, IAS)
function cmdLeftDial_Alt()
		if battery_switch1 == 1 then
			left_dial = "Alt"
			XPLMSpeakString ( "Altitude mode." )
		end
end

function cmdLeftDial_VS()
		if battery_switch1 == 1 then
 			left_dial = "VS"
			XPLMSpeakString ( "Vertical speed mode." )
		end
end

function cmdLeftDial_HDG()
		if battery_switch1 == 1 then
 			left_dial = "HDG"
			XPLMSpeakString ( "Heading mode." )
		end
end

function cmdLeftDial_CRS()
		if battery_switch1 == 1 then
 			left_dial = "CRS"
			XPLMSpeakString ( "Course mode." )
		end
end

function cmdLeftDial_IAS()
		if battery_switch1 == 1 then
 			left_dial = "IAS"
			XPLMSpeakString ( "Airspeed mode." )
		end
end

-- If the right button is turned up (depending how you configure it)
-- we check what value the variable left_dial has and then increase the
-- value of the selected register
  
function cmdRightDial_up()
		if battery_switch1 == 1 then
			if left_dial == "Alt" then
				command_once('sim/autopilot/altitude_up')
			elseif left_dial == "VS" then
				command_once('sim/autopilot/vertical_speed_up')
			elseif left_dial == "HDG" then
				command_once('sim/autopilot/heading_up')
			elseif left_dial == "CRS" then
				command_once('sim/radios/obs1_up')
			elseif left_dial == "IAS" then
				command_once('sim/autopilot/airspeed_up')
			end
		end
end			

-- If the right button is turned down (depending how you configure it)
-- we check what value the variable left_dial has and then decrease the
-- value of the selected register

function cmdRightDial_dn()
		if battery_switch1 == 1 then
			if left_dial == "Alt" then
				command_once('sim/autopilot/altitude_down')
			elseif left_dial == "VS" then
				command_once('sim/autopilot/vertical_speed_down')
			elseif left_dial == "HDG" then
				command_once('sim/autopilot/heading_down')
			elseif left_dial == "CRS" then
				command_once('sim/radios/obs1_down')
			elseif left_dial == "IAS" then
				command_once('sim/autopilot/airspeed_down')
			end
		end
end			

-- Here we create the commands you can then assign to the dial buttons.
-- Left button/dial functions
create_command("FlyWithLua/BravoDial/Set_Alt", "Left Dial set to Alt", "cmdLeftDial_Alt()", "", "")
create_command("FlyWithLua/BravoDial/Set_VS", "Left Dial set to VS", "cmdLeftDial_VS()", "", "")
create_command("FlyWithLua/BravoDial/Set_HDG", "Left Dial set to HDG", "cmdLeftDial_HDG()", "", "")
create_command("FlyWithLua/BravoDial/Set_CRS", "Left Dial set to CRS", "cmdLeftDial_CRS()", "", "")
create_command("FlyWithLua/BravoDial/Set_IAS", "Left Dial set to IAS", "cmdLeftDial_IAS()", "", "")

-- Right button/dial functions
create_command("FlyWithLua/BravoDial/Right_Dial_UP", "Right Dial turn right", "cmdRightDial_up()", "", "")
create_command("FlyWithLua/BravoDial/Right_Dial_DN", "Right Dial turn left", "cmdRightDial_dn()", "", "")

-- That's it.
