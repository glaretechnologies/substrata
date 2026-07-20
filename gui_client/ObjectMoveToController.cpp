/*=====================================================================
ObjectMoveToController.cpp
--------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "ObjectMoveToController.h"


#include "PhysicsWorld.h"
#include "PhysicsObject.h"
#include "../shared/Protocol.h"
#include <opengl/OpenGLEngine.h>
#include <maths/mathstypes.h>


ObjectMoveToController::ObjectMoveToController(WorldObjectRef controlled_ob_)
:	controlled_ob(controlled_ob_),
	pos_active(false), pos_start(0.f), pos_target(0.f), pos_duration(1.f), pos_easing(Protocol::MoveTo_EasingLinear), pos_elapsed(0.f),
	rot_active(false), rot_start(Quatf::identity()), rot_target(Quatf::identity()), rot_duration(1.f), rot_easing(Protocol::MoveTo_EasingLinear), rot_elapsed(0.f)
{}


ObjectMoveToController::~ObjectMoveToController()
{}


static inline float applyEasing(float frac, uint32 easing)
{
	if(easing == Protocol::MoveTo_EasingSmoothstep)
		return Maths::smoothStep(0.f, 1.f, frac);
	else
		return frac; // MoveTo_EasingLinear
}


void ObjectMoveToController::setPositionTrack(const Vec4f& start_pos, const Vec4f& target_pos, float duration, uint32 easing, float cur_elapsed)
{
	pos_start    = start_pos;
	pos_target   = target_pos;
	pos_duration = myMax(duration, 1.0e-4f);
	pos_easing   = easing;
	pos_elapsed  = myMax(cur_elapsed, 0.f);
	pos_active   = true;
}


void ObjectMoveToController::setRotationTrack(const Quatf& start_rot, const Quatf& target_rot, float duration, uint32 easing, float cur_elapsed)
{
	rot_start    = start_rot;
	rot_target   = target_rot;
	rot_duration = myMax(duration, 1.0e-4f);
	rot_easing   = easing;
	rot_elapsed  = myMax(cur_elapsed, 0.f);
	rot_active   = true;
}


bool ObjectMoveToController::update(PhysicsWorld& physics_world, OpenGLEngine* opengl_engine, float dtime)
{
	// Determine current position.  For an inactive position track, hold the object's current position.
	Vec4f new_pos;
	if(pos_active)
	{
		pos_elapsed += dtime;
		const float frac = myClamp(pos_elapsed / pos_duration, 0.f, 1.f);
		new_pos = Maths::uncheckedLerp(pos_start, pos_target, applyEasing(frac, pos_easing));
		if(pos_elapsed >= pos_duration)
			pos_active = false;
	}
	else
		new_pos = controlled_ob->pos.toVec4fPoint();

	// Determine current rotation.  For an inactive rotation track, hold the object's current orientation.
	Quatf new_rot;
	if(rot_active)
	{
		rot_elapsed += dtime;
		const float frac = myClamp(rot_elapsed / rot_duration, 0.f, 1.f);
		new_rot = Quatf::slerp(rot_start, rot_target, applyEasing(frac, rot_easing));
		if(rot_elapsed >= rot_duration)
			rot_active = false;
	}
	else
		new_rot = Quatf::fromAxisAndAngle(normalise(controlled_ob->axis), controlled_ob->angle);

	// Update the object's world-state transform so it stays consistent (used for world-space AABB, LOD reloads etc.)
	controlled_ob->pos = Vec3d(new_pos[0], new_pos[1], new_pos[2]);
	Vec4f unit_axis;
	float angle;
	new_rot.toAxisAndAngle(unit_axis, angle);
	controlled_ob->axis = Vec3f(unit_axis);
	controlled_ob->angle = angle;

	// Update the physics body.
	if(controlled_ob->physics_object.nonNull())
	{
		PhysicsObject* po = controlled_ob->physics_object.ptr();
		if(po->isKinematic())
			physics_world.moveKinematicObject(*po, new_pos, new_rot, dtime); // Smoothly move; players standing on it are carried along.
		else if(!po->isDynamic())
			physics_world.setNewObToWorldTransform(*po, new_pos, new_rot, /*linear vel=*/Vec4f(0.f), /*angular vel=*/Vec4f(0.f)); // Static body: keep the collision shape in sync.
		// Dynamic bodies are left to the physics simulation.
	}

	// Set the OpenGL transform directly.  We don't rely on the physics->OpenGL sync (which only runs for kinematic
	// path-controlled objects), so this works for static objects too.
	if(opengl_engine && controlled_ob->opengl_engine_ob.nonNull())
	{
		controlled_ob->opengl_engine_ob->ob_to_world_matrix = controlled_ob->obToWorldMatrix();
		opengl_engine->updateObjectTransformData(*controlled_ob->opengl_engine_ob);
	}

	return pos_active || rot_active;
}
