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
#include <graphics/ImageMapSequence.h>
#include <string>
class WorldMaterial;
class WorldObject;
class ResourceManager;
namespace glare { class TaskManager; }


/*=====================================================================
LODGeneration
-------------

=====================================================================*/
namespace LODGeneration
{

BatchedMeshRef loadModel(const std::string& model_path);

BatchedMeshRef computeLODModel(BatchedMeshRef batched_mesh, int lod_level);

// Generate and save to disk
void generateLODModel(BatchedMeshRef batched_mesh, int lod_level, const std::string& LOD_model_path);

void generateLODModel(const std::string& model_path, int lod_level, const std::string& LOD_model_path);

void generateOptimisedMesh(const std::string& source_mesh_abs_path, int lod_level, const std::string& optimised_mesh_path);

bool textureHasAlphaChannel(const std::string& tex_path, Map2DRef map);

void generateLODTexture(const std::string& base_tex_path, int lod_level, const std::string& LOD_tex_path, glare::TaskManager& task_manager);

void generateBasisTexture(const std::string& src_tex_path, int base_lod_level, int lod_level, const std::string& basis_tex_path, glare::TaskManager& task_manager);

// Generate LOD and KTX textures for materials, if not already present on disk.
//void generateLODTexturesForMaterialsIfNotPresent(std::vector<WorldMaterialRef>& materials, ResourceManager& resource_manager, glare::TaskManager& task_manager);

//void writeBasisUniversalKTXFile(const ImageMapUInt8& imagemap, const std::string& path);
void writeBasisUniversalFile(const ImageMapUInt8& imagemap, const std::string& path, int quality_level);
void writeBasisUniversalFileForSequence(const ImageMapSequenceUInt8& imagemap, const std::string& path, int quality_level);

void test();

}
