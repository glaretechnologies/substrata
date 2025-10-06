/*=====================================================================
LoadItemQueue.cpp
-----------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "LoadItemQueue.h"


#include "../shared/WorldObject.h"
#include "../shared/Avatar.h"
#include <opengl/OpenGLMeshRenderData.h>
#include <graphics/BatchedMesh.h>
#include <utils/ConPrint.h>
#include <utils/Timer.h>
#include <utils/Lock.h>
#include <utils/StringUtils.h>
#include <algorithm>


LoadItemQueue::LoadItemQueue()
:	begin_i(0)
{}


LoadItemQueue::~LoadItemQueue()
{}


void LoadItemQueue::enqueueItem(const URLString& key, const WorldObject& ob, const glare::TaskRef& task, float task_max_dist)
{
	enqueueItem(key, ob.getCentroidWS(), LoadItemQueueItem::sizeFactorForAABBWS(ob.getAABBWSLongestLength(), /*importance_factor=*/1.f), task, task_max_dist);
}


void LoadItemQueue::enqueueItem(const URLString& key, const Avatar& avatar, const glare::TaskRef& task, float task_max_dist, bool our_avatar)
{
	// Prioritise laoding our avatar first
	const float our_avatar_importance_factor = our_avatar ? 1.0e4f : 1.f;

	enqueueItem(key, avatar.pos.toVec4fPoint(), LoadItemQueueItem::sizeFactorForAABBWS(/*aabb_ws_longest_len=*/1.8f, our_avatar_importance_factor), task, task_max_dist);
}


void LoadItemQueue::enqueueItem(const URLString& key, const Vec4f& pos, float aabb_ws_longest_len, const glare::TaskRef& task, float task_max_dist, float importance_factor)
{
	enqueueItem(key, pos, LoadItemQueueItem::sizeFactorForAABBWS(aabb_ws_longest_len, importance_factor), task, task_max_dist);
}


void LoadItemQueue::enqueueItem(const URLString& key, const Vec4f& pos, float size_factor, const glare::TaskRef& task, float task_max_dist)
{
	assert(pos.isFinite());

	LoadItemQueueItem* item = new LoadItemQueueItem();
	item->pos_info.resize(1);
	item->pos_info[0].pos = Vec3f(pos);
	item->pos_info[0].size_factor = size_factor;
	item->key = key;
	item->task = task;
	item->task_max_dist = task_max_dist;

	items.push_back(item);

	item_map.insert(std::make_pair(key, item));
}


void LoadItemQueue::checkUpdateItemPosition(const URLString& key, const WorldObject& ob)
{
	checkUpdateItemPosition(key, ob.getCentroidWS(), LoadItemQueueItem::sizeFactorForAABBWS(ob.getAABBWSLongestLength(), /*importance_factor=*/1.f));
}


void LoadItemQueue::checkUpdateItemPosition(const URLString& key, const Avatar& avatar, bool our_avatar)
{
	// Prioritise loading our avatar first
	const float our_avatar_importance_factor = our_avatar ? 1.0e4f : 1.f;

	checkUpdateItemPosition(key, avatar.pos.toVec4fPoint(), LoadItemQueueItem::sizeFactorForAABBWS(/*aabb_ws_longest_len=*/1.8f, our_avatar_importance_factor));
}


void LoadItemQueue::checkUpdateItemPosition(const URLString& key, const Vec4f& pos, float aabb_ws_longest_len, float importance_factor)
{
	checkUpdateItemPosition(key, pos, LoadItemQueueItem::sizeFactorForAABBWS(aabb_ws_longest_len, importance_factor));
}


void LoadItemQueue::checkUpdateItemPosition(const URLString& key, const Vec4f& pos, float size_factor)
{
	auto res = item_map.find(key);
	if(res != item_map.end())
	{
		LoadItemQueueItem* existing_item = res->second;

		LoadItemQueuePosInfo new_pos_info;
		new_pos_info.pos = Vec3f(pos);
		new_pos_info.size_factor = size_factor;
		existing_item->pos_info.push_back(new_pos_info);
	}
}


size_t LoadItemQueue::size() const
{
	return items.size() - begin_i;
}


void LoadItemQueue::clear()
{
	for(size_t i = begin_i; i < items.size(); ++i)
		delete items[i];

	begin_i = 0;
	items.clear();
	item_map.clear();
}


struct QueueItemDistComparator
{
	bool operator () (const LoadItemQueueItem* a, const LoadItemQueueItem* b)
	{
		return a->priority < b->priority;
	}
};


void LoadItemQueue::sortQueue(const Vec3d& campos_) // Sort queue
{
	// Sort items into ascending order of priority: roughly distance from camera
	
	const Vec4f campos_zero_w((float)campos_.x, (float)campos_.y, (float)campos_.z, 0.f);

	Timer timer;

	// Do pass over queue items to compute priority, store and use that for sorting.
	const size_t items_size = items.size();
	for(size_t i = begin_i; i < items_size; ++i)
	{
		SmallVector<LoadItemQueuePosInfo, 4>& pos_info = items[i]->pos_info;
		assert(pos_info.size() >= 1);
		float smallest_priority = campos_zero_w.getDist(maskWToZero(loadUnalignedVec4f(&pos_info[0].pos.x))) * pos_info[0].size_factor;
		for(size_t z=1; z<pos_info.size(); ++z)
		{
			const float pos_info_z_priority = campos_zero_w.getDist(maskWToZero(loadUnalignedVec4f(&pos_info[z].pos.x))) * pos_info[z].size_factor;
			smallest_priority = myMin(smallest_priority, pos_info_z_priority);
		}

		items[i]->priority = smallest_priority;
	}

	QueueItemDistComparator comparator;
	std::sort(items.begin() + begin_i, items.end(), comparator);

	//conPrint("\n!!!!Sorting load item queue (" + toString(items.size() - begin_i) + " items) took " + timer.elapsedStringNSigFigs(4));

	/*
	If the unused space at the start of the array gets too large, copy items back to the start of the array, and trim off the end.
	                 begin_i                    end
	---------------------------------------------
	   unused          |         data           |
	---------------------------------------------
	
	|
	v

	begin_i                  end
	--------------------------
	|         data           |
	--------------------------
	
	*/
	if(begin_i > 1000)
	{
		//conPrint("LoadItemQueue::sortQueue(): copying backwards (begin_i: " + toString(begin_i) + ")");
		for(size_t i = begin_i; i < items_size; ++i)
			items[i - begin_i] = items[i];
		items.resize(items.size() - begin_i);
		begin_i = 0;
	}
}


void LoadItemQueue::dequeueFront(LoadItemQueueItem& item_out)
{
	assert(begin_i < items.size());

	item_map.erase(items[begin_i]->key);

	item_out = *items[begin_i]; // Copy to item_out

	delete items[begin_i];
	items[begin_i] = NULL;
	begin_i++;
}


float LoadItemQueueItem::getDistanceToCamera(const Vec4f& cam_pos_) const
{
	const Vec4f cam_pos_zero_w = maskWToZero(cam_pos_);
	float smallest_dist = std::numeric_limits<float>::infinity();
	for(size_t i=0; i<pos_info.size(); ++i)
		smallest_dist = myMin(smallest_dist, maskWToZero(loadUnalignedVec4f(&pos_info[i].pos.x)).getDist(cam_pos_zero_w));
	return smallest_dist;
}
