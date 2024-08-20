/*=====================================================================
ChunkGenThread.cpp
------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "ChunkGenThread.h"


#include "ServerWorldState.h"
#include "../shared/LODGeneration.h"
#include "../shared/VoxelMeshBuilding.h"
#include "../shared/ImageDecoding.h"
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
#include <graphics/ImageMapSequence.h>
#include <graphics/Map2D.h>
#include <graphics/ImageMap.h>
#include <dll/include/IndigoMesh.h>
#include <dll/include/IndigoException.h>
#include <dll/IndigoStringUtils.h>
#include <utils/IncludeHalf.h>
#include <utils/RuntimeCheck.h>
#include <utils/FileUtils.h>
#include <maths/matrix3.h>
#include <indigo/UVUnwrapper.h>


ChunkGenThread::ChunkGenThread(ServerAllWorldsState* world_state_)
:	world_state(world_state_)
{
}


ChunkGenThread::~ChunkGenThread()
{
}


//struct MaterialKey
//{
//	int ob_

void ChunkGenThread::doRun()
{
	PlatformUtils::setCurrentThreadName("ChunkGenThread");

	glare::TaskManager task_manager;

	try
	{
		while(1)
		{
			// Get vector of objects
			std::vector<WorldObjectRef> obs;

			Timer timer;
			{
				WorldStateLock lock(world_state->mutex);
				for(auto world_it = world_state->world_states.begin(); world_it != world_state->world_states.end(); ++world_it)
				{
					ServerWorldState* world = world_it->second.ptr();

					ServerWorldState::ObjectMapType& objects = world->getObjects(lock);
					for(auto it = objects.begin(); it != objects.end(); ++it)
						obs.push_back(it->second);
				}
			} // end lock scope

			conPrint("ChunkGenThread: Getting vector of objects took " + timer.elapsedStringNSigFigs(4));



			Reference<ServerWorldState> root_world_state = world_state->getRootWorldState();
			const float chunk_w = 256;
			for(int x=0; x<1; ++x)
			for(int y=0; y<1; ++y)
			{
				const js::AABBox chunk_aabb(
					Vec4f(x * chunk_w, y * chunk_w, -1000.f, 1.f), //min
					Vec4f((x + 1) * chunk_w, (y + 1) * chunk_w, 1000.f, 1.f) // max
				);

				std::vector<WorldObjectRef> chunk_obs;

				{
					WorldStateLock lock(world_state->mutex);
					ServerWorldState::ObjectMapType& objects = root_world_state->getObjects(lock);
					for(auto it = objects.begin(); it != objects.end(); ++it)
					{
						WorldObjectRef ob = it->second;
						if(chunk_aabb.contains(ob->pos.toVec4fPoint()))
						{
							chunk_obs.push_back(ob);
						}
					}
				} // End lock scope.

				if(!chunk_obs.empty())
				{
					// Make a mesh from merging all objects in the chunk together



					// Do a pass over objects to build the texture atlas
					//std::vector<Matrix3<float>> tex_coord_mapping;
					std::map<std::pair<size_t, size_t>, size_t> tex_coord_mapping; // Map from (index in chunk_obs, material index) to tex_map_infos index


					struct TexMapInfo
					{
						std::string tex_path;
						Matrix3<float> matrix;
					};

					std::vector<TexMapInfo> tex_map_infos;
					std::vector<BinRect> rects;

	
					for(size_t ob_i=0; ob_i<chunk_obs.size(); ++ob_i)
					{
						WorldObjectRef ob = chunk_obs[ob_i];

						try
						{

							bool have_mesh = false;
							if(ob->object_type == WorldObject::ObjectType_Generic)
							{
								if(!ob->model_url.empty())
								{
									const std::string model_path = world_state->resource_manager->pathForURL(ob->model_url);
									if(FileUtils::fileExists(model_path))
										have_mesh = true;
								}
							}
							else if(ob->object_type == WorldObject::ObjectType_VoxelGroup)
							{
								if(ob->getCompressedVoxels().size() > 0)
									have_mesh = true;
							}


							if(have_mesh)
							{
								for(size_t m=0; m<ob->materials.size(); ++m)
								{
									WorldMaterial* mat = ob->materials[m].ptr();
									if(mat)
									{
										if(!mat->colour_texture_url.empty())
										{
											const std::string tex_path = world_state->resource_manager->pathForURL(mat->colour_texture_url);
											if(FileUtils::fileExists(tex_path))
											{
												Reference<Map2D> map;
												if(hasExtension(tex_path, "gif"))
													map = GIFDecoder::decodeImageSequence(tex_path);
												else
													map = ImageDecoding::decodeImage(".", tex_path); // Load texture from disk and decode it.

												// Process 8-bit textures (do DXT compression, mip-map computation etc..) in this thread.
												const ImageMapUInt8* imagemap;
												if(dynamic_cast<const ImageMapUInt8*>(map.ptr()))
												{
													imagemap = map.downcastToPtr<ImageMapUInt8>();
												}
												else if(dynamic_cast<const ImageMapSequenceUInt8*>(map.ptr()))
												{
													const ImageMapSequenceUInt8* imagemapseq = map.downcastToPtr<ImageMapSequenceUInt8>();
													if(imagemapseq->images.empty())
														throw glare::Exception("imagemapseq was empty");
													imagemap = imagemapseq->images[0].ptr();
												}
												else
													throw glare::Exception("Unhandled texture type.");


												tex_coord_mapping[std::make_pair(ob_i, m)] = tex_map_infos.size();


												const float ob_size = ob->getAABBWS().longestLength();

												const float use_tex_w = myMin(128.f, ob_size * 20.f);
												const float use_tex_h = use_tex_w / (float)imagemap->getWidth() * (float)imagemap->getHeight();
												BinRect rect;
												rect.w = use_tex_w;
												rect.h = use_tex_h;
												rect.index = (int)rects.size();

												rects.push_back(rect);

												TexMapInfo tex_map_info;
												tex_map_info.tex_path = tex_path;
												tex_map_infos.push_back(tex_map_info);
											}
										}
									}
								}
							}
						}
						catch(glare::Exception& e)
						{
							conPrint("ChunkGenThread error while processing texture for ob: " + e.what());
						}
					}


					// Pack texture rectangles together
					if(!rects.empty())
					{
						UVUnwrapper::shelfPack(rects);

						// Do another pass to copy the actual texture data
						const int atlas_map_w = 4096;
						ImageMapUInt8Ref atlas_map = new ImageMapUInt8(4096, 4096, 3);
						atlas_map->zero();


						for(size_t i=0; i<rects.size(); ++i)
						{
							const BinRect& rect = rects[i];
							Matrix3f mat = Matrix3f::identity();
							mat.e[0] = rect.scale.x;
							mat.e[4] = rect.scale.y;
							mat.e[2] = rect.pos.x;
							mat.e[3] = rect.pos.y;

							if(rect.rotated)
								mat = Matrix3f(Vec3f(0, 1, 0), Vec3f(1, 0, 0), Vec3f(0, 0, 1)) * mat; // swap x and y
							tex_map_infos[i].matrix = mat;

							try
							{
								const std::string tex_path = tex_map_infos[i].tex_path;
								Reference<Map2D> map;
								if(hasExtension(tex_path, "gif"))
									map = GIFDecoder::decodeImageSequence(tex_path);
								else
									map = ImageDecoding::decodeImage(".", tex_path); // Load texture from disk and decode it.

								// Process 8-bit textures (do DXT compression, mip-map computation etc..) in this thread.
								const ImageMapUInt8* imagemap;
								if(dynamic_cast<const ImageMapUInt8*>(map.ptr()))
								{
									imagemap = map.downcastToPtr<ImageMapUInt8>();
								}
								else if(dynamic_cast<const ImageMapSequenceUInt8*>(map.ptr()))
								{
									const ImageMapSequenceUInt8* imagemapseq = map.downcastToPtr<ImageMapSequenceUInt8>();
									if(imagemapseq->images.empty())
										throw glare::Exception("imagemapseq was empty");
									imagemap = imagemapseq->images[0].ptr();
								}
								else
									throw glare::Exception("Unhandled texture type.");


								const float rect_output_w = rect.rotatedWidth() * rect.scale.x;
								const float rect_output_h = rect.rotatedHeight() * rect.scale.y;

								const int output_pixel_w = (int)(rect_output_w * atlas_map_w);
								const int output_pixel_h = (int)(rect_output_h * atlas_map_w);

								if(output_pixel_w > 0 && output_pixel_h)
								{
									// Resize image down
									Reference<Map2D> resized_map = imagemap->resizeMidQuality(output_pixel_w, output_pixel_h, &task_manager);

									runtimeCheck(resized_map.isType<ImageMapUInt8>());
									ImageMapUInt8Ref resized_map_uint8 = resized_map.downcast<ImageMapUInt8>();



									const int dest_begin_x = (int)(atlas_map_w * rect.pos.x);
									const int dest_begin_y = (int)(atlas_map_w * rect.pos.y);

									for(int dy=dest_begin_y; dy<dest_begin_y + output_pixel_h; ++dy)
									for(int dx=dest_begin_x; dx<dest_begin_x + output_pixel_w; ++dx)
									{
										const float ux = (dx - dest_begin_x) / (float)output_pixel_w;
										const float uy = (dy - dest_begin_y) / (float)output_pixel_h;

										Colour4f col = resized_map_uint8->vec3Sample(ux, uy, /*wrap=*/true);

										atlas_map->getPixel(dx, dy)[0] = (uint8)(col[0] * 255.f);
										atlas_map->getPixel(dx, dy)[1] = (uint8)(col[1] * 255.f);
										atlas_map->getPixel(dx, dy)[2] = (uint8)(col[2] * 255.f);
									}
								}

							}
							catch(glare::Exception& e)
							{
								conPrint("ChunkGenThread error while processing texture for ob: " + e.what());
							}
						}


						PNGDecoder::write(*atlas_map, "d:/tempfiles/atlas_256_" + toString(x) + "_" + toString(y) + ".png");
					}




					BatchedMeshRef combined_mesh = new BatchedMesh();
					size_t offset = 0;
					combined_mesh->vert_attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_Position, BatchedMesh::ComponentType_Float, /*offset_B=*/offset));
					offset += BatchedMesh::vertAttributeSize(combined_mesh->vert_attributes.back());

					const size_t combined_mesh_normal_offset_B = offset;
					combined_mesh->vert_attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_Normal, BatchedMesh::ComponentType_PackedNormal, /*offset_B=*/offset));
					offset += BatchedMesh::vertAttributeSize(combined_mesh->vert_attributes.back());

					//combined_mesh->vert_attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_Colour, BatchedMesh::ComponentType_Float, /*offset_B=*/offset));
					//offset += BatchedMesh::vertAttributeSize(combined_mesh->vert_attributes.back());

					const size_t combined_mesh_uv0_offset_B = offset;
					combined_mesh->vert_attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_UV_0, BatchedMesh::ComponentType_Float, /*offset_B=*/offset));
					offset += BatchedMesh::vertAttributeSize(combined_mesh->vert_attributes.back());
				
					const size_t combined_mesh_vert_size = offset;

					js::Vector<uint32, 16> combined_indices;
					js::AABBox aabb_os = js::AABBox::emptyAABBox();

					for(size_t ob_i=0; ob_i<chunk_obs.size(); ++ob_i)
					{
						WorldObjectRef ob = chunk_obs[ob_i];

						try
						{
							if(!isFinite(ob->angle))
								ob->angle = 0;

							if(!isFinite(ob->angle) || !ob->axis.isFinite())
								throw glare::Exception("Invalid angle or axis");

							BatchedMeshRef mesh;
							if(ob->object_type == WorldObject::ObjectType_Generic)
							{
								if(!ob->model_url.empty())
								{
									const std::string model_path = world_state->resource_manager->pathForURL(ob->model_url);

									mesh = LODGeneration::loadModel(model_path);
								}
							}
							else if(ob->object_type == WorldObject::ObjectType_VoxelGroup)
							{
								if(ob->getCompressedVoxels().size() > 0)
								{
									js::Vector<bool, 16> mat_transparent; // TEMP HACK

									VoxelGroup voxel_group;
									WorldObject::decompressVoxelGroup(ob->getCompressedVoxels().data(), ob->getCompressedVoxels().size(), /*mem_allocator=*/NULL, voxel_group); // TEMP use mem allocator
									Indigo::MeshRef indigo_mesh = VoxelMeshBuilding::makeIndigoMeshForVoxelGroup(voxel_group, /*subsample_factor=*/1, /*generate_shading_normals=*/true, mat_transparent, /*mem_allocator=*/NULL);

									mesh = BatchedMesh::buildFromIndigoMesh(*indigo_mesh);
								}
							}


							if(mesh.nonNull())
							{
								const Matrix4f ob_to_world = obToWorldMatrix(*ob);
								Matrix4f ob_normals_to_world;
								const bool invertible = ob_to_world.getUpperLeftInverseTranspose(ob_normals_to_world);
								if(!invertible)
								{
									conPrint("Warning: ob_to_world not invertible.");
									ob_normals_to_world = ob_to_world;
								}

								// Allocate room for new verts
								const size_t write_i = combined_mesh->vertex_data.size();
								combined_mesh->vertex_data.resize(write_i + mesh->numVerts() * combined_mesh_vert_size);

								const BatchedMesh::VertAttribute& pos = mesh->getAttribute(BatchedMesh::VertAttribute_Position);
								if(pos.component_type != BatchedMesh::ComponentType_Float)
									throw glare::Exception("unhandled pos component type");

								const size_t num_verts = mesh->numVerts();
								const size_t vert_stride_B = mesh->vertexSize();

								//------------------------------------------ Copy vertex positions ------------------------------------------
								for(size_t i = 0; i < num_verts; ++i)
								{
									runtimeCheck(vert_stride_B * i + pos.offset_B + sizeof(Vec3f) <= mesh->vertex_data.size());

									Vec3f v;
									std::memcpy(&v, &mesh->vertex_data[vert_stride_B * i + pos.offset_B], sizeof(Vec3f));

									const Vec4f new_v_vec4f = ob_to_world * v.toVec4fPoint();

									aabb_os.enlargeToHoldPoint(new_v_vec4f);

									const Vec3f new_v = Vec3f(new_v_vec4f);

									std::memcpy(&combined_mesh->vertex_data[write_i + combined_mesh_vert_size * i], &new_v, sizeof(Vec3f));
								}

								//------------------------------------------ Copy vertex normals ------------------------------------------
								const BatchedMesh::VertAttribute* normal_attr = mesh->findAttribute(BatchedMesh::VertAttribute_Normal);
								if(normal_attr)
								{
									if(normal_attr->component_type == BatchedMesh::ComponentType_PackedNormal)
									{
										for(size_t i = 0; i < num_verts; ++i)
										{
											runtimeCheck(vert_stride_B * i + normal_attr->offset_B + sizeof(uint32) <= mesh->vertex_data.size());

											uint32 packed_normal;
											std::memcpy(&packed_normal, &mesh->vertex_data[vert_stride_B * i + normal_attr->offset_B], sizeof(uint32));

											const Vec4f n = batchedMeshUnpackNormal(packed_normal); // TODO: do this with integer manipulation instead of floats? Will avoid possible rounding error.

											const Vec4f new_n = normalise(ob_normals_to_world * n);

											const uint32 new_packed_normal = batchedMeshPackNormal(new_n);

											std::memcpy(&combined_mesh->vertex_data[write_i + combined_mesh_vert_size * i + combined_mesh_normal_offset_B], &new_packed_normal, sizeof(uint32));
										}
									}
									else
										throw glare::Exception("unhandled normal component type");
								}
								else
								{
									// TODO: Compute geometric normals
									for(size_t i = 0; i < num_verts; ++i)
									{
										const Vec4f new_n = Vec4f(1, 0, 0, 0); // TEMP HACK

										const uint32 new_packed_normal = batchedMeshPackNormal(new_n);

										std::memcpy(&combined_mesh->vertex_data[write_i + combined_mesh_vert_size * i + combined_mesh_normal_offset_B], &new_packed_normal, sizeof(uint32));
									}
								}

								//------------------------------------------ Copy vertex UV0s ------------------------------------------
								const BatchedMesh::VertAttribute* uv0_attr = mesh->findAttribute(BatchedMesh::VertAttribute_UV_0);
								if(uv0_attr)
								{
									//TEMP: just use uv transformation matrix for mat 0 for this object
									Matrix3f uv_matrix = Matrix3f::identity();
									auto res = tex_coord_mapping.find(std::make_pair(ob_i, /*mat_i=*/0));
									if(res != tex_coord_mapping.end())
									{
										const TexMapInfo& info = tex_map_infos[res->second];
										uv_matrix = info.matrix;
									}


									if(uv0_attr->component_type == BatchedMesh::ComponentType_Float)
									{
										for(size_t i = 0; i < num_verts; ++i)
										{
											runtimeCheck(vert_stride_B * i + uv0_attr->offset_B + sizeof(Vec2f) <= mesh->vertex_data.size());

											Vec2f uv;
											std::memcpy(&uv, &mesh->vertex_data[vert_stride_B * i + uv0_attr->offset_B], sizeof(Vec2f));

											const Vec3f new_uv3 = uv_matrix * Vec3f(uv.x, uv.y, 1.f);
											const Vec2f new_uv(new_uv3.x, new_uv3.y);
											std::memcpy(&combined_mesh->vertex_data[write_i + combined_mesh_vert_size * i + combined_mesh_uv0_offset_B], &new_uv, sizeof(Vec2f));
										}
									}
									else if(uv0_attr->component_type == BatchedMesh::ComponentType_Half)
									{
										for(size_t i = 0; i < num_verts; ++i)
										{
											runtimeCheck(vert_stride_B * i + uv0_attr->offset_B + sizeof(half) * 2 <= mesh->vertex_data.size());

											half uv[2];
											std::memcpy(&uv, &mesh->vertex_data[vert_stride_B * i + uv0_attr->offset_B], sizeof(half) * 2);

											const Vec3f new_uv3 = uv_matrix * Vec3f(uv[0], uv[1], 1.f);
											const Vec2f new_uv(new_uv3.x, new_uv3.y);
											std::memcpy(&combined_mesh->vertex_data[write_i + combined_mesh_vert_size * i + combined_mesh_uv0_offset_B], &new_uv, sizeof(Vec2f));
										}
									}
									else
										throw glare::Exception("unhandled uv0 component type");
								}
								else // else UV0 was not present in source mesh, so just write out (0,0), uvs.
								{
									for(size_t i = 0; i < num_verts; ++i)
									{
										const Vec2f new_uv(0.f, 0.f);
										std::memcpy(&combined_mesh->vertex_data[write_i + combined_mesh_vert_size * i + combined_mesh_uv0_offset_B], &new_uv, sizeof(Vec2f));
									}
								}

								//------------------------------------------ Copy indices ------------------------------------------
								const uint32 vert_offset = (uint32)(write_i / combined_mesh_vert_size);

								const size_t num_indices = mesh->numIndices();

								const size_t indices_write_i = combined_indices.size();
								combined_indices.resize(indices_write_i + num_indices);

								if(mesh->index_type == BatchedMesh::ComponentType_UInt8)
								{
									for(size_t i=0; i<num_indices; ++i)
										combined_indices[indices_write_i + i] = vert_offset + ((const uint8*)mesh->index_data.data())[i];
								}
								else if(mesh->index_type == BatchedMesh::ComponentType_UInt16)
								{
									for(size_t i=0; i<num_indices; ++i)
										combined_indices[indices_write_i + i] = vert_offset + ((const uint16*)mesh->index_data.data())[i];
								}
								else if(mesh->index_type == BatchedMesh::ComponentType_UInt32)
								{
									for(size_t i=0; i<num_indices; ++i)
										combined_indices[indices_write_i + i] = vert_offset + ((const uint32*)mesh->index_data.data())[i];
								}
								else
									throw glare::Exception("unhandled index_type");


								//------------------------------------------ Compute geometric normals, if no shading normal attribute is present in source mesh ------------------------------------------
								if(!normal_attr)
								{
									assert(num_indices % 3 == 0);
									for(size_t i=0; i<num_indices; i+=3) // For each tri in source mesh:
									{
										const uint32 v0 = combined_indices[indices_write_i + i + 0];
										const uint32 v1 = combined_indices[indices_write_i + i + 1];
										const uint32 v2 = combined_indices[indices_write_i + i + 2];

										// Read transformed vertex positions
										runtimeCheck(combined_mesh_vert_size * v0 + sizeof(Vec3f) <= combined_mesh->vertex_data.size());
										runtimeCheck(combined_mesh_vert_size * v1 + sizeof(Vec3f) <= combined_mesh->vertex_data.size());
										runtimeCheck(combined_mesh_vert_size * v2 + sizeof(Vec3f) <= combined_mesh->vertex_data.size());
										
										Vec3f v0pos, v1pos, v2pos;
										std::memcpy(&v0pos, &combined_mesh->vertex_data[combined_mesh_vert_size * v0], sizeof(Vec3f));
										std::memcpy(&v1pos, &combined_mesh->vertex_data[combined_mesh_vert_size * v1], sizeof(Vec3f));
										std::memcpy(&v2pos, &combined_mesh->vertex_data[combined_mesh_vert_size * v2], sizeof(Vec3f));

										const Vec3f new_n = normalise(crossProduct(v1pos - v0pos, v2pos - v0pos));

										const uint32 new_packed_normal = batchedMeshPackNormal(new_n.toVec4fVector());

										// Write the new geometric normal for the vertices v0, v1, v2
										runtimeCheck(combined_mesh_vert_size * v0 + combined_mesh_normal_offset_B + sizeof(Vec3f) <= combined_mesh->vertex_data.size());
										runtimeCheck(combined_mesh_vert_size * v1 + combined_mesh_normal_offset_B + sizeof(Vec3f) <= combined_mesh->vertex_data.size());
										runtimeCheck(combined_mesh_vert_size * v2 + combined_mesh_normal_offset_B + sizeof(Vec3f) <= combined_mesh->vertex_data.size());
										
										std::memcpy(&combined_mesh->vertex_data[combined_mesh_vert_size * v0 + combined_mesh_normal_offset_B], &new_packed_normal, sizeof(uint32));
										std::memcpy(&combined_mesh->vertex_data[combined_mesh_vert_size * v1 + combined_mesh_normal_offset_B], &new_packed_normal, sizeof(uint32));
										std::memcpy(&combined_mesh->vertex_data[combined_mesh_vert_size * v2 + combined_mesh_normal_offset_B], &new_packed_normal, sizeof(uint32));
									}
								}

							} // end if(mesh.nonNull())
						}
						catch(glare::Exception& e)
						{
							conPrint("ChunkGenThread error while processing ob: " + e.what());
						}
					} // End for each ob

					combined_mesh->setIndexDataFromIndices(combined_indices, combined_mesh->numVerts());

					BatchedMesh::IndicesBatch batch;
					batch.indices_start = 0;
					batch.material_index = 0;
					batch.num_indices = (uint32)combined_indices.size();
					combined_mesh->batches.push_back(batch);

					combined_mesh->aabb_os = aabb_os;

					// Write combined mesh to disk
					const std::string path = "d:/tempfiles/chunk_256_" + toString(x) + "_" + toString(y) + ".bmesh";
					combined_mesh->writeToFile(path);

					conPrint("Wrote chunk mesh to '" + path + "'.");
				}
			}


			conPrint("ChunkGenThread: Done. (Elapsed: " + timer.elapsedStringNSigFigs(4));


			//if(made_change)
			//	world_state->markAsChanged();

			// PlatformUtils::Sleep(30*1000 * 100);
			return; // Just run once for now.
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("ChunkGenThread: glare::Exception: " + e.what());
	}
	catch(std::exception& e) // catch std::bad_alloc etc..
	{
		conPrint(std::string("ChunkGenThread: Caught std::exception: ") + e.what());
	}
}
