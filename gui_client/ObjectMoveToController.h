/*=====================================================================
ObjectMoveToController.h
------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "../shared/WorldObject.h"
#include <maths/Vec4f.h>
#include <maths/Quat.h>
#include <utils/MemAlloc.h>
#include <utils/RefCounted.h>
#include <utils/Reference.h>


class PhysicsWorld;
class OpenGLEngine;


/*=====================================================================
ObjectMoveToController
----------------------
Client-side controller that smoothly moves and/or rotates an object to a target transform over some duration, in response
to the scripted moveTo()/rotateTo() Lua functions (which are evaluated on the server, see SubstrataLuaVM.cpp).

The controller holds an independent position track (moveTo) and rotation track (rotateTo), each of which can be
(re)started independently.  Interpolation is a closed-form function of the elapsed time seeded from the shared global
clock at message-receive time, so all clients converge to the same transform.

The controlled object is driven as a kinematic physics object (via PhysicsWorld::moveKinematicObject()), like
ObjectPathController, so that players standing on it are carried along.
=====================================================================*/
class ObjectMoveToController : public RefCounted
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	ObjectMoveToController(WorldObjectRef controlled_ob);
	~ObjectMoveToController();

	// Start or replace the position track.  cur_elapsed is (current global time - move start time), clamped to >= 0, so
	// that a delayed or late-joining message still lands at the correct point.
	void setPositionTrack(const Vec4f& start_pos, const Vec4f& target_pos, float duration, uint32 easing, float cur_elapsed);

	// Start or replace the rotation track.
	void setRotationTrack(const Quatf& start_rot, const Quatf& target_rot, float duration, uint32 easing, float cur_elapsed);

	// Advance the active tracks by dtime and update the object's physics/render transform.
	// Returns true if a track is still active, false if the move has finished (in which case the caller should remove this controller).
	bool update(PhysicsWorld& physics_world, OpenGLEngine* opengl_engine, float dtime);

	bool isActive() const { return pos_active || rot_active; }

	WorldObjectRef controlled_ob;
private:
	bool pos_active;
	Vec4f pos_start, pos_target;
	float pos_duration;
	uint32 pos_easing;
	float pos_elapsed;

	bool rot_active;
	Quatf rot_start, rot_target;
	float rot_duration;
	uint32 rot_easing;
	float rot_elapsed;
};


struct ObjectMoveToControllerRefHash
{
	size_t operator() (const Reference<ObjectMoveToController>& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};
