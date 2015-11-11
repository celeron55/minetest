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
#ifndef __FAR_MAP_SERVER_H__
#define __FAR_MAP_SERVER_H__

#include "far_map_common.h"
#include "irrlichttypes.h"
#include "voxel.h" // VoxelArea
#include <map>

class INodeDefManager;

// In how many pieces MapBlocks have been divided in each dimension to create
// FarNodes
#define SERVER_FB_MB_DIV 4
#define SERVER_FN_SIZE (MAP_BLOCKSIZE / SERVER_FB_MB_DIV)

struct ServerFarBlock
{
	// Position in FarBlocks
	v3s16 p;
	// Contained area in FarNodes. Is used to index node_ids and lights.
	VoxelArea content_area;

	std::vector<FarNode> content;

	u32 modification_counter;

	// TODO: Differentiate loaded and loaded-but-ungenerated blocks
	BlockAreaBitmap<bool> loaded_mapblocks;

	ServerFarBlock(v3s16 p);
	~ServerFarBlock();
};

std::string analyze_far_block(ServerFarBlock *b);

struct ServerFarMapPiece
{
	VoxelArea content_area;

	std::vector<FarNode> content;

	void generateFrom(VoxelManipulator &vm, INodeDefManager *ndef);
};

class ServerFarMap
{
public:
	ServerFarMap();
	~ServerFarMap();

	ServerFarBlock* getBlock(v3s16 p);
	ServerFarBlock* getOrCreateBlock(v3s16 p);

	// Piece must consist of full MapBlocks
	void updateFrom(const ServerFarMapPiece &piece);

private:
	std::map<v3s16, ServerFarBlock*> m_blocks;
};

#endif
