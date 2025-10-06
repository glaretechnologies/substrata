/*=====================================================================
DownloadingResourceQueue.cpp
----------------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "DownloadingResourceQueue.h"


#include <utils/ConPrint.h>
#include <utils/Timer.h>
#include <utils/Lock.h>
#include <utils/StringUtils.h>
#include <algorithm>


DownloadingResourceQueue::DownloadingResourceQueue()
:	begin_i(0)
{}


DownloadingResourceQueue::~DownloadingResourceQueue()
{
	for(size_t i = begin_i; i < items.size(); ++i)
		delete items[i];
}


void DownloadingResourceQueue::enqueueOrUpdateItem(/*const DownloadQueueItem& item*/const URLString& URL, const Vec4f& pos, float size_factor)
{
	assert(pos.isFinite());

	bool already_inserted;
	{
		Lock lock(mutex);

		auto res = item_URL_map.find(URL);
		if(res == item_URL_map.end()) // If not already inserted:
		{
			DownloadQueueItem* new_item = new DownloadQueueItem();
			new_item->URL = URL;
			new_item->pos_info.resize(1);
			new_item->pos_info[0].pos = Vec3f(pos);
			new_item->pos_info[0].size_factor = size_factor;
			
			item_URL_map[URL] = new_item;
			items.push_back(new_item);

			already_inserted = false;
		}
		else
		{
			DownloadQueueItem* existing_item = res->second;

			DownloadQueuePosInfo new_pos_info;
			new_pos_info.pos = Vec3f(pos);
			new_pos_info.size_factor = size_factor;
			existing_item->pos_info.push_back(new_pos_info);
			
			already_inserted = true;
		}
	}

	if(!already_inserted)
		nonempty.notify(); // Notify one or more suspended threads that there is an item in the queue.
}


size_t DownloadingResourceQueue::size() const
{
	Lock lock(mutex);
	return items.size() - begin_i;
}


struct QueueItemDistComparator
{
	bool operator () (const DownloadQueueItem* a, const DownloadQueueItem* b)
	{
		return a->priority < b->priority;
	}
};


void DownloadingResourceQueue::sortQueue(const Vec3d& campos_) // Sort queue
{
	// Sort download list by distance from camera
	const Vec4f campos_zero_w((float)campos_.x, (float)campos_.y, (float)campos_.z, 0.f);

	{
		Lock lock(mutex);

		Timer timer;

		QueueItemDistComparator comparator;

		// Do pass over queue items to compute priority, store and use that for sorting.
		const size_t items_size = items.size();
		for(size_t i = begin_i; i < items_size; ++i)
		{
			const SmallVector<DownloadQueuePosInfo, 4>& pos_info = items[i]->pos_info;
			assert(pos_info.size() >= 1);
			float smallest_priority = campos_zero_w.getDist(maskWToZero(loadUnalignedVec4f(&pos_info[0].pos.x))) * pos_info[0].size_factor;
			for(size_t z=1; z<pos_info.size(); ++z)
			{
				const float pos_info_z_priority = campos_zero_w.getDist(maskWToZero(loadUnalignedVec4f(&pos_info[z].pos.x))) * pos_info[z].size_factor;
				smallest_priority = myMin(smallest_priority, pos_info_z_priority);
			}

			items[i]->priority = smallest_priority;
		}

		std::sort(items.begin() + begin_i, items.end(), comparator);

		/*conPrint("Download queue: ");
		for(int i=(int)begin_i; i<(int)items.size(); ++i)
		{
			conPrint("item " + toString(i) + ": " + items[i].URL + ", (" + doubleToStringNSigFigs(items[i].pos.getDist(campos), 3) + " m away)");
		}*/

		// conPrint("!!!!Sorting download queue (" + toString(items.size() - begin_i) + " items) took " + timer.elapsedStringNSigFigs(4) + " (begin_i: " + toString(begin_i) + ")");
	}
}


void DownloadingResourceQueue::dequeueItemsWithTimeOut(double wait_time_seconds, size_t max_num_items, std::vector<DownloadQueueItem>& items_out)
{
	items_out.resize(0);

	Lock lock(mutex);

	if(begin_i < items.size()) // If there are any items in the queue:
	{
		for(size_t i=0; (i<max_num_items) && (begin_i < items.size()); ++i) // while we have removed <= max_num_items and there are still items in the queue:
		{
			items_out.push_back(*items[begin_i]);
			item_URL_map.erase(items[begin_i]->URL);
			delete items[begin_i];
			items[begin_i] = NULL;
			begin_i++;
		}
		return;
	}

	nonempty.waitWithTimeout(mutex, wait_time_seconds); // Suspend thread until there are (maybe) items in the queue

	for(size_t i=0; (i<max_num_items) && (begin_i < items.size()); ++i) // while we have removed <= max_num_items and there are still items in the queue:
	{
		items_out.push_back(*items[begin_i]);
		item_URL_map.erase(items[begin_i]->URL);
		delete items[begin_i];
		items[begin_i] = NULL;
		begin_i++;
	}
}


bool DownloadingResourceQueue::tryDequeueItem(DownloadQueueItem& item_out)
{
	Lock lock(mutex);

	if(begin_i < items.size()) // If there are any items in the queue:
	{
		item_out = *items[begin_i];
		item_URL_map.erase(items[begin_i]->URL);
		delete items[begin_i];
		items[begin_i] = NULL;
		begin_i++;
		return true;
	}
	else
		return false;
}
