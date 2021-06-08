/*=====================================================================
PhysicsWorld.cpp
----------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#include "PhysicsWorld.h"


#include <simpleraytracer/ray.h>
#include <utils/StringUtils.h>
#include <utils/ConPrint.h>
#include <utils/Timer.h>


PhysicsWorld::PhysicsWorld()
{
}


PhysicsWorld::~PhysicsWorld()
{
}


void PhysicsWorld::updateObjectTransformData(PhysicsObject& object)
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
	updateObjectTransformData(*object.getPointer());
	
	//this->objects.push_back(object);
	this->objects_set.insert(object);
}


void PhysicsWorld::removeObject(const Reference<PhysicsObject>& object)
{
	// NOTE: linear time
	/*for(size_t i=0; i<objects.size(); ++i)
		if(objects[i].getPointer() == object.getPointer())
		{
			objects.erase(i);
			break;
		}*/

	this->objects_set.erase(object);
}

void PhysicsWorld::rebuild(glare::TaskManager& task_manager, PrintOutput& print_output)
{
	//conPrint("PhysicsWorld::rebuild()");
	
	// object_bvh is not used currently.

	//object_bvh.objects.resizeNoCopy(objects.size());
	//for(size_t i=0; i<objects.size(); ++i)
	//	object_bvh.objects[i] = objects[i].getPointer();

	//object_bvh.build(task_manager, print_output, 
	//	false // verbose
	//);
}


size_t PhysicsWorld::getTotalMemUsage() const
{
	size_t sum = 0;
	for(auto it = objects_set.begin(); it != objects_set.end(); ++it)
		sum += (*it)->getTotalMemUsage();

	sum += object_bvh.getTotalMemUsage();
	return sum;
}



void PhysicsWorld::traceRay(const Vec4f& origin, const Vec4f& dir, ThreadContext& thread_context, RayTraceResult& results_out) const
{
	results_out.hit_object = NULL;

	float closest_dist = std::numeric_limits<float>::infinity();

	const Ray ray(origin, dir, 0.f, std::numeric_limits<float>::infinity());

	for(auto it = objects_set.begin(); it != objects_set.end(); ++it)
	{
		const PhysicsObject* object = it->ptr();

		RayTraceResult ob_results;
		object->traceRay(ray, 1.0e30f, ob_results);
		if(ob_results.hit_object && ob_results.hitdist_ws >= 0 && ob_results.hitdist_ws < closest_dist)
		{
			results_out = ob_results;
			results_out.hit_object = object;
			closest_dist = ob_results.hitdist_ws;
		}
	}
}


void PhysicsWorld::traceSphere(const js::BoundingSphere& sphere, const Vec4f& translation_ws, ThreadContext& thread_context, RayTraceResult& results_out) const
{
	results_out.hit_object = NULL;

	// Compute AABB of sphere path in world space
	const Vec4f startpos_ws = sphere.getCenter();
	const Vec4f endpos_ws   = sphere.getCenter() + translation_ws;

	const float r = sphere.getRadius();
	const js::AABBox spherepath_aabb_ws(min(startpos_ws, endpos_ws) - Vec4f(r, r, r, 0), max(startpos_ws, endpos_ws) + Vec4f(r, r, r, 0));

	float closest_dist_ws = std::numeric_limits<float>::infinity();

	for(auto it = objects_set.begin(); it != objects_set.end(); ++it)
	{
		const PhysicsObject* object = it->ptr();

		RayTraceResult ob_results;
		object->traceSphere(sphere, translation_ws, spherepath_aabb_ws, ob_results);
		if(ob_results.hitdist_ws >= 0 && ob_results.hitdist_ws < closest_dist_ws)
		{
			results_out = ob_results;
			results_out.hit_object = object;
			closest_dist_ws = ob_results.hitdist_ws;
		}
	}
}


void PhysicsWorld::getCollPoints(const js::BoundingSphere& sphere, ThreadContext& thread_context, std::vector<Vec4f>& points_out) const
{
	points_out.resize(0);

	const float r = sphere.getRadius();
	const js::AABBox sphere_aabb_ws(sphere.getCenter() - Vec4f(r, r, r, 0), sphere.getCenter() + Vec4f(r, r, r, 0));

	for(auto it = objects_set.begin(); it != objects_set.end(); ++it)
	{
		const PhysicsObject* object = it->ptr();
		object->appendCollPoints(sphere, sphere_aabb_ws, points_out);
	}
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
