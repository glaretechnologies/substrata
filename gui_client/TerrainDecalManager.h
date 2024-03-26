/*=====================================================================
TerrainDecalManager.h
---------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "PhysicsObject.h"
#include <opengl/AsyncTextureLoader.h>
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
	float dopacity_dt;
};


/*=====================================================================
TerrainDecalManager
-------------------
=====================================================================*/
class TerrainDecalManager : public RefCounted, public AsyncTextureLoadedHandler
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	TerrainDecalManager(const std::string& base_dir_path, AsyncTextureLoader* async_tex_loader, OpenGLEngine* opengl_engine);
	~TerrainDecalManager();

	void clear();

	virtual void textureLoaded(Reference<OpenGLTexture> texture, const std::string& local_filename) override;

	enum DecalType
	{
		DecalType_ThickFoam,
		DecalType_SparseFoam
	};

	void addFoamDecal(const Vec4f& foam_pos, float ob_width, float opacity, DecalType decal_type);

	void think(float dt);
private:
	PCG32 rng;
	OpenGLEngine* opengl_engine;
	Reference<OpenGLTexture> foam_texture;
	Reference<OpenGLTexture> foam_sprite_front;
	std::vector<FoamDecal> foam_decals;

	AsyncTextureLoader* async_tex_loader;
};
