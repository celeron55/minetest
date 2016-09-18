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

#include "lua_api/l_player_physics.h"
#include "lua_api/l_internal.h"
#include "log.h"
#include "client.h"

int ModApiPlayerPhysics::l_send_local_player_physics_message(lua_State *L)
{
	//infostream<<"ModApiPlayerPhysics::l_send_local_player_physics_message"<<std::endl;

	size_t message_len = 0;
	const char *message_c = luaL_checklstring(L, 1, &message_len);
	std::string message(message_c, message_len);

	Client *client = getClient(L);
	client->sendPhysicsScriptMessage(message);

	return 0;
}

void ModApiPlayerPhysics::Initialize(lua_State *L, int top)
{
	API_FCT(send_local_player_physics_message);
}
