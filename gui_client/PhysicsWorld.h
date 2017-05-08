/*=====================================================================
PhysicsWorld.h
--------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "PhysicsObject.h"
#include "PhysicsObjectBVH.h"
#include "../maths/Vec4f.h"
#include "../maths/vec3.h"
#include "../maths/vec2.h"
#include "../utils/ThreadSafeRefCounted.h"
#include "utils/Vector.h"
#include "physics/jscol_boundingsphere.h"
namespace Indigo { class TaskManager; }
class PrintOutput;


class RayTraceResult
{
public:
	Vec4f hit_normal_ws;
	const PhysicsObject* hit_object;
	float hitdist_ws;
	unsigned int hit_tri_index;
	Vec2f coords; // hit object barycentric coords
};


/*=====================================================================
PhysicsWorld
-------------

=====================================================================*/
class PhysicsWorld : public ThreadSafeRefCounted
{
public:
	PhysicsWorld();
	~PhysicsWorld();

	
	void addObject(const Reference<PhysicsObject>& object);
	
	void removeObject(const Reference<PhysicsObject>& object);

	void updateObjectTransformData(PhysicsObject& object);


	void rebuild(Indigo::TaskManager& task_manager, PrintOutput& print_output);


	void traceRay(const Vec4f& origin, const Vec4f& dir, ThreadContext& thread_context, RayTraceResult& results_out) const;

	void traceSphere(const js::BoundingSphere& sphere, const Vec4f& translation_ws, ThreadContext& thread_context, RayTraceResult& results_out) const;


	void getCollPoints(const js::BoundingSphere& sphere, ThreadContext& thread_context, std::vector<Vec4f>& points_out) const;


private:
	js::Vector<Reference<PhysicsObject>, 32> objects;

	PhysicsObjectBVH object_bvh;
};


