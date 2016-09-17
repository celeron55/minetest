/*
Minetest
Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "scripting_player_physics.h"
#include "cpp_api/s_internal.h"
#include "lua_api/l_player_physics.h"
#include "server.h"
#include "log.h"
#include "settings.h"
#include "client.h"

extern "C" {
#include "lualib.h"
}

PlayerPhysicsScripting::PlayerPhysicsScripting(Client *client)
{
	setClient(client);

	// setEnv(env) is called by ScriptApiEnv::initializeEnvironment()
	// once the environment has been created

	SCRIPTAPI_PRECHECKHEADER

	// Always initialize security
	initializeSecurity();

	lua_getglobal(L, "core");
	int top = lua_gettop(L);

	// TODO: Remove?
	//lua_newtable(L);
	//lua_setfield(L, -2, "object_refs");

	// TODO: Remove?
	//lua_newtable(L);
	//lua_setfield(L, -2, "luaentities");

	// TODO: Remove?
	// Initialize our lua_api modules
	InitializeModApi(L, top);
	lua_pop(L, 1);

	// Push builtin initialization type
	lua_pushstring(L, "game");
	lua_setglobal(L, "INIT");

	infostream<<"SCRIPTAPI: Initialized game modules"<<std::endl;
}

void PlayerPhysicsScripting::loadScript(const std::string &script_content)
{
	verbosestream<<"PlayerPhysicsScripting::loadScript: \""<<script_content
			<<"\""<<std::endl;

	lua_State *L = getStack();

	int error_handler = PUSH_ERROR_HANDLER(L);

	bool ok = ScriptApiSecurity::safeLoadContent(
			L, "player_physics_script", script_content);
	ok = ok && !lua_pcall(L, 0, 0, error_handler);
	if (!ok) {
		std::string error_msg = lua_tostring(L, -1);
		lua_pop(L, 2); // Pop error message and error handler
		throw ModError("Failed to load and run player physics script:\n"+error_msg);
	}
	lua_pop(L, 1); // Pop error handler
}

void PlayerPhysicsScripting::InitializeModApi(lua_State *L, int top)
{
	// Initialize mod api modules
	ModApiPlayerPhysics::Initialize(L, top);

	// Register reference classes (userdata)
	// (none)
}

