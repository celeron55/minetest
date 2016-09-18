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

#ifndef SCRIPTING_PLAYER_PHYSICS_H_
#define SCRIPTING_PLAYER_PHYSICS_H_

#include "cpp_api/s_base.h"
#include "cpp_api/s_security.h"
#include "irr_v3d.h"

class Client;
class Player;

// TODO: Maybe rename to LocalPlayerPhysicsScripting
class PlayerPhysicsScripting :
		virtual public ScriptApiBase,
		public ScriptApiSecurity
{
public:
	PlayerPhysicsScripting(Client *client);
	
	void loadScriptContent(const std::string &script_content);

	// TODO: Maybe move these into ScriptApiLocalPlayerPhysics
	void apply_control(float dtime, Player *player);
	void move(float dtime, Player *player);
	bool camera_up_vector(v3f *result);
	void on_message(const std::string &message);

private:
	void control_call(const char *func_name, float dtime, Player *player);

	void InitializeModApi(lua_State *L, int top);
};

#endif /* SCRIPTING_PLAYER_PHYSICS_H_ */
