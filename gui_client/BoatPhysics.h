/*=====================================================================
BoatPhysics.h
-------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "VehiclePhysics.h"
#include "PlayerPhysics.h"
#include "PhysicsObject.h"
#include "PlayerPhysicsInput.h"
#include "Scripting.h"
#include "../physics/jscol_boundingsphere.h"
#include "../maths/Vec4f.h"
#include "../maths/vec3.h"
#include "../maths/PCG32.h"
#include <vector>
#include <Jolt/Jolt.h>


class CameraController;
class PhysicsWorld;
class ParticleManager;
class TerrainDecalManager;


struct BoatPhysicsSettings
{
	GLARE_ALIGNED_16_NEW_DELETE

	Reference<Scripting::BoatScriptSettings> script_settings;
	float boat_mass;
};


/*=====================================================================
BoatPhysics
---------------

=====================================================================*/
class BoatPhysics final : public VehiclePhysics
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	BoatPhysics(WorldObjectRef object, JPH::BodyID boat_body_id, BoatPhysicsSettings settings, PhysicsWorld& physics_world, ParticleManager* particle_manager,
		TerrainDecalManager* terrain_decal_manager);
	~BoatPhysics();

	WorldObject* getControlledObject() override { return world_object; }

	void startRightingVehicle() override;

	void userEnteredVehicle(int seat_index) override; // Should set cur_seat_index;

	void userExitedVehicle(int old_seat_index) override; // Should set cur_seat_index

	VehiclePhysicsUpdateEvents update(PhysicsWorld& physics_world, const PlayerPhysicsInput& physics_input, float dtime) override;

	Vec4f getFirstPersonCamPos(PhysicsWorld& physics_world, uint32 seat_index, bool use_smoothed_network_transform) const override;

	Vec4f getThirdPersonCamTargetTranslation() const override;

	float getThirdPersonCamTraceSelfAvoidanceDist() const override { return 3; }

	Matrix4f getBodyTransform(PhysicsWorld& physics_world) const override;

	// Sitting position is (0,0,0) in seat space, forwards is (0,1,0), right is (1,0,0)
	Matrix4f getSeatToWorldTransform(PhysicsWorld& physics_world, uint32 seat_index, bool use_smoothed_network_transform) const override;

	Vec4f getLinearVel(PhysicsWorld& physics_world) const override;

	JPH::BodyID getBodyID() const override { return body_id; }

	const Scripting::VehicleScriptedSettings& getSettings() const override { return *settings.script_settings; }

private:
	WorldObject* world_object;
	ParticleManager* particle_manager;
	TerrainDecalManager* terrain_decal_manager;
	BoatPhysicsSettings settings;
	JPH::BodyID body_id;
	bool user_in_driver_seat;

	PCG32 rng;

	float righting_time_remaining;
	float shape_volume;
	
	struct SplashPoint
	{
		GLARE_ALIGNED_16_NEW_DELETE

		Vec4f pos_os;
		float right_sign; // +1 if right side, -1 if on left side.
		//bool immersed;
	};
	js::Vector<SplashPoint> splash_points;
};
