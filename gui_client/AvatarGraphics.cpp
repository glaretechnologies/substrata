/*=====================================================================
AvatarGraphics.cpp
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-12 12:24:54 +1300
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
	last_rotation.set(0,0,0);
	cur_sideweays_lean = 0;
	cur_forwards_lean = 0;
	last_vel = Vec3d(0.0);

	cur_eye_target_ws = Vec4f(0,0,0,1);
	next_eye_target_ws = Vec4f(0,0,0,1);
	eye_start_transition_time = -2;
	eye_end_transition_time = -1;
	saccade_gap = 0.5;
}


AvatarGraphics::~AvatarGraphics()
{

}


static const int NUM_FRAMES = 52;

static const Matrix4f rotationMatrix(const Vec3f& rotation)
{
	Matrix4f m;
	const float rot_len2 = rotation.length2();
	if(rot_len2 < 1.0e-20f)
		m.setToIdentity();
	else
	{
		const float rot_len = std::sqrt(rot_len2);
		m.setToRotationMatrix(rotation.toVec4fVector() / rot_len, rot_len);
	}
	return m;
}

static const Matrix4f rotateThenTranslateMatrix(const Vec3d& translation, const Vec3f& rotation)
{
	Matrix4f m;
	const float rot_len2 = rotation.length2();
	if(rot_len2 < 1.0e-20f)
		m.setToIdentity();
	else
	{
		const float rot_len = std::sqrt(rot_len2);
		m.setToRotationMatrix(rotation.toVec4fVector() / rot_len, rot_len);
	}
	m.setColumn(3, Vec4f((float)translation.x, (float)translation.y, (float)translation.z, 1.f));
	return m;
}



void AvatarGraphics::setOverallTransform(OpenGLEngine& engine, const Vec3d& pos, const Vec3f& rotation, const Matrix4f& pre_ob_to_world_matrix, uint32 anim_state, double cur_time, double dt, AnimEvents& anim_events_out)
{
	if(skinned_gl_ob.nonNull())
	{
		if(cur_time >= skinned_gl_ob->transition_end_time && skinned_gl_ob->next_anim_i != -1)
		{
			skinned_gl_ob->current_anim_i = skinned_gl_ob->next_anim_i;
			skinned_gl_ob->next_anim_i = -1;
			//conPrint("Finished transitioning to anim " + skinned_gl_ob->mesh_data->animation_data.animations[skinned_gl_ob->current_anim_i]->name);
		}


		const Vec4f forwards_vec = rotationMatrix(rotation) * Vec4f(1,0,0,0);
		const Vec4f right_vec    = rotationMatrix(rotation) * Vec4f(0,1,0,0);
		const Vec4f up_vec       = rotationMatrix(rotation) * Vec4f(0,0,1,0);
		//conPrint("forwards_vec: " + forwards_vec.toString());

		
		int idle_anim_i = skinned_gl_ob->mesh_data->animation_data.findAnimation("Idle");
		if(idle_anim_i < 0) idle_anim_i = 0;

		int walking_anim_i = skinned_gl_ob->mesh_data->animation_data.findAnimation("Walking");
		if(walking_anim_i < 0) walking_anim_i = 0;

		int walking_backwards_anim_i = myMax(0, skinned_gl_ob->mesh_data->animation_data.findAnimation("Walking Backward"));

		int running_anim_i = skinned_gl_ob->mesh_data->animation_data.findAnimation("Running");
		if(running_anim_i < 0) running_anim_i = 0;

		int running_backwards_anim_i = myMax(0, skinned_gl_ob->mesh_data->animation_data.findAnimation("Running Backward"));

		int floating_anim_i = myMax(0, skinned_gl_ob->mesh_data->animation_data.findAnimation("Floating"));
		int flying_anim_i = myMax(0, skinned_gl_ob->mesh_data->animation_data.findAnimation("Flying"));
	
		int turn_left_anim_i  = myMax(0, skinned_gl_ob->mesh_data->animation_data.findAnimation("Left Turn"));
		int turn_right_anim_i = myMax(0, skinned_gl_ob->mesh_data->animation_data.findAnimation("Right Turn"));


		const Vec3d dpos = pos - last_pos;
		const Vec3d vel = dpos / dt;
		const double speed = vel.length();
		
		const Vec3d old_vel = last_vel;
		const Vec3d accel = (vel - old_vel) / dt;//(midpoint - last_pos) / dt;
		const float unclamped_sideways_accel = dot(right_vec,    accel.toVec4fVector());
		const float unclamped_forwards_accel = dot(forwards_vec, accel.toVec4fVector());
		
		const Vec3f drot = rotation - last_rotation;
		const Vec3f rot_vel = drot / (float)dt;

		Matrix4f lean_matrix = Matrix4f::identity();
		int new_anim_i = 0;
		if((anim_state & ANIM_STATE_IN_AIR) == 0) // if on ground:
		{
			const float max_accel_mag = 10.2f;
			float clamped_sideways_accel = myClamp(unclamped_sideways_accel, -max_accel_mag, max_accel_mag);
			float clamped_forwards_accel = myClamp(unclamped_forwards_accel, -max_accel_mag, max_accel_mag);
			const float blend_frac = 0.03f;
			cur_sideweays_lean = cur_sideweays_lean * (1 - blend_frac) + clamped_sideways_accel * blend_frac;
			cur_forwards_lean  = cur_forwards_lean  * (1 - blend_frac) + clamped_forwards_accel * blend_frac;

			const float forwards_vel = dot(forwards_vec, vel.toVec4fVector());
			const bool moving_forwards = dot(forwards_vec, normalise(dpos.toVec4fVector())) > -0.1f;

			//if(speed > 0.1 && (forwards_vel < -0.1f || forwards_vel > 0.1f))
				lean_matrix = Matrix4f::rotationAroundXAxis(cur_sideweays_lean * -0.02f) * Matrix4f::rotationAroundYAxis(cur_forwards_lean * -0.02f);

			if(speed > 6)
			{
				if(moving_forwards)
					new_anim_i = running_anim_i;
				else
					new_anim_i = running_backwards_anim_i;
			}
			else if(speed > 0.1)
			{
				if(moving_forwards)
					new_anim_i = walking_anim_i;
				else
					new_anim_i = walking_backwards_anim_i;
			}
			else // else if (nearly) stationary:
			{
				new_anim_i = idle_anim_i;

				if(rot_vel.z > 0.5f)
					new_anim_i = turn_left_anim_i;
				else if(rot_vel.z < -0.5f)
					new_anim_i = turn_right_anim_i;
			}
		}
		else
		{
			const bool flying = (anim_state & ANIM_STATE_FLYING) != 0;
			const bool moving_forwards = dot(vel.toVec4fVector(), forwards_vec) > speed * 0.4f;

			const float max_accel_mag = 40.f;
			float clamped_sideways_accel = myClamp(unclamped_sideways_accel, -max_accel_mag, max_accel_mag);
			float clamped_forwards_accel = myClamp(unclamped_forwards_accel, -max_accel_mag, max_accel_mag);
			const float blend_frac = 0.03f;

			if(!flying) // If jumping:
				clamped_forwards_accel = 0;

			cur_sideweays_lean = cur_sideweays_lean * (1 - blend_frac) + clamped_sideways_accel * blend_frac;
			cur_forwards_lean  = cur_forwards_lean  * (1 - blend_frac) + clamped_forwards_accel * blend_frac;


			// flying
			if(speed > 10 && moving_forwards && flying)
				new_anim_i = flying_anim_i;
			else
				new_anim_i = floating_anim_i;

			lean_matrix = Matrix4f::rotationAroundXAxis(cur_sideweays_lean * -0.02f) * Matrix4f::rotationAroundYAxis(cur_forwards_lean * -0.02f);
		}


		// NOTE: not sure why this rotationAroundZAxis is needed.  Added to make animated GLB avatar work
		skinned_gl_ob->ob_to_world_matrix = rotateThenTranslateMatrix(pos, rotation) * lean_matrix * Matrix4f::rotationAroundZAxis(Maths::pi_2<float>()) * pre_ob_to_world_matrix;
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
				//conPrint("Started transitioning to anim " + skinned_gl_ob->mesh_data->animation_data.animations[new_anim_i]->name);

				skinned_gl_ob->next_anim_i = new_anim_i;

				skinned_gl_ob->transition_start_time = cur_time;
				skinned_gl_ob->transition_end_time = cur_time + 0.3f;
			}
		}

		last_vel = vel;


		//------------------- Do eye saccades (eye darting movements) -----------------------
		const int left_eye_node_i  = skinned_gl_ob->mesh_data->animation_data.getNodeIndex("LeftEye");
		const int right_eye_node_i = skinned_gl_ob->mesh_data->animation_data.getNodeIndex("RightEye");

		Matrix4f world_to_ob;
		const bool inverted = skinned_gl_ob->ob_to_world_matrix.getInverseForAffine3Matrix(world_to_ob);
		assert(inverted);

		const Vec4f up_os(0,1,0,0);

		if(left_eye_node_i >= 0 && left_eye_node_i < (int)skinned_gl_ob->anim_node_data.size())
		{
			const Vec4f left_eye_pos_os = Vec4f(0.04, 1.67, 0, 1); // TEMP HACK TODO: use actual eye bone pos instead of this hard-coded value.

			// Get rotation quat for looking at cur_eye_target_ws
			Quatf to_cur_rot;
			{
				const Vec4f cur_target_os = world_to_ob * cur_eye_target_ws;
				const Vec4f to_cur_target_os = normalise(cur_target_os - left_eye_pos_os);
				const Vec4f i = normalise(crossProduct(up_os, to_cur_target_os));
				const Vec4f j = crossProduct(to_cur_target_os, i);
				const Matrix4f cur_lookat_matrix(i, j, to_cur_target_os, Vec4f(0,0,0,1));
				to_cur_rot = Quatf::fromMatrix(cur_lookat_matrix);
			}

			// Get rotation quat for looking at next_eye_target_ws
			Quatf to_next_rot;
			{
				const Vec4f next_target_os = world_to_ob * next_eye_target_ws;
				const Vec4f to_next_target_os = normalise(next_target_os - left_eye_pos_os);
				const Vec4f i = normalise(crossProduct(up_os, to_next_target_os));
				const Vec4f j = crossProduct(to_next_target_os, i);
				const Matrix4f next_lookat_matrix(i, j, to_next_target_os, Vec4f(0,0,0,1));
				to_next_rot = Quatf::fromMatrix(next_lookat_matrix);
			}

			// Blend between the rotations based on the transition times.
			const float lerp_frac = Maths::smoothStep(eye_start_transition_time, eye_end_transition_time, cur_time);
			const Quatf lerped_rot = Quatf::nlerp(to_cur_rot, to_next_rot, lerp_frac);

			skinned_gl_ob->anim_node_data[left_eye_node_i].procedural_transform = lerped_rot.toMatrix();
		}

		if(right_eye_node_i >= 0 && right_eye_node_i < (int)skinned_gl_ob->anim_node_data.size())
		{
			const Vec4f right_eye_pos_os = Vec4f(-0.04, 1.67, 0, 1); // TEMP HACK TODO: use actual eye bone pos instead of this hard-coded value.

			Quatf to_cur_rot;
			{
				const Vec4f cur_target_os = world_to_ob * cur_eye_target_ws;
				const Vec4f to_cur_target_os = normalise(cur_target_os - right_eye_pos_os);
				const Vec4f i = normalise(crossProduct(up_os, to_cur_target_os));
				const Vec4f j = crossProduct(to_cur_target_os, i);
				const Matrix4f cur_lookat_matrix(i, j, to_cur_target_os, Vec4f(0,0,0,1));
				to_cur_rot = Quatf::fromMatrix(cur_lookat_matrix);
			}

			Quatf to_next_rot;
			{
				const Vec4f next_target_os = world_to_ob * next_eye_target_ws;
				const Vec4f to_next_target_os = normalise(next_target_os - right_eye_pos_os);
				const Vec4f i = normalise(crossProduct(up_os, to_next_target_os));
				const Vec4f j = crossProduct(to_next_target_os, i);
				const Matrix4f next_lookat_matrix(i, j, to_next_target_os, Vec4f(0,0,0,1));
				to_next_rot = Quatf::fromMatrix(next_lookat_matrix);
			}

			const float lerp_frac = Maths::smoothStep(eye_start_transition_time, eye_end_transition_time, cur_time);
			const Quatf lerped_rot = Quatf::nlerp(to_cur_rot, to_next_rot, lerp_frac);

			skinned_gl_ob->anim_node_data[right_eye_node_i].procedural_transform = lerped_rot.toMatrix();//Matrix4f::rotationAroundYAxis((float)cur_time);
		}

		const double SACCADE_DURATION = 0.03; // 30ms, rough value from wikipedia
		if(cur_time > eye_end_transition_time + saccade_gap)
		{
			eye_start_transition_time = cur_time;
			eye_end_transition_time = cur_time + SACCADE_DURATION;

			cur_eye_target_ws = next_eye_target_ws;
			next_eye_target_ws = pos.toVec4fPoint() + forwards_vec * 2 + right_vec * 0.5f * (-0.5f + rng.unitRandom()) + up_vec * 0.2f * (-0.5f + rng.unitRandom());

			saccade_gap = 0.4 + rng.unitRandom() * rng.unitRandom() * rng.unitRandom() * 3;
		}
	}
	else
	{
		//this->last_hand_pos = toVec3d(this->lower_arms[0].gl_ob->ob_to_world_matrix * Vec4f(0, 0, 1, 1)); // (0,0,1) in forearm cylinder object space is where the hand is.
	}

	
	last_pos = pos;
	last_rotation = rotation;
}


//void AvatarGraphics::create(OpenGLEngine& engine, const std::string& URL)
//{
//}


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
