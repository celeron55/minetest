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
#include "lua_api/l_util.h"
#include "lua_api/l_player_physics.h"
#include "common/c_converter.h"
#include "common/c_content.h"
#include "server.h"
#include "log.h"
#include "settings.h"
#include "client.h"
#include "filesys.h"

extern "C" {
#include "lualib.h"
}

PlayerPhysicsScripting::PlayerPhysicsScripting(Client *client)
{
	setClient(client);

	SCRIPTAPI_PRECHECKHEADER

	// Always initialize security
	initializeSecurity();

	lua_getglobal(L, "core");
	int top = lua_gettop(L);

	// Initialize our lua_api modules
	InitializeModApi(L, top);
	lua_pop(L, 1);

	// Push builtin initialization type
	lua_pushstring(L, "local_player_physics");
	lua_setglobal(L, "INIT");

	// Run builtin stuff
	std::string script = porting::path_share + DIR_DELIM "builtin" + DIR_DELIM "init.lua";
	loadMod(script, BUILTIN_MOD_NAME);
}

void PlayerPhysicsScripting::loadScriptContent(const std::string &script_content)
{
	verbosestream<<"PlayerPhysicsScripting::loadScriptContent: \""<<script_content
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

void PlayerPhysicsScripting::apply_control(float dtime, Player *player)
{
	control_call("registered_local_player_physics_apply_control", dtime, player);
}

void PlayerPhysicsScripting::move(float dtime, Player *player)
{
	control_call("registered_local_player_physics_move", dtime, player);
}

static void push_player_params(lua_State *L, const Player &player)
{
	lua_newtable(L);
	push_v3f(L, player.getPosition());
	lua_setfield(L, -2, "position");
	push_v3f(L, player.getSpeed());
	lua_setfield(L, -2, "velocity");
	lua_pushnumber(L, player.getPitch() / 180.0 * M_PI);
	lua_setfield(L, -2, "pitch");
	lua_pushnumber(L, player.getYaw() / 180.0 * M_PI);
	lua_setfield(L, -2, "yaw");
}

static void read_player_params(lua_State *L, int table, Player *player)
{
	lua_getfield(L, table, "position");
	if(!lua_isnil(L, -1))
		player->setPosition(read_v3f(L, -1));
	lua_pop(L, 1);

	lua_getfield(L, table, "velocity");
	if(!lua_isnil(L, -1))
		player->setSpeed(read_v3f(L, -1));
	lua_pop(L, 1);

	lua_getfield(L, table, "pitch");
	if(!lua_isnil(L, -1))
		player->setPitch(lua_tonumber(L, -1) * 180.0 / M_PI);
	lua_pop(L, 1);

	lua_getfield(L, table, "yaw");
	if(!lua_isnil(L, -1))
		player->setYaw(lua_tonumber(L, -1) * 180.0 / M_PI);
	lua_pop(L, 1);
}

void PlayerPhysicsScripting::control_call(
		const char *func_name, float dtime, Player *player)
{
	lua_State *L = getStack();

	int error_handler = PUSH_ERROR_HANDLER(L);

	lua_getglobal(L, "core");
	lua_getfield(L, -1, func_name);
	if (lua_isnil(L, -1)){
		lua_pop(L, 2); // player params, error handler, core
		return;
	}
	lua_remove(L, -2); // Remove core

	if (lua_type(L, -1) != LUA_TFUNCTION)
		return;

	lua_pushnumber(L, dtime);
	push_player_control_full(L, player->control);
	push_player_params(L, *player);

	PCALL_RES(lua_pcall(L, 3, 1, error_handler));

	if(!lua_isnil(L, -1))
		read_player_params(L, -1, player);

	lua_pop(L, 1); // Pop player params (return value)
	lua_pop(L, 1); // Pop error handler
}

bool PlayerPhysicsScripting::camera_up_vector(v3f *result)
{
	lua_State *L = getStack();

	int error_handler = PUSH_ERROR_HANDLER(L);

	lua_getglobal(L, "core");
	lua_getfield(L, -1, "registered_local_player_physics_camera_up_vector");
	if (lua_isnil(L, -1)){
		lua_pop(L, 2); // player params, error handler, core
		return false;
	}
	lua_remove(L, -2); // Remove core

	if (lua_type(L, -1) != LUA_TFUNCTION)
		return false;

	PCALL_RES(lua_pcall(L, 0, 1, error_handler));

	if (lua_isnil(L, -1)){
		lua_pop(L, 1); // Pop error handler
		return false;
	}

	*result = read_v3f(L, -1);

	lua_pop(L, 1); // Pop return value
	lua_pop(L, 1); // Pop error handler

	return true;
}

void PlayerPhysicsScripting::on_message(const std::string &message)
{
	lua_State *L = getStack();

	int error_handler = PUSH_ERROR_HANDLER(L);

	lua_getglobal(L, "core");
	lua_getfield(L, -1, "registered_local_player_physics_on_message");
	if (lua_isnil(L, -1)){
		lua_pop(L, 2); // player params, error handler, core
		return;
	}
	lua_remove(L, -2); // Remove core

	if (lua_type(L, -1) != LUA_TFUNCTION)
		return;

	lua_pushlstring(L, message.c_str(), message.size());

	PCALL_RES(lua_pcall(L, 1, 0, error_handler));

	if (lua_isnil(L, -1)){
		lua_pop(L, 1); // Pop error handler
		return;
	}

	lua_pop(L, 1); // Pop error handler
}

void PlayerPhysicsScripting::InitializeModApi(lua_State *L, int top)
{
	// NOTE: Do not add a lot of stuff in here because this script environment
	//       is run synchronously to physics and can't do heavy processing or
	//       synchronous i/o.
	//       For that purpose, add a messaging interface that can transfer
	//       events and data between this and some asynchronous client-side Lua
	//       environment.

	// Initialize API modules
	ModApiUtil::Initialize(L, top);
	ModApiPlayerPhysics::Initialize(L, top);

	// Register reference classes (userdata)
	// (none)
}

