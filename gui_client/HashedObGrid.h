/*=====================================================================
HashedObGrid.h
--------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "../shared/WorldObject.h"
#include <unordered_set>



class HashedObGridBucket
{
public:
	//SmallVector<WorldObjectRef, 2> objects;
	std::unordered_set<WorldObjectRef, WorldObjectRefHash> objects;
};


/*=====================================================================
HashedObGrid
------------
TODO: Try using open addressing (don't use a vector in buckets, spill into adjacent buckets)
=====================================================================*/
class HashedObGrid
{
public:
	HashedObGrid(float cell_w_, int expected_num_items)
	:	cell_w(cell_w_),
		recip_cell_w(1 / cell_w_)
	{
		assert(expected_num_items > 0);

		const unsigned int num_buckets = myMax<unsigned int>(8, (unsigned int)Maths::roundToNextHighestPowerOf2((unsigned int)expected_num_items/* * 2*/));
		buckets.resize(num_buckets);

		hash_mask = num_buckets - 1;
	}

	inline void clear()
	{
		for(size_t i = 0; i < buckets.size(); ++i)
			buckets[i].objects.clear();
	}

	inline Vec4i bucketIndicesForPoint(const Vec4f& p) const
	{
		return floorToVec4i(p * recip_cell_w);
	}

	inline void insert(const WorldObjectRef& ob)
	{
		unsigned int bucket_i = getBucketIndexForPoint(ob->pos.toVec4fPoint());
		//buckets[bucket_i].objects.push_back(ob);
		buckets[bucket_i].objects.insert(ob);
	}

	inline void remove(const WorldObjectRef& ob)
	{
		unsigned int bucket_i = getBucketIndexForPoint(ob->pos.toVec4fPoint());
		// Find item
		/*for(size_t i=0; i<buckets[bucket_i].objects.size(); ++i)
		{
			if(buckets[bucket_i].objects[i] == ob)
			{
				buckets[bucket_i].objects.erase(buckets[bucket_i].objects.begin() + i);
				return;
			}
		}*/
		buckets[bucket_i].objects.erase(ob);
	}

#if GUI_CLIENT
	inline void removeAtLastPos(const WorldObjectRef& ob)
	{
		//unsigned int bucket_i = getBucketIndexForPoint(ob->last_pos.toVec4fPoint());
		//buckets[bucket_i].objects.erase(ob);
	}
#endif


	inline const HashedObGridBucket& getBucketForIndices(const Vec4i& p) const
	{
		return buckets[computeHash(p)];
	}

	inline const HashedObGridBucket& getBucketForIndices(const int x, const int y, const int z) const
	{
		return buckets[computeHash(x, y, z)];
	}

	inline unsigned int getBucketIndexForPoint(const Vec4f& p) const
	{
		const Vec4i p_i = floorToVec4i(p * recip_cell_w); //truncateToVec4i((p - grid_aabb.min_) * recip_cell_w);

		return computeHash(p_i);
	}


	inline unsigned int computeHash(const Vec4i& p_i) const
	{
		// NOTE: technically possible undefined behaviour here (signed overflow)

		//unsigned int u[4];
		//storeVec4i(p_i, u);

		return ((p_i[0] * 73856093) ^ (p_i[1] * 19349663) ^ (p_i[2] * 83492791)) & hash_mask;
	}

	inline unsigned int computeHash(int x, int y, int z) const
	{
		// NOTE: technically possible undefined behaviour here (signed overflow)

		return ((x * 73856093) ^ (y * 19349663) ^ (z * 83492791)) & hash_mask;
	}


	float recip_cell_w;
	float cell_w;
	std::vector<HashedObGridBucket> buckets;
	uint32 hash_mask; // hash_mask = buckets_size - 1;
};
