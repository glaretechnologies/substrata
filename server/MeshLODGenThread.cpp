/*=====================================================================
MeshLODGenThread.cpp
--------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "MeshLODGenThread.h"


#include "ServerWorldState.h"
#include "../shared/LODGeneration.h"
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
#include <dll/include/IndigoMesh.h>
#include <dll/include/IndigoException.h>
#include <dll/IndigoStringUtils.h>


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
	std::string model_path;
	std::string LOD_model_path;
	std::string lod_URL;
	int lod_level;
	UserID owner_id;
};


struct LODTextureToGen
{
	std::string tex_path;
	std::string LOD_tex_path;
	std::string lod_URL;
	int lod_level;
	UserID owner_id;
};


struct MeshLODGenThreadTexInfo
{
	bool has_alpha;
	bool is_hi_res;
};


void MeshLODGenThread::doRun()
{
	PlatformUtils::setCurrentThreadName("MeshLODGenThread");

	glare::TaskManager task_manager;

	//TEMP;
	//textureHasAlphaChannel("C:\\Users\\nick\\AppData\\Roaming\\Substrata\\server_data\\server_resources\\Elementals__125_png_8837233801540030562.png");
	//generateLODTexture("C:\\Users\\nick\\AppData\\Roaming\\Substrata\\server_data\\server_resources\\Leaves_d2x2_4_baseColor_png_14072515031151040629.png", 1, 
	//	"C:\\Users\\nick\\AppData\\Roaming\\Substrata\\server_data\\server_resources\\Leaves_d2x2_4_baseColor_png_14072515031151040629_lod1.png", task_manager);

	try
	{
		while(1)
		{
			// Get vector of objects
			std::vector<WorldObjectRef> obs;

			Timer timer;
			{
				Lock lock(world_state->mutex);
				for(auto world_it = world_state->world_states.begin(); world_it != world_state->world_states.end(); ++world_it)
				{
					ServerWorldState* world = world_it->second.ptr();

					for(auto it = world->objects.begin(); it != world->objects.end(); ++it)
						obs.push_back(it->second);
				}
			} // end lock scope

			conPrint("MeshLODGenThread: Getting vector of objects took " + timer.elapsedStringNSigFigs(4));

			// Iterate over objects.
			// Set object world space AABB.
			// Set object max_lod_level if it is a generic model or a voxel model.
			// Compute list of LOD meshes we need to generate.
			//
			// Note that we will do this without holding the world lock, since we are calling loadModel which is slow.
			std::vector<LODMeshToGen> meshes_to_gen;
			std::vector<LODTextureToGen> textures_to_gen;
			std::unordered_set<std::string> lod_URLs_considered;
			std::map<std::string, MeshLODGenThreadTexInfo> tex_info; // Cached info about textures
			bool made_change = false;

			conPrint("MeshLODGenThread: Iterating over objects...");
			timer.reset();

			for(size_t i=0; i<obs.size(); ++i)
			{
				WorldObject* ob = obs[i].ptr();

				try
				{
					// Set object world space AABB if not set yet:
					//if(ob->aabb_ws.isEmpty()) // If not set yet:
					{
						// conPrint("computing AABB for object " + ob->uid.toString() + "...");

						// First get object space AABB
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
						else if(ob->object_type == WorldObject::ObjectType_VoxelGroup)
						{
							try
							{
								VoxelGroup voxel_group;
								WorldObject::decompressVoxelGroup(ob->getCompressedVoxels().data(), ob->getCompressedVoxels().size(), voxel_group);
								aabb_os = voxel_group.getAABB();

								const int new_max_lod_level = (voxel_group.voxels.size() > 256) ? 2 : 0;
								if(new_max_lod_level != ob->max_model_lod_level)
									made_change = true;
								
								ob->max_model_lod_level = new_max_lod_level;
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
								if(true)
								{
									const std::string model_path = world_state->resource_manager->pathForURL(ob->model_url);

									BatchedMeshRef batched_mesh = LODGeneration::loadModel(model_path);

									const int new_max_lod_level = (batched_mesh->numVerts() <= 4 * 6) ? 0 : 2; // If this is a very small model (e.g. a cuboid), don't generate LOD versions of it.
									if(new_max_lod_level != ob->max_model_lod_level)
										made_change = true;

									ob->max_model_lod_level = new_max_lod_level;

									if(new_max_lod_level == 2)
									{
										for(int lvl = 1; lvl <= 2; ++lvl)
										{
											const std::string lod_path = WorldObject::getLODModelURLForLevel(model_path, lvl);
											const std::string lod_URL  = WorldObject::getLODModelURLForLevel(ob->model_url, lvl);

											if(lod_URLs_considered.count(lod_URL) == 0)
											{
												lod_URLs_considered.insert(lod_URL);

												if(!world_state->resource_manager->isFileForURLPresent(lod_URL))
												{
													// Generate the model
													LODMeshToGen mesh_to_gen;
													mesh_to_gen.lod_level = lvl;
													mesh_to_gen.model_path = model_path;
													mesh_to_gen.LOD_model_path = lod_path;
													mesh_to_gen.lod_URL = lod_URL;
													mesh_to_gen.owner_id = world_state->resource_manager->getExistingResourceForURL(ob->model_url)->owner_id;
													meshes_to_gen.push_back(mesh_to_gen);
												}
												else // Else if LOD model is present on disk:
												{
													if(lvl == 1)
													{
														try
														{
															BatchedMeshRef lod1_mesh = LODGeneration::loadModel(lod_path);
															if((batched_mesh->numVerts() > 1024) && // If this is a med/large mesh
																((float)lod1_mesh->numVerts() > (batched_mesh->numVerts() / 4.f))) // If we acheived less than a 4x reduction in the number of vertices, try again with sloppy simplification
															{
																conPrint("Mesh '" + lod_URL + "' was not simplified enough, recomputing LOD 1 mesh...");
																
																// Generate the model
																LODMeshToGen mesh_to_gen;
																mesh_to_gen.lod_level = lvl;
																mesh_to_gen.model_path = model_path;
																mesh_to_gen.LOD_model_path = lod_path;
																mesh_to_gen.lod_URL = lod_URL;
																mesh_to_gen.owner_id = world_state->resource_manager->getExistingResourceForURL(ob->model_url)->owner_id;
																meshes_to_gen.push_back(mesh_to_gen);
															}
														}
														catch(glare::Exception& e)
														{
															conPrint("Error while trying to load LOD 1 mesh: " + e.what());
														}
													}
												}
											}
										}
									}

									aabb_os = batched_mesh->aabb_os;
								}
							}
						}
						else
							throw glare::Exception("invalid object type.");

						// Compute and assign aabb_ws to object.
						if(!aabb_os.isEmpty()) // If we got a valid aabb_os:
						{
							Lock lock(world_state->mutex);

							//TEMP
							if(!isFinite(ob->angle))
								ob->angle = 0;

							if(!isFinite(ob->angle) || !ob->axis.isFinite())
								throw glare::Exception("Invalid angle or axis");

							const Matrix4f to_world = obToWorldMatrix(*ob);

							const js::AABBox new_aabb_ws = aabb_os.transformedAABB(to_world);

							const bool updating_aabb_ws = new_aabb_ws != ob->aabb_ws;

							if(updating_aabb_ws)
							{
								conPrint("Updating voxel ob AABB:");
								conPrint("Old AABB: "+ ob->aabb_ws.toString());
								conPrint("New AABB: "+ new_aabb_ws.toString());

								ob->aabb_ws = new_aabb_ws;
								made_change = true;
							}
						}


						// Process textures
						// Process lightmap
						// if(false) // TEMP NO LIGHTMAP LOD  !ob->lightmap_url.empty())
						// {
						// 	ResourceRef base_resource = world_state->resource_manager->getExistingResourceForURL(ob->lightmap_url);
						// 	if(base_resource.nonNull())
						// 	{
						// 		const std::string tex_path = base_resource->getLocalPath();
						// 		const std::string lod_URL  = WorldObject::getLODTextureURLForLevel(ob->lightmap_url, lvl, /*has alpha=*/false);
						// 		const std::string lod_path = world_state->resource_manager->pathForURL(lod_URL);
						// 
						// 		if(lod_URLs_considered.count(lod_URL) == 0)
						// 		{
						// 			lod_URLs_considered.insert(lod_URL);
						// 
						// 			if(!world_state->resource_manager->isFileForURLPresent(lod_URL))
						// 			{
						// 				// Generate the texture
						// 				LODTextureToGen tex_to_gen;
						// 				tex_to_gen.lod_level = lvl;
						// 				tex_to_gen.tex_path = tex_path;
						// 				tex_to_gen.LOD_tex_path = lod_path;
						// 				tex_to_gen.lod_URL = lod_URL;
						// 				tex_to_gen.owner_id = base_resource->owner_id;
						// 				textures_to_gen.push_back(tex_to_gen);
						// 			}
						// 		}
						// 	}
						// }

						// Process material textures
						for(size_t z=0; z<ob->materials.size(); ++z)
						{
							WorldMaterial* mat = ob->materials[z].ptr();
							if(mat)
							{
								if(!mat->colour_texture_url.empty())
								{
									ResourceRef base_resource = world_state->resource_manager->getExistingResourceForURL(mat->colour_texture_url);
									if(base_resource.nonNull())
									{
										const std::string tex_path = base_resource->getLocalPath();

										if(hasExtension(tex_path, "mp4"))
										{
											// Don't generate LOD for mp4 currently.
										}
										else
										{
											const uint32 old_flags = mat->flags;

											try
											{
												// Compute has_alpha and is_high_res for the texture if we haven't already.
												auto res = tex_info.find(tex_path);
												if(res == tex_info.end())
												{
													Reference<Map2D> map = ImFormatDecoder::decodeImage(".", tex_path); // Load texture from disk and decode it.
													const bool is_hi_res = map->getMapWidth() > 1024 || map->getMapHeight() > 1024;
													const bool has_alpha = textureHasAlphaChannel(map);

													tex_info[tex_path].has_alpha = has_alpha;
													tex_info[tex_path].is_hi_res = is_hi_res;
												}

												// If the texture is very high res, set minimum texture lod level to -1.  Lod level 0 will be the texture resized to 1024x1024 or below.

												const bool is_high_res = tex_info[tex_path].is_hi_res;
												const bool has_alpha   = tex_info[tex_path].has_alpha;

												// conPrint("tex " + tex_path + " is_hi_res: " + boolToString(is_high_res));

												BitUtils::setOrZeroBit(mat->flags, WorldMaterial::MIN_LOD_LEVEL_IS_NEGATIVE_1, is_high_res);
												BitUtils::setOrZeroBit(mat->flags, WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG,   has_alpha);

												made_change = made_change || (mat->flags != old_flags);

												if(mat->flags != old_flags)
													conPrint("Updated mat flags: (for mat with tex " + tex_path + "): is_hi_res: " + boolToString(is_high_res));

												const int start_lod_level = mat->minLODLevel() + 1;
												for(int lvl = start_lod_level; lvl <= 2; ++lvl)
												{
													const std::string lod_URL = mat->getLODTextureURLForLevel(mat->colour_texture_url, lvl, has_alpha);
											
													if(lod_URL != mat->colour_texture_url) // We don't do LOD for some texture types.
													{
														const std::string lod_path = world_state->resource_manager->pathForURL(lod_URL);

														if(lod_URLs_considered.count(lod_URL) == 0)
														{
															lod_URLs_considered.insert(lod_URL);

															if(!world_state->resource_manager->isFileForURLPresent(lod_URL))
															{
																// Generate the texture
																LODTextureToGen tex_to_gen;
																tex_to_gen.lod_level = lvl;
																tex_to_gen.tex_path = tex_path;
																tex_to_gen.LOD_tex_path = lod_path;
																tex_to_gen.lod_URL = lod_URL;
																tex_to_gen.owner_id = base_resource->owner_id;
																textures_to_gen.push_back(tex_to_gen);
															}
														}
													}
												}
											}
											catch(glare::Exception& e)
											{
												conPrint("Excep while loading texture: " + e.what());
											}
										}
									}
								}
							}
						}
					}
				}
				catch(glare::Exception& e)
				{
					conPrint("MeshLODGenThread: exception while processing object: " + e.what());
				}
			}

			conPrint("MeshLODGenThread: Iterating over objects took " + timer.elapsedStringNSigFigs(4));


			// Generate each mesh, without holding the world lock.
			conPrint("MeshLODGenThread: Generating LOD meshes...");
			timer.reset();

			for(size_t i=0; i<meshes_to_gen.size(); ++i)
			{
				const LODMeshToGen& mesh_to_gen = meshes_to_gen[i];
				try
				{
					conPrint("MeshLODGenThread: Generating LOD mesh with URL " + mesh_to_gen.lod_URL);

					LODGeneration::generateLODModel(mesh_to_gen.model_path, mesh_to_gen.lod_level, mesh_to_gen.LOD_model_path);

					// Now that we have generated the LOD model, add it to resources.
					{ // lock scope
						Lock lock(world_state->mutex);

						ResourceRef resource = new Resource(
							mesh_to_gen.lod_URL, // URL
							mesh_to_gen.LOD_model_path, // local path
							Resource::State_Present, // state
							mesh_to_gen.owner_id
						);

						world_state->resource_manager->addResource(resource);
						
						made_change = true;
					} // End lock scope
				}
				catch(glare::Exception& e)
				{
					conPrint("MeshLODGenThread: glare::Exception while generating LOD model: " + e.what());
				}
			}

			conPrint("MeshLODGenThread: Done generating LOD meshes. (Elapsed: " + timer.elapsedStringNSigFigs(4));


			// Generate each texture, without holding the world lock.
			conPrint("MeshLODGenThread: Generating LOD textures...");
			timer.reset();

			for(size_t i=0; i<textures_to_gen.size(); ++i)
			{
				const LODTextureToGen& tex_to_gen = textures_to_gen[i];
				try
				{
					conPrint("MeshLODGenThread: Generating LOD texture with URL " + tex_to_gen.lod_URL);

					LODGeneration::generateLODTexture(tex_to_gen.tex_path, tex_to_gen.lod_level, tex_to_gen.LOD_tex_path, task_manager);

					// Now that we have generated the LOD model, add it to resources.
					{ // lock scope
						Lock lock(world_state->mutex);

						ResourceRef resource = new Resource(
							tex_to_gen.lod_URL, // URL
							tex_to_gen.LOD_tex_path, // local path
							Resource::State_Present, // state
							tex_to_gen.owner_id
						);

						world_state->resource_manager->addResource(resource);

						made_change = true;
					} // End lock scope
				}
				catch(glare::Exception& e)
				{
					conPrint("MeshLODGenThread: excep while generating LOD texture: " + e.what());
				}
			}

			conPrint("MeshLODGenThread: Done generating LOD textures. (Elapsed: " + timer.elapsedStringNSigFigs(4));


			if(made_change)
				world_state->markAsChanged();

			// PlatformUtils::Sleep(30*1000 * 100);
			return; // Just run once for now.
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
