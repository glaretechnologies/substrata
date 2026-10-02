/*=====================================================================
SnowboardPhysics.h
-----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "VehiclePhysics.h"
#include <maths/PCG32.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
struct GLObject;
class ParticleManager;


/*=====================================================================
SnowboardPhysics
----------------
A sliding board supported by four hidden sphere-cast suspension contacts.
Gravity supplies downhill acceleration; input supplies a small push, edge
steering, braking and a grounded jump, charged by holding Space and released
on key up. A rounded hull handles obstacles.
=====================================================================*/
class SnowboardPhysics final : public VehiclePhysics
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	SnowboardPhysics(WorldObject* object, Reference<Scripting::SnowboardScriptSettings> settings, PhysicsWorld& world, ParticleManager* particle_manager);
	~SnowboardPhysics();

	WorldObject* getControlledObject() override { return object; }

	void startRightingVehicle() override { righting_time = 2.0f; }

	void userEnteredVehicle(int seat_index) override;

	void userExitedVehicle(int old_seat_index) override;

	VehiclePhysicsUpdateEvents update(PhysicsWorld& world, const PlayerPhysicsInput& input, float dt) override;

	Vec4f getFirstPersonCamPos(PhysicsWorld& world, uint32 seat_index, bool smoothed) const override;

	Vec4f getThirdPersonCamTargetTranslation() const override { return Vec4f(0,0,0,0); }

	float getThirdPersonCamTraceSelfAvoidanceDist() const override { return 1.0f; }

	Matrix4f getBodyTransform(PhysicsWorld& world) const override;

	Matrix4f getSeatToWorldTransformNoScale(PhysicsWorld& world, uint32 seat_index, bool smoothed) const override;

	Matrix4f getObjectToWorldTransformNoScale(PhysicsWorld& world, bool smoothed) const override;

	Vec4f getLinearVel(PhysicsWorld& world) const override;

	JPH::BodyID getBodyID() const override { return body_id; }

	const Scripting::VehicleScriptedSettings& getSettings() const override { return *settings; }

	void updateRiderPose(PoseConstraint& pose) const override;
	void setDebugVisEnabled(bool enabled, OpenGLEngine& opengl_engine) override;
	void updateDebugVisObjects() override;

	std::string getUIInfoMsg() override;

	static void test();

private:
	void setSuspensionEnabled(bool enabled);
	void removeVisualisationObs();
	void emitCarveParticles(const Matrix4f& board_to_world, const Vec4f& normal, const Vec4f& tangent_right, const Vec4f& relative_vel, float edge, float dt);

	OpenGLEngine* m_opengl_engine;
	bool show_debug_vis_obs;
	Reference<GLObject> foot_point_gl_obs[2];
	Reference<GLObject> collision_aabb_gl_ob;

	ParticleManager* particle_manager; // May be null.
	PCG32 rng;
	float carve_particles_to_emit; // Fractional particle count carried between updates.
	Vec4f board_bottom_centre_bs; // Centre of the hull's underside, in board space.
	float board_half_width; // Half extent along board-space X.
	float board_half_length; // Half extent along board-space Y.

	WorldObject* object;
	Reference<Scripting::SnowboardScriptSettings> settings;
	PhysicsWorld* physics_world;
	JPH::BodyID body_id;
	JPH::RefConst<JPH::Shape> original_shape;
	JPH::RefConst<JPH::Shape> riding_shape;
	JPH::RefConst<JPH::Shape> raised_riding_shape;
	bool suspension_enabled;
	JPH::Ref<JPH::VehicleConstraint> vehicle_constraint;
	Vec4f ground_normal;
	float original_friction;
	bool original_enhanced_internal_edge_removal;
	JPH::ObjectLayer original_object_layer;
	bool occupied;
	bool jump_was_down;
	float jump_charge_time; // How long Space has been held, capped at the full-charge time.
	bool grounded;
	float jump_cooldown;
	float righting_time;
	float crouch;
	float steering;
	float forward_direction; // +1 or -1, selected from longitudinal ground-relative velocity.
	float rider_lean;
};
