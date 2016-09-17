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

class Client;

class PlayerPhysicsScripting :
		virtual public ScriptApiBase,
		public ScriptApiSecurity
{
public:
	PlayerPhysicsScripting(Client *client);
	
	void loadScript(const std::string &script_content);

private:
	void InitializeModApi(lua_State *L, int top);
};

#endif /* SCRIPTING_PLAYER_PHYSICS_H_ */
