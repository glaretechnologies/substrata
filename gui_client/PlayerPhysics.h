/*=====================================================================
PlayerPhysics.h
---------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "PhysicsObject.h"
#include "PlayerPhysicsInput.h"
#include <physics/jscol_boundingsphere.h>
#include <maths/Vec4f.h>
#include <maths/vec3.h>
#include <vector>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Character/Character.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>


class CameraController;
class PhysicsWorld;


struct UpdateEvents
{
	UpdateEvents() : jumped(false) {}
	bool jumped;
};


/*=====================================================================
PlayerPhysics
-------------

=====================================================================*/
class PlayerPhysics : JPH::CharacterContactListener
{
public:
	PlayerPhysics();
	~PlayerPhysics();

	void init(PhysicsWorld& physics_world, const Vec3d& initial_player_pos);
	void shutdown();

	void setPosition(const Vec3d& new_player_pos, const Vec4f& linear_vel = Vec4f(0,0,0,1)); // Move discontinuously.  Zeroes velocity also.  For teleporting etc.

	// Adds to desired velocity (move_desired_vel).
	void processMoveForwards(float factor, bool runpressed, CameraController& cam); // factor should be -1 for move backwards, 1 otherwise.
	void processStrafeRight(float factor, bool runpressed, CameraController& cam);
	void processMoveUp(float factor, bool runpressed, CameraController& cam);
	void processJump(CameraController& cam, double cur_time);

	// NOTE: cur_time should be from Clock::getTimeSinceInit().
	UpdateEvents update(PhysicsWorld& physics_world, const PlayerPhysicsInput& physics_input, float dtime, double cur_time, Vec4f& campos_out);

	bool isMoveDesiredVelNonZero();
	void zeroMoveDesiredVel();

	void setFlyModeEnabled(bool enabled);
	bool flyModeEnabled() const { return fly_mode; }
	
	bool onGroundRecently() const { return on_ground; }
	//bool onGroundRecently() const { return time_since_on_ground < 0.2f; }

	Vec3f getLastXYPlaneVelRelativeToGround() const { return last_xy_plane_vel_rel_ground; }

	bool isRunPressed() const { return last_runpressed; }

	void debugGetCollisionSpheres(const Vec4f& campos, std::vector<js::BoundingSphere>& spheres_out);

	// JPH::CharacterContactListener interface:
	// Called whenever the character collides with a body. Returns true if the contact can push the character.
	virtual void OnContactAdded(const JPH::CharacterVirtual *inCharacter, const JPH::BodyID &inBodyID2, const JPH::SubShapeID &inSubShapeID2, JPH::RVec3Arg inContactPosition, JPH::Vec3Arg inContactNormal, JPH::CharacterContactSettings &ioSettings) override;

	struct ContactedEvent
	{
		PhysicsObject* ob;
	};
	std::vector<ContactedEvent> contacted_events;

private:
	Vec3f last_xy_plane_vel_rel_ground;

	Vec3f move_desired_vel;

	double last_jump_time; // The time when the user last hit the jump key.
	bool on_ground; // Was the user on the ground as of the last update() call.
	bool last_runpressed; // Was the run key pressed as of the last time processMoveForwards etc. was called?

	bool fly_mode;

	//float time_since_on_ground;

	// This is the amount which the displayed camera position is below the actual physical avatar position.
	// This is to allow the physical avatar position to step up discontinuously, where as the camera position will smoothly increase to match the physical avatar position.
	float campos_z_delta; // campos.z returned will be cam_return = cam_actual.z - campos_z_delta;
	
	JPH::PhysicsSystem* physics_system;

	JPH::Ref<JPH::CharacterVirtual> jolt_character;

	JPH::Ref<JPH::Character> interaction_character;
};
