/*=====================================================================
DownloadingResourceQueue.h
--------------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "../shared/URLString.h"
#include <physics/jscol_aabbox.h>
#include <utils/Platform.h>
#include <utils/Mutex.h>
#include <utils/Condition.h>
#include <utils/Vector.h>
#include <utils/SmallVector.h>
#include <maths/Vec4.h>
#include <maths/vec3.h>
#include <vector>
#include <unordered_map>


struct DownloadQueuePosInfo
{
	Vec3f pos;
	float size_factor;
};

struct DownloadQueueItem
{
	GLARE_ALIGNED_16_NEW_DELETE

	static float sizeFactorForAABBWS(float aabb_ws_longest_len)
	{
		// object projected angle    theta ~= aabb_ws.longestLength() / ob_dist
		
		// We will sort in ascending order by 1 / theta so that objects with a larger projected angle are loaded first.

		// 1 / theta = 1 / (aabb_ws.longestLength() / ob_dist) = ob_dist / aabb_ws.longestLength()

		const float min_len = 1.0f; // Objects smaller than 1 m are considered just as important as 1 m wide objects.

		return 1.f / myMax(min_len, aabb_ws_longest_len);
	}

	SmallVector<DownloadQueuePosInfo, 4> pos_info; // Store multiple positions and size factors, since multiple different objects may be using the same resource.
	URLString URL;

	float priority;
};


/*=====================================================================
DownloadingResourceQueue
------------------------
Queue of resource URLs to download, together with the position of the object using the resource,
which is used for sorting the items based on distance from the camera.

DownloadResourcesThreads will dequeue items from this queue.
=====================================================================*/
class DownloadingResourceQueue
{
public:
	DownloadingResourceQueue();
	~DownloadingResourceQueue();

	void enqueueOrUpdateItem(const URLString& URL, const Vec4f& pos, float size_factor); // Adds item to queue if it is not already in queue.

	size_t size() const;

	void sortQueue(const Vec3d& campos); // Sort queue (approximately by item distance to camera)

	void dequeueItemsWithTimeOut(double wait_time_s, size_t max_num_items, std::vector<DownloadQueueItem>& items_out); // Blocks for up to wait_time_s

	bool tryDequeueItem(DownloadQueueItem& item_out);
private:

	mutable Mutex mutex;
	Condition nonempty;
	size_t begin_i										GUARDED_BY(mutex);
	js::Vector<DownloadQueueItem*, 16> items			GUARDED_BY(mutex);
	std::unordered_map<URLString, DownloadQueueItem*> item_URL_map	GUARDED_BY(mutex); // Map from item URL to pointer to DownloadQueueItem in items.
};
