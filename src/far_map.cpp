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
	crude_mesh(NULL),
	fine_mesh(NULL),
	generating_mesh(false),
	mesh_is_empty(false)
{
}

FarBlock::~FarBlock()
{
	if(crude_mesh)
		crude_mesh->drop();

	unloadFineMesh();
	unloadMapblockMeshes();
}

void FarBlock::unloadFineMesh()
{
	if (!fine_mesh)
		return;
	fine_mesh->drop();
	fine_mesh = NULL;
}

void FarBlock::unloadMapblockMeshes()
{
	for (size_t i=0; i<mapblock_meshes.size(); i++) {
		if (!mapblock_meshes[i])
			continue;
		mapblock_meshes[i]->drop();
		mapblock_meshes[i] = NULL;
	}
	for (size_t i=0; i<mapblock2_meshes.size(); i++) {
		if (!mapblock2_meshes[i])
			continue;
		mapblock2_meshes[i]->drop();
		mapblock2_meshes[i] = NULL;
	}
	mapblock_meshes.clear();
	mapblock2_meshes.clear();
}

void FarBlock::resize(v3s16 new_divs_per_mb)
{
	divs_per_mb = new_divs_per_mb;

	// Block's effective origin in FarNodes based on divs_per_mb
	dp00 = v3s16(
		p.X * FMP_SCALE * divs_per_mb.X,
		p.Y * FMP_SCALE * divs_per_mb.Y,
		p.Z * FMP_SCALE * divs_per_mb.Z
	);

	// Effective size of content in FarNodes based on divs_per_mb in global
	// coordinates
	effective_size = v3s16(
			FMP_SCALE * divs_per_mb.X,
			FMP_SCALE * divs_per_mb.Y,
			FMP_SCALE * divs_per_mb.Z);

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
	if (camera_offset == current_camera_offset)
		return;

	if (crude_mesh) {
		translateMesh(crude_mesh,
				intToFloat(current_camera_offset-camera_offset, BS));
	}

	if (fine_mesh) {
		translateMesh(fine_mesh,
				intToFloat(current_camera_offset-camera_offset, BS));

		for (size_t i=0; i<mapblock_meshes.size(); i++) {
			if (!mapblock_meshes[i])
				continue;
			translateMesh(mapblock_meshes[i],
					intToFloat(current_camera_offset-camera_offset, BS));
		}
		for (size_t i=0; i<mapblock2_meshes.size(); i++) {
			if (!mapblock2_meshes[i])
				continue;
			translateMesh(mapblock2_meshes[i],
					intToFloat(current_camera_offset-camera_offset, BS));
		}
	}

	current_camera_offset = camera_offset;
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
}

FarSector::~FarSector()
{
	for (std::map<s16, FarBlock*>::iterator i = blocks.begin();
			i != blocks.end(); i++) {
		delete i->second;
	}
}

FarBlock* FarSector::getBlock(s16 y)
{
	std::map<s16, FarBlock*>::iterator i = blocks.find(y);
	if(i != blocks.end())
		return i->second;
	return NULL;
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
		FarMap *far_map, const FarBlock &source_block,
		GenLevel level):
	far_map(far_map),
	block(source_block),
	level(level)
{
	// We don't want to deal with whatever mesh the block was currently holding;
	// set things to NULL so that FarBlock's destructor will not drop the
	// original contents and we can store our results in it.
	block.fine_mesh = NULL;
	block.crude_mesh = NULL;
	for (size_t i=0; i<block.mapblock_meshes.size(); i++) {
		block.mapblock_meshes[i] = NULL;
	}
	for (size_t i=0; i<block.mapblock2_meshes.size(); i++) {
		block.mapblock2_meshes[i] = NULL;
	}
}

FarBlockMeshGenerateTask::~FarBlockMeshGenerateTask()
{
}

static void add_face(MeshCollector *collector,
		const FarNode &n, const v3s16 &p, s16 dir_x, s16 dir_y, s16 dir_z,
		const std::vector<FarNode> &data,
		const VoxelArea &data_area,
		const v3s16 &divs_per_mb,
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
		MAP_BLOCKSIZE / divs_per_mb.X,
		MAP_BLOCKSIZE / divs_per_mb.Y,
		MAP_BLOCKSIZE / divs_per_mb.Z
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

	/*u8 light_day_4 = n.light % 0x0f;
	u8 light_night_4 = (n.light & 0xf0) >> 4;
	u8 light_day_8 = decode_light(light_day_4);
	u8 light_night_8 = decode_light(light_night_4);*/
	// Both lightbanks as 16-bit values from decode_light() as produced by
	// getFaceLight (day | (night << 8))
	// TODO
	//u16 light_encoded_16 = (light_day_8) | (light_night_8<<8);
	u16 light_encoded_16 = (255) | (255<<8);
	// Light produced by the node itself
	u8 light_source = 0;

	// Get texture from texture atlas
	u8 face = dir.Y == 1 ? 0 : dir.Y == -1 ? 1 : 2;
	bool crude = (divs_per_mb.X == 1);
	const atlas::AtlasSegmentCache *asc =
			far_map->atlas.getNode(n.id, face, crude);
	if (!asc) {
		const ContentFeatures &f = ndef->get(n.id);
		assert(f.tiledef[0].name == "" && "All nodes that have a texture should"
				" be found in FarMap's atlas");
		return; // Makes no faces
	}
	assert(asc->texture_pp);
	assert(*asc->texture_pp);

	// Texture coordinates
	float x0 = asc->coord0.X;
	float y0 = asc->coord0.Y;
	float x1 = asc->coord1.X;
	float y1 = asc->coord1.Y;

	TileSpec t;
	t.texture_id = 0; // atlas::AtlasRegistry doesn't provide this
	t.texture = *asc->texture_pp;
	t.alpha = alpha;
	t.material_type = TILE_MATERIAL_BASIC;

	if (far_map->config_enable_shaders) {
		t.shader_id = far_map->farblock_shader_id;
		bool normalmap_present = false;
		t.flags_texture = tsrc->getShaderFlagsTexture(normalmap_present);
	}

	// Finally create the vertices that we have been preparing for
	video::S3DVertex vertices[4];
	vertices[0] = video::S3DVertex(vertex_pos[0], normal,
			MapBlock_LightColor(alpha, light_encoded_16, light_source),
			core::vector2d<f32>(x1, y1));
	vertices[1] = video::S3DVertex(vertex_pos[1], normal,
			MapBlock_LightColor(alpha, light_encoded_16, light_source),
			core::vector2d<f32>(x0, y1));
	vertices[2] = video::S3DVertex(vertex_pos[2], normal,
			MapBlock_LightColor(alpha, light_encoded_16, light_source),
			core::vector2d<f32>(x0, y0));
	vertices[3] = video::S3DVertex(vertex_pos[3], normal,
			MapBlock_LightColor(alpha, light_encoded_16, light_source),
			core::vector2d<f32>(x1, y0));

	collector->append(t, vertices, 4, indices, 6);
}

static void extract_faces(MeshCollector *collector,
		const std::vector<FarNode> &data,
		const VoxelArea &data_area, const VoxelArea &gen_area,
		const v3s16 &divs_per_mb,
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
					data_area, divs_per_mb, far_map);
			(*num_faces_added)++;
		}
		else if(s000 < s001){
			v3s16 p001 = p000 + v3s16(0,0,1);
			add_face(collector, n001, p001, 0,0,-1, data,
					data_area, divs_per_mb, far_map);
			(*num_faces_added)++;
		}
		if(s000 > s010){
			add_face(collector, n000, p000,  0,1,0, data,
					data_area, divs_per_mb, far_map);
			(*num_faces_added)++;
		}
		else if(s000 < s010){
			v3s16 p010 = p000 + v3s16(0,1,0);
			add_face(collector, n010, p010, 0,-1,0, data,
					data_area, divs_per_mb, far_map);
			(*num_faces_added)++;
		}
		if(s000 > s100){
			add_face(collector, n000, p000,  1,0,0, data,
					data_area, divs_per_mb, far_map);
			(*num_faces_added)++;
		}
		else if(s000 < s100){
			v3s16 p100 = p000 + v3s16(1,0,0);
			add_face(collector, n100, p100, -1,0,0, data,
					data_area, divs_per_mb, far_map);
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
			video::SColor &vc = vertex->Color;
			/*// Note applyFacesShading second parameter is precalculated sqrt
			// value for speed improvement
			// Skip it for lightsources and top faces.
			// NOTE: Don't use this because we bake shading into the atlas
			//       textures in order to cheat with lighting effects
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
			}*/
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

	if (block.content.empty() || block.content_area.getVolume() == 0) {
		// This will have no meshes whatsoever
		return;
	}

	// Crude main mesh (always generated)
	{
		MeshCollector collector;

		size_t num_faces_added = 0;

		// FarBlock position in MapBlocks
		v3s16 bp00 = block.p * FMP_SCALE;
		v3s16 bp01 = bp00 + v3s16(1,1,1) * FMP_SCALE - v3s16(1,1,1);

		// Effective area in MapBlocks
		VoxelArea gen_area(bp00, bp01);

		// Content buffer area in MapBlocks
		VoxelArea content_buf_area(
			gen_area.MinEdge - v3s16(1,1,1),
			gen_area.MaxEdge + v3s16(1,1,1)
		);

		std::vector<FarNode> content_buf;
		content_buf.resize(content_buf_area.getVolume(), CONTENT_AIR);

		// Fill in everything but the edges
		v3s16 p;
		for (p.Y=gen_area.MinEdge.Y; p.Y<=gen_area.MaxEdge.Y; p.Y++)
		for (p.X=gen_area.MinEdge.X; p.X<=gen_area.MaxEdge.X; p.X++)
		for (p.Z=gen_area.MinEdge.Z; p.Z<=gen_area.MaxEdge.Z; p.Z++) {
			v3s16 source_p(
				p.X * block.divs_per_mb.X + block.divs_per_mb.X / 2,
				p.Y * block.divs_per_mb.Y + block.divs_per_mb.Y - 1,
				p.Z * block.divs_per_mb.Z + block.divs_per_mb.Z / 2
			);
			// Get topmost visible node
			for (; source_p.Y >= p.Y * block.divs_per_mb.Y; source_p.Y--) {
				FarNode n = block.content[block.content_area.index(source_p)];
				if (n.id != CONTENT_IGNORE && n.id != CONTENT_AIR) {
					content_buf[content_buf_area.index(p)] = n;
					break;
				}
			}
		}

		extract_faces(&collector, content_buf, content_buf_area,
				gen_area, v3s16(1,1,1), far_map,
				&num_faces_added);

		g_profiler->avg("Far: num faces per crude mesh", num_faces_added);
		g_profiler->add("Far: num crude meshes generated", 1);

		assert(block.crude_mesh == NULL);
		if (num_faces_added > 0) {
			block.crude_mesh = create_farblock_mesh(
					block.crude_mesh, far_map, &collector);
			// This gives Irrlicht permission to store this mesh on the GPU
			block.crude_mesh->setHardwareMappingHint(scene::EHM_STATIC);
		}
	}

	// Fine main mesh
	if (level >= GL_FINE) {
		MeshCollector collector;

		size_t num_faces_added = 0;

		extract_faces(&collector, block.content, block.content_area,
				block.effective_area, block.divs_per_mb, far_map,
				&num_faces_added);

		g_profiler->avg("Far: num faces per mesh", num_faces_added);
		g_profiler->add("Far: num meshes generated", 1);

		assert(block.fine_mesh == NULL);
		if (num_faces_added > 0) {
			block.fine_mesh = create_farblock_mesh(
					block.fine_mesh, far_map, &collector);
			// This gives Irrlicht permission to store this mesh on the GPU
			block.fine_mesh->setHardwareMappingHint(scene::EHM_STATIC);
		}
	}

	// Auxiliary meshes
	if (level >= GL_FINE_AND_AUX) {
		v3s16 mp;

		// MapBlock-sized meshes
		block.mapblock_meshes.resize(FMP_SCALE * FMP_SCALE * FMP_SCALE);
		VoxelArea mapblock_meshes_area(
			v3s16(0, 0, 0),
			v3s16(FMP_SCALE-1, FMP_SCALE-1, FMP_SCALE-1)
		);
		std::vector<FarNode> content_buf;
		for (mp.Z=0; mp.Z<FMP_SCALE; mp.Z++)
		for (mp.Y=0; mp.Y<FMP_SCALE; mp.Y++)
		for (mp.X=0; mp.X<FMP_SCALE; mp.X++) {
			// Index to mapblock_meshes
			size_t mi = mapblock_meshes_area.index(mp);

			MeshCollector collector;

			VoxelArea gen_area( // effective
				block.dp00 + v3s16(
					block.divs_per_mb.X * mp.X,
					block.divs_per_mb.Y * mp.Y,
					block.divs_per_mb.Z * mp.Z
				),
				block.dp00 + v3s16(
					block.divs_per_mb.X * mp.X + block.divs_per_mb.X - 1,
					block.divs_per_mb.Y * mp.Y + block.divs_per_mb.Y - 1,
					block.divs_per_mb.Z * mp.Z + block.divs_per_mb.Z - 1
				)
			);
			VoxelArea content_buf_area(
				gen_area.MinEdge - v3s16(1,1,1),
				gen_area.MaxEdge + v3s16(1,1,1)
			);
			content_buf.clear();
			content_buf.resize(content_buf_area.getVolume(), CONTENT_AIR);
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
					gen_area, block.divs_per_mb, far_map,
					&num_faces_added);

			g_profiler->avg("Far: num faces per mb mesh", num_faces_added);
			g_profiler->add("Far: num mb meshes generated", 1);

			if (num_faces_added > 0) {
				block.mapblock_meshes[mi] = create_farblock_mesh(
						block.mapblock_meshes[mi], far_map, &collector);
				// NOTE: Don't se this scene::EHM_STATIC. This is so small that
				// a VBO will do nothing but waste GPU resources, and we
				// actively avoid rendering many of these at a time.
			}
		}

		// 2x2x2-MapBlock-sized meshes
		block.mapblock2_meshes.resize(FMP_SCALE/2 * FMP_SCALE/2 * FMP_SCALE/2);
		VoxelArea mapblock2_meshes_area(
			v3s16(0, 0, 0),
			v3s16(FMP_SCALE/2-1, FMP_SCALE/2-1, FMP_SCALE/2-1)
		);
		for (mp.Z=0; mp.Z<FMP_SCALE/2; mp.Z++)
		for (mp.Y=0; mp.Y<FMP_SCALE/2; mp.Y++)
		for (mp.X=0; mp.X<FMP_SCALE/2; mp.X++) {
			// Index to mapblock2_meshes
			size_t mi = mapblock2_meshes_area.index(mp);

			MeshCollector collector;

			VoxelArea gen_area( // effective
				block.dp00 + v3s16(
					block.divs_per_mb.X * mp.X * 2,
					block.divs_per_mb.Y * mp.Y * 2,
					block.divs_per_mb.Z * mp.Z * 2
				),
				block.dp00 + v3s16(
					block.divs_per_mb.X * mp.X * 2 + block.divs_per_mb.X * 2 - 1,
					block.divs_per_mb.Y * mp.Y * 2 + block.divs_per_mb.Y * 2 - 1,
					block.divs_per_mb.Z * mp.Z * 2 + block.divs_per_mb.Z * 2 - 1
				)
			);
			VoxelArea content_buf_area(
				gen_area.MinEdge - v3s16(1,1,1),
				gen_area.MaxEdge + v3s16(1,1,1)
			);
			content_buf.clear();
			content_buf.resize(content_buf_area.getVolume(), CONTENT_AIR);
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
					gen_area, block.divs_per_mb, far_map,
					&num_faces_added);

			g_profiler->avg("Far: num faces per mb mesh", num_faces_added);
			g_profiler->add("Far: num mb meshes generated", 1);

			if (num_faces_added > 0) {
				block.mapblock2_meshes[mi] = create_farblock_mesh(
						block.mapblock2_meshes[mi], far_map, &collector);
				// This gives Irrlicht permission to store this mesh on the GPU
				block.crude_mesh->setHardwareMappingHint(scene::EHM_STATIC);
				// TODO: Check whether these get correctly deleted from the GPU
				// when block.mapblock2_meshes are dropped
			}
		}
	}
}

void FarBlockMeshGenerateTask::sync()
{
	if(block.crude_mesh || block.fine_mesh){
		far_map->insertGeneratedBlockMesh(
				block.p, block.crude_mesh, block.fine_mesh,
				block.mapblock_meshes, block.mapblock2_meshes);
		if (block.crude_mesh) {
			block.crude_mesh->drop();
			block.crude_mesh = NULL;
		}
		if (block.fine_mesh) {
			block.fine_mesh->drop();
			block.fine_mesh = NULL;
		}
		for (size_t i=0; i<block.mapblock_meshes.size(); i++) {
			if (block.mapblock_meshes[i] != NULL) {
				block.mapblock_meshes[i]->drop();
				block.mapblock_meshes[i] = NULL;
			}
		}
		for (size_t i=0; i<block.mapblock2_meshes.size(); i++) {
			if (block.mapblock2_meshes[i] != NULL) {
				block.mapblock2_meshes[i]->drop();
				block.mapblock2_meshes[i] = NULL;
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
			//infostream<<"FarMapWorkerThread: Running task in sync"<<std::endl;
			// TODO: Spread these sync() calls as thin as possible, because if a
			// bunch of them is called in a single frame, screen updates become
			// choppy
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
			FarMapTask *t = m_queue_in.pop_front(250);
			infostream<<"FarMapWorkerThread: Running task in thread"<<std::endl;
			t->inThread();
			m_queue_sync.push_back(t);

			s32 length = --m_queue_in_length;
			g_profiler->avg("Far: task queue length (avg)", length);
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

atlas::AtlasSegmentReference FarAtlas::addTexture(const std::string &name,
		bool is_top, bool crude)
{
	atlas::AtlasSegmentDefinition def;
	def.image_name = name;
	def.total_segments = v2s32(1, 1);
	def.select_segment = v2s32(0, 0);
	def.lod_simulation = crude ? 16 : 4;
	if (is_top)
		def.lod_simulation |= atlas::ATLAS_LOD_TOP_FACE;
	def.lod_simulation |= atlas::ATLAS_LOD_BAKE_SHADOWS;
	return atlas->find_or_add_segment(def);
}

void FarAtlas::addNode(content_t id, const std::string &top,
		const std::string &bottom, const std::string &side)
{
	NodeSegRefs nsr;
	nsr.refs[0] = addTexture(top, true, false);
	nsr.refs[1] = addTexture(bottom, false, false);
	nsr.refs[2] = addTexture(side, false, false);
	nsr.crude_refs[0] = addTexture(top, true, true);
	nsr.crude_refs[1] = addTexture(bottom, false, true);
	nsr.crude_refs[2] = addTexture(side, false, true);

	if((content_t)node_segrefs.size() < id + 1)
		node_segrefs.resize(id + 1);
	node_segrefs[id] = nsr;
}

const atlas::AtlasSegmentCache* FarAtlas::getNode(
	content_t id, u8 face, bool crude) const
{
	assert(face < 3);
	if(node_segrefs.size() <= id)
		return NULL;
	if (crude) {
		return atlas->get_texture(node_segrefs[id].crude_refs[face]);
	} else {
		return atlas->get_texture(node_segrefs[id].refs[face]);
	}
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
	config_enable_shaders(false),
	config_trilinear_filter(false),
	config_bilinear_filter(false),
	config_anisotropic_filter(false),
	config_far_map_range(800),
	config_far_map_minimize_memory_usage(false),
	farblock_shader_id(0),
	m_farblocks_exist_up_to_d(0),
	m_farblocks_exist_up_to_d_reset_counter(0)
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

FarSector* FarMap::getSector(v2s16 p)
{
	std::map<v2s16, FarSector*>::iterator i = m_sectors.find(p);
	if(i != m_sectors.end())
		return i->second;
	return NULL;
}

FarBlock* FarMap::getBlock(v3s16 p)
{
	v2s16 p2d(p.X, p.Z);
	FarSector *s = getSector(p2d);
	if (!s) return NULL;
	return s->getBlock(p.Y);
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

void FarMap::insertData(v3s16 fbp, v3s16 divs_per_mb,
		const std::vector<u16> &node_ids, const std::vector<u8> &lights)
{
	infostream<<"FarMap::insertData: fbp: "<<PP(fbp)
			<<", divs_per_mb: "<<PP(divs_per_mb)
			<<", node_ids.size(): "<<node_ids.size()
			<<", lights.size(): "<<lights.size()
			<<std::endl;

	v3s16 area_offset_mb(
			FMP_SCALE * fbp.X,
			FMP_SCALE * fbp.Y,
			FMP_SCALE * fbp.Z);

	v3s16 area_size_mb(FMP_SCALE, FMP_SCALE, FMP_SCALE);

	// Convert to divisions (which will match FarNodes)
	v3s16 source_fnp0(
		area_offset_mb.X * divs_per_mb.X,
		area_offset_mb.Y * divs_per_mb.Y,
		area_offset_mb.Z * divs_per_mb.Z
	);
	v3s16 source_fnp1 = source_fnp0 + v3s16(
		area_size_mb.X * divs_per_mb.X - 1,
		area_size_mb.Y * divs_per_mb.Y - 1,
		area_size_mb.Z * divs_per_mb.Z - 1
	);
	// This can be used for indexing node_ids and lights
	VoxelArea source_area(source_fnp0, source_fnp1);

	if (node_ids.size() != (size_t)source_area.getVolume() ||
			lights.size() != (size_t)source_area.getVolume()) {
		warningstream<<"FarMap::insertData: fbp: "<<PP(fbp)
				<<", divs_per_mb: "<<PP(divs_per_mb)
				<<", node_ids.size(): "<<node_ids.size()
				<<", lights.size(): "<<lights.size()
				<<": Invalid data sizes"<<std::endl;
		return;
	}

	infostream<<"FarMap::insertData: FarBlock "<<PP(fbp)<<std::endl;

	FarBlock *b = getOrCreateBlock(fbp);
	b->is_culled_by_server = false;
	b->resize(divs_per_mb);

	v3s16 dp_in_fb;
	for (dp_in_fb.Y=0; dp_in_fb.Y<b->effective_size.Y; dp_in_fb.Y++)
	for (dp_in_fb.X=0; dp_in_fb.X<b->effective_size.X; dp_in_fb.X++)
	for (dp_in_fb.Z=0; dp_in_fb.Z<b->effective_size.Z; dp_in_fb.Z++) {
		v3s16 dp1 = b->dp00 + dp_in_fb;
		// The source area does not necessarily contain all positions that
		// the matching blocks contain
		if(!source_area.contains(dp1))
			continue;
		size_t source_i = source_area.index(dp1);
		size_t dst_i = b->index(dp1);
		b->content[dst_i].id = node_ids[source_i];
		/*b->content[dst_i].id = ((dp1.X + dp1.Y + dp1.Z) % 3 == 0) ?
				5 : CONTENT_AIR;*/
		b->content[dst_i].light = lights[source_i];
	}
}

void FarMap::insertEmptyBlock(v3s16 fbp)
{
	FarBlock *b = getOrCreateBlock(fbp);
	b->is_culled_by_server = false;
}

void FarMap::insertCulledBlock(v3s16 fbp)
{
	FarBlock *b = getOrCreateBlock(fbp);
	b->is_culled_by_server = true;
}

void FarMap::startGeneratingBlockMesh(FarBlock *b,
		FarBlockMeshGenerateTask::GenLevel level)
{
	b->generating_mesh = true;

	FarBlockMeshGenerateTask *t = new FarBlockMeshGenerateTask(
			this, *b, level);

	m_worker_thread.addTask(t);
}

void FarMap::insertGeneratedBlockMesh(
		v3s16 p, scene::SMesh *crude_mesh, scene::SMesh *fine_mesh,
		const std::vector<scene::SMesh*> &mapblock_meshes,
		const std::vector<scene::SMesh*> &mapblock2_meshes)
{
	FarBlock *b = getOrCreateBlock(p);

	b->generating_mesh = false;

	if (b->crude_mesh) {
		b->crude_mesh->drop();
		b->crude_mesh = NULL;
	}
	if (crude_mesh) {
		crude_mesh->grab();
		b->crude_mesh = crude_mesh;
	}

	if (b->fine_mesh) {
		b->fine_mesh->drop();
		b->fine_mesh = NULL;
	}
	if (fine_mesh) {
		fine_mesh->grab();
		b->fine_mesh = fine_mesh;
	}

	if (b->crude_mesh == NULL && b->fine_mesh == NULL) {
		b->mesh_is_empty = true;
	} else {
		b->mesh_is_empty = false;
	}

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

	for (size_t i=0; i<b->mapblock2_meshes.size(); i++) {
		if(b->mapblock2_meshes[i])
			b->mapblock2_meshes[i]->drop();
		b->mapblock2_meshes[i] = NULL;
	}
	b->mapblock2_meshes.resize(mapblock2_meshes.size());
	for (size_t i=0; i<mapblock2_meshes.size(); i++) {
		if (mapblock2_meshes[i] != NULL) {
			mapblock2_meshes[i]->grab();
			b->mapblock2_meshes[i] = mapblock2_meshes[i];
		}
	}

	b->resetCameraOffset(current_camera_offset);

	g_profiler->add("Far: generated farblock meshes", 1);
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
	if (camera_offset == current_camera_offset) return;

	current_camera_offset = camera_offset;

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

	/*verbosestream<<"FarMap::reportNormallyRenderedBlocks: "
			<<"reported area: ";
			normally_rendered_blocks.blocks_area.print(verbosestream);
			verbosestream<<std::endl;*/
}

void FarMap::createAtlas()
{
	INodeDefManager *ndef = client->getNodeDefManager();

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

std::vector<v3s16> FarMap::suggestFarBlocksToFetch(v3s16 camera_p)
{
	// Don't fetch anything if the task queue length is too high
	// TODO: Where to get this value?
	const size_t max_queue_length = 50;
	size_t queue_length = m_worker_thread.getQueueLength();
	if (queue_length >= max_queue_length)
		return std::vector<v3s16>();
	static const size_t wanted_num_results = max_queue_length - queue_length;

	std::vector<v3s16> suggested_fbs;

	v3s16 center_mb = getContainerPos(camera_p, MAP_BLOCKSIZE);
	v3s16 center_fb = getContainerPos(center_mb, FMP_SCALE);

	s16 fetch_distance_farblocks =
			ceilf((float)config_far_map_range / MAP_BLOCKSIZE / FMP_SCALE);

	// Avoid running the algorithm through all the close FarBlocks that probably
	// have already been fetched, except once in a while to catch up with
	// possible missed FarBlocks due to player movement or whatever.
	s16 start_d = m_farblocks_exist_up_to_d; // Start one lower than have to
	if (++m_farblocks_exist_up_to_d_reset_counter >= 10) {
		m_farblocks_exist_up_to_d_reset_counter = 0;
		start_d = 0;
	}
	m_farblocks_exist_up_to_d = -1; // Reset and recalculate
	if (start_d < 0)
		start_d = 0;

	for (s16 d = start_d; d <= fetch_distance_farblocks; d++) {
		std::vector<v3s16> ps = FacePositionCache::getFacePositions(d);
		for (size_t i=0; i<ps.size(); i++) {
			v3s16 p = center_fb + ps[i];
			FarBlock *b = getBlock(p);
			if (b != NULL)
				continue; // Exists
			if (m_farblocks_exist_up_to_d == -1)
				m_farblocks_exist_up_to_d = d - 1;
			suggested_fbs.push_back(p);
			if (suggested_fbs.size() >= wanted_num_results)
				goto done;
		}
	}
done:

	infostream << "suggested_fbs.size()=" << suggested_fbs.size() << std::endl;
	return suggested_fbs;
}

s16 FarMap::suggestAutosendFarblocksRadius()
{
	s16 fetch_distance_farblocks =
			ceilf((float)config_far_map_range / MAP_BLOCKSIZE / FMP_SCALE);
	return fetch_distance_farblocks;
}

float FarMap::suggestFogDistance()
{
	return config_far_map_range * BS;
}

void FarMap::updateSettings()
{
	config_enable_shaders = g_settings->getBool("enable_shaders");
	config_trilinear_filter = g_settings->getBool("trilinear_filter");
	config_bilinear_filter = g_settings->getBool("bilinear_filter");
	config_anisotropic_filter = g_settings->getBool("anisotropic_filter");
	config_far_map_range = g_settings->getS16("far_map_range");
	config_far_map_minimize_memory_usage =
		g_settings->getBool("far_map_minimize_memory_usage");

	// Sanitize these a bit
	if (config_far_map_range < 100)
		config_far_map_range = 100;
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
				far_map->config_anisotropic_filter);

		const video::SMaterial& material = buf->getMaterial();

		driver->setMaterial(material);

		driver->drawMeshBuffer(buf);
	}
}

static void renderBlock(FarMap *far_map, FarBlock *b,
		video::IVideoDriver* driver,
		u32 *profiler_render_time_farblocks_us,
		u32 *profiler_render_time_fbcrudes_us,
		u32 *profiler_render_time_fbmbparts_us,
		u32 *profiler_render_time_fbmb2parts_us,
		size_t *profiler_num_rendered_farblocks,
		size_t *profiler_num_rendered_fbcrudes,
		size_t *profiler_num_rendered_fbmbparts,
		size_t *profiler_num_rendered_fbmb2parts)
{
	if (b->mesh_is_empty) {
		/*infostream<<"FarMap::renderBlock: "<<PP(b->p)<<": Mesh is empty"
				<<std::endl;*/
		return;
	}

	/*
		Cull FarBlock if possible so that we don't have to generate so many
		meshes. Meshes use nasty amounts of memory.
	*/

	scene::ICameraSceneNode *camera =
			far_map->getSceneManager()->getActiveCamera();
	v3f camera_pf = camera->getAbsolutePosition();
	camera_pf += v3f(
		far_map->current_camera_offset.X * BS,
		far_map->current_camera_offset.Y * BS,
		far_map->current_camera_offset.Z * BS
	);
	v3f block_pf(
		b->p.X * FMP_SCALE * MAP_BLOCKSIZE * BS,
		b->p.Y * FMP_SCALE * MAP_BLOCKSIZE * BS,
		b->p.Z * FMP_SCALE * MAP_BLOCKSIZE * BS
	);
	float d = (camera_pf - block_pf).getLength();

	/*std::cout<<"b->p="<<PP(b->p)
			<<", camera_pf="<<PP(camera_pf)
			<<", b->current_camera_offset="<<PP(b->current_camera_offset)
			<<", far_map->current_camera_offset="
					<<PP(far_map->current_camera_offset)
			<<", block_pf="<<PP(block_pf)
			<<", d="<<d
			<<std::endl;*/

	// Distance culling
	if (d > far_map->config_far_map_range * BS)
		return;

	// TODO: Frustum culling

	// TODO: Occlusion culling?

	/*
		This FarBlock will be rendered
	*/

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
	VoxelArea area_in_mapblock2x2x2s_from_origin(
		v3s16(0, 0, 0),
		v3s16(FMP_SCALE/2-1, FMP_SCALE/2-1, FMP_SCALE/2-1)
	);

	// If some of the MapBlocks inside this FarBlock are rendered normally, we
	// have to render MapBlock-sized pieces instead of the full FarBlock.
	bool fb_being_normally_rendered = false;
	if (far_map->normally_rendered_blocks.blocks_area.touches(area_in_mapblocks)) {
		v3s16 mp0;
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
	}

	bool render_in_pieces = fb_being_normally_rendered;
	bool avoid_crude_mesh = false;

	if (render_in_pieces && (b->mapblock_meshes.empty() ||
			b->mapblock2_meshes.empty())) {
		if (!b->generating_mesh && !b->content.empty()) {
			// We need small meshes for this ASAP
			far_map->startGeneratingBlockMesh(b,
					FarBlockMeshGenerateTask::GL_FINE_AND_AUX);
		}
		// Can't render in pieces because we don't have meshes for the pieces.
		// Render normally so that things don't blink annoyingly meanwhile.
		render_in_pieces = false;
		// Don't render crude mesh here even temporarily; it would look horrible
		// and would then immediately blink away making it even more horrible.
		avoid_crude_mesh = true;
	}

	if (render_in_pieces) {
		v3s16 mp20;
		for (mp20.Z=0; mp20.Z<FMP_SCALE/2; mp20.Z++)
		for (mp20.Y=0; mp20.Y<FMP_SCALE/2; mp20.Y++)
		for (mp20.X=0; mp20.X<FMP_SCALE/2; mp20.X++) {
			v3s16 mp0 = mp20 * 2;
			v3s16 mp1 = fb_origin_mapblock + mp0;
			if (!far_map->normally_rendered_blocks.get(mp1 + v3s16(1,0,0)) &&
				!far_map->normally_rendered_blocks.get(mp1 + v3s16(1,1,0)) &&
				!far_map->normally_rendered_blocks.get(mp1 + v3s16(0,1,0)) &&
				!far_map->normally_rendered_blocks.get(mp1 + v3s16(0,0,0)) &&
				!far_map->normally_rendered_blocks.get(mp1 + v3s16(1,0,1)) &&
				!far_map->normally_rendered_blocks.get(mp1 + v3s16(1,1,1)) &&
				!far_map->normally_rendered_blocks.get(mp1 + v3s16(0,1,1)) &&
				!far_map->normally_rendered_blocks.get(mp1 + v3s16(0,0,1)))
			{
				TimeTaker tt(NULL, NULL, PRECISION_MICRO);
				// Index to mapblock2_meshes
				size_t mi = area_in_mapblock2x2x2s_from_origin.index(mp20);
				scene::SMesh *mesh = b->mapblock2_meshes[mi];
				if (!mesh)
					continue;
				renderMesh(far_map, mesh, driver);
				(*profiler_num_rendered_fbmb2parts)++;
				*profiler_render_time_fbmb2parts_us += tt.stop(true);
			} else {
				TimeTaker tt(NULL, NULL, PRECISION_MICRO);
				v3s16 mp01;
				for (mp01.Z=0; mp01.Z<2; mp01.Z++)
				for (mp01.Y=0; mp01.Y<2; mp01.Y++)
				for (mp01.X=0; mp01.X<2; mp01.X++) {
					v3s16 mp = mp1 + mp01;
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
				*profiler_render_time_fbmbparts_us += tt.stop(true);
			}
		}
	} else {
		// Decide when to draw a normal mesh or a crude mesh
		// TODO: Configurable
		bool fine_mesh_wanted = (d < BS * 500);

		if (fine_mesh_wanted && b->fine_mesh) {
			scene::SMesh *mesh = b->fine_mesh;
			TimeTaker tt(NULL, NULL, PRECISION_MICRO);
			renderMesh(far_map, mesh, driver);
			*profiler_render_time_farblocks_us += tt.stop(true);
			(*profiler_num_rendered_farblocks)++;
		} else if(b->crude_mesh && !avoid_crude_mesh) {
			scene::SMesh *mesh = b->crude_mesh;
			TimeTaker tt(NULL, NULL, PRECISION_MICRO);
			renderMesh(far_map, mesh, driver);
			*profiler_render_time_fbcrudes_us += tt.stop(true);
			(*profiler_num_rendered_fbcrudes)++;
		}

		if (!b->generating_mesh) {
			if (fine_mesh_wanted && !b->fine_mesh) {
				// We need a fine mesh for this
				far_map->startGeneratingBlockMesh(b,
						FarBlockMeshGenerateTask::GL_FINE);
			} else if(!b->crude_mesh) {
				// We need a crude mesh for this so that we can render anything
				far_map->startGeneratingBlockMesh(b,
						FarBlockMeshGenerateTask::GL_CRUDE);
			}
		}

		// Save RAM from these monstrosities if at all possible. We can unload
		// them if we didn't start generating anything in the previous check.
		// TODO: A timeout instead of an immediate drop would be good
		if (!b->generating_mesh) {
			if (!fine_mesh_wanted && b->fine_mesh) {
				b->unloadFineMesh();
				b->unloadMapblockMeshes();
			}
		}
	}
}

void FarMap::render()
{
	ScopeProfiler sp_render(g_profiler, "Far: render time /frame", SPT_AVG);

	video::IVideoDriver* driver = SceneManager->getVideoDriver();
	driver->setTransform(video::ETS_WORLD, AbsoluteTransformation);

	u32 profiler_render_time_farblocks_us = 0;
	u32 profiler_render_time_fbcrudes_us = 0;
	u32 profiler_render_time_fbmbparts_us = 0;
	u32 profiler_render_time_fbmb2parts_us = 0;
	size_t profiler_num_total_farblocks = 0;
	size_t profiler_num_rendered_farblocks = 0;
	size_t profiler_num_rendered_fbcrudes = 0;
	size_t profiler_num_rendered_fbmbparts = 0;
	size_t profiler_num_rendered_fbmb2parts = 0;

	//ClientEnvironment *client_env = &client->getEnv();
	//ClientMap *map = &client_env->getClientMap();

	for (std::map<v2s16, FarSector*>::iterator i = m_sectors.begin();
			i != m_sectors.end(); i++) {
		FarSector *s = i->second;
		profiler_num_total_farblocks += s->blocks.size();

		for (std::map<s16, FarBlock*>::iterator i = s->blocks.begin();
				i != s->blocks.end(); i++) {
			FarBlock *b = i->second;

			renderBlock(this, b, driver,
					&profiler_render_time_farblocks_us,
					&profiler_render_time_fbcrudes_us,
					&profiler_render_time_fbmbparts_us,
					&profiler_render_time_fbmb2parts_us,
					&profiler_num_rendered_farblocks,
					&profiler_num_rendered_fbcrudes,
					&profiler_num_rendered_fbmbparts,
					&profiler_num_rendered_fbmb2parts);
		}
	}

	g_profiler->avg("Far: total: farblocks",
			profiler_num_total_farblocks);
	g_profiler->avg("Far: render count: farblocks /frame",
			profiler_num_rendered_farblocks);
	g_profiler->avg("Far: render count: fbcrudes /frame",
			profiler_num_rendered_fbcrudes);
	g_profiler->avg("Far: render count: fbmbparts /frame",
			profiler_num_rendered_fbmbparts);
	g_profiler->avg("Far: render count: fbmb2parts /frame",
			profiler_num_rendered_fbmb2parts);
	g_profiler->avg("Far: render time: farblocks /frame",
			profiler_render_time_farblocks_us / 1000000.f);
	g_profiler->avg("Far: render time: fbcrudes /frame",
			profiler_render_time_fbcrudes_us / 1000000.f);
	g_profiler->avg("Far: render time: fbmbparts /frame",
			profiler_render_time_fbmbparts_us / 1000000.f);
	g_profiler->avg("Far: render time: fbmb2parts /frame",
			profiler_render_time_fbmb2parts_us / 1000000.f);
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
