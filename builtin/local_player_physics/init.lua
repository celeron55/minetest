-- minetest/builtin/local_player_physics/init.lua

local scriptpath = core.get_builtin_path()..DIR_DELIM
local commonpath = scriptpath.."common"..DIR_DELIM
dofile(commonpath.."vector.lua")

function core.set_local_player_physics(def)
	core.log("core.set_local_player_physics")
	core.registered_local_player_physics_apply_control = def and def.apply_control
	core.registered_local_player_physics_move = def and def.move
end
