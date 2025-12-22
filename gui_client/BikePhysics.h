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
#include "../audio/AudioEngine.h"
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


struct BikePhysicsSettings
{
	GLARE_ALIGNED_16_NEW_DELETE

	Reference<Scripting::BikeScriptSettings> script_settings;
	float bike_mass;
};


/*=====================================================================
BikePhysics
-----------

=====================================================================*/
class BikePhysics final : public VehiclePhysics
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	BikePhysics(WorldObjectRef object, BikePhysicsSettings settings, PhysicsWorld& physics_world, glare::AudioEngine* audio_engine, const std::string& base_dir_path,
		ParticleManager* particle_manager);
	~BikePhysics();

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
	Matrix4f getSeatToWorldTransformNoScale(PhysicsWorld& physics_world, uint32 seat_index, bool use_smoothed_network_transform) const override;

	Matrix4f getObjectToWorldTransformNoScale(PhysicsWorld& physics_world, bool use_smoothed_network_transform) const override;

	Vec4f getLinearVel(PhysicsWorld& physics_world) const override;

	JPH::BodyID getBodyID() const override { return bike_body_id; }

	const Scripting::VehicleScriptedSettings& getSettings() const override { return *settings.script_settings; }

	void setDebugVisEnabled(bool enabled, OpenGLEngine& opengl_engine) override;
	void updateDebugVisObjects() override;

	void updateDopplerEffect(const Vec4f& listener_linear_vel, const Vec4f& listener_pos) override;

	std::string getUIInfoMsg() override;

private:
	Matrix4f getWheelToWorldTransform(PhysicsWorld& physics_world, int wheel_index) const;
	void removeVisualisationObs();

	BikePhysicsSettings settings;
	WorldObject* world_object;
	PhysicsWorld* m_physics_world;
	OpenGLEngine* m_opengl_engine;
	glare::AudioEngine* m_audio_engine;
	ParticleManager* particle_manager;
	JPH::BodyID bike_body_id;

	PCG32 rng;

	Reference<glare::AudioSource> engine_audio_source;
	Reference<glare::AudioSource> wheel_audio_source[2];

	bool user_in_driver_seat;

	float righting_time_remaining;
	
	JPH::Ref<JPH::VehicleConstraint> vehicle_constraint; // The vehicle constraint

	float cur_steering_right; // in [-1, 1], a magnitude of one corresponding to max steering angle.

	float smoothed_desired_roll_angle;

	float cur_target_tilt_angle;


	JPH::Array<JPH::Vec3> convex_hull_pts; // convex hull points, object space

	// Debug vis:
	bool show_debug_vis_obs;
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
	std::vector<Reference<GLObject>> hand_hold_point_gl_obs;
	
	Vec4f last_desired_up_vec;
	Vec4f last_force_point;
	Vec4f last_force_vec;

	//float last_roll_error;

	int steering_node_i;
	int back_arm_node_i;
	int front_wheel_node_i;
	int back_wheel_node_i;
	int upper_piston_left_node_i;
	int upper_piston_right_node_i;
	int lower_piston_left_node_i;
	int lower_piston_right_node_i;
};