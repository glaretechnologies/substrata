/*=====================================================================
VehiclePhysics.h
----------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "PlayerPhysicsInput.h"
#include "Scripting.h"
#include "../maths/Vec4f.h"
#include "../maths/vec3.h"


class CameraController;
class PhysicsWorld;


struct VehiclePhysicsUpdateEvents
{
};



/*=====================================================================
VehiclePhysics
--------------
A physics controller for vehicles
=====================================================================*/
class VehiclePhysics : public RefCounted
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	VehiclePhysics() : cur_seat_index(0) {}
	virtual ~VehiclePhysics() {}

	virtual VehiclePhysicsUpdateEvents update(PhysicsWorld& physics_world, const PlayerPhysicsInput& physics_input, float dtime) = 0;

	virtual Vec4f getFirstPersonCamPos(PhysicsWorld& physics_world) const = 0;

	virtual Matrix4f getBodyTransform(PhysicsWorld& physics_world) const = 0;

	// Sitting position is (0,0,0) in seat space, forwards is (0,1,0), right is (1,0,0)
	virtual Matrix4f getSeatToWorldTransform(PhysicsWorld& physics_world) const = 0;

	virtual Vec4f getLinearVel(PhysicsWorld& physics_world) const = 0;

	virtual const Scripting::VehicleScriptedSettings& getSettings() const = 0;

	uint32 cur_seat_index;
};
