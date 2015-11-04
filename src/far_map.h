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
#ifndef __FAR_MAP_H__
#define __FAR_MAP_H__

#include "irrlichttypes_bloated.h"
#include <ISceneNode.h>
#include <vector>
#include <map>

class Client;
//class ITextureSource;

// FarMapBlock size in MapBlocks in every dimension
#define FMP_SCALE 8

struct FarMapNode
{
	u16 id;
	u8 light;
};

struct FarMapBlock
{
	v3s16 p;

	// In how many pieces MapBlocks have been divided per dimension
	v3s16 block_div;

	std::vector<FarMapNode> content;

	scene::SMesh *mesh;

	FarMapBlock(v3s16 p, v3s16 block_div);
	~FarMapBlock();

	void generateMesh(scene::SMesh *new_mesh);
};

struct FarMapSector
{
	v2s16 p;

	std::map<s16, FarMapBlock*> blocks;

	FarMapSector(v2s16 p);
	~FarMapSector();

	FarMapBlock* getOrCreateBlock(s16 p);
};

struct FarMapTask
{
	virtual ~FarMapTask(){}
	virtual void inThread() = 0;
	virtual void afterThread() = 0;
};

struct FarMapBlockMeshGenerateTask: public FarMapTask
{
	FarMap *far_map;
	FarMapBlock source_block;
	scene::SMesh *mesh;

	FarMapBlockMeshGenerateTask(FarMap *far_map, const FarMapBlock &source_block);
	~FarMapBlockMeshGenerateTask();
	void inThread();
	void afterThread();
};

class FarMapWorkerThread: public UpdateThread
{
public:
	FarMapWorkerThread(): UpdateThread("FarMapWorker") {}

	void addTask(FarMapTask *task);
	void sync();

private:
	void doUpdate();

	MutexedQueue<FarMapTask> m_queue_in;
	//v3s16 m_camera_offset; // TODO
};

class FarMap: public scene::ISceneNode
{
public:
	FarMap(
			Client *client,
			scene::ISceneNode* parent,
			scene::ISceneManager* mgr,
			s32 id
	);
	~FarMap();

	FarMapSector* getOrCreateSector(v2s16 p);
	FarMapBlock* getOrCreateBlock(v3s16 p);

	// Parameter dimensions are in MapBlocks
	void insertData(v3s16 area_offset_mapblocks, v3s16 area_size_mapblocks,
			v3s16 block_div,
			const std::vector<u16> &node_ids, const std::vector<u8> &lights);

	void startGeneratingBlockMesh(v3s16 p);
	void insertGeneratedBlockMesh(v3s16 p, scene::SMesh *mesh);

	void update();

	// ISceneNode methods
	void OnRegisterSceneNode();
	void render();
	const core::aabbox3d<f32>& getBoundingBox() const;

private:
	Client *m_client;
	FarMapWorkerThread m_worker_thread;

	// Source data
	std::map<v2s16, FarMapSector*> m_sectors;

	// Rendering stuff
	core::aabbox3d<f32> m_bounding_box;
};

#endif
