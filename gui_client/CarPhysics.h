/*=====================================================================
CarPhysics.h
------------
Copyright Glare Technologies Limited 2022 -
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

	void userEnteredVehicle(int seat_index) override; // Should set cur_seat_index

	void userExitedVehicle(int old_seat_index) override; // Should set cur_seat_index

	VehiclePhysicsUpdateEvents update(PhysicsWorld& physics_world, const PlayerPhysicsInput& physics_input, float dtime) override;

	Vec4f getFirstPersonCamPos(PhysicsWorld& physics_world, uint32 seat_index, bool use_smoothed_network_transform) const override;

	Vec4f getThirdPersonCamTargetTranslation() const override;

	float getThirdPersonCamTraceSelfAvoidanceDist() const override { return 1.6f; }

	Matrix4f getBodyTransform(PhysicsWorld& physics_world) const override;

	// Sitting position is (0,0,0) in seat space, forwards is (0,1,0), right is (1,0,0)
	Matrix4f getSeatToWorldTransform(PhysicsWorld& physics_world, uint32 seat_index, bool use_smoothed_network_transform) const override;

	Vec4f getLinearVel(PhysicsWorld& physics_world) const override;

	JPH::BodyID getBodyID() const override { return car_body_id; }

	const Scripting::VehicleScriptedSettings& getSettings() const override { return *settings.script_settings; }

	void updateDebugVisObjects(OpenGLEngine& opengl_engine, bool should_show) override;

	void updateDopplerEffect(const Vec4f& listener_linear_vel, const Vec4f& listener_pos) override;

	std::string getUIInfoMsg() override;

private:
	Matrix4f getWheelToWorldTransform(PhysicsWorld& physics_world, int wheel_index) const;
	void removeVisualisationObs();

	CarPhysicsSettings settings;

	JPH::BodyID car_body_id;
	WorldObject* world_object;
	PhysicsWorld* m_physics_world;
	OpenGLEngine* m_opengl_engine;
	ParticleManager* particle_manager;
	PCG32 rng;

#if USE_JOLT
	JPH::Ref<JPH::VehicleConstraint> vehicle_constraint; // The vehicle constraint
	JPH::Body* jolt_body;
	JPH::Ref<JPH::VehicleCollisionTester>	m_tester; // Collision testers for the wheel
#endif


	bool user_in_driver_seat;
	float righting_time_remaining;
	float cur_steering_right; // in [-1, 1], a magnitude of one corresponding to max steering angle.

	JPH::Array<JPH::Vec3> convex_hull_pts; // convex hull points, object space

	// Debug vis:
	Reference<GLObject> body_gl_ob;
	Reference<GLObject> wheel_attach_point_gl_ob[2];
	Reference<GLObject> wheel_gl_ob[2];
	Reference<GLObject> coll_tester_gl_ob[2];
	Reference<GLObject> contact_point_gl_ob[2];
	Reference<GLObject> contact_laterial_force_gl_ob[2];
	Reference<GLObject> contact_suspension_force_gl_ob[2];
	Reference<GLObject> righting_force_gl_ob;
	Reference<GLObject> desired_bike_up_vec_gl_ob;
	std::vector<Reference<GLObject>> convex_hull_pts_gl_obs;


	int wheel_node_indices[4];
	int wheelbrake_node_indices[4];
};
