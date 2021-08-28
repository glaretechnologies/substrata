/*=====================================================================
AvatarGraphics.h
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-12 12:24:54 +1300
=====================================================================*/
#pragma once


#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include "../shared/UID.h"
#include "vec3.h"
#include "PCG32.h"
#include "Matrix4f.h"
#include <string>
#include <vector>
struct GLObject;
class OpenGLEngine;


struct AnimEvents
{
	AnimEvents() : footstrike(false) {}
	bool footstrike;
	Vec3d footstrike_pos;
};


/*=====================================================================
AvatarGraphics
--------------

=====================================================================*/
class AvatarGraphics : public ThreadSafeRefCounted
{
public:
	AvatarGraphics();
	~AvatarGraphics();

	static const uint32 ANIM_STATE_IN_AIR = 1; // Is the avatar not touching the ground? Could be jumping or flying etc..
	static const uint32 ANIM_STATE_FLYING = 2; // Is the player flying (e.g. do they have flying movement mode on)

	void setOverallTransform(OpenGLEngine& engine, const Vec3d& pos, const Vec3f& rotation, const Matrix4f& pre_ob_to_world_matrix, uint32 anim_state, double cur_time, double dt, AnimEvents& anim_events_out);

	//void create(OpenGLEngine& engine, const std::string& URL);

	void destroy(OpenGLEngine& engine);
	
	void setSelectedObBeam(OpenGLEngine& engine, const Vec3d& target_pos); // create or update beam
	void hideSelectedObBeam(OpenGLEngine& engine);

	static float walkCyclePeriod() { return 7.f / Maths::get2Pi<float>(); }

	Reference<GLObject> selected_ob_beam;
	
	Reference<GLObject> skinned_gl_ob;
	int loaded_lod_level;

private:
	Vec3f last_rotation;
	Vec3d last_pos;
	Vec3d last_vel;
	Vec3d last_hand_pos;
	Vec3d last_selected_ob_target_pos;
	float cur_sideweays_lean;
	float cur_forwards_lean;

	// Eye saccades:
	Vec4f cur_eye_target_ws;
	Vec4f next_eye_target_ws;

	double saccade_gap;
	double eye_start_transition_time;
	double eye_end_transition_time;

	PCG32 rng;
};


typedef Reference<AvatarGraphics> AvatarGraphicsRef;
