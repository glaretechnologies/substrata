/*=====================================================================
CarPhysics.h
------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#pragma once


#include "PlayerPhysics.h"
#include "VehiclePhysics.h"
#include "PhysicsObject.h"
#include "PlayerPhysicsInput.h"
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


struct CarPhysicsSettings
{
	GLARE_ALIGNED_16_NEW_DELETE

	Reference<Scripting::CarScriptSettings> script_settings;
	float car_mass;
};


/*=====================================================================
CarPhysics
----------
Car controller
=====================================================================*/
class CarPhysics final : public VehiclePhysics
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	CarPhysics(WorldObjectRef object, JPH::BodyID car_body_id, CarPhysicsSettings settings, PhysicsWorld& physics_world, glare::AudioEngine* audio_engine, const std::string& base_dir_path,
		ParticleManager* particle_manager);
	~CarPhysics();

	WorldObject* getControlledObject() override { return world_object; }

	void vehicleSummoned() override; // Set engine revs to zero etc.

	void startRightingVehicle() override;

	void userEnteredVehicle(int seat_index) override;

	void userExitedVehicle(int old_seat_index) override;

	VehiclePhysicsUpdateEvents update(PhysicsWorld& physics_world, const PlayerPhysicsInput& physics_input, float dtime) override;

	Vec4f getFirstPersonCamPos(PhysicsWorld& physics_world, uint32 seat_index, bool use_smoothed_network_transform) const override;

	Vec4f getThirdPersonCamTargetTranslation() const override;

	float getThirdPersonCamTraceSelfAvoidanceDist() const override { return 3.f; }

	Matrix4f getBodyTransform(PhysicsWorld& physics_world) const override;

	// Sitting position is (0,0,0) in seat space, forwards is (0,1,0), right is (1,0,0)
	Matrix4f getSeatToWorldTransform(PhysicsWorld& physics_world, uint32 seat_index, bool use_smoothed_network_transform) const override;

	Matrix4f getObjectToWorldTransform(PhysicsWorld& physics_world, bool use_smoothed_network_transform) const override;

	Vec4f getLinearVel(PhysicsWorld& physics_world) const override;

	JPH::BodyID getBodyID() const override { return car_body_id; }

	const Scripting::VehicleScriptedSettings& getSettings() const override { return *settings.script_settings; }

	void setDebugVisEnabled(bool enabled, OpenGLEngine& opengl_engine) override;
	void updateDebugVisObjects() override;

	void updateDopplerEffect(const Vec4f& listener_linear_vel, const Vec4f& listener_pos) override;

	std::string getUIInfoMsg() override;

private:
	Matrix4f getWheelToWorldTransform(PhysicsWorld& physics_world, int wheel_index) const;
	void removeVisualisationObs();

	CarPhysicsSettings settings;

	WorldObject* world_object;
	PhysicsWorld* m_physics_world;
	OpenGLEngine* m_opengl_engine;
	ParticleManager* particle_manager;
	PCG32 rng;

	JPH::Ref<JPH::VehicleConstraint> vehicle_constraint; // The vehicle constraint
	JPH::BodyID car_body_id;
	JPH::Body* jolt_body;
	JPH::Ref<JPH::VehicleCollisionTester> m_tester; // Collision tester for the wheel

	bool user_in_driver_seat;
	float righting_time_remaining;
	float cur_steering_right; // in [-1, 1], a magnitude of one corresponding to max steering angle.

	JPH::Array<JPH::Vec3> convex_hull_pts; // convex hull points, object space

	int wheel_node_indices[4];
	int wheelbrake_node_indices[4];

	// Debug vis:
	bool show_debug_vis_obs;
	Reference<GLObject> coll_tester_gl_ob[4];
	std::vector<Reference<GLObject>> convex_hull_pts_gl_obs;
};
