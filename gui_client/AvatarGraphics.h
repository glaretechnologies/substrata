/*=====================================================================
AvatarGraphics.h
----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include "../shared/UID.h"
#include "vec3.h"
#include "PCG32.h"
#include "Matrix4f.h"
#include "Quat.h"
#include <string>
#include <vector>
struct GLObject;
class OpenGLEngine;


struct AnimEvents
{
	AnimEvents() : footstrike(false) {}
	bool footstrike;
	Vec3d footstrike_pos;

	Vec4f blob_sphere_positions[4];
	int num_blobs;
};


struct AnimToPlay
{
	AnimToPlay() : anim_i(-1), play_end_time(-1) {}
	int anim_i;
	double play_end_time;
	bool animated_head;
};


/*=====================================================================
AvatarGraphics
--------------

Animation system for avatars.

TODO:

There is an issue with jumping not playing the jumping animation immediately sometimes.

This is an instance of a more general problem that stems from the way the animation transitions work, as blends from one animation (A) to another one (B).

We can't currently interrupt this transition from A to B, if we suddenly want to transition to C instead.

We could solve this by allowing blends of more than 2 animations, or by not blending animations but instead using the current bone positions and velocities and blending to the target positions and velocities.

=====================================================================*/
class AvatarGraphics : public ThreadSafeRefCounted
{
public:
	AvatarGraphics();
	~AvatarGraphics();

	static const uint32 ANIM_STATE_IN_AIR = 1; // Is the avatar not touching the ground? Could be jumping or flying etc..
	static const uint32 ANIM_STATE_FLYING = 2; // Is the player flying (e.g. do they have flying movement mode on)

	void setOverallTransform(OpenGLEngine& engine, const Vec3d& pos, const Vec3f& rotation, const Matrix4f& pre_ob_to_world_matrix, uint32 anim_state, double cur_time, double dt, AnimEvents& anim_events_out);

	void build();
	//void create(OpenGLEngine& engine, const std::string& URL);

	void destroy(OpenGLEngine& engine);
	
	void setSelectedObBeam(OpenGLEngine& engine, const Vec3d& target_pos); // create or update beam
	void hideSelectedObBeam(OpenGLEngine& engine);

	// These are just measured with mk.1 eyeball and a stopwatch.
	static float walkCyclePeriod() { return 1.015f; }
	static float runCyclePeriod() { return  0.7f; }

	Vec4f getLastHeadPosition();

	void performGesture(double cur_time, const std::string& gesture_name, bool animate_head, bool loop_anim);
	void stopGesture(double cur_time/*, const std::string& gesture_name*/);

	Reference<GLObject> selected_ob_beam;
	
	Reference<GLObject> skinned_gl_ob;
	int loaded_lod_level;

private:
	Vec3f avatar_rotation_at_turn_start;
	Vec3f avatar_rotation; // The avatar rotation is decoupled from the camera rotation.  The avatar will perform a turn animation when the difference becomes too large.
	Vec3f last_cam_rotation;
	Vec3d last_pos;
	Vec3d last_vel;
	Vec3d last_hand_pos;
	Vec3d last_selected_ob_target_pos;
	float cur_sideweays_lean;
	float cur_forwards_lean;

	// Eye saccades:
	Vec4f cur_eye_target_os;
	Vec4f next_eye_target_os;

	double saccade_gap;
	double eye_start_transition_time;
	double eye_end_transition_time;

	double last_cam_rotation_time;


	AnimToPlay gesture_anim;
	AnimToPlay next_gesture_anim;

	float cur_head_rot_z;
	//Quatf cur_head_rot_quat;
	Quatf gesture_neck_quat;
	Quatf gesture_head_quat;

	double turn_anim_end_time;
	bool turning;
	bool turning_left;

	PCG32 rng;

	Reference<GLObject> debug_avatar_basis_ob;

	int idle_anim_i;
	int walking_anim_i;
	int walking_backwards_anim_i;
	int running_anim_i;
	int running_backwards_anim_i;
	int floating_anim_i;
	int flying_anim_i;
	int turn_left_anim_i;
	int turn_right_anim_i;

	//int root_node_i;
	int neck_node_i;
	int head_node_i;
	int left_eye_node_i;
	int right_eye_node_i;
	int left_foot_node_i;
	int right_foot_node_i;
	int left_knee_node_i;
	int right_knee_node_i;

	int hips_node_i;

	int spine2_node_i;
};


typedef Reference<AvatarGraphics> AvatarGraphicsRef;
