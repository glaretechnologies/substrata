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
class AnimationManager;
class ResourceManager;


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




#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable:4324) // Disable 'structure was padded due to __declspec(align())' warning.
#endif
struct PoseConstraint
{
	GLARE_ALIGNED_16_NEW_DELETE

	PoseConstraint() : sitting(false), upper_leg_rot_around_thigh_bone_angle(0) {}

	// For sitting:
	Matrix4f seat_to_world; // Sitting position is (0,0,0) in seat space, forwards is (0,1,0), right is (1,0,0).  Should just have rotation and translation, no scaling.
	Quatf model_to_y_forwards_rot_1;
	Quatf model_to_y_forwards_rot_2;
	float upper_body_rot_angle; // radians.  Positive number means lean back.
	float upper_leg_rot_angle; // radians.  Positive number means bend leg forwards at hip.
	float upper_leg_rot_around_thigh_bone_angle; // radians.  positive number rotates leg around thigh bone to move lower leg outwards
	float upper_leg_apart_angle; // radians.  
	float lower_leg_rot_angle; // radians.  Negative number means bend lower leg backwards at knee.  Rotation is relative to upper leg.
	float lower_leg_apart_angle; // radians.
	float rotate_foot_out_angle; // radians.
	float arm_down_angle; // radians.  from overhead
	float arm_out_angle; // radians. from straight out in front
	float upper_arm_shoulder_lift_angle;
	float lower_arm_up_angle; // radians.  From straight out from upper arm.  Positive number means bend lower arm forwards at elbow.
	Vec4f left_hand_hold_point_ws;
	Vec4f right_hand_hold_point_ws;

	bool sitting;
};

#ifdef _WIN32
#pragma warning(pop)
#endif


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

	// anim_state flags
	static const uint32 ANIM_STATE_IN_AIR = 1; // Is the avatar not touching the ground? Could be jumping or flying etc..
	static const uint32 ANIM_STATE_FLYING = 2; // Is the player flying (e.g. do they have flying movement mode on)
	static const uint32 ANIM_STATE_MOVE_IMPULSE_ZERO = 4; // Is the player not pressing down any move keys.

	// xyplane_speed_rel_ground_override is the speed from the local physics sim.
	void setOverallTransform(OpenGLEngine& engine, const Vec3d& pos, const Vec3f& rotation, bool use_xyplane_speed_rel_ground_override, float xyplane_speed_rel_ground_override,
		const Matrix4f& pre_ob_to_world_matrix, uint32 anim_state, double cur_time, double dt, const PoseConstraint& pose_constraint, AnimEvents& anim_events_out);

	void build();
	//void create(OpenGLEngine& engine, const std::string& URL);

	void destroy(OpenGLEngine& engine);
	
	void setSelectedObBeam(OpenGLEngine& engine, const Vec3d& target_pos); // create or update beam
	void hideSelectedObBeam(OpenGLEngine& engine);

	// These are just measured with mk.1 eyeball and a stopwatch.
	static float walkCyclePeriod() { return 1.015f; }
	static float runCyclePeriod() { return  0.7f; }

	Vec4f getLastHeadPosition() const;
	Vec4f getLastLeftEyePosition() const;
	Vec4f getLastRightEyePosition() const;

	void performGesture(double cur_time, const std::string& gesture_name, bool animate_head, bool loop_anim, AnimationManager& animation_manager, ResourceManager& resource_manager);
	void stopGesture(double cur_time/*, const std::string& gesture_name*/);

	void setPendingGesture(const std::string& gesture_name, bool animate_head, bool loop_anim); // Set if we wanted this avatar to perform the gesture but the animation file wasn't present.  To play when it's downloaded.
	void performPendingGesture(double cur_time, AnimationManager& animation_manager, ResourceManager& resource_manager);

	const Vec3d& getLastVel() const { return last_vel; }

	// Build .subanim processed animation files from animation GLBs.
	static void processAnimationData();

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
	int left_up_leg_node_i;
	int right_up_leg_node_i;

	int left_arm_node_i;
	int right_arm_node_i;

	int left_forearm_node_i;
	int right_forearm_node_i;

	int left_shoulder_node_i;
	int right_shoulder_node_i;

	int left_hand_node_i;
	int right_hand_node_i;

	int hips_node_i;

	int spine2_node_i;



	int LeftHandThumb1_i;
	int LeftHandThumb2_i;
	int LeftHandThumb3_i;
	int LeftHandThumb4_i;
	int LeftHandIndex1_i;
	int LeftHandIndex2_i;
	int LeftHandIndex3_i;
	int LeftHandIndex4_i;
	int LeftHandMiddle1_i;
	int LeftHandMiddle2_i;
	int LeftHandMiddle3_i;
	int LeftHandMiddle4_i;
	int LeftHandRing1_i;
	int LeftHandRing2_i;
	int LeftHandRing3_i;
	int LeftHandRing4_i;
	int LeftHandPinky1_i;
	int LeftHandPinky2_i;
	int LeftHandPinky3_i;
	int LeftHandPinky4_i;

	int RightHandThumb1_i;
	int RightHandThumb2_i;
	int RightHandThumb3_i;
	int RightHandThumb4_i;
	int RightHandIndex1_i;
	int RightHandIndex2_i;
	int RightHandIndex3_i;
	int RightHandIndex4_i;
	int RightHandMiddle1_i;
	int RightHandMiddle2_i;
	int RightHandMiddle3_i;
	int RightHandMiddle4_i;
	int RightHandRing1_i;
	int RightHandRing2_i;
	int RightHandRing3_i;
	int RightHandRing4_i;
	int RightHandPinky1_i;
	int RightHandPinky2_i;
	int RightHandPinky3_i;
	int RightHandPinky4_i;

	float upper_right_arm_len, lower_right_arm_len;
	float upper_left_arm_len, lower_left_arm_len;

public:
	std::string pending_gesture_name; // Set if we wanted this avatar to perform the gesture but the animation file wasn't present.  To play when it's downloaded.
	bool pending_gesture_animate_head;
	bool pending_gesture_loop_anim;
};


typedef Reference<AvatarGraphics> AvatarGraphicsRef;
