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
#include "util/thread.h" // UpdateThread
#include "threading/atomic.h"
#include "voxel.h" // VoxelArea
#include <ISceneNode.h>
#include <SMesh.h>
#include <vector>
#include <map>

class Client;
//class ITextureSource;
struct FarMap;

// FarBlock size in MapBlocks in every dimension
#define FMP_SCALE 8

struct FarNode
{
	u16 id;
	u8 light;

	FarNode(u16 id=0, u8 light=0): id(id), light(light) {}
};

struct FarBlock
{
	v3s16 p;

	// In how many pieces MapBlocks have been divided per dimension
	v3s16 block_div;
	// Block's effective origin in FarNodes based on block_div
	v3s16 dp00;
	// Effective size of content in FarNodes based on block_div in global
	// coordinates
	v3s16 effective_size;
	// Effective size of content in FarNodes based on block_div in global
	// coordinates
	VoxelArea effective_area;
	// Raw size of content in FarNodes based on block_div in global
	// coordinates
	v3s16 content_size;
	// Raw area of content in FarNodes based on block_div in global
	// coordinates
	VoxelArea content_area;

	std::vector<FarNode> content;

	// Mesh covering everything in this FarBlock
	scene::SMesh *mesh;

	// An array of MapBlock-sized meshes to be used when the area is partly
	// being rendered from the regular Map and the whole mesh cannnot be used
	std::vector<scene::SMesh*> mapblock_meshes;

	v3s16 current_camera_offset;

	FarBlock(v3s16 p);
	~FarBlock();

	void resize(v3s16 new_block_div);
	void updateCameraOffset(v3s16 camera_offset);
	void resetCameraOffset(v3s16 camera_offset = v3s16(0, 0, 0));

	size_t index(v3s16 p) {
		//p -= dp00;
		assert(content_area.contains(p));
		return content_area.index(p);
	}
};

struct FarSector
{
	v2s16 p;

	std::map<s16, FarBlock*> blocks;

	std::vector<v2s16> mapsectors_covered;

	FarSector(v2s16 p);
	~FarSector();

	FarBlock* getOrCreateBlock(s16 p);
};

struct FarMapTask
{
	virtual ~FarMapTask(){}
	virtual void inThread() = 0;
	virtual void sync() = 0;
};

struct FarBlockMeshGenerateTask: public FarMapTask
{
	FarMap *far_map;
	FarBlock block;

	FarBlockMeshGenerateTask(FarMap *far_map, const FarBlock &source_block);
	~FarBlockMeshGenerateTask();
	void inThread();
	void sync();
};

class FarMapWorkerThread: public UpdateThread
{
public:
	FarMapWorkerThread():
		UpdateThread("FarMapWorker"),
		m_queue_in_length(0)
	{}
	~FarMapWorkerThread();

	void addTask(FarMapTask *task);
	void sync();

private:
	void doUpdate();

	MutexedQueue<FarMapTask*> m_queue_in;
	MutexedQueue<FarMapTask*> m_queue_sync;

	Atomic<s32> m_queue_in_length; // For profiling
};

// ClientMap uses this to report what it's rendering so that FarMap can avoid
// rendering over it
struct BlockAreaBitmap
{
	VoxelArea blocks_area;
	std::vector<bool> blocks;

	void reset(const VoxelArea &new_blocks_area);
	void set(v3s16 bp, bool v);
	bool get(v3s16 bp);
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

	FarSector* getOrCreateSector(v2s16 p);
	FarBlock* getOrCreateBlock(v3s16 p);

	// Parameter dimensions are in MapBlocks
	void insertData(v3s16 area_offset_mapblocks, v3s16 area_size_mapblocks,
			v3s16 block_div,
			const std::vector<u16> &node_ids, const std::vector<u8> &lights);

	void startGeneratingBlockMesh(FarBlock *b);
	void insertGeneratedBlockMesh(v3s16 p, scene::SMesh *mesh,
			const std::vector<scene::SMesh*> &mapblock_meshes);

	void update();
	void updateCameraOffset(v3s16 camera_offset);

	void reportNormallyRenderedBlocks(const BlockAreaBitmap &nrb);

	// ISceneNode methods
	void OnRegisterSceneNode();
	void render();
	const core::aabbox3d<f32>& getBoundingBox() const;

	Client *client;

	bool config_enable_shaders;
	bool config_trilinear_filter;
	bool config_bilinear_filter;
	bool config_anistropic_filter;

	u32 farblock_shader_id;
	BlockAreaBitmap normally_rendered_blocks;

private:
	void updateSettings();

	FarMapWorkerThread m_worker_thread;

	// Source data
	std::map<v2s16, FarSector*> m_sectors;

	// Rendering stuff
	core::aabbox3d<f32> m_bounding_box;
	v3s16 m_camera_offset;
};

#endif
