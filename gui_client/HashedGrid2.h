/*=====================================================================
HashedGrid2.h
------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include <utils/Vector.h>
#include <utils/HashSet.h>


template <typename Key, typename HashFunc>
class HashedGrid2Bucket
{
public:
	HashedGrid2Bucket(const Key& empty_key, size_t expected_num_items) : objects(empty_key, expected_num_items) {}
	HashSet<Key, HashFunc> objects;
};


/*=====================================================================
HashedGrid2
-----------
=====================================================================*/
template <typename Key, typename HashFunc>
class HashedGrid2
{
public:
	HashedGrid2(float cell_w_, /*int expected_num_items,*/ size_t num_buckets, size_t expected_num_items_per_bucket, const Key& empty_key)
	:	cell_w(cell_w_),
		recip_cell_w(1 / cell_w_)
	{
		assert(expected_num_items > 0);
		assert(Maths::isPowerOfTwo(num_buckets));

		const HashedGrid2Bucket<Key, HashFunc> bucket(empty_key, expected_num_items_per_bucket);

		//const unsigned int num_buckets = myMax<unsigned int>(8, (unsigned int)Maths::roundToNextHighestPowerOf2((unsigned int)expected_num_items/* * 2*/));
		buckets.resize(num_buckets, bucket);

		hash_mask = (uint32)num_buckets - 1;
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

	inline int numCellsForAABB(const js::AABBox& aabb)
	{
		const Vec4i min_bucket_i = bucketIndicesForPoint(aabb.min_);
		const Vec4i max_bucket_i = bucketIndicesForPoint(aabb.max_);

		return
			(max_bucket_i[0] - min_bucket_i[0] + 1) *
			(max_bucket_i[1] - min_bucket_i[1] + 1) *
			(max_bucket_i[2] - min_bucket_i[2] + 1);
	}

	inline void insert(const Key& key, const js::AABBox& aabb)
	{
		const Vec4i min_bucket_i = bucketIndicesForPoint(aabb.min_);
		const Vec4i max_bucket_i = bucketIndicesForPoint(aabb.max_);

		for(int x=min_bucket_i[0]; x <= max_bucket_i[0]; ++x)
		for(int y=min_bucket_i[1]; y <= max_bucket_i[1]; ++y)
		for(int z=min_bucket_i[2]; z <= max_bucket_i[2]; ++z)
		{

			unsigned int bucket_i = computeHash(x, y, z);
			buckets[bucket_i].objects.insert(key);
		}
	}

	inline void remove(const Key& key, const js::AABBox& aabb)
	{
		const Vec4i min_bucket_i = bucketIndicesForPoint(aabb.min_);
		const Vec4i max_bucket_i = bucketIndicesForPoint(aabb.max_);

		for(int x=min_bucket_i[0]; x <= max_bucket_i[0]; ++x)
		for(int y=min_bucket_i[1]; y <= max_bucket_i[1]; ++y)
		for(int z=min_bucket_i[2]; z <= max_bucket_i[2]; ++z)
		{

			unsigned int bucket_i = computeHash(x, y, z);
			buckets[bucket_i].objects.erase(key);
		}
	}

	/*inline void queryObjects(const js::AABBox& aabb, js::Vector<Key, 16>& objects_out)
	{
		const Vec4i min_bucket_i = bucketIndicesForPoint(aabb.min_);
		const Vec4i max_bucket_i = bucketIndicesForPoint(aabb.max_);

		for(int x=min_bucket_i[0]; x <= max_bucket_i[0]; ++x)
		for(int y=min_bucket_i[1]; y <= max_bucket_i[1]; ++y)
		for(int z=min_bucket_i[2]; z <= max_bucket_i[2]; ++z)
		{
			if(
			unsigned int bucket_i = getBucketForIndices(x, y, z);
			buckets[bucket_i].objects.erase(key);
		}
	}*/

	/*inline const HashedObGridBucket& getBucketForIndices(const Vec4i& p) const
	{
		return buckets[computeHash(p)];
	}*/

	inline const HashedGrid2Bucket<Key, HashFunc>& getBucketForIndices(const int x, const int y, const int z) const
	{
		return buckets[computeHash(x, y, z)];
	}

	//inline unsigned int getBucketIndexForPoint(const Vec4f& p) const
	//{
	//	const Vec4i p_i = floorToVec4i(p * recip_cell_w); //truncateToVec4i((p - grid_aabb.min_) * recip_cell_w);

	//	return computeHash(p_i);
	//}


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
	js::Vector<HashedGrid2Bucket<Key, HashFunc>, 16> buckets;
	uint32 hash_mask; // hash_mask = buckets_size - 1;
};
