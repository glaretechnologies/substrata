/*=====================================================================
Scripting.cpp
-------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "Scripting.h"


#include "WinterShaderEvaluator.h"
#include "PhysicsWorld.h"
#include "WorldState.h"
#include "../shared/WorldObject.h"
#include <opengl/OpenGLEngine.h>
#include <utils/Timer.h>
#include <utils/Lock.h>
#include "superluminal/PerformanceAPI.h"


namespace Scripting
{


void evalObjectScript(WorldObject* ob, float use_global_time, double dt, OpenGLEngine* opengl_engine, PhysicsWorld* physics_world, Matrix4f& ob_to_world_out)
{
	CybWinterEnv winter_env;
	winter_env.instance_index = 0;
	winter_env.num_instances = 1;

	if(ob->script_evaluator->jitted_evalRotation)
	{
		const Vec4f rot = ob->script_evaluator->evalRotation(use_global_time, winter_env);
		ob->angle = rot.length();
		if(isFinite(ob->angle))
		{
			if(ob->angle > 0)
				ob->axis = Vec3f(normalise(rot));
			else
				ob->axis = Vec3f(1, 0, 0);
		}
		else
		{
			ob->angle = 0;
			ob->axis = Vec3f(1, 0, 0);
		}
	}

	if(ob->script_evaluator->jitted_evalTranslation)
	{
		ob->translation = ob->script_evaluator->evalTranslation(use_global_time, winter_env);
	}

	// Compute object-to-world matrix, similarly to obToWorldMatrix().  Do it here so we can reuse some components of the computation.
	const Vec4f pos((float)ob->pos.x, (float)ob->pos.y, (float)ob->pos.z, 1.f);
	const Vec4f translation = pos + ob->translation;

	// Don't use a zero scale component, because it makes the matrix uninvertible, which breaks various things, including picking and normals.
	Vec4f use_scale = ob->scale.toVec4fVector();
	if(use_scale[0] == 0) use_scale[0] = 1.0e-6f;
	if(use_scale[1] == 0) use_scale[1] = 1.0e-6f;
	if(use_scale[2] == 0) use_scale[2] = 1.0e-6f;

	const Vec4f unit_axis = normalise(ob->axis.toVec4fVector());
	const Matrix4f rot = Matrix4f::rotationMatrix(unit_axis, ob->angle);
	Matrix4f ob_to_world;
	ob_to_world.setColumn(0, rot.getColumn(0) * use_scale[0]);
	ob_to_world.setColumn(1, rot.getColumn(1) * use_scale[1]);
	ob_to_world.setColumn(2, rot.getColumn(2) * use_scale[2]);
	ob_to_world.setColumn(3, translation);

	/* Compute upper-left inverse transpose matrix.
	upper left inverse transpose:
	= ((RS)^-1)^T
	= (S^1 R^1)^T
	= R^1^T S^1^T
	= R S^1
	*/

	const Vec4f recip_scale = maskWToZero(div(Vec4f(1.f), use_scale));

	// Right-multiplying with a scale matrix is equivalent to multiplying column 0 with scale_x, column 1 with scale_y etc.
	Matrix4f ob_to_world_inv_transpose;
	ob_to_world_inv_transpose.setColumn(0, rot.getColumn(0) * recip_scale[0]);
	ob_to_world_inv_transpose.setColumn(1, rot.getColumn(1) * recip_scale[1]);
	ob_to_world_inv_transpose.setColumn(2, rot.getColumn(2) * recip_scale[2]);
	ob_to_world_inv_transpose.setColumn(3, Vec4f(0, 0, 0, 1));

	// Update transform in 3d engine.
	if(ob->opengl_engine_ob.nonNull())
	{
		ob->opengl_engine_ob->ob_to_world_matrix = ob_to_world;
		ob->opengl_engine_ob->ob_to_world_inv_transpose_matrix = ob_to_world_inv_transpose;
		ob->opengl_engine_ob->aabb_ws = ob->opengl_engine_ob->mesh_data->aabb_os.transformedAABBFast(ob_to_world);

		// Update object world space AABB (used for computing LOD level).
		// For objects with animated rotation, we want to compute an AABB without rotation, otherwise we can get a world-space AABB
		// that effectively oscillates in size.  See https://youtu.be/Wo_PauArb6A for an example.
		// This is bad because it can cause the object to oscillate between LOD levels.
		// The AABB will be somewhat wrong, but hopefully it shouldn't matter too much.
		if(ob->script_evaluator->jitted_evalRotation)
		{
			Matrix4f TS;
			TS.setColumn(0, Vec4f(use_scale[0], 0, 0, 0));
			TS.setColumn(1, Vec4f(0, use_scale[1], 0, 0));
			TS.setColumn(2, Vec4f(0, 0, use_scale[2], 0));
			TS.setColumn(3, translation);

			ob->aabb_ws = ob->opengl_engine_ob->mesh_data->aabb_os.transformedAABBFast(TS);
		}
		else
		{
			ob->aabb_ws = ob->opengl_engine_ob->aabb_ws;
		}


		// TODO: need to call assignLightsToObject() somehow
		// opengl_engine->updateObjectTransformData(*ob->opengl_engine_ob);
	}

	// Update in physics engine
	if(ob->physics_object.nonNull())
		physics_world->moveKinematicObject(*ob->physics_object, translation, Quatf::fromAxisAndAngle(unit_axis, ob->angle), (float)dt);


	if(ob->opengl_light.nonNull())
	{
		ob->opengl_light->gpu_data.dir = normalise(ob_to_world * Vec4f(0, 0, -1, 0));

		opengl_engine->setLightPos(ob->opengl_light, setWToOne(translation));
	}

	ob_to_world_out = ob_to_world;
}


void evalObjectInstanceScript(InstanceInfo* ob, float use_global_time, double dt, OpenGLEngine* opengl_engine, PhysicsWorld* physics_world, Matrix4f& ob_to_world_out)
{
	CybWinterEnv winter_env;
	winter_env.instance_index = ob->instance_index;
	winter_env.num_instances = ob->num_instances;

	if(ob->script_evaluator->jitted_evalRotation)
	{
		const Vec4f rot = ob->script_evaluator->evalRotation(use_global_time, winter_env);
		ob->angle = rot.length();
		if(isFinite(ob->angle))
		{
			if(ob->angle > 0)
				ob->axis = Vec3f(normalise(rot));
			else
				ob->axis = Vec3f(1, 0, 0);
		}
		else
		{
			ob->angle = 0;
			ob->axis = Vec3f(1, 0, 0);
		}
	}

	if(ob->script_evaluator->jitted_evalTranslation)
	{
		if(ob->prototype_object)
			ob->pos = ob->prototype_object->pos;
		ob->translation = ob->script_evaluator->evalTranslation(use_global_time, winter_env);
	}

	// Compute object-to-world matrix, similarly to obToWorldMatrix().  Do it here so we can reuse some components of the computation.
	const Vec4f pos((float)ob->pos.x, (float)ob->pos.y, (float)ob->pos.z, 1.f);
	const Vec4f translation = pos + ob->translation;

	// Don't use a zero scale component, because it makes the matrix uninvertible, which breaks various things, including picking and normals.
	Vec4f use_scale = ob->scale.toVec4fVector();
	if(use_scale[0] == 0) use_scale[0] = 1.0e-6f;
	if(use_scale[1] == 0) use_scale[1] = 1.0e-6f;
	if(use_scale[2] == 0) use_scale[2] = 1.0e-6f;

	const Vec4f unit_axis = normalise(ob->axis.toVec4fVector());
	const Matrix4f rot = Matrix4f::rotationMatrix(unit_axis, ob->angle);
	Matrix4f ob_to_world;
	ob_to_world.setColumn(0, rot.getColumn(0) * use_scale[0]);
	ob_to_world.setColumn(1, rot.getColumn(1) * use_scale[1]);
	ob_to_world.setColumn(2, rot.getColumn(2) * use_scale[2]);
	ob_to_world.setColumn(3, translation);

	// Update in physics engine
	if(ob->physics_object.nonNull())
		physics_world->moveKinematicObject(*ob->physics_object, translation, Quatf::fromAxisAndAngle(unit_axis, ob->angle), (float)dt);

	ob_to_world_out = ob_to_world;
}


void evaluateObjectScripts(std::set<WorldObjectRef>& obs_with_scripts, double global_time, double dt, WorldState* world_state, OpenGLEngine* opengl_engine, PhysicsWorld* physics_world,
	int& num_scripts_processed_out)
{
	// Evaluate scripts on objects
	if(world_state)
	{
		PERFORMANCEAPI_INSTRUMENT("eval scripts");
		int num_scripts_processed = 0;

		Lock lock(world_state->mutex);

		// When float values get too large, the gap between successive values gets greater than the frame period,
		// resulting in 'jumpy' transformations.  So mod the double value down to a smaller range (that wraps e.g. once per hour)
		// and then cast to float.
		const float use_global_time = (float)Maths::doubleMod(global_time, 3600);

		for(auto it = obs_with_scripts.begin(); it != obs_with_scripts.end(); ++it)
		{
			WorldObject* ob = it->getPointer();

			assert(ob->script_evaluator.nonNull());
			if(ob->script_evaluator.nonNull())
			{
				Matrix4f ob_to_world;
				evalObjectScript(ob, use_global_time, dt, opengl_engine, physics_world, ob_to_world);
				num_scripts_processed++;

				// If this object has instances (and has a graphics ob):
				if(!ob->instances.empty() && ob->opengl_engine_ob.nonNull())
				{
					// Update instance ob-to-world transform based on the script.
					// Compute AABB over all instances of the object
					js::AABBox all_instances_aabb_ws = js::AABBox::emptyAABBox();
					const js::AABBox aabb_os = ob->opengl_engine_ob->mesh_data->aabb_os;
					for(size_t z=0; z<ob->instances.size(); ++z)
					{
						InstanceInfo* instance = &ob->instances[z];
						Matrix4f instance_ob_to_world;
						evalObjectInstanceScript(instance, use_global_time, dt, opengl_engine, physics_world, instance_ob_to_world); // Updates instance->physics_object->ob_to_world
						num_scripts_processed++;

						all_instances_aabb_ws.enlargeToHoldAABBox(aabb_os.transformedAABBFast(instance_ob_to_world));

						ob->instance_matrices[z] = instance_ob_to_world;
					}

					// Manually set AABB of instanced object.
					// NOTE: we will avoid opengl_engine->updateObjectTransformData(), since it doesn't handle instances currently.
					ob->opengl_engine_ob->aabb_ws = all_instances_aabb_ws;

					// Also update instance_matrix_vbo.
					assert(ob->instance_matrices.size() == ob->instances.size());
					if(ob->opengl_engine_ob->instance_matrix_vbo.nonNull())
					{
						ob->opengl_engine_ob->instance_matrix_vbo->updateData(ob->instance_matrices.data(), ob->instance_matrices.dataSizeBytes());
					}
				}
			}
		}

		num_scripts_processed_out = num_scripts_processed;
	}
	else
	{
		num_scripts_processed_out = 0;
	}
}


} // end namespace Scripting
