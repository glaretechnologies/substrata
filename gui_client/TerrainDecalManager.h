/*=====================================================================
TerrainDecalManager.h
---------------------
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


struct FoamDecal
{
	GLARE_ALIGNED_16_NEW_DELETE

	Vec4f pos;
	Matrix4f rot_matrix;
	
	GLObjectRef decal_ob;

	float cur_width;
	float cur_opacity;
};

/*=====================================================================
TerrainDecalManager
-------------------
=====================================================================*/
class TerrainDecalManager : public RefCounted
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	TerrainDecalManager(const std::string& base_dir_path, OpenGLEngine* opengl_engine);
	~TerrainDecalManager();

	void clear();

	void addFoamDecal(const Vec4f& foam_pos, float ob_width, float opacity);

	void think(float dt);
private:
	PCG32 rng;
	OpenGLEngine* opengl_engine;
	Reference<OpenGLTexture> foam_texture;
	std::vector<FoamDecal> foam_decals;
};
