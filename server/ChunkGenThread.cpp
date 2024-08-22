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
#include <graphics/ShelfPack.h>
#include <dll/include/IndigoMesh.h>
#include <dll/include/IndigoException.h>
#include <dll/IndigoStringUtils.h>
#include <utils/IncludeHalf.h>
#include <utils/RuntimeCheck.h>
#include <utils/FileUtils.h>
#include <maths/matrix3.h>


ChunkGenThread::ChunkGenThread(ServerAllWorldsState* world_state_)
:	world_state(world_state_)
{
}


ChunkGenThread::~ChunkGenThread()
{
}


struct MatInfo
{
	std::string tex_path;
	WorldMaterialRef world_mat;
};

struct ObInfo
{
	Matrix4f ob_to_world;
	js::AABBox aabb_ws;
	std::string model_path;
	js::Vector<uint8, 16> compressed_voxels;
	uint32 object_type;
	std::vector<MatInfo> mat_info;
};

struct TexMapInfo
{
	TexMapInfo() : largest_use_tex_w(0), largest_use_tex_h(0), to_atlas_coords_matrix(Matrix3f::identity()) {}

	std::string tex_path;
	//Matrix3<float> matrix;
	int bin_rect_index;

	Matrix3f to_atlas_coords_matrix;

	float largest_use_tex_w, largest_use_tex_h;
};


static void buildChunkForObInfo(std::vector<ObInfo>& ob_infos, int chunk_x, int chunk_y, glare::TaskManager& task_manager)
{
	std::map<std::string, TexMapInfo> tex_map_infos; // Map from tex_path to TexMapInfo

	for(size_t i=0; i<ob_infos.size(); ++i)
	{
		ObInfo& ob_info = ob_infos[i];
		try
		{
			for(size_t m=0; m<ob_info.mat_info.size(); ++m)
			{
				MatInfo& mat_info = ob_info.mat_info[m];

				const std::string tex_path = mat_info.tex_path;

				if(!tex_path.empty())
				{
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


						// See if this tex has been processed already
						if(tex_map_infos.count(tex_path) == 0)
						{
							tex_map_infos[tex_path] = TexMapInfo();
							tex_map_infos[tex_path].tex_path = tex_path;
						}

						const float ob_size = ob_info.aabb_ws.longestLength();

						const float use_tex_w = myMin(128.f, ob_size * 20.f);
						const float use_tex_h = use_tex_w / (float)imagemap->getWidth() * (float)imagemap->getHeight();

						tex_map_infos[tex_path].largest_use_tex_w = myMax(tex_map_infos[tex_path].largest_use_tex_w, use_tex_w);
						tex_map_infos[tex_path].largest_use_tex_h = myMax(tex_map_infos[tex_path].largest_use_tex_h, use_tex_h);
					}
				}
			}
		}
		catch(glare::Exception& e)
		{
			conPrint("ChunkGenThread error while processing texture for ob: " + e.what());
		}
	}


	std::vector<BinRect> rects;

	// Do a pass over used textures to assign binning rectangles
	for(auto it = tex_map_infos.begin(); it != tex_map_infos.end(); ++it)
	{
		TexMapInfo& tex_info = it->second;

		const int bin_rect_index = (int)rects.size();
		tex_info.bin_rect_index = bin_rect_index;

		BinRect rect;
		rect.w = tex_info.largest_use_tex_w;
		rect.h = tex_info.largest_use_tex_h;

		rects.push_back(rect);
	}


	// Pack texture rectangles together
	if(!rects.empty())
		ShelfPack::shelfPack(rects);

	const int atlas_map_w = 2048;
	ImageMapUInt8Ref atlas_map = new ImageMapUInt8(atlas_map_w, atlas_map_w, 3);
	atlas_map->zero();

	// Do another pass to copy the actual texture data
	for(auto it = tex_map_infos.begin(); it != tex_map_infos.end(); ++it)
	{
		TexMapInfo& tex_info = it->second;

		const BinRect& rect = rects[tex_info.bin_rect_index];

		/*
		0 1 2

		3 4 5

		6 7 8
		*/
		Matrix3f mat = Matrix3f::identity();
		mat.e[0] = rect.rotatedWidth()  * rect.scale.x;
		mat.e[4] = rect.rotatedHeight() * rect.scale.y;

		if(rect.rotated)
			mat = mat * Matrix3f(Vec3f(0, 1, 0), Vec3f(1, 0, 0), Vec3f(0, 0, 1)); // swap x and y

		// Translate UVs to correct part of atlas
		mat.e[2] = rect.pos.x;
		mat.e[5] = rect.pos.y;


		tex_info.to_atlas_coords_matrix = mat;

		try
		{
			const std::string tex_path = tex_info.tex_path;
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


			const int rotated_output_pixel_w = (int)(rect.rotatedWidth()  * rect.scale.x * atlas_map_w);
			const int rotated_output_pixel_h = (int)(rect.rotatedHeight() * rect.scale.y * atlas_map_w);

			if(rotated_output_pixel_w > 0 && rotated_output_pixel_h > 0)
			{
				int unrotated_output_pixel_w = rotated_output_pixel_w;
				int unrotated_output_pixel_h = rotated_output_pixel_h;
				if(rect.rotated)
					mySwap(unrotated_output_pixel_w, unrotated_output_pixel_h);

				// Resize image down
				Reference<Map2D> resized_map = imagemap->resizeMidQuality(unrotated_output_pixel_w, unrotated_output_pixel_h, &task_manager);

				runtimeCheck(resized_map.isType<ImageMapUInt8>());
				ImageMapUInt8Ref resized_map_uint8 = resized_map.downcast<ImageMapUInt8>();

				if(resized_map_uint8->numChannels() > 3)
					resized_map_uint8 = resized_map_uint8->extract3ChannelImage();

				if(rect.rotated)
					resized_map_uint8 = resized_map_uint8->rotateCounterClockwise();


				/*
				pixel coords:                                 uv cooords

				0      ---------------------                  1
				1      |                   |
				2      |                   |
				       |                   |
				       |                   |
				       |                   |
				2048   ------------------------------->       0
				*/

				const int dest_left_x = (int)(atlas_map_w * rect.pos.x);
				const int dest_bot_y = (int)(atlas_map_w * (1.f - rect.pos.y));
				const int dest_top_y = dest_bot_y - (int)resized_map_uint8->getHeight();

				resized_map_uint8->blitToImage(*atlas_map, /*dest_x=*/dest_left_x, /*dest_y=*/dest_top_y);
			}

		}
		catch(glare::Exception& e)
		{
			conPrint("ChunkGenThread error while processing texture for ob: " + e.what());
		}
	}

	PNGDecoder::write(*atlas_map, "d:/tempfiles/atlas_256_" + toString(chunk_x) + "_" + toString(chunk_y) + ".png");


	// Create combined mesh

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
	js::AABBox aabb_os = js::AABBox::emptyAABBox(); // AABB of combined mesh

	for(size_t ob_i=0; ob_i<ob_infos.size(); ++ob_i)
	{
		ObInfo& ob_info = ob_infos[ob_i];

		try
		{
			BatchedMeshRef mesh;
			if(ob_info.object_type == WorldObject::ObjectType_Generic)
			{
				if(!ob_info.model_path.empty())
				{
					mesh = LODGeneration::loadModel(ob_info.model_path);
				}
			}
			else if(ob_info.object_type == WorldObject::ObjectType_VoxelGroup)
			{
				if(ob_info.compressed_voxels.size() > 0)
				{
					js::Vector<bool, 16> mat_transparent; // TEMP HACK

					VoxelGroup voxel_group;
					WorldObject::decompressVoxelGroup(ob_info.compressed_voxels.data(), ob_info.compressed_voxels.size(), /*mem_allocator=*/NULL, voxel_group); // TEMP use mem allocator
					Indigo::MeshRef indigo_mesh = VoxelMeshBuilding::makeIndigoMeshForVoxelGroup(voxel_group, /*subsample_factor=*/1, /*generate_shading_normals=*/true, mat_transparent, /*mem_allocator=*/NULL);

					mesh = BatchedMesh::buildFromIndigoMesh(*indigo_mesh);
				}
			}


			if(mesh.nonNull())
			{
				const Matrix4f ob_to_world = ob_info.ob_to_world;
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
					// Since we will be using the atlas texture map instead of individual textures, we will need to adjust the vertex UVs.
									
					//TEMP: just use uv transformation matrix for mat 0 for this object
					//Matrix3f uv_matrix = Matrix3f::identity();
					//auto res = tex_coord_mapping.find(std::make_pair(ob_i, /*mat_i=*/0));
					//if(res != tex_coord_mapping.end())
					//{
					//	const TexMapInfo& info = tex_map_infos[res->second];
					//	uv_matrix = info.matrix;
					//}

					Matrix3f uv_matrix = Matrix3f::identity();

					//TEMP: just use uv transformation matrix for mat 0 for this object
					const std::string tex_path = ob_info.mat_info[/*mat index=*/0].tex_path;

					auto res = tex_map_infos.find(tex_path);
					if(res != tex_map_infos.end())
					{
						TexMapInfo& tex_info = res->second;

						uv_matrix = tex_info.to_atlas_coords_matrix;
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
	const std::string path = "d:/tempfiles/chunk_256_" + toString(chunk_x) + "_" + toString(chunk_y) + ".bmesh";
	combined_mesh->writeToFile(path);

	conPrint("Wrote chunk mesh to '" + path + "'.");
}


void buildChunk(ServerAllWorldsState* world_state, Reference<ServerWorldState> world, const js::AABBox chunk_aabb, int chunk_x, int chunk_y, glare::TaskManager& task_manager)
{
	std::vector<ObInfo> ob_infos;

	{
		WorldStateLock lock(world_state->mutex);
		ServerWorldState::ObjectMapType& objects = world->getObjects(lock);
		for(auto it = objects.begin(); it != objects.end(); ++it)
		{
			WorldObjectRef ob = it->second;
			if(chunk_aabb.contains(ob->getCentroidWS())) // ob->pos.toVec4fPoint()))
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
					if(!isFinite(ob->angle))
						ob->angle = 0;

					if(/*!isFinite(ob->angle) || */!ob->axis.isFinite())
					{
						//	throw glare::Exception("Invalid angle or axis");
					}
					else
					{
						ObInfo ob_info;

						if(!ob->model_url.empty())
							ob_info.model_path = world_state->resource_manager->pathForURL(ob->model_url);

						ob_info.ob_to_world = obToWorldMatrix(*ob);
						ob_info.object_type = ob->object_type;
						ob_info.aabb_ws = ob->getAABBWS();

						ob_info.mat_info.resize(ob->materials.size());

						for(size_t i=0; i<ob->materials.size(); ++i)
						{
							WorldMaterial* mat = ob->materials[i].ptr();

							if(!mat->colour_texture_url.empty())
							{
								const std::string tex_path = world_state->resource_manager->pathForURL(mat->colour_texture_url);
								ob_info.mat_info[i].tex_path = tex_path;
							}
						}


						ob_infos.push_back(ob_info);
					}
				}
			}
		}
	} // End lock scope.


	buildChunkForObInfo(ob_infos, chunk_x, chunk_y, task_manager);
}



void ChunkGenThread::doRun()
{
	PlatformUtils::setCurrentThreadName("ChunkGenThread");

	glare::TaskManager task_manager;

	Timer timer;

	try
	{
		while(1)
		{
			Reference<ServerWorldState> root_world_state = world_state->getRootWorldState();

			const float chunk_w = 256;
			for(int x=0; x<1; ++x)
			for(int y=0; y<1; ++y)
			{
				// Compute chunk AABB
				const js::AABBox chunk_aabb(
					Vec4f(x * chunk_w, y * chunk_w, -1000.f, 1.f), // min
					Vec4f((x + 1) * chunk_w, (y + 1) * chunk_w, 1000.f, 1.f) // max
				);

				buildChunk(world_state, root_world_state, chunk_aabb, x, y, task_manager);
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
