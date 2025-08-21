/*=====================================================================
MeshLODGenThread.cpp
--------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "MeshLODGenThread.h"


#include "Server.h"
#include "ServerWorldState.h"
#include "../shared/LODGeneration.h"
#include "../shared/ImageDecoding.h"
#include "../shared/Protocol.h"
#include <ConPrint.h>
#include <Exception.h>
#include <Lock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <Timer.h>
#include <TaskManager.h>
#include <FileUtils.h>
#include <KillThreadMessage.h>
#include <graphics/ImageMap.h>


MeshLODGenThread::MeshLODGenThread(Server* server_, ServerAllWorldsState* world_state_)
:	server(server_),
	world_state(world_state_)
{
}


MeshLODGenThread::~MeshLODGenThread()
{
}


static bool textureHasAlphaChannel(Reference<Map2D>& map)
{
	return map->hasAlphaChannel() && !map->isAlphaChannelAllWhite();
}


// Also for optimised meshes to generate.
struct LODMeshToGen
{
	std::string model_abs_path;
	std::string LOD_model_abs_path;
	std::string lod_URL;
	int lod_level;
	UserID owner_id;
	bool build_optimised_mesh;
};


struct LODTextureToGen
{
	std::string source_tex_abs_path; // Absolute base texture path, to read texture from.
	std::string LOD_tex_abs_path; // LOD texture path, to write LOD texture to.
	std::string lod_URL;
	int lod_level;
	UserID owner_id;
};


struct BasisTextureToGen
{
	std::string source_tex_abs_path; // source texture abs path
	std::string basis_tex_abs_path; // abs path to write KTX texture to.
	std::string basis_URL;
	int base_lod_level;
	int lod_level;
	UserID owner_id;
};



struct MeshLODGenThreadTexInfo
{
	bool has_alpha;
	bool is_hi_res;
};


// Set object world space AABB if not set yet, or if it's incorrect.
static void checkObjectSpaceAABB(ServerAllWorldsState* world_state, ServerWorldState* world, WorldObject* ob)
{
	try
	{
		// conPrint("computing AABB for object " + ob->uid.toString() + "...");


		checkTransformOK(ob); // Throws glare::Exception if not ok.

		// Get object space AABB
		js::AABBox aabb_os = js::AABBox::emptyAABBox();

		if(ob->object_type == WorldObject::ObjectType_Hypercard)
		{
			aabb_os = js::AABBox(Vec4f(0,0,0,1), Vec4f(1,0,1,1));
		}
		else if(ob->object_type == WorldObject::ObjectType_Spotlight)
		{
			const float fixture_w = 0.1;
			aabb_os = js::AABBox(Vec4f(-fixture_w/2, -fixture_w/2, 0,1), Vec4f(fixture_w/2,  fixture_w/2, 0,1));
		}
		else if(ob->object_type == WorldObject::ObjectType_WebView)
		{
			aabb_os = js::AABBox(Vec4f(0,0,0,1), Vec4f(1,0,1,1));
		}
		else if(ob->object_type == WorldObject::ObjectType_Video)
		{
			aabb_os = js::AABBox(Vec4f(0,0,0,1), Vec4f(1,0,1,1));
		}
		else if(ob->object_type == WorldObject::ObjectType_VoxelGroup)
		{
			try
			{
				if(!ob->getCompressedVoxels())
					throw glare::Exception("null compressed voxels");

				VoxelGroup voxel_group;
				WorldObject::decompressVoxelGroup(ob->getCompressedVoxels()->data(), ob->getCompressedVoxels()->size(), /*mem allocator=*/NULL, voxel_group);
				aabb_os = voxel_group.getAABB();

				/*const int new_max_lod_level = (voxel_group.voxels.size() > 256) ? 2 : 0;
				if(new_max_lod_level != ob->max_model_lod_level)
				{
					Lock lock(world_state->mutex);
					world->addWorldObjectAsDBDirty(ob);
				}

				ob->max_model_lod_level = new_max_lod_level;*/
			}
			catch(glare::Exception& e)
			{
				throw glare::Exception("Error while decompressing voxel group: " + e.what());
			}
		}
		else if(ob->object_type == WorldObject::ObjectType_Generic)
		{
			// Try and load mesh, get AABB from it.
			if(!ob->model_url.empty())
			{
				const std::string model_abs_path = world_state->resource_manager->pathForURL(ob->model_url);

				BatchedMeshRef batched_mesh = LODGeneration::loadModel(model_abs_path);
					
				aabb_os = batched_mesh->aabb_os;
			}
		}
		else
			throw glare::Exception("invalid object type.");

		// Compute and assign aabb_ws to object.
		if(!aabb_os.isEmpty()) // If we got a valid aabb_os:
		{
			WorldStateLock lock(world_state->mutex);

			const bool updating_aabb_ws = !(approxEq(aabb_os.min_, ob->getAABBOS().min_) && approxEq(aabb_os.max_, ob->getAABBOS().max_)); //aabb_os != ob->getAABBOS();
			if(updating_aabb_ws)
			{
				conPrint("Updating object AABB_os:");
				conPrint("Old AABB_os: "+ ob->getAABBOS().toString());
				conPrint("New AABB_os: "+ aabb_os.toString());

				ob->setAABBOS(aabb_os);
				world->addWorldObjectAsDBDirty(ob, lock);
			}
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("MeshLODGenThread: glare::Exception: " + e.what());
	}
	catch(std::exception& e) // catch std::bad_alloc etc..
	{
		conPrint(std::string("MeshLODGenThread: Caught std::exception: ") + e.what());
	}
}


static void checkForLODMeshesToGenerate(ServerAllWorldsState* world_state, ServerWorldState* world, WorldObject* ob, std::unordered_set<std::string>& lod_URLs_considered, std::vector<LODMeshToGen>& meshes_to_gen)
{
	try
	{
		if(ob->object_type == WorldObject::ObjectType_Generic)
		{
			if(!ob->model_url.empty())
			{
				ResourceRef base_resource = world_state->resource_manager->getExistingResourceForURL(ob->model_url);
				if(base_resource && base_resource->isPresent()) // Base resource needs to be fully present before we start processing it.
				{
					const std::string base_model_abs_path = world_state->resource_manager->getLocalAbsPathForResource(*base_resource);

					// Check new_max_lod_level is correct:
					/*BatchedMeshRef batched_mesh = LODGeneration::loadModel(model_abs_path);
				
					const int new_max_lod_level = (batched_mesh->numVerts() <= 4 * 6) ? 0 : 2; // If this is a very small model (e.g. a cuboid), don't generate LOD versions of it.
					if(new_max_lod_level != ob->max_model_lod_level)
					{
						Lock lock(world_state->mutex);
						world->addWorldObjectAsDBDirty(ob);
					}

					ob->max_model_lod_level = new_max_lod_level;
					*/

					if(ob->max_model_lod_level == 2)
					{
						for(int lvl = 1; lvl <= 2; ++lvl)
						{
							WorldObject::GetLODModelURLOptions options(/*get_optimised_mesh=*/false, Protocol::OPTIMISED_MESH_VERSION);
							const std::string lod_abs_path = WorldObject::getLODModelURLForLevel(base_model_abs_path, lvl, options);
							const std::string lod_URL  = WorldObject::getLODModelURLForLevel(ob->model_url, lvl, options);

							if(lod_URLs_considered.count(lod_URL) == 0)
							{
								lod_URLs_considered.insert(lod_URL);

								if(!world_state->resource_manager->isFileForURLPresent(lod_URL))
								{
									// Add to list of models to generate
									LODMeshToGen mesh_to_gen;
									mesh_to_gen.lod_level = lvl;
									mesh_to_gen.model_abs_path = base_model_abs_path;
									mesh_to_gen.LOD_model_abs_path = lod_abs_path;
									mesh_to_gen.lod_URL = lod_URL;
									mesh_to_gen.owner_id = base_resource->owner_id;
									mesh_to_gen.build_optimised_mesh = false;
									meshes_to_gen.push_back(mesh_to_gen);
								}
								//else // Else if LOD model is present on disk:
								//{
								//	if(lvl == 1)
								//	{
								//		try
								//		{
								//			BatchedMeshRef lod1_mesh = LODGeneration::loadModel(lod_abs_path);
								//			if((batched_mesh->numVerts() > 1024) && // If this is a med/large mesh
								//				((float)lod1_mesh->numVerts() > (batched_mesh->numVerts() / 4.f))) // If we acheived less than a 4x reduction in the number of vertices, try again with sloppy simplification
								//			{
								//				conPrint("Mesh '" + lod_URL + "' was not simplified enough, recomputing LOD 1 mesh...");
								//
								//				// Generate the model
								//				LODMeshToGen mesh_to_gen;
								//				mesh_to_gen.lod_level = lvl;
								//				mesh_to_gen.model_abs_path = model_abs_path;
								//				mesh_to_gen.LOD_model_abs_path = lod_abs_path;
								//				mesh_to_gen.lod_URL = lod_URL;
								//				mesh_to_gen.owner_id = world_state->resource_manager->getExistingResourceForURL(ob->model_url)->owner_id;
								//				meshes_to_gen.push_back(mesh_to_gen);
								//			}
								//		}
								//		catch(glare::Exception& e)
								//		{
								//			conPrint("Error while trying to load LOD 1 mesh: " + e.what());
								//		}
								//	}
								//}
							}
						}
					}
				}
			}
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("MeshLODGenThread: glare::Exception: " + e.what());
	}
	catch(std::exception& e) // catch std::bad_alloc etc..
	{
		conPrint(std::string("MeshLODGenThread: Caught std::exception: ") + e.what());
	}
}


//static size_t sum_base_size_B = 0;
//static size_t sum_optimised_size_B = 0;


static void checkForOptimisedMeshesToGenerate(ServerAllWorldsState* world_state, ServerWorldState* world, WorldObject* ob, std::unordered_set<std::string>& lod_URLs_considered, std::vector<LODMeshToGen>& meshes_to_gen)
{
	try
	{
		if(ob->object_type == WorldObject::ObjectType_Generic)
		{
			if(!ob->model_url.empty())
			{
				//if(StringUtils::containsString(ob->model_url, "bad_apple_gds"))
				//	return; // TEMP HACK DON'T PROCESS SLOW MESH

				ResourceRef base_resource = world_state->resource_manager->getExistingResourceForURL(ob->model_url);
				if(base_resource && base_resource->isPresent()) // Base resource needs to be fully present before we start processing it.
				{
					const std::string base_model_abs_path = world_state->resource_manager->getLocalAbsPathForResource(*base_resource);

					for(int lvl = 0; lvl <= myMin(2, ob->max_model_lod_level); ++lvl)
					{
						WorldObject::GetLODModelURLOptions options(/*get_optimised_mesh=*/true, Protocol::OPTIMISED_MESH_VERSION);

						const std::string lod_abs_path = WorldObject::getLODModelURLForLevel(base_model_abs_path, lvl, options);
						const std::string lod_URL  = WorldObject::getLODModelURLForLevel(ob->model_url, lvl, options);

						if(lod_URLs_considered.count(lod_URL) == 0)
						{
							lod_URLs_considered.insert(lod_URL);

							if(!world_state->resource_manager->isFileForURLPresent(lod_URL))
							{
								// Add to list of models to generate
								LODMeshToGen mesh_to_gen;
								mesh_to_gen.lod_level = lvl;
								mesh_to_gen.model_abs_path = base_model_abs_path;
								mesh_to_gen.LOD_model_abs_path = lod_abs_path;
								mesh_to_gen.lod_URL = lod_URL;
								mesh_to_gen.owner_id = base_resource->owner_id;
								mesh_to_gen.build_optimised_mesh = true;
								meshes_to_gen.push_back(mesh_to_gen);
							}
							else
							{
								/*if(FileUtils::fileExists(base_model_abs_path) && FileUtils::fileExists(lod_abs_path))
								{
									//conPrint("base model size: " + toString(FileUtils::getFileSize(base_model_abs_path)));
									sum_base_size_B += FileUtils::getFileSize(base_model_abs_path);
							
									//conPrint("optimised model size: " + toString(FileUtils::getFileSize(lod_abs_path)));
									sum_optimised_size_B += FileUtils::getFileSize(lod_abs_path);
								}*/
							}
						}
					}
				}
			}
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("MeshLODGenThread: glare::Exception: " + e.what());
	}
	catch(std::exception& e) // catch std::bad_alloc etc..
	{
		conPrint(std::string("MeshLODGenThread: Caught std::exception: ") + e.what());
	}
}


static void checkForOptimisedMeshToGenerateForURL(const std::string& URL, ResourceManager* resource_manager, std::unordered_set<std::string>& lod_URLs_considered, std::vector<LODMeshToGen>& meshes_to_gen)
{
	try
	{
		if(hasExtension(URL, "bmesh")) // Just support building optimised meshes for bmeshes for now.  All upload meshes should be bmeshes anyway.
		{
			ResourceRef base_resource = resource_manager->getExistingResourceForURL(URL);
			if(base_resource && base_resource->isPresent())
			{
				const std::string base_model_abs_path = resource_manager->getLocalAbsPathForResource(*base_resource);

				const int lvl = 0;

				WorldObject::GetLODModelURLOptions options(/*get_optimised_mesh=*/true, Protocol::OPTIMISED_MESH_VERSION);

				const std::string lod_abs_path = WorldObject::getLODModelURLForLevel(base_model_abs_path, lvl, options);
				const std::string lod_URL      = WorldObject::getLODModelURLForLevel(URL, lvl, options);

				if(lod_URLs_considered.count(lod_URL) == 0)
				{
					lod_URLs_considered.insert(lod_URL);

					if(!resource_manager->isFileForURLPresent(lod_URL))
					{
						// Add to list of models to generate
						LODMeshToGen mesh_to_gen;
						mesh_to_gen.lod_level = lvl;
						mesh_to_gen.model_abs_path = base_model_abs_path;
						mesh_to_gen.LOD_model_abs_path = lod_abs_path;
						mesh_to_gen.lod_URL = lod_URL;
						mesh_to_gen.owner_id = base_resource->owner_id;
						mesh_to_gen.build_optimised_mesh = true;
						meshes_to_gen.push_back(mesh_to_gen);
					}
				}
			}
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("MeshLODGenThread: glare::Exception: " + e.what());
	}
	catch(std::exception& e) // catch std::bad_alloc etc..
	{
		conPrint(std::string("MeshLODGenThread: Caught std::exception: ") + e.what());
	}
}


static void checkMaterialFlags(ServerAllWorldsState* world_state, ServerWorldState* world, WorldObject* ob, std::map<std::string, MeshLODGenThreadTexInfo>& tex_info)
{
	for(size_t z=0; z<ob->materials.size(); ++z)
	{
		WorldMaterial* mat = ob->materials[z].ptr();
		if(mat)
		{
			// Check WorldMaterial::MIN_LOD_LEVEL_IS_NEGATIVE_1 and WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG flags for the material are correct, set if not.
			if(!mat->colour_texture_url.empty())
			{
				ResourceRef base_resource = world_state->resource_manager->getExistingResourceForURL(mat->colour_texture_url);
				if(base_resource && base_resource->isPresent()) // Base resource needs to be fully present before we start processing it.
				{
					const std::string tex_abs_path = world_state->resource_manager->getLocalAbsPathForResource(*base_resource);

					if(hasExtension(tex_abs_path, "mp4"))
					{
						// Don't generate LOD for mp4 currently.
					}
					else
					{
						try
						{
							// Compute has_alpha and is_high_res for the texture if we haven't already.
							auto res = tex_info.find(tex_abs_path);
							if(res == tex_info.end())
							{
								Reference<Map2D> map = ImageDecoding::decodeImage(".", tex_abs_path); // Load texture from disk and decode it.
								const bool is_hi_res = map->getMapWidth() > 1024 || map->getMapHeight() > 1024;
								const bool has_alpha = textureHasAlphaChannel(map);

								tex_info[tex_abs_path].has_alpha = has_alpha;
								tex_info[tex_abs_path].is_hi_res = is_hi_res;
							}

							// If the texture is very high res, set minimum texture lod level to -1.  Lod level 0 will be the texture resized to 1024x1024 or below.

							const bool is_high_res = tex_info[tex_abs_path].is_hi_res;
							const bool has_alpha   = tex_info[tex_abs_path].has_alpha;

							// conPrint("tex " + tex_path + " is_hi_res: " + boolToString(is_high_res));

							const uint32 old_flags = mat->flags;
							BitUtils::setOrZeroBit(mat->flags, WorldMaterial::MIN_LOD_LEVEL_IS_NEGATIVE_1, is_high_res);
							BitUtils::setOrZeroBit(mat->flags, WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG,   has_alpha);

							if(mat->flags != old_flags)
							{
								{
									WorldStateLock lock(world_state->mutex);
									world->addWorldObjectAsDBDirty(ob, lock);
								}
								conPrint("Updated mat flags: (for mat with tex " + tex_abs_path + "): is_hi_res: " + boolToString(is_high_res));
							}
						}
						catch(glare::Exception& e)
						{
							conPrint("\tExcep while loading texture: " + e.what());
						}
					}
				}
			}
		}
	}
}


// Make tasks for generating LOD level textures.
static void checkForLODTexturesToGenerate(ServerAllWorldsState* world_state, ServerWorldState* world, WorldObject* ob, std::unordered_set<std::string>& lod_URLs_considered, //std::map<std::string, MeshLODGenThreadTexInfo>& tex_info,
	std::vector<LODTextureToGen>& textures_to_gen)
{
	for(size_t z=0; z<ob->materials.size(); ++z)
	{
		const WorldMaterial* mat = ob->materials[z].ptr();

		const int start_lod_level = mat->minLODLevel() + 1;
		for(int lvl = start_lod_level; lvl <= 2; ++lvl)
		{
			std::vector<std::string> URLs;
			URLs.push_back(mat->colour_texture_url);
			URLs.push_back(mat->emission_texture_url);
			URLs.push_back(mat->roughness.texture_url);
			URLs.push_back(mat->normal_map_url);

			for(size_t q=0; q<URLs.size(); ++q)
			{
				const std::string texture_URL = URLs[q];
				if(!texture_URL.empty() && !hasExtension(texture_URL, "mp4")) // Don't generate LOD for mp4.
				{
					ResourceRef base_resource = world_state->resource_manager->getExistingResourceForURL(texture_URL);
					if(base_resource && base_resource->isPresent()) // Base resource needs to be fully present before we start processing it.
					{
						const std::string tex_abs_path = world_state->resource_manager->getLocalAbsPathForResource(*base_resource);

						bool has_alpha = false;
						if(texture_URL == mat->colour_texture_url)
							has_alpha = BitUtils::isBitSet(mat->flags, WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG); // Assume mat->flags are correct.

						const std::string lod_URL = mat->getLODTextureURLForLevel(texture_URL, lvl, has_alpha, /*use basis=*/false);

						if(lod_URL != texture_URL) // We don't do LOD for some texture types.
						{
							if(lod_URLs_considered.count(lod_URL) == 0)
							{
								lod_URLs_considered.insert(lod_URL);

								if(!world_state->resource_manager->isFileForURLPresent(lod_URL))
								{
									const std::string lod_abs_path = world_state->resource_manager->pathForURL(lod_URL);

									// Generate the texture
									LODTextureToGen tex_to_gen;
									tex_to_gen.lod_level = lvl;
									tex_to_gen.source_tex_abs_path = tex_abs_path;
									tex_to_gen.LOD_tex_abs_path = lod_abs_path;
									tex_to_gen.lod_URL = lod_URL;
									tex_to_gen.owner_id = base_resource->owner_id;
									textures_to_gen.push_back(tex_to_gen);
								}
							}
						}
					}
				}
			}
		}
	}
}


// Make tasks for generating Basis level textures.
static void checkForBasisTexturesToGenerateForMaterials(ServerAllWorldsState* world_state, const std::vector<WorldMaterialRef>& materials, std::unordered_set<std::string>& lod_URLs_considered,
	std::vector<BasisTextureToGen>& basis_textures_to_gen)
{
	for(size_t z=0; z<materials.size(); ++z)
	{
		const WorldMaterial* mat = materials[z].ptr();

		std::vector<std::string> URLs;
		URLs.push_back(mat->colour_texture_url);
		URLs.push_back(mat->emission_texture_url);
		URLs.push_back(mat->roughness.texture_url);
		URLs.push_back(mat->normal_map_url);

		for(size_t q=0; q<URLs.size(); ++q)
		{
			const std::string texture_URL = URLs[q];

			if(!texture_URL.empty() && !hasExtension(texture_URL, "mp4")) // Don't generate basis textures for mp4s
			{
				ResourceRef base_resource = world_state->resource_manager->getExistingResourceForURL(texture_URL);
				if(base_resource && base_resource->isPresent()) // Base resource needs to be fully present before we start processing it.
				{
					const std::string tex_abs_path = world_state->resource_manager->getLocalAbsPathForResource(*base_resource);

					for(int lvl = mat->minLODLevel(); lvl <= 2; ++lvl)
					{
						const std::string basis_lod_URL = mat->getLODTextureURLForLevel(texture_URL, lvl, /*has_alpha=*/false, /*use basis=-*/true);  // Lod URL without ktx extension (jpg or PNG)
						if(hasExtension(basis_lod_URL, "basis"))
						{
							if(lod_URLs_considered.count(basis_lod_URL) == 0)
							{
								lod_URLs_considered.insert(basis_lod_URL);

								if(!world_state->resource_manager->isFileForURLPresent(basis_lod_URL))
								{
									const std::string basis_abs_path = world_state->resource_manager->pathForURL(basis_lod_URL);

									// Add to list of textures to generate
									BasisTextureToGen tex_to_gen;
									tex_to_gen.source_tex_abs_path = tex_abs_path; // source texture abs path
									tex_to_gen.basis_tex_abs_path = basis_abs_path; // abs path to write Basis texture to.
									tex_to_gen.basis_URL = basis_lod_URL;
									tex_to_gen.base_lod_level = mat->minLODLevel();
									tex_to_gen.lod_level = lvl;
									tex_to_gen.owner_id = base_resource->owner_id;
									basis_textures_to_gen.push_back(tex_to_gen);
								}
							}
						}
					}
				}
			}
		}
	}
}


// Make tasks for generating Basis level textures.
static void checkForBasisTexturesToGenerateForOb(ServerAllWorldsState* world_state, WorldObject* ob, std::unordered_set<std::string>& lod_URLs_considered,
	std::vector<BasisTextureToGen>& basis_textures_to_gen)
{
	checkForBasisTexturesToGenerateForMaterials(world_state, /*world, */ob->materials, lod_URLs_considered, basis_textures_to_gen);
}


// Make tasks for generating Basis level textures.
static void checkForBasisTexturesToGenerateForURL(const std::string& URL, ResourceManager* resource_manager, std::unordered_set<std::string>& lod_URLs_considered,
	std::vector<BasisTextureToGen>& basis_textures_to_gen)
{
	const std::string base_texture_URL = URL;

	if(ImageDecoding::hasSupportedImageExtension(URL) && !hasExtension(base_texture_URL, "mp4")) // Don't generate basis textures for mp4s
	{
		ResourceRef base_resource = resource_manager->getExistingResourceForURL(base_texture_URL);
		if(base_resource && base_resource->isPresent()) // Base resource needs to be fully present before we start processing it.
		{
			const std::string tex_abs_path = resource_manager->getLocalAbsPathForResource(*base_resource);

			for(int lvl = 0; lvl <= 2; ++lvl)
			{
				const std::string basis_lod_URL = removeDotAndExtension(base_texture_URL) + ((lvl > 0) ? ("_lod" + toString(lvl)) : std::string()) + ".basis";
				if(lod_URLs_considered.count(basis_lod_URL) == 0)
				{
					lod_URLs_considered.insert(basis_lod_URL);

					if(!resource_manager->isFileForURLPresent(basis_lod_URL))
					{
						const std::string basis_abs_path = resource_manager->pathForURL(basis_lod_URL);

						// Add to list of textures to generate
						BasisTextureToGen tex_to_gen;
						tex_to_gen.source_tex_abs_path = tex_abs_path; // source texture abs path
						tex_to_gen.basis_tex_abs_path = basis_abs_path; // abs path to write Basis texture to.
						tex_to_gen.basis_URL = basis_lod_URL;
						tex_to_gen.base_lod_level = 0;
						tex_to_gen.lod_level = lvl;
						tex_to_gen.owner_id = base_resource->owner_id;
						basis_textures_to_gen.push_back(tex_to_gen);
					}
				}
			}
		}
	}
}


#if 0
static void markOptimisedMeshesAsNotPresentForOb(ServerAllWorldsState* world_state, ServerWorldState* world, WorldObject* ob)
{
	try
	{
		if(ob->object_type == WorldObject::ObjectType_Generic)
		{
			if(!ob->model_url.empty())
			{
				for(int lvl = 0; lvl <= myMin(2, ob->max_model_lod_level); ++lvl)
				{
					WorldObject::GetLODModelURLOptions options(/*get_optimised_mesh=*/true, Protocol::OPTIMISED_MESH_VERSION);

					const std::string lod_URL = WorldObject::getLODModelURLForLevel(ob->model_url, lvl, options);

					ResourceRef resource = world_state->resource_manager->getExistingResourceForURL(lod_URL);
					if(resource && resource->isPresent())
						resource->setState(Resource::State_NotPresent);
				}
			}
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("MeshLODGenThread: glare::Exception: " + e.what());
	}
	catch(std::exception& e) // catch std::bad_alloc etc..
	{
		conPrint(std::string("MeshLODGenThread: Caught std::exception: ") + e.what());
	}
}


static void markOptimisedMeshesAsNotPresent(ServerAllWorldsState* world_state)
{
	conPrint("================================ markOptimisedMeshesAsNotPresent() ================================");
	WorldStateLock lock(world_state->mutex);

	for(auto world_it = world_state->world_states.begin(); world_it != world_state->world_states.end(); ++world_it)
	{
		ServerWorldState* world = world_it->second.ptr();
		ServerWorldState::ObjectMapType& objects = world->getObjects(lock);
		for(auto it = objects.begin(); it != objects.end(); ++it)
		{
			WorldObject* ob = it->second.ptr();
			try
			{
				markOptimisedMeshesAsNotPresentForOb(world_state, world, ob);
			}
			catch(glare::Exception& e)
			{
				conPrint("\tMeshLODGenThread: exception while processing object: " + e.what());
			}
		}
	}

	// Check user avatars
	for(auto it = world_state->user_id_to_users.begin(); it != world_state->user_id_to_users.end(); ++it)
	{
		const User* user = it->second.ptr();
		if(!user->avatar_settings.model_url.empty())
		{
			ResourceRef resource = world_state->resource_manager->getExistingResourceForURL(user->avatar_settings.model_url);
			if(resource && resource->isPresent())
				resource->setState(Resource::State_NotPresent);
		}
	}
}
#endif


void MeshLODGenThread::doRun()
{
	PlatformUtils::setCurrentThreadName("MeshLODGenThread");

	glare::TaskManager task_manager("MeshLODGenThread task manager");

	// When this thread starts, we will do a full scan over all objects.
	// After that we will wait for CheckGenResourcesForObject messages, which instructs this thread to just scan a single object.
	bool do_initial_full_scan = true;

	try
	{
		js::Vector<ThreadMessageRef> messages;

		while(1)
		{
			std::set<UID> obs_to_scan_UIDs;
			std::set<std::string> URLs_to_check;
			if(!do_initial_full_scan)
			{
				// Block until we have one or more messages.
				// Dequeue as many messages as we can, since we want to generate meshes before textures (so we can show something asap).
				getMessageQueue().dequeueAllQueuedItemsBlocking(messages);

				for(size_t i=0; i<messages.size(); ++i)
				{
					ThreadMessageRef msg = messages[i];

					if(dynamic_cast<CheckGenResourcesForObject*>(msg.ptr()))
					{
						const CheckGenResourcesForObject* check_gen_msg = static_cast<CheckGenResourcesForObject*>(msg.ptr());
						obs_to_scan_UIDs.insert(check_gen_msg->ob_uid);

						conPrint("MeshLODGenThread: Received message to scan object with UID " + check_gen_msg->ob_uid.toString());
					}
					else if(CheckGenLodResourcesForURL* check_gen_msg = dynamic_cast<CheckGenLodResourcesForURL*>(msg.ptr()))
					{
						// Textures used by e.g. an avatar need to have .basis versions generated.
						URLs_to_check.insert(check_gen_msg->URL);

						conPrint("MeshLODGenThread: Received message to scan URL " + check_gen_msg->URL);
					}
					else if(dynamic_cast<KillThreadMessage*>(msg.ptr()))
					{
						return;
					}
				}
			}

			// Iterate over objects.
			// Set object world space AABB.
			// Set object max_lod_level if it is a generic model or a voxel model.
			// Compute list of LOD meshes we need to generate.
			std::vector<LODMeshToGen> meshes_to_gen;
			std::vector<LODTextureToGen> lod_textures_to_gen;
			std::vector<BasisTextureToGen> basis_textures_to_gen;
			std::unordered_set<std::string> lod_URLs_considered;
			std::map<std::string, MeshLODGenThreadTexInfo> tex_info; // Cached info about textures

			// conPrint("MeshLODGenThread: Iterating over world object(s)...");
			Timer timer;
			
			{
				WorldStateLock lock(world_state->mutex);

				// markOptimisedMeshesAsNotPresent(world_state);

				if(do_initial_full_scan)
				{
					for(auto world_it = world_state->world_states.begin(); world_it != world_state->world_states.end(); ++world_it)
					{
						ServerWorldState* world = world_it->second.ptr();
						ServerWorldState::ObjectMapType& objects = world->getObjects(lock);
						for(auto it = objects.begin(); it != objects.end(); ++it)
						{
							WorldObject* ob = it->second.ptr();
							try
							{
								if(false)
									checkObjectSpaceAABB(world_state, world, ob);

								if(false)
									checkMaterialFlags(world_state, world, ob, tex_info);

								checkForLODMeshesToGenerate(world_state, world, ob, lod_URLs_considered, meshes_to_gen);
								checkForOptimisedMeshesToGenerate(world_state, world, ob, lod_URLs_considered, meshes_to_gen);
								checkForLODTexturesToGenerate(world_state, world, ob, lod_URLs_considered, lod_textures_to_gen);
								checkForBasisTexturesToGenerateForOb(world_state, ob, lod_URLs_considered, basis_textures_to_gen);
							}
							catch(glare::Exception& e)
							{
								conPrint("\tMeshLODGenThread: exception while processing object: " + e.what());
							}
						}

						// Check world settings textures
						for(int i=0; i<4; ++i)
						{
							const std::string detail_col_map_URL = world->world_settings.terrain_spec.detail_col_map_URLs[i];
							checkForBasisTexturesToGenerateForURL(detail_col_map_URL, world_state->resource_manager.ptr(), lod_URLs_considered, basis_textures_to_gen);

							const std::string detail_height_map_URL = world->world_settings.terrain_spec.detail_height_map_URLs[i];
							checkForBasisTexturesToGenerateForURL(detail_height_map_URL, world_state->resource_manager.ptr(), lod_URLs_considered, basis_textures_to_gen);
						}
					}

					// Check user avatars
					for(auto it = world_state->user_id_to_users.begin(); it != world_state->user_id_to_users.end(); ++it)
					{
						const User* user = it->second.ptr();
						checkForOptimisedMeshToGenerateForURL(user->avatar_settings.model_url, world_state->resource_manager.ptr(), lod_URLs_considered, meshes_to_gen);

						checkForBasisTexturesToGenerateForMaterials(world_state, user->avatar_settings.materials, lod_URLs_considered, basis_textures_to_gen);
					}

					do_initial_full_scan = false;
				}
				else
				{
					for(auto it = obs_to_scan_UIDs.begin(); it != obs_to_scan_UIDs.end(); ++it)
					{
						const UID ob_to_scan_UID = *it;
						// Look up object for UID
						for(auto world_it = world_state->world_states.begin(); world_it != world_state->world_states.end(); ++world_it)
						{
							ServerWorldState* world = world_it->second.ptr();
							auto res = world->getObjects(lock).find(ob_to_scan_UID);
							if(res != world->getObjects(lock).end())
							{
								WorldObject* ob = res->second.ptr();
								try
								{
									checkForLODMeshesToGenerate(world_state, world, ob, lod_URLs_considered, meshes_to_gen);
									checkForOptimisedMeshesToGenerate(world_state, world, ob, lod_URLs_considered, meshes_to_gen);
									checkForLODTexturesToGenerate(world_state, world, ob, lod_URLs_considered, lod_textures_to_gen);
									checkForBasisTexturesToGenerateForOb(world_state, ob, lod_URLs_considered, basis_textures_to_gen);
								}
								catch(glare::Exception& e)
								{
									conPrint("\tMeshLODGenThread: exception while processing object: " + e.what());
								}
							}
						}
					}

					for(auto it = URLs_to_check.begin(); it != URLs_to_check.end(); ++it)
					{
						const std::string URL_to_check = *it;
						checkForBasisTexturesToGenerateForURL(URL_to_check, world_state->resource_manager.ptr(), lod_URLs_considered, basis_textures_to_gen);
						checkForOptimisedMeshToGenerateForURL(URL_to_check, world_state->resource_manager.ptr(), lod_URLs_considered, meshes_to_gen);
					}
				}
			} // End lock scope

			if(!meshes_to_gen.empty() || !lod_textures_to_gen.empty() || !basis_textures_to_gen.empty())
				conPrint("MeshLODGenThread: Iterating over objects took " + timer.elapsedStringNSigFigs(4) + ", meshes_to_gen: " + toString(meshes_to_gen.size()) + ", lod_textures_to_gen: " + toString(lod_textures_to_gen.size()) + 
					", basis_textures_to_gen: " + toString(basis_textures_to_gen.size()));


			//-------------------------------------------  Generate each mesh, without holding the world lock -------------------------------------------
			if(!meshes_to_gen.empty())
			{
				conPrint("MeshLODGenThread: Generating LOD meshes...");
				timer.reset();

				for(size_t i=0; i<meshes_to_gen.size(); ++i)
				{
					const LODMeshToGen& mesh_to_gen = meshes_to_gen[i];
					try
					{
						conPrint("MeshLODGenThread: (mesh " + toString(i) + " / " + toString(meshes_to_gen.size()) + "): Generating " + (mesh_to_gen.build_optimised_mesh ? "optimised" : "LOD") + " mesh with URL " + mesh_to_gen.lod_URL);

						if(mesh_to_gen.build_optimised_mesh) // If building optimised mesh (may be LOD mesh also):
						{
							LODGeneration::generateOptimisedMesh(mesh_to_gen.model_abs_path, mesh_to_gen.lod_level, mesh_to_gen.LOD_model_abs_path);

							conPrint("\tMeshLODGenThread: done generating mesh.");
						}
						else // Else if building (unoptimised) LOD mesh:
						{
							LODGeneration::generateLODModel(mesh_to_gen.model_abs_path, mesh_to_gen.lod_level, mesh_to_gen.LOD_model_abs_path);
						}

						// Now that we have generated the LOD model, add it to resources.
						{ // lock scope
							Lock lock(world_state->mutex);

							const std::string raw_path = FileUtils::getFilename(mesh_to_gen.LOD_model_abs_path); // NOTE: assuming we can get raw/relative path from abs path like this.

							ResourceRef resource = new Resource(
								mesh_to_gen.lod_URL, // URL
								raw_path, // raw local path
								Resource::State_Present, // state
								mesh_to_gen.owner_id,
								/*external_resource=*/false
							);

							world_state->addResourceAsDBDirty(resource);
							world_state->resource_manager->addResource(resource);
						
						} // End lock scope

						server->enqueueMsg(new NewResourceGenerated(mesh_to_gen.lod_URL));
					}
					catch(glare::Exception& e)
					{
						conPrint("\tMeshLODGenThread: glare::Exception while generating LOD model for URL '" + mesh_to_gen.lod_URL + "': " + e.what());
					}

					if(should_quit)
						return;
				}

				conPrint("MeshLODGenThread: Done generating LOD meshes. (Elapsed: " + timer.elapsedStringNSigFigs(4) + ")");
			}


			//------------------------------------------- Generate each texture, without holding the world lock -------------------------------------------
			if(!lod_textures_to_gen.empty())
			{
				conPrint("MeshLODGenThread: Generating LOD textures...");
				timer.reset();

				for(size_t i=0; i<lod_textures_to_gen.size(); ++i)
				{
					const LODTextureToGen& tex_to_gen = lod_textures_to_gen[i];
					try
					{
						conPrint("MeshLODGenThread:  (LOD tex " + toString(i) + " / " + toString(lod_textures_to_gen.size()) + "): Generating LOD texture with URL " + tex_to_gen.lod_URL);

						LODGeneration::generateLODTexture(tex_to_gen.source_tex_abs_path, tex_to_gen.lod_level, tex_to_gen.LOD_tex_abs_path, task_manager);

						// Now that we have generated the LOD model, add it to resources.
						{ // lock scope
							Lock lock(world_state->mutex);

							const std::string raw_path = FileUtils::getFilename(tex_to_gen.LOD_tex_abs_path); // NOTE: assuming we can get raw/relative path from abs path like this.

							ResourceRef resource = new Resource(
								tex_to_gen.lod_URL, // URL
								raw_path, // raw local path
								Resource::State_Present, // state
								tex_to_gen.owner_id,
								/*external_resource=*/false
							);

							world_state->addResourceAsDBDirty(resource);
							world_state->resource_manager->addResource(resource);

						} // End lock scope

						server->enqueueMsg(new NewResourceGenerated(tex_to_gen.lod_URL));
					}
					catch(glare::Exception& e)
					{
						conPrint("\tMeshLODGenThread: excep while generating LOD texture: " + e.what());
					}

					if(should_quit)
						return;
				}

				conPrint("MeshLODGenThread: Done generating LOD textures. (Elapsed: " + timer.elapsedStringNSigFigs(4));
			}

			//------------------------------------------- Generate each Basis texture, without holding the world lock -------------------------------------------
			if(!basis_textures_to_gen.empty())
			{
				conPrint("MeshLODGenThread: Generating Basis textures...");
				timer.reset();

				for(size_t i=0; i<basis_textures_to_gen.size(); ++i)
				{
					const BasisTextureToGen& tex_to_gen = basis_textures_to_gen[i];
					try
					{
						conPrint("MeshLODGenThread: (basis " + toString(i) + " / " + toString(basis_textures_to_gen.size()) + "): Generating basis texture with URL " + tex_to_gen.basis_URL);

						LODGeneration::generateBasisTexture(tex_to_gen.source_tex_abs_path, 
							tex_to_gen.base_lod_level, tex_to_gen.lod_level, tex_to_gen.basis_tex_abs_path, 
							task_manager);

						// Now that we have generated the LOD model, add it to resources.
						{ // lock scope
							Lock lock(world_state->mutex);

							const std::string raw_path = FileUtils::getFilename(tex_to_gen.basis_tex_abs_path); // NOTE: assuming we can get raw/relative path from abs path like this.

							ResourceRef resource = new Resource(
								tex_to_gen.basis_URL, // URL
								raw_path, // raw local path
								Resource::State_Present, // state
								tex_to_gen.owner_id,
								/*external resource=*/false
							);

							world_state->addResourceAsDBDirty(resource);
							world_state->resource_manager->addResource(resource);

						} // End lock scope

						server->enqueueMsg(new NewResourceGenerated(tex_to_gen.basis_URL));
					}
					catch(glare::Exception& e)
					{
						conPrint("\tMeshLODGenThread: excep while generating Basis texture: " + e.what());
					}

					if(should_quit)
						return;
				}

				conPrint("MeshLODGenThread: Done generating Basis textures. (Elapsed: " + timer.elapsedStringNSigFigs(4) + ")");
			}
			//------------------------------------------- End Generate each KTX texture  -------------------------------------------
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("MeshLODGenThread: glare::Exception: " + e.what());
	}
	catch(std::exception& e) // catch std::bad_alloc etc..
	{
		conPrint(std::string("MeshLODGenThread: Caught std::exception: ") + e.what());
	}
}

void MeshLODGenThread::kill()
{
	should_quit = 1;
}
