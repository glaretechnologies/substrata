/*=====================================================================
PlayerPhysics.h
---------------
Copyright Glare Technologies Limited 2021 -
File created by ClassTemplate on Mon Sep 23 15:14:04 2002
=====================================================================*/
#pragma once


#include "PhysicsObject.h"
#include "PlayerPhysicsInput.h"
#include "../physics/jscol_boundingsphere.h"
#include "../maths/Vec4f.h"
#include "../maths/vec3.h"
#include <vector>

#define USE_JOLT_PLAYER_PHYSICS 1

#if USE_JOLT_PLAYER_PHYSICS
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Character/Character.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#endif

class CameraController;
class PhysicsWorld;
class ThreadContext;


struct SpringSphereSet
{
	std::vector<Vec4f> collpoints;
	js::BoundingSphere sphere;
};

struct UpdateEvents
{
	UpdateEvents() : jumped(false) {}
	bool jumped;
};


/*=====================================================================
PlayerPhysics
-------------

=====================================================================*/
class PlayerPhysics
{
public:
	PlayerPhysics();
	~PlayerPhysics();

	void init(PhysicsWorld& physics_world, const Vec3d& initial_player_pos);
	void shutdown();

	void setPosition(const Vec3d& new_player_pos); // Move discontinuously.  For teleporting etc.

	void processMoveForwards(float factor, bool runpressed, CameraController& cam); // factor should be -1 for move backwards, 1 otherwise.
	void processStrafeRight(float factor, bool runpressed, CameraController& cam);
	void processMoveUp(float factor, bool runpressed, CameraController& cam);
	void processJump(CameraController& cam);

	UpdateEvents update(PhysicsWorld& physics_world, const PlayerPhysicsInput& physics_input, float dtime, Vec4f& campos_in_out);

	bool isMoveImpulseNonZero();
	void zeroMoveImpulse();

	void setFlyModeEnabled(bool enabled);
	bool flyModeEnabled() const { return flymode; }
	
#if USE_JOLT
	bool onGroundRecently() const { return onground; }
#else
	bool onGroundRecently() const { return time_since_on_ground < 0.2f; }
#endif

	Vec3f getLastXYPlaneVelRelativeToGround() const { return last_xy_plane_vel_rel_ground; }

	bool isRunPressed() const { return last_runpressed; }

	void debugGetCollisionSpheres(const Vec4f& campos, std::vector<js::BoundingSphere>& spheres_out);
private:
	Vec3f vel;
	Vec3f lastvel;

	Vec3f last_xy_plane_vel_rel_ground;

	Vec3f moveimpulse;
	Vec3f lastgroundnormal;

	//AGENTREF lastgroundagent;
	//Agent* lastgroundagent;

	float jumptimeremaining;
	bool onground;
	bool last_runpressed;

	bool flymode;

	//float time_since_on_ground;
	

	std::vector<SpringSphereSet> springspheresets;

	// This is the amount which the displayed camera position is below the actual physical avatar position.
	// This is to allow the physical avatar position to step up discontinuously, where as the camera position will smoothly increase to match the physical avatar position.
	float campos_z_delta; // campos.z returned will be cam_return = cam_actual.z - campos_z_delta;
	
#if USE_JOLT_PLAYER_PHYSICS
	//JPH::Ref<JPH::Character> jolt_character;
	JPH::Ref<JPH::CharacterVirtual> jolt_character;
#endif
};
