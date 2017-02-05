/*=====================================================================
PhysicsWorld.cpp
----------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#include "PhysicsWorld.h"


#include "../utils/stringutils.h"


PhysicsWorld::PhysicsWorld()
{
}


PhysicsWorld::~PhysicsWorld()
{
}


void PhysicsWorld::updateObjectTransformData(PhysicsObject& object)
{
	const Vec4f min_os = object.geometry->getAABBox().min_;
	const Vec4f max_os = object.geometry->getAABBox().max_;
	const Matrix4f& to_world = object.ob_to_world;

	to_world.getInverseForRandTMatrix(object.world_to_ob);

	js::AABBox bbox_ws = js::AABBox::emptyAABBox();
	bbox_ws.enlargeToHoldPoint(to_world * Vec4f(min_os.x[0], min_os.x[1], min_os.x[2], 1.0f));
	bbox_ws.enlargeToHoldPoint(to_world * Vec4f(min_os.x[0], min_os.x[1], max_os.x[2], 1.0f));
	bbox_ws.enlargeToHoldPoint(to_world * Vec4f(min_os.x[0], max_os.x[1], min_os.x[2], 1.0f));
	bbox_ws.enlargeToHoldPoint(to_world * Vec4f(min_os.x[0], max_os.x[1], max_os.x[2], 1.0f));
	bbox_ws.enlargeToHoldPoint(to_world * Vec4f(max_os.x[0], min_os.x[1], min_os.x[2], 1.0f));
	bbox_ws.enlargeToHoldPoint(to_world * Vec4f(max_os.x[0], min_os.x[1], max_os.x[2], 1.0f));
	bbox_ws.enlargeToHoldPoint(to_world * Vec4f(max_os.x[0], max_os.x[1], min_os.x[2], 1.0f));
	bbox_ws.enlargeToHoldPoint(to_world * Vec4f(max_os.x[0], max_os.x[1], max_os.x[2], 1.0f));
	object.aabb_ws = bbox_ws;
}



void PhysicsWorld::addObject(const Reference<PhysicsObject>& object)
{
	// Compute world space AABB of object
	updateObjectTransformData(*object.getPointer());
	
	this->objects.push_back(object);
}


void PhysicsWorld::removeObject(const Reference<PhysicsObject>& object)
{
	// NOTE: linear time

	for(size_t i=0; i<objects.size(); ++i)
		if(objects[i].getPointer() == object.getPointer())
		{
			objects.erase(i);
			break;
		}
}

void PhysicsWorld::build(Indigo::TaskManager& task_manager, PrintOutput& print_output)
{
	for(size_t i=0; i<objects.size(); ++i)
		object_bvh.objects.push_back(objects[i].getPointer());

	object_bvh.build(task_manager, print_output, 
		false // verbose
	);
}


void PhysicsWorld::traceRay(const Vec4f& origin, const Vec4f& dir, ThreadContext& thread_context, RayTraceResult& results_out) const
{
	results_out.hit_object = NULL;

	float closest_dist = std::numeric_limits<float>::infinity();

	const Ray ray(origin, dir, 0.f);

	for(size_t i=0; i<objects.size(); ++i)
	{
		RayTraceResult ob_results;
		objects[i]->traceRay(ray, 1.0e30f, thread_context, ob_results);
		if(ob_results.hit_object && ob_results.hitdist >= 0 && ob_results.hitdist < closest_dist)
		{
			results_out = ob_results;
			results_out.hit_object = objects[i].getPointer();
			closest_dist = ob_results.hitdist;
		}
	}
}


void PhysicsWorld::traceSphere(const js::BoundingSphere& sphere, const Vec4f& translation_ws, RayTraceResult& results_out) const
{
	results_out.hit_object = NULL;

	// Compute AABB of sphere path in world space
	const Vec4f startpos_ws = sphere.getCenter();
	const Vec4f endpos_ws   = sphere.getCenter() + translation_ws;

	js::AABBox spherepath_aabb_ws = js::AABBox::emptyAABBox();

	// Make it hold sphere at start pos
	const float r = sphere.getRadius();
	spherepath_aabb_ws.enlargeToHoldPoint(startpos_ws + Vec4f(0,  0,  r, 0.f));
	spherepath_aabb_ws.enlargeToHoldPoint(startpos_ws + Vec4f(0,  0, -r, 0.f));
	spherepath_aabb_ws.enlargeToHoldPoint(startpos_ws + Vec4f(0,  r,  0, 0.f));
	spherepath_aabb_ws.enlargeToHoldPoint(startpos_ws + Vec4f(0,  -r, 0, 0.f));
	spherepath_aabb_ws.enlargeToHoldPoint(startpos_ws + Vec4f(r,  0,  0, 0.f));
	spherepath_aabb_ws.enlargeToHoldPoint(startpos_ws + Vec4f(-r, 0,  0, 0.f));

	// Make it hold sphere at end pos
	spherepath_aabb_ws.enlargeToHoldPoint(endpos_ws + Vec4f(0,  0,  r, 0.f));
	spherepath_aabb_ws.enlargeToHoldPoint(endpos_ws + Vec4f(0,  0, -r, 0.f));
	spherepath_aabb_ws.enlargeToHoldPoint(endpos_ws + Vec4f(0,  r,  0, 0.f));
	spherepath_aabb_ws.enlargeToHoldPoint(endpos_ws + Vec4f(0,  -r, 0, 0.f));
	spherepath_aabb_ws.enlargeToHoldPoint(endpos_ws + Vec4f(r,  0,  0, 0.f));
	spherepath_aabb_ws.enlargeToHoldPoint(endpos_ws + Vec4f(-r, 0,  0, 0.f));


	float closest_dist = std::numeric_limits<float>::infinity();

	for(size_t i=0; i<objects.size(); ++i)
	{
		RayTraceResult ob_results;
		objects[i]->traceSphere(sphere, translation_ws, spherepath_aabb_ws, ob_results);
		if(ob_results.hitdist >= 0 && ob_results.hitdist < closest_dist)
		{
			results_out = ob_results;
			results_out.hit_object = objects[i].getPointer();
			closest_dist = ob_results.hitdist;
		}
	}
}


void PhysicsWorld::getCollPoints(const js::BoundingSphere& sphere, std::vector<Vec4f>& points_out) const
{
	points_out.resize(0);

	js::AABBox sphere_aabb_ws = js::AABBox::emptyAABBox();

	// Make it hold sphere at start pos
	const float r = sphere.getRadius();
	sphere_aabb_ws.enlargeToHoldPoint(sphere.getCenter() + Vec4f(0,  0,  r, 0.f));
	sphere_aabb_ws.enlargeToHoldPoint(sphere.getCenter() + Vec4f(0,  0, -r, 0.f));
	sphere_aabb_ws.enlargeToHoldPoint(sphere.getCenter() + Vec4f(0,  r,  0, 0.f));
	sphere_aabb_ws.enlargeToHoldPoint(sphere.getCenter() + Vec4f(0,  -r, 0, 0.f));
	sphere_aabb_ws.enlargeToHoldPoint(sphere.getCenter() + Vec4f(r,  0,  0, 0.f));
	sphere_aabb_ws.enlargeToHoldPoint(sphere.getCenter() + Vec4f(-r, 0,  0, 0.f));

	for(size_t i=0; i<objects.size(); ++i)
	{
		objects[i]->appendCollPoints(sphere, sphere_aabb_ws, points_out);
	}
}

