/*=====================================================================
HoverCarPhysics.h
-----------------
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
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>


class CameraController;
class PhysicsWorld;
class ParticleManager;


struct HoverCarPhysicsSettings
{
	GLARE_ALIGNED_16_NEW_DELETE

	Reference<Scripting::HoverCarScriptSettings> script_settings;
	float hovercar_mass;
};


/*=====================================================================
HoverCarPhysics
---------------

=====================================================================*/
class HoverCarPhysics final : public VehiclePhysics
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	HoverCarPhysics(WorldObjectRef object, JPH::BodyID car_body_id, HoverCarPhysicsSettings settings, PhysicsWorld& physics_world, ParticleManager* particle_manager);
	~HoverCarPhysics();

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
	Matrix4f getSeatToWorldTransformNoScale(PhysicsWorld& physics_world, uint32 seat_index, bool use_smoothed_network_transform) const override;

	Matrix4f getObjectToWorldTransformNoScale(PhysicsWorld& physics_world, bool use_smoothed_network_transform) const override;

	Vec4f getLinearVel(PhysicsWorld& physics_world) const override;

	virtual JPH::BodyID getBodyID() const override { return car_body_id; }

	const Scripting::VehicleScriptedSettings& getSettings() const override { return *settings.script_settings; }

	void setDebugVisEnabled(bool enabled, OpenGLEngine& opengl_engine) override;
	void updateDebugVisObjects() override;

private:
	void removeVisualisationObs();

	WorldObject* world_object;
	HoverCarPhysicsSettings settings;
	JPH::BodyID car_body_id;
	float unflip_up_force_time_remaining;
	bool user_in_driver_seat;

	PCG32 rng;
	ParticleManager* particle_manager;

	PhysicsWorld* m_physics_world;
	OpenGLEngine* m_opengl_engine;

	// Debug vis:
	bool show_debug_vis_obs;
	Reference<GLObject> raycast_origin_gl_ob;

	Vec4f last_trace_origin_ws;
};
