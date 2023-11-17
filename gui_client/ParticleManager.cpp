/*=====================================================================
ParticleManager.cpp
-------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "ParticleManager.h"


#include "PhysicsWorld.h"


ParticleManager::ParticleManager(const std::string& base_dir_path_, OpenGLEngine* opengl_engine_, PhysicsWorld* physics_world_)
:	base_dir_path(base_dir_path_), opengl_engine(opengl_engine_), physics_world(physics_world_)
{
	smoke_sprite_top	= opengl_engine->getTexture(base_dir_path + "/resources/sprites/smoke_sprite_top.ktx2");
	smoke_sprite_bottom = opengl_engine->getTexture(base_dir_path + "/resources/sprites/smoke_sprite_bottom.ktx2");
	smoke_sprite_left	= opengl_engine->getTexture(base_dir_path + "/resources/sprites/smoke_sprite_left.ktx2");
	smoke_sprite_right	= opengl_engine->getTexture(base_dir_path + "/resources/sprites/smoke_sprite_right.ktx2");
	smoke_sprite_rear	= opengl_engine->getTexture(base_dir_path + "/resources/sprites/smoke_sprite_rear.ktx2");
	smoke_sprite_front	= opengl_engine->getTexture(base_dir_path + "/resources/sprites/smoke_sprite_front.ktx2");
}


ParticleManager::~ParticleManager()
{
	clear();
}


void ParticleManager::clear()
{
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
	GLObjectRef ob = new GLObject();
	ob->mesh_data = opengl_engine->getSpriteQuadMeshData();
	ob->materials.resize(1);
	ob->materials[0].albedo_linear_rgb = particle_.colour;
	ob->materials[0].alpha = particle_.cur_opacity;
	ob->materials[0].participating_media = true;
	ob->materials[0].albedo_texture             = smoke_sprite_top;
	ob->materials[0].metallic_roughness_texture = smoke_sprite_bottom;
	ob->materials[0].lightmap_texture           = smoke_sprite_left;
	ob->materials[0].emission_texture           = smoke_sprite_right;
	ob->materials[0].backface_albedo_texture    = smoke_sprite_rear;
	ob->materials[0].transmission_texture       = smoke_sprite_front;
	ob->ob_to_world_matrix = Matrix4f::translationMatrix(particle_.pos) * Matrix4f::uniformScaleMatrix(particle_.width);
	opengl_engine->addObject(ob);

	Particle particle = particle_;
	particle.gl_ob = ob;

	particles[use_index] = particle;
}


void ParticleManager::think(const float dt)
{
	Timer timer;

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
			physics_world->traceRay(particle.pos, particle.vel, dt, results);

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
		}
		else
		{
			particle.pos += pos_delta;

			if(water_buoyancy_enabled && (particle.pos[2] < water_z))
			{
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
		
		particle.cur_opacity -= particle.opacity_rate_of_change_mag * dt;

		particle.gl_ob->ob_to_world_matrix = translationMulUniformScaleMatrix(/*translation=*/particle.pos, /*scale=*/particle.width);
		opengl_engine->updateObjectTransformData(*particle.gl_ob);

		particle.gl_ob->materials[0].alpha = particle.cur_opacity;
		opengl_engine->updateAllMaterialDataOnGPU(*particle.gl_ob); // Since opacity changed.  TODO: this call is slow, do not every frame.

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

	//conPrint("ParticleManager::think() took " + timer.elapsedStringMSWIthNSigFigs(4));
}
