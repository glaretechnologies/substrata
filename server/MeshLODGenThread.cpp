/*=====================================================================
MeshLODGenThread.cpp
--------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "MeshLODGenThread.h"


#include "../server/ServerWorldState.h"
#include <ConPrint.h>
#include <Exception.h>
#include <Lock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <KillThreadMessage.h>
#include <Timer.h>
#include <graphics/MeshSimplification.h>
#include <graphics/formatdecoderobj.h>
#include <graphics/FormatDecoderSTL.h>
#include <graphics/FormatDecoderGLTF.h>
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


static BatchedMeshRef loadModel(const std::string& model_path)
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


static void generateLODModel(const std::string& model_path, int lod_level, const std::string& LOD_model_path)
{
	BatchedMeshRef batched_mesh = loadModel(model_path);

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


struct LODMeshToGen
{
	std::string model_path;
	std::string LOD_model_path;
	std::string lod_URL;
	int lod_level;
	UserID owner_id;
};


void MeshLODGenThread::doRun()
{
	PlatformUtils::setCurrentThreadName("MeshLODGenThread");

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
			// Set object max_lod_level if it is a generic model.
			// Compute list of LOD meshes we need to generate.
			//
			// Note that we will do this without holding the world lock, since we are calling loadModel which is slow.
			std::vector<LODMeshToGen> meshes_to_gen;
			std::unordered_set<std::string> lod_URLs_considered;
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
							VoxelGroup voxel_group;
							WorldObject::decompressVoxelGroup(ob->getCompressedVoxels().data(), ob->getCompressedVoxels().size(), voxel_group);
							aabb_os = voxel_group.getAABB();
						}
						else
						{
							assert(ob->object_type == WorldObject::ObjectType_Generic);

							// Try and load mesh, get AABB from it.
							if(!ob->model_url.empty())
							{
								const std::string model_path = world_state->resource_manager->pathForURL(ob->model_url);

								BatchedMeshRef batched_mesh = loadModel(model_path);

								const int new_max_lod_level = (batched_mesh->numVerts() <= 4 * 6) ? 0 : 2; // If this is a very small model (e.g. a cuboid), don't generate LOD versions of it.
								if(new_max_lod_level != ob->max_lod_level)
									made_change = true;

								ob->max_lod_level = new_max_lod_level;

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
										}
									}
								}

								aabb_os = batched_mesh->aabb_os;
							}
						}

						if(!aabb_os.isEmpty()) // If we got a valid aabb_os:
						{
							//TEMP
							if(!isFinite(ob->angle))
								ob->angle = 0;

							if(!isFinite(ob->angle) || !ob->axis.isFinite())
								throw glare::Exception("Invalid angle or axis");

							const Matrix4f to_world = obToWorldMatrix(*ob);

							const js::AABBox new_aabb_ws = aabb_os.transformedAABB(to_world);
							
							made_change = made_change || !(new_aabb_ws == ob->aabb_ws);

							ob->aabb_ws = aabb_os.transformedAABB(to_world);
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

					generateLODModel(mesh_to_gen.model_path, mesh_to_gen.lod_level, mesh_to_gen.LOD_model_path);

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
