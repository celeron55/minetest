/*
Minetest
Copyright (C) 2013, 2017 celeron55, Perttu Ahola <celeron55@gmail.com>

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

#include "mesh_generator_thread.h"
#include "settings.h"
#include "profiler.h"
#include "client.h"
#include "mapblock.h"
#include "map.h"

#include "porting.h"
#include "profiler.h"
#define PROF_START \
		{ \
			u32 t0 = porting::getTime(PRECISION_MICRO);
#define PROF_ADD(desc) \
			u32 t1 = porting::getTime(PRECISION_MICRO); \
			g_profiler->graphAdd(desc " (s)", (t1 - t0) / 1000000.0); \
		}

/*
	CachedMapBlockData
*/

CachedMapBlockData::CachedMapBlockData():
	p(-1337,-1337,-1337),
	data(NULL),
	refcount_from_queue(0),
	last_used_timestamp(time(0))
{
}

CachedMapBlockData::~CachedMapBlockData()
{
	assert(refcount_from_queue == 0);

	if (data)
		delete[] data;
}

/*
	QueuedMeshUpdate
*/

QueuedMeshUpdate::QueuedMeshUpdate():
	p(-1337,-1337,-1337),
	ack_block_to_server(false),
	urgent(false),
	crack_level(-1),
	crack_pos(0,0,0),
	data(NULL)
{
}

QueuedMeshUpdate::~QueuedMeshUpdate()
{
	if (data)
		delete data;
}

/*
	MeshUpdateQueue
*/

MeshUpdateQueue::MeshUpdateQueue(Client *client):
	m_client(client)
{
	m_cache_enable_shaders = g_settings->getBool("enable_shaders");
	m_cache_use_tangent_vertices = m_cache_enable_shaders && (
		g_settings->getBool("enable_bumpmapping") ||
		g_settings->getBool("enable_parallax_occlusion"));
	m_cache_smooth_lighting = g_settings->getBool("smooth_lighting");
}

MeshUpdateQueue::~MeshUpdateQueue()
{
	MutexAutoLock lock(m_mutex);

	for(std::vector<QueuedMeshUpdate*>::iterator
			i = m_queue.begin();
			i != m_queue.end(); ++i)
	{
		QueuedMeshUpdate *q = *i;
		delete q;
	}
}

void MeshUpdateQueue::addBlock(Map *map, v3s16 p, bool ack_block_to_server, bool urgent)
{
	DSTACK(FUNCTION_NAME);

	MutexAutoLock lock(m_mutex);

	cleanupCache();

	/*
		Cache the block data (force-update the center block, don't update the
		neighbors but get them if they aren't already cached)
	*/
	std::vector<CachedMapBlockData*> cached_blocks;
	cached_blocks.reserve(3*3*3);
	v3s16 dp;
	for (dp.X = -1; dp.X <= 1; dp.X++) {
		for (dp.Y = -1; dp.Y <= 1; dp.Y++) {
			for (dp.Z = -1; dp.Z <= 1; dp.Z++) {
				v3s16 p1 = p + dp;
				CachedMapBlockData *cached_block;
				if (dp == v3s16(0, 0, 0))
					cached_block = cacheBlock(map, p1, FORCE_UPDATE);
				else
					cached_block = cacheBlock(map, p1, SKIP_UPDATE_IF_ALREADY_CACHED);
				cached_blocks.push_back(cached_block);
			}
		}
	}

	/*
		Mark the block as urgent if requested
	*/
	if(urgent)
		m_urgents.insert(p);

	/*
		Find if block is already in queue.
		If it is, update the data and quit.
	*/
	for(std::vector<QueuedMeshUpdate*>::iterator
			i = m_queue.begin();
			i != m_queue.end(); ++i)
	{
		QueuedMeshUpdate *q = *i;
		if(q->p == p)
		{
			// NOTE: We are not adding a new position to the queue, thus
			//       refcount_from_queue stays the same.
			if(ack_block_to_server)
				q->ack_block_to_server = true;
			q->crack_level = m_client->getCrackLevel();
			q->crack_pos = m_client->getCrackPos();
			return;
		}
	}

	/*
		Add the block
	*/
	QueuedMeshUpdate *q = new QueuedMeshUpdate;
	q->p = p;
	q->ack_block_to_server = ack_block_to_server;
	q->crack_level = m_client->getCrackLevel();
	q->crack_pos = m_client->getCrackPos();
	m_queue.push_back(q);

	// This queue entry is a new reference to the cached blocks
	for (size_t i=0; i<cached_blocks.size(); i++) {
		cached_blocks[i]->refcount_from_queue++;
	}
}

// Returned pointer must be deleted
// Returns NULL if queue is empty
QueuedMeshUpdate *MeshUpdateQueue::pop()
{
	MutexAutoLock lock(m_mutex);

	bool must_be_urgent = !m_urgents.empty();
	for(std::vector<QueuedMeshUpdate*>::iterator
			i = m_queue.begin();
			i != m_queue.end(); ++i)
	{
		QueuedMeshUpdate *q = *i;
		if(must_be_urgent && m_urgents.count(q->p) == 0)
			continue;
		m_queue.erase(i);
		m_urgents.erase(q->p);
		fillDataFromMapBlockCache(q);
		return q;
	}
	return NULL;
}

CachedMapBlockData* MeshUpdateQueue::cacheBlock(Map *map, v3s16 p, UpdateMode mode)
{
	std::map<v3s16, CachedMapBlockData*>::iterator it =
			m_cache.find(p);
	if (it != m_cache.end()) {
		// Already in cache
		CachedMapBlockData *cached_block = it->second;
		if (mode == SKIP_UPDATE_IF_ALREADY_CACHED)
			return cached_block;
		MapBlock *b = map->getBlockNoCreateNoEx(p);
		if (b) {
			if (cached_block->data == NULL)
				cached_block->data =
						new MapNode[MAP_BLOCKSIZE * MAP_BLOCKSIZE * MAP_BLOCKSIZE];
			memcpy(cached_block->data, b->getData(),
					MAP_BLOCKSIZE * MAP_BLOCKSIZE * MAP_BLOCKSIZE * sizeof(MapNode));
		} else {
			delete[] cached_block->data;
			cached_block->data = NULL;
		}
		return cached_block;
	} else {
		// Not yet in cache
		CachedMapBlockData *cached_block = new CachedMapBlockData();
		m_cache[p] = cached_block;
		MapBlock *b = map->getBlockNoCreateNoEx(p);
		if (b) {
			cached_block->data =
					new MapNode[MAP_BLOCKSIZE * MAP_BLOCKSIZE * MAP_BLOCKSIZE];
			memcpy(cached_block->data, b->getData(),
					MAP_BLOCKSIZE * MAP_BLOCKSIZE * MAP_BLOCKSIZE * sizeof(MapNode));
		}
		return cached_block;
	}
}

CachedMapBlockData* MeshUpdateQueue::getCachedBlock(const v3s16 &p)
{
	std::map<v3s16, CachedMapBlockData*>::iterator it = m_cache.find(p);
	if (it != m_cache.end()) {
		return it->second;
	}
	return NULL;
}

void MeshUpdateQueue::fillDataFromMapBlockCache(QueuedMeshUpdate *q)
{
	PROF_START

	MeshMakeData *data = new MeshMakeData(m_client, m_cache_enable_shaders,
			m_cache_use_tangent_vertices);
	q->data = data;

	data->fillBlockDataBegin(q->p);

	// Collect data for 3*3*3 blocks from cache
	v3s16 dp;
	for (dp.X = -1; dp.X <= 1; dp.X++) {
		for (dp.Y = -1; dp.Y <= 1; dp.Y++) {
			for (dp.Z = -1; dp.Z <= 1; dp.Z++) {
				v3s16 p = q->p + dp;
				CachedMapBlockData *cached_block = getCachedBlock(p);
				if (cached_block) {
					cached_block->refcount_from_queue--;
					if (cached_block->data)
						data->fillBlockData(dp, cached_block->data);
				}
			}
		}
	}

	data->setCrack(q->crack_level, q->crack_pos);
	data->setSmoothLighting(m_cache_smooth_lighting);

	PROF_ADD("MeshUpdateQueue::fillDataFromMapBlockCache")
}

void MeshUpdateQueue::cleanupCache()
{
	g_profiler->avg("MeshUpdateQueue MapBlock cache size", m_cache.size());

	const int cache_seconds = 10;
	int t_now = time(0);

	size_t num_blocks_erased = 0;
	for (std::map<v3s16, CachedMapBlockData*>::iterator it = m_cache.begin();
			it != m_cache.end(); ) {
		CachedMapBlockData *cached_block = it->second;
		if (cached_block->refcount_from_queue == 0 &&
				cached_block->last_used_timestamp < t_now - cache_seconds) {
			m_cache.erase(it++);
			num_blocks_erased++;
		} else {
			++it;
		}
	}
	g_profiler->add("MeshUpdateQueue MapBlock cache erased blocks", num_blocks_erased);
}

/*
	MeshUpdateThread
*/

MeshUpdateThread::MeshUpdateThread(Client *client):
	UpdateThread("Mesh"),
	m_queue_in(client)
{
	m_generation_interval = g_settings->getU16("mesh_generation_interval");
	m_generation_interval = rangelim(m_generation_interval, 0, 50);
}

void MeshUpdateThread::updateBlock(Map *map, v3s16 p, bool ack_block_to_server,
		bool urgent)
{
	// Allow the MeshUpdateQueue to do whatever it wants
	m_queue_in.addBlock(map, p, ack_block_to_server, urgent);
	deferUpdate();
}

void MeshUpdateThread::doUpdate()
{
	QueuedMeshUpdate *q;
	while ((q = m_queue_in.pop())) {
		if (m_generation_interval)
			sleep_ms(m_generation_interval);
		ScopeProfiler sp(g_profiler, "Client: Mesh making");

		MapBlockMesh *mesh_new = new MapBlockMesh(q->data, m_camera_offset);

		MeshUpdateResult r;
		r.p = q->p;
		r.mesh = mesh_new;
		r.ack_block_to_server = q->ack_block_to_server;

		m_queue_out.push_back(r);

		delete q;
	}
}
