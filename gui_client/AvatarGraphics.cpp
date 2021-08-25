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
		if(cur_time >= skinned_gl_ob->mesh_data->transition_end_time && skinned_gl_ob->mesh_data->next_anim_i != -1)
		{
			skinned_gl_ob->mesh_data->current_anim_i = skinned_gl_ob->mesh_data->next_anim_i;
			skinned_gl_ob->mesh_data->next_anim_i = -1;
			conPrint("Finished transitioning to anim " + skinned_gl_ob->mesh_data->animation_data.animations[skinned_gl_ob->mesh_data->current_anim_i]->name);
		}


		const Vec4f forwards_vec = rotationMatrix(rotation) * Vec4f(1,0,0,0);
		//conPrint("forwards_vec: " + forwards_vec.toString());

		// NOTE: not sure why this rotationAroundZAxis is needed.  Added to make animated GLB avatar work
		skinned_gl_ob->ob_to_world_matrix = rotateThenTranslateMatrix(pos, rotation) * Matrix4f::rotationAroundZAxis(Maths::pi_2<float>()) * pre_ob_to_world_matrix;
		engine.updateObjectTransformData(*skinned_gl_ob);

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

		const Vec3d dpos = pos - last_pos;
		const double speed = pos.getDist(last_pos) / dt;

		int new_anim_i = 0;
		if(anim_state == 0) // On ground
		{
			if(speed > 6)
			{
				const bool moving_forwards = dot(forwards_vec, normalise(dpos.toVec4fVector())) > -0.1f;

				if(moving_forwards)
					new_anim_i = running_anim_i;
				else
					new_anim_i = running_backwards_anim_i;
			}
			else if(speed > 0.1)
			{
				const bool moving_forwards = dot(forwards_vec, normalise(dpos.toVec4fVector())) > -0.1f;

				if(moving_forwards)
					new_anim_i = walking_anim_i;
				else
					new_anim_i = walking_backwards_anim_i;
			}
			else // else if (nearly) stationary:
			{
				new_anim_i = idle_anim_i;
			}
		}
		else
		{
			const bool moving_forwards = dot(forwards_vec, normalise(dpos.toVec4fVector())) > -0.1f;

			// flying
			if(speed > 10 && moving_forwards)
				new_anim_i = flying_anim_i;
			else
				new_anim_i = floating_anim_i;
		}

		// See if we need to start a transition to a new animation
		if(new_anim_i != skinned_gl_ob->mesh_data->current_anim_i)
		{
			// If we are currently transitioning, don't change next
			if(cur_time >= skinned_gl_ob->mesh_data->transition_start_time && cur_time < skinned_gl_ob->mesh_data->transition_end_time)
			{
			}
			else
			{
				conPrint("Started transitioning to anim " + skinned_gl_ob->mesh_data->animation_data.animations[new_anim_i]->name);

				skinned_gl_ob->mesh_data->next_anim_i = new_anim_i;

				skinned_gl_ob->mesh_data->transition_start_time = cur_time;
				skinned_gl_ob->mesh_data->transition_end_time = cur_time + 0.3f;
			}
		}
	}
	else
	{
		//this->last_hand_pos = toVec3d(this->lower_arms[0].gl_ob->ob_to_world_matrix * Vec4f(0, 0, 1, 1)); // (0,0,1) in forearm cylinder object space is where the hand is.
	}

	last_pos = pos;
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
