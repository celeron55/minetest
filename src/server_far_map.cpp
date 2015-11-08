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

#include "server_far_map.h"
#include "constants.h"

/*
	ServerFarBlock
*/

ServerFarBlock::ServerFarBlock(v3s16 p):
	p(p)
{
	// Block's effective origin in FarNodes based on block_div
	fnp0 = v3s16(
			p.X * FMP_SCALE * SERVER_FARBLOCK_DIV,
			p.Y * FMP_SCALE * SERVER_FARBLOCK_DIV,
			p.Z * FMP_SCALE * SERVER_FARBLOCK_DIV);
	
	v3s16 area_size_fns = v3s16(
			FMP_SCALE * SERVER_FARBLOCK_DIV,
			FMP_SCALE * SERVER_FARBLOCK_DIV,
			FMP_SCALE * SERVER_FARBLOCK_DIV);

	v3s16 fnp1 = fnp0 + area_size_fns - v3s16(1,1,1); // Inclusive

	content_area = VoxelArea(fnp0, fnp1);

	size_t content_size_n = content_area.getVolume();

	node_ids.resize(content_size_n, CONTENT_IGNORE);
	lights.resize(content_size_n);
}

ServerFarBlock::~ServerFarBlock()
{
}

/*
	ServerFarMap
*/

ServerFarMap::ServerFarMap()
{
}

ServerFarMap::~ServerFarMap()
{
}

ServerFarBlock* ServerFarMap::getBlock(v3s16 p)
{
	std::map<v3s16, ServerFarBlock*>::iterator i = m_blocks.find(p);
	if(i != m_blocks.end())
		return i->second;
	return NULL;
}

ServerFarBlock* ServerFarMap::getOrCreateBlock(v3s16 p)
{
	std::map<v3s16, ServerFarBlock*>::iterator i = m_blocks.find(p);
	if(i != m_blocks.end())
		return i->second;
	ServerFarBlock *b = new ServerFarBlock(p);
	m_blocks[p] = b;
	return b;
}


