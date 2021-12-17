/*=====================================================================
DownloadingResourceQueue.cpp
----------------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "DownloadingResourceQueue.h"


#include <ConPrint.h>
#include <Timer.h>
#include <Lock.h>
#include <StringUtils.h>
#include <algorithm>


DownloadingResourceQueue::DownloadingResourceQueue()
:	begin_i(0)
{}


DownloadingResourceQueue::~DownloadingResourceQueue()
{}


void DownloadingResourceQueue::enqueueItem(const DownloadQueueItem& item/*const Vec4f& pos, const std::string& URL*/)
{
	{
		Lock lock(mutex);

		items.push_back(item);
	}

	nonempty.notify(); // Notify one or more suspended threads that there is an item in the queue.
}


size_t DownloadingResourceQueue::size() const
{
	Lock lock(mutex);
	return items.size() - begin_i;
}


struct QueueItemDistComparator
{
	bool operator () (const DownloadQueueItem& a, const DownloadQueueItem& b)
	{
		const float a_priority = a.pos.getDist(campos) * a.size_factor;
		const float b_priority = b.pos.getDist(campos) * b.size_factor;
		return a_priority < b_priority;
	}

	Vec4f campos;
};


void DownloadingResourceQueue::sortQueue(const Vec3d& campos_) // Sort queue
{
	// Sort download list by distance from camera
	const Vec4f campos((float)campos_.x, (float)campos_.y, (float)campos_.z, 1.f);

	{
		Lock lock(mutex);

		//Timer timer;

		QueueItemDistComparator comparator;
		comparator.campos = campos;

		std::sort(items.begin() + begin_i, items.end(), comparator);


		/*conPrint("Download queue: ");
		for(int i=(int)begin_i; i<(int)items.size(); ++i)
		{
			conPrint("item " + toString(i) + ": " + items[i].URL + ", (" + doubleToStringNSigFigs(items[i].pos.getDist(campos), 3) + " m away)");
		}*/

		//conPrint("!!!!Sorting download queue (" + toString(items.size() - begin_i) + " items) took " + timer.elapsedStringNSigFigs(4));
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
			items_out.push_back(items[begin_i]);
			begin_i++;
		}
		return;
	}

	nonempty.waitWithTimeout(mutex, wait_time_seconds); // Suspend thread until there are (maybe) items in the queue

	for(size_t i=0; (i<max_num_items) && (begin_i < items.size()); ++i) // while we have removed <= max_num_items and there are still items in the queue:
	{
		items_out.push_back(items[begin_i]);
		begin_i++;
	}
}
