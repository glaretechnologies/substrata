/*=====================================================================
ParticleManager.cpp
-------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "ParticleManager.h"


#include "PhysicsWorld.h"
#include "TerrainDecalManager.h"


ParticleManager::ParticleManager(const std::string& base_dir_path_, AsyncTextureLoader* async_tex_loader_, OpenGLEngine* opengl_engine_, PhysicsWorld* physics_world_, TerrainDecalManager* terrain_decal_manager_)
:	base_dir_path(base_dir_path_), opengl_engine(opengl_engine_), physics_world(physics_world_), terrain_decal_manager(terrain_decal_manager_),
	async_tex_loader(async_tex_loader_)
{
	TextureParams params;
	params.wrapping = OpenGLTexture::Wrapping::Wrapping_Clamp;

	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/smoke_sprite_top.basis",    this, params));
	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/smoke_sprite_bottom.basis", this, params));
	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/smoke_sprite_left.basis",   this, params));
	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/smoke_sprite_right.basis",  this, params));
	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/smoke_sprite_rear.basis",   this, params));
	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/smoke_sprite_front.basis",  this, params));

	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/foam_sprite_top.basis",    this, params));
	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/foam_sprite_bottom.basis", this, params));
	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/foam_sprite_left.basis",   this, params));
	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/foam_sprite_right.basis",  this, params));
	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/foam_sprite_rear.basis",   this, params));
	loading_handles.push_back(async_tex_loader->startLoadingTexture("/resources/sprites/foam_sprite_front.basis",  this, params));
}


ParticleManager::~ParticleManager()
{
	clear();
}


void ParticleManager::textureLoaded(Reference<OpenGLTexture> texture, const std::string& local_filename)
{
	// conPrint("ParticleManager::textureLoaded: local_filename: '" + local_filename + "'");

	if(     local_filename == "/resources/sprites/smoke_sprite_top.basis")    smoke_sprite_top    = texture;
	else if(local_filename == "/resources/sprites/smoke_sprite_bottom.basis") smoke_sprite_bottom = texture;
	else if(local_filename == "/resources/sprites/smoke_sprite_left.basis")   smoke_sprite_left   = texture;
	else if(local_filename == "/resources/sprites/smoke_sprite_right.basis")  smoke_sprite_right  = texture;
	else if(local_filename == "/resources/sprites/smoke_sprite_rear.basis")   smoke_sprite_rear   = texture;
	else if(local_filename == "/resources/sprites/smoke_sprite_front.basis")  smoke_sprite_front  = texture;
	else if(local_filename == "/resources/sprites/foam_sprite_top.basis")     foam_sprite_top     = texture;
	else if(local_filename == "/resources/sprites/foam_sprite_bottom.basis")  foam_sprite_bottom  = texture;
	else if(local_filename == "/resources/sprites/foam_sprite_left.basis")    foam_sprite_left    = texture;
	else if(local_filename == "/resources/sprites/foam_sprite_right.basis")   foam_sprite_right   = texture;
	else if(local_filename == "/resources/sprites/foam_sprite_rear.basis")    foam_sprite_rear    = texture;
	else if(local_filename == "/resources/sprites/foam_sprite_front.basis")   foam_sprite_front   = texture;
	else
	{
		assert(0);
		conPrint("unknown local_filename: " + local_filename);
	}
}


void ParticleManager::clear()
{
	// Cancel any pending async downloads.
	for(size_t i=0; i<loading_handles.size(); ++i)
		async_tex_loader->cancelLoadingTexture(loading_handles[i]);
	loading_handles.clear();

	for(size_t i=0; i<particles.size(); ++i)
	{
		if(particles[i].gl_ob.nonNull())
			opengl_engine->removeObject(particles[i].gl_ob);
	}
	particles.clear();
}


void ParticleManager::addParticle(const Particle& particle_)
{
	// conPrint("addParticle, particles.size(): " + toString(particles.size()));

	const size_t MAX_NUM_PARTICLES = 2048;

	size_t use_index;
	if(particles.size() >= MAX_NUM_PARTICLES) // If we have enough particles already:
	{
		use_index = rng.nextUInt((uint32)particles.size()); // Pick a random existing particle to replace

		// Remove existing particle at this index
		opengl_engine->removeObject(particles[use_index].gl_ob);
	}
	else
	{
		use_index = particles.size();
		particles.resize(use_index + 1);
	}


	// Add gl ob
	GLObjectRef ob = opengl_engine->allocateObject();
	ob->mesh_data = opengl_engine->getSpriteQuadMeshData();
	ob->materials.resize(1);
	ob->materials[0].albedo_linear_rgb = particle_.colour;
	ob->materials[0].alpha = particle_.cur_opacity;
	ob->materials[0].participating_media = true;
	if(particle_.particle_type == Particle::ParticleType_Smoke)
	{
		ob->materials[0].albedo_texture             = smoke_sprite_top;
		ob->materials[0].metallic_roughness_texture = smoke_sprite_bottom;
		ob->materials[0].lightmap_texture           = smoke_sprite_left;
		ob->materials[0].emission_texture           = smoke_sprite_right;
		ob->materials[0].backface_albedo_texture    = smoke_sprite_rear;
		ob->materials[0].transmission_texture       = smoke_sprite_front;
	}
	else if(particle_.particle_type == Particle::ParticleType_Foam)
	{
		ob->materials[0].albedo_texture             = foam_sprite_top;
		ob->materials[0].metallic_roughness_texture = foam_sprite_bottom;
		ob->materials[0].lightmap_texture           = foam_sprite_left;
		ob->materials[0].emission_texture           = foam_sprite_right;
		ob->materials[0].backface_albedo_texture    = foam_sprite_rear;
		ob->materials[0].transmission_texture       = foam_sprite_front;
	}

	ob->materials[0].materialise_start_time = opengl_engine->getCurrentTime(); // For participating media and decals: materialise_start_time = spawn time
	ob->materials[0].dopacity_dt = particle_.dopacity_dt;

	ob->ob_to_world_matrix = Matrix4f::translationMatrix(particle_.pos) * Matrix4f::uniformScaleMatrix(particle_.width);
	ob->ob_to_world_matrix.e[1] = particle_.theta; // Since object-space vert positions are just (0,0,0) for particle geometry, we can store info in the model matrix.
	opengl_engine->addObject(ob);

	Particle particle = particle_;
	particle.gl_ob = ob;

	particles[use_index] = particle;
}


void ParticleManager::think(const float dt)
{
	//Timer timer;

	const bool water_buoyancy_enabled = physics_world->getWaterBuoyancyEnabled();
	const float water_z = physics_world->getWaterZ();

	for(size_t i=0; i<particles.size();)
	{
		Particle& particle = particles[i];

		assert(particle.pos.isFinite());

		const Vec4f pos_delta = particle.vel * dt;

		
		RayTraceResult results;
		results.hit_object = NULL;
		//if(pos_delta.length2() > Maths::square(1.0e-3f))
			physics_world->traceRay(particle.pos, particle.vel, dt, /*ignore body id=*/JPH::BodyID(), results);

		float remaining_dt = dt;
		if(results.hit_object)
		{
			const float to_hit_dt = results.hit_t;
			assert(to_hit_dt <= dt);
			remaining_dt -= to_hit_dt;

			const Vec4f hitpos = particle.pos + particle.vel * to_hit_dt;

			// Reflect velocity vector in hit normal
			particle.vel -= results.hit_normal_ws * (2 * dot(results.hit_normal_ws, particle.vel));
			particle.vel *= particle.restitution; // Apply restitution factor for inelastic collisions.

			assert(particle.pos.isFinite());
			assert(particle.vel.isFinite());

			particle.pos = hitpos + 
				results.hit_normal_ws * 1.0e-3f + // nudge off surface
				particle.vel * remaining_dt;

			assert(particle.pos.isFinite());
			assert(particle.vel.isFinite());

			if(particle.die_when_hit_surface)
				particle.cur_opacity = -1;
		}
		else
		{
			particle.pos += pos_delta;

			if(water_buoyancy_enabled && (particle.pos[2] < water_z))
			{
				if(particle.die_when_hit_surface && (particle.vel[2] < 0)) // If should die when hit surface, and are moving downwards:
				{
					particle.cur_opacity = -1;

					// Create foam decal at hit position
					Vec4f foam_pos = particle.pos;
					foam_pos[2] = water_z;
					terrain_decal_manager->addFoamDecal(foam_pos, /*width=*/particle.width, /*opacity=*/1.f, TerrainDecalManager::DecalType_SparseFoam);
				}

				// underwater
				particle.vel[2] = myMax(particle.vel[2], 0.5f); // apply buoyancy in a hacky way while not limiting positive z velocity (e.g. for water spray shooting out of water)
			}
			else
				particle.vel[2] -= 9.81f * dt; // Apply gravity
		}

		assert(particle.vel.isFinite());

		// Apply wind-resistance drag force
		const float v_mag2 = particle.vel.length2();
		if(v_mag2 > Maths::square(1.0e-3f))
		{
			// ||a|| = F_d rho * ||v||^2 C_d A / m

			// dvel = -vel/||vel|| * ||a|| * dt    = vel * (||a|| * dt / ||v||)
			// vel' = vel + devl = vel - vel * (||a|| * dt / ||v||)
			// vel' = vel - vel * (F_d rho * ||v||^2 C_d A * dt / (m * ||v||))
			// vel' = vel - vel * (F_d rho * ||v|| C_d A * dt / m)
			// vel' = vel * (1 - F_d rho * ||v|| C_d A * dt / m)

			const float rho = 1.293f; // air density, kg m^-3
			const float projected_forwards_area = particle.area;
			const float forwards_C_d = 0.5f; // drag coefficient
			const float forwards_F_d = 0.5f * rho * v_mag2 * forwards_C_d * projected_forwards_area;
			const float mass = particle.mass;
			const float accel_mag = myMin(10.f, forwards_F_d / mass);

			// dvel = -vel/||vel|| * ||a|| * dt    = vel * (||a|| * dt / ||vel||)
			// vel' = vel + dvel = vel - vel * (||a|| * dt / ||vel||)
			// vel' = vel * (1 - (||a|| * dt / ||vel||))
			particle.vel *= myMax(0.f, 1.f - accel_mag * dt / std::sqrt(v_mag2));

			assert(particle.vel.isFinite());
		}

		assert(particle.pos.isFinite());
		assert(particle.vel.isFinite());
		
		particle.cur_opacity += particle.dopacity_dt * dt;
		particle.width       += particle.dwidth_dt   * dt;

		particle.gl_ob->ob_to_world_matrix = translationMulUniformScaleMatrix(/*translation=*/particle.pos, /*scale=*/particle.width);
		particle.gl_ob->ob_to_world_matrix.e[1] = particle.theta; // Since object-space vert positions are just (0,0,0) for particle geometry, we can store info in the model matrix.

		opengl_engine->updateObjectTransformData(*particle.gl_ob);

		// NOTE: changing alpha directly in shader based on particle lifetime now.
		//particle.gl_ob->materials[0].alpha = particle.cur_opacity;
		//opengl_engine->updateAllMaterialDataOnGPU(*particle.gl_ob); // Since opacity changed.

		if(particle.cur_opacity <= 0)
		{
			//conPrint("removed particle");
			opengl_engine->removeObject(particle.gl_ob);

			// Remove particle: swap with last particle in array
			mySwap(particle, particles.back());
			particles.pop_back(); // Now remove last array element.
			
			// Don't increment i as we there is a new particle in position i that we want to process.
		}
		else
			++i;
	}

	//conPrint("ParticleManager::think() took " + timer.elapsedStringMSWIthNSigFigs(4) + " for " + toString(particles.size()) + " particles.");
}
