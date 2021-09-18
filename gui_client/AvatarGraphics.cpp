/*=====================================================================
AvatarGraphics.cpp
------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "AvatarGraphics.h"


#include "opengl/OpenGLEngine.h"
#include "../dll/include/IndigoMesh.h"
#include <utils/ConPrint.h>
#include <utils/StringUtils.h>


AvatarGraphics::AvatarGraphics()
:	loaded_lod_level(-1)
{
	last_pos.set(0, 0, 0);
	last_selected_ob_target_pos.set(0, 0, 0);
	last_cam_rotation.set(0,0,0);
	avatar_rotation.set(0,0,0);
	cur_sideweays_lean = 0;
	cur_forwards_lean = 0;
	last_vel = Vec3d(0.0);

	gesture_neck_quat = Quatf::identity();
	gesture_head_quat = Quatf::identity();

	cur_eye_target_os = Vec4f(0,0,1,1);
	next_eye_target_os = Vec4f(0,0,1,1);
	eye_start_transition_time = -2;
	eye_end_transition_time = -1;
	saccade_gap = 0.5;

	cur_head_rot_z = 0;
	//cur_head_rot_quat = Quatf::identity();

	turn_anim_end_time = -1;
	turning = false;
}


AvatarGraphics::~AvatarGraphics()
{

}


static const int NUM_FRAMES = 52;


// rotation is (roll, pitch, heading)
static const Matrix4f rotationMatrix(const Vec3f& rotation)
{
	// Just rotate avatar according to heading, for pitch we will move head up/down.
	return Matrix4f::rotationAroundZAxis(rotation.z);
}


static const Matrix4f rotateThenTranslateMatrix(const Vec3d& translation, const Vec3f& rotation)
{
	Matrix4f m = Matrix4f::rotationAroundZAxis(rotation.z);
	m.setColumn(3, Vec4f((float)translation.x, (float)translation.y, (float)translation.z, 1.f));
	return m;
}


void AvatarGraphics::setOverallTransform(OpenGLEngine& engine, const Vec3d& pos, const Vec3f& cam_rotation, const Matrix4f& pre_ob_to_world_matrix, uint32 anim_state, double cur_time, double dt, AnimEvents& anim_events_out)
{
	if(false) // debug_avatar_basis_ob.isNull())
	{
		debug_avatar_basis_ob = new GLObject();
		debug_avatar_basis_ob->mesh_data = engine.make3DBasisArrowMesh(); // Base will be at origin, tip will lie at (1, 0, 0)
		debug_avatar_basis_ob->materials.resize(3);
		debug_avatar_basis_ob->materials[0].albedo_rgb = Colour3f(0.9f, 0.5f, 0.3f);
		debug_avatar_basis_ob->materials[1].albedo_rgb = Colour3f(0.5f, 0.9f, 0.5f);
		debug_avatar_basis_ob->materials[2].albedo_rgb = Colour3f(0.3f, 0.5f, 0.9f);
		debug_avatar_basis_ob->ob_to_world_matrix = Matrix4f::translationMatrix(1000, 0, 0);
		engine.addObject(debug_avatar_basis_ob);
	}


	if(skinned_gl_ob.nonNull())
	{
		if(cur_time >= skinned_gl_ob->transition_end_time && skinned_gl_ob->next_anim_i != -1)
		{
			skinned_gl_ob->current_anim_i = skinned_gl_ob->next_anim_i;
			skinned_gl_ob->next_anim_i = -1;
			//conPrint("Finished transitioning to anim " + skinned_gl_ob->mesh_data->animation_data.animations[skinned_gl_ob->current_anim_i]->name);
		}

		int new_anim_i = 0;

		const Vec4f forwards_vec = rotationMatrix(avatar_rotation) * Vec4f(1,0,0,0);
		const Vec4f right_vec    = rotationMatrix(avatar_rotation) * Vec4f(0,1,0,0);
		//const Vec4f up_vec       = rotationMatrix(avatar_rotation) * Vec4f(0,0,1,0);

		const Vec3d dpos = pos - last_pos;
		const Vec3d vel = dpos / dt;
		const double speed = vel.length();
		
		const Vec3d old_vel = last_vel;
		const Vec3d accel = (vel - old_vel) / dt;
		const float unclamped_sideways_accel = dot(right_vec,    accel.toVec4fVector());
		const float unclamped_forwards_accel = dot(forwards_vec, accel.toVec4fVector());
		
		const Vec3f drot = cam_rotation - last_cam_rotation;
		//const Vec3f rot_vel = drot / (float)dt;

		if(drot.length() > 0.01)
			last_cam_rotation_time = cur_time;

		Matrix4f lean_matrix = Matrix4f::identity();

		// float turn_forwards_nudge = 0;
		double new_anim_transition_duration = 0.3; // Duration of the blend period, if we have a new animation to transiton to.
		
		if((anim_state & ANIM_STATE_IN_AIR) == 0) // if on ground:
		{
			const float max_accel_mag = 10.2f;
			float clamped_sideways_accel = myClamp(unclamped_sideways_accel, -max_accel_mag, max_accel_mag);
			float clamped_forwards_accel = myClamp(unclamped_forwards_accel, -max_accel_mag, max_accel_mag);
			const float blend_frac = 0.03f;
			cur_sideweays_lean = cur_sideweays_lean * (1 - blend_frac) + clamped_sideways_accel * blend_frac;
			cur_forwards_lean  = cur_forwards_lean  * (1 - blend_frac) + clamped_forwards_accel * blend_frac;

			//const float forwards_vel = dot(forwards_vec, vel.toVec4fVector());
			const bool moving_forwards = dot(forwards_vec, normalise(dpos.toVec4fVector())) > -0.1f;

			//if(speed > 0.1 && (forwards_vel < -0.1f || forwards_vel > 0.1f))
				lean_matrix = Matrix4f::rotationAroundXAxis(cur_sideweays_lean * -0.02f) * Matrix4f::rotationAroundYAxis(cur_forwards_lean * -0.02f);

			if(speed > 6)
			{
				// Blend rotation of avatar towards camera rotation
				const float rot_blend_frac = (float)(10 * dt);
				this->avatar_rotation = this->avatar_rotation * (1 - rot_blend_frac) + cam_rotation * rot_blend_frac;

				if(moving_forwards)
					new_anim_i = running_anim_i;
				else
					new_anim_i = running_backwards_anim_i;

				new_anim_transition_duration = 0.1;
			}
			else if(speed > 0.1)
			{
				// Blend rotation of avatar towards camera rotation
				const float rot_blend_frac = (float)(10 * dt);
				this->avatar_rotation = this->avatar_rotation * (1 - rot_blend_frac) + cam_rotation * rot_blend_frac;

				if(moving_forwards)
					new_anim_i = walking_anim_i;
				else
					new_anim_i = walking_backwards_anim_i;

				new_anim_transition_duration = 0.2;
			}
			else // else if (nearly) stationary:
			{
				const double turn_anim_duration = 57.0 / 60;// Left and right turn anims are 57 frames at 60fps

				if(cur_time < turn_anim_end_time) // If we are currently performing a turn left/right anim:
				{
					// Slowly rotate in the turn direction.
					const float turn_sign = turning_left ? 1.f : -1.f;

					if(turning_left)
						new_anim_i = turn_left_anim_i;
					else
						new_anim_i = turn_right_anim_i;

					{
						const double turn_anim_start_time = turn_anim_end_time - turn_anim_duration;
						const float frac = (float)((cur_time - turn_anim_start_time) / turn_anim_duration);
						this->avatar_rotation = avatar_rotation_at_turn_start + Vec3f(0, 0, turn_sign * ::degreeToRad(118.f) * frac);

						const double time_remaining = turn_anim_end_time - cur_time;
						if(time_remaining < 0.3)
						{
							// Start blending into idle pose anim
							new_anim_i = idle_anim_i;
						}
					}
				}
				else
				{
					if(turning && (skinned_gl_ob->current_anim_i == turn_left_anim_i || skinned_gl_ob->current_anim_i == turn_right_anim_i)) // If we were performing a turn, and we have finished the animation:
					{
						//conPrint("Finished the turn anim");
						turning = false;
					}


					if(cur_time < gesture_anim.play_end_time) // if we are playing a gesture:
					{
						new_anim_i = gesture_anim.anim_i; // Continue current gesture


						const double time_remaining = gesture_anim.play_end_time - cur_time;
						//printVar(time_remaining);
						if(time_remaining < 0.3)
						{
							// conPrint("Starting blend to idle pose anim");
							// Start blending into idle pose anim
							new_anim_i = idle_anim_i;
						}

					}
					else // Else if we have finished playing any gesture:
					{
						//if(next_gesture_anim.anim_i >= 0) // If we have a next gesture:
						//{
						//	gesture_anim = next_gesture_anim;
						//	next_gesture_anim.anim_i = -1;
						//	//gesture_anim_i = next_gesture_anim_i;
						//	//next_gesture_anim_i = -1;
						//	new_anim_i = gesture_anim.anim_i;
						//}
						//else
							new_anim_i = idle_anim_i;
					}

					const float yaw_diff = cam_rotation.z - avatar_rotation.z;

					// conPrint("cam_rotation.z: " + doubleToStringNSigFigs(cam_rotation.z, 3) + ", avatar_rotation.z: " + doubleToStringNSigFigs(avatar_rotation.z, 3));

					if(std::fabs(yaw_diff) > ::degreeToRad(118.f))
					{
						avatar_rotation_at_turn_start = avatar_rotation;

						if(yaw_diff > 0)
						{
							new_anim_i = turn_left_anim_i;
							// conPrint("Starting left turn anim");
							turning_left = true;
						}
						else
						{
							new_anim_i = turn_right_anim_i;
							// conPrint("Starting right turn anim");
							turning_left = false;
						}

						turn_anim_end_time = cur_time + turn_anim_duration;
						skinned_gl_ob->use_time_offset = -cur_time; // Set anim time offset so that we are at the beginning of the animation. NOTE: bit of a hack, messes with the blended anim also.
						turning = true;

						new_anim_transition_duration = 0.3;
					}
				}
			}
		}
		else
		{
			this->avatar_rotation = cam_rotation;

			const bool flying = (anim_state & ANIM_STATE_FLYING) != 0;
			const bool moving_forwards = dot(vel.toVec4fVector(), forwards_vec) > speed * 0.4f;

			const float max_accel_mag = 40.f;
			float clamped_sideways_accel = myClamp(unclamped_sideways_accel, -max_accel_mag, max_accel_mag);
			float clamped_forwards_accel = myClamp(unclamped_forwards_accel, -max_accel_mag, max_accel_mag);
			const float blend_frac = 0.03f;

			if(!flying) // If jumping:
			{
				clamped_forwards_accel = 0;
				new_anim_transition_duration = 0.15; // Make the transition to jumping faster than the default.
			}

			cur_sideweays_lean = cur_sideweays_lean * (1 - blend_frac) + clamped_sideways_accel * blend_frac;
			cur_forwards_lean  = cur_forwards_lean  * (1 - blend_frac) + clamped_forwards_accel * blend_frac;


			// flying
			if(speed > 10 && moving_forwards && flying)
				new_anim_i = flying_anim_i;
			else
				new_anim_i = floating_anim_i;

			lean_matrix = Matrix4f::rotationAroundXAxis(cur_sideweays_lean * -0.02f) * Matrix4f::rotationAroundYAxis(cur_forwards_lean * -0.02f);
		}


		// pre_ob_to_world_matrix will rotate avatars from y-up and z-forwards to z-up and -y forwards.  Rotate around z axis to change to +x-forwards
		skinned_gl_ob->ob_to_world_matrix = /*Matrix4f::translationMatrix(forwards_vec * turn_forwards_nudge) * */rotateThenTranslateMatrix(pos, avatar_rotation) * lean_matrix * 
			Matrix4f::rotationAroundZAxis(Maths::pi_2<float>()) * pre_ob_to_world_matrix;
		engine.updateObjectTransformData(*skinned_gl_ob);

		// See if we need to start a transition to a new animation
		if(new_anim_i != skinned_gl_ob->current_anim_i)
		{
			// If we are currently transitioning, don't change next
			if(cur_time >= skinned_gl_ob->transition_start_time && cur_time < skinned_gl_ob->transition_end_time)
			{
			}
			else
			{
				//conPrint("Started transitioning to anim " + skinned_gl_ob->mesh_data->animation_data.animations[new_anim_i]->name + " transition duration: " + doubleToStringNSigFigs(new_anim_transition_duration, 3));

				skinned_gl_ob->next_anim_i = new_anim_i;

				skinned_gl_ob->transition_start_time = cur_time;
				skinned_gl_ob->transition_end_time = cur_time + new_anim_transition_duration;
			}
		}

		last_vel = vel;


		// Set transform of debug basis arrows.
		// debug_avatar_basis_ob->ob_to_world_matrix = skinned_gl_ob->ob_to_world_matrix;
		// engine.updateObjectTransformData(*debug_avatar_basis_ob);


		const Vec4f up_os(0,1,0,0);

		
		// Check node indices are in-bounds
		if(	head_node_i >= 0 && head_node_i < (int)skinned_gl_ob->anim_node_data.size() &&
			neck_node_i >= 0 && neck_node_i < (int)skinned_gl_ob->anim_node_data.size() &&
			left_eye_node_i >= 0 && left_eye_node_i < (int)skinned_gl_ob->anim_node_data.size() &&
			right_eye_node_i >= 0 && right_eye_node_i < (int)skinned_gl_ob->anim_node_data.size())
		{
			const bool is_VRM_model = skinned_gl_ob->mesh_data->animation_data.vrm_data.nonNull(); // VRM models tend to have anime eyes which are bigger and can move less without loooking weird.
			// TODO: use the metadata limits from the VRM file.

			const float MAX_EYE_YAW_MAG  = is_VRM_model ? 0.25f : 0.4f; // relative to head
			float MAX_EYE_PITCH_MAG_UP   = is_VRM_model ? 0.1f : 0.15f; // relative to head
			float MAX_EYE_PITCH_MAG_DOWN = is_VRM_model ? 0.15f : 0.4f; // relative to head

			const float MAX_HEAD_YAW_MAG = 0.8f;


			// Get total yaw and pitch differences between cam rotation and avatar rotation
			const float total_yaw_amount   = cam_rotation.z - avatar_rotation.z;
			const float total_pitch_amount = cam_rotation.y - Maths::pi_2<float>();

			//------------------- Do head look-at procedural movement -----------------------
			// Get clamped desired/target head yaw.
			const float target_head_yaw_amount = myClamp(total_yaw_amount, -MAX_HEAD_YAW_MAG, MAX_HEAD_YAW_MAG);

			const float target_head_rot_z = avatar_rotation.z + target_head_yaw_amount; // Get target head rotation given the clamped target yaw

			// Blend current head rot z towards target_head_rot_z
			const float head_rot_frac = myMin(0.2f, (float)(10 * dt));
			this->cur_head_rot_z = this->cur_head_rot_z * (1 - head_rot_frac) + target_head_rot_z * head_rot_frac;


			// Update gesture_neck_quat, gesture_head_quat if we are doing a gesture, otherwise leave the last values as is.
			const bool doing_gesture_with_animated_head = (cur_time < gesture_anim.play_end_time) && gesture_anim.animated_head;
			if(doing_gesture_with_animated_head)
			{
				gesture_neck_quat = skinned_gl_ob->anim_node_data[neck_node_i].last_rot;
				gesture_head_quat = skinned_gl_ob->anim_node_data[head_node_i].last_rot;
			}

				
			//const Quatf target_head_rot_quat = Quatf::fromAxisAndAngle(Vec3f(0, 1, 0), target_head_rot_z);
			//this->cur_head_rot_quat = Quatf::nlerp(this->cur_head_rot_quat, target_head_rot_quat, 1 - head_rot_frac);


			const float unclamped_head_yaw_amount = this->cur_head_rot_z - avatar_rotation.z; // Get actual blended yaw of the head.
			const float head_yaw_amount =  myClamp(unclamped_head_yaw_amount, -MAX_HEAD_YAW_MAG, MAX_HEAD_YAW_MAG);

			const float MAX_HEAD_PITCH_MAG = 0.8f;
			const float head_pitch_amount = myClamp(total_pitch_amount, -MAX_HEAD_PITCH_MAG, MAX_HEAD_PITCH_MAG);

			// We will interpolate between the gesture head rotation (if the head is being animated), and the procedural lookat rotation.
			const float transition_frac = (float)Maths::smoothStep<double>(skinned_gl_ob->transition_start_time, skinned_gl_ob->transition_end_time, cur_time);
			float lookat_frac;
			if(doing_gesture_with_animated_head)
			{
				if(new_anim_i == gesture_anim.anim_i) // If we are transitioning *to* the gesture:
					lookat_frac = 1 - transition_frac;
				else
					lookat_frac = transition_frac; // Else we are transitioning out of the gesture.
			}
			else
				lookat_frac = 1;


			/*
			Lets say we have the head to model transform T:
			We want to set T to some target value (T_target) and solve for a procedural head transform that will give T_target.

			T = hip_T * neck_T * head_T = body_T * head_T

			T_target = body_T * head_T * proc_head

			(body_T * head_T)^-1 T_target = proc_head
			*/
			Matrix4f inverse_last_pre_proc_head_to_world;
			skinned_gl_ob->anim_node_data[head_node_i].last_pre_proc_to_object.getInverseForAffine3Matrix(inverse_last_pre_proc_head_to_world);
			inverse_last_pre_proc_head_to_world.setColumn(3, Vec4f(0,0,0,1));

			// Get rotation quatf from this matrix:
			const Quatf inverse_last_pre_proc_head_to_world_rot = Quatf::fromMatrix(inverse_last_pre_proc_head_to_world);

			const Quatf last_head_rot = skinned_gl_ob->anim_node_data[head_node_i].last_rot;

			const Quatf head_gesture_rot = (last_head_rot.conjugate() * gesture_head_quat); // Procedural rotation that leaves the head in the last rotation (relative to neck) from the gesture

			const Quatf head_lookat_rot = inverse_last_pre_proc_head_to_world_rot * Quatf::fromAxisAndAngle(Vec3f(0,1,0), head_yaw_amount) * Quatf::fromAxisAndAngle(Vec3f(1,0,0), head_pitch_amount);

			const Quatf head_rot = Quatf::nlerp(head_gesture_rot, head_lookat_rot, lookat_frac);

			skinned_gl_ob->anim_node_data[head_node_i].procedural_transform = head_rot.toMatrix(); 

				

			const float NECK_FACTOR = 0.5f; // relative to amount of head rotation and translation
			const float pitch_move_forwards_factor = head_pitch_amount * 0.0f * NECK_FACTOR;
			const float neck_yaw_amount = head_yaw_amount * NECK_FACTOR;
			const float neck_pitch_amount = 0.3f + head_pitch_amount * NECK_FACTOR; // Idle pose neck rotation is 0.5.  A value of 0 gives a very erect posture.  Use 0.3 as a compromise.
			// Note that ideally we would compute the neck pitch as some fraction betweeen 0.3 and head pitch amount.
				
			//Matrix4f neck_rot = Matrix4f::translationMatrix(0, 0, pitch_move_forwards_factor /*- fabs(yaw_amount) * 0.04 * NECK_FACTOR*/) *
			//	Matrix4f::rotationAroundZAxis(-0.2f * head_yaw_amount * NECK_FACTOR) * Matrix4f::rotationAroundYAxis(head_yaw_amount * NECK_FACTOR) * Matrix4f::rotationAroundXAxis(head_pitch_amount * NECK_FACTOR);

			Matrix4f inverse_last_pre_proc_neck_to_world;
			skinned_gl_ob->anim_node_data[neck_node_i].last_pre_proc_to_object.getInverseForAffine3Matrix(inverse_last_pre_proc_neck_to_world);
			inverse_last_pre_proc_neck_to_world.setColumn(3, Vec4f(0,0,0,1));

			const Quatf inverse_last_pre_proc_neck_to_world_rot = Quatf::fromMatrix(inverse_last_pre_proc_neck_to_world);

			const Quatf last_neck_rot = skinned_gl_ob->anim_node_data[neck_node_i].last_rot;

			// Vec4f neck_rot_axis;
			// float neck_rot_angle;
			// last_neck_rot.toAxisAndAngle(neck_rot_axis, neck_rot_angle);
			// conPrint(neck_rot_axis.toStringNSigFigs(3));
			// printVar(neck_rot_angle);

			const Quatf neck_gesture_rot = (last_neck_rot.conjugate() * gesture_neck_quat); // Procedural rotation that leaves the head in the last rotation (relative to neck) from the gesture

			const Quatf neck_lookat_rot = inverse_last_pre_proc_neck_to_world_rot * Quatf::fromAxisAndAngle(Vec3f(0,1,0), neck_yaw_amount) * Quatf::fromAxisAndAngle(Vec3f(1,0,0), neck_pitch_amount);

			const Quatf neck_rot = Quatf::nlerp(neck_gesture_rot, neck_lookat_rot, lookat_frac * 0.5f); // Don't blend totally to lookat for neck rotation.  Sitting looks better this way.

			skinned_gl_ob->anim_node_data[neck_node_i].procedural_transform = neck_rot.toMatrix();


			// Compute 'camera pitch/yaw', and clamp it.
			// This is the pitch and yaw around which we will do eye saccades.
			const float MAX_CAM_YAW_MAG = MAX_HEAD_YAW_MAG + MAX_EYE_YAW_MAG;
			const float use_cam_yaw = myClamp(total_yaw_amount, -MAX_CAM_YAW_MAG, MAX_CAM_YAW_MAG);
			const float MAX_CAM_PITCH_MAG_UP   = MAX_HEAD_PITCH_MAG + MAX_EYE_PITCH_MAG_UP;
			const float MAX_CAM_PITCH_MAG_DOWN = MAX_HEAD_PITCH_MAG + MAX_EYE_PITCH_MAG_DOWN;
			const float use_cam_pitch = myClamp(total_pitch_amount, -MAX_CAM_PITCH_MAG_UP, MAX_CAM_PITCH_MAG_DOWN);

			const Matrix4f cam_rot = Matrix4f::rotationAroundYAxis(use_cam_yaw) * Matrix4f::rotationAroundXAxis(use_cam_pitch);
			const Vec4f cam_forwards_os = cam_rot * Vec4f(0,0,1,0);
			const Vec4f cam_right_os    = cam_rot * Vec4f(1,0,0,0);
			const Vec4f cam_up_os       = cam_rot * Vec4f(0,1,0,0);
			//conPrint("cam_forwards_os: " + cam_forwards_os.toStringNSigFigs(4));


			const Matrix4f last_head_to_object_space = skinned_gl_ob->anim_node_data[head_node_i].node_hierarchical_to_object; // last head bone to object space (y-up) transformation.
			Matrix4f head_object_to_bone_space;
			last_head_to_object_space.getInverseForAffine3Matrix(head_object_to_bone_space);
			const Vec4f last_head_pos_os = last_head_to_object_space * Vec4f(0,0,0,1);

			
			//const Vec4f last_head_forwards_os = last_head_to_object_space * Vec4f(0,0,1,0); // in object/model space
			const Vec4f last_head_right_os    = last_head_to_object_space * Vec4f(1,0,0,0); // in object/model space
			const Vec4f last_head_up_os       = last_head_to_object_space * Vec4f(0,1,0,0); // in object/model space

			//conPrint("last_head_forwards_os: " + last_head_forwards_os.toStringNSigFigs(4));

			bool eye_rot_valid = true; // Is eye still a valid rotation relative to the head, or is it rotated too far?
			{				
				const Vec4f cur_target_os = cur_eye_target_os;
				const Vec4f eye_pos_os = last_head_pos_os; // Approx eye pos
				const Vec4f to_cur_target_os = normalise(cur_target_os - eye_pos_os);

				// Get eye yaw relative to head:
				const float right_dot = dot(to_cur_target_os, last_head_right_os);
				const float up_dot    = dot(to_cur_target_os, last_head_up_os);

				const float cur_eye_pitch = -asin(up_dot);
				const float cur_eye_yaw   = -asin(right_dot);

				eye_rot_valid = 
					cur_eye_pitch > -MAX_EYE_PITCH_MAG_UP && cur_eye_pitch < MAX_EYE_PITCH_MAG_DOWN &&
					std::fabs(cur_eye_yaw) < MAX_EYE_YAW_MAG;
			}

			//------------------- Do eye saccades (eye darting movements) -----------------------
			{
				const Vec4f left_eye_pos_os = skinned_gl_ob->anim_node_data[left_eye_node_i].node_hierarchical_to_object * Vec4f(0,0,0,1);

				// Get rotation quat for looking at cur_eye_target_ws
				Quatf to_cur_rot;
				{
					const Vec4f to_cur_target_os = normalise(cur_eye_target_os - left_eye_pos_os);
					const Vec4f to_cur_target_bs = head_object_to_bone_space * to_cur_target_os;

					const Vec4f i = normalise(crossProduct(up_os, to_cur_target_bs));
					const Vec4f j = crossProduct(to_cur_target_bs, i);
					const Matrix4f cur_lookat_matrix(i, j, to_cur_target_bs, Vec4f(0,0,0,1));
					to_cur_rot = Quatf::fromMatrix(cur_lookat_matrix);
				}

				// Get rotation quat for looking at next_eye_target_ws
				Quatf to_next_rot;
				{
					const Vec4f to_next_target_os = normalise(next_eye_target_os - left_eye_pos_os);
					const Vec4f to_next_target_bs = head_object_to_bone_space * to_next_target_os;

					const Vec4f i = normalise(crossProduct(up_os, to_next_target_bs));
					const Vec4f j = crossProduct(to_next_target_bs, i);
					const Matrix4f next_lookat_matrix(i, j, to_next_target_bs, Vec4f(0,0,0,1));
					to_next_rot = Quatf::fromMatrix(next_lookat_matrix);
				}

				// Blend between the rotations based on the transition times.
				const float lerp_frac = (float)Maths::smoothStep(eye_start_transition_time, eye_end_transition_time, cur_time);
				const Quatf lerped_rot = Quatf::nlerp(to_cur_rot, to_next_rot, lerp_frac);

				skinned_gl_ob->anim_node_data[left_eye_node_i].procedural_transform = lerped_rot.toMatrix();
			}

			{
				const Vec4f right_eye_pos_os = skinned_gl_ob->anim_node_data[right_eye_node_i].node_hierarchical_to_object * Vec4f(0,0,0,1);

				Quatf to_cur_rot;
				{
					const Vec4f to_cur_target_os = normalise(cur_eye_target_os - right_eye_pos_os);
					const Vec4f to_cur_target_bs = head_object_to_bone_space * to_cur_target_os;

					const Vec4f i = normalise(crossProduct(up_os, to_cur_target_bs));
					const Vec4f j = crossProduct(to_cur_target_bs, i);
					const Matrix4f cur_lookat_matrix(i, j, to_cur_target_bs, Vec4f(0,0,0,1));
					to_cur_rot = Quatf::fromMatrix(cur_lookat_matrix);
				}

				Quatf to_next_rot;
				{
					const Vec4f to_next_target_os = normalise(next_eye_target_os - right_eye_pos_os);
					const Vec4f to_next_target_bs = head_object_to_bone_space * to_next_target_os;

					const Vec4f i = normalise(crossProduct(up_os, to_next_target_bs));
					const Vec4f j = crossProduct(to_next_target_bs, i);
					const Matrix4f next_lookat_matrix(i, j, to_next_target_bs, Vec4f(0,0,0,1));
					to_next_rot = Quatf::fromMatrix(next_lookat_matrix);
				}

				const float lerp_frac = (float)Maths::smoothStep(eye_start_transition_time, eye_end_transition_time, cur_time);
				const Quatf lerped_rot = Quatf::nlerp(to_cur_rot, to_next_rot, lerp_frac);

				skinned_gl_ob->anim_node_data[right_eye_node_i].procedural_transform = lerped_rot.toMatrix();//Matrix4f::rotationAroundYAxis((float)cur_time);
			}


			const double SACCADE_DURATION = 0.03; // 30ms, rough value from wikipedia
			if((cur_time > eye_end_transition_time + saccade_gap) || !eye_rot_valid)
			{
				eye_start_transition_time = cur_time;
				eye_end_transition_time = cur_time + SACCADE_DURATION;

				cur_eye_target_os = next_eye_target_os;

				// NOTE: could do this a few times with rejection sampling until we satisfy constraints.  But looks fine as it is.
				//const int max_iters = 10;
				//for(int i=0; i<max_iters; ++i)
				//{
					const float forwards_comp = 2;
					const float right_comp = 0.3f * (-0.5f + rng.unitRandom());
					const float up_comp    = 0.1f * (-0.5f + rng.unitRandom());

					next_eye_target_os = last_head_pos_os + cam_forwards_os * forwards_comp + cam_right_os * right_comp + cam_up_os * up_comp;

					// Check constraints
					//const float up_dot = dot(normalise(next_eye_target_os), last_head_up_os);
					//float cur_eye_pitch = -asin(up_dot);
					//printVar(cur_eye_pitch);

					//const bool valid = cur_eye_pitch > -MAX_EYE_PITCH_MAG_UP && cur_eye_pitch < MAX_EYE_PITCH_MAG_DOWN;
					//printVar(valid);
					//if(valid)
					//	break;
				//}

				saccade_gap = 0.4 + rng.unitRandom() * rng.unitRandom() * rng.unitRandom() * 3; // Compute a random time until the next saccade
			}

			// If the camera has moved recently, make the eyes point in the camera direction as much as possible (subject to constraints)
			const double time_since_last_cam_rotation = cur_time - last_cam_rotation_time;
			if(time_since_last_cam_rotation < 0.5)
			{
				const float eye_yaw_amount_unclamped = total_yaw_amount - head_yaw_amount; // Desired eye yaw is yaw remaining after head yaw.
				const float eye_yaw_amount = myClamp(eye_yaw_amount_unclamped, -MAX_EYE_YAW_MAG, MAX_EYE_YAW_MAG);

				const float eye_pitch_amount_unclamped = total_pitch_amount - head_pitch_amount;
				const float eye_pitch_amount = myClamp(eye_pitch_amount_unclamped, -MAX_EYE_PITCH_MAG_UP, MAX_EYE_PITCH_MAG_DOWN);

				skinned_gl_ob->anim_node_data[left_eye_node_i ].procedural_transform = Matrix4f::rotationAroundYAxis(eye_yaw_amount) * Matrix4f::rotationAroundXAxis(eye_pitch_amount);
				skinned_gl_ob->anim_node_data[right_eye_node_i].procedural_transform = Matrix4f::rotationAroundYAxis(eye_yaw_amount) * Matrix4f::rotationAroundXAxis(eye_pitch_amount);

				eye_end_transition_time = cur_time; // Don't start doing saccades for a little while
			}
			

			if(doing_gesture_with_animated_head)
			{
				skinned_gl_ob->anim_node_data[left_eye_node_i ].procedural_transform = Matrix4f::identity();
				skinned_gl_ob->anim_node_data[right_eye_node_i].procedural_transform = Matrix4f::identity();
			}
		}
	}
	else
	{
		//this->last_hand_pos = toVec3d(this->lower_arms[0].gl_ob->ob_to_world_matrix * Vec4f(0, 0, 1, 1)); // (0,0,1) in forearm cylinder object space is where the hand is.
	}

	
	last_pos = pos;
	last_cam_rotation = cam_rotation;
}


//void AvatarGraphics::create(OpenGLEngine& engine, const std::string& URL)
//{
//}


static int findAnimation(GLObject& ob, const std::string& name)
{
	const int anim_i = ob.mesh_data->animation_data.getAnimationIndex(name);
	if(anim_i < 0)
	{
		conPrint("Failed to find animation '" + name + "'");
		assert(0);
		return 0;
	}
	else
		return anim_i;
}


void AvatarGraphics::build()
{
	idle_anim_i = findAnimation(*skinned_gl_ob, "Idle");
	walking_anim_i = findAnimation(*skinned_gl_ob, "Walking");
	walking_backwards_anim_i = findAnimation(*skinned_gl_ob, "Walking Backward");
	running_anim_i = findAnimation(*skinned_gl_ob, "Running");
	running_backwards_anim_i = findAnimation(*skinned_gl_ob, "Running Backward");
	floating_anim_i = findAnimation(*skinned_gl_ob, "Floating");
	flying_anim_i = findAnimation(*skinned_gl_ob, "Flying");
	turn_left_anim_i  = findAnimation(*skinned_gl_ob, "Left Turn");
	turn_right_anim_i = findAnimation(*skinned_gl_ob, "Right Turn");

	// root_node_i = skinned_gl_ob->mesh_data->animation_data.getNodeIndex("player_rig");
	neck_node_i  = skinned_gl_ob->mesh_data->animation_data.getNodeIndex("Neck");
	head_node_i  = skinned_gl_ob->mesh_data->animation_data.getNodeIndex("Head");
	left_eye_node_i  = skinned_gl_ob->mesh_data->animation_data.getNodeIndex("LeftEye");
	right_eye_node_i = skinned_gl_ob->mesh_data->animation_data.getNodeIndex("RightEye");
	// left_foot_node_i = skinned_gl_ob->mesh_data->animation_data.getNodeIndex("LeftFoot");
}


void AvatarGraphics::destroy(OpenGLEngine& engine)
{
	if(skinned_gl_ob.nonNull())
		engine.removeObject(skinned_gl_ob);
	skinned_gl_ob = NULL;


	if(selected_ob_beam.nonNull())
		engine.removeObject(selected_ob_beam);
}


void AvatarGraphics::setSelectedObBeam(OpenGLEngine& engine, const Vec3d& target_pos) // create or update beam
{
//	const Vec3d src_pos = last_hand_pos;
//	this->last_selected_ob_target_pos = target_pos;
//
//	Matrix4f dir_matrix; dir_matrix.constructFromVector(normalise((target_pos - src_pos).toVec4fVector()));
//	Matrix4f scale_matrix = Matrix4f::scaleMatrix(/*radius=*/0.03f,/*radius=*/0.03f, (float)target_pos.getDist(src_pos));
//	Matrix4f ob_to_world = Matrix4f::translationMatrix(src_pos.toVec4fPoint()) * dir_matrix * scale_matrix;
//
//	if(selected_ob_beam.isNull())
//	{
//		selected_ob_beam = new GLObject();
//		selected_ob_beam->ob_to_world_matrix = ob_to_world;
//		selected_ob_beam->mesh_data = engine.getCylinderMesh();
//
//		OpenGLMaterial material;
//		material.albedo_rgb = Colour3f(0.5f, 0.2f, 0.2f);
//		material.transparent = true;
//		material.alpha = 0.2f;
//
//		selected_ob_beam->materials = std::vector<OpenGLMaterial>(1, material);
//		engine.addObject(selected_ob_beam);
//	}
//	else // Else if ob has already been created:
//	{
//		selected_ob_beam->ob_to_world_matrix = ob_to_world;
//		engine.updateObjectTransformData(*selected_ob_beam);
//	}
}


void AvatarGraphics::hideSelectedObBeam(OpenGLEngine& engine)
{
	if(selected_ob_beam.nonNull())
	{
		engine.removeObject(selected_ob_beam);
		selected_ob_beam = NULL;
	}
}


Vec4f AvatarGraphics::getLastHeadPosition()
{
	if(skinned_gl_ob.nonNull())
	{
		if(head_node_i >= 0 && head_node_i < (int)skinned_gl_ob->anim_node_data.size())
			return skinned_gl_ob->ob_to_world_matrix * skinned_gl_ob->anim_node_data[head_node_i].node_hierarchical_to_object * Vec4f(0,0,0,1);
		else
			return skinned_gl_ob->ob_to_world_matrix * Vec4f(0,0,0,1);
	}
	else
		return Vec4f(0,0,0,1);
}


static float getAnimLength(const AnimationData& animation_data, int anim_i)
{
	if(anim_i >= 0)
	{
		const AnimationDatum* anim = animation_data.animations[anim_i].ptr();
		return animation_data.getAnimationLength(*anim);
	}
	else
	{
		assert(0);
		return 1;
	}
}


void AvatarGraphics::performGesture(double cur_time, const std::string& gesture_name, bool animate_head, bool loop_anim)
{
	// conPrint("AvatarGraphics::performGesture: " + gesture_name + ", animate_head: " + boolToString(animate_head));

	if(skinned_gl_ob.nonNull())
	{
		skinned_gl_ob->transition_start_time = cur_time;
		skinned_gl_ob->transition_end_time = cur_time + 0.3;
		skinned_gl_ob->use_time_offset = -cur_time; // Set anim time offset so that we are at the beginning of the animation. NOTE: bit of a hack, messes with the blended anim also.

		if(false) // gesture_name == "Sit")
		{
			// Start with "Standing To Sitting" anim
			this->gesture_anim.anim_i = skinned_gl_ob->mesh_data->animation_data.getAnimationIndex("Standing To Sitting");
			this->gesture_anim.animated_head = true;
			this->gesture_anim.play_end_time = cur_time + getAnimLength(skinned_gl_ob->mesh_data->animation_data, this->gesture_anim.anim_i);


			this->next_gesture_anim.anim_i = skinned_gl_ob->mesh_data->animation_data.getAnimationIndex("Sit");
			this->next_gesture_anim.animated_head = animate_head;
			this->next_gesture_anim.play_end_time = loop_anim ? 1.0e10 : (cur_time + getAnimLength(skinned_gl_ob->mesh_data->animation_data, this->next_gesture_anim.anim_i));
		}
		else
		{
			this->gesture_anim.anim_i = skinned_gl_ob->mesh_data->animation_data.getAnimationIndex(gesture_name);
			this->gesture_anim.animated_head = animate_head;
			this->gesture_anim.play_end_time = loop_anim ? 1.0e10 : (cur_time + getAnimLength(skinned_gl_ob->mesh_data->animation_data, this->gesture_anim.anim_i));
		}

		skinned_gl_ob->next_anim_i = this->gesture_anim.anim_i;
	}
}


void AvatarGraphics::stopGesture(double cur_time/*, const std::string& gesture_name*/)
{
	this->gesture_anim.play_end_time = cur_time + 0.3;

	if(skinned_gl_ob.nonNull())
	{
		skinned_gl_ob->transition_start_time = cur_time;
		skinned_gl_ob->transition_end_time = cur_time + 0.3;
		skinned_gl_ob->next_anim_i = idle_anim_i;
	}
}

