/*=====================================================================
HoverCarPhysics.h
-----------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "PlayerPhysics.h"
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


struct HoverCarPhysicsUpdateEvents
{
};


struct HoverCarPhysicsSettings
{
	GLARE_ALIGNED_16_NEW_DELETE

	Scripting::HoverCarScriptedSettings script_settings;
	float hovercar_mass;
};


/*=====================================================================
HoverCarPhysics
---------------

=====================================================================*/
class HoverCarPhysics : public RefCounted
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	HoverCarPhysics(JPH::BodyID car_body_id, HoverCarPhysicsSettings settings);
	~HoverCarPhysics();

	HoverCarPhysicsUpdateEvents update(PhysicsWorld& physics_world, const PlayerPhysicsInput& physics_input, float dtime);

	Vec4f getFirstPersonCamPos(PhysicsWorld& physics_world) const;

	Matrix4f getBodyTransform(PhysicsWorld& physics_world) const;

	// Sitting position is (0,0,0) in seat space, forwards is (0,1,0), right is (1,0,0)
	Matrix4f getSeatToWorldTransform(PhysicsWorld& physics_world) const;

	Vec4f getLinearVel(PhysicsWorld& physics_world) const;


	HoverCarPhysicsSettings settings;
	uint32 cur_seat_index;
private:
	JPH::BodyID car_body_id;
	float unflip_up_force_time_remaining;
};
