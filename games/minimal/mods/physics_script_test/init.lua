local airplane_players = {}
local mod_base_path = core.get_modpath(core.get_current_modname())

local function read_file_content(fpath)
	local f, errmsg = io.open(fpath, 'rb')
	if f then
		local content = f:read "*a"
		f:close()
		return content
	else
		minetest.log("error", "failed to open "..fpath)
	end
end

local function activate_airplane_mode(player)
	print("Calling player:set_physics_script")

	local state = {
		player = player,
	}

	airplane_players[player:get_player_name()] = state

	state.hud_id_engine_text = player:hud_add({
		hud_elem_type = "text",
		position = {x=0.05, y=0.85},
        alignment = {x=1, y=0},
		number = 0xffffff,
		text = "Engine: OFF (press space to toggle)",
	})

	state.hud_id_speed_text = player:hud_add({
		hud_elem_type = "text",
		position = {x=0.05, y=0.9},
        alignment = {x=1, y=0},
		number = 0xffffff,
		text = "Speed: 0",
	})

	local script = read_file_content(mod_base_path.."/local_player_physics.lua")

	player:set_physics_script(script, {
		on_message = function(serialized_message)
			local message = minetest.deserialize(serialized_message)
			if message.name == "throttle_status" then
				if message.status then
					player:hud_change(state.hud_id_engine_text, "text", "Engine: ON")
				else
					player:hud_change(state.hud_id_engine_text, "text", "Engine: OFF")
				end
			else
				print("player:set_physics_script on_message unknown: "..dump(message))
			end
		end,
	})
end

local dt_accu = 0.0
minetest.register_globalstep(function(dtime)
	dt_accu = dt_accu + dtime
	if dt_accu < 1.0 then
		return
	end
	dt_accu = 0.0
	for name, state in pairs(airplane_players) do
		state.player:hud_change(state.hud_id_speed_text, "text", "Speed: "..
				(math.floor(vector.length(state.player:get_player_velocity())*10)/10).." m/s")
		--state.player:send_physics_script_message("test message")
	end
end)

minetest.register_on_joinplayer(function(player)
	-- TODO: Activate by using an item or something maybe
	minetest.after(2, function()
		activate_airplane_mode(player)
	end)
end)
