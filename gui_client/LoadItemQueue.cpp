/*=====================================================================
LoadItemQueue.cpp
-----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "LoadItemQueue.h"


#include "../shared/WorldObject.h"
#include "../shared/Avatar.h"
#include <ConPrint.h>
#include <Timer.h>
#include <Lock.h>
#include <StringUtils.h>
#include <algorithm>


LoadItemQueue::LoadItemQueue()
:	begin_i(0)
{}


LoadItemQueue::~LoadItemQueue()
{}


void LoadItemQueue::enqueueItem(const WorldObject& ob, const glare::TaskRef& task)
{
	enqueueItem(ob.pos, LoadItemQueueItem::sizeFactorForAABBWS(ob.aabb_ws), task);
}

void LoadItemQueue::enqueueItem(const Avatar& ob, const glare::TaskRef& task)
{
	// approx AABB of avatar
	const js::AABBox aabb_ws( 
		ob.pos.toVec4fPoint() - Vec4f(0.3f, 0.3f, -2.f, 0),
		ob.pos.toVec4fPoint() + Vec4f(0.3f, 0.3f, 0.2f, 0)
	);

	enqueueItem(ob.pos, LoadItemQueueItem::sizeFactorForAABBWS(aabb_ws), task);
}


void LoadItemQueue::enqueueItem(const Vec3d& pos, float size_factor, const glare::TaskRef& task)
{
	assert(pos.isFinite());

	LoadItemQueueItem item;
	item.pos = pos.toVec4fPoint();
	item.size_factor = size_factor;
	item.task = task;

	items.push_back(item);
}



size_t LoadItemQueue::size() const
{
	return items.size() - begin_i;
}


void LoadItemQueue::clear()
{
	begin_i = 0;
	items.clear();
}


struct QueueItemDistComparator
{
	bool operator () (const LoadItemQueueItem& a, const LoadItemQueueItem& b)
	{
		const float a_priority = a.pos.getDist(campos) * a.size_factor;
		const float b_priority = b.pos.getDist(campos) * b.size_factor;
		return a_priority < b_priority;
	}

	Vec4f campos;
};


void LoadItemQueue::sortQueue(const Vec3d& campos_) // Sort queue
{
	// Sort list by distance from camera
	const Vec4f campos((float)campos_.x, (float)campos_.y, (float)campos_.z, 1.f);

	//Timer timer;

	QueueItemDistComparator comparator;
	comparator.campos = campos;

	std::sort(items.begin() + begin_i, items.end(), comparator);

	//conPrint("!!!!Sorting load item queue (" + toString(items.size() - begin_i) + " items) took " + timer.elapsedStringNSigFigs(4));

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
		const size_t items_size = items.size();
		for(size_t i = begin_i; i < items_size; ++i)
			items[i - begin_i] = items[i];
		items.resize(items.size() - begin_i);
		begin_i = 0;
	}
}


glare::TaskRef LoadItemQueue::dequeueFront()
{
	assert(begin_i < items.size());

	return items[begin_i++].task;
}
