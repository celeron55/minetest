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
#include "irrlichttypes_extrabloated.h"
#include <IMaterialRenderer.h>

FarMapBlock::FarMapBlock(v3s16 p, v3s16 block_div):
	p(p),
	block_div(block_div),
	mesh(NULL)
{
	v3s16 area_size(FMP_SCALE, FMP_SCALE, FMP_SCALE);

	v3s16 total_size(
			area_size.X * block_div.X,
			area_size.Y * block_div.Y,
			area_size.Z * block_div.Z);

	size_t total_size_n = total_size.X * total_size.Y * total_size.Z;

	content.resize(total_size_n);
}

FarMapBlock::~FarMapBlock()
{
	mesh->drop();
}

void FarMapBlock::setMesh(scene::SMesh *new_mesh)
{
	if(mesh)
		mesh->drop();
	new_mesh->grab();
	mesh = new_mesh;
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
		FarMap *far_map, const FarMapBlock &source_block),
	far_map(far_map),
	source_block(source_block),
	mesh(NULL)
{
}

FarMapBlockMeshGenerateTask::~FarMapBlockMeshGenerateTask()
{
	if(mesh)
		mesh->drop();
}

void FarMapBlockMeshGenerateTask::inThread()
{
	// TODO
}

void FarMapBlockMeshGenerateTask::afterThread()
{
	// TODO
}

void FarMapWorkerThread::doUpdate()
{
	QueuedFarMapWorker *q;
	while ((q = m_queue_in.pop())) {

		ScopeProfiler sp(g_profiler, "Client: Mesh making");

		MapBlockMesh *mesh_new = new MapBlockMesh(q->data, m_camera_offset);

		FarMapWorkerResult r;
		r.p = q->p;
		r.mesh = mesh_new;
		r.ack_block_to_server = q->ack_block_to_server;

		m_queue_out.push_back(r);

		delete q;
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
	// Convert to FarMapBlock positions
	// Inclusive
	v3s16 area_p0 = getContainerPos(area_offset_mapblocks, FMP_SCALE);
	// Exclusive
	v3s16 area_p1 = getContainerPos(
			area_offset_mapblocks + area_size_mapblocks, FMP_SCALE);

	v3s16 fbp;
	for (fbp.Y=area_p0.Y; fbp.Y<area_p1.Y; fbp.Y++)
	for (fbp.X=area_p0.X; fbp.X<area_p1.X; fbp.X++)
	for (fbp.Z=area_p0.Z; fbp.Z<area_p1.Z; fbp.Z++) {
		FarMapBlock *b = getOrCreateBlock(fbp);

		// TODO: Remove this
		b->content[0] = 5;
		b->content[10] = 6;
		b->content[20] = 7;
		b->content[30] = 8;

		// TODO: Copy stuff into b

		startGeneratingBlockMesh(b);
	}
}

void FarMap::startGeneratingBlockMesh(v3s16 p)
{
	// TODO
}

void FarMap::insertGeneratedBlockMesh(v3s16 p, scene::SMesh *mesh)
{
	// TODO
}

void update()
{
	m_worker_thread.sync();
}

static void renderBlock(FarMapBlock *b, video::IVideoDriver* driver)
{
	scene::SMesh *mesh = b->mesh;

	u32 c = mesh->getMeshBufferCount();
	for(u32 i=0; i<c; i++)
	{
		scene::IMeshBuffer *buf = mesh->getMeshBuffer(i);
		// TODO
		//buf->getMaterial().setFlag(video::EMF_TRILINEAR_FILTER, m_cache_trilinear_filter);
		//buf->getMaterial().setFlag(video::EMF_BILINEAR_FILTER, m_cache_bilinear_filter);
		//buf->getMaterial().setFlag(video::EMF_ANISOTROPIC_FILTER, m_cache_anistropic_filter);

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
			renderBlock(b, driver);
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
