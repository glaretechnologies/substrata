/*=====================================================================
PhysicsObject.h
---------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "../shared/UID.h"
#include <maths/Vec4f.h>
#include <maths/Quat.h>
#include <maths/vec3.h>
#include <maths/Matrix4f.h>
#include <utils/Vector.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/Reference.h>
#include <physics/jscol_aabbox.h>
#if USE_JOLT
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#endif

class RayTraceResult;


/*=====================================================================
PhysicsShape
------------
Acceleration structure for a mesh.
RayMesh in old code, Jolt shape with Jolt code.
=====================================================================*/
class PhysicsShape
{
public:
	PhysicsShape() : size_B(0) {}

	js::AABBox getAABBOS() const;

#if USE_JOLT
	JPH::Ref<JPH::Shape> jolt_shape;
#endif
	size_t size_B; // Compute this once when the shape is created, as querying it from Jolt requires construction of a std::unordered_set for GetStatsRecursive(), which is slow.
};


/*=====================================================================
PhysicsObject
-------------

=====================================================================*/
class PhysicsObject : public ThreadSafeRefCounted
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	friend class PhysicsWorld;

	PhysicsObject(bool collidable);
	PhysicsObject(bool collidable, const PhysicsShape& shape, void* userdata, int userdata_type);
	~PhysicsObject();


	void traceRay(const Ray& ray, RayTraceResult& results_out) const;

	const js::AABBox getAABBoxWS() const;

	const Matrix4f getSmoothedObToWorldMatrix() const;
	const Matrix4f getSmoothedObToWorldNoScaleMatrix() const;
	const Matrix4f getObToWorldMatrix() const;
	const Matrix4f getObToWorldMatrixNoScale() const;
	const Matrix4f getWorldToObMatrix() const;

public:
	PhysicsShape shape; // This has a ref to the undecorated shape, actual shape used may be a ScaledShape.

	bool collidable; // Is this object solid, for the purposes of player physics?

	void* userdata;
	int userdata_type; // 0 = WorldObject, 1 = Parcel, 2 = InstanceInfo

	UID ob_uid; // Just for debugging.

	// TODO: This state is redundant, since it is stored in Jolt as well.  Remove?
	Vec4f pos;
	Quatf rot; // Set in PhysicsWorld::think() from Jolt data
	Vec3f scale;

	/*
	When we receive a physics snapshot, we set pos and rot instantly to the snapshot values, pos_snap and rot_snap
	The values used for rendering however are 
	pos' = smooth_translation + pos_snap
	and 
	rot' = smooth_rotation * rot_snap
	Where smooth_translation = pos_old - pos_snap
	and smooth_rotation = rot_old * rot_snap^-1

	We then slowly reduce smooth_translation and smooth_rotation over time to zero / identity rotation, until pos' and pos_snap converge.
	*/
	Vec4f smooth_translation;
	Quatf smooth_rotation;

#if USE_JOLT
	JPH::BodyID jolt_body_id;
	bool is_sphere;
	bool is_cube;
#endif
	bool dynamic;
	bool kinematic;

	float mass;
	float friction;
	float restitution;
};


typedef Reference<PhysicsObject> PhysicsObjectRef;


struct PhysicsObjectHash
{
	size_t operator() (const PhysicsObjectRef& ob) const
	{
		return (size_t)ob.getPointer() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};
