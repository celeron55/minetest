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
#include "util/atlas.h"
#include <ISceneNode.h>
#include <SMesh.h>
#include <vector>
#include <map>

class Client;
struct FarMap;

struct FarNode
{
	u16 id;
	u8 light;

	FarNode(u16 id=0, u8 light=0): id(id), light(light) {}
};

struct FarBlock
{
	// Position in FarBlocks
	v3s16 p;
	// In how many pieces MapBlocks have been divided per dimension
	v3s16 divs_per_mb;
	// Block's effective origin in FarNodes based on divs_per_mb
	v3s16 dp00;
	// Effective size of content in FarNodes based on divs_per_mb in global
	// coordinates
	v3s16 effective_size;
	// Effective size of content in FarNodes based on divs_per_mb in global
	// coordinates
	VoxelArea effective_area;
	// Raw size of content in FarNodes based on divs_per_mb in global
	// coordinates
	v3s16 content_size;
	// Raw area of content in FarNodes based on divs_per_mb in global
	// coordinates
	VoxelArea content_area;

	// Can be empty if server reports the FarBlock to be completely empty
	std::vector<FarNode> content;

	// If true, server has opted not to send this to us but may send it later.
	// If true, content should always be empty.
	bool is_culled_by_server;

	// Information given by server. Really the important bit about this is that
	// it's partially NOT loaded, which means we should ask again sometime so
	// that the server may give us a more full loaded version.
	bool is_partly_loaded_by_server;
	s32 partly_loaded_refresh_counter;

	// Very crude mesh covering everything in this FarBlock
	scene::SMesh *crude_mesh;

	// Mesh covering everything in this FarBlock
	scene::SMesh *fine_mesh;

	// An array of MapBlock-sized meshes to be used when the area is partly
	// being rendered from the regular Map and the whole mesh cannnot be used
	std::vector<scene::SMesh*> mapblock_meshes;

	// An array of 2x2x2-MapBlock-sized meshes to be used when the area is
	// partly being rendered from the regular Map and the whole mesh cannnot be
	// used
	std::vector<scene::SMesh*> mapblock2_meshes;

	// TODO: Some kind of usage timer for these small meshes so that they can be
	//       unloaded when they are not needed anymore. However it is not
	//       important because relatively few of them will be generated to begin
	//       with.

	// True while the mesh generator is running for this FarBlock. Old meshes
	// may be available for rendering while it is running.
	bool generating_mesh;

	// Can be set to true while the mesh generator is running if needed
	bool mesh_is_outdated;

	// This is true after mesh has been generated and it has turned out that
	// this FarBlock does not require any geometry. In that case, mesh will be
	// NULL and regeneration should not be triggered based on it being NULL.
	bool mesh_is_empty;

	// Used by the faraway rendering error minimizing system
	v3s16 current_camera_offset;

	FarBlock(v3s16 p);
	~FarBlock();

	void unloadFineMesh();
	void unloadMapblockMeshes();

	void resize(v3s16 new_divs_per_mb);
	void updateCameraOffset(v3s16 camera_offset);
	void resetCameraOffset(v3s16 camera_offset = v3s16(0, 0, 0));

	size_t index(v3s16 p) {
		//p -= dp00;
		assert(content_area.contains(p));
		return content_area.index(p);
	}
};

// For some reason sectors seem to be faster than just a plain std::map<v3s16,
// FarBlock*>, so that's what we use here.
struct FarSector
{
	v2s16 p;

	std::map<s16, FarBlock*> blocks;

	FarSector(v2s16 p);
	~FarSector();

	FarBlock* getBlock(s16 p);
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
	enum GenLevel {
		GL_CRUDE,
		GL_FINE,
		GL_FINE_AND_AUX
	};

	FarMap *far_map;
	FarBlock block;
	GenLevel level;

	FarBlockMeshGenerateTask(FarMap *far_map, const FarBlock &source_block,
			GenLevel level);
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
	s32 getQueueLength() { return m_queue_in_length; }

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

struct FarAtlas
{
	struct NodeSegRefs {
		atlas::AtlasSegmentReference refs[3]; // top, bottom, side
		atlas::AtlasSegmentReference crude_refs[3]; // top, bottom, side
	};

	atlas::AtlasRegistry *atlas;
	std::vector<NodeSegRefs> node_segrefs;

	FarAtlas(FarMap *far_map);
	~FarAtlas();
	atlas::AtlasSegmentReference addTexture(const std::string &name,
			bool is_top, bool crude);
	void addNode(content_t id, const std::string &top,
			const std::string &bottom, const std::string &side);
	const atlas::AtlasSegmentCache* getNode(
			content_t id, u8 face, bool crude) const;
	void update();
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

	FarSector* getSector(v2s16 p);
	FarBlock* getBlock(v3s16 p);
	FarSector* getOrCreateSector(v2s16 p);
	FarBlock* getOrCreateBlock(v3s16 p);

	// Parameter dimensions are in MapBlocks
	void insertData(v3s16 fbp, v3s16 divs_per_mb,
			const std::vector<u16> &node_ids, const std::vector<u8> &lights,
			bool is_partly_loaded);
	void insertEmptyBlock(v3s16 fbp);
	void insertCulledBlock(v3s16 fbp);
	void insertLoadInProgressBlock(v3s16 fbp);

	void startGeneratingBlockMesh(FarBlock *b,
			FarBlockMeshGenerateTask::GenLevel level);
	void insertGeneratedBlockMesh(
			v3s16 p, scene::SMesh *crude_mesh, scene::SMesh *fine_mesh,
			const std::vector<scene::SMesh*> &mapblock_meshes,
			const std::vector<scene::SMesh*> &mapblock2_meshes);

	void update();
	void updateCameraOffset(v3s16 camera_offset);

	void reportNormallyRenderedBlocks(const BlockAreaBitmap &nrb);

	// Shall be called after the client receives all node definitions
	void createAtlas();

	std::vector<v3s16> suggestFarBlocksToFetch(v3s16 camera_p);
	s16 suggestAutosendFarblocksRadius(); // Result in FarBlocks
	float suggestFogDistance();

	// ISceneNode methods
	void OnRegisterSceneNode();
	void render();
	const core::aabbox3d<f32>& getBoundingBox() const;

	Client *client;
	FarAtlas atlas;

	bool config_enable_shaders;
	bool config_trilinear_filter;
	bool config_bilinear_filter;
	bool config_anisotropic_filter;
	s16 config_far_map_range;
	bool config_far_map_minimize_memory_usage;

	u32 farblock_shader_id;
	BlockAreaBitmap normally_rendered_blocks;

	v3s16 current_camera_offset;

private:
	void updateSettings();

	FarMapWorkerThread m_worker_thread;

	// Source data
	std::map<v2s16, FarSector*> m_sectors;

	// Rendering stuff
	core::aabbox3d<f32> m_bounding_box;

	// Fetch suggestion algorithm
	s16 m_farblocks_exist_up_to_d;
	s16 m_farblocks_exist_up_to_d_reset_counter;
};

#endif
