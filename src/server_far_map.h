/*
Minetest
Copyright (C) 2015 celeron55, Perttu Ahola <celeron55@gmail.com>

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
#ifndef __SERVER_FAR_MAP_H__
#define __SERVER_FAR_MAP_H__

#include "irrlichttypes.h"
#include "voxel.h" // VoxelArea
#include <map>

// In how many pieces MapBlocks have been divided in each dimension to create
// FarNodes
#define SERVER_FARBLOCK_DIV 4

struct ServerFarBlock
{
	// Position in FarBlocks
	v3s16 p;
	// Block's effective origin in FarNodes
	v3s16 fnp0;
	// Contained area in FarNodes. Is used to index node_ids and lights.
	VoxelArea content_area;

	std::vector<u16> node_ids;
	std::vector<u8> lights;

	ServerFarBlock(v3s16 p);
	~ServerFarBlock();
};

class ServerFarMap
{
public:
	ServerFarMap();
	~ServerFarMap();

	ServerFarBlock* getBlock(v3s16 p);
	ServerFarBlock* getOrCreateBlock(v3s16 p);

private:
	std::map<v3s16, ServerFarBlock*> m_blocks;
};

#endif
