local mod_base_path = core.get_modpath(core.get_current_modname())

local function activate_airplane_mode(player)
	print("Calling player:set_physics_script")
	local fpath = mod_base_path.."/local_player_physics.lua"
	local f, errmsg = io.open(fpath, 'rb')
	if f then
		local content = f:read "*a"
		f:close()
		player:set_physics_script(content)
	else
		minetest.log("error", "failed to open "..fpath)
	end

	player:hud_add({
		hud_elem_type = "text",
		position = {x=0.2, y=0.9},
		name = "engine_text",
		number = 0x888888,
		text = "Engine: OFF (press space to toggle)",
	})
end

minetest.register_on_joinplayer(function(player)
	-- TODO: Activate by using an item or something maybe
	minetest.after(2, function()
		activate_airplane_mode(player)
	end)
end)

