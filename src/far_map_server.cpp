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

#include "far_map_server.h"
#include "constants.h"
#include "nodedef.h"
#include "profiler.h"
#include "util/numeric.h"
#include "util/string.h"

#define PP(x) "("<<(x).X<<","<<(x).Y<<","<<(x).Z<<")"

/*
	ServerFarBlock
*/

ServerFarBlock::ServerFarBlock(v3s16 p):
	p(p),
	modification_counter(0)
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

	content.resize(content_size_n, CONTENT_IGNORE);

	VoxelArea blocks_area(p * FMP_SCALE, (p+1) * FMP_SCALE - v3s16(1,1,1));
	loaded_mapblocks.reset(blocks_area);
}

ServerFarBlock::~ServerFarBlock()
{
}

std::string analyze_far_block(ServerFarBlock *b)
{
	if (b == NULL)
		return "NULL";
	std::string s = "["+analyze_far_block(
			b->p, b->content, b->content_area, b->content_area);
	s += ", modification_counter="+itos(b->modification_counter);
	s += ", loaded_mapblocks="+itos(b->loaded_mapblocks.count(true))+"/"+
			itos(b->loaded_mapblocks.size());
	return s+"]";
}

/*
	ServerFarMapPiece
*/

void ServerFarMapPiece::generateFrom(VoxelManipulator &vm, INodeDefManager *ndef)
{
	TimeTaker tt(NULL, NULL, PRECISION_MICRO);

	v3s16 fnp0 = getContainerPos(vm.m_area.MinEdge, SERVER_FN_SIZE);
	v3s16 fnp1 = getContainerPos(vm.m_area.MaxEdge, SERVER_FN_SIZE);
	content_area = VoxelArea(fnp0, fnp1);

	size_t content_size_n = content_area.getVolume();

	content.resize(content_size_n, CONTENT_IGNORE);

	v3s16 fnp;
	for (fnp.Y=content_area.MinEdge.Y; fnp.Y<=content_area.MaxEdge.Y; fnp.Y++)
	for (fnp.X=content_area.MinEdge.X; fnp.X<=content_area.MaxEdge.X; fnp.X++)
	for (fnp.Z=content_area.MinEdge.Z; fnp.Z<=content_area.MaxEdge.Z; fnp.Z++) {
		size_t i = content_area.index(fnp);

		u16 node_id = CONTENT_IGNORE;
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
				if(node_id == CONTENT_IGNORE){
					node_id = n.getContent();
				}
				break;
			} else {
				// TODO: Get light of a nearby node; something that defines
				//       how brightly this division should be rendered
				// (day | (night << 4))
				light = (15) | (0<<4);
				node_id = n.getContent();
			}
			np.Y++;
		}

		content[i].id = node_id;
		content[i].light = light;
	}

	g_profiler->avg("Far: gen from MapBlock (avg us)", tt.stop(true));
}

void ServerFarMapPiece::generateEmpty(const VoxelArea &area_nodes)
{
	v3s16 fnp0 = getContainerPos(area_nodes.MinEdge, SERVER_FN_SIZE);
	v3s16 fnp1 = getContainerPos(area_nodes.MaxEdge, SERVER_FN_SIZE);
	content_area = VoxelArea(fnp0, fnp1);

	size_t content_size_n = content_area.getVolume();

	content.resize(content_size_n, CONTENT_IGNORE);

	v3s16 fnp;
	for (fnp.Y=content_area.MinEdge.Y; fnp.Y<=content_area.MaxEdge.Y; fnp.Y++)
	for (fnp.X=content_area.MinEdge.X; fnp.X<=content_area.MaxEdge.X; fnp.X++)
	for (fnp.Z=content_area.MinEdge.Z; fnp.Z<=content_area.MaxEdge.Z; fnp.Z++) {
		size_t i = content_area.index(fnp);
		content[i].id = CONTENT_IGNORE;
		content[i].light = 0; // (day | (night << 4))
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
			b->content[dst_i] = piece.content[source_i];

			v3s16 mbp = getContainerPos(fp1, SERVER_FB_MB_DIV);
			b->loaded_mapblocks.set(mbp, true);
		}

		b->modification_counter++;

		/*// Call analyze_far_block before starting the line to keep lines
		// mostly intact when multiple threads are printing
		std::string s = analyze_far_block(b);
		dstream<<"ServerFarMap: Updated block: "<<s<<std::endl;*/
	}
}

