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
#include "nodedef.h"
#include "util/numeric.h"

#define PP(x) "("<<(x).X<<","<<(x).Y<<","<<(x).Z<<")"

/*
	ServerFarBlock
*/

ServerFarBlock::ServerFarBlock(v3s16 p):
	p(p)
{
	// Block's effective origin in FarNodes based on block_div
	v3s16 fnp0 = v3s16(
			p.X * FMP_SCALE * SERVER_FB_MB_DIV,
			p.Y * FMP_SCALE * SERVER_FB_MB_DIV,
			p.Z * FMP_SCALE * SERVER_FB_MB_DIV);
	
	v3s16 area_size_fns = v3s16(
			FMP_SCALE * SERVER_FB_MB_DIV,
			FMP_SCALE * SERVER_FB_MB_DIV,
			FMP_SCALE * SERVER_FB_MB_DIV);

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
	ServerFarMapPiece
*/

void ServerFarMapPiece::generateFrom(VoxelManipulator &vm, INodeDefManager *ndef)
{
	v3s16 fnp0 = getContainerPos(vm.m_area.MinEdge, SERVER_FN_SIZE);
	v3s16 fnp1 = getContainerPos(vm.m_area.MaxEdge, SERVER_FN_SIZE);
	content_area = VoxelArea(fnp0, fnp1);

	size_t content_size_n = content_area.getVolume();

	node_ids.resize(content_size_n, CONTENT_IGNORE);
	lights.resize(content_size_n);

	v3s16 fnp;
	for (fnp.Y=content_area.MinEdge.Y; fnp.Y<=content_area.MaxEdge.Y; fnp.Y++)
	for (fnp.X=content_area.MinEdge.X; fnp.X<=content_area.MaxEdge.X; fnp.X++)
	for (fnp.Z=content_area.MinEdge.Z; fnp.Z<=content_area.MaxEdge.Z; fnp.Z++) {
		size_t i = content_area.index(fnp);

		u16 node_id = 0;
		u8 light = 0;

		// Node at center of FarNode (horizontally)
		v3s16 np(
			fnp.X * SERVER_FN_SIZE + SERVER_FN_SIZE/2,
			fnp.Y * SERVER_FN_SIZE + SERVER_FN_SIZE/2,
			fnp.Z * SERVER_FN_SIZE + SERVER_FN_SIZE/2);
		for(s32 i=0; i<SERVER_FN_SIZE+1; i++){
			MapNode n = vm.getNodeNoEx(np);
			if(n.getContent() == CONTENT_IGNORE)
				break;
			const ContentFeatures &f = ndef->get(n);
			if (!f.name.empty() && f.param_type == CPT_LIGHT) {
				light = n.param1;
				if(node_id == 0){
					node_id = n.getContent();
				}
				break;
			} else {
				// TODO: Get light of a nearby node; something that defines
				//       how brightly this division should be rendered
				// (day | (night << 8))
				light = (15) | (0<<4);
				node_id = n.getContent();
			}
			np.Y++;
		}

		node_ids[i] = node_id;
		lights[i] = light;
	}
}

/*
	ServerFarMap
*/

ServerFarMap::ServerFarMap()
{
}

ServerFarMap::~ServerFarMap()
{
	for (std::map<v3s16, ServerFarBlock*>::iterator i = m_blocks.begin();
			i != m_blocks.end(); i++) {
		delete i->second;
	}
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

void ServerFarMap::updateFrom(const ServerFarMapPiece &piece)
{
	v3s16 fnp000 = piece.content_area.MinEdge;
	v3s16 fnp001 = piece.content_area.MaxEdge;

	// Convert to ServerFarBlock positions (this can cover extra area)
	VoxelArea fb_area(
		getContainerPos(fnp000, FMP_SCALE * SERVER_FB_MB_DIV),
		getContainerPos(fnp001, FMP_SCALE * SERVER_FB_MB_DIV)
	);

	v3s16 fbp;
	for (fbp.Y=fb_area.MinEdge.Y; fbp.Y<=fb_area.MaxEdge.Y; fbp.Y++)
	for (fbp.X=fb_area.MinEdge.X; fbp.X<=fb_area.MaxEdge.X; fbp.X++)
	for (fbp.Z=fb_area.MinEdge.Z; fbp.Z<=fb_area.MaxEdge.Z; fbp.Z++) {
		dstream<<"ServerFarMap::updateFrom: ServerFarBlock "
				<<PP(fbp)<<std::endl;

		ServerFarBlock *b = getOrCreateBlock(fbp);

		v3s16 fp00 = piece.content_area.MinEdge;
		v3s16 fp01 = piece.content_area.MaxEdge;
		v3s16 fp1;
		for (fp1.Y=fp00.Y; fp1.Y<=fp01.Y; fp1.Y++)
		for (fp1.X=fp00.X; fp1.X<=fp01.X; fp1.X++)
		for (fp1.Z=fp00.Z; fp1.Z<=fp01.Z; fp1.Z++) {
			// The source area does not necessarily contain all positions that
			// the matching blocks contain
			if(!piece.content_area.contains(fp1))
				continue;
			size_t source_i = piece.content_area.index(fp1);
			size_t dst_i = b->content_area.index(fp1);
			b->node_ids[dst_i] = piece.node_ids[source_i];
			b->lights[dst_i] = piece.lights[source_i];
		}
	}
}

