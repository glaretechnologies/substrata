/*=====================================================================
MeshLODGenThread.cpp
--------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "MeshLODGenThread.h"


#include "ServerWorldState.h"
#include "../shared/LODGeneration.h"
#include "../shared/ImageDecoding.h"
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


MeshLODGenThread::MeshLODGenThread(ServerAllWorldsState* world_state_)
:	world_state(world_state_)
{
}


MeshLODGenThread::~MeshLODGenThread()
{
}


static bool textureHasAlphaChannel(Reference<Map2D>& map)
{
	return map->hasAlphaChannel() && !map->isAlphaChannelAllWhite();
}


struct LODMeshToGen
{
	std::string model_abs_path;
	std::string LOD_model_abs_path;
	std::string lod_URL;
	int lod_level;
	UserID owner_id;
};


struct LODTextureToGen
{
	std::string source_tex_abs_path; // Absolute base texture path, to read texture from.
	std::string LOD_tex_abs_path; // LOD texture path, to write LOD texture to.
	std::string lod_URL;
	int lod_level;
	UserID owner_id;
};


struct KTXTextureToGen
{
	std::string source_tex_abs_path; // source texture abs path
	std::string ktx_tex_abs_path; // abs path to write KTX texture to.
	std::string ktx_URL;
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
				VoxelGroup voxel_group;
				WorldObject::decompressVoxelGroup(ob->getCompressedVoxels().data(), ob->getCompressedVoxels().size(), /*mem allocator=*/NULL, voxel_group);
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
				const std::string model_abs_path = world_state->resource_manager->pathForURL(ob->model_url);

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
						const std::string lod_abs_path = WorldObject::getLODModelURLForLevel(model_abs_path, lvl);
						const std::string lod_URL  = WorldObject::getLODModelURLForLevel(ob->model_url, lvl);

						if(lod_URLs_considered.count(lod_URL) == 0)
						{
							lod_URLs_considered.insert(lod_URL);

							if(!world_state->resource_manager->isFileForURLPresent(lod_URL))
							{
								// Add to list of models to generate
								LODMeshToGen mesh_to_gen;
								mesh_to_gen.lod_level = lvl;
								mesh_to_gen.model_abs_path = model_abs_path;
								mesh_to_gen.LOD_model_abs_path = lod_abs_path;
								mesh_to_gen.lod_URL = lod_URL;
								mesh_to_gen.owner_id = world_state->resource_manager->getExistingResourceForURL(ob->model_url)->owner_id;
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

						const std::string lod_URL = mat->getLODTextureURLForLevel(texture_URL, lvl, has_alpha);

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


// Make tasks for generating KTX level textures.
static void checkForKTXTexturesToGenerate(ServerAllWorldsState* world_state, ServerWorldState* world, WorldObject* ob, std::unordered_set<std::string>& lod_URLs_considered,
	std::vector<KTXTextureToGen>& ktx_textures_to_gen)
{
	for(size_t z=0; z<ob->materials.size(); ++z)
	{
		const WorldMaterial* mat = ob->materials[z].ptr();

		for(int lvl = mat->minLODLevel(); lvl <= 2; ++lvl)
		{
			std::vector<std::string> URLs;
			URLs.push_back(mat->colour_texture_url);
			URLs.push_back(mat->emission_texture_url);
			URLs.push_back(mat->roughness.texture_url);
			URLs.push_back(mat->normal_map_url);

			for(size_t q=0; q<URLs.size(); ++q)
			{
				const std::string texture_URL = URLs[q];
				if(!texture_URL.empty() && !hasExtension(texture_URL, "mp4") && !hasExtension(texture_URL, "gif")) // Don't generate KTX textures for mp4s or gifs
				{
					ResourceRef base_resource = world_state->resource_manager->getExistingResourceForURL(texture_URL);
					if(base_resource && base_resource->isPresent()) // Base resource needs to be fully present before we start processing it.
					{
						const std::string lod_URL = mat->getLODTextureURLForLevel(texture_URL, lvl, /*has_alpha=*/false);  // Lod URL without ktx extension (jpg or PNG)
						if(!hasExtension(lod_URL, "ktx2"))
						{
							const std::string ktx_lod_URL = ::eatExtension(lod_URL) + "ktx2";

							if(lod_URLs_considered.count(ktx_lod_URL) == 0)
							{
								lod_URLs_considered.insert(ktx_lod_URL);

								if(!world_state->resource_manager->isFileForURLPresent(ktx_lod_URL))
								{
									const std::string lod_abs_path = world_state->resource_manager->pathForURL(lod_URL);
									const std::string ktx_abs_path = world_state->resource_manager->pathForURL(ktx_lod_URL);

									// Generate the texture
									KTXTextureToGen tex_to_gen;
									tex_to_gen.source_tex_abs_path = lod_abs_path; // source texture abs path
									tex_to_gen.ktx_tex_abs_path = ktx_abs_path; // abs path to write KTX texture to.
									tex_to_gen.ktx_URL = ktx_lod_URL;
									tex_to_gen.base_lod_level = mat->minLODLevel();
									tex_to_gen.lod_level = lvl;
									tex_to_gen.owner_id = base_resource->owner_id;
									ktx_textures_to_gen.push_back(tex_to_gen);
								}
							}
						}
					}
				}
			}
		}
	}
}


void MeshLODGenThread::doRun()
{
	PlatformUtils::setCurrentThreadName("MeshLODGenThread");

	glare::TaskManager task_manager("MeshLODGenThread task manager");

	// When this thread starts, we will do a full scan over all objects.
	// After that we will wait for CheckGenResourcesForObject messages, which instructs this thread to just scan a single object.
	bool do_initial_full_scan = true;

	try
	{
		while(1)
		{
			UID ob_to_scan_UID = UID::invalidUID();
			if(!do_initial_full_scan)
			{
				// Block until we have a message
				ThreadMessageRef msg;
				getMessageQueue().dequeue(msg);

				if(dynamic_cast<CheckGenResourcesForObject*>(msg.ptr()))
				{
					const CheckGenResourcesForObject* check_gen_msg = static_cast<CheckGenResourcesForObject*>(msg.ptr());
					ob_to_scan_UID = check_gen_msg->ob_uid;

					conPrint("MeshLODGenThread: Received message to scan object with UID " + ob_to_scan_UID.toString());
				}
				else if(dynamic_cast<KillThreadMessage*>(msg.ptr()))
				{
					return;
				}
			}

			// Iterate over objects.
			// Set object world space AABB.
			// Set object max_lod_level if it is a generic model or a voxel model.
			// Compute list of LOD meshes we need to generate.
			std::vector<LODMeshToGen> meshes_to_gen;
			std::vector<LODTextureToGen> lod_textures_to_gen;
			std::vector<KTXTextureToGen> ktx_textures_to_gen;
			std::unordered_set<std::string> lod_URLs_considered;
			std::map<std::string, MeshLODGenThreadTexInfo> tex_info; // Cached info about textures

			conPrint("MeshLODGenThread: Iterating over world object(s)...");
			Timer timer;
			
			{
				WorldStateLock lock(world_state->mutex);

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
								if(true)
									checkObjectSpaceAABB(world_state, world, ob);

								if(false)
									checkMaterialFlags(world_state, world, ob, tex_info);

								checkForLODMeshesToGenerate(world_state, world, ob, lod_URLs_considered, meshes_to_gen);
								checkForLODTexturesToGenerate(world_state, world, ob, lod_URLs_considered, lod_textures_to_gen);
								checkForKTXTexturesToGenerate(world_state, world, ob, lod_URLs_considered, ktx_textures_to_gen);
							}
							catch(glare::Exception& e)
							{
								conPrint("\tMeshLODGenThread: exception while processing object: " + e.what());
							}
						}
					}
					do_initial_full_scan = false;
				}
				else
				{
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
								checkForLODTexturesToGenerate(world_state, world, ob, lod_URLs_considered, lod_textures_to_gen);
								checkForKTXTexturesToGenerate(world_state, world, ob, lod_URLs_considered, ktx_textures_to_gen);
							}
							catch(glare::Exception& e)
							{
								conPrint("\tMeshLODGenThread: exception while processing object: " + e.what());
							}
						}
					}
				}
			} // End lock scope

			conPrint("MeshLODGenThread: Iterating over objects took " + timer.elapsedStringNSigFigs(4) + ", meshes_to_gen: " + toString(meshes_to_gen.size()) + ", lod_textures_to_gen: " + toString(lod_textures_to_gen.size()) + 
				", ktx_textures_to_gen: " + toString(ktx_textures_to_gen.size()));


			//-------------------------------------------  Generate each mesh, without holding the world lock -------------------------------------------
			conPrint("MeshLODGenThread: Generating LOD meshes...");
			timer.reset();

			for(size_t i=0; i<meshes_to_gen.size(); ++i)
			{
				const LODMeshToGen& mesh_to_gen = meshes_to_gen[i];
				try
				{
					conPrint("MeshLODGenThread: Generating LOD mesh with URL " + mesh_to_gen.lod_URL);

					LODGeneration::generateLODModel(mesh_to_gen.model_abs_path, mesh_to_gen.lod_level, mesh_to_gen.LOD_model_abs_path);

					// Now that we have generated the LOD model, add it to resources.
					{ // lock scope
						Lock lock(world_state->mutex);

						const std::string raw_path = FileUtils::getFilename(mesh_to_gen.LOD_model_abs_path); // NOTE: assuming we can get raw/relative path from abs path like this.

						ResourceRef resource = new Resource(
							mesh_to_gen.lod_URL, // URL
							raw_path, // raw local path
							Resource::State_Present, // state
							mesh_to_gen.owner_id
						);

						world_state->addResourcesAsDBDirty(resource);
						world_state->resource_manager->addResource(resource);
						
					} // End lock scope
				}
				catch(glare::Exception& e)
				{
					conPrint("\tMeshLODGenThread: glare::Exception while generating LOD model: " + e.what());
				}
			}

			conPrint("MeshLODGenThread: Done generating LOD meshes. (Elapsed: " + timer.elapsedStringNSigFigs(4) + ")");


			//------------------------------------------- Generate each texture, without holding the world lock -------------------------------------------
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
							tex_to_gen.owner_id
						);

						world_state->addResourcesAsDBDirty(resource);
						world_state->resource_manager->addResource(resource);

					} // End lock scope
				}
				catch(glare::Exception& e)
				{
					conPrint("\tMeshLODGenThread: excep while generating LOD texture: " + e.what());
				}
			}

			conPrint("MeshLODGenThread: Done generating LOD textures. (Elapsed: " + timer.elapsedStringNSigFigs(4));

			//------------------------------------------- Generate each KTX texture, without holding the world lock -------------------------------------------
// NOTE: Disable KTX texture generation currently, since basis universal has lots of compile warnings which clutter up the build output, and we don't use basisu KTX files currently.
#if 0
			conPrint("MeshLODGenThread: Generating KTX textures...");
			timer.reset();

			for(size_t i=0; i<ktx_textures_to_gen.size(); ++i)
			{
				const KTXTextureToGen& tex_to_gen = ktx_textures_to_gen[i];
				try
				{
					conPrint("MeshLODGenThread: (ktx " + toString(i) + " / " + toString(ktx_textures_to_gen.size()) + "): Generating KTX texture with URL " + tex_to_gen.ktx_URL);

					LODGeneration::generateKTXTexture(tex_to_gen.source_tex_abs_path, 
						tex_to_gen.base_lod_level, tex_to_gen.lod_level, tex_to_gen.ktx_tex_abs_path, 
						task_manager);

					// Now that we have generated the LOD model, add it to resources.
					{ // lock scope
						Lock lock(world_state->mutex);

						const std::string raw_path = FileUtils::getFilename(tex_to_gen.ktx_tex_abs_path); // NOTE: assuming we can get raw/relative path from abs path like this.

						ResourceRef resource = new Resource(
							tex_to_gen.ktx_URL, // URL
							raw_path, // raw local path
							Resource::State_Present, // state
							tex_to_gen.owner_id
						);

						world_state->addResourcesAsDBDirty(resource);
						world_state->resource_manager->addResource(resource);

					} // End lock scope
				}
				catch(glare::Exception& e)
				{
					conPrint("\tMeshLODGenThread: excep while generating KTX texture: " + e.what());
				}
			}

			conPrint("MeshLODGenThread: Done generating KTX textures. (Elapsed: " + timer.elapsedStringNSigFigs(4) + ")");
#endif
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
