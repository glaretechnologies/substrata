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
#include <utils/Array2D.h>
#include <set>

#if USE_JOLT
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#endif

namespace glare { class TaskManager; }
namespace glare { class StackAllocator; }
namespace Indigo { class Mesh; }
class PrintOutput;
class BatchedMesh;
namespace JPH { class PhysicsSystem; }
namespace JPH { class TempAllocator; }
namespace JPH { class JobSystem; }
class BPLayerInterfaceImpl;
class MyBroadPhaseLayerFilter;
class MyObjectLayerPairFilter;


class RayTraceResult
{
public:
	Vec4f hit_normal_ws;
	const PhysicsObject* hit_object;
	float hit_t;
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


// Jolt Physics Layer that objects can be in, determines which other objects it can collide with.
// We will have separate layers for vehicles and the player interaction character, so we can make the interaction character not collide with vehicles it is in.
namespace Layers
{
	static constexpr uint8 NON_MOVING = 0;
	static constexpr uint8 MOVING = 1;
	static constexpr uint8 NON_COLLIDABLE = 2;
	//static constexpr uint8 INTERACTION_CHARACTER = 3;
	static constexpr uint8 VEHICLES = 3;
	static constexpr uint8 NUM_LAYERS = 4;
};


class PhysicsWorldEventListener
{
public:
	virtual void physicsObjectEnteredWater(PhysicsObject& ob) {} // Called on main thread

	// Called off main thread, needs to be threadsafe
	virtual void contactAdded(const JPH::Body &inBody1, const JPH::Body &inBody2/*PhysicsObject* ob_a, PhysicsObject* ob_b*/, const JPH::ContactManifold& contact_manifold) {}

	// Called off main thread, needs to be threadsafe
	virtual void contactPersisted(const JPH::Body &inBody1, const JPH::Body &inBody2/*PhysicsObject* ob_a, PhysicsObject* ob_b*/, const JPH::ContactManifold& contact_manifold) {}
};



void computeToWorldAndToObMatrices(const Vec4f& translation, const Quatf& rot_quat, const Vec4f& scale, Matrix4f& ob_to_world_out, Matrix4f& world_to_ob_out);

/*=====================================================================
PhysicsWorld
------------

=====================================================================*/
class PhysicsWorld : public ThreadSafeRefCounted
#if USE_JOLT
	, public JPH::BodyActivationListener, public JPH::ContactListener
#endif
{
public:
	PhysicsWorld(glare::TaskManager* task_manager, glare::StackAllocator* stack_allocator);
	~PhysicsWorld();

	static void init(/*Reference<glare::Allocator> global_jolt_allocator*/);

	void setWaterBuoyancyEnabled(bool enabled);
	bool getWaterBuoyancyEnabled() const { return water_buoyancy_enabled; }
	void setWaterZ(float water_z);
	float getWaterZ() const { return water_z; }
		
	void addObject(const Reference<PhysicsObject>& object);
	
	void removeObject(const Reference<PhysicsObject>& object);

	void activateObject(const Reference<PhysicsObject>& object);

	void setObjectLayer(const Reference<PhysicsObject>& object, uint8 new_object_layer);

	static PhysicsShape createJoltShapeForIndigoMesh(const Indigo::Mesh& mesh, bool build_dynamic_physics_ob, glare::Allocator* mem_allocator = nullptr);
	static PhysicsShape createJoltShapeForBatchedMesh(const BatchedMesh& mesh, bool build_dynamic_physics_ob, glare::Allocator* mem_allocator = nullptr, 
		const js::Vector<bool>* create_tris_for_mat = nullptr);

	static PhysicsShape createJoltHeightFieldShape(int vert_res, const Array2D<float>& heightfield, float quad_w);

	// Creates a box, centered at (0,0,0), with x and y extent = ground_quad_w, and z extent = 1.
	static PhysicsShape createGroundQuadShape(float ground_quad_w);

	static PhysicsShape createCOMOffsetShapeForShape(const PhysicsShape& shape, const Vec4f& COM_offset);

	static PhysicsShape createScaledAndTranslatedShapeForShape(const PhysicsShape& shape, const Vec3f& translation, const Vec3f& scale);

	void think(double dt);

#if USE_JOLT
	// BodyActivationListener interface:
	virtual void OnBodyActivated(const JPH::BodyID& inBodyID, uint64 inBodyUserData) override;
	virtual void OnBodyDeactivated(const JPH::BodyID& inBodyID, uint64 inBodyUserData) override;

	// ContactListener interface:
	virtual void OnContactAdded(const JPH::Body &inBody1, const JPH::Body &inBody2, const JPH::ContactManifold &inManifold, JPH::ContactSettings &ioSettings) override;
	virtual void OnContactPersisted(const JPH::Body &inBody1, const JPH::Body &inBody2, const JPH::ContactManifold &inManifold, JPH::ContactSettings &ioSettings) override;
#endif

	void setNewObToWorldTransform(PhysicsObject& object, const Vec4f& translation, const Quatf& rot, const Vec4f& scale);
	void setNewObToWorldTransform(PhysicsObject& object, const Vec4f& translation, const Quatf& rot, const Vec4f& linear_vel, const Vec4f& angular_vel);

	// Updates position.  Does not activate object.
	void setNewPosition(PhysicsObject& object, const Vec4f& pos);

	Vec4f getObjectLinearVelocity(const PhysicsObject& object) const;
	void setLinearAndAngularVelToZero(PhysicsObject& object);

	void moveKinematicObject(PhysicsObject& object, const Vec4f& translation, const Quatf& rot, float dt);

	void clear(); // Remove all objects

	//----------------------------------- Diagnostics ----------------------------------------
	struct MemUsageStats
	{
		size_t mem;
		size_t num_meshes;
		std::vector<int> layer_counts;
	};
	MemUsageStats getMemUsageStats() const;

	std::string getDiagnostics() const;

	std::string getLoadedMeshes() const;

	const Vec4f getPosInJolt(const Reference<PhysicsObject>& object);

	size_t getNumObjects() const;
	//----------------------------------------------------------------------------------------

	void traceRay(const Vec4f& origin, const Vec4f& dir, float max_t, JPH::BodyID ignore_body_id, RayTraceResult& results_out) const;

	bool doesRayHitAnything(const Vec4f& origin, const Vec4f& dir, float max_t) const;

	void writeJoltSnapshotToDisk(const std::string& path);

	static size_t computeSizeBForShape(JPH::Ref<JPH::Shape> jolt_shape);

	static void test();
	
public:
	mutable Mutex activated_obs_mutex;
	HashSet<PhysicsObject*> activated_obs GUARDED_BY(activated_obs_mutex); // Currently activated objects
	//std::set<JPH::BodyID> activated_obs;
	
	HashSet<PhysicsObject*> newly_activated_obs GUARDED_BY(activated_obs_mutex); // Objects that have become activated recently.
	
	PhysicsWorldEventListener* event_listener;
//private:
public:

	// Jolt
	JPH::PhysicsSystem* physics_system;
	JPH::TempAllocator* temp_allocator;
	JPH::JobSystem* job_system;
	BPLayerInterfaceImpl* broad_phase_layer_interface;
	MyBroadPhaseLayerFilter* broad_phase_layer_filter;
	MyObjectLayerPairFilter* object_layer_pair_filter;

private:
	bool water_buoyancy_enabled;
	float water_z;

	glare::TaskManager* task_manager;
	glare::StackAllocator* stack_allocator;
};


inline void checkRemoveObAndSetRefToNull(const Reference<PhysicsWorld>& physics_world, Reference<PhysicsObject>& physics_object)
{
	if(physics_object)
	{
		physics_world->removeObject(physics_object);
		physics_object = nullptr;
	}
}


inline void checkRemoveObAndSetRefToNull(PhysicsWorld& physics_world, Reference<PhysicsObject>& physics_object)
{
	if(physics_object)
	{
		physics_world.removeObject(physics_object);
		physics_object = nullptr;
	}
}
