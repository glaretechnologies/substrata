/*=====================================================================
TerrainDecalManager.cpp
-----------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "TerrainDecalManager.h"


TerrainDecalManager::TerrainDecalManager(const std::string& base_dir_path, OpenGLEngine* opengl_engine_)
:	opengl_engine(opengl_engine_)
{
	foam_texture = opengl_engine->getTexture(base_dir_path + "/resources/foam_windowed.ktx2"); 
}


TerrainDecalManager::~TerrainDecalManager()
{
	clear();
}


void TerrainDecalManager::clear()
{
	for(size_t i=0; i<foam_decals.size(); ++i)
	{
		opengl_engine->addObject(foam_decals[i].decal_ob);
	}
	foam_decals.clear();
}


static inline Matrix4f foamTransform(const FoamDecal& decal)
{
	return leftTranslateAffine3(maskWToZero(decal.pos), decal.rot_matrix) * 
		scaleMulTranslationMatrix(decal.cur_width, decal.cur_width, 0.1f, /*translation=*/Vec4f(-0.5f));
}


void TerrainDecalManager::addFoamDecal(const Vec4f& foam_pos, float ob_width, float opacity)
{
	const size_t MAX_NUM_DECALS = 512;

	size_t use_index;
	if(foam_decals.size() >= MAX_NUM_DECALS) // If we have enough decals already:
	{
		use_index = rng.nextUInt((uint32)foam_decals.size()); // Pick a random existing decal to replace

		// Remove existing decal at this index
		opengl_engine->removeObject(foam_decals[use_index].decal_ob);
	}
	else
	{
		use_index = foam_decals.size();
		foam_decals.resize(use_index + 1);
	}


	FoamDecal decal;
	decal.pos = foam_pos;
	decal.rot_matrix = Matrix4f::rotationAroundZAxis(rng.unitRandom() * Maths::get2Pi<float>());
	decal.cur_width = ob_width * 0.85f;
	decal.cur_opacity = opacity;

	// Add decal
	GLObjectRef ob = new GLObject();
	ob->decal = true;
	ob->mesh_data = opengl_engine->getCubeMeshData();
	ob->materials.resize(1);
	ob->materials[0].albedo_linear_rgb = Colour3f(1.f);
	ob->materials[0].alpha = 1;
	ob->materials[0].double_sided = true;
	ob->materials[0].decal = true;
	ob->materials[0].albedo_texture = foam_texture;
	ob->ob_to_world_matrix = foamTransform(decal);
	opengl_engine->addObject(ob);

	decal.decal_ob = ob;

	foam_decals[use_index] = decal;
}


void TerrainDecalManager::think(float dt)
{
	for(size_t i=0; i<foam_decals.size();)
	{
		FoamDecal& decal = foam_decals[i];

		decal.cur_width += 0.5f * dt;

		decal.cur_opacity -= 0.3f * dt;

		decal.decal_ob->ob_to_world_matrix = foamTransform(decal);
		opengl_engine->updateObjectTransformData(*decal.decal_ob);

		decal.decal_ob->materials[0].alpha = decal.cur_opacity;
		opengl_engine->updateAllMaterialDataOnGPU(*decal.decal_ob); // Since opacity changed

		if(decal.cur_opacity <= 0)
		{
			//conPrint("removed decal");
			opengl_engine->removeObject(decal.decal_ob);

			// Remove decal: swap with last decal in array
			mySwap(decal, foam_decals.back());
			foam_decals.pop_back(); // Now remove last array element.
			
			// Don't increment i as we there is a new decal in position i that we want to process.
		}
		else
			++i;
	}
}