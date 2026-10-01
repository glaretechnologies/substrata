/*=====================================================================
SnowboardPhysics.h
-----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "VehiclePhysics.h"
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
struct GLObject;


/*=====================================================================
SnowboardPhysics
----------------
A sliding board supported by four hidden sphere-cast suspension contacts.
Gravity supplies downhill acceleration; input supplies a small push, edge
steering, braking and a grounded jump. A rounded hull handles obstacles.
=====================================================================*/
class SnowboardPhysics final : public VehiclePhysics
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	SnowboardPhysics(WorldObject* object, Reference<Scripting::SnowboardScriptSettings> settings, PhysicsWorld& world);
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

	OpenGLEngine* m_opengl_engine;
	bool show_debug_vis_obs;
	Reference<GLObject> foot_point_gl_obs[2];

	WorldObject* object;
	Reference<Scripting::SnowboardScriptSettings> settings;
	PhysicsWorld* physics_world;
	JPH::BodyID body_id;
	JPH::RefConst<JPH::Shape> original_shape;
	JPH::RefConst<JPH::Shape> riding_shape;
	bool suspension_enabled;
	JPH::Ref<JPH::VehicleConstraint> vehicle_constraint;
	Vec4f ground_normal;
	float original_friction;
	bool original_enhanced_internal_edge_removal;
	bool original_ignore_terrain_contacts;
	bool occupied;
	bool jump_was_down;
	bool grounded;
	float jump_cooldown;
	float righting_time;
	float crouch;
	float steering;
	float rider_lean;
};
