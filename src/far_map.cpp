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
#include "irrlichttypes_extrabloated.h"
#include <IMaterialRenderer.h>

#define PP(x) "("<<(x).X<<","<<(x).Y<<","<<(x).Z<<")"

FarMapBlock::FarMapBlock(v3s16 p):
	p(p),
	mesh(NULL)
{
}

FarMapBlock::~FarMapBlock()
{
	if(mesh)
		mesh->drop();
}

void FarMapBlock::resize(v3s16 new_block_div)
{
	block_div = new_block_div;

	v3s16 area_size(FMP_SCALE, FMP_SCALE, FMP_SCALE);

	v3s16 total_size(
			area_size.X * block_div.X,
			area_size.Y * block_div.Y,
			area_size.Z * block_div.Z);

	size_t total_size_n = total_size.X * total_size.Y * total_size.Z;

	content.resize(total_size_n);
}

void FarMapBlock::updateCameraOffset(v3s16 camera_offset)
{
	if (!mesh) return;

	if (camera_offset != current_camera_offset) {
		translateMesh(mesh, intToFloat(current_camera_offset-camera_offset, BS));
		current_camera_offset = camera_offset;
	}
}

void FarMapBlock::resetCameraOffset(v3s16 camera_offset)
{
	current_camera_offset = v3s16(0, 0, 0);
	updateCameraOffset(camera_offset);
}

FarMapSector::FarMapSector(v2s16 p):
	p(p)
{
}

FarMapSector::~FarMapSector()
{
	for (std::map<s16, FarMapBlock*>::iterator i = blocks.begin();
			i != blocks.end(); i++) {
		delete i->second;
	}
}

FarMapBlock* FarMapSector::getOrCreateBlock(s16 y)
{
	std::map<s16, FarMapBlock*>::iterator i = blocks.find(y);
	if(i != blocks.end())
		return i->second;
	v3s16 p3d(p.X, y, p.Y);
	FarMapBlock *b = new FarMapBlock(p3d);
	blocks[y] = b;
	return b;
}

FarMapBlockMeshGenerateTask::FarMapBlockMeshGenerateTask(
		FarMap *far_map, const FarMapBlock &source_block_):
	far_map(far_map),
	source_block(source_block_),
	mesh(NULL)
{
	// We don't want to deal with whatever mesh the block is currently holding;
	// set it to NULL so that no destructor will drop it.
	source_block.mesh = NULL;
}

FarMapBlockMeshGenerateTask::~FarMapBlockMeshGenerateTask()
{
	if(mesh)
		mesh->drop();
}

void FarMapBlockMeshGenerateTask::inThread()
{
	infostream<<"Generating FarMapBlock mesh for "
			<<PP(source_block.p)<<std::endl;

	MeshCollector collector;

	// TODO

	// Test
	for (size_t i0=0; i0<5; i0++)
	{
		const u16 indices[] = {0,1,2,2,3,0};

		v3s16 dir(0, 0, 1);
		v3s16 vertex_dirs[4];
		getNodeVertexDirs(dir, vertex_dirs);

		v3f scale(
			MAP_BLOCKSIZE / source_block.block_div.X,
			MAP_BLOCKSIZE / source_block.block_div.Y,
			MAP_BLOCKSIZE / source_block.block_div.Z
		);

		v3f pf = v3f(source_block.p.X, source_block.p.Y, source_block.p.Z)
				* MAP_BLOCKSIZE * FMP_SCALE * BS;
		pf += v3f(0.5, 0.0, 0.5) * MAP_BLOCKSIZE * FMP_SCALE * BS;
		pf.Y += 0.2 * MAP_BLOCKSIZE * FMP_SCALE * BS * i0;

		v3f vertex_pos[4];
		for(u16 i=0; i<4; i++)
		{
			vertex_pos[i] = v3f(
					BS/2*vertex_dirs[i].X,
					BS/2*vertex_dirs[i].Y,
					BS/2*vertex_dirs[i].Z
			);
			vertex_pos[i].X *= scale.X;
			vertex_pos[i].Y *= scale.Y;
			vertex_pos[i].Z *= scale.Z;
			vertex_pos[i] += pf;
		}

		v3f normal(dir.X, dir.Y, dir.Z);

		u8 alpha = 255;

		// As produced by getFaceLight (day | (night << 8))
		u16 light_encoded = (255<<8) || (255);
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
		float w = 1.0;
		float h = 1.0;

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

		TileSpec t;
		t.alpha = alpha;

		collector.append(t, vertices, 4, indices, 6);
	}
	
	/*
		Convert MeshCollector to SMesh
	*/

	assert(mesh == NULL);
	mesh = new scene::SMesh();

	for(u32 i = 0; i < collector.prebuffers.size(); i++)
	{
		PreMeshBuffer &p = collector.prebuffers[i];

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
		//material.setFlag(video::EMF_BACK_FACE_CULLING, true); // TODO
		material.setFlag(video::EMF_BACK_FACE_CULLING, false);
		material.setFlag(video::EMF_BILINEAR_FILTER, false);
		//material.setFlag(video::EMF_FOG_ENABLE, true); // TODO
		material.setFlag(video::EMF_FOG_ENABLE, false);
		//material.setTexture(0, p.tile.texture); // TODO

		if (p.tile.material_flags & MATERIAL_FLAG_HIGHLIGHTED) {
			material.MaterialType = video::EMT_TRANSPARENT_ADD_COLOR;
		} else {
			// TODO: When shaders are enabled, use a special shader for this
			p.tile.applyMaterialOptions(material);
			/*if (far_map->config_enable_shaders) {
				material.MaterialType = m_shdrsrc->getShaderInfo(p.tile.shader_id).material;
				p.tile.applyMaterialOptionsWithShaders(material);
				if (p.tile.normal_texture) {
					material.setTexture(1, p.tile.normal_texture);
				}
				material.setTexture(2, p.tile.flags_texture);
			} else {
				p.tile.applyMaterialOptions(material);
			}*/
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
}

void FarMapBlockMeshGenerateTask::sync()
{
	if(mesh){
		far_map->insertGeneratedBlockMesh(source_block.p, mesh);
		mesh->drop();
		mesh = NULL;
	} else {
		infostream<<"No FarMapBlock mesh result for "
				<<PP(source_block.p)<<std::endl;
	}
}

FarMapWorkerThread::~FarMapWorkerThread()
{
	// TODO: Delete remaining tasks from both queues
}

void FarMapWorkerThread::addTask(FarMapTask *task)
{
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
		} catch(ItemNotFoundException &e){
			break;
		}
	}
}

FarMap::FarMap(
		Client *client,
		scene::ISceneNode* parent,
		scene::ISceneManager* mgr,
		s32 id
):
	scene::ISceneNode(parent, mgr, id),
	m_client(client)
{
	m_bounding_box = core::aabbox3d<f32>(-BS*1000000,-BS*1000000,-BS*1000000,
			BS*1000000,BS*1000000,BS*1000000);
	
	m_worker_thread.start();
}

FarMap::~FarMap()
{
	m_worker_thread.stop();
	m_worker_thread.wait();

	for (std::map<v2s16, FarMapSector*>::iterator i = m_sectors.begin();
			i != m_sectors.end(); i++) {
		delete i->second;
	}
}

FarMapSector* FarMap::getOrCreateSector(v2s16 p)
{
	std::map<v2s16, FarMapSector*>::iterator i = m_sectors.find(p);
	if(i != m_sectors.end())
		return i->second;
	FarMapSector *s = new FarMapSector(p);
	m_sectors[p] = s;
	return s;
}

FarMapBlock* FarMap::getOrCreateBlock(v3s16 p)
{
	v2s16 p2d(p.X, p.Z);
	FarMapSector *s = getOrCreateSector(p2d);
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
	// Convert to FarMapBlock positions
	// Inclusive
	v3s16 area_p0 = getContainerPos(area_offset_mapblocks, FMP_SCALE);
	// Inclusive
	v3s16 area_p1 = getContainerPos(
			area_offset_mapblocks + area_size_mapblocks - v3s16(1,1,1), FMP_SCALE);

	v3s16 fbp;
	for (fbp.Y=area_p0.Y; fbp.Y<=area_p1.Y; fbp.Y++)
	for (fbp.X=area_p0.X; fbp.X<=area_p1.X; fbp.X++)
	for (fbp.Z=area_p0.Z; fbp.Z<=area_p1.Z; fbp.Z++) {
		infostream<<"FarMap::insertData: FarBlock "<<PP(fbp)<<std::endl;

		FarMapBlock *b = getOrCreateBlock(fbp);
		
		b->resize(block_div);

		// TODO: Remove this
		b->content[0].id = 5;
		b->content[10].id = 6;
		b->content[20].id = 7;
		b->content[30].id = 8;

		// TODO: Copy stuff into b

		startGeneratingBlockMesh(b);
	}
}

void FarMap::startGeneratingBlockMesh(FarMapBlock *b)
{
	FarMapBlockMeshGenerateTask *t = new FarMapBlockMeshGenerateTask(this, *b);

	m_worker_thread.addTask(t);
}

void FarMap::insertGeneratedBlockMesh(v3s16 p, scene::SMesh *mesh)
{
	FarMapBlock *b = getOrCreateBlock(p);
	if(b->mesh)
		b->mesh->drop();
	mesh->grab();
	b->mesh = mesh;
	b->resetCameraOffset(m_camera_offset);
}

void FarMap::update()
{
	config_enable_shaders = g_settings->getBool("enable_shaders");
	config_trilinear_filter = g_settings->getBool("trilinear_filter");
	config_bilinear_filter = g_settings->getBool("bilinear_filter");
	config_anistropic_filter = g_settings->getBool("anisotropic_filter");

	m_worker_thread.sync();
}

void FarMap::updateCameraOffset(v3s16 camera_offset)
{
	if (camera_offset == m_camera_offset) return;

	m_camera_offset = camera_offset;

	for (std::map<v2s16, FarMapSector*>::iterator i = m_sectors.begin();
			i != m_sectors.end(); i++) {
		FarMapSector *s = i->second;

		for (std::map<s16, FarMapBlock*>::iterator i = s->blocks.begin();
				i != s->blocks.end(); i++) {
			FarMapBlock *b = i->second;
			b->updateCameraOffset(camera_offset);
		}
	}
}

static void renderBlock(FarMap *far_map, FarMapBlock *b, video::IVideoDriver* driver)
{
	scene::SMesh *mesh = b->mesh;
	if(!mesh){
		infostream<<"FarMap::renderBlock: "<<PP(b->p)<<": No mesh"<<std::endl;
		return;
	}

	u32 c = mesh->getMeshBufferCount();
	infostream<<"FarMap::renderBlock: "<<PP(b->p)<<": Rendering "
			<<c<<" meshbuffers"<<std::endl;
	for(u32 i=0; i<c; i++)
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

void FarMap::render()
{
	video::IVideoDriver* driver = SceneManager->getVideoDriver();
	driver->setTransform(video::ETS_WORLD, AbsoluteTransformation);

	for (std::map<v2s16, FarMapSector*>::iterator i = m_sectors.begin();
			i != m_sectors.end(); i++) {
		FarMapSector *s = i->second;

		for (std::map<s16, FarMapBlock*>::iterator i = s->blocks.begin();
				i != s->blocks.end(); i++) {
			FarMapBlock *b = i->second;
			renderBlock(this, b, driver);
		}
	}
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
