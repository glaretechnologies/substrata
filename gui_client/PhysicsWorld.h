/*=====================================================================
PhysicsWorld.h
--------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "PhysicsObject.h"
#include "PhysicsObjectBVH.h"
#include <physics/jscol_boundingsphere.h>
#include <physics/HashedGrid2.h>
#include <maths/Vec4f.h>
#include <maths/Quat.h>
#include <maths/vec2.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/Vector.h>
#include <utils/Mutex.h>
#include <set>

#include <Jolt/Jolt.h>
#include <Jolt\Physics\Body\BodyID.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>

namespace Indigo { class TaskManager; }
class PrintOutput;
namespace JPH { class PhysicsSystem; }
namespace JPH { class TempAllocatorImpl; }
namespace JPH { class JobSystemThreadPool; }

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



class PhysicsWorldBodyActivationCallbacks
{
public:
	virtual void onBodyActivated(PhysicsObject& ob) = 0;
	virtual void onBodyDeactivated(PhysicsObject& ob) = 0;
};


/*=====================================================================
PhysicsWorld
------------

=====================================================================*/
class PhysicsWorld : public ThreadSafeRefCounted, public JPH::BodyActivationListener
{
public:
	PhysicsWorld(/*PhysicsWorldBodyActivationCallbacks* activation_callbacks*/);
	~PhysicsWorld();
		
	void addObject(const Reference<PhysicsObject>& object);
	
	void removeObject(const Reference<PhysicsObject>& object);

	void think(double dt);

	//TEMP:
	//void setObjectAsJoltSphere(const Reference<PhysicsObject>& object);
	//void setObjectAsJoltCube(const Reference<PhysicsObject>& object);


	// BodyActivationListener interface:
	virtual void OnBodyActivated(const JPH::BodyID& inBodyID, uint64 inBodyUserData) override;
	virtual void OnBodyDeactivated(const JPH::BodyID& inBodyID, uint64 inBodyUserData) override;


	// Updates transform data and grid cells
	//void setNewObToWorldMatrix(PhysicsObject& object, const Matrix4f& new_ob_to_world);
	//void setNewObToWorldMatrix(PhysicsObject& object, const Matrix4f& new_ob_to_world, const Matrix4f& new_world_to_ob); // For when the new_world_to_ob is already computed, so don't to invert new_ob_to_world.

	void setNewObToWorldTransform(PhysicsObject& object, const Vec4f& translation, const Quatf& rot, const Vec3f& scale);

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
	void setNewObToWorldTransformInternal(PhysicsObject& object, const Vec4f& translation, const Quatf& rot, const Vec3f& scale, bool udpate_jolt_ob_state);
	//void setNewObToWorldMatrixInternal(PhysicsObject& object, const Matrix4f& new_ob_to_world, const Matrix4f& new_world_to_ob, bool udpate_jolt_ob_state); // For when the new_world_to_ob is already computed, so don't to invert new_ob_to_world.
	std::set<Reference<PhysicsObject>> objects_set; // Use std::set for fast iteration.

	HashedGrid2<PhysicsObject*, std::hash<PhysicsObject*>> ob_grid;

	// For very large objects that would occupy many grid cells, store in a separate set instead to avoid spamming the hashed grid.
	HashSet<PhysicsObject*> large_objects;


	//PhysicsWorldBodyActivationCallbacks* activation_callbacks;

public:
	Mutex activated_obs_mutex;
	std::set<PhysicsObject*> activated_obs GUARDED_BY(activated_obs_mutex);
	//std::set<JPH::BodyID> activated_obs;
private:
	std::vector<PhysicsObject*> temp_activated_obs;

	// Jolt
	JPH::PhysicsSystem* physics_system;
	//JPH::BodyID sphere_id;

	//JPH::BodyID cube_id;

	JPH::TempAllocatorImpl* temp_allocator;
	JPH::JobSystemThreadPool* job_system;

};
