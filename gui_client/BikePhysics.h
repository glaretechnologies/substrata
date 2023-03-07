/*=====================================================================
BikePhysics.h
-------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "PlayerPhysics.h"
#include "VehiclePhysics.h"
#include "PhysicsObject.h"
#include "PlayerPhysicsInput.h"
#include "Scripting.h"
#include "../physics/jscol_boundingsphere.h"
#include "../maths/Vec4f.h"
#include "../maths/vec3.h"
#include <vector>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>


class CameraController;
class PhysicsWorld;


struct BikePhysicsSettings
{
	GLARE_ALIGNED_16_NEW_DELETE

	Scripting::VehicleScriptedSettings script_settings;
	float bike_mass;
};


/*=====================================================================
BikePhysics
-----------

=====================================================================*/
class BikePhysics : public VehiclePhysics
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	BikePhysics(JPH::BodyID car_body_id, BikePhysicsSettings settings, PhysicsWorld& physics_world);
	~BikePhysics();

	//virtual void shutdown(PhysicsWorld& physics_world);

	VehiclePhysicsUpdateEvents update(PhysicsWorld& physics_world, const PlayerPhysicsInput& physics_input, float dtime);

	Vec4f getFirstPersonCamPos(PhysicsWorld& physics_world) const;

	Matrix4f getBodyTransform(PhysicsWorld& physics_world) const;


	Matrix4f getWheelToWorldTransform(PhysicsWorld& physics_world, int wheel_index) const;

	// Sitting position is (0,0,0) in seat space, forwards is (0,1,0), right is (1,0,0)
	Matrix4f getSeatToWorldTransform(PhysicsWorld& physics_world) const;

	Vec4f getLinearVel(PhysicsWorld& physics_world) const;

	virtual const Scripting::VehicleScriptedSettings& getSettings() const { return settings.script_settings; }

	BikePhysicsSettings settings;
	uint32 cur_seat_index;
	
private:
	PhysicsWorld* m_physics_world;
	JPH::BodyID car_body_id;
	float unflip_up_force_time_remaining;


	JPH::Ref<JPH::VehicleConstraint>		vehicle_constraint; // The vehicle constraint
	JPH::Ref<JPH::VehicleCollisionTester>	collision_tester; // Collision testers for the wheel

	float cur_steering_right;

	float smoothed_desired_roll_angle;


	//JPH::AngleConstraintPart				roll_constraint;
};