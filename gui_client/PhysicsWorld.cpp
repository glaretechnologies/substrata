/*=====================================================================
PhysicsWorld.cpp
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "PhysicsWorld.h"


#include <dll/include/IndigoMesh.h>
#include "../graphics/BatchedMesh.h"
#include <simpleraytracer/ray.h>
#include <simpleraytracer/raymesh.h>
#include <utils/StringUtils.h>
#include <utils/ConPrint.h>
#include <utils/Timer.h>
#include <utils/HashMapInsertOnly2.h>
#include <utils/string_view.h>
#include <stdarg.h>
#include <Lock.h>


#if USE_JOLT
#ifndef NDEBUG
#define JPH_PROFILE_ENABLED 1
#endif
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
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#endif
#include <HashSet.h>


#if USE_JOLT
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


// Function that determines if two object layers can collide
static bool MyObjectCanCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2)
{
	switch(inObject1)
	{
	case Layers::NON_MOVING:
		return inObject2 == Layers::MOVING; // Non moving only collides with moving
	case Layers::MOVING:
		return inObject2 != Layers::NON_COLLIDABLE; // Moving collides with everything apart from Layers::NON_COLLIDABLE
	case Layers::NON_COLLIDABLE:
		return false;
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
		mObjectToBroadPhase[Layers::NON_COLLIDABLE] = BroadPhaseLayers::MOVING; // NOTE: this a good thing to do?
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
	case Layers::NON_COLLIDABLE:
		return false;
	default:
		assert(false);
		return false;
	}
}


#endif // USE_JOLT


void PhysicsWorld::init()
{
#if USE_JOLT
	// Register allocation hook
	JPH::RegisterDefaultAllocator();

	// Install callbacks
	JPH::Trace = traceImpl;
	//JPH_IF_ENABLE_ASSERTS(AssertFailed = AssertFailedImpl;)

	// Create a factory
	JPH::Factory::sInstance = new JPH::Factory();

	// Register all Jolt physics types
	JPH::RegisterTypes();
#endif
}


PhysicsWorld::PhysicsWorld(/*PhysicsWorldBodyActivationCallbacks* activation_callbacks_*/)
:	activated_obs(NULL)
#if !USE_JOLT
	//activation_callbacks(activation_callbacks_),
	,ob_grid(/*cell_w=*/32.0, /*num_buckets=*/4096, /*expected_num_items_per_bucket=*/4, /*empty key=*/NULL),
	large_objects(/*empty key=*/NULL, /*expected num items=*/32),
#endif
{
#if USE_JOLT
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
#endif
}


PhysicsWorld::~PhysicsWorld()
{
}


static const int LARGE_OB_NUM_CELLS_THRESHOLD = 32;


inline static JPH::Vec3 toJoltVec3(const Vec4f& v)
{
	return JPH::Vec3(v[0], v[1], v[2]);
}

inline static Vec4f toVec4fVec(const JPH::Vec3& v)
{
	return Vec4f(v.GetX(), v.GetY(), v.GetZ(), 0.f);
}

inline static Vec4f toVec4fPos(const JPH::Vec3& v)
{
	return Vec4f(v.GetX(), v.GetY(), v.GetZ(), 1.f);
}

inline static JPH::Quat toJoltQuat(const Quatf& q)
{
	return JPH::Quat(q.v[0], q.v[1], q.v[2], q.v[3]);
}

inline static Quatf toQuat(const JPH::Quat& q)
{
	return Quatf(q.mValue[0], q.mValue[1], q.mValue[2], q.mValue[3]);
}


void PhysicsWorld::setNewObToWorldTransform(PhysicsObject& object, const Vec4f& translation, const Quatf& rot_quat, const Vec4f& scale)
{
	if(!object.jolt_body_id.IsInvalid()) // If we are updating Jolt state, and this object has a corresponding Jolt object:
	{
		JPH::BodyInterface& body_interface = physics_system->GetBodyInterface();

		body_interface.SetPositionRotationAndVelocity(object.jolt_body_id, /*pos=*/toJoltVec3(translation),
			/*rot=*/toJoltQuat(rot_quat), /*vel=*/JPH::Vec3(0, 0, 0), /*ang vel=*/JPH::Vec3(0, 0, 0));


		// Update scale if needed.  This is a little complicated because we need to use the ScaledShape decorated shape.
		JPH::RefConst<JPH::Shape> cur_shape = body_interface.GetShape(object.jolt_body_id);
		if(cur_shape->GetSubType() == JPH::EShapeSubType::Scaled) // If current Jolt shape is a scaled shape:
		{
			assert(dynamic_cast<const JPH::ScaledShape*>(cur_shape.GetPtr()));
			const JPH::ScaledShape* cur_scaled_shape = static_cast<const JPH::ScaledShape*>(cur_shape.GetPtr());

			if(toJoltVec3(scale) != cur_scaled_shape->GetScale()) // If scale has changed:
			{
				const JPH::Shape* inner_shape = cur_scaled_shape->GetInnerShape(); // Get inner shape

				JPH::RefConst<JPH::Shape> new_shape = new JPH::ScaledShape(inner_shape, toJoltVec3(scale)); // Make new decorated scaled shape with new scale

				// conPrint("Made new scaled shape for new scale");
				body_interface.SetShape(object.jolt_body_id, new_shape, /*inUpdateMassProperties=*/true, JPH::EActivation::DontActivate);
			}
		}
		else // Else if current Jolt shape is not a scaled shape:
		{
			if(maskWToZero(scale) != Vec4f(1,1,1,0)) // And scale is != 1:
			{
				JPH::RefConst<JPH::Shape> new_shape = new JPH::ScaledShape(cur_shape, toJoltVec3(scale));

				// conPrint("Changing to scaled shape");
				body_interface.SetShape(object.jolt_body_id, new_shape, /*inUpdateMassProperties=*/true, JPH::EActivation::DontActivate);
			}
		}

		body_interface.ActivateBody(object.jolt_body_id);
	}
}


void computeToWorldAndToObMatrices(const Vec4f& translation, const Quatf& rot_quat, const Vec4f& scale, Matrix4f& ob_to_world_out, Matrix4f& world_to_ob_out)
{
	// Don't use a zero scale component, because it makes the matrix uninvertible, which breaks various things, including picking and normals.
	Vec4f use_scale = scale;
	if(use_scale[0] == 0) use_scale[0] = 1.0e-6f;
	if(use_scale[1] == 0) use_scale[1] = 1.0e-6f;
	if(use_scale[2] == 0) use_scale[2] = 1.0e-6f;


	const Matrix4f rot = rot_quat.toMatrix();
	Matrix4f new_ob_to_world;
	new_ob_to_world.setColumn(0, rot.getColumn(0) * use_scale[0]);
	new_ob_to_world.setColumn(1, rot.getColumn(1) * use_scale[1]);
	new_ob_to_world.setColumn(2, rot.getColumn(2) * use_scale[2]);
	new_ob_to_world.setColumn(3, setWToOne(translation));

	/*
	inverse:
	= (TRS)^-1
	= S^-1 R^-1 T^-1
	= S^-1 R^T T^-1
	*/
	const Matrix4f rot_inv = rot.getTranspose();
	Matrix4f S_inv_R_inv;

	const Vec4f recip_scale = maskWToZero(div(Vec4f(1.f), use_scale));

	// left-multiplying with a scale matrix is equivalent to multiplying column 0 with the scale vector (s_x, s_y, s_z, 0) etc.
	S_inv_R_inv.setColumn(0, rot_inv.getColumn(0) * recip_scale);
	S_inv_R_inv.setColumn(1, rot_inv.getColumn(1) * recip_scale);
	S_inv_R_inv.setColumn(2, rot_inv.getColumn(2) * recip_scale);
	S_inv_R_inv.setColumn(3, Vec4f(0, 0, 0, 1));

	assert(epsEqual(S_inv_R_inv, Matrix4f::scaleMatrix(recip_scale[0], recip_scale[1], recip_scale[2]) * rot_inv));

	const Matrix4f new_world_to_ob = rightTranslate(S_inv_R_inv, -translation);

	ob_to_world_out = new_ob_to_world;
	world_to_ob_out = new_world_to_ob;
}


void PhysicsWorld::moveKinematicObject(PhysicsObject& object, const Vec4f& translation, const Quatf& rot, float dt)
{
	if(!object.jolt_body_id.IsInvalid()) // If we are updating Jolt state, and this object has a corresponding Jolt object:
	{
		JPH::BodyInterface& body_interface = physics_system->GetBodyInterface();

		if(body_interface.GetMotionType(object.jolt_body_id) == JPH::EMotionType::Kinematic)
		{
			body_interface.MoveKinematic(object.jolt_body_id, toJoltVec3(translation), toJoltQuat(rot), dt);
		}
		else
		{
			//assert(0); // Tried to move a non-kinematic object with MoveKinematic().  Catch this ourself otherwise jolt crashes.
		}
	}
}


// Just store the original material index, so we can recover it in traceRay().
class SubstrataPhysicsMaterial : public JPH::PhysicsMaterial
{
public:
	//JPH_DECLARE_SERIALIZABLE_VIRTUAL(SubstrataPhysicsMaterial)   // NOTE: need this?

	SubstrataPhysicsMaterial(uint32 index_) : index(index_) {}

	virtual const char *					GetDebugName() const override		{ return "SubstrataPhysicsMaterial"; }

	// See: PhysicsMaterial::SaveBinaryState
	//virtual void							SaveBinaryState(StreamOut &inStream) const override;

protected:
	// See: PhysicsMaterial::RestoreBinaryState
	//virtual void							RestoreBinaryState(StreamIn &inStream) override;

public:
	uint32 index;
};


JPH::Ref<JPH::Shape> PhysicsWorld::createJoltShapeForIndigoMesh(const Indigo::Mesh& mesh)
{
	const Indigo::Vector<Indigo::Vec3f>& verts = mesh.vert_positions;
	const Indigo::Vector<Indigo::Triangle>& tris = mesh.triangles;
	const Indigo::Vector<Indigo::Quad>& quads = mesh.quads;
	
	const size_t verts_size = verts.size();
	const size_t final_num_tris_size = tris.size() + quads.size() * 2;
	
	JPH::VertexList vertex_list(verts_size);
	JPH::IndexedTriangleList tri_list(final_num_tris_size);
	
	for(size_t i = 0; i < verts_size; ++i)
	{
		const Indigo::Vec3f& vert = verts[i];
	
		vertex_list[i] = JPH::Float3(vert.x, vert.y, vert.z);
	}
	
	
	for(size_t i = 0; i < tris.size(); ++i)
	{
		const Indigo::Triangle& tri = tris[i];
	
		const uint use_mat_index = tri.tri_mat_index < 32 ? tri.tri_mat_index : 0; // Jolt has a maximum of 32 materials per mesh
		tri_list[i] = JPH::IndexedTriangle(tri.vertex_indices[0], tri.vertex_indices[1], tri.vertex_indices[2], /*inMaterialIndex=*/use_mat_index);
	}

	for(size_t i = 0; i < quads.size(); ++i)
	{
		const Indigo::Quad& quad = quads[i];

		const uint use_mat_index = quad.mat_index < 32 ? quad.mat_index : 0;
		tri_list[tris.size() + i * 2 + 0] = JPH::IndexedTriangle(quad.vertex_indices[0], quad.vertex_indices[1], quad.vertex_indices[2], /*inMaterialIndex=*/use_mat_index);
		tri_list[tris.size() + i * 2 + 1] = JPH::IndexedTriangle(quad.vertex_indices[0], quad.vertex_indices[2], quad.vertex_indices[3], /*inMaterialIndex=*/use_mat_index);
	}
	
	// Create materials
	const uint32 use_num_mats = myMin(32u, mesh.num_materials_referenced); // Jolt has a maximum of 32 materials per mesh
	JPH::PhysicsMaterialList materials(use_num_mats);
	for(uint32 i = 0; i < use_num_mats; ++i)
		materials[i] = new SubstrataPhysicsMaterial(i);
	
	JPH::MeshShapeSettings* mesh_body_settings = new JPH::MeshShapeSettings(vertex_list, tri_list, materials);
	JPH::Result<JPH::Ref<JPH::Shape>> result = mesh_body_settings->Create();
	if(result.HasError())
		throw glare::Exception(std::string("Error building Jolt shape: ") + result.GetError().c_str());
	JPH::Ref<JPH::Shape> shape = result.Get();
	return shape;
}


JPH::Ref<JPH::Shape> PhysicsWorld::createJoltShapeForBatchedMesh(const BatchedMesh& mesh)
{
	const size_t vert_size = mesh.vertexSize();
	const size_t num_verts = mesh.numVerts();
	const size_t num_tris = mesh.numIndices() / 3;

	JPH::VertexList vertex_list(num_verts);
	JPH::IndexedTriangleList tri_list(num_tris);

	const BatchedMesh::VertAttribute* pos_attr = mesh.findAttribute(BatchedMesh::VertAttribute_Position);
	if(!pos_attr)
		throw glare::Exception("Pos attribute not present.");
	if(pos_attr->component_type != BatchedMesh::ComponentType_Float)
		throw glare::Exception("Pos attribute must have float type.");
	const size_t pos_offset = pos_attr->offset_B;

	// Copy Vertices
	const uint8* src_vertex_data = mesh.vertex_data.data();
	for(size_t i = 0; i < num_verts; ++i)
	{
		Vec3f vert_pos;
		std::memcpy(&vert_pos, src_vertex_data + pos_offset + i * vert_size, sizeof(::Vec3f));

		vertex_list[i] = JPH::Float3(vert_pos.x, vert_pos.y, vert_pos.z);
	}

	// Copy Triangles
	const BatchedMesh::ComponentType index_type = mesh.index_type;

	const uint8*  const index_data_uint8  = (const uint8* )mesh.index_data.data();
	const uint16* const index_data_uint16 = (const uint16*)mesh.index_data.data();
	const uint32* const index_data_uint32 = (const uint32*)mesh.index_data.data();

	unsigned int dest_tri_i = 0;
	for(size_t b = 0; b < mesh.batches.size(); ++b)
	{
		const size_t tri_begin = mesh.batches[b].indices_start / 3;
		const size_t tri_end   = tri_begin + mesh.batches[b].num_indices / 3;
		const uint32 mat_index = mesh.batches[b].material_index;

		for(size_t t = tri_begin; t < tri_end; ++t)
		{
			uint32 vertex_indices[3];
			if(index_type == BatchedMesh::ComponentType_UInt8)
			{
				vertex_indices[0] = index_data_uint8[t*3 + 0];
				vertex_indices[1] = index_data_uint8[t*3 + 1];
				vertex_indices[2] = index_data_uint8[t*3 + 2];
			}
			else if(index_type == BatchedMesh::ComponentType_UInt16)
			{
				vertex_indices[0] = index_data_uint16[t*3 + 0];
				vertex_indices[1] = index_data_uint16[t*3 + 1];
				vertex_indices[2] = index_data_uint16[t*3 + 2];
			}
			else if(index_type == BatchedMesh::ComponentType_UInt32)
			{
				vertex_indices[0] = index_data_uint32[t*3 + 0];
				vertex_indices[1] = index_data_uint32[t*3 + 1];
				vertex_indices[2] = index_data_uint32[t*3 + 2];
			}
			else
			{
				throw glare::Exception("Invalid index type.");
			}


			const uint use_mat_index = mat_index < 32 ? mat_index : 0;
			tri_list[dest_tri_i] = JPH::IndexedTriangle(vertex_indices[0], vertex_indices[1], vertex_indices[2], /*inMaterialIndex=*/use_mat_index);

			dest_tri_i++;
		}
	}

	// Create materials
	const uint32 use_num_mats = myMin(32u, (uint32)mesh.numMaterialsReferenced());
	JPH::PhysicsMaterialList materials(use_num_mats);
	for(uint32 i = 0; i < use_num_mats; ++i)
		materials[i] = new SubstrataPhysicsMaterial(i);

	JPH::MeshShapeSettings* mesh_body_settings = new JPH::MeshShapeSettings(vertex_list, tri_list, materials);
	JPH::Result<JPH::Ref<JPH::Shape>> result = mesh_body_settings->Create();
	if(result.HasError())
		throw glare::Exception(std::string("Error building Jolt shape: ") + result.GetError().c_str());
	JPH::Ref<JPH::Shape> shape = result.Get();
	return shape;
}


void PhysicsWorld::addObject(const Reference<PhysicsObject>& object)
{
	assert(object->pos.isFinite());
	assert(object->scale.isFinite());
	assert(object->rot.v.isFinite());

	this->objects_set.insert(object);

	if(object->scale.x == 0 || object->scale.y == 0 || object->scale.z == 0)
	{
		//assert(0);
		return;
	}

	JPH::BodyInterface& body_interface = physics_system->GetBodyInterface();

	if(object->is_player)
	{

	}
	else if(object->is_sphere)
	{
		JPH::SphereShapeSettings* sphere_shape = new JPH::SphereShapeSettings(0.5f);

		JPH::ShapeSettings* final_shape_settings;
		if(object->scale == Vec3f(1.f))
			final_shape_settings = sphere_shape;
		else
			final_shape_settings = new JPH::ScaledShapeSettings(sphere_shape, JPH::Vec3(object->scale[0], object->scale[1], object->scale[2]));

		JPH::BodyCreationSettings sphere_settings(final_shape_settings,
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
		JPH::BoxShapeSettings* cube_shape_settings = new JPH::BoxShapeSettings(JPH::Vec3(0.5f, 0.5f, 0.5f));

		JPH::ShapeSettings* final_shape_settings;
		if(object->scale == Vec3f(1.f))
			final_shape_settings = cube_shape_settings;
		else
			final_shape_settings = new JPH::ScaledShapeSettings(cube_shape_settings, JPH::Vec3(object->scale[0], object->scale[1], object->scale[2]));

		// Create the settings for the body itself. Note that here you can also set other properties like the restitution / friction.
		JPH::BodyCreationSettings cube_settings(final_shape_settings,
			JPH::Vec3(object->pos[0], object->pos[1], object->pos[2]),
			JPH::Quat(object->rot.v[0], object->rot.v[1], object->rot.v[2], object->rot.v[3]),
			object->dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static, 
			Layers::MOVING);
		//cube_settings.mRestitution = 0.5f;
		cube_settings.mUserData = (uint64)object.ptr();
		cube_settings.mFriction = 1.f;

		object->jolt_body_id = body_interface.CreateAndAddBody(cube_settings, JPH::EActivation::Activate);

		//conPrint("Added Jolt cube body, dynamic: " + boolToString(object->dynamic));
	}
	else
	{
		JPH::Ref<JPH::Shape> shape = object->shape.jolt_shape;
		if(shape.GetPtr() == NULL)
			return;

		JPH::Ref<JPH::Shape> final_shape;
		if(object->scale == Vec3f(1.f))
			final_shape = shape;
		else
			final_shape = new JPH::ScaledShape(shape, JPH::Vec3(object->scale[0], object->scale[1], object->scale[2]));

		JPH::BodyCreationSettings settings(final_shape,
			JPH::Vec3(object->pos[0], object->pos[1], object->pos[2]),
			JPH::Quat(object->rot.v[0], object->rot.v[1], object->rot.v[2], object->rot.v[3]),
			object->kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Static,
			object->collidable ? Layers::NON_MOVING : Layers::NON_COLLIDABLE);
		settings.mMassPropertiesOverride.mMass = 100.f;
		settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;

		settings.mUserData = (uint64)object.ptr();
		settings.mFriction = 1.f;

		object->jolt_body_id = body_interface.CreateAndAddBody(settings, JPH::EActivation::Activate);

		//conPrint("Added Jolt mesh body");
	}
}



void PhysicsWorld::removeObject(const Reference<PhysicsObject>& object)
{
	JPH::BodyInterface& body_interface = physics_system->GetBodyInterface();

	// Remove jolt body if it exists
	if(!object->jolt_body_id.IsInvalid())
	{
		body_interface.RemoveBody(object->jolt_body_id);

		body_interface.DestroyBody(object->jolt_body_id);

		object->jolt_body_id = JPH::BodyID();

		//conPrint("Removed Jolt body");
	}

	activated_obs.erase(object.ptr()); // Object should have been removed from there when its Jolt body is removed (and deactivated), but do it again to be safe.

	this->objects_set.erase(object);
}


void PhysicsWorld::think(double dt)
{
	// If you take larger steps than 1 / 60th of a second you need to do multiple collision steps in order to keep the simulation stable. Do 1 collision step per 1 / 60th of a second (round up).
	const int cCollisionSteps = 1;

	// If you want more accurate step results you can do multiple sub steps within a collision step. Usually you would set this to 1.
	const int cIntegrationSubSteps = 1;

	// We simulate the physics world in discrete time steps. 60 Hz is a good rate to update the physics system.
	physics_system->Update((float)dt, cCollisionSteps, cIntegrationSubSteps, temp_allocator, job_system);
}


void PhysicsWorld::updateActiveObjects()
{
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
		if(!ob->jolt_body_id.IsInvalid() && (ob->dynamic || ob->kinematic))
		{
			JPH::Vec3 pos = body_interface.GetCenterOfMassPosition(ob->jolt_body_id);  // NOTE: should we use GetPosition() here?
			JPH::Quat rot = body_interface.GetRotation(ob->jolt_body_id);

			//conPrint("Setting active object " + toString(ob->jolt_body_id.GetIndex()) + " state from jolt: " + toString(pos.GetX()) + ", " + toString(pos.GetY()) + ", " + toString(pos.GetZ()));

			const Vec4f new_pos = toVec4fPos(pos);
			const Quatf new_rot = toQuat(rot);

			ob->rot = new_rot;
			ob->pos = new_pos;
		}
	}
}


// NOTE: may be called from a Jolt thread!
// "Called whenever a body activates, note this can be called from any thread so make sure your code is thread safe."
void PhysicsWorld::OnBodyActivated(const JPH::BodyID& inBodyID, uint64 inBodyUserData)
{
	//conPrint("Jolt body activated");

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
	//conPrint("Jolt body deactivated");

	if(inBodyUserData != 0)
	{
		Lock lock(activated_obs_mutex);
		activated_obs.erase((PhysicsObject*)inBodyUserData);
	}
	//activated_obs.erase(inBodyID);
}


void PhysicsWorld::clear()
{
	// TODO: remove all jolt objects

	this->objects_set.clear();
}


PhysicsWorld::MemUsageStats PhysicsWorld::getTotalMemUsage() const
{
	HashSet<const JPH::Shape*> meshes(/*empty_key=*/NULL, /*expected_num_items=*/objects_set.size());
	MemUsageStats stats;
	stats.num_meshes = 0;
	stats.mem = 0;

	JPH::Shape::VisitedShapes visited_shapes; // Jolt uses this to make sure it doesn't double-count sub-shapes.
	JPH::BodyInterface& body_interface = physics_system->GetBodyInterface();

	for(auto it = objects_set.begin(); it != objects_set.end(); ++it)
	{
		const PhysicsObject* ob = it->getPointer();

		const JPH::Shape* shape = body_interface.GetShape(ob->jolt_body_id).GetPtr(); // Get actual possibly-decorated shape used.
		if(shape)
		{
			const bool added = meshes.insert(shape).second;
			if(added)
			{
				JPH::Shape::Stats shape_stats = shape->GetStatsRecursive(visited_shapes);

				stats.mem += shape_stats.mSizeBytes;
			}
		}
	}

	for(auto it = visited_shapes.begin(); it != visited_shapes.end(); ++it)
	{
		const JPH::Shape* shape = *it;
		if(dynamic_cast<const JPH::MeshShape*>(shape))
			stats.num_meshes++;
	}

	return stats;
}


std::string PhysicsWorld::getDiagnostics() const
{
	const MemUsageStats stats = getTotalMemUsage();
	std::string s;
	s += "Objects: " + toString(objects_set.size()) + "\n";
	s += "Jolt bodies: " + toString(this->physics_system->GetNumBodies()) + "\n";
	s += "Meshes:  " + toString(stats.num_meshes) + "\n";
	s += "mem usage: " + getNiceByteSize(stats.mem) + "\n";

	return s;
}


std::string PhysicsWorld::getLoadedMeshes() const
{
	std::string s;
	HashMapInsertOnly2<const RayMesh*, int64> meshes(/*empty key=*/NULL, objects_set.size());
	for(auto it = objects_set.begin(); it != objects_set.end(); ++it)
	{
		//const PhysicsObject* ob = it->getPointer();
		//const bool added = meshes.insert(std::make_pair(ob->shape->raymesh.ptr(), 0)).second;
		//if(added)
		//{
		//	s += ob->shape->raymesh->getName() + "\n";
		//}
	}

	return s;
}


const Vec4f PhysicsWorld::getPosInJolt(const Reference<PhysicsObject>& object)
{
	JPH::BodyInterface& body_interface = physics_system->GetBodyInterface();

	const JPH::Vec3 pos = body_interface.GetCenterOfMassPosition(object->jolt_body_id);

	return toVec4fPos(pos);
}


void PhysicsWorld::traceRay(const Vec4f& origin, const Vec4f& dir, float max_t, RayTraceResult& results_out) const
{
	results_out.hit_object = NULL;

	const JPH::RRayCast ray(toJoltVec3(origin), toJoltVec3(dir * max_t));
	JPH::RayCastResult hit_result;
	const bool found_hit = this->physics_system->GetNarrowPhaseQuery().CastRay(ray, hit_result);
	if(found_hit)
	{
		// Lock the body.  Use locking interface so we can call body->GetWorldSpaceSurfaceNormal().
		JPH::BodyLockRead lock(physics_system->GetBodyLockInterfaceNoLock(), hit_result.mBodyID);
		assert(lock.Succeeded()); // When this runs all bodies are locked so this should not fail

		const JPH::Body* body = &lock.GetBody();

		const uint64 user_data = body->GetUserData();
		if(user_data != 0)
		{
			results_out.hit_object = (PhysicsObject*)user_data;
			results_out.coords = Vec2f(0.f);
			results_out.hitdist_ws = hit_result.mFraction * max_t;
			results_out.hit_normal_ws = toVec4fVec(body->GetWorldSpaceSurfaceNormal(hit_result.mSubShapeID2, ray.GetPointOnRay(hit_result.mFraction)));

			const JPH::PhysicsMaterial* mat = body->GetShape()->GetMaterial(hit_result.mSubShapeID2);
			const SubstrataPhysicsMaterial* submat = dynamic_cast<const SubstrataPhysicsMaterial*>(mat);
			results_out.hit_mat_index = submat ? submat->index : 0;

			// conPrint("Hit object, hitdist_ws: " + toString(results_out.hitdist_ws) + ", hit_tri_index: " + toString(results_out.hit_tri_index));
		}
	}
}


bool PhysicsWorld::doesRayHitAnything(const Vec4f& origin, const Vec4f& dir, float max_t) const
{
	const JPH::RRayCast ray(toJoltVec3(origin), toJoltVec3(dir * max_t));
	JPH::RayCastResult hit_result;
	const bool found_hit = this->physics_system->GetNarrowPhaseQuery().CastRay(ray, hit_result);
	return found_hit;
}
