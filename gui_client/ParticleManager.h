/*=====================================================================
ParticleManager.h
-----------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "PhysicsObject.h"
#include <opengl/IncludeOpenGL.h>
#include <opengl/OpenGLTexture.h>
#include <opengl/OpenGLEngine.h>
#include <maths/PCG32.h>
#include <utils/RefCounted.h>
#include <utils/Reference.h>
class OpenGLShader;
class OpenGLMeshRenderData;
class VertexBufferAllocator;
class PhysicsWorld;
class BiomeManager;


struct Particle
{
	GLARE_ALIGNED_16_NEW_DELETE

	Particle() : restitution(0.5f), width(0.1f), cur_opacity(1.f), opacity_rate_of_change_mag(0.3f), colour(0.8f), mass(1.0e-6f), area(1.0e-6f) {}

	Vec4f pos;
	Vec4f vel;

	GLObjectRef gl_ob;

	Colour3f colour;

	float area; // particle cross-sectional area (m^2).  Larger area = more wind drag.  TODO: just store ratio of area to mass?
	float mass;
	float restitution; // "Restitution of body (dimensionless number, usually between 0 and 1, 0 = completely inelastic collision response, 1 = completely elastic collision response)"

	float width;
	float cur_opacity;
	float opacity_rate_of_change_mag; // dopacity / dt = -opacity_rate_of_change_mag
};


/*=====================================================================
ParticleManager
---------------
The basic idea is to simulate point particles with ray-traced collisions, and a simple physics model with 
bouncing off surfaces and with wind resistance.
See https://github.com/jrouwe/JoltPhysics/discussions/756 for a discussion of the approach.
=====================================================================*/
class ParticleManager : public RefCounted
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	ParticleManager(const std::string& base_dir_path, OpenGLEngine* opengl_engine, PhysicsWorld* physics_world);
	~ParticleManager();

	void clear();

	void addParticle(const Particle& particle);

	void think(float dt);

private:
	std::string base_dir_path;
	OpenGLEngine* opengl_engine;
	PhysicsWorld* physics_world;
	PCG32 rng;
	std::vector<Particle> particles;

	Reference<OpenGLTexture> smoke_sprite_top;
	Reference<OpenGLTexture> smoke_sprite_bottom;
	Reference<OpenGLTexture> smoke_sprite_left;
	Reference<OpenGLTexture> smoke_sprite_right;
	Reference<OpenGLTexture> smoke_sprite_rear;
	Reference<OpenGLTexture> smoke_sprite_front;
};
