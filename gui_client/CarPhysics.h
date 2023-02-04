/*=====================================================================
CarPhysics.h
------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "PlayerPhysics.h"
#include "PhysicsObject.h"
#include "PlayerPhysicsInput.h"
#include "../physics/jscol_boundingsphere.h"
#include "../maths/Vec4f.h"
#include "../maths/vec3.h"
#include <vector>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>


class CameraController;
class PhysicsWorld;
class ThreadContext;


struct CarPhysicsUpdateEvents
{
};


/*=====================================================================
CarPhysics
----------

=====================================================================*/
class CarPhysics
{
public:
	CarPhysics();
	~CarPhysics();

	void init(PhysicsWorld& physics_world);
	void shutdown();

	CarPhysicsUpdateEvents update(PhysicsWorld& physics_world, const PlayerPhysicsInput& physics_input, float dtime, Vec4f& campos_in_out);

	Matrix4f getWheelTransform(int i);
	Matrix4f getBodyTransform();

private:
#if USE_JOLT
	JPH::Body* jolt_body;

	JPH::Ref<JPH::VehicleConstraint>		mVehicleConstraint;		///< The vehicle constraint
	JPH::Ref<JPH::VehicleCollisionTester>	mTester;				///< Collision testers for the wheel
	//float						mPreviousForward = 1.0f;			///< Keeps track of last car direction so we know when to brake and when to accelerate
#endif
	float cur_steering_right;
};
