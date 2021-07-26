/*=====================================================================
LODGeneration.cpp
-----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "LODGeneration.h"


#include "../server/ServerWorldState.h"
#include <ConPrint.h>
#include <Exception.h>
#include <Lock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <KillThreadMessage.h>
#include <Timer.h>
#include <TaskManager.h>
#include <graphics/MeshSimplification.h>
#include <graphics/formatdecoderobj.h>
#include <graphics/FormatDecoderSTL.h>
#include <graphics/FormatDecoderGLTF.h>
#include <graphics/GifDecoder.h>
#include <graphics/jpegdecoder.h>
#include <graphics/PNGDecoder.h>
#include <graphics/imformatdecoder.h>
#include <graphics/Map2D.h>
#include <graphics/ImageMap.h>
#include <graphics/ImageMapSequence.h>
#include <dll/include/IndigoMesh.h>
#include <dll/include/IndigoException.h>
#include <dll/IndigoStringUtils.h>


namespace LODGeneration
{


BatchedMeshRef loadModel(const std::string& model_path)
{
	BatchedMeshRef batched_mesh = new BatchedMesh();

	if(hasExtension(model_path, "obj"))
	{
		Indigo::MeshRef mesh = new Indigo::Mesh();

		MLTLibMaterials mats;
		FormatDecoderObj::streamModel(model_path, *mesh, 1.f, /*parse mtllib=*/false, mats); // Throws glare::Exception on failure.

		batched_mesh->buildFromIndigoMesh(*mesh);
	}
	else if(hasExtension(model_path, "stl"))
	{
		Indigo::MeshRef mesh = new Indigo::Mesh();

		FormatDecoderSTL::streamModel(model_path, *mesh, 1.f);

		batched_mesh->buildFromIndigoMesh(*mesh);
	}
	else if(hasExtension(model_path, "gltf"))
	{
		Indigo::MeshRef mesh = new Indigo::Mesh();

		GLTFMaterials mats;
		FormatDecoderGLTF::streamModel(model_path, *mesh, 1.0f, mats);

		batched_mesh->buildFromIndigoMesh(*mesh);
	}
	else if(hasExtension(model_path, "igmesh"))
	{
		Indigo::MeshRef mesh = new Indigo::Mesh();

		try
		{
			Indigo::Mesh::readFromFile(toIndigoString(model_path), *mesh);
		}
		catch(Indigo::IndigoException& e)
		{
			throw glare::Exception(toStdString(e.what()));
		}

		batched_mesh->buildFromIndigoMesh(*mesh);
	}
	else if(hasExtension(model_path, "bmesh"))
	{
		BatchedMesh::readFromFile(model_path, *batched_mesh);
	}
	else
		throw glare::Exception("Format not supported: " + getExtension(model_path));

	return batched_mesh;
}


void generateLODModel(BatchedMeshRef batched_mesh, int lod_level, const std::string& LOD_model_path)
{
	BatchedMeshRef simplified_mesh;
	if(lod_level == 1)
	{
		simplified_mesh = MeshSimplification::buildSimplifiedMesh(*batched_mesh, /*target_reduction_ratio=*/10.f, /*target_error=*/0.02f, /*sloppy=*/false);
	}
	else
	{
		assert(lod_level == 2);
		simplified_mesh = MeshSimplification::buildSimplifiedMesh(*batched_mesh, /*target_reduction_ratio=*/100.f, /*target_error=*/0.08f, /*sloppy=*/true);
	}

	simplified_mesh->writeToFile(LOD_model_path);
}


void generateLODModel(const std::string& model_path, int lod_level, const std::string& LOD_model_path)
{
	BatchedMeshRef batched_mesh = loadModel(model_path);

	generateLODModel(batched_mesh, lod_level, LOD_model_path);
}


bool textureHasAlphaChannel(const std::string& tex_path)
{
	if(hasExtension(tex_path, "gif") || hasExtension(tex_path, "jpg"))
		return false;
	else
	{
		Reference<Map2D> map = ImFormatDecoder::decodeImage(".", tex_path); // Load texture from disk and decode it.

		return map->hasAlphaChannel() && !map->isAlphaChannelAllWhite();
	}
}


bool textureHasAlphaChannel(const std::string& tex_path, Map2DRef map)
{
	if(hasExtension(tex_path, "gif") || hasExtension(tex_path, "jpg")) // Some formats can't have alpha at all, so just check the extension to cover those.
		return false;
	else
	{
		return map->hasAlphaChannel() && !map->isAlphaChannelAllWhite();
	}
}


void generateLODTexture(const std::string& base_tex_path, int lod_level, const std::string& LOD_tex_path, glare::TaskManager& task_manager)
{
	const int new_max_w_h = (lod_level == 1) ? 256 : 64;
	const int min_w_h = 4;

	Reference<Map2D> map;
	if(hasExtension(base_tex_path, "gif"))
	{
		//map = GIFDecoder::decodeImageSequence(base_tex_path);

		GIFDecoder::resizeGIF(base_tex_path, LOD_tex_path, new_max_w_h);
	}
	else
	{
		map = ImFormatDecoder::decodeImage(".", base_tex_path); // Load texture from disk and decode it.

		if(dynamic_cast<const ImageMapUInt8*>(map.ptr()))
		{
			int new_w, new_h;
			if(map->getMapWidth() > map->getMapHeight())
			{
				new_w = new_max_w_h;
				new_h = myMax(min_w_h, (int)((float)new_w * (float)map->getMapHeight() / (float)map->getMapWidth()));
			}
			else
			{
				new_h = new_max_w_h;
				new_w = myMax(min_w_h, (int)((float)new_h * (float)map->getMapWidth() / (float)map->getMapHeight()));
			}


			const ImageMapUInt8* imagemap = map.downcastToPtr<ImageMapUInt8>();

			Reference<Map2D> resized_map = imagemap->resizeMidQuality(new_w, new_h, task_manager);
			assert(resized_map.isType<ImageMapUInt8>());

			// Save as a JPEG or PNG depending if there is an alpha channel.
			if(hasExtension(LOD_tex_path, "jpg"))
			{
				if(resized_map->numChannels() > 3)
				{
					// Convert to a 3 channel image
					resized_map = resized_map.downcast<ImageMapUInt8>()->extract3ChannelImage();
				}


				JPEGDecoder::SaveOptions options;
				options.quality = 90;
				JPEGDecoder::save(resized_map.downcast<ImageMapUInt8>(), LOD_tex_path, options);
			}
			else
			{
				assert(hasExtension(LOD_tex_path, "png"));

				PNGDecoder::write(*resized_map.downcastToPtr<ImageMapUInt8>(), LOD_tex_path);
			}
		}
		else
			throw glare::Exception("Unhandled image type: " + base_tex_path);
	}
}


// Look up from cache or recompute.
// returns false if could not load tex.
bool texHasAlpha(const std::string& tex_path, std::map<std::string, bool>& tex_has_alpha)
{
	if(tex_has_alpha.find(tex_path) != tex_has_alpha.end())
	{
		return tex_has_alpha[tex_path];
	}
	else
	{
		bool has_alpha = false;
		try
		{
			has_alpha = textureHasAlphaChannel(tex_path);
		}
		catch(glare::Exception& e)
		{
			conPrint("Excep while calling textureHasAlphaChannel(): " + e.what());
		}
		tex_has_alpha[tex_path] = has_alpha;
		return has_alpha;
	}
}


// Generate LOD textures for materials, if not already present on disk.
void generateLODTexturesForMaterialsIfNotPresent(WorldObject& world_ob, ResourceManager& resource_manager, glare::TaskManager& task_manager)
{
	for(size_t z=0; z<world_ob.materials.size(); ++z)
	{
		WorldMaterial* mat = world_ob.materials[z].ptr();

		if(!mat->colour_texture_url.empty())
		{
			const std::string& base_tex_URL = mat->colour_texture_url;

			for(int lvl = 1; lvl <= 2; ++lvl)
			{
				const std::string lod_URL = WorldObject::getLODTextureURLForLevel(base_tex_URL, lvl, mat->colourTexHasAlpha());

				if(!resource_manager.isFileForURLPresent(lod_URL))
				{
					const std::string local_base_path = resource_manager.pathForURL(base_tex_URL);
					const std::string local_lod_path  = resource_manager.pathForURL(lod_URL); // Path where we will write the LOD texture.

					conPrint("Generating LOD texture '" + local_lod_path + "'...");
					LODGeneration::generateLODTexture(local_base_path, lvl, local_lod_path, task_manager);

					resource_manager.setResourceAsLocallyPresentForURL(lod_URL);
				}
			}
		}
	}
}


} // end namespace LODGeneration
