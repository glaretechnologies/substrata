/*=====================================================================
DownloadingResourceQueue.h
--------------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include <Platform.h>
#include <Mutex.h>
#include <Condition.h>
#include <Vector.h>
#include <physics/jscol_aabbox.h>
#include <maths/Vec4.h>
#include <maths/vec3.h>
#include <vector>


struct DownloadQueueItem
{
	GLARE_ALIGNED_16_NEW_DELETE

	static float sizeFactorForAABBWS(const js::AABBox& aabb_ws)
	{
		// object projected angle    theta ~= aabb_ws.longestLength() / ob_dist
		
		// We will sort in ascending order by 1 / theta so that objects with a larger projected angle are loaded first.

		// 1 / theta = 1 / (aabb_ws.longestLength() / ob_dist) = ob_dist / aabb_ws.longestLength()

		const float min_len = 0.5f; // Objects smaller than 0.5m are considered just as important as 0.5m wide objects.

		return 1.f / myMax(min_len, aabb_ws.longestLength());
	}

	Vec4f pos;
	float size_factor;
	std::string URL;
};


/*=====================================================================
DownloadingResourceQueue
------------------------

=====================================================================*/
class DownloadingResourceQueue
{
public:
	DownloadingResourceQueue();
	~DownloadingResourceQueue();

	void enqueueItem(const DownloadQueueItem& item);

	void sortQueue(const Vec3d& campos); // Sort queue (by item distance to camera)

	void dequeueItemsWithTimeOut(double wait_time_s, size_t max_num_items, std::vector<DownloadQueueItem>& items_out); // Blocks for up to wait_time_s
private:

	Mutex mutex;
	Condition nonempty;
	size_t begin_i;
	js::Vector<DownloadQueueItem, 16> items;
};
