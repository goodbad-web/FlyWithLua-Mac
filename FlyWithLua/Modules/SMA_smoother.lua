-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
-- -- Lua module "SMA_smoother.lua" v1.0  -- --
-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
module(..., package.seeall);

function create_SMA(axis_number, samples)
	axis_number = tonumber(axis_number)
	samples = tonumber(samples) or 10
	if axis_number == nil or axis_number < 0 or axis_number % 1 ~= 0 then
		logMsg("SMA_smoother: axis_number must be a non-negative integer")
		return false
	end
	if samples < 1 or samples % 1 ~= 0 then
		logMsg("SMA_smoother: samples must be a positive integer")
		return false
	end

	axis_number = string.format("%d", axis_number)
	samples = string.format("%d", samples)
	
	-- create the code
	local code = 'dataref("real_axis_' .. axis_number
	code = code .. '", "sim/joystick/joystick_axis_values", "readonly", ' .. axis_number .. ')\n'
	code = code .. "local values_axis_" .. axis_number .. " = { }\n"
	code = code .. "for i = 1, " .. samples .. " do\n"
	code = code .. "    values_axis_" .. axis_number .. "[i] = real_axis_" .. axis_number .. "\n"
	code = code .. "end\n"
	code = code .. "axis_" .. axis_number .. " = real_axis_" .. axis_number .. "\n\n"
	code = code .. "function calculate_axis_" .. axis_number .. "()\n"
    code = code .. "    axis_" .. axis_number .. " = real_axis_" .. axis_number .. "\n"
    code = code .. "    for i = 2, " .. samples .. " do\n"
    code = code .. "        axis_" .. axis_number .. " = axis_" .. axis_number .. " + values_axis_" .. axis_number .. "[i]\n"
    code = code .. "        values_axis_" .. axis_number .. "[i-1] = values_axis_" .. axis_number .. "[i]\n"
	code = code .. "    end\n"
	code = code .. "    values_axis_" .. axis_number .. "[" .. samples .. "] = real_axis_" .. axis_number .. "\n"
    code = code .. "    axis_" .. axis_number .. " = axis_" .. axis_number .. " / " .. samples .. "\n"
    code = code .. "end\n\n"
    code = code .. 'do_every_frame("calculate_axis_' .. axis_number .. '()")\n'
	
	-- Compile and execute the generated code without aborting the entire Lua
	-- runtime when an invalid input or host API error is encountered.
	local chunk, compile_error = loadstring(code)
	if not chunk then
		logMsg("SMA_smoother: unable to compile generated code: " .. tostring(compile_error))
		return false
	end
	local ok, runtime_error = pcall(chunk)
	if not ok then
		logMsg("SMA_smoother: generated code failed: " .. tostring(runtime_error))
		return false
	end
	return true
end
