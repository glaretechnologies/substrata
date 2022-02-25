/*=====================================================================
PhysicsWorld.h
--------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#pragma once


#include "PhysicsObject.h"
#include "PhysicsObjectBVH.h"
#include <physics/jscol_boundingsphere.h>
#include <maths/Vec4f.h>
#include <maths/vec2.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/Vector.h>
#include <unordered_set>
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


class SphereTraceResult
{
public:
	Vec4f hit_pos_ws;
	Vec4f hit_normal_ws;
	const PhysicsObject* hit_object;
	float hitdist_ws;
	bool point_in_tri;
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

	void rebuild(glare::TaskManager& task_manager, PrintOutput& print_output);

	void clear(); // Remove all objects

	//----------------------------------- Diagnostics ----------------------------------------
	struct MemUsageStats
	{
		size_t mem;
		size_t num_meshes;
	};
	MemUsageStats getTotalMemUsage() const;

	std::string getDiagnostics() const;

	std::string getLoadedMeshes() const;
	//----------------------------------------------------------------------------------------

	void traceRay(const Vec4f& origin, const Vec4f& dir, float max_t, RayTraceResult& results_out) const;

	bool doesRayHitAnything(const Vec4f& origin, const Vec4f& dir, float max_t) const;

	void traceSphere(const js::BoundingSphere& sphere, const Vec4f& translation_ws, SphereTraceResult& results_out) const;

	void getCollPoints(const js::BoundingSphere& sphere, std::vector<Vec4f>& points_out) const;

private:
	std::unordered_set<Reference<PhysicsObject>, PhysicsObjectHash> objects_set;

	PhysicsObjectBVH object_bvh;
};
