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

#include "cpp_api/s_player_physics.h"
#include "cpp_api/s_internal.h"
#include "log.h"

void ScriptApiPlayerPhysics::player_physics_on_message(const std::string &message)
{
	SCRIPTAPI_PRECHECKHEADER

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

	lua_pop(L, 1); // Pop error handler
}
