/*=====================================================================
PhysicsWorld.h
--------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "PhysicsObject.h"
#include "PhysicsObjectBVH.h"
#include <physics/jscol_boundingsphere.h>
#include <maths/Vec4f.h>
#include <maths/vec2.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/Vector.h>
#include "HashedGrid2.h"
#include <set>
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
------------

=====================================================================*/
class PhysicsWorld : public ThreadSafeRefCounted
{
public:
	PhysicsWorld();
	~PhysicsWorld();
		
	void addObject(const Reference<PhysicsObject>& object);
	
	void removeObject(const Reference<PhysicsObject>& object);

	// Updates transform data and grid cells
	void setNewObToWorldMatrix(PhysicsObject& object, const Matrix4f& new_ob_to_world);

	void computeObjectTransformData(PhysicsObject& object);

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
	std::set<Reference<PhysicsObject>> objects_set; // Use std::set for fast iteration.

	HashedGrid2<PhysicsObject*, std::hash<PhysicsObject*>> ob_grid;

	// For very large objects that would occupy many grid cells, store in a separate set instead to avoid spamming the hashed grid.
	HashSet<PhysicsObject*> large_objects;
};
