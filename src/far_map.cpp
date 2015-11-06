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

#include "far_map.h"
#include "constants.h"
#include "settings.h"
#include "mapblock_mesh.h" // MeshCollector
#include "mesh.h" // translateMesh
#include "util/numeric.h" // getContainerPos
#include "client.h" // For use of Client's IGameDef interface
#include "profiler.h"
#include "util/atlas.h"
#include "irrlichttypes_extrabloated.h"
#include <IMaterialRenderer.h>

#define PP(x) "("<<(x).X<<","<<(x).Y<<","<<(x).Z<<")"

/*
	FarBlock
*/

FarBlock::FarBlock(v3s16 p):
	p(p),
	mesh(NULL)
{
	mapblock_meshes.resize(FMP_SCALE * FMP_SCALE * FMP_SCALE);
}

FarBlock::~FarBlock()
{
	if(mesh)
		mesh->drop();

	for (size_t i=0; i<mapblock_meshes.size(); i++) {
		if(mapblock_meshes[i])
			mapblock_meshes[i]->drop();
	}
}

void FarBlock::resize(v3s16 new_block_div)
{
	block_div = new_block_div;

	// Block's effective origin in FarNodes based on block_div
	dp00 = v3s16(
		p.X * FMP_SCALE * block_div.X,
		p.Y * FMP_SCALE * block_div.Y,
		p.Z * FMP_SCALE * block_div.Z
	);

	// Effective size of content in FarNodes based on block_div in global
	// coordinates
	effective_size = v3s16(
			FMP_SCALE * block_div.X,
			FMP_SCALE * block_div.Y,
			FMP_SCALE * block_div.Z);

	v3s16 edp0 = dp00;
	v3s16 edp1 = edp0 + effective_size - v3s16(1,1,1); // Inclusive

	effective_area = VoxelArea(edp0, edp1);

	// One extra FarNode layer per edge as required by mesh generation
	content_size = effective_size + v3s16(2,2,2);

	// Min and max edge of content (one extra FarNode layer per edge as
	// required by mesh generation)
	v3s16 dp0 = dp00 - v3s16(1,1,1);
	v3s16 dp1 = dp0 + content_size - v3s16(1,1,1); // Inclusive

	content_area = VoxelArea(dp0, dp1);

	size_t content_size_n = content_area.getVolume();

	content.resize(content_size_n);
}

void FarBlock::updateCameraOffset(v3s16 camera_offset)
{
	if (!mesh) return;

	if (camera_offset != current_camera_offset) {
		translateMesh(mesh, intToFloat(current_camera_offset-camera_offset, BS));
		for (size_t i=0; i<mapblock_meshes.size(); i++) {
			if (!mapblock_meshes[i])
				continue;
			translateMesh(mapblock_meshes[i],
					intToFloat(current_camera_offset-camera_offset, BS));
		}
		current_camera_offset = camera_offset;
	}
}

void FarBlock::resetCameraOffset(v3s16 camera_offset)
{
	current_camera_offset = v3s16(0, 0, 0);
	updateCameraOffset(camera_offset);
}

/*
	FarSector
*/

FarSector::FarSector(v2s16 p):
	p(p)
{
	v2s16 bp0(p.X * FMP_SCALE, p.Y * FMP_SCALE);
	v2s16 bp1 = bp0 + v2s16(FMP_SCALE, FMP_SCALE); // Exclusive

	v2s16 bp;
	for (bp.X=bp0.X; bp.X<bp1.X; bp.X++)
	for (bp.Y=bp0.Y; bp.Y<bp1.Y; bp.Y++) {
		mapsectors_covered.push_back(bp);
	}
}

FarSector::~FarSector()
{
	for (std::map<s16, FarBlock*>::iterator i = blocks.begin();
			i != blocks.end(); i++) {
		delete i->second;
	}
}

FarBlock* FarSector::getOrCreateBlock(s16 y)
{
	std::map<s16, FarBlock*>::iterator i = blocks.find(y);
	if(i != blocks.end())
		return i->second;
	v3s16 p3d(p.X, y, p.Y);
	FarBlock *b = new FarBlock(p3d);
	blocks[y] = b;
	return b;
}

/*
	FarBlockMeshGenerateTask
*/

FarBlockMeshGenerateTask::FarBlockMeshGenerateTask(
		FarMap *far_map, const FarBlock &source_block):
	far_map(far_map),
	block(source_block)
{
	// We don't want to deal with whatever mesh the block was currently holding;
	// set things to NULL so that FarBlock's destructor will not drop the
	// original contents and we can store our results in it.
	block.mesh = NULL;
	for (size_t i=0; i<block.mapblock_meshes.size(); i++) {
		block.mapblock_meshes[i] = NULL;
	}
}

FarBlockMeshGenerateTask::~FarBlockMeshGenerateTask()
{
}

static void add_face(MeshCollector *collector,
		const FarNode &n, const v3s16 &p, s16 dir_x, s16 dir_y, s16 dir_z,
		const std::vector<FarNode> &data,
		const VoxelArea &data_area,
		const v3s16 &block_div,
		const FarMap *far_map)
{
	ITextureSource *tsrc = far_map->client->getTextureSource();
	//IShaderSource *ssrc = far_map->client->getShaderSource();
	INodeDefManager *ndef = far_map->client->getNodeDefManager();

	static const u16 indices[] = {0,1,2,2,3,0};

	v3s16 dir(dir_x, dir_y, dir_z);
	v3s16 vertex_dirs[4];
	getNodeVertexDirs(dir, vertex_dirs);

	// This is the size of one FarNode (without BS being factored in)
	v3f scale(
		MAP_BLOCKSIZE / block_div.X,
		MAP_BLOCKSIZE / block_div.Y,
		MAP_BLOCKSIZE / block_div.Z
	);

	v3f pf(
		(scale.X * p.X + scale.X/2 - 0.5) * BS,
		(scale.Y * p.Y + scale.Y/2 - 0.5) * BS,
		(scale.Z * p.Z + scale.Z/2 - 0.5) * BS
	);

	v3f vertex_pos[4];
	for(u16 i=0; i<4; i++)
	{
		vertex_pos[i] = v3f(
			BS / 2 * vertex_dirs[i].X,
			BS / 2 * vertex_dirs[i].Y,
			BS / 2 * vertex_dirs[i].Z
		);
		vertex_pos[i].X *= scale.X;
		vertex_pos[i].Y *= scale.Y;
		vertex_pos[i].Z *= scale.Z;
		vertex_pos[i] += pf;
	}

	v3f normal(dir.X, dir.Y, dir.Z);

	u8 alpha = 255;

	// As produced by getFaceLight (day | (night << 8))
	u16 light_encoded = (255) | (255<<8);
	// Light produced by the node itself
	u8 light_source = 0;

	// Some kind of texture coordinate aspect stretch thing
	f32 abs_scale = 1.0;
	if     (scale.X < 0.999 || scale.X > 1.001) abs_scale = scale.X;
	else if(scale.Y < 0.999 || scale.Y > 1.001) abs_scale = scale.Y;
	else if(scale.Z < 0.999 || scale.Z > 1.001) abs_scale = scale.Z;

	// Texture coordinates
	float x0 = 0.0;
	float y0 = 0.0;
	float w = 1.0 / 8; // WTF
	float h = 1.0;

	// Get texture from texture atlas
	u8 face = dir.Y == 1 ? 0 : dir.Y == -1 ? 1 : 2;
	const atlas::AtlasSegmentCache *asc = far_map->atlas.getNode(n.id, face);
	if (!asc) {
		const ContentFeatures &f = ndef->get(n.id);
		assert(f.tiledef[0].name == "" && "All nodes that have a texture should"
				" be found in FarMap's atlas");
		return; // Makes no faces
	}
	assert(asc->texture_pp);
	assert(*asc->texture_pp);

	// Copy texture coordinates
	x0 = asc->coord0.X;
	y0 = asc->coord0.Y;
	w = asc->coord1.X - x0;
	h = asc->coord1.Y - y0;

	/*const ContentFeatures &f = ndef->get(n.id);
	const std::string *tile_name = NULL;
	if(dir.Y == 1) // Top
		tile_name = &f.tiledef[0].name;
	else if(dir.Y == -1) // Bottom
		tile_name = &f.tiledef[1].name;
	else // Side
		tile_name = &f.tiledef[2].name;*/

	//std::string tile_name = "unknown_node.png";

	// TODO
	TileSpec t;

	/*t.texture_id = tsrc->getTextureId(*tile_name);
	t.texture = tsrc->getTexture(t.texture_id);
	t.texture_id = 0;*/

	/*t.texture_id = tsrc->getTextureId(tile_name);
	t.texture = tsrc->getTexture(t.texture_id);
	t.texture_id = 0;*/

	t.texture_id = 0; // atlas::AtlasRegistry doesn't provide this
	t.texture = *asc->texture_pp;

	/*t.texture = far_map->client->getSceneManager()->getVideoDriver()
			->getTexture("atlas_FarMap_texture_1");*/
	/*t.texture = far_map->client->getSceneManager()->getVideoDriver()
			->getTexture("unknown_node.png");*/
	t.alpha = alpha;
	t.material_type = TILE_MATERIAL_BASIC;
	//t.material_flags &= ~MATERIAL_FLAG_BACKFACE_CULLING;

	if (far_map->config_enable_shaders) {
		t.shader_id = far_map->farblock_shader_id;
		bool normalmap_present = false;
		t.flags_texture = tsrc->getShaderFlagsTexture(normalmap_present);
	}

	// Finally create the vertices that we have been preparing for
	video::S3DVertex vertices[4];
	vertices[0] = video::S3DVertex(vertex_pos[0], normal,
			MapBlock_LightColor(alpha, light_encoded, light_source),
			core::vector2d<f32>(x0+w*abs_scale, y0+h));
	vertices[1] = video::S3DVertex(vertex_pos[1], normal,
			MapBlock_LightColor(alpha, light_encoded, light_source),
			core::vector2d<f32>(x0, y0+h));
	vertices[2] = video::S3DVertex(vertex_pos[2], normal,
			MapBlock_LightColor(alpha, light_encoded, light_source),
			core::vector2d<f32>(x0, y0));
	vertices[3] = video::S3DVertex(vertex_pos[3], normal,
			MapBlock_LightColor(alpha, light_encoded, light_source),
			core::vector2d<f32>(x0+w*abs_scale, y0));

	collector->append(t, vertices, 4, indices, 6);
}

static void extract_faces(MeshCollector *collector,
		const std::vector<FarNode> &data,
		const VoxelArea &data_area, const VoxelArea &gen_area,
		const v3s16 &block_div,
		const FarMap *far_map,
		size_t *num_faces_added)
{
	// At least one extra node at each edge is required. This enables speed
	// optimization of lookups in this algorithm.
	assert(data_area.MinEdge.X <= gen_area.MinEdge.X - 1);
	assert(data_area.MinEdge.Y <= gen_area.MinEdge.Y - 1);
	assert(data_area.MinEdge.Z <= gen_area.MinEdge.Z - 1);
	assert(data_area.MaxEdge.X >= gen_area.MaxEdge.X + 1);
	assert(data_area.MaxEdge.Y >= gen_area.MaxEdge.Y + 1);
	assert(data_area.MaxEdge.Z >= gen_area.MaxEdge.Z + 1);

	INodeDefManager *ndef = far_map->client->getNodeDefManager();

	v3s16 data_extent = data_area.getExtent();

	v3s16 p000;
	for (p000.Y=gen_area.MinEdge.Y-1; p000.Y<=gen_area.MaxEdge.Y; p000.Y++)
	for (p000.X=gen_area.MinEdge.X-1; p000.X<=gen_area.MaxEdge.X; p000.X++)
	for (p000.Z=gen_area.MinEdge.Z-1; p000.Z<=gen_area.MaxEdge.Z; p000.Z++)
	{
		size_t i000 = data_area.index(p000);
		const FarNode &n000 = data[i000];
		const FarNode &n001 = data[data_area.added_z(data_extent, i000, 1)];
		const FarNode &n010 = data[data_area.added_y(data_extent, i000, 1)];
		const FarNode &n100 = data[data_area.added_x(data_extent, i000, 1)];
		const ContentFeatures &f000 = ndef->get(n000.id);
		const ContentFeatures &f001 = ndef->get(n001.id);
		const ContentFeatures &f010 = ndef->get(n010.id);
		const ContentFeatures &f100 = ndef->get(n100.id);
		int s000 = f000.solidness ?: f000.visual_solidness;
		int s001 = f001.solidness ?: f001.visual_solidness;
		int s010 = f010.solidness ?: f010.visual_solidness;
		int s100 = f100.solidness ?: f100.visual_solidness;

		// TODO: Somehow handle non-cubic things maybe
		// NOTE: Don't call 'continue' though; it would break this
		//       cube-collecting algorithm.
		/*if(f000.drawtype == NDT_){
		}*/

		if(s000 > s001){
			add_face(collector, n000, p000,  0,0,1, data,
					data_area, block_div, far_map);
			(*num_faces_added)++;
		}
		else if(s000 < s001){
			v3s16 p001 = p000 + v3s16(0,0,1);
			add_face(collector, n001, p001, 0,0,-1, data,
					data_area, block_div, far_map);
			(*num_faces_added)++;
		}
		if(s000 > s010){
			add_face(collector, n000, p000,  0,1,0, data,
					data_area, block_div, far_map);
			(*num_faces_added)++;
		}
		else if(s000 < s010){
			v3s16 p010 = p000 + v3s16(0,1,0);
			add_face(collector, n010, p010, 0,-1,0, data,
					data_area, block_div, far_map);
			(*num_faces_added)++;
		}
		if(s000 > s100){
			add_face(collector, n000, p000,  1,0,0, data,
					data_area, block_div, far_map);
			(*num_faces_added)++;
		}
		else if(s000 < s100){
			v3s16 p100 = p000 + v3s16(1,0,0);
			add_face(collector, n100, p100, -1,0,0, data,
					data_area, block_div, far_map);
			(*num_faces_added)++;
		}
	}
}

static scene::SMesh* create_farblock_mesh(scene::SMesh *old_mesh,
		FarMap *far_map, MeshCollector *collector)
{
	IShaderSource *ssrc = far_map->client->getShaderSource();

	scene::SMesh *mesh = NULL;
	if (old_mesh) {
		old_mesh->clear();
		mesh = old_mesh;
	} else {
		mesh = new scene::SMesh();
	}

	for(u32 i = 0; i < collector->prebuffers.size(); i++)
	{
		PreMeshBuffer &p = collector->prebuffers[i];

		for(u32 j = 0; j < p.vertices.size(); j++)
		{
			video::S3DVertexTangents *vertex = &p.vertices[j];
			// Note applyFacesShading second parameter is precalculated sqrt
			// value for speed improvement
			// Skip it for lightsources and top faces.
			video::SColor &vc = vertex->Color;
			if (!vc.getBlue()) {
				if (vertex->Normal.Y < -0.5) {
					applyFacesShading (vc, 0.447213);
				} else if (vertex->Normal.X > 0.5) {
					applyFacesShading (vc, 0.670820);
				} else if (vertex->Normal.X < -0.5) {
					applyFacesShading (vc, 0.670820);
				} else if (vertex->Normal.Z > 0.5) {
					applyFacesShading (vc, 0.836660);
				} else if (vertex->Normal.Z < -0.5) {
					applyFacesShading (vc, 0.836660);
				}
			}
			if(!far_map->config_enable_shaders)
			{
				// - Classic lighting (shaders handle this by themselves)
				// Set initial real color and store for later updates
				u8 day = vc.getRed();
				u8 night = vc.getGreen();
				finalColorBlend(vc, day, night, 1000);
			}
		}

		// Create material
		video::SMaterial material;
		material.setFlag(video::EMF_LIGHTING, false);
		material.setFlag(video::EMF_BACK_FACE_CULLING, true);
		material.setFlag(video::EMF_BILINEAR_FILTER, false);
		material.setFlag(video::EMF_FOG_ENABLE, true);
		material.setTexture(0, p.tile.texture);

		// TODO: A special texture atlas needs to be generated to be used here

		if (far_map->config_enable_shaders) {
			material.MaterialType = ssrc->getShaderInfo(p.tile.shader_id).material;
			p.tile.applyMaterialOptionsWithShaders(material);
			if (p.tile.normal_texture) {
				material.setTexture(1, p.tile.normal_texture);
			}
			material.setTexture(2, p.tile.flags_texture);
		} else {
			p.tile.applyMaterialOptions(material);
		}

		// Create meshbuffer
		scene::SMeshBufferTangents *buf = new scene::SMeshBufferTangents();
		// Set material
		buf->Material = material;
		// Add to mesh
		mesh->addMeshBuffer(buf);
		// Mesh grabbed it
		buf->drop();
		buf->append(&p.vertices[0], p.vertices.size(),
			&p.indices[0], p.indices.size());
	}

	if (far_map->config_enable_shaders) {
		scene::IMeshManipulator* meshmanip =
				far_map->client->getSceneManager()->getMeshManipulator();
		meshmanip->recalculateTangents(mesh, true, false, false);
	}

	return mesh;
}

void FarBlockMeshGenerateTask::inThread()
{
	infostream<<"Generating FarBlock mesh for "
			<<PP(block.p)<<std::endl;

	// Main mesh
	{
		MeshCollector collector;

		size_t num_faces_added = 0;

		extract_faces(&collector, block.content, block.content_area,
				block.effective_area, block.block_div, far_map,
				&num_faces_added);

		g_profiler->avg("Far: num faces per mesh", num_faces_added);
		g_profiler->add("Far: num meshes generated", 1);

		assert(block.mesh == NULL);
		if (num_faces_added > 0) {
			block.mesh = create_farblock_mesh(block.mesh, far_map, &collector);
			block.mesh->setHardwareMappingHint(scene::EHM_STATIC);
		}
	}

	// MapBlock-sized meshes
	VoxelArea mapblock_meshes_area(
		v3s16(0, 0, 0),
		v3s16(FMP_SCALE-1, FMP_SCALE-1, FMP_SCALE-1)
	);
	std::vector<FarNode> content_buf;
	v3s16 mp;
	for (mp.Z=0; mp.Z<FMP_SCALE; mp.Z++)
	for (mp.Y=0; mp.Y<FMP_SCALE; mp.Y++)
	for (mp.X=0; mp.X<FMP_SCALE; mp.X++) {
		// Index to mapblock_meshes
		size_t mi = mapblock_meshes_area.index(mp);

		MeshCollector collector;

		VoxelArea gen_area( // effective
			block.dp00 + v3s16(
				block.block_div.X * mp.X,
				block.block_div.Y * mp.Y,
				block.block_div.Z * mp.Z
			),
			block.dp00 + v3s16(
				block.block_div.X * mp.X + block.block_div.X - 1,
				block.block_div.Y * mp.Y + block.block_div.Y - 1,
				block.block_div.Z * mp.Z + block.block_div.Z - 1
			)
		);
		VoxelArea content_buf_area(
			gen_area.MinEdge - v3s16(1,1,1),
			gen_area.MaxEdge + v3s16(1,1,1)
		);
		content_buf.resize(content_buf_area.getVolume());
		v3s16 p;
		// Fill in everything but the edges
		for (p.Y=gen_area.MinEdge.Y; p.Y<=gen_area.MaxEdge.Y; p.Y++)
		for (p.X=gen_area.MinEdge.X; p.X<=gen_area.MaxEdge.X; p.X++)
		for (p.Z=gen_area.MinEdge.Z; p.Z<=gen_area.MaxEdge.Z; p.Z++) {
			content_buf[content_buf_area.index(p)] =
					block.content[block.content_area.index(p)];
		}

		size_t num_faces_added = 0;

		extract_faces(&collector, content_buf, content_buf_area,
				gen_area, block.block_div, far_map,
				&num_faces_added);

		g_profiler->avg("Far: num faces per mb mesh", num_faces_added);
		g_profiler->add("Far: num mb meshes generated", 1);

		if (num_faces_added > 0) {
			block.mapblock_meshes[mi] = create_farblock_mesh(
					block.mapblock_meshes[mi], far_map, &collector);
		}
	}
}

void FarBlockMeshGenerateTask::sync()
{
	if(block.mesh){
		far_map->insertGeneratedBlockMesh(block.p, block.mesh,
				block.mapblock_meshes);
		block.mesh->drop();
		block.mesh = NULL;
		for (size_t i=0; i<block.mapblock_meshes.size(); i++) {
			if (block.mapblock_meshes[i] != NULL) {
				block.mapblock_meshes[i]->drop();
				block.mapblock_meshes[i] = NULL;
			}
		}
	} else {
		infostream<<"No FarBlock mesh result for "
				<<PP(block.p)<<std::endl;
	}
}

/*
	FarMapWorkerThread
*/

FarMapWorkerThread::~FarMapWorkerThread()
{
	verbosestream<<"FarMapWorkerThread: Deleting remaining tasks (in)"<<std::endl;
	for(;;){
		try {
			FarMapTask *t = m_queue_in.pop_front(0);
			delete t;
		} catch(ItemNotFoundException &e){
			break;
		}
	}
	verbosestream<<"FarMapWorkerThread: Deleting remaining tasks (sync)"<<std::endl;
	for(;;){
		try {
			FarMapTask *t = m_queue_sync.pop_front(0);
			delete t;
		} catch(ItemNotFoundException &e){
			break;
		}
	}
}

void FarMapWorkerThread::addTask(FarMapTask *task)
{
	g_profiler->add("Far: tasks added", 1);

	s32 length = ++m_queue_in_length;
	g_profiler->avg("Far: task queue length (avg)", length);

	m_queue_in.push_back(task);
	deferUpdate();
}

void FarMapWorkerThread::sync()
{
	for(;;){
		try {
			FarMapTask *t = m_queue_sync.pop_front(0);
			infostream<<"FarMapWorkerThread: Running task in sync"<<std::endl;
			t->sync();
			delete t;
			g_profiler->add("Far: tasks finished", 1);
		} catch(ItemNotFoundException &e){
			break;
		}
	}
}

void FarMapWorkerThread::doUpdate()
{
	for(;;){
		try {
			s32 length = --m_queue_in_length;
			g_profiler->avg("Far: task queue length (avg)", length);

			FarMapTask *t = m_queue_in.pop_front(250);
			infostream<<"FarMapWorkerThread: Running task in thread"<<std::endl;
			t->inThread();
			m_queue_sync.push_back(t);
		} catch(ItemNotFoundException &e){
			break;
		}
	}
}

/*
	BlockAreaBitmap
*/

void BlockAreaBitmap::reset(const VoxelArea &new_blocks_area)
{
	blocks_area = new_blocks_area;
	blocks.clear();
	blocks.resize(new_blocks_area.getVolume());
}

void BlockAreaBitmap::set(v3s16 bp, bool v)
{
	if(!blocks_area.contains(bp))
		return;
	blocks[blocks_area.index(bp)] = v;
}

bool BlockAreaBitmap::get(v3s16 bp)
{
	if(!blocks_area.contains(bp))
		return false;
	return blocks[blocks_area.index(bp)];
}

/*
	FarAtlas
*/

FarAtlas::FarAtlas(FarMap *far_map)
{
	video::IVideoDriver* driver =
			far_map->client->getSceneManager()->getVideoDriver();
	atlas = atlas::createAtlasRegistry("FarMap", driver, far_map->client);
}

FarAtlas::~FarAtlas()
{
	delete atlas;
}

atlas::AtlasSegmentReference FarAtlas::addTexture(const std::string &name)
{
	atlas::AtlasSegmentDefinition def;
	def.image_name = name;
	def.total_segments = v2s32(1, 1);
	def.select_segment = v2s32(0, 0);
	//def.lod_simulation = 2; // TODO
	return atlas->find_or_add_segment(def);
}

void FarAtlas::addNode(content_t id, const std::string &top,
		const std::string &bottom, const std::string &side)
{
	NodeSegRefs nsr;
	nsr.refs[0] = addTexture(top);
	nsr.refs[1] = addTexture(bottom);
	nsr.refs[2] = addTexture(side);

	if((content_t)node_segrefs.size() < id + 1)
		node_segrefs.resize(id + 1);
	node_segrefs[id] = nsr;
}

const atlas::AtlasSegmentCache* FarAtlas::getNode(content_t id, u8 face) const
{
	assert(face < 3);
	if(node_segrefs.size() <= id)
		return NULL;
	return atlas->get_texture(node_segrefs[id].refs[face]);
}

void FarAtlas::update()
{
	atlas->update();
}

/*
	FarMap
*/

FarMap::FarMap(
		Client *client,
		scene::ISceneNode* parent,
		scene::ISceneManager* mgr,
		s32 id
):
	scene::ISceneNode(parent, mgr, id),
	client(client),
	atlas(this),
	farblock_shader_id(0)
{
	m_bounding_box = core::aabbox3d<f32>(-BS*1000000,-BS*1000000,-BS*1000000,
			BS*1000000,BS*1000000,BS*1000000);

	updateSettings();
	
	m_worker_thread.start();

	// TODO: Add nodes to atlas
}

FarMap::~FarMap()
{
	m_worker_thread.stop();
	m_worker_thread.wait();

	for (std::map<v2s16, FarSector*>::iterator i = m_sectors.begin();
			i != m_sectors.end(); i++) {
		delete i->second;
	}
}

FarSector* FarMap::getOrCreateSector(v2s16 p)
{
	std::map<v2s16, FarSector*>::iterator i = m_sectors.find(p);
	if(i != m_sectors.end())
		return i->second;
	FarSector *s = new FarSector(p);
	m_sectors[p] = s;
	return s;
}

FarBlock* FarMap::getOrCreateBlock(v3s16 p)
{
	v2s16 p2d(p.X, p.Z);
	FarSector *s = getOrCreateSector(p2d);
	return s->getOrCreateBlock(p.Y);
}

void FarMap::insertData(v3s16 area_offset_mapblocks, v3s16 area_size_mapblocks,
		v3s16 block_div,
		const std::vector<u16> &node_ids, const std::vector<u8> &lights)
{
	infostream<<"FarMap::insertData: "
			<<"area_offset_mapblocks: "<<PP(area_offset_mapblocks)
			<<", area_size_mapblocks: "<<PP(area_size_mapblocks)
			<<", block_div: "<<PP(block_div)
			<<", node_ids.size(): "<<node_ids.size()
			<<", lights.size(): "<<lights.size()
			<<std::endl;

	// Convert to divisions (which will match FarNodes)
	v3s16 div_p0(
		area_offset_mapblocks.X * block_div.X,
		area_offset_mapblocks.Y * block_div.Y,
		area_offset_mapblocks.Z * block_div.Z
	);
	v3s16 div_p1 = div_p0 + v3s16(
		area_size_mapblocks.X * block_div.X,
		area_size_mapblocks.Y * block_div.Y,
		area_size_mapblocks.Z * block_div.Z
	);
	// This can be used for indexing node_ids and lights
	VoxelArea div_area(div_p0, div_p1 - v3s16(1,1,1));

	// Convert to FarBlock positions (this can cover extra area)
	VoxelArea fb_area(
		getContainerPos(area_offset_mapblocks, FMP_SCALE),
		getContainerPos(area_offset_mapblocks + area_size_mapblocks -
				v3s16(1,1,1), FMP_SCALE)
	);

	v3s16 fbp;
	for (fbp.Y=fb_area.MinEdge.Y; fbp.Y<=fb_area.MaxEdge.Y; fbp.Y++)
	for (fbp.X=fb_area.MinEdge.X; fbp.X<=fb_area.MaxEdge.X; fbp.X++)
	for (fbp.Z=fb_area.MinEdge.Z; fbp.Z<=fb_area.MaxEdge.Z; fbp.Z++) {
		infostream<<"FarMap::insertData: FarBlock "<<PP(fbp)<<std::endl;

		FarBlock *b = getOrCreateBlock(fbp);
		
		b->resize(block_div);

		v3s16 dp_in_fb;
		for (dp_in_fb.Y=0; dp_in_fb.Y<b->effective_size.Y; dp_in_fb.Y++)
		for (dp_in_fb.X=0; dp_in_fb.X<b->effective_size.X; dp_in_fb.X++)
		for (dp_in_fb.Z=0; dp_in_fb.Z<b->effective_size.Z; dp_in_fb.Z++) {
			v3s16 dp1 = b->dp00 + dp_in_fb;
			// The source area does not necessarily contain all positions that
			// the matching blocks contain
			if(!div_area.contains(dp1))
				continue;
			size_t source_i = div_area.index(dp1);
			size_t dst_i = b->index(dp1);
			b->content[dst_i].id = node_ids[source_i];
			/*b->content[dst_i].id = ((dp1.X + dp1.Y + dp1.Z) % 3 == 0) ?
					5 : CONTENT_AIR;*/
			b->content[dst_i].light = lights[source_i];
		}

		startGeneratingBlockMesh(b);
	}
}

void FarMap::startGeneratingBlockMesh(FarBlock *b)
{
	FarBlockMeshGenerateTask *t = new FarBlockMeshGenerateTask(this, *b);

	m_worker_thread.addTask(t);
}

void FarMap::insertGeneratedBlockMesh(v3s16 p, scene::SMesh *mesh,
		const std::vector<scene::SMesh*> &mapblock_meshes)
{
	FarBlock *b = getOrCreateBlock(p);

	if(b->mesh)
		b->mesh->drop();

	mesh->grab();
	b->mesh = mesh;

	for (size_t i=0; i<b->mapblock_meshes.size(); i++) {
		if(b->mapblock_meshes[i])
			b->mapblock_meshes[i]->drop();
		b->mapblock_meshes[i] = NULL;
	}
	b->mapblock_meshes.resize(mapblock_meshes.size());
	for (size_t i=0; i<mapblock_meshes.size(); i++) {
		if (mapblock_meshes[i] != NULL) {
			mapblock_meshes[i]->grab();
			b->mapblock_meshes[i] = mapblock_meshes[i];
		}
	}

	b->resetCameraOffset(m_camera_offset);

	g_profiler->add("Far: generated farblocks meshes", 1);
}

void FarMap::update()
{
	updateSettings();

	if (farblock_shader_id == 0 && config_enable_shaders) {
		// Fetch a basic node shader
		// NOTE: ShaderSource does not implement asynchronous fetching of
		// shaders from the main thread like TextureSource. While it probably
		// should do that, we can just fetch this id here for now as we use a
		// static shader anyway.
		u8 material_type = TILE_MATERIAL_BASIC;
		enum NodeDrawType drawtype = NDT_NORMAL;
		infostream<<"FarBlockMeshGenerate: Getting shader..."<<std::endl;
		IShaderSource *ssrc = client->getShaderSource();
		farblock_shader_id = ssrc->getShader(
				"nodes_shader", material_type, drawtype);
		infostream<<"FarBlockMeshGenerate: shader_id="<<farblock_shader_id
				<<std::endl;
	}

	atlas.update();

	m_worker_thread.sync();
}

void FarMap::updateCameraOffset(v3s16 camera_offset)
{
	if (camera_offset == m_camera_offset) return;

	m_camera_offset = camera_offset;

	for (std::map<v2s16, FarSector*>::iterator i = m_sectors.begin();
			i != m_sectors.end(); i++) {
		FarSector *s = i->second;

		for (std::map<s16, FarBlock*>::iterator i = s->blocks.begin();
				i != s->blocks.end(); i++) {
			FarBlock *b = i->second;
			b->updateCameraOffset(camera_offset);
		}
	}
}

void FarMap::reportNormallyRenderedBlocks(const BlockAreaBitmap &nrb)
{
	normally_rendered_blocks = nrb;

	verbosestream<<"FarMap::reportNormallyRenderedBlocks: "
			<<"reported area: ";
	normally_rendered_blocks.blocks_area.print(verbosestream);
	verbosestream<<std::endl;
}

void FarMap::createAtlas()
{
	INodeDefManager *ndef = client->getNodeDefManager();

	// TODO

	// Umm... let's just start from zero and see how far we get?
	for(content_t id=0; ; id++){
		const ContentFeatures &f = ndef->get(id);
		if ((f.name.empty() || f.name == "unknown") && id != CONTENT_UNKNOWN)
			break;

		infostream<<"FarMap: Adding node "<<id<<" = \""<<f.name<<"\""<<std::endl;

		std::string top = f.tiledef[0].name;
		if (!top.empty()) {
			std::string bottom = f.tiledef[1].name;
			std::string side = f.tiledef[2].name;
			if(bottom.empty())
				bottom = top;
			if(side.empty())
				side = top;
			atlas.addNode(id, top, bottom, side);
		}

		if(id == 65535)
			break;
	}

	infostream<<"FarMap: Refreshing atlas textures"<<std::endl;

	atlas.atlas->refresh_textures();

	infostream<<"FarMap: Created atlas out of "<<atlas.node_segrefs.size()
			<<" nodes";
}

void FarMap::updateSettings()
{
	config_enable_shaders = g_settings->getBool("enable_shaders");
	config_trilinear_filter = g_settings->getBool("trilinear_filter");
	config_bilinear_filter = g_settings->getBool("bilinear_filter");
	config_anistropic_filter = g_settings->getBool("anisotropic_filter");
}

static void renderMesh(FarMap *far_map, scene::SMesh *mesh,
		video::IVideoDriver* driver)
{
	u32 c = mesh->getMeshBufferCount();
	/*infostream<<"FarMap::renderBlock: "<<PP(b->p)<<": Rendering "
			<<c<<" meshbuffers"<<std::endl;*/
	for (u32 i=0; i<c; i++)
	{
		scene::IMeshBuffer *buf = mesh->getMeshBuffer(i);
		buf->getMaterial().setFlag(video::EMF_TRILINEAR_FILTER,
				far_map->config_trilinear_filter);
		buf->getMaterial().setFlag(video::EMF_BILINEAR_FILTER,
				far_map->config_bilinear_filter);
		buf->getMaterial().setFlag(video::EMF_ANISOTROPIC_FILTER,
				far_map->config_anistropic_filter);

		const video::SMaterial& material = buf->getMaterial();

		driver->setMaterial(material);

		driver->drawMeshBuffer(buf);
	}
}

static void renderBlock(FarMap *far_map, FarBlock *b,
		video::IVideoDriver* driver,
		size_t *profiler_num_rendered_farblocks,
		size_t *profiler_num_rendered_fbmbparts)
{
	if(!b->mesh){
		//infostream<<"FarMap::renderBlock: "<<PP(b->p)<<": No mesh"<<std::endl;
		return;
	}
	(*profiler_num_rendered_farblocks)++;

	// Check what ClientMap is rendering and avoid rendering over it
	VoxelArea area_in_mapblocks_from_origin(
		v3s16(0, 0, 0),
		v3s16(FMP_SCALE-1, FMP_SCALE-1, FMP_SCALE-1)
	);
	v3s16 fb_origin_mapblock = b->p * FMP_SCALE;
	VoxelArea area_in_mapblocks(
		area_in_mapblocks_from_origin.MinEdge + fb_origin_mapblock,
		area_in_mapblocks_from_origin.MaxEdge + fb_origin_mapblock
	);

	v3s16 mp0;

	bool fb_being_normally_rendered = false;
	for (mp0.Z=0; mp0.Z<FMP_SCALE; mp0.Z++)
	for (mp0.Y=0; mp0.Y<FMP_SCALE; mp0.Y++)
	for (mp0.X=0; mp0.X<FMP_SCALE; mp0.X++) {
		v3s16 mp = fb_origin_mapblock + mp0;
		if (far_map->normally_rendered_blocks.get(mp)) {
			// This MapBlock is being rendered by ClientMap
			fb_being_normally_rendered = true;
			goto big_break;
		}
	}
big_break:;

	if (fb_being_normally_rendered) {
		for (mp0.Z=0; mp0.Z<FMP_SCALE; mp0.Z++)
		for (mp0.Y=0; mp0.Y<FMP_SCALE; mp0.Y++)
		for (mp0.X=0; mp0.X<FMP_SCALE; mp0.X++) {
			v3s16 mp = fb_origin_mapblock + mp0;
			if (far_map->normally_rendered_blocks.get(mp))
				continue;
			assert(area_in_mapblocks.contains(mp));
			// Index to mapblock_meshes
			size_t mi = area_in_mapblocks.index(mp);
			scene::SMesh *mesh = b->mapblock_meshes[mi];
			if (!mesh)
				continue;
			renderMesh(far_map, mesh, driver);
			(*profiler_num_rendered_fbmbparts)++;
		}
	} else {
		scene::SMesh *mesh = b->mesh;
		if (mesh) {
			renderMesh(far_map, mesh, driver);
		}
	}
}

void FarMap::render()
{
	ScopeProfiler sp_render(g_profiler, "Far: render time (avg)", SPT_AVG);

	video::IVideoDriver* driver = SceneManager->getVideoDriver();
	driver->setTransform(video::ETS_WORLD, AbsoluteTransformation);

	size_t profiler_num_rendered_farblocks = 0;
	size_t profiler_num_rendered_fbmbparts = 0;

	//ClientEnvironment *client_env = &client->getEnv();
	//ClientMap *map = &client_env->getClientMap();

	for (std::map<v2s16, FarSector*>::iterator i = m_sectors.begin();
			i != m_sectors.end(); i++) {
		FarSector *s = i->second;

		for (std::map<s16, FarBlock*>::iterator i = s->blocks.begin();
				i != s->blocks.end(); i++) {
			FarBlock *b = i->second;

			renderBlock(this, b, driver,
					&profiler_num_rendered_farblocks,
					&profiler_num_rendered_fbmbparts);
		}
	}

	g_profiler->avg("Far: rendered farblocks per frame",
			profiler_num_rendered_farblocks);
	g_profiler->avg("Far: rendered farblock-mb-parts frame",
			profiler_num_rendered_fbmbparts);
}

void FarMap::OnRegisterSceneNode()
{
	if(IsVisible)
	{
		SceneManager->registerNodeForRendering(this, scene::ESNRP_SOLID);
		//SceneManager->registerNodeForRendering(this, scene::ESNRP_TRANSPARENT);
	}

	ISceneNode::OnRegisterSceneNode();
}

const core::aabbox3d<f32>& FarMap::getBoundingBox() const
{
	return m_bounding_box;
}
