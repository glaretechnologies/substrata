/*=====================================================================
PhysicsWorld.h
--------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "PhysicsObject.h"
//#include <physics/jscol_boundingsphere.h>
//#include <physics/HashedGrid2.h>
#include <maths/Vec4f.h>
#include <maths/Quat.h>
#include <maths/vec2.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/Vector.h>
#include <utils/Mutex.h>
#include <utils/HashSet.h>
#include <set>

#if USE_JOLT
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#endif

namespace Indigo { class TaskManager; }
namespace Indigo { class Mesh; }
class PrintOutput;
class BatchedMesh;
namespace JPH { class PhysicsSystem; }
namespace JPH { class TempAllocatorImpl; }
namespace JPH { class JobSystemThreadPool; }

class RayTraceResult
{
public:
	Vec4f hit_normal_ws;
	const PhysicsObject* hit_object;
	float hitdist_ws;
	//unsigned int hit_tri_index;
	unsigned int hit_mat_index;
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


// Jolt stuff:
// Layer that objects can be in, determines which other objects it can collide with
// Typically you at least want to have 1 layer for moving bodies and 1 layer for static bodies, but you can have more
// layers if you want. E.g. you could have a layer for high detail collision (which is not used by the physics simulation
// but only if you do collision testing).
namespace Layers
{
	static constexpr uint8 NON_MOVING = 0;
	static constexpr uint8 MOVING = 1;
	static constexpr uint8 NON_COLLIDABLE = 2;
	static constexpr uint8 NUM_LAYERS = 3;
};



void computeToWorldAndToObMatrices(const Vec4f& translation, const Quatf& rot_quat, const Vec4f& scale, Matrix4f& ob_to_world_out, Matrix4f& world_to_ob_out);

/*=====================================================================
PhysicsWorld
------------

=====================================================================*/
class PhysicsWorld : public ThreadSafeRefCounted
#if USE_JOLT
	, public JPH::BodyActivationListener
#endif
{
public:
	PhysicsWorld();
	~PhysicsWorld();

	static void init();
		
	void addObject(const Reference<PhysicsObject>& object);
	
	void removeObject(const Reference<PhysicsObject>& object);

	static JPH::Ref<JPH::Shape> createJoltShapeForIndigoMesh(const Indigo::Mesh& mesh);
	static JPH::Ref<JPH::Shape> createJoltShapeForBatchedMesh(const BatchedMesh& mesh);

	void think(double dt);

	void updateActiveObjects();

	// BodyActivationListener interface:
#if USE_JOLT
	virtual void OnBodyActivated(const JPH::BodyID& inBodyID, uint64 inBodyUserData) override;
	virtual void OnBodyDeactivated(const JPH::BodyID& inBodyID, uint64 inBodyUserData) override;
#endif

	void setNewObToWorldTransform(PhysicsObject& object, const Vec4f& translation, const Quatf& rot, const Vec4f& scale);

	void moveKinematicObject(PhysicsObject& object, const Vec4f& translation, const Quatf& rot, float dt);

	void clear(); // Remove all objects

	//----------------------------------- Diagnostics ----------------------------------------
	struct MemUsageStats
	{
		size_t mem;
		size_t num_meshes;
	};
	MemUsageStats getMemUsageStats() const;

	std::string getDiagnostics() const;

	std::string getLoadedMeshes() const;

	const Vec4f getPosInJolt(const Reference<PhysicsObject>& object);

	size_t getNumObjects() const { return objects_set.size(); }
	//----------------------------------------------------------------------------------------

	void traceRay(const Vec4f& origin, const Vec4f& dir, float max_t, RayTraceResult& results_out) const;

	bool doesRayHitAnything(const Vec4f& origin, const Vec4f& dir, float max_t) const;

	void writeJoltSnapshotToDisk(const std::string& path);

	static void test();
private:
	std::set<Reference<PhysicsObject>> objects_set; // Use std::set for fast iteration.  TODO: can remove?
	
public:
	Mutex activated_obs_mutex;
	HashSet<PhysicsObject*> activated_obs GUARDED_BY(activated_obs_mutex);
	//std::set<JPH::BodyID> activated_obs;
//private:
	std::vector<PhysicsObject*> temp_activated_obs;

public:

	// Jolt
	JPH::PhysicsSystem* physics_system;
	JPH::TempAllocatorImpl* temp_allocator;
	JPH::JobSystemThreadPool* job_system;
};
