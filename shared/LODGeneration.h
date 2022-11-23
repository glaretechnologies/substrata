/*=====================================================================
LODGeneration.h
---------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "WorldMaterial.h"
#include <graphics/BatchedMesh.h>
#include <graphics/Map2D.h>
#include <graphics/ImageMap.h>
#include <string>
class WorldMaterial;
class WorldObject;
class ResourceManager;
namespace glare { class TaskManager; }
namespace glare { class GeneralMemAllocator; }


/*=====================================================================
LODGeneration
-------------

=====================================================================*/
namespace LODGeneration
{

BatchedMeshRef loadModel(const std::string& model_path);

void generateLODModel(BatchedMeshRef batched_mesh, int lod_level, const std::string& LOD_model_path);

void generateLODModel(const std::string& model_path, int lod_level, const std::string& LOD_model_path);

bool textureHasAlphaChannel(const std::string& tex_path, Map2DRef map);

void generateLODTexture(const std::string& base_tex_path, int lod_level, const std::string& LOD_tex_path, glare::TaskManager& task_manager);

void generateKTXTexture(const std::string& src_tex_path, int base_lod_level, int lod_level, const std::string& ktx_tex_path, glare::TaskManager& task_manager);

// Generate LOD and KTX textures for materials, if not already present on disk.
void generateLODTexturesForMaterialsIfNotPresent(std::vector<WorldMaterialRef>& materials, ResourceManager& resource_manager, glare::TaskManager& task_manager);

void writeBasisUniversalKTXFile(const ImageMapUInt8& imagemap, const std::string& path);

void test();

}
