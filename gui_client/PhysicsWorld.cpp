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
#include <utils/string_view.h>


#define JPH_PROFILE_ENABLED 1


#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/PhysicsMaterialSimple.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <stdarg.h>
#include <Lock.h>


typedef uint32 uint;


// Callback for traces from Jolt, connect this to your own trace function if you have one
static void traceImpl(const char* inFMT, ...)
{
	// Format the message
	va_list list;
	va_start(list, inFMT);
	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), inFMT, list);

	conPrint(buffer);
}


// Layer that objects can be in, determines which other objects it can collide with
// Typically you at least want to have 1 layer for moving bodies and 1 layer for static bodies, but you can have more
// layers if you want. E.g. you could have a layer for high detail collision (which is not used by the physics simulation
// but only if you do collision testing).
namespace Layers
{
	static constexpr uint8 NON_MOVING = 0;
	static constexpr uint8 MOVING = 1;
	static constexpr uint8 NUM_LAYERS = 2;
};


// Function that determines if two object layers can collide
static bool MyObjectCanCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2)
{
	switch(inObject1)
	{
	case Layers::NON_MOVING:
		return inObject2 == Layers::MOVING; // Non moving only collides with moving
	case Layers::MOVING:
		return true; // Moving collides with everything
	default:
		assert(false);
		return false;
	}
};


// Each broadphase layer results in a separate bounding volume tree in the broad phase. You at least want to have
// a layer for non-moving and moving objects to avoid having to update a tree full of static objects every frame.
// You can have a 1-on-1 mapping between object layers and broadphase layers (like in this case) but if you have
// many object layers you'll be creating many broad phase trees, which is not efficient. If you want to fine tune
// your broadphase layers define JPH_TRACK_BROADPHASE_STATS and look at the stats reported on the TTY.
namespace BroadPhaseLayers
{
	static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
	static constexpr JPH::BroadPhaseLayer MOVING(1);
	static constexpr uint32 NUM_LAYERS(2);
};


// BroadPhaseLayerInterface implementation
// This defines a mapping between object and broadphase layers.
class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
public:
	BPLayerInterfaceImpl()
	{
		// Create a mapping table from object to broad phase layer
		mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
		mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
	}

	virtual uint32 GetNumBroadPhaseLayers() const override
	{
		return BroadPhaseLayers::NUM_LAYERS;
	}

	virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
	{
		assert(inLayer < Layers::NUM_LAYERS);
		return mObjectToBroadPhase[inLayer];
	}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
	{
		switch((JPH::BroadPhaseLayer::Type)inLayer)
		{
		case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING:	return "NON_MOVING";
		case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:		return "MOVING";
		default: assert(false);											return "INVALID";
		}
	}
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

private:
	JPH::BroadPhaseLayer					mObjectToBroadPhase[Layers::NUM_LAYERS];
};

// Function that determines if two broadphase layers can collide
static bool MyBroadPhaseCanCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2)
{
	switch(inLayer1)
	{
	case Layers::NON_MOVING:
		return inLayer2 == BroadPhaseLayers::MOVING;
	case Layers::MOVING:
		return true;
	default:
		assert(false);
		return false;
	}
}


PhysicsWorld::PhysicsWorld(/*PhysicsWorldBodyActivationCallbacks* activation_callbacks_*/)
:	//activation_callbacks(activation_callbacks_),
	ob_grid(/*cell_w=*/32.0, /*num_buckets=*/4096, /*expected_num_items_per_bucket=*/4, /*empty key=*/NULL),
	large_objects(/*empty key=*/NULL, /*expected num items=*/32)
{

	// Register allocation hook
	JPH::RegisterDefaultAllocator();

	// Install callbacks
	JPH::Trace = traceImpl;
	//JPH_IF_ENABLE_ASSERTS(AssertFailed = AssertFailedImpl;)

	// Create a factory
	JPH::Factory::sInstance = new JPH::Factory();

	// Register all Jolt physics types
	JPH::RegisterTypes();

	// We need a temp allocator for temporary allocations during the physics update. We're
	// pre-allocating 10 MB to avoid having to do allocations during the physics update. 
	// B.t.w. 10 MB is way too much for this example but it is a typical value you can use.
	// If you don't want to pre-allocate you can also use TempAllocatorMalloc to fall back to
	// malloc / free.
	temp_allocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024);

	// We need a job system that will execute physics jobs on multiple threads. Typically
	// you would implement the JobSystem interface yourself and let Jolt Physics run on top
	// of your own job scheduler. JobSystemThreadPool is an example implementation.
	job_system = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, JPH::thread::hardware_concurrency() - 1);

	// This is the max amount of rigid bodies that you can add to the physics system. If you try to add more you'll get an error.
	// Note: This value is low because this is a simple test. For a real project use something in the order of 65536.
	const uint32 cMaxBodies = 65536;

	// This determines how many mutexes to allocate to protect rigid bodies from concurrent access. Set it to 0 for the default settings.
	const uint32 cNumBodyMutexes = 0;

	// This is the max amount of body pairs that can be queued at any time (the broad phase will detect overlapping
	// body pairs based on their bounding boxes and will insert them into a queue for the narrowphase). If you make this buffer
	// too small the queue will fill up and the broad phase jobs will start to do narrow phase work. This is slightly less efficient.
	// Note: This value is low because this is a simple test. For a real project use something in the order of 65536.
	const uint32 cMaxBodyPairs = 65536;

	// This is the maximum size of the contact constraint buffer. If more contacts (collisions between bodies) are detected than this
	// number then these contacts will be ignored and bodies will start interpenetrating / fall through the world.
	// Note: This value is low because this is a simple test. For a real project use something in the order of 10240.
	const uint32 cMaxContactConstraints = 10240;

	// Create mapping table from object layer to broadphase layer
	// Note: As this is an interface, PhysicsSystem will take a reference to this so this instance needs to stay alive!
	BPLayerInterfaceImpl* broad_phase_layer_interface = new BPLayerInterfaceImpl();

	// Now we can create the actual physics system.
	physics_system = new JPH::PhysicsSystem();
	physics_system->Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints, *broad_phase_layer_interface, MyBroadPhaseCanCollide, MyObjectCanCollide);

	physics_system->SetGravity(JPH::Vec3Arg(0, 0, -9.81f));

	// A body activation listener gets notified when bodies activate and go to sleep
	// Note that this is called from a job so whatever you do here needs to be thread safe.
	// Registering one is entirely optional.
	physics_system->SetBodyActivationListener(this);

	// A contact listener gets notified when bodies (are about to) collide, and when they separate again.
	// Note that this is called from a job so whatever you do here needs to be thread safe.
	// Registering one is entirely optional.
	//MyContactListener contact_listener;
	//physics_system.SetContactListener(&contact_listener);

	// The main way to interact with the bodies in the physics system is through the body interface. There is a locking and a non-locking
	// variant of this. We're going to use the locking version (even though we're not planning to access bodies from multiple threads)
	JPH::BodyInterface& body_interface = physics_system->GetBodyInterface();

	// Next we can create a rigid body to serve as the floor, we make a large box
	// Create the settings for the collision volume (the shape). 
	// Note that for simple shapes (like boxes) you can also directly construct a BoxShape.
	JPH::BoxShapeSettings floor_shape_settings(JPH::Vec3(100.0f, 100.0f, 1.0f));

	// Create the shape
	JPH::ShapeSettings::ShapeResult floor_shape_result = floor_shape_settings.Create();
	JPH::ShapeRefC floor_shape = floor_shape_result.Get(); // We don't expect an error here, but you can check floor_shape_result for HasError() / GetError()

	// Create the settings for the body itself. Note that here you can also set other properties like the restitution / friction.
	JPH::BodyCreationSettings floor_settings(floor_shape, JPH::Vec3(0.0f, 0.0f, -1.0f), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::NON_MOVING);

	// Create the actual rigid body
	JPH::Body* floor = body_interface.CreateBody(floor_settings); // Note that if we run out of bodies this can return nullptr

	// Add it to the world
	body_interface.AddBody(floor->GetID(), JPH::EActivation::DontActivate);
}


PhysicsWorld::~PhysicsWorld()
{
}


static const int LARGE_OB_NUM_CELLS_THRESHOLD = 32;


void PhysicsWorld::setNewObToWorldTransform(PhysicsObject& object, const Vec4f& translation, const Quatf& rot, const Vec3f& scale)
{
	setNewObToWorldTransformInternal(object, translation, rot, scale, /*udpate_jolt_ob_state=*/true);
}



void PhysicsWorld::setNewObToWorldTransformInternal(PhysicsObject& object, const Vec4f& translation, const Quatf& rot, const Vec3f& scale, bool udpate_jolt_ob_state)
{
	const Matrix4f new_ob_to_world = Matrix4f::translationMatrix(translation) * rot.toMatrix() * Matrix4f::scaleMatrix(scale.x, scale.y, scale.z); // TODO: optimise

	// Compute world-to-ob matrix.
	Matrix4f new_world_to_ob;
	const bool invertible = new_ob_to_world.getInverseForAffine3Matrix(/*inverse out=*/new_world_to_ob);
	//assert(invertible);
	if(!invertible)
		new_world_to_ob = Matrix4f::identity(); // If not invertible, just use to-world matrix.  TEMP HACK

	const js::AABBox new_aabb_ws = object.geometry->getAABBox().transformedAABBFast(new_ob_to_world);





	if(large_objects.count(&object) > 0)
	{
		// Just keep in large objects.
	}
	else
	{
		const js::AABBox& old_aabb_ws = object.aabb_ws;

		// See if the object has changed grid cells
		const Vec4i old_min_bucket_i = ob_grid.bucketIndicesForPoint(old_aabb_ws.min_);
		const Vec4i old_max_bucket_i = ob_grid.bucketIndicesForPoint(old_aabb_ws.max_);

		const Vec4i new_min_bucket_i = ob_grid.bucketIndicesForPoint(new_aabb_ws.min_);
		const Vec4i new_max_bucket_i = ob_grid.bucketIndicesForPoint(new_aabb_ws.max_);

		//conPrint("setNewObToWorldMatrix()");
		if(new_min_bucket_i != old_min_bucket_i || new_max_bucket_i != old_max_bucket_i)
		{
			// cells have changed.
			ob_grid.remove(&object, old_aabb_ws);
			ob_grid.insert(&object, new_aabb_ws);


			// NOTE: could do something like this, but is tricky due to bucket hashing:
			// Iterate over old cells, remove object from any cell not in new cells, 
			// then iterate over new cells, add object to new cell if not already inserted.

			//conPrint("cell changed!");
		}
	}

	object.ob_to_world = new_ob_to_world;
	object.world_to_ob = new_world_to_ob;
	object.aabb_ws = new_aabb_ws;

	//===================================== Jolt =====================================
	if(udpate_jolt_ob_state && !object.jolt_body_id.IsInvalid()) // If we are updating Jolt state, and this object has a corresponding Jolt object:
	{
		JPH::BodyInterface& body_interface = physics_system->GetBodyInterface();

		body_interface.SetPositionRotationAndVelocity(object.jolt_body_id, /*pos=*/JPH::Vec3(translation[0], translation[1], translation[2]),
			/*rot=*/JPH::Quat(rot.v[0], rot.v[1], rot.v[2], rot.v[3]), /*vel=*/JPH::Vec3(0, 0, 0), /*ang vel=*/JPH::Vec3(0, 0, 0));

		body_interface.ActivateBody(object.jolt_body_id);
	}
	//===================================== End Jolt =====================================
}


#if 0
void PhysicsWorld::setNewObToWorldMatrix(PhysicsObject& object, const Matrix4f& new_ob_to_world)
{
	// Compute world-to-ob matrix.
	Matrix4f new_world_to_ob;
	const bool invertible = new_ob_to_world.getInverseForAffine3Matrix(/*inverse out=*/new_world_to_ob);
	//assert(invertible);
	if(!invertible)
		new_world_to_ob = Matrix4f::identity(); // If not invertible, just use to-world matrix.  TEMP HACK

	setNewObToWorldMatrix(object, new_ob_to_world, new_world_to_ob);
}


void PhysicsWorld::setNewObToWorldMatrix(PhysicsObject& object, const Matrix4f& new_ob_to_world, const Matrix4f& new_world_to_ob)
{
	setNewObToWorldMatrixInternal(object, new_ob_to_world, new_world_to_ob, /*udpate_jolt_ob_state=*/true);
}


void PhysicsWorld::setNewObToWorldMatrixInternal(PhysicsObject& object, const Matrix4f& new_ob_to_world, const Matrix4f& new_world_to_ob, bool udpate_jolt_ob_state)
{
	const js::AABBox new_aabb_ws = object.geometry->getAABBox().transformedAABBFast(new_ob_to_world);

	if(large_objects.count(&object) > 0)
	{
		// Just keep in large objects.
	}
	else
	{
		const js::AABBox& old_aabb_ws = object.aabb_ws;

		// See if the object has changed grid cells
		const Vec4i old_min_bucket_i = ob_grid.bucketIndicesForPoint(old_aabb_ws.min_);
		const Vec4i old_max_bucket_i = ob_grid.bucketIndicesForPoint(old_aabb_ws.max_);

		const Vec4i new_min_bucket_i = ob_grid.bucketIndicesForPoint(new_aabb_ws.min_);
		const Vec4i new_max_bucket_i = ob_grid.bucketIndicesForPoint(new_aabb_ws.max_);

		//conPrint("setNewObToWorldMatrix()");
		if(new_min_bucket_i != old_min_bucket_i || new_max_bucket_i != old_max_bucket_i)
		{
			// cells have changed.
			ob_grid.remove(&object, old_aabb_ws);
			ob_grid.insert(&object, new_aabb_ws);


			// NOTE: could do something like this, but is tricky due to bucket hashing:
			// Iterate over old cells, remove object from any cell not in new cells, 
			// then iterate over new cells, add object to new cell if not already inserted.

			//conPrint("cell changed!");
		}
	}

	object.ob_to_world = new_ob_to_world;
	object.world_to_ob = new_world_to_ob;
	object.aabb_ws = new_aabb_ws;

	if(udpate_jolt_ob_state && !object.jolt_body_id.IsInvalid())
	{
		//const JPH::BodyLockInterfaceLocking& lock_interface = physics_system->GetBodyLockInterface();

		//// Scoped lock
		//{
		//	JPH::BodyLockWrite lock(lock_interface, sphere_id);
		//	if(lock.Succeeded()) // body_id may no longer be valid
		//	{
		//		JPH::Body& body = lock.GetBody();

		//		// Do something with body
		//		body.SetPositionAndRotationInternal
		//	}
		//}

		JPH::BodyInterface& body_interface = physics_system->GetBodyInterface();
		body_interface.SetPositionRotationAndVelocity(object.jolt_body_id, /*pos=*/JPH::Vec3(new_ob_to_world.getColumn(3)[0], new_ob_to_world.getColumn(3)[1], new_ob_to_world.getColumn(3)[2]),
			/*rot=*/JPH::Quat::sIdentity(), /*vel=*/JPH::Vec3(0, 0, 0), /*ang vel=*/JPH::Vec3(0, 0, 0));

		body_interface.ActivateBody(object.jolt_body_id);
	}
}
#endif

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
	assert(object->pos.isFinite());

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



	//===================================== Jolt =====================================
	JPH::BodyInterface& body_interface = physics_system->GetBodyInterface();

	if(object->is_sphere)
	{
		JPH::BodyCreationSettings sphere_settings(new JPH::SphereShape(0.5f), 
			JPH::Vec3(object->pos[0], object->pos[1], object->pos[2]),
			JPH::Quat(object->rot.v[0], object->rot.v[1], object->rot.v[2], object->rot.v[3]),
			object->dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static, 
			Layers::MOVING);
		sphere_settings.mRestitution = 0.7f;
		sphere_settings.mUserData = (uint64)object.ptr();
		
		object->jolt_body_id = body_interface.CreateAndAddBody(sphere_settings, JPH::EActivation::Activate);

		conPrint("Added Jolt sphere body, dynamic: " + boolToString(object->dynamic));
	}
	else if(object->is_cube)
	{
		JPH::BoxShapeSettings cube_shape_settings(JPH::Vec3(0.5f, 0.5f, 0.5f));

		// Create the shape
		JPH::ShapeSettings::ShapeResult cube_shape_result = cube_shape_settings.Create();
		JPH::ShapeRefC cube_shape = cube_shape_result.Get(); // We don't expect an error here, but you can check floor_shape_result for HasError() / GetError()

		// Create the settings for the body itself. Note that here you can also set other properties like the restitution / friction.
		JPH::BodyCreationSettings cube_settings(cube_shape, 
			JPH::Vec3(object->pos[0], object->pos[1], object->pos[2]),
			JPH::Quat(object->rot.v[0], object->rot.v[1], object->rot.v[2], object->rot.v[3]),
			object->dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static, 
			Layers::MOVING);
		cube_settings.mRestitution = 0.5f;
		cube_settings.mUserData = (uint64)object.ptr();

		object->jolt_body_id = body_interface.CreateAndAddBody(cube_settings, JPH::EActivation::Activate);

		conPrint("Added Jolt cube body, dynamic: " + boolToString(object->dynamic));
	}
	else
	{
		// Create regular grid of triangles
		uint32 max_material_index = 0;
		JPH::TriangleList triangles;

		const js::Vector<RayMeshTriangle, 32>& tris = object->geometry->getTriangles();
		const RayMesh::VertexVectorType& verts = object->geometry->getVertices();

		triangles.resize(object->geometry->getNumTris());

		for(size_t i = 0; i < tris.size(); ++i)
		{
			const RayMeshTriangle& tri = tris[i];

			const Vec3f v0pos = verts[tri.vertex_indices[0]].pos;
			const Vec3f v1pos = verts[tri.vertex_indices[1]].pos;
			const Vec3f v2pos = verts[tri.vertex_indices[2]].pos;
			const JPH::Float3 v0(v0pos.x, v0pos.y, v0pos.z);
			const JPH::Float3 v1(v1pos.x, v1pos.y, v1pos.z);
			const JPH::Float3 v2(v2pos.x, v2pos.y, v2pos.z);

			triangles.push_back(JPH::Triangle(v0, v1, v2, 0/*tri.getTriMatIndex()*/));

			max_material_index = myMax(max_material_index, tri.getTriMatIndex());
		}

		max_material_index = 0; // TEMP

		// Create materials
		JPH::PhysicsMaterialList materials;
		materials.resize(max_material_index + 1);
		for(uint i = 0; i <= max_material_index; ++i)
			materials[i] = new JPH::PhysicsMaterialSimple("Material " + toString(i), JPH::Color::sGetDistinctColor(i));

		JPH::MeshShapeSettings* mesh_body_settings = new JPH::MeshShapeSettings(triangles, materials);

		JPH::Body* body;
		if(object->scale == Vec3f(1.f))
		{
			JPH::BodyCreationSettings settings(mesh_body_settings,
				JPH::Vec3(object->pos[0], object->pos[1], object->pos[2]),
				JPH::Quat(object->rot.v[0], object->rot.v[1], object->rot.v[2], object->rot.v[3]),
				JPH::EMotionType::Static, 
				Layers::NON_MOVING);

			settings.mUserData = (uint64)object.ptr();

			body = body_interface.CreateBody(settings);
		}
		else
		{
			JPH::BodyCreationSettings settings(
				new JPH::ScaledShapeSettings(mesh_body_settings, JPH::Vec3(object->scale[0], object->scale[1], object->scale[2])),
				JPH::Vec3(object->pos[0], object->pos[1], object->pos[2]),
				JPH::Quat(object->rot.v[0], object->rot.v[1], object->rot.v[2], object->rot.v[3]),
				JPH::EMotionType::Static, 
				Layers::NON_MOVING);

			settings.mUserData = (uint64)object.ptr();

			body = body_interface.CreateBody(settings);
		}

		body_interface.AddBody(body->GetID(), JPH::EActivation::DontActivate);

		object->jolt_body_id = body->GetID();

		conPrint("Added Jolt mesh body");
	}
	//===================================== End Jolt =====================================
}



void PhysicsWorld::removeObject(const Reference<PhysicsObject>& object)
{
	//===================================== Jolt =====================================
	JPH::BodyInterface& body_interface = physics_system->GetBodyInterface();

	// Remove jolt body if it exists
	if(!object->jolt_body_id.IsInvalid())
	{
		body_interface.RemoveBody(object->jolt_body_id);
		object->jolt_body_id = JPH::BodyID();

		conPrint("Removed Jolt body");
	}

	activated_obs.erase(object.ptr()); // Object should have been removed from there when its Jolt body is removed (and deactivated), but do it again to be safe.
	//===================================== End Jolt =====================================


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

	// Make sure there aren't any dangling references to the object in ob_grid etc.
	assert(large_objects.count(object.ptr()) == 0);
#ifndef NDEBUG // If in Debug mode:
	for(size_t i=0; i<ob_grid.buckets.size(); ++i)
		assert(ob_grid.buckets[i].objects.count(object.ptr()) == 0);
#endif
}


void PhysicsWorld::think(double dt)
{
	//===================================== Jolt =====================================
	JPH::BodyInterface& body_interface = physics_system->GetBodyInterface();

	// Set PhysicsObject state from Jolt bodies, for active objects.

	// Copy activated_obs to temp_activated_obs, so we can iterate over the objects without holding activated_obs_mutex, which may lead to a deadlock.
	{
		Lock lock(activated_obs_mutex); // NOTE: deadlock potential?
		temp_activated_obs.resize(activated_obs.size());
		size_t i = 0;
		for(auto it = activated_obs.begin(); it != activated_obs.end(); ++it)
			temp_activated_obs[i++] = *it;
	} // End lock scope


	for(auto it = temp_activated_obs.begin(); it != temp_activated_obs.end(); ++it)
	{
		PhysicsObject* ob = *it;
		if(!ob->jolt_body_id.IsInvalid())
		{
			JPH::Vec3 pos = body_interface.GetCenterOfMassPosition(ob->jolt_body_id);
			JPH::Quat rot = body_interface.GetRotation(ob->jolt_body_id);

			conPrint("Setting active object " + toString(ob->jolt_body_id.GetIndex()) + " state from jolt: " + toString(pos.GetX()) + ", " + toString(pos.GetY()) + ", " + toString(pos.GetZ()));

			const Vec4f new_pos(pos.GetX(), pos.GetY(), pos.GetZ(), 1.f);
			const Quatf new_rot(rot.GetX(), rot.GetY(), rot.GetZ(), rot.GetW());
			this->setNewObToWorldTransformInternal(*ob, new_pos, new_rot, ob->scale, /*udpate_jolt_ob_state=*/false); // Don't overwrite Jolt state, we are reading from it.

			ob->rot = new_rot;
			ob->pos = new_pos;
		}
	}

	// If you take larger steps than 1 / 60th of a second you need to do multiple collision steps in order to keep the simulation stable. Do 1 collision step per 1 / 60th of a second (round up).
	const int cCollisionSteps = 1;

	// If you want more accurate step results you can do multiple sub steps within a collision step. Usually you would set this to 1.
	const int cIntegrationSubSteps = 1;

	// We simulate the physics world in discrete time steps. 60 Hz is a good rate to update the physics system.
	const float cDeltaTime = myMin(1.f / 60.f, (float)dt); // 1.0f / 60.0f;

	// Step the world
	physics_system->Update(cDeltaTime, cCollisionSteps, cIntegrationSubSteps, temp_allocator, job_system);

	//===================================== End Jolt =====================================
}


// NOTE: may be called from a Jolt thread!
void PhysicsWorld::OnBodyActivated(const JPH::BodyID& inBodyID, uint64 inBodyUserData)
{
	conPrint("Jolt body activated");

	if(inBodyUserData != 0)
	{
		Lock lock(activated_obs_mutex);
		activated_obs.insert((PhysicsObject*)inBodyUserData);
	}
	//activated_obs.insert(inBodyID);
}


// NOTE: may be called from a Jolt thread!
void PhysicsWorld::OnBodyDeactivated(const JPH::BodyID& inBodyID, uint64 inBodyUserData)
{
	conPrint("Jolt body deactivated");

	if(inBodyUserData != 0)
	{
		Lock lock(activated_obs_mutex);
		activated_obs.erase((PhysicsObject*)inBodyUserData);
	}
	//activated_obs.erase(inBodyID);
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

	for(size_t i=0; i<ob_grid.buckets.size(); ++i)
		stats.mem += ob_grid.buckets[i].objects.buckets_size * sizeof(PhysicsObject*);

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

	// For large max_t values, the ray path will have a very large bounding box, and the hashed grid will most likely be slower, and in fact take infinite time for infinite max_t.
	// So only use the hashed grid for smaller max_t values.
	if(max_t < 100.f)
	{
		// Test against large objects
		for(auto it = large_objects.begin(); it != large_objects.end(); ++it)
		{
			const PhysicsObject* object = *it;

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

		// Test against objects in hashed grid

		// Compute AABB of ray path (Using ray.max_t that may have been set from an intersection with large objects above)
		js::AABBox ray_path_AABB(origin, origin);
		ray_path_AABB.enlargeToHoldPoint(ray.pointf(ray.max_t));

		const Vec4i min_bucket_i = ob_grid.bucketIndicesForPoint(ray_path_AABB.min_);
		const Vec4i max_bucket_i = ob_grid.bucketIndicesForPoint(ray_path_AABB.max_);

		//Timer timer; 
		//int num_buckets_tested = 0;
		//int num_obs_tested = 0;

		for(int x = min_bucket_i[0]; x <= max_bucket_i[0]; ++x)
		for(int y = min_bucket_i[1]; y <= max_bucket_i[1]; ++y)
		for(int z = min_bucket_i[2]; z <= max_bucket_i[2]; ++z)
		{
			const auto& bucket = ob_grid.getBucketForIndices(x, y, z);

			for(auto it = bucket.objects.begin(); it != bucket.objects.end(); ++it)
			{
				//num_obs_tested++;

				const PhysicsObject* object = *it;

				RayTraceResult ob_results;
				object->traceRay(ray, ob_results);
				if(ob_results.hit_object && ob_results.hitdist_ws < closest_dist)
				{
					results_out = ob_results;
					results_out.hit_object = object;
					closest_dist = ob_results.hitdist_ws;

					ray.max_t = ob_results.hitdist_ws; // Now that we have hit something, we only need to consider closer hits.
					// NOTE: Could recompute bucket bounds now that max_t is smaller.
				}
			}
			//num_buckets_tested++;
		}

		//conPrint("traceRay(): Testing against " + toString(num_buckets_tested) + " buckets, " + toString(num_obs_tested) + " obs tested, took " + timer.elapsedStringNSigFigs(4));
	}
	else
	{
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
}


bool PhysicsWorld::doesRayHitAnything(const Vec4f& origin, const Vec4f& dir, float max_t) const
{
	const Ray ray(origin, dir, 0.f, max_t);
	
	// Compute AABB of ray path
	js::AABBox ray_path_AABB(origin, origin);
	ray_path_AABB.enlargeToHoldPoint(ray.pointf(max_t));

	// Test against large objects
	for(auto it = large_objects.begin(); it != large_objects.end(); ++it)
	{
		const PhysicsObject* object = *it;
		RayTraceResult ob_results;
		object->traceRay(ray, ob_results);
		if(ob_results.hit_object)
			return true;
	}


	// Test against objects in hashed grid
	const Vec4i min_bucket_i = ob_grid.bucketIndicesForPoint(ray_path_AABB.min_);
	const Vec4i max_bucket_i = ob_grid.bucketIndicesForPoint(ray_path_AABB.max_);

	//Timer timer; 
	//int num_buckets_tested = 0;
	//int num_obs_tested = 0;

	for(int x = min_bucket_i[0]; x <= max_bucket_i[0]; ++x)
	for(int y = min_bucket_i[1]; y <= max_bucket_i[1]; ++y)
	for(int z = min_bucket_i[2]; z <= max_bucket_i[2]; ++z)
	{
		const auto& bucket = ob_grid.getBucketForIndices(x, y, z);

		for(auto it = bucket.objects.begin(); it != bucket.objects.end(); ++it)
		{
			//num_obs_tested++;

			const PhysicsObject* object = *it;
			RayTraceResult ob_results;
			object->traceRay(ray, ob_results);
			if(ob_results.hit_object)
				return true;
		}

		//num_buckets_tested++;
	}

	//conPrint("doesRayHitAnything(): Testing against " + toString(num_buckets_tested) + " buckets, " + toString(num_obs_tested) + " obs tested, took " + timer.elapsedStringNSigFigs(4));

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
		const auto& bucket = ob_grid.getBucketForIndices(x, y, z);

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
		const auto& bucket = ob_grid.getBucketForIndices(x, y, z);

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
