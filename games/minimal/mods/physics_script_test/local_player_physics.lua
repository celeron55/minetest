local function vector_rotate_x(v, a)
	return vector.new(
		v.x,
		v.y * math.cos(a) - v.z * math.sin(a),
		v.y * math.sin(a) + v.z * math.cos(a)
	)
end

local function vector_rotate_y(v, a)
	return vector.new(
		v.x * math.cos(a) - v.z * math.sin(a),
		v.y,
		v.x * math.sin(a) + v.z * math.cos(a)
	)
end

local function vector_rotate_z(v, a)
	return vector.new(
		v.x * math.cos(a) - v.y * math.sin(a),
		v.x * math.sin(a) + v.y * math.cos(a),
		v.z
	)
end

local function vector_dot(v, w)
	return v.x * w.x + v.y * w.y + v.z * w.z
end

local function accelerate_towards(current, towards, acceleration, dtime)
	local rel = towards - current
	local max_change = math.abs(acceleration) * dtime

	if math.abs(rel) < math.abs(max_change) then
		return towards
	else
		if towards > current then
			return current + max_change
		else
			return current - max_change
		end
	end
end

local throttle_on = false
local previous_control = {}
local previous_player_params = {}
local current_roll = 0.0

core.set_local_player_physics({
	apply_control = function(dtime, control, player_params)
		--print("apply_control: dtime="..dump(dtime)..", control="..dump(control)..
		--		", player_params="..dump(player_params))

		if control.jump and not previous_control.jump then
			throttle_on = not throttle_on
			core.send_local_player_physics_message({
				name = "throttle_status",
				status = throttle_on,
			})
		end

		-- Left/right
		local d = 1.0
		if control.left then
			current_roll = current_roll + 1.0 * dtime * d
			player_params.yaw = player_params.yaw + 0.2 * dtime * d
		elseif control.right then
			current_roll = current_roll - 1.0 * dtime * d
			player_params.yaw = player_params.yaw - 0.2 * dtime * d
		end

		-- Wrap
		if current_roll < -math.pi - 0.1 then
			current_roll = current_roll + math.pi * 2
		elseif current_roll > -math.pi + 0.1 then
			current_roll = current_roll - math.pi * 2
		end

		-- Down/up
		local pitch_factor = math.cos(current_roll)
		local yaw_factor = math.sin(current_roll) * 2
		local d = 0.5
		if control.down then
			player_params.pitch = player_params.pitch + pitch_factor * -1.0 * dtime * d
			player_params.yaw = player_params.yaw + yaw_factor * 0.5 * dtime * d
		elseif control.up then
			player_params.pitch = player_params.pitch + pitch_factor * 1.0 * dtime * d
			player_params.yaw = player_params.yaw + yaw_factor * -0.5 * dtime * d
		end

		-- Wrap
		if player_params.pitch < -math.pi - 0.1 then
			player_params.pitch = player_params.pitch + math.pi * 2
		elseif player_params.pitch > -math.pi + 0.1 then
			player_params.pitch = player_params.pitch + math.pi * 2
		end

		-- Wrap
		if player_params.yaw < -math.pi - 0.1 then
			player_params.yaw = player_params.yaw + math.pi * 2
		elseif player_params.yaw > -math.pi + 0.1 then
			player_params.yaw = player_params.yaw + math.pi * 2
		end

		local v = vector.new(0, 0, 1)
		v = vector_rotate_z(v, current_roll)
		v = vector_rotate_x(v, player_params.pitch)
		v = vector_rotate_y(v, player_params.yaw)
		local forward_vector = v
		local forward_velocity = vector_dot(player_params.velocity, forward_vector)

		local max_forward_velocity = 200
		--local acceleration = 40 -- Less than gravitation (if 9.81)
		local acceleration = 66
		--local acceleration = 120 -- A lot of power

		if throttle_on then
			forward_velocity = accelerate_towards(
					forward_velocity, max_forward_velocity, acceleration, dtime)
		else
			forward_velocity = accelerate_towards(
					forward_velocity, 0, 5, dtime)
		end

		local v = vector.new(1, 0, 0)
		v = vector_rotate_z(v, current_roll)
		v = vector_rotate_x(v, player_params.pitch)
		v = vector_rotate_y(v, player_params.yaw)
		local rightward_vector = v
		local rightward_velocity = vector_dot(player_params.velocity, rightward_vector)

		local v = vector.new(0, 1, 0)
		v = vector_rotate_z(v, current_roll)
		v = vector_rotate_x(v, player_params.pitch)
		v = vector_rotate_y(v, player_params.yaw)
		local upward_vector = v
		local upward_velocity = vector_dot(player_params.velocity, upward_vector)

		-- Pull the plane's pitch, yaw and roll towards velocity vector to
		-- straighten it up

		--print("rightward_velocity: "..rightward_velocity)
		local rightward_rotate = dtime * rightward_velocity * 0.02
		local d = 0.05
		if rightward_rotate >  math.pi * d then rightward_rotate =  math.pi * d end
		if rightward_rotate < -math.pi * d then rightward_rotate = -math.pi * d end
		--print("rightward_rotate: "..rightward_rotate)
		local pitch_factor = math.cos(current_roll + math.pi / 2) * 2
		local yaw_factor = math.sin(current_roll - math.pi / 2)
		--player_params.pitch = player_params.pitch - pitch_factor * rightward_rotate
		player_params.yaw = player_params.yaw + yaw_factor * rightward_rotate

		--print("upward_velocity: "..upward_velocity)
		local upward_rotate = dtime * upward_velocity * -0.02
		local d = 0.05
		if upward_rotate >  math.pi * d then upward_rotate =  math.pi * d end
		if upward_rotate < -math.pi * d then upward_rotate = -math.pi * d end
		--print("upward_rotate: "..upward_rotate)
		local pitch_factor = math.cos(current_roll)
		local yaw_factor = math.sin(current_roll) * 2
		--player_params.pitch = player_params.pitch - pitch_factor * upward_rotate
		player_params.yaw = player_params.yaw + yaw_factor * upward_rotate

		-- Restrict sideways movement
		local a = 0.2 + forward_velocity * 0.2
		rightward_velocity = accelerate_towards(rightward_velocity, 0, a, dtime)

		-- Provide lift
		local a = 1.0 + forward_velocity * 1.0
		-- Stay level
		upward_velocity = accelerate_towards(upward_velocity, 0, a, dtime)
		-- Rise a bit
		--upward_velocity = upward_velocity + forward_velocity * dtime * 0.5

		-- Weird roll-based auto-yaw
		local yaw_factor = math.sin(current_roll)
		local d = yaw_factor * forward_velocity * dtime * 0.002
		player_params.yaw = player_params.yaw + d

		local v = vector.new(rightward_velocity, upward_velocity, forward_velocity)
		v = vector_rotate_z(v, current_roll)
		v = vector_rotate_x(v, player_params.pitch)
		v = vector_rotate_y(v, player_params.yaw)

		player_params.velocity = v

		-- Gravity
		--player_params.velocity.y = player_params.velocity.y - dtime * 10 * 9.81
		player_params.velocity.y = player_params.velocity.y - dtime * 10 * 3.3

		-- Apply velocity to position
		local dp = vector.multiply(player_params.velocity, dtime)
		player_params.position = vector.add(player_params.position, dp)

		previous_control = control
		previous_player_params = player_params
		return player_params
	end,
	move = function(dtime, control, player_params)
		if dtime < 0.00001 then
			--print("move: Something went probably wrong (dtime is very small)")
			-- Crash somehow
			local a = nil
			a.a = nil
		end
		--print("move: dtime="..dump(dtime))
	end,
    camera_up_vector = function()
		local v = vector.new(0, 1, 0)
		v = vector_rotate_z(v, current_roll)
		v = vector_rotate_x(v, previous_player_params.pitch)
		v = vector_rotate_y(v, previous_player_params.yaw)
		return v
    end,
})
