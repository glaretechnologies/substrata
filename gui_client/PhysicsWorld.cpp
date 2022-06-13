/*=====================================================================
PhysicsWorld.cpp
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "PhysicsWorld.h"


#include <simpleraytracer/ray.h>
#include <utils/StringUtils.h>
#include <utils/ConPrint.h>
#include <utils/Timer.h>
#include <utils/HashMapInsertOnly2.h>


PhysicsWorld::PhysicsWorld()
:	ob_grid(/*cell_w=*/32.0, /*num_buckets=*/4096, /*expected_num_items_per_bucket=*/4, /*empty key=*/NULL),
	large_objects(/*empty key=*/NULL, /*expected num items=*/32)
{
}


PhysicsWorld::~PhysicsWorld()
{
}


static const int LARGE_OB_NUM_CELLS_THRESHOLD = 32;


void PhysicsWorld::setNewObToWorldMatrix(PhysicsObject& object, const Matrix4f& new_ob_to_world)
{
	// Compute world-to-ob matrix.
	const bool invertible = new_ob_to_world.getInverseForAffine3Matrix(/*inverse out=*/object.world_to_ob); 
	//assert(invertible);
	if(!invertible)
		object.world_to_ob = Matrix4f::identity(); // If not invertible, just use to-world matrix.  TEMP HACK


	if(large_objects.count(&object) > 0)
	{
		// Just keep in large objects.
	}
	else
	{
		const js::AABBox& old_aabb_ws = object.aabb_ws;
		const js::AABBox new_aabb_ws = object.geometry->getAABBox().transformedAABBFast(new_ob_to_world);

		// See if the object has changed grid cells
		const Vec4i old_min_bucket_i = ob_grid.bucketIndicesForPoint(old_aabb_ws.min_);
		const Vec4i old_max_bucket_i = ob_grid.bucketIndicesForPoint(old_aabb_ws.max_);

		const Vec4i new_min_bucket_i = ob_grid.bucketIndicesForPoint(new_aabb_ws.min_);
		const Vec4i new_max_bucket_i = ob_grid.bucketIndicesForPoint(new_aabb_ws.max_);

		if(new_min_bucket_i != old_min_bucket_i || new_max_bucket_i != old_max_bucket_i)
		{
			// cells have changed.
			ob_grid.remove(&object, old_aabb_ws);
			ob_grid.insert(&object, new_aabb_ws);
		}

		object.ob_to_world = new_ob_to_world;
		object.aabb_ws = new_aabb_ws;
	}
}


void PhysicsWorld::computeObjectTransformData(PhysicsObject& object)
{
	const Matrix4f& to_world = object.ob_to_world;

	const bool invertible = to_world.getInverseForAffine3Matrix(/*inverse out=*/object.world_to_ob); // Compute world-to-ob matrix.
	//assert(invertible);
	if(!invertible)
		object.world_to_ob = Matrix4f::identity(); // If not invertible, just use to-world matrix.  TEMP HACK

	object.aabb_ws = object.geometry->getAABBox().transformedAABBFast(to_world);
}


void PhysicsWorld::addObject(const Reference<PhysicsObject>& object)
{
	// Compute world space AABB of object
	computeObjectTransformData(*object.getPointer());

	const int num_cells = ob_grid.numCellsForAABB(object->aabb_ws);
	if(num_cells >= LARGE_OB_NUM_CELLS_THRESHOLD)
	{
		large_objects.insert(object.ptr());
	}
	else
	{
		ob_grid.insert(object.ptr(), object->aabb_ws);
	}

	this->objects_set.insert(object);
}


void PhysicsWorld::removeObject(const Reference<PhysicsObject>& object)
{
	auto res = large_objects.find(object.ptr());
	if(res != large_objects.end()) // If was in large_objects:
	{
		large_objects.erase(res);
	}
	else
	{
		ob_grid.remove(object.ptr(), object->aabb_ws);
	}

	this->objects_set.erase(object);
}


void PhysicsWorld::clear()
{
	this->large_objects.clear();
	this->ob_grid.clear();
	this->objects_set.clear();
}


PhysicsWorld::MemUsageStats PhysicsWorld::getTotalMemUsage() const
{
	HashMapInsertOnly2<const RayMesh*, int64> meshes(/*empty key=*/NULL, objects_set.size());
	MemUsageStats stats;
	stats.num_meshes = 0;
	stats.mem = 0;
	for(auto it = objects_set.begin(); it != objects_set.end(); ++it)
	{
		const PhysicsObject* ob = it->getPointer();

		const bool added = meshes.insert(std::make_pair(ob->geometry.ptr(), 0)).second;

		if(added)
		{
			stats.mem += ob->geometry->getTotalMemUsage();
			stats.num_meshes++;
		}
	}

	return stats;
}


std::string PhysicsWorld::getDiagnostics() const
{
	const MemUsageStats stats = getTotalMemUsage();
	std::string s;
	s += "Objects: " + toString(objects_set.size()) + "\n";
	s += "Meshes:  " + toString(stats.num_meshes) + "\n";
	s += "mem usage: " + getNiceByteSize(stats.mem) + "\n";

	size_t hashed_grid_mem_usage = ob_grid.buckets.size() * sizeof(*ob_grid.buckets.data());

	size_t sum_cell_num_buckets = 0;
	size_t sum_cell_num_obs = 0;
	for(size_t i=0; i<ob_grid.buckets.size(); ++i)
	{
		sum_cell_num_buckets += ob_grid.buckets[i].objects.buckets_size;
		sum_cell_num_obs += ob_grid.buckets[i].objects.size();
	}

	hashed_grid_mem_usage += sum_cell_num_buckets * sizeof(PhysicsObject*);

	const double av_cell_num_buckets = (double)sum_cell_num_buckets / (double)ob_grid.buckets.size();
	const double av_cell_num_obs     = (double)sum_cell_num_obs     / (double)ob_grid.buckets.size();

	s += "num buckets: " + toString(ob_grid.buckets.size()) + "\n";
	s += "av_cell_num_buckets: " + doubleToStringNSigFigs(av_cell_num_buckets, 3) + " obs\n";
	s += "av_cell_num_obs: " + doubleToStringNSigFigs(av_cell_num_obs, 3) + " obs\n";
	s += "hashed grid mem usage: " + getNiceByteSize(hashed_grid_mem_usage) + "\n";

	return s;
}


std::string PhysicsWorld::getLoadedMeshes() const
{
	std::string s;
	HashMapInsertOnly2<const RayMesh*, int64> meshes(/*empty key=*/NULL, objects_set.size());
	for(auto it = objects_set.begin(); it != objects_set.end(); ++it)
	{
		const PhysicsObject* ob = it->getPointer();
		const bool added = meshes.insert(std::make_pair(ob->geometry.ptr(), 0)).second;
		if(added)
		{
			s += ob->geometry->getName() + "\n";
		}
	}

	return s;
}


void PhysicsWorld::traceRay(const Vec4f& origin, const Vec4f& dir, float max_t, RayTraceResult& results_out) const
{
	results_out.hit_object = NULL;

	float closest_dist = std::numeric_limits<float>::infinity();

	Ray ray(origin, dir, 0.f, max_t);

	for(auto it = objects_set.begin(); it != objects_set.end(); ++it)
	{
		const PhysicsObject* object = it->ptr();

		RayTraceResult ob_results;
		object->traceRay(ray, ob_results);
		if(ob_results.hit_object && ob_results.hitdist_ws < closest_dist)
		{
			results_out = ob_results;
			results_out.hit_object = object;
			closest_dist = ob_results.hitdist_ws;

			ray.max_t = ob_results.hitdist_ws; // Now that we have hit something, we only need to consider closer hits.
		}
	}
}


bool PhysicsWorld::doesRayHitAnything(const Vec4f& origin, const Vec4f& dir, float max_t) const
{
	const Ray ray(origin, dir, 0.f, max_t);

	for(auto it = objects_set.begin(); it != objects_set.end(); ++it)
	{
		const PhysicsObject* object = it->ptr();

		RayTraceResult ob_results;
		object->traceRay(ray, ob_results);
		if(ob_results.hit_object)
			return true;
	}
	return false;
}


void PhysicsWorld::traceSphere(const js::BoundingSphere& sphere, const Vec4f& translation_ws, SphereTraceResult& results_out) const
{
	results_out.hit_object = NULL;

	// Compute AABB of sphere path in world space
	const Vec4f startpos_ws = sphere.getCenter();
	const Vec4f endpos_ws   = sphere.getCenter() + translation_ws;

	const float r = sphere.getRadius();
	const js::AABBox spherepath_aabb_ws(min(startpos_ws, endpos_ws) - Vec4f(r, r, r, 0), max(startpos_ws, endpos_ws) + Vec4f(r, r, r, 0));


	float closest_dist_ws = std::numeric_limits<float>::infinity();

	// Query large objects
	for(auto it = large_objects.begin(); it != large_objects.end(); ++it)
	{
		const PhysicsObject* object = *it;
		SphereTraceResult ob_results;
		object->traceSphere(sphere, translation_ws, spherepath_aabb_ws, ob_results);
		if(ob_results.hitdist_ws >= 0 && ob_results.hitdist_ws < closest_dist_ws)
		{
			results_out = ob_results;
			results_out.hit_object = object;
			closest_dist_ws = ob_results.hitdist_ws;
		}
	}


	// Query hashed grid
	const Vec4i min_bucket_i = ob_grid.bucketIndicesForPoint(spherepath_aabb_ws.min_);
	const Vec4i max_bucket_i = ob_grid.bucketIndicesForPoint(spherepath_aabb_ws.max_);

	int num_buckets_tested = 0;
	int num_obs_tested = 0;
	int num_obs_considered = 0;
	Timer timer;

	// Mailbox code can be used to prevent testing against the same object multiple times if it occupies multiple grid cells.
	// Not sure how needed it is though.
	//const int NUM_MAILBOXES = 16;
	//const PhysicsObject* mailboxes[NUM_MAILBOXES];
	//for(int i=0; i<NUM_MAILBOXES; ++i)
	//	mailboxes[i] = NULL;

	for(int x=min_bucket_i[0]; x <= max_bucket_i[0]; ++x)
	for(int y=min_bucket_i[1]; y <= max_bucket_i[1]; ++y)
	for(int z=min_bucket_i[2]; z <= max_bucket_i[2]; ++z)
	{
		const auto bucket = ob_grid.getBucketForIndices(x, y, z);

		for(auto it = bucket.objects.begin(); it != bucket.objects.end(); ++it)
		{
			const PhysicsObject* object = *it;
			//std::hash<const PhysicsObject*> hasher;
			//const size_t box = hasher(object) % NUM_MAILBOXES;
			//if(mailboxes[box] != object)
			{
				SphereTraceResult ob_results;
				object->traceSphere(sphere, translation_ws, spherepath_aabb_ws, ob_results);
				if(ob_results.hitdist_ws >= 0 && ob_results.hitdist_ws < closest_dist_ws)
				{
					results_out = ob_results;
					results_out.hit_object = object;
					closest_dist_ws = ob_results.hitdist_ws;
				}

				//mailboxes[box] = object;
				num_obs_tested++;
			}
			num_obs_considered++;
		}

		num_buckets_tested++;
	}

	//conPrint("traceSphere(): Testing against " + toString(num_buckets_tested) + " buckets, " + toString(num_obs_considered) + 
	//	" obs considered and " + toString(num_obs_tested) + " obs tested, took " + timer.elapsedStringNSigFigs(4));

	/*float closest_dist_ws = std::numeric_limits<float>::infinity();

	for(auto it = objects_set.begin(); it != objects_set.end(); ++it)
	{
		const PhysicsObject* object = it->ptr();

		SphereTraceResult ob_results;
		object->traceSphere(sphere, translation_ws, spherepath_aabb_ws, ob_results);
		if(ob_results.hitdist_ws >= 0 && ob_results.hitdist_ws < closest_dist_ws)
		{
			results_out = ob_results;
			results_out.hit_object = object;
			closest_dist_ws = ob_results.hitdist_ws;
		}
	}*/
}


void PhysicsWorld::getCollPoints(const js::BoundingSphere& sphere, std::vector<Vec4f>& points_out) const
{
	points_out.resize(0);

	const float r = sphere.getRadius();
	const js::AABBox sphere_aabb_ws(sphere.getCenter() - Vec4f(r, r, r, 0), sphere.getCenter() + Vec4f(r, r, r, 0));


	// Query large objects
	for(auto it = large_objects.begin(); it != large_objects.end(); ++it)
	{
		const PhysicsObject* object = *it;
		object->appendCollPoints(sphere, sphere_aabb_ws, points_out);
	}

	
	// Query hashed grid
	const Vec4i min_bucket_i = ob_grid.bucketIndicesForPoint(sphere_aabb_ws.min_);
	const Vec4i max_bucket_i = ob_grid.bucketIndicesForPoint(sphere_aabb_ws.max_);

	for(int x=min_bucket_i[0]; x <= max_bucket_i[0]; ++x)
	for(int y=min_bucket_i[1]; y <= max_bucket_i[1]; ++y)
	for(int z=min_bucket_i[2]; z <= max_bucket_i[2]; ++z)
	{
		const auto bucket = ob_grid.getBucketForIndices(x, y, z);

		for(auto it = bucket.objects.begin(); it != bucket.objects.end(); ++it)
		{
			const PhysicsObject* object = *it;
			object->appendCollPoints(sphere, sphere_aabb_ws, points_out);
		}
	}


	/*for(auto it = objects_set.begin(); it != objects_set.end(); ++it)
	{
		const PhysicsObject* object = it->ptr();
		object->appendCollPoints(sphere, sphere_aabb_ws, points_out);
	}*/
}


// TEMP: test iteration speed
/*{
	Timer timer;
	size_t num_collidable = 0;
	for(size_t i=0; i<objects.size(); ++i)
	{
		num_collidable += objects[i]->collidable ? 1 : 0;
	}
	conPrint("array iter:         " + timer.elapsedStringNSigFigs(5) + " (num_collidable=" + toString(num_collidable) + ")");
}
{
	Timer timer;
	size_t num_collidable = 0;
	for(auto it = objects_set.begin(); it != objects_set.end(); ++it)
	{
		num_collidable += (*it)->collidable ? 1 : 0;
	}
	conPrint("unordered_set iter: " + timer.elapsedStringNSigFigs(5) + " (num_collidable=" + toString(num_collidable) + ")");
}*/
