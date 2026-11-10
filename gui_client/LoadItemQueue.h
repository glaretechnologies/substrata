/*=====================================================================
LoadItemQueue.h
---------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "../shared/URLString.h"
#include "PhysicsObject.h"
#include <physics/jscol_aabbox.h>
#include <maths/Vec4.h>
#include <maths/vec3.h>
#include <utils/Platform.h>
#include <utils/Vector.h>
#include <utils/Task.h>
#include <utils/SmallVector.h>
#include <vector>
#include <unordered_map>
class WorldObject;
class Avatar;


struct LoadItemQueueKey
{
	// A URL can need several independent loads, e.g. a streaming MP3 and a decoded copy, or a mesh with different physics shape types to build.
	// We want to allow multiple load items in the queue for the independent loads, so we need the keys to distinguish them.
	enum Type
	{
		Type_Model,
		Type_Texture,
		Type_Audio,
		Type_Script
	};

	LoadItemQueueKey()                                                                        :            type(Type_Model), shape_type(PhysicsObject::ShapeType_tri_mesh), mem_map_file(false)         {}
	LoadItemQueueKey(const URLString& url_, Type type_)                                       : url(url_), type(type_),      shape_type(PhysicsObject::ShapeType_tri_mesh), mem_map_file(false)         {} // For Type_Texture, Type_Script
	LoadItemQueueKey(const URLString& url_, Type type_, PhysicsObject::ShapeType shape_type_) : url(url_), type(type_),      shape_type(shape_type_)                      , mem_map_file(false)         {} // For Type_Model
	LoadItemQueueKey(const URLString& url_, Type type_, bool mem_map_file_)                   : url(url_), type(type_),      shape_type(PhysicsObject::ShapeType_tri_mesh), mem_map_file(mem_map_file_) {} // For Type_Audio

	bool operator == (const LoadItemQueueKey& other) const { return url == other.url && type == other.type && shape_type == other.shape_type && mem_map_file == other.mem_map_file; }

	URLString url;
	Type type;
	PhysicsObject::ShapeType shape_type; // for Type_Model
	bool mem_map_file; // for Type_Audio
};

struct LoadItemQueueKeyHasher
{
	size_t operator() (const LoadItemQueueKey& key) const
	{
		const size_t variant = ((size_t)key.type << 8) | ((size_t)key.shape_type << 1) | (size_t)key.mem_map_file;
		return URLStringHasher()(key.url) ^ (variant * (size_t)0x9E3779B1u);
	}
};


struct LoadItemQueuePosInfo
{
	Vec3f pos;
	float size_factor;
};


struct LoadItemQueueItem
{
	GLARE_ALIGNED_16_NEW_DELETE

	static float sizeFactorForAABBWS(float aabb_ws_longest_len, float importance_factor)
	{
		// object projected angle    theta ~= aabb_ws.longestLength() / ob_dist
		
		// We will sort in ascending order by 1 / theta so that objects with a larger projected angle are loaded first.

		// 1 / theta = 1 / (aabb_ws.longestLength() / ob_dist) = ob_dist / aabb_ws.longestLength()

		const float min_len = 1.f; // Objects smaller than 1 m are considered just as important as 1 m wide objects.

		return 1.f / (myMax(min_len, aabb_ws_longest_len) * importance_factor);
	}

	float getDistanceToCamera(const Vec4f& cam_pos_) const; // Get distance from camera to closest position stored in pos_info.

	SmallVector<LoadItemQueuePosInfo, 4> pos_info; // Store multiple positions and size factors, since multiple different objects may be using the same resource.
	LoadItemQueueKey key;
	float task_max_dist; // Max distance from camera before task should be discarded.
	glare::TaskRef task;
	
	float priority;
};


/*=====================================================================
LoadItemQueue
-------------
Queue of load model tasks, load texture tasks etc, together with the position of the item,
which is used for sorting the tasks based on distance from the camera.
=====================================================================*/
class LoadItemQueue
{
public:
	LoadItemQueue();
	~LoadItemQueue();

	void enqueueItem(const LoadItemQueueKey& key, const WorldObject& ob, const glare::TaskRef& task, float task_max_dist);
	void enqueueItem(const LoadItemQueueKey& key, const Avatar& avatar, const glare::TaskRef& task, float task_max_dist);
	void enqueueItem(const LoadItemQueueKey& key, const Vec4f& pos, float aabb_ws_longest_len, const glare::TaskRef& task, float task_max_dist, float importance_factor);
	void enqueueItem(const LoadItemQueueKey& key, const Vec4f& pos, float size_factor, const glare::TaskRef& task, float task_max_dist);

	void checkUpdateItemPosition(const LoadItemQueueKey& key, const WorldObject& ob);
	void checkUpdateItemPosition(const LoadItemQueueKey& key, const Avatar& ob);
	void checkUpdateItemPosition(const LoadItemQueueKey& key, const Vec4f& pos, float aabb_ws_longest_len, float importance_factor);
	void checkUpdateItemPosition(const LoadItemQueueKey& key, const Vec4f& pos, float size_factor);

	void clear();

	bool empty() const { return size() == 0; }

	size_t size() const;

	void sortQueue(const Vec3d& campos); // Sort queue (approximately by item distance to camera)

	void dequeueFront(LoadItemQueueItem& item_out);
private:
	// Valid items are at indices [begin_i, items.size())
	size_t begin_i;
	js::Vector<LoadItemQueueItem*, 16> items;

	std::unordered_map<LoadItemQueueKey, LoadItemQueueItem*, LoadItemQueueKeyHasher> item_map;
};
