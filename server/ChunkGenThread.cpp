/*=====================================================================
ChunkGenThread.cpp
------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "ChunkGenThread.h"


#include "Server.h"
#include "ServerWorldState.h"
#include "../shared/LODGeneration.h"
#include "../shared/MessageUtils.h"
#include "../shared/VoxelMeshBuilding.h"
#include "../shared/ImageDecoding.h"
#include "../shared/Protocol.h"
#include <graphics/MeshSimplification.h>
#include <graphics/GifDecoder.h>
#include <graphics/ImageMapSequence.h>
#include <graphics/Map2D.h>
#include <graphics/ImageMap.h>
#include <graphics/SRGBUtils.h>
#include <graphics/FormatDecoderGLTF.h>
#include <dll/include/IndigoMesh.h>
#include <dll/include/IndigoException.h>
#include <dll/IndigoStringUtils.h>
#include <utils/ConPrint.h>
#include <utils/Exception.h>
#include <utils/Lock.h>
#include <utils/StringUtils.h>
#include <utils/PlatformUtils.h>
#include <utils/Timer.h>
#include <utils/TaskManager.h>
#include <utils/IncludeHalf.h>
#include <utils/RuntimeCheck.h>
#include <utils/FileOutStream.h>
#include <utils/FileUtils.h>
#include <utils/LRUCache.h>
#include <maths/matrix3.h>
#include <SocketBufferOutStream.h>
#if !GUI_CLIENT
#include <encoder/basisu_comp.h>
#endif
#include <zstd.h>
#include <FileChecksum.h>


static const float chunk_w = 128;


ChunkGenThread::ChunkGenThread(Server* server_, ServerAllWorldsState* all_worlds_state_)
:	server(server_), all_worlds_state(all_worlds_state_)
{
}


ChunkGenThread::~ChunkGenThread()
{
}


struct MatInfo
{
	std::string tex_path;
	WorldMaterialRef world_mat;
	Matrix2f tex_matrix;
	float emission_lum_flux_or_lum;
	float roughness;
	float metallic;
	Colour3f colour_rgb; // non-linear
	float opacity;
};

struct ObInfo
{
	Matrix4f ob_to_world;
	js::AABBox aabb_ws;
	std::string model_path;
	Reference<glare::SharedImmutableArray<uint8> > compressed_voxels;
	uint32 object_type;
	std::vector<MatInfo> mat_info;
	float ob_to_world_scale;
	UID ob_uid;
};

struct OutputMatInfo
{
	// From WorldMaterial:
	Matrix2f tex_matrix_col_major; // 0 - 3
	float emission_lum_flux_or_lum; // 4
	float roughness; // 5
	float metallic; // 6
	Colour3f linear_colour_rgb; // 7, 8, 9

	float flags; // 10
	float array_image_index; // 11
};


// Sub-range of the indices from the LOD chunk geometry that correspond to the given object.
struct ObjectBatchRanges
{
	ObjectBatchRanges() : ob_uid(UID::invalidUID()), batch0_start(0), batch0_end(0), batch1_start(0), batch1_end(0) {}
	UID ob_uid;
	uint32 batch0_start;
	uint32 batch0_end;
	uint32 batch1_start;
	uint32 batch1_end;
};


struct ChunkBuildResults
{
	js::Vector<ObjectBatchRanges> ob_batch_ranges;
	js::Vector<OutputMatInfo> output_mat_infos;
	std::string combined_mesh_path;
	uint64 combined_mesh_hash;
	std::string optimised_mesh_path;
	std::string combined_texture_path;
	uint64 combined_texture_hash;
};


// NOTE: code duplicated from ModelLoading.cpp
static inline Colour3f sanitiseAlbedoColour(const Colour3f& col)
{
	const Colour3f clamped = col.clamp(0.f, 1.f);
	if(clamped.isFinite()) // Check for NaN components
		return clamped;
	else
		return Colour3f(0.2f);
}


static inline Colour3f sanitiseAndConvertToLinearAlbedoColour(const Colour3f& col)
{
	return toLinearSRGB(sanitiseAlbedoColour(col));
}



// NOTE: code duplicated from PhysicsWorld.cpp.  Factor out?
inline static Vec4f transformSkinnedVertex(const Vec4f vert_pos, size_t joint_offset_B, size_t weights_offset_B, BatchedMesh::ComponentType joints_component_type, BatchedMesh::ComponentType weights_component_type,
	const js::Vector<Matrix4f, 16>& joint_matrices, const uint8* src_vertex_data, const size_t vert_size_B, size_t i)
{
	// Read joint indices
	uint32 use_joints[4];
	if(joints_component_type == BatchedMesh::ComponentType_UInt8)
	{
		uint8 joints[4];
		std::memcpy(joints, &src_vertex_data[i * vert_size_B + joint_offset_B], sizeof(uint8) * 4);
		for(int z=0; z<4; ++z)
			use_joints[z] = joints[z];
	}
	else
	{
		runtimeCheck(joints_component_type == BatchedMesh::ComponentType_UInt16);

		uint16 joints[4];
		std::memcpy(joints, &src_vertex_data[i * vert_size_B + joint_offset_B], sizeof(uint16) * 4);
		for(int z=0; z<4; ++z)
			use_joints[z] = joints[z];
	}

	// Read weights
	float use_weights[4];
	if(weights_component_type == BatchedMesh::ComponentType_UInt8)
	{
		uint8 weights[4];
		std::memcpy(weights, &src_vertex_data[i * vert_size_B + weights_offset_B], sizeof(uint8) * 4);
		for(int z=0; z<4; ++z)
			use_weights[z] = weights[z] * (1.0f / 255.f);
	}
	else if(weights_component_type == BatchedMesh::ComponentType_UInt16)
	{
		uint16 weights[4];
		std::memcpy(weights, &src_vertex_data[i * vert_size_B + weights_offset_B], sizeof(uint16) * 4);
		for(int z=0; z<4; ++z)
			use_weights[z] = weights[z] * (1.0f / 65535.f);
	}
	else
	{
		runtimeCheck(weights_component_type == BatchedMesh::ComponentType_Float);

		std::memcpy(use_weights, &src_vertex_data[i * vert_size_B + weights_offset_B], sizeof(float) * 4);
	}

	for(int z=0; z<4; ++z)
		assert(use_joints[z] < (uint32)joint_matrices.size());
	
	return
		joint_matrices[use_joints[0]] * vert_pos * use_weights[0] + // joint indices should have been bound checked in BatchedMesh::checkValidAndSanitiseMesh()
		joint_matrices[use_joints[1]] * vert_pos * use_weights[1] + 
		joint_matrices[use_joints[2]] * vert_pos * use_weights[2] + 
		joint_matrices[use_joints[3]] * vert_pos * use_weights[3];
}


static BatchedMeshRef simplerMesh(BatchedMeshRef a, BatchedMeshRef b)
{
	return a->numIndices() < b->numIndices() ? a : b;
}


// May return null mesh if there were no voxels or mesh was simplified away.
// May also return mesh with zero indices.
BatchedMeshRef loadAndSimplifyGeometry(const ObInfo& ob_info, LRUCache<std::string, BatchedMeshRef>& mesh_cache, Matrix4f& voxel_scale_matrix_out)
{
	float voxel_scale = 1.f;
	voxel_scale_matrix_out = Matrix4f::identity();

	BatchedMeshRef mesh;
	if(ob_info.object_type == WorldObject::ObjectType_Generic)
	{
		if(!ob_info.model_path.empty())
		{
			auto res = mesh_cache.find(ob_info.model_path);
			if(res == mesh_cache.end())
			{
				conPrint("ChunkGenThread: Loading '" + ob_info.model_path + "'...");
				mesh = LODGeneration::loadModel(ob_info.model_path);

				mesh_cache.insert(std::make_pair(ob_info.model_path, mesh), mesh->getTotalMemUsage());

				// conPrint("New cache total size: " + toString(mesh_cache.totalValueSizeB()) + " B");

				// Clear out old items from cache if needed, so that total cache size is < 256 MB.
				mesh_cache.removeLRUItemsUntilSizeLessEqualN(256 * 1024 * 1024);
			}
			else
			{
				// conPrint("Using '" + ob_info.model_path + "' from cache.");

				// already in cache
				mesh = res->second.value;
				mesh_cache.itemWasUsed(ob_info.model_path);
			}
		}
	}
	else if(ob_info.object_type == WorldObject::ObjectType_VoxelGroup)
	{
		if(ob_info.compressed_voxels && (ob_info.compressed_voxels->size() > 0))
		{
			js::Vector<bool, 16> mat_transparent(ob_info.mat_info.size());
			for(size_t i=0; i<mat_transparent.size(); ++i)
				mat_transparent[i] = ob_info.mat_info[i].opacity < 1.f;

			VoxelGroup voxel_group;
			WorldObject::decompressVoxelGroup(ob_info.compressed_voxels->data(), ob_info.compressed_voxels->size(), /*mem_allocator=*/NULL, voxel_group); // TEMP use mem allocator

			assert(voxel_group.voxels.size() > 0);

			int subsample_factor = 1;
			if(voxel_group.voxels.size() > 64)
				subsample_factor = 2;
			Indigo::MeshRef indigo_mesh = VoxelMeshBuilding::makeIndigoMeshWithShadingNormalsForVoxelGroup(voxel_group, 
				subsample_factor, mat_transparent, /*mem_allocator=*/NULL);

			mesh = BatchedMesh::buildFromIndigoMesh(*indigo_mesh);

			voxel_scale_matrix_out = Matrix4f::uniformScaleMatrix((float)subsample_factor);
		}
	}

	// Simplify mesh
	if(mesh)
	{
		//conPrint("ChunkGenThread: Simplifying mesh..");

		//const size_t original_num_tris = mesh->numIndices()/3;

		// NOTE: This code is pretty similar to LODGeneration::computeLODModel(), with a slightly more world-space focus.

		// Chunks are displayed >= 150 m away from the camera.
		// For a render resolution of 2560 x 1282 pixels,
		// pixel/h = 2560 / (w/l) = 1828.571428 pixels/projected_len_h
		// So a world-space error of 0.4 m gives a projected length of 0.4 m / 150 m = 0.002666
		// So this corresponds to an error of 0.002666 * pixel/h = 0.002666 * 1828.57142 = 4.87 pixels.

		const float error_threshold_ws = 0.4f; // absolute error threshold in world space
		const float error_threshold_os_abs = error_threshold_ws / (ob_info.ob_to_world_scale * voxel_scale); // absolute error threshold in object space
		const size_t sloppy_tri_threshold = 1500; // Number of tris in the non-sloppy simplified mesh at which we should also try using sloppy simplification.

		mesh = MeshSimplification::removeSmallComponents(mesh, error_threshold_os_abs);
		if(mesh->numIndices() == 0)
		{
			//conPrint("\tChunkGenThread: removeSmallComponents() removed all tris.");
			return mesh;
		}

		// NOTE: compute the relative error threshold after removeSmallComponents() as removeSmallComponents() may change the mesh AABB.
		const float error_threshold_os_rel = error_threshold_os_abs / mesh->aabb_os.longestLength(); // final relative error threshold, in object space.

		// conPrint("\tChunkGenThread: error_threshold_os_abs: " + doubleToStringNDecimalPlaces(error_threshold_os_abs, 3) + ", error_threshold_os_rel: " + doubleToStringNDecimalPlaces(error_threshold_os_rel, 3));

		BatchedMeshRef simplified_mesh = MeshSimplification::buildSimplifiedMesh(*mesh, /*target_reduction_ratio=*/100000.f, /*target_error=*/error_threshold_os_abs, /*sloppy=*/false);
		
		//conPrint("\tChunkGenThread: simplified_mesh num tris: " + uInt64ToStringCommaSeparated(simplified_mesh->numIndices() / 3) + " (original_num_tris: " + uInt64ToStringCommaSeparated(original_num_tris) + ")");

		// If the simplified mesh is still quite complex, try again with sloppy simplification.
		if((simplified_mesh->numIndices()/3) > sloppy_tri_threshold)
		{
			BatchedMeshRef sloppy_mesh = MeshSimplification::buildSimplifiedMesh(*mesh, /*target_reduction_ratio=*/100000.f, /*target_error (relative)=*/error_threshold_os_rel, /*sloppy=*/true);

			//conPrint("\tChunkGenThread: Tried sloppy simplification, sloppy_mesh num tris: " + uInt64ToStringCommaSeparated(sloppy_mesh->numIndices() / 3) + " (original_num_tris: " + uInt64ToStringCommaSeparated(original_num_tris) + ")");

			return simplerMesh(sloppy_mesh, simplerMesh(simplified_mesh, mesh)); // Return the mesh that actually ended up the most simple.
		}
		else
		{
			return simplerMesh(simplified_mesh, mesh);  // Return the mesh that actually ended up the most simple.
		}
	}
	else
		return nullptr;
}


static void buildAndSaveArrayTexture(const std::vector<std::string>& used_tex_paths, glare::TaskManager& task_manager, int chunk_x, int chunk_y, std::map<std::string, int>& array_image_indices_out,
	std::string& combined_texture_path_out, uint64& combined_texture_hash_out)
{
	if(!used_tex_paths.empty())
	{
		basisu::basisu_encoder_init(); // Can be called multiple times harmlessly.
		basisu::basis_compressor_params params;

		for(auto it = used_tex_paths.begin(); it != used_tex_paths.end(); ++it)
		{
			try
			{
				const std::string tex_path = *it;

				conPrint("ChunkGenThread: Loading '" + tex_path + "'...");
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
					throw glare::Exception("Unhandled texture type (not ImageMapUInt8 or ImageMapSequenceUInt8).");


				const int new_W = 64;

				// Resize image down
				Reference<Map2D> resized_map = imagemap->resizeMidQuality(new_W, new_W, &task_manager);

				runtimeCheck(resized_map.isType<ImageMapUInt8>());
				ImageMapUInt8Ref resized_map_uint8 = resized_map.downcast<ImageMapUInt8>();

				if(resized_map_uint8->numChannels() > 3)
					resized_map_uint8 = resized_map_uint8->extract3ChannelImage();

				if(resized_map_uint8->numChannels() < 3)
				{
					ImageMapUInt8Ref new_map = new ImageMapUInt8(new_W, new_W, 3);
					for(size_t i=0; i<new_W * new_W; ++i)
						new_map->getPixel(i)[0] = new_map->getPixel(i)[1] = new_map->getPixel(i)[2] = resized_map_uint8->getPixel(i)[0];

					resized_map_uint8 = new_map;
				}

				basisu::image img(resized_map_uint8->getData(), (uint32)new_W, (uint32)new_W, (uint32)3);

				//tex_info.array_image_index = params.m_source_images.size();
				array_image_indices_out[tex_path] = (int)params.m_source_images.size();

				params.m_source_images.push_back(img);
			}
			catch(glare::Exception& e)
			{
				conPrint("ChunkGenThread: Error while loading image: " + e.what());
			}
		}


		if(!params.m_source_images.empty())
		{
			Timer timer;

			params.m_tex_type = basist::cBASISTexType2DArray;
		
			params.m_perceptual = true;

			params.m_status_output = false;
	
			params.m_write_output_basis_or_ktx2_files = true;
			params.m_out_filename = PlatformUtils::getTempDirPath() + "/chunk_array_texture_" + toString(chunk_x) + "_" + toString(chunk_y) + "_q128.basis";
			//params.m_out_filename = "d:/tempfiles/main_world/chunk_array_texture_" + toString(chunk_x) + "_" + toString(chunk_y) + ".basis";
			params.m_create_ktx2_file = false;

			params.m_mip_gen = true; // Generate mipmaps for each source image
			params.m_mip_srgb = true; // Convert image to linear before filtering, then back to sRGB

			params.m_etc1s_quality_level = 128;

			basisu::job_pool jpool(PlatformUtils::getNumLogicalProcessors());
			params.m_pJob_pool = &jpool;

			basisu::basis_compressor basisCompressor;
			basisu::enable_debug_printf(false);

			const bool res = basisCompressor.init(params);
			if(!res)
				throw glare::Exception("Failed to create basisCompressor");

			basisu::basis_compressor::error_code result = basisCompressor.process();

			if(result != basisu::basis_compressor::cECSuccess)
				throw glare::Exception("basisCompressor.process() failed.");

			conPrint("ChunkGenThread: Basisu compression and writing of file to '" + params.m_out_filename + "' took " + timer.elapsedStringNSigFigs(3));

			// Compute hash over it
			const uint64 hash = FileChecksum::fileChecksum(params.m_out_filename);

			combined_texture_path_out = params.m_out_filename;
			combined_texture_hash_out = hash;
		}
		else
			conPrint("ChunkGenThread: Not writing texture array, no textures to process.");
	}
	else
		conPrint("ChunkGenThread: Not writing texture array, no textures to process.");
}


static ChunkBuildResults buildChunkForObInfo(std::vector<ObInfo>& ob_infos, int chunk_x, int chunk_y, glare::TaskManager& task_manager)
{
	ChunkBuildResults results;
	results.ob_batch_ranges.resize(ob_infos.size());

	LRUCache<std::string, BatchedMeshRef> mesh_cache;

	//-------------------------- Create combined mesh -----------------------------
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
	combined_mesh->vert_attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_UV_0, BatchedMesh::ComponentType_Half, /*offset_B=*/offset));
	offset += BatchedMesh::vertAttributeSize(combined_mesh->vert_attributes.back());

	const size_t combined_mesh_mat_index_offset_B = offset;
	combined_mesh->vert_attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_MatIndex, BatchedMesh::ComponentType_UInt32, /*offset_B=*/offset));
	offset += BatchedMesh::vertAttributeSize(combined_mesh->vert_attributes.back());

	const size_t combined_mesh_vert_size = offset;

	js::Vector<uint32, 16> combined_opaque_indices; // Vertex indices of triangles with an opaque material assigned.
	js::Vector<uint32, 16> combined_trans_indices; // Vertex indices of triangles with a transparent material assigned.
	js::AABBox aabb_os = js::AABBox::emptyAABBox(); // AABB of combined mesh

	size_t num_obs_combined = 0;
	size_t num_batches_combined = 0;

	std::vector<MatInfo> combined_mat_infos;

	// const Vec4f chunk_coords_origin = Vec4f((chunk_x + 0.5f) * chunk_w, (chunk_y + 0.5f) * chunk_w, 0, 1);

	for(size_t ob_i=0; ob_i<ob_infos.size(); ++ob_i)
	{
		ObInfo& ob_info = ob_infos[ob_i];

		results.ob_batch_ranges[ob_i].ob_uid = ob_info.ob_uid;
		results.ob_batch_ranges[ob_i].batch0_start = 0;
		results.ob_batch_ranges[ob_i].batch0_end = 0;
		results.ob_batch_ranges[ob_i].batch1_start = 0;
		results.ob_batch_ranges[ob_i].batch1_end = 0;

		const size_t initial_combined_mesh_vert_data_size = combined_mesh->vertex_data.size();
		const size_t initial_combined_opaque_indices_size = combined_opaque_indices.size();
		const size_t initial_combined_trans_indices_size  = combined_trans_indices.size();

		try
		{
			Matrix4f voxel_scale_matrix;
			BatchedMeshRef mesh = loadAndSimplifyGeometry(ob_info, mesh_cache, /*voxel_scale_matrix_out=*/voxel_scale_matrix);
			
			if(mesh.nonNull() && (mesh->numIndices() > 0))
			{
				// The WorldObject material array can be smaller than the number of materials referenced
				// by the mesh.  In this case we need to add some default/dummy materials.
				// See also ModelLoading::makeGLObjectForMeshDataAndMaterials.
				const size_t num_mats_referenced = mesh->numMaterialsReferenced();
				if(ob_info.mat_info.size() < num_mats_referenced)
				{
					MatInfo dummy;
					dummy.colour_rgb = Colour3f(0.7f);
					dummy.emission_lum_flux_or_lum = 0;
					dummy.roughness = 0.5f;
					dummy.metallic = 0;
					dummy.opacity = 0;
					ob_info.mat_info.resize(num_mats_referenced, dummy);
				}

				const int object_combined_mat_infos_offset = (int)combined_mat_infos.size();

				for(size_t m=0; m<ob_info.mat_info.size(); ++m)
					combined_mat_infos.push_back(ob_info.mat_info[m]);


				const size_t num_verts = mesh->numVerts();
				const size_t vert_stride_B = mesh->vertexSize();

				// If mesh has joints and weights, take the skinning transform into account.
				// NOTE: Code duplicated from PhysicsWorld::createJoltShapeForBatchedMesh().  Factor out?
				const AnimationData& anim_data = mesh->animation_data;

				const bool use_skin_transforms = mesh->findAttribute(BatchedMesh::VertAttribute_Joints) && mesh->findAttribute(BatchedMesh::VertAttribute_Weights) &&
					!anim_data.joint_nodes.empty();

				js::Vector<Matrix4f, 16> joint_matrices;

				size_t joint_offset_B, weights_offset_B;
				BatchedMesh::ComponentType joints_component_type, weights_component_type;
				joint_offset_B = weights_offset_B = 0;
				joints_component_type = weights_component_type = BatchedMesh::ComponentType_UInt8;
				if(use_skin_transforms)
				{
					js::Vector<Matrix4f, 16> node_matrices;

					const size_t num_nodes = anim_data.sorted_nodes.size();
					node_matrices.resizeNoCopy(num_nodes);

					for(size_t n=0; n<anim_data.sorted_nodes.size(); ++n)
					{
						const int node_i = anim_data.sorted_nodes[n];
						runtimeCheck(node_i >= 0 && node_i < (int)anim_data.nodes.size()); // All these indices should have been bound checked in BatchedMesh::readFromData(), check again anyway.
						const AnimationNodeData& node_data = anim_data.nodes[node_i];
						const Vec4f trans = node_data.trans;
						const Quatf rot = node_data.rot;
						const Vec4f scale = node_data.scale;

						const Matrix4f rot_mat = rot.toMatrix();
						const Matrix4f TRS(
							rot_mat.getColumn(0) * copyToAll<0>(scale),
							rot_mat.getColumn(1) * copyToAll<1>(scale),
							rot_mat.getColumn(2) * copyToAll<2>(scale),
							setWToOne(trans));

						runtimeCheck(node_data.parent_index >= -1 && node_data.parent_index < (int)node_matrices.size());
						const Matrix4f node_transform = (node_data.parent_index == -1) ? TRS : (node_matrices[node_data.parent_index] * TRS);
						node_matrices[node_i] = node_transform;
					}

					joint_matrices.resizeNoCopy(anim_data.joint_nodes.size());

					for(size_t i=0; i<anim_data.joint_nodes.size(); ++i)
					{
						const int node_i = anim_data.joint_nodes[i];
						runtimeCheck(node_i >= 0 && node_i < (int)node_matrices.size() && node_i >= 0 && node_i < (int)anim_data.nodes.size());
						joint_matrices[i] = node_matrices[node_i] * anim_data.nodes[node_i].inverse_bind_matrix;
					}

					const BatchedMesh::VertAttribute& joints_attr = mesh->getAttribute(BatchedMesh::VertAttribute_Joints);
					joint_offset_B = joints_attr.offset_B;
					joints_component_type = joints_attr.component_type;
					runtimeCheck(joints_component_type == BatchedMesh::ComponentType_UInt8 || joints_component_type == BatchedMesh::ComponentType_UInt16); // See BatchedMesh::checkValidAndSanitiseMesh().
					runtimeCheck((num_verts - 1) * vert_stride_B + joint_offset_B + BatchedMesh::vertAttributeSize(joints_attr) <= mesh->vertex_data.size());

					const BatchedMesh::VertAttribute& weights_attr = mesh->getAttribute(BatchedMesh::VertAttribute_Weights);
					weights_offset_B = weights_attr.offset_B;
					weights_component_type = weights_attr.component_type;
					runtimeCheck(weights_component_type == BatchedMesh::ComponentType_UInt8 || weights_component_type == BatchedMesh::ComponentType_UInt16 || weights_component_type == BatchedMesh::ComponentType_Float); // See BatchedMesh::checkValidAndSanitiseMesh().
					runtimeCheck((num_verts - 1) * vert_stride_B + weights_offset_B + BatchedMesh::vertAttributeSize(weights_attr) <= mesh->vertex_data.size());
				}




				const Matrix4f ob_to_world = ob_info.ob_to_world * voxel_scale_matrix;
				Matrix4f ob_normals_to_world;
				const bool invertible = ob_to_world.getUpperLeftInverseTranspose(ob_normals_to_world);
				if(!invertible)
				{
					conPrint("ChunkGenThread: Warning: ob_to_world not invertible.");
					ob_normals_to_world = ob_to_world;
				}

				// Allocate room for new verts
				const size_t write_i_B = combined_mesh->vertex_data.size();
				combined_mesh->vertex_data.resize(write_i_B + mesh->numVerts() * combined_mesh_vert_size);

				const BatchedMesh::VertAttribute& pos = mesh->getAttribute(BatchedMesh::VertAttribute_Position);
				if(pos.component_type != BatchedMesh::ComponentType_Float)
					throw glare::Exception("unhandled pos component type");

				

				//------------------------------------------ Copy vert indices ------------------------------------------
				const uint32 vert_offset = (uint32)(write_i_B / combined_mesh_vert_size);

				results.ob_batch_ranges[ob_i].batch0_start = (uint32)combined_opaque_indices.size();
				results.ob_batch_ranges[ob_i].batch1_start = (uint32)combined_trans_indices.size();


				const size_t num_indices = mesh->numIndices();

				// We need to know what material is assigned to each vertex, for the 'original material index' vertex attribute.
				// We will compute this by splatting the material assignment for each vert.  Note that multiple batches with different materials may share the same vertex.
				std::vector<uint32> vert_combined_mat_index(num_verts);

				std::vector<uint32> new_combined_indices;
				new_combined_indices.reserve(num_indices);

				for(size_t b=0; b<mesh->batches.size(); ++b)
				{
					const BatchedMesh::IndicesBatch& batch = mesh->batches[b];

					const bool mat_opaque = ob_info.mat_info[batch.material_index].opacity == 1.f;

					js::Vector<uint32, 16>& dest_combined_indices = mat_opaque ? combined_opaque_indices : combined_trans_indices;

					const uint32 combined_mat_index = object_combined_mat_infos_offset + batch.material_index;

					if(mesh->index_type == BatchedMesh::ComponentType_UInt8)
					{
						for(size_t z = batch.indices_start; z < batch.indices_start + batch.num_indices; ++z)
						{
							const uint32 vert_index = ((const uint8*)mesh->index_data.data())[z]; // Index of the vertex in mesh
							
							vert_combined_mat_index[vert_index] = combined_mat_index;

							dest_combined_indices.push_back(vert_offset + vert_index);
							new_combined_indices.push_back(vert_offset + vert_index);
						}
					}
					else if(mesh->index_type == BatchedMesh::ComponentType_UInt16)
					{
						for(size_t z = batch.indices_start; z < batch.indices_start + batch.num_indices; ++z)
						{
							const uint32 vert_index = ((const uint16*)mesh->index_data.data())[z]; // Index of the vertex in mesh

							vert_combined_mat_index[vert_index] = combined_mat_index;

							dest_combined_indices.push_back(vert_offset + vert_index);
							new_combined_indices.push_back(vert_offset + vert_index);
						}
					}
					else if(mesh->index_type == BatchedMesh::ComponentType_UInt32)
					{
						for(size_t z = batch.indices_start; z < batch.indices_start + batch.num_indices; ++z)
						{
							const uint32 vert_index = ((const uint32*)mesh->index_data.data())[z]; // Index of the vertex in mesh

							vert_combined_mat_index[vert_index] = combined_mat_index;

							dest_combined_indices.push_back(vert_offset + vert_index);
							new_combined_indices.push_back(vert_offset + vert_index);
						}
					}
					else
						throw glare::Exception("unhandled index_type");
				}


				results.ob_batch_ranges[ob_i].batch0_end = (uint32)combined_opaque_indices.size();
				results.ob_batch_ranges[ob_i].batch1_end = (uint32)combined_trans_indices.size();


				//------------------------------------------ Set material index vertex attribute values ------------------------------------------
				// Copy into combined mesh data
				for(size_t i = 0; i < num_verts; ++i)
				{
					const uint32 combined_mat_index = vert_combined_mat_index[i];
					std::memcpy(&combined_mesh->vertex_data[write_i_B + combined_mesh_vert_size * i + combined_mesh_mat_index_offset_B], &combined_mat_index, sizeof(uint32));
				}
				
				//------------------------------------------ Copy vertex positions ------------------------------------------
				const uint8* const src_vertex_data = mesh->vertex_data.data();
				for(size_t i = 0; i < num_verts; ++i)
				{
					runtimeCheck(vert_stride_B * i + pos.offset_B + sizeof(Vec3f) <= mesh->vertex_data.size());

					Vec3f v;
					std::memcpy(&v, &mesh->vertex_data[vert_stride_B * i + pos.offset_B], sizeof(Vec3f));

					Vec4f v_os = v.toVec4fPoint();
					if(use_skin_transforms)
						v_os = transformSkinnedVertex(v_os, joint_offset_B, weights_offset_B, joints_component_type, weights_component_type, joint_matrices, src_vertex_data, vert_stride_B, i);

					// Compute world-space vertex position
					const Vec4f v_ws = ob_to_world * v_os;

					const Vec4f v_chunksp = v_ws;// - chunk_coords_origin; // Compute chunk-space position

					aabb_os.enlargeToHoldPoint(v_chunksp);

					std::memcpy(&combined_mesh->vertex_data[write_i_B + combined_mesh_vert_size * i], v_chunksp.x, sizeof(Vec3f));
				}

				//------------------------------------------ Copy or compute vertex normals ------------------------------------------
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

							Vec4f n = batchedMeshUnpackNormal(packed_normal);

							if(use_skin_transforms)
								// TEMP: just use to-world matrix instead of inverse transpose.
								n = transformSkinnedVertex(n, joint_offset_B, weights_offset_B, joints_component_type, weights_component_type, joint_matrices, src_vertex_data, vert_stride_B, i);

							const Vec4f new_n = normalise(ob_normals_to_world * n);

							const uint32 new_packed_normal = batchedMeshPackNormal(new_n);

							std::memcpy(&combined_mesh->vertex_data[write_i_B + combined_mesh_vert_size * i + combined_mesh_normal_offset_B], &new_packed_normal, sizeof(uint32));
						}
					}
					else
						throw glare::Exception("unhandled normal component type");
				}
				else
				{
					//------------------------------------------ Compute shading normals as geometric normals, if no shading normal attribute is present in source mesh ------------------------------------------
					const size_t new_combined_indices_size = new_combined_indices.size();
					runtimeCheck(new_combined_indices_size % 3 == 0);
					for(size_t i=0; i<new_combined_indices_size; i+=3)
					{
						const uint32 v0 = new_combined_indices[i + 0];
						const uint32 v1 = new_combined_indices[i + 1];
						const uint32 v2 = new_combined_indices[i + 2];

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
						runtimeCheck(combined_mesh_vert_size * v0 + combined_mesh_normal_offset_B + sizeof(uint32) <= combined_mesh->vertex_data.size());
						runtimeCheck(combined_mesh_vert_size * v1 + combined_mesh_normal_offset_B + sizeof(uint32) <= combined_mesh->vertex_data.size());
						runtimeCheck(combined_mesh_vert_size * v2 + combined_mesh_normal_offset_B + sizeof(uint32) <= combined_mesh->vertex_data.size());
										
						std::memcpy(&combined_mesh->vertex_data[combined_mesh_vert_size * v0 + combined_mesh_normal_offset_B], &new_packed_normal, sizeof(uint32));
						std::memcpy(&combined_mesh->vertex_data[combined_mesh_vert_size * v1 + combined_mesh_normal_offset_B], &new_packed_normal, sizeof(uint32));
						std::memcpy(&combined_mesh->vertex_data[combined_mesh_vert_size * v2 + combined_mesh_normal_offset_B], &new_packed_normal, sizeof(uint32));
					}
				}

				//------------------------------------------ Copy vertex UV0s ------------------------------------------
				const BatchedMesh::VertAttribute* uv0_attr = mesh->findAttribute(BatchedMesh::VertAttribute_UV_0);
				if(uv0_attr)
				{
					if(uv0_attr->component_type == BatchedMesh::ComponentType_Float)
					{
						for(size_t i = 0; i < num_verts; ++i)
						{
							runtimeCheck(vert_stride_B * i + uv0_attr->offset_B + sizeof(Vec2f) <= mesh->vertex_data.size());

							Vec2f uv;
							std::memcpy(&uv, &mesh->vertex_data[vert_stride_B * i + uv0_attr->offset_B], sizeof(Vec2f));

							// Clamp UVs to some reasonable range, otherwise the 8-bit UV quantisation which is spread across the entire UV 
							// range for the chunk can result in the coordinates 0 and 1 ending up as the same quantised value, which breaks texture mapping
							// for most of the chunk.  The cost is the UV-clamped object will render incorrectly, but it should be worth the cost.
							uv.x = myClamp(uv.x, -4.f, 4.f);
							uv.y = myClamp(uv.y, -4.f, 4.f);

							const half new_uv[2] = {half(uv.x), half(uv.y)};
							std::memcpy(
								/*dest=*/&combined_mesh->vertex_data[write_i_B + combined_mesh_vert_size * i + combined_mesh_uv0_offset_B], 
								/*src=*/&new_uv, 
								/*size=*/sizeof(half) * 2);

							//std::memcpy(
							//	/*dest=*/&combined_mesh->vertex_data[write_i_B + combined_mesh_vert_size * i + combined_mesh_uv0_offset_B], 
							//	/*src=*/&mesh->vertex_data[vert_stride_B * i + uv0_attr->offset_B], 
							//	/*size=*/sizeof(Vec2f));
						}
					}
					else if(uv0_attr->component_type == BatchedMesh::ComponentType_Half)
					{
						for(size_t i = 0; i < num_verts; ++i)
						{
							runtimeCheck(vert_stride_B * i + uv0_attr->offset_B + sizeof(half) * 2 <= mesh->vertex_data.size());

							std::memcpy(
								&combined_mesh->vertex_data[write_i_B + combined_mesh_vert_size * i + combined_mesh_uv0_offset_B], 
								&mesh->vertex_data[vert_stride_B * i + uv0_attr->offset_B], sizeof(half) * 2);

							/*half uv[2];
							std::memcpy(&uv, &mesh->vertex_data[vert_stride_B * i + uv0_attr->offset_B], sizeof(half) * 2);

							const Vec2f new_uv(uv[0], uv[1]);
							std::memcpy(&combined_mesh->vertex_data[write_i_B + combined_mesh_vert_size * i + combined_mesh_uv0_offset_B], &new_uv, sizeof(Vec2f));*/
						}
					}
					else
						throw glare::Exception("unhandled uv0 component type");
				}
				else // else UV0 was not present in source mesh, so just write out (0,0) uvs.
				{
					const half new_uv[2] = {half(0.f), half(0.f)};

					for(size_t i = 0; i < num_verts; ++i)
					{
						//std::memcpy(&combined_mesh->vertex_data[write_i_B + combined_mesh_vert_size * i + combined_mesh_uv0_offset_B], &new_uv, sizeof(Vec2f));
						
						std::memcpy(&combined_mesh->vertex_data[write_i_B + combined_mesh_vert_size * i + combined_mesh_uv0_offset_B], &new_uv, sizeof(half) * 2);
					}
				}

				num_obs_combined++;
				num_batches_combined += mesh->batches.size();

			} // end if(mesh.nonNull())
		}
		catch(glare::Exception& e)
		{
			conPrint("ChunkGenThread: error while processing ob: " + e.what());

			// If an exception was thrown after space was allocated for the mesh verts, we want to trim that off.
			combined_mesh->vertex_data.resize(initial_combined_mesh_vert_data_size);
			combined_opaque_indices.resize(initial_combined_opaque_indices_size);
			combined_trans_indices.resize(initial_combined_trans_indices_size);

			results.ob_batch_ranges[ob_i].batch0_start = 0;
			results.ob_batch_ranges[ob_i].batch0_end = 0;
			results.ob_batch_ranges[ob_i].batch1_start = 0;
			results.ob_batch_ranges[ob_i].batch1_end = 0;
		}
	} // End for each ob

	
	js::Vector<uint32> combined_indices = combined_opaque_indices;
	combined_indices.append(combined_trans_indices);

	for(size_t z=0; z<results.ob_batch_ranges.size(); ++z)
	{
		results.ob_batch_ranges[z].batch1_start += (uint32)combined_opaque_indices.size();
		results.ob_batch_ranges[z].batch1_end   += (uint32)combined_opaque_indices.size();
	}

	// Check combined indices are in-bounds
	{
		const size_t combined_num_verts = combined_mesh->numVerts();
		for(size_t i=0; i<combined_indices.size(); ++i)
			runtimeCheck(combined_indices[i] < combined_num_verts);
	}

	if(!combined_indices.empty())
	{
		combined_mesh->setIndexDataFromIndices(combined_indices, combined_mesh->numVerts());

		if(!combined_opaque_indices.empty())
		{
			BatchedMesh::IndicesBatch opaque_batch;
			opaque_batch.indices_start = 0;
			opaque_batch.material_index = 0;
			opaque_batch.num_indices = (uint32)combined_opaque_indices.size();
			combined_mesh->batches.push_back(opaque_batch);
		}

		if(!combined_trans_indices.empty())
		{
			BatchedMesh::IndicesBatch trans_batch;
			trans_batch.indices_start = (uint32)combined_opaque_indices.size();
			trans_batch.material_index = 1;
			trans_batch.num_indices = (uint32)combined_trans_indices.size();
			combined_mesh->batches.push_back(trans_batch);
		}

		combined_mesh->aabb_os = aabb_os;

		std::vector<uint32> index_map;
		combined_mesh = MeshSimplification::removeInvisibleTriangles(combined_mesh, index_map, task_manager);

		// Some triangles (i.e. their 3 associated indices) have been removed.
		// We need to update the corresponding object index ranges.
		for(size_t z=0; z<results.ob_batch_ranges.size(); ++z)
		{
			ObjectBatchRanges& ob_ranges = results.ob_batch_ranges[z];

			runtimeCheck(ob_ranges.batch0_start < index_map.size());
			runtimeCheck(ob_ranges.batch0_end   < index_map.size());

			ob_ranges.batch0_start = index_map[ob_ranges.batch0_start];
			ob_ranges.batch0_end   = index_map[ob_ranges.batch0_end];

			assert(ob_ranges.batch0_start <= ob_ranges.batch0_end);
			assert((ob_ranges.batch0_start < combined_mesh->numIndices()) || (ob_ranges.batch0_start == ob_ranges.batch0_end));
			assert(ob_ranges.batch0_end <= combined_mesh->numIndices());


			runtimeCheck(ob_ranges.batch1_start < index_map.size());
			runtimeCheck(ob_ranges.batch1_end   < index_map.size());

			ob_ranges.batch1_start = index_map[ob_ranges.batch1_start];
			ob_ranges.batch1_end   = index_map[ob_ranges.batch1_end];

			assert(ob_ranges.batch1_start <= ob_ranges.batch1_end);
			assert((ob_ranges.batch1_start < combined_mesh->numIndices()) || (ob_ranges.batch1_start == ob_ranges.batch1_end));
			assert(ob_ranges.batch1_end <= combined_mesh->numIndices());
		}


		if(combined_mesh->numVerts() > 0)
		{
			//-------------------------------------- Remove unused materials --------------------------------------
			std::vector<MatInfo> new_mat_infos;
			{
				conPrint("ChunkGenThread: Raw combined mesh num materials: " + toString(combined_mat_infos.size()));

				const size_t num_verts = combined_mesh->numVerts();

				runtimeCheck(combined_mesh->getAttribute(BatchedMesh::VertAttribute_MatIndex).component_type == BatchedMesh::ComponentType_UInt32);

				std::vector<uint32> new_mat_i(combined_mat_infos.size(), std::numeric_limits<uint32>::max()); // Map from old material index to new material index.
				for(size_t v = 0; v<num_verts; ++v)
				{
					uint32 mat_i;
					std::memcpy(&mat_i, combined_mesh->vertex_data.data() + combined_mesh_vert_size * v + combined_mesh_mat_index_offset_B, sizeof(uint32)); // Get vertex material index from combined_mesh
					uint32 new_mat_i_val = new_mat_i[mat_i];
					if(new_mat_i_val == std::numeric_limits<uint32>::max())
					{
						new_mat_i_val = (uint32)new_mat_infos.size();
						new_mat_infos.push_back(combined_mat_infos[mat_i]);
						new_mat_i[mat_i] = new_mat_i_val;
					}
				
					std::memcpy(combined_mesh->vertex_data.data() + combined_mesh_vert_size * v + combined_mesh_mat_index_offset_B, &new_mat_i_val, sizeof(uint32)); // Copy new value back to combined_mesh
				}

				conPrint("ChunkGenThread: Used combined mesh num materials: " + toString(new_mat_infos.size()));
			}

			//-------------------------------------- Build list of used textures --------------------------------------
			// Build list of used textures, maintaining order.
			std::vector<std::string> used_tex_paths;
			std::set<std::string> textures_added;
			for(size_t m=0; m<new_mat_infos.size(); ++m)
			{
				const MatInfo& mat_info = new_mat_infos[m];

				const std::string tex_path = mat_info.tex_path;
				if(!tex_path.empty())
				{
					if(textures_added.count(tex_path) == 0)
					{
						textures_added.insert(tex_path);
						used_tex_paths.push_back(tex_path);
					}
				}
			}

			//-------------------------------------- Build texture array, save basis file to disk --------------------------------------
			std::map<std::string, int> array_image_indices; // Index of texture in texture array.
			// There will be no entry in the map for the path if the texture could not be loaded.

			buildAndSaveArrayTexture(used_tex_paths, task_manager, chunk_x, chunk_y, 
				array_image_indices, // array_image_indices_out
				results.combined_texture_path, // combined_texture_path_out
				results.combined_texture_hash // combined_texture_hash_out
			);

			// TEMP HACK from openglengine.cpp
			// MaterialData flag values
			#define HAVE_SHADING_NORMALS_FLAG			1
			#define HAVE_TEXTURE_FLAG					2

			//-------------------------------------- Build output_mat_infos --------------------------------------
			js::Vector<OutputMatInfo> output_mat_infos;
			for(size_t m=0; m<new_mat_infos.size(); ++m)
			{
				const MatInfo& mat_info = new_mat_infos[m];

				OutputMatInfo output_mat_info;
				output_mat_info.tex_matrix_col_major = mat_info.tex_matrix.transpose();
				output_mat_info.emission_lum_flux_or_lum = mat_info.emission_lum_flux_or_lum;
				output_mat_info.roughness = mat_info.roughness;
				output_mat_info.metallic = mat_info.metallic;
				output_mat_info.linear_colour_rgb = sanitiseAndConvertToLinearAlbedoColour(mat_info.colour_rgb);
				output_mat_info.flags = 0;

				const std::string tex_path = mat_info.tex_path;
				if(!tex_path.empty() && (array_image_indices.count(tex_path) > 0))
				{
					output_mat_info.flags += (float)HAVE_TEXTURE_FLAG;

					output_mat_info.array_image_index = (float)array_image_indices[tex_path];
				}

				output_mat_infos.push_back(output_mat_info);
			}

			runtimeCheck(combined_mesh->numIndices() > 0);
			runtimeCheck(combined_mesh->numVerts() > 0);

			// Write combined mesh to disk
			conPrint("ChunkGenThread: Writing combined mesh to disk... (num indices: " + toString(combined_mesh->numIndices()) + ", num verts: " + toString(combined_mesh->numVerts()) + ")");
			// NOTE: naming scheme needs to start with "chunk_", see if(hasPrefix(lod_model_url, "chunk_")) check in GUIClient::handleUploadedMeshData().
			const std::string path = PlatformUtils::getTempDirPath() + "/chunk_128_" + toString(chunk_x) + "_" + toString(chunk_y) + ".bmesh";
			//const std::string path = "d:/tempfiles/main_world/chunk_128_" + toString(chunk_x) + "_" + toString(chunk_y) + ".bmesh";
			{
				BatchedMesh::WriteOptions options;
				options.write_mesh_version_2 = true; // Write older batched mesh version for backwards compatibility
				options.compression_level = 19;
				options.use_meshopt = true;
				options.meshopt_vertex_version = 0; // For backwards compat.
				options.pos_mantissa_bits = 14;
				options.uv_mantissa_bits = 8;
				combined_mesh->writeToFile(path, options);
			}

			// FormatDecoderGLTF::writeBatchedMeshToGLBFile(*combined_mesh, "d:/tempfiles/main_world/chunk_128_" + toString(chunk_x) + "_" + toString(chunk_y) + ".glb", GLTFWriteOptions());

			conPrint("ChunkGenThread: num_obs_combined: " + toString(num_obs_combined));
			conPrint("ChunkGenThread: num_batches_combined: " + toString(num_batches_combined));
			conPrint("ChunkGenThread: Wrote chunk mesh to '" + path + "'.");

			// Compute hash over it
			const uint64 hash = FileChecksum::fileChecksum(path);


			//--------------------------- Build optimised mesh ---------------------------
			// Can't do meshopt optimisations because they reorder indices, which we need to preserve for object index ranges.

			BatchedMesh::QuantiseOptions quantise_options;
			quantise_options.pos_bits = 13;
			quantise_options.uv_bits  = 10;
			combined_mesh = combined_mesh->buildQuantisedMesh(quantise_options);

			const std::string opt_mesh_path = path + "_opt"; // The final optimised mesh URL will be computed later.

			// Write optimised mesh (using quantised position etc.)
			{
				BatchedMesh::WriteOptions options;
				options.compression_level = 19;
				options.use_meshopt = true;
				combined_mesh->writeToFile(opt_mesh_path, options);
			}

			conPrint("ChunkGenThread: Wrote optimised chunk mesh to '" + opt_mesh_path + "'.");
			//---------------------------------------------------------------------------------


			// Write output_mat_infos for testing
			if(false)
			{
				conPrint("Writing mat info to disk...");
				FileOutStream file("d:/tempfiles/main_world/mat_info_" + toString(chunk_x) + "_" + toString(chunk_y) + ".bin");

				for(size_t i=0; i<output_mat_infos.size(); ++i)
					file.writeData(&output_mat_infos[i], sizeof(OutputMatInfo));


				//------------ Build compressed mat_info ------------
				js::Vector<uint8> compressed_data(ZSTD_compressBound(output_mat_infos.dataSizeBytes()));

				const size_t compressed_size = ZSTD_compress(/*dest=*/compressed_data.data(), /*dest capacity=*/compressed_data.size(), /*src=*/output_mat_infos.data(), /*src size=*/output_mat_infos.dataSizeBytes(),
					19 // compression level  TODO: use higher level? test a few.
				);
				if(ZSTD_isError(compressed_size))
					throw glare::Exception(std::string("Compression failed: ") + ZSTD_getErrorName(compressed_size));
				compressed_data.resize(compressed_size);
				FileUtils::writeEntireFile("d:/tempfiles/main_world/compressed_mat_info_" + toString(chunk_x) + "_" + toString(chunk_y) + ".bin", (const char*)compressed_data.data(), compressed_data.size());
				//---------------------------------------------------
			}

			results.output_mat_infos = output_mat_infos;
			results.combined_mesh_path = path;
			results.combined_mesh_hash = hash;
			results.optimised_mesh_path = opt_mesh_path;
		}
	}

	return results;
}


static ChunkBuildResults buildChunk(ServerAllWorldsState* world_state, Reference<ServerWorldState> world, const js::AABBox chunk_aabb, int chunk_x, int chunk_y, glare::TaskManager& task_manager)
{
	std::vector<ObInfo> ob_infos;

	{
		WorldStateLock lock(world_state->mutex);
		ServerWorldState::ObjectMapType& objects = world->getObjects(lock);
		for(auto it = objects.begin(); it != objects.end(); ++it)
		{
			WorldObjectRef ob = it->second;
			if(chunk_aabb.contains(ob->getCentroidWS()) && !BitUtils::isBitSet(ob->flags, WorldObject::EXCLUDE_FROM_LOD_CHUNK_MESH))
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
					if(ob->getCompressedVoxels() && ob->getCompressedVoxels()->size() > 0)
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

						ob_info.ob_uid = ob->uid;

						if(!ob->model_url.empty())
							ob_info.model_path = world_state->resource_manager->pathForURL(ob->model_url);
						
						ob_info.compressed_voxels = ob->getCompressedVoxels();

						ob_info.ob_to_world = obToWorldMatrix(*ob);
						ob_info.ob_to_world_scale = myMax(ob->scale.x, ob->scale.y, ob->scale.z);
						ob_info.object_type = ob->object_type;
						ob_info.aabb_ws = ob->getAABBWS();

						ob_info.mat_info.resize(ob->materials.size());
						
						for(size_t i=0; i<ob->materials.size(); ++i)
						{
							WorldMaterial* mat = ob->materials[i].ptr();

							ob_info.mat_info[i].tex_matrix = mat->tex_matrix;

							if(!mat->colour_texture_url.empty())
							{
								const std::string tex_path = world_state->resource_manager->pathForURL(mat->colour_texture_url);
								ob_info.mat_info[i].tex_path = tex_path;
							}

							ob_info.mat_info[i].emission_lum_flux_or_lum = mat->emission_lum_flux_or_lum;
							ob_info.mat_info[i].roughness = mat->roughness.val;
							ob_info.mat_info[i].metallic = mat->metallic_fraction.val;
							ob_info.mat_info[i].colour_rgb = mat->colour_rgb;
							ob_info.mat_info[i].opacity = mat->opacity.val;
							//ob_info.mat_info[i].flags = OpenGLEngine::matFlags(*mat);
						}


						ob_infos.push_back(ob_info);
					}
				}
			}
		}
	} // End lock scope.


	ChunkBuildResults results = buildChunkForObInfo(ob_infos, chunk_x, chunk_y, task_manager);
	return results;
}


inline static bool shouldExcludeObjectFromLODChunkMesh(const WorldObject* ob)
{
	// Objects with scripts are likely to be moving, so don't bake into chunk.
	if(!ob->script.empty())
	{
		// Scripts like
		// def evalTranslation(float time, WinterEnv env) vec3 : vec3(0.28, 0.2, 1.65)
		// Where the object transform is static (independent of time) are compatible with chunk baking, so don't need to be excluded.
		if(StringUtils::containsString(ob->script, "evalTranslation") && (StringUtils::countOccurrences(ob->script, "time") == 1))
			return false;

		// Allow dynamic_texture_update script objects to be baked into chunks.  Chunks can be rebuilt when the object's texture maps are updated.
		if(StringUtils::containsString(ob->script, "dynamic_texture_update"))
			return false; // Don't exclude from chunk baking

		// Lua scripts tend to be more about onTouchEvents, less about moving around.
		if(StringUtils::containsString(ob->script, "--lua"))
			if(!(StringUtils::containsString(ob->script, "moveTo") || StringUtils::containsString(ob->script, "rotateTo"))) // If the script doesn't contain moveTo or rotateTo calls:
				return false; // Don't exclude from chunk baking

		return true;
	}

	// Objects that have the park biome are used for computing grass and tree scattering coverage.  This won't work if they are baked into the chunk.
	// So keep separate.
	if(!ob->content.empty() && hasPrefix(ob->content, "biome: park"))
		return true;

	// Large objects may extend past the chunk they are in (since objects are classified into chunks by centroid)
	// This allows the camera to come close to objects in their LOD'd form, which we don't want.
	// So if an object extends a significant distance out of the chunk, don't do chunk LODing on it.
	{
		const Vec4f centroid = ob->getCentroidWS();
		const int chunk_x = Maths::floorToInt(centroid[0] / chunk_w);
		const int chunk_y = Maths::floorToInt(centroid[1] / chunk_w);

		const js::AABBox chunk_aabb(
			Vec4f(chunk_x       * chunk_w, chunk_y       * chunk_w, -2000, 1),
			Vec4f((chunk_x + 1) * chunk_w, (chunk_y + 1) * chunk_w,  2000, 1)
		);

		const js::AABBox ob_aabb_ws = ob->getAABBWS();
		const float extension = myMax(horizontalMax((ob_aabb_ws.max_ - chunk_aabb.max_).v), horizontalMax((chunk_aabb.min_ - ob_aabb_ws.min_).v)); // Distance the object extends out of chunk AABB
		if(extension > (chunk_w / 4.0f))
			return true;
	}

	return false;
}


// Iterates over WorldObjects, and creates a LODChunk containing the object if one does not already exist.
// Also sets or unsets INCLUDE_IN_LOD_CHUNK_MESH flag for all objects in world.
static void updateObjectExcludeFlagsAndUpdateChunks(ServerAllWorldsState* all_worlds_state, const std::string& world_name, ServerWorldState* world_state, WorldStateLock& lock)
{
	Timer timer;

	ServerWorldState::ObjectMapType& objects = world_state->getObjects(lock);
	ServerWorldState::LODChunkMapType& lod_chunks = world_state->getLODChunks(lock);

	for(auto it = objects.begin(); it != objects.end(); ++it)
	{
		WorldObject* ob = it->second.ptr();

		if(!ob->axis.isFinite())
			ob->axis = Vec3f(0,0,1);

		if(!isFinite(ob->angle))
			ob->angle = 0;

		// Update EXCLUDE_FROM_LOD_CHUNK_MESH flag if needed.
		const bool should_exclude = shouldExcludeObjectFromLODChunkMesh(ob);
		const bool cur_excluded = BitUtils::isBitSet(ob->flags, WorldObject::EXCLUDE_FROM_LOD_CHUNK_MESH);
		const bool exclusion_changed = cur_excluded != should_exclude;
		if(exclusion_changed)
		{
			conPrint("ChunkGenThread: Updating EXCLUDE_FROM_LOD_CHUNK_MESH flag for ob to " + toString(should_exclude));
			BitUtils::setOrZeroBit(ob->flags, WorldObject::EXCLUDE_FROM_LOD_CHUNK_MESH, should_exclude);

			// Mark as db-dirty so gets saved to disk.
			world_state->addWorldObjectAsDBDirty(ob, lock);
			all_worlds_state->markAsChanged();
		}

		if(!should_exclude || exclusion_changed)
		{
			const Vec4f centroid = ob->getCentroidWS();
			const int chunk_x = Maths::floorToInt(centroid[0] / chunk_w);
			const int chunk_y = Maths::floorToInt(centroid[1] / chunk_w);
			const Vec3i chunk_coords(chunk_x, chunk_y, 0);

			auto chunk_res = lod_chunks.find(chunk_coords);

			if(!should_exclude && (chunk_res == lod_chunks.end()))
			{
				// Need new chunk
				conPrint("ChunkGenThread: Adding new LODChunk with coords " + chunk_coords.toString());

				LODChunkRef chunk = new LODChunk();
				chunk->coords = chunk_coords;
				chunk->needs_rebuild = true;

				// Add to world state, mark as db-dirty so gets saved to disk.
				lod_chunks.insert(std::make_pair(chunk_coords, chunk));
				world_state->addLODChunkAsDBDirty(chunk, lock);
				all_worlds_state->markAsChanged();

				chunk_res = lod_chunks.find(chunk_coords);
			}

			// If exclusion changed for this object, and there is a chunk object containing it, mark the chunk as needs-rebuild.
			if(exclusion_changed && (chunk_res != lod_chunks.end()))
			{
				conPrint("ChunkGenThread: Object " + ob->uid.toString() + " exclude-from-chunk changed to " + boolToString(should_exclude) + ", marking chunk " + chunk_coords.toString() + " as needs-rebuild.");
				chunk_res->second->needs_rebuild = true;
			}

		}
	}

	// conPrint("ChunkGenThread::updateObjectExcludeFlagsAndUpdateChunks() done. Elapsed: " + timer.elapsedStringMSWIthNSigFigs(4));
}


void ChunkGenThread::doRun()
{
	PlatformUtils::setCurrentThreadName("ChunkGenThread");

	glare::TaskManager task_manager;

	Timer timer;


	struct ChunkToBuild
	{
		LODChunkRef chunk;
		Reference<ServerWorldState> world_state;
	};

	try
	{
#if 0
		{
			WorldStateLock lock(all_worlds_state->mutex);
			//Reference<ServerWorldState> world_state = all_worlds_state->getRootWorldState();
			Reference<ServerWorldState> world_state = all_worlds_state->world_states[""];//joblank"];
			
			//for(int x=-10; x<10; ++x)
			//for(int y=-10; y<10; ++y)
			int x = 1;
			int y = 4;
			{
				if(all_worlds_state->getRootWorldState()->getLODChunks(lock).count(Vec3i(x, y, 0)) != 0)
				{
					// Compute chunk AABB
					const js::AABBox chunk_aabb(
						Vec4f(x * chunk_w, y * chunk_w, -1000.f, 1.f), // min
						Vec4f((x + 1) * chunk_w, (y + 1) * chunk_w, 1000.f, 1.f) // max
					);

					buildChunk(all_worlds_state, world_state, chunk_aabb, x, y, task_manager);
				}
			}

			conPrint("ChunkGenThread: Done. (Elapsed: " + timer.elapsedStringNSigFigs(4));

			return; // Just run once for now.
		}
#endif

		//TEMP HACK: invalidate all chunks in main world
		if(false)
		{
			WorldStateLock lock(all_worlds_state->mutex);
			for(auto chunk_it = all_worlds_state->getRootWorldState()->getLODChunks(lock).begin(); chunk_it != all_worlds_state->getRootWorldState()->getLODChunks(lock).end(); ++chunk_it)
			{
				LODChunk* chunk = chunk_it->second.ptr();
				chunk->needs_rebuild = true;
			}
		}

		while(1)
		{
			
			std::vector<ChunkToBuild> dirty_chunks;

			{
				WorldStateLock lock(all_worlds_state->mutex);
				for(auto it = all_worlds_state->world_states.begin(); it != all_worlds_state->world_states.end(); ++it)
				{
					ServerWorldState* world_state = it->second.ptr();

					updateObjectExcludeFlagsAndUpdateChunks(all_worlds_state, it->first, world_state, lock);

					for(auto chunk_it = world_state->getLODChunks(lock).begin(); chunk_it != world_state->getLODChunks(lock).end(); ++chunk_it)
					{
						LODChunk* chunk = chunk_it->second.ptr();

						if(chunk->needs_rebuild)
							dirty_chunks.push_back({chunk, world_state});
					}
				}
			}


			for(size_t i=0; i<dirty_chunks.size(); ++i)
			{
				LODChunkRef chunk = dirty_chunks[i].chunk;
				const int x = chunk->coords.x;
				const int y = chunk->coords.y;

				// Compute chunk AABB
				const js::AABBox chunk_aabb(
					Vec4f(x       * chunk_w, y       * chunk_w, -100.f, 1.f), // min
					Vec4f((x + 1) * chunk_w, (y + 1) * chunk_w,  500.f, 1.f) // max
				);

				conPrint("================================= ChunkGenThread: Building chunk " + toString(x) + ", " + toString(y) + " (" + toString(i) + "/" + toString(dirty_chunks.size()) + " dirty chunks) =================================");

				const ChunkBuildResults results = buildChunk(all_worlds_state, dirty_chunks[i].world_state, chunk_aabb, x, y, task_manager);

				conPrint("====== ChunkGenThread: chunk " + toString(x) + ", " + toString(y) + " built. ======");

				//------------ Build compressed mat_info ------------
				js::Vector<uint8> compressed_data(ZSTD_compressBound(results.output_mat_infos.dataSizeBytes()));

				const size_t compressed_size = ZSTD_compress(/*dest=*/compressed_data.data(), /*dest capacity=*/compressed_data.size(), /*src=*/results.output_mat_infos.data(), /*src size=*/results.output_mat_infos.dataSizeBytes(),
					19 // compression level  TODO: use higher level? test a few.
				);
				if(ZSTD_isError(compressed_size))
					throw glare::Exception(std::string("Compression failed: ") + ZSTD_getErrorName(compressed_size));
				compressed_data.resize(compressed_size);
				//---------------------------------------------------

				// Copy combined mesh and texture array files into resource system.

				const int MESH_EPOCH = 8; // This can be bumped to punch through caches, in particular if the optimised mesh needs to be rebuilt.
				// Note that because we store mesh_url in the LodChunk object, which is sent to clients, they will automatically pick up a new epoch version if it's incremented.

				URLString mesh_URL;
				if(!results.combined_mesh_path.empty())
				{
					mesh_URL = ResourceManager::URLForPathAndHashAndEpoch(results.combined_mesh_path, results.combined_mesh_hash, MESH_EPOCH);
					if(!all_worlds_state->resource_manager->isFileForURLPresent(mesh_URL))
					{
						all_worlds_state->resource_manager->copyLocalFileToResourceDir(results.combined_mesh_path, mesh_URL);

						WorldStateLock lock(all_worlds_state->mutex);
						all_worlds_state->addResourceAsDBDirty(all_worlds_state->resource_manager->getOrCreateResourceForURL(mesh_URL));
					}
				}

				// Copy optimised mesh into resource system.
				if(!results.optimised_mesh_path.empty())
				{	
					const URLString optimised_mesh_URL = removeDotAndExtension(ResourceManager::URLForPathAndHashAndEpoch(results.combined_mesh_path, results.combined_mesh_hash, MESH_EPOCH)) + "_opt" + toURLString(toString(Protocol::OPTIMISED_MESH_VERSION)) + ".bmesh";

					if(!all_worlds_state->resource_manager->isFileForURLPresent(optimised_mesh_URL))
					{
						all_worlds_state->resource_manager->copyLocalFileToResourceDir(results.optimised_mesh_path, optimised_mesh_URL);

						WorldStateLock lock(all_worlds_state->mutex);
						all_worlds_state->addResourceAsDBDirty(all_worlds_state->resource_manager->getOrCreateResourceForURL(optimised_mesh_URL));
					}
				}

				URLString tex_URL;
				if(!results.combined_texture_path.empty())
				{
					tex_URL = ResourceManager::URLForPathAndHash(results.combined_texture_path, results.combined_texture_hash);
					if(!all_worlds_state->resource_manager->isFileForURLPresent(tex_URL))
					{
						all_worlds_state->resource_manager->copyLocalFileToResourceDir(results.combined_texture_path, tex_URL);

						WorldStateLock lock(all_worlds_state->mutex);
						all_worlds_state->addResourceAsDBDirty(all_worlds_state->resource_manager->getOrCreateResourceForURL(tex_URL));
					}
				}

				// Update the chunk object if it has changed.  Mark chunk as db-dirty so it gets saved to disk.
				{
					WorldStateLock lock(all_worlds_state->mutex);

					chunk->mesh_url = mesh_URL;
					chunk->combined_array_texture_url = tex_URL;
					chunk->compressed_mat_info = compressed_data;
					chunk->needs_rebuild = false;

					chunk->db_dirty = true;

					dirty_chunks[i].world_state->addLODChunkAsDBDirty(chunk, lock);


					// Set object vertex indices range
					for(size_t z=0; z<results.ob_batch_ranges.size(); ++z)
					{
						const ObjectBatchRanges& ob_batch_ranges = results.ob_batch_ranges[z];

						auto res = dirty_chunks[i].world_state->getObjects(lock).find(ob_batch_ranges.ob_uid);
						if(res != dirty_chunks[i].world_state->getObjects(lock).end())
						{
							WorldObject* ob = res->second.ptr();
							ob->chunk_batch0_start = ob_batch_ranges.batch0_start;
							ob->chunk_batch0_end   = ob_batch_ranges.batch0_end;
							ob->chunk_batch1_start = ob_batch_ranges.batch1_start;
							ob->chunk_batch1_end   = ob_batch_ranges.batch1_end;

							// TODO: send out object updated message to clients.

							dirty_chunks[i].world_state->addWorldObjectAsDBDirty(ob, lock);
						}
					}

					all_worlds_state->markAsChanged();


					// Send out a chunk-updated message to clients connected to this world, so they load the newly built chunk mesh and texture.
					// conPrint("============== Sending LODChunkUpdatedMessage to clients ==================");
					SocketBufferOutStream scratch_packet(SocketBufferOutStream::DontUseNetworkByteOrder);
					MessageUtils::initPacket(scratch_packet, Protocol::LODChunkUpdatedMessage);
					chunk->writeToStream(scratch_packet);
					MessageUtils::updatePacketLengthField(scratch_packet);

					server->enqueuePacketToBroadcastForWorld(scratch_packet, dirty_chunks[i].world_state.ptr());
				}
			}

			if(!dirty_chunks.empty())
				conPrint("---------ChunkGenThread: Finished building " + toString(dirty_chunks.size()) + " dirty chunks.---------");

			bool keep_running = true;
			waitForPeriod(30.0, keep_running);
			if(!keep_running)
				break;
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


#if BUILD_TESTS


#include <utils/TestUtils.h>


namespace ChunkGenThreadTests
{

static const float test_cube_half_w = 5.f; // Large enough that removeSmallComponents() and mesh simplification leave the cube intact.


struct TestCubeSpec
{
	TestCubeSpec() : index_type(BatchedMesh::ComponentType_UInt16), have_normals(true), normal_type(BatchedMesh::ComponentType_PackedNormal), have_uv0(true), uv0_type(BatchedMesh::ComponentType_Float), num_mats(1), quantise(false),
		skinned(false), joints_type(BatchedMesh::ComponentType_UInt8), weights_type(BatchedMesh::ComponentType_UInt8) {}

	BatchedMesh::ComponentType index_type; // ComponentType_UInt16 or ComponentType_UInt32
	bool have_normals;
	BatchedMesh::ComponentType normal_type;
	bool have_uv0;
	BatchedMesh::ComponentType uv0_type;
	int num_mats; // Faces are split between materials in contiguous batches.
	bool quantise; // Write the mesh quantised with buildQuantisedMesh() (uint16 positions, oct16 normals, uint16 UVs), as used for optimised meshes.

	// If skinned, all verts are fully weighted to a single joint, whose bind-pose transform is test_skin_offset.
	bool skinned;
	BatchedMesh::ComponentType joints_type; // ComponentType_UInt8 or ComponentType_UInt16
	BatchedMesh::ComponentType weights_type; // ComponentType_UInt8, ComponentType_UInt16 or ComponentType_Float
};


// Translation applied by the skin of skinned test cubes: a root node translating by (0, 0, 10), with a child joint node translating by (0, 10, 0).
static const Vec4f test_skin_offset(0, 10, 10, 0);


static size_t roundUpTo4(size_t x) { return (x + 3) & ~(size_t)3; }


// Makes a cube with 4 verts per face, centred on the origin, and writes it to a bmesh file in the temp dir.  Returns the file path.
static std::string writeTestCubeMesh(const std::string& name, const TestCubeSpec& spec)
{
	try
	{
		BatchedMeshRef mesh = new BatchedMesh();

		size_t offset = 0;
		mesh->vert_attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_Position, BatchedMesh::ComponentType_Float, offset));
		offset = roundUpTo4(offset + BatchedMesh::vertAttributeSize(mesh->vert_attributes.back()));

		size_t normal_offset = 0;
		if(spec.have_normals)
		{
			normal_offset = offset;
			mesh->vert_attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_Normal, spec.normal_type, offset));
			offset = roundUpTo4(offset + BatchedMesh::vertAttributeSize(mesh->vert_attributes.back()));
		}

		size_t uv0_offset = 0;
		if(spec.have_uv0)
		{
			uv0_offset = offset;
			mesh->vert_attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_UV_0, spec.uv0_type, offset));
			offset = roundUpTo4(offset + BatchedMesh::vertAttributeSize(mesh->vert_attributes.back()));
		}

		size_t weights_offset = 0;
		if(spec.skinned)
		{
			mesh->vert_attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_Joints, spec.joints_type, offset));
			offset = roundUpTo4(offset + BatchedMesh::vertAttributeSize(mesh->vert_attributes.back()));

			weights_offset = offset;
			mesh->vert_attributes.push_back(BatchedMesh::VertAttribute(BatchedMesh::VertAttribute_Weights, spec.weights_type, offset));
			offset = roundUpTo4(offset + BatchedMesh::vertAttributeSize(mesh->vert_attributes.back()));

			AnimationNodeData root_node;
			root_node.inverse_bind_matrix = Matrix4f::identity();
			root_node.trans = Vec4f(0, 0, 10, 0);
			root_node.rot = Quatf::identity();
			root_node.scale = Vec4f(1, 1, 1, 0);
			root_node.name = "root";
			root_node.parent_index = -1;

			AnimationNodeData joint_node = root_node;
			joint_node.trans = Vec4f(0, 10, 0, 0);
			joint_node.name = "joint";
			joint_node.parent_index = 0;

			mesh->animation_data.nodes.push_back(root_node);
			mesh->animation_data.nodes.push_back(joint_node);
			mesh->animation_data.sorted_nodes = { 0, 1 };
			mesh->animation_data.joint_nodes = { 1 }; // Joint index 0 in vertex data refers to node 1.
		}

		const size_t vert_size = offset;
		testAssert(mesh->vertexSize() == vert_size);

		const size_t num_verts = 24;
		mesh->vertex_data.resize(num_verts * vert_size);
		std::memset(mesh->vertex_data.data(), 0, mesh->vertex_data.size()); // Oct16 normals and UInt16 UVs are left zeroed, their values don't matter.  Joint indices are left as zero.

		const float corner_u[4] = { -1, 1, 1, -1 };
		const float corner_w[4] = { -1, -1, 1, 1 };

		js::Vector<uint32, 16> indices;
		size_t v = 0;
		for(int axis=0; axis<3; ++axis)
		for(int s=-1; s<=1; s+=2)
		{
			const Vec4f n((axis == 0) ? (float)s : 0.f, (axis == 1) ? (float)s : 0.f, (axis == 2) ? (float)s : 0.f, 0);
			Vec4f u((axis == 2) ? 1.f : 0.f, (axis == 0) ? 1.f : 0.f, (axis == 1) ? 1.f : 0.f, 0); // Next axis after 'axis'
			Vec4f w((axis == 1) ? 1.f : 0.f, (axis == 2) ? 1.f : 0.f, (axis == 0) ? 1.f : 0.f, 0); // Axis after that
			if(s < 0)
				std::swap(u, w); // Keep cross(u, w) = n, so triangles are wound counter-clockwise when seen from outside.

			const uint32 face_first_vert = (uint32)v;
			for(int c=0; c<4; ++c)
			{
				uint8* const vert = &mesh->vertex_data[v * vert_size];

				const Vec4f pos = (n + u * corner_u[c] + w * corner_w[c]) * test_cube_half_w;
				const Vec3f pos3(pos[0], pos[1], pos[2]);
				std::memcpy(vert, &pos3, sizeof(Vec3f));

				if(spec.have_normals)
				{
					if(spec.normal_type == BatchedMesh::ComponentType_PackedNormal)
					{
						const uint32 packed = batchedMeshPackNormal(n);
						std::memcpy(vert + normal_offset, &packed, sizeof(uint32));
					}
					else if(spec.normal_type == BatchedMesh::ComponentType_Float)
					{
						const Vec3f n3(n[0], n[1], n[2]);
						std::memcpy(vert + normal_offset, &n3, sizeof(Vec3f));
					}
				}

				if(spec.have_uv0)
				{
					const float uv_x = corner_u[c] * 0.5f + 0.5f;
					const float uv_y = corner_w[c] * 0.5f + 0.5f;
					if(spec.uv0_type == BatchedMesh::ComponentType_Float)
					{
						const Vec2f uv(uv_x, uv_y);
						std::memcpy(vert + uv0_offset, &uv, sizeof(Vec2f));
					}
					else if(spec.uv0_type == BatchedMesh::ComponentType_Half)
					{
						const half uv[2] = { half(uv_x), half(uv_y) };
						std::memcpy(vert + uv0_offset, uv, sizeof(half) * 2);
					}
				}

				if(spec.skinned)
				{
					// Put full weight on the first joint slot, which references joint 0.
					if(spec.weights_type == BatchedMesh::ComponentType_UInt8)
					{
						const uint8 weight = 255;
						std::memcpy(vert + weights_offset, &weight, sizeof(uint8));
					}
					else if(spec.weights_type == BatchedMesh::ComponentType_UInt16)
					{
						const uint16 weight = 65535;
						std::memcpy(vert + weights_offset, &weight, sizeof(uint16));
					}
					else
					{
						testAssert(spec.weights_type == BatchedMesh::ComponentType_Float);
						const float weight = 1.f;
						std::memcpy(vert + weights_offset, &weight, sizeof(float));
					}
				}

				v++;
			}

			indices.push_back(face_first_vert + 0); indices.push_back(face_first_vert + 1); indices.push_back(face_first_vert + 2);
			indices.push_back(face_first_vert + 0); indices.push_back(face_first_vert + 2); indices.push_back(face_first_vert + 3);
		}
		testAssert(v == num_verts);

		if(spec.index_type == BatchedMesh::ComponentType_UInt32)
		{
			// setIndexDataFromIndices() would pick uint16 indices for this few verts, so set uint32 indices directly.
			mesh->index_type = BatchedMesh::ComponentType_UInt32;
			mesh->index_data.resize(indices.size() * sizeof(uint32));
			std::memcpy(mesh->index_data.data(), indices.data(), indices.size() * sizeof(uint32));
		}
		else
		{
			mesh->setIndexDataFromIndices(indices, num_verts);
			testAssert(mesh->index_type == spec.index_type);
		}

		const uint32 num_indices_per_face = 6;
		uint32 face_i = 0;
		for(int m=0; m<spec.num_mats; ++m)
		{
			const uint32 end_face = (uint32)(6 * (m + 1) / spec.num_mats);
			BatchedMesh::IndicesBatch batch;
			batch.indices_start = face_i * num_indices_per_face;
			batch.num_indices = (end_face - face_i) * num_indices_per_face;
			batch.material_index = (uint32)m;
			mesh->batches.push_back(batch);
			face_i = end_face;
		}

		mesh->aabb_os = mesh->computeAABB();

		if(spec.quantise)
			mesh = mesh->buildQuantisedMesh(BatchedMesh::QuantiseOptions());

		// Use meshopt, which compresses the interleaved vertex data, so that attributes smaller than 4 bytes (e.g. Oct16 normals) can be written.
		BatchedMesh::WriteOptions write_options;
		write_options.use_meshopt = true;

		const std::string path = PlatformUtils::getTempDirPath() + "/chunkgen_test_" + name + ".bmesh";
		mesh->writeToFile(path, write_options);
		return path;
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
		return nullptr;
	}
}


static ObInfo makeObInfo(const std::string& model_path, uint64 uid, float pos_x, const std::vector<float>& mat_opacities)
{
	ObInfo ob_info;
	ob_info.ob_to_world = Matrix4f::translationMatrix(pos_x, 0, 0);
	ob_info.aabb_ws = js::AABBox(Vec4f(pos_x - test_cube_half_w, -test_cube_half_w, -test_cube_half_w, 1), Vec4f(pos_x + test_cube_half_w, test_cube_half_w, test_cube_half_w, 1));
	ob_info.model_path = model_path;
	ob_info.object_type = WorldObject::ObjectType_Generic;
	ob_info.ob_to_world_scale = 1.f;
	ob_info.ob_uid = UID(uid);

	for(size_t i=0; i<mat_opacities.size(); ++i)
	{
		MatInfo mat;
		mat.tex_matrix = Matrix2f::identity();
		mat.emission_lum_flux_or_lum = 0;
		mat.roughness = 0.5f;
		mat.metallic = 0;
		mat.colour_rgb = Colour3f(0.5f);
		mat.opacity = mat_opacities[i];
		ob_info.mat_info.push_back(mat);
	}
	return ob_info;
}


struct TestOb
{
	ObInfo ob_info;
	bool expect_included; // Should the object's geometry end up in the chunk mesh?
	int num_mesh_mats; // Number of materials the object's mesh references.
	Vec4f expected_offset = Vec4f(0, 0, 0, 0); // Offset of the cube from the object position in the chunk mesh, e.g. from skinning.
};


static bool rangesOverlap(uint32 a_start, uint32 a_end, uint32 b_start, uint32 b_end)
{
	return (a_start < a_end) && (b_start < b_end) && (a_start < b_end) && (b_start < a_end);
}


// Builds a chunk from the given objects and checks that:
// * Objects expected to fail have empty index ranges, and still have their UID set.
// * Included objects have non-empty index ranges, which only reference that object's geometry.
// * The written chunk mesh is valid (all indices in bounds), and only has materials from included objects.
static void testBuildChunk(const std::string& test_name, const std::vector<TestOb>& test_obs, glare::TaskManager& task_manager, int chunk_x)
{
	conPrint("ChunkGenThread test: " + test_name);

	std::vector<ObInfo> ob_infos;
	for(size_t i=0; i<test_obs.size(); ++i)
		ob_infos.push_back(test_obs[i].ob_info);

	ChunkBuildResults results;
	try
	{
		results = buildChunkForObInfo(ob_infos, chunk_x, /*chunk_y=*/-1000, task_manager);
	}
	catch(glare::Exception& e)
	{
		failTest(test_name + ": buildChunkForObInfo() threw: " + e.what());
	}

	testAssert(results.ob_batch_ranges.size() == test_obs.size());

	size_t num_included = 0;
	size_t expected_num_mats = 0;
	for(size_t i=0; i<test_obs.size(); ++i)
	{
		const ObjectBatchRanges& ranges = results.ob_batch_ranges[i];
		testAssert(ranges.ob_uid == test_obs[i].ob_info.ob_uid);
		testAssert(ranges.batch0_start <= ranges.batch0_end);
		testAssert(ranges.batch1_start <= ranges.batch1_end);

		const uint32 num_ob_indices = (ranges.batch0_end - ranges.batch0_start) + (ranges.batch1_end - ranges.batch1_start);
		if(test_obs[i].expect_included)
		{
			testAssert(num_ob_indices > 0);
			num_included++;
			expected_num_mats += test_obs[i].num_mesh_mats;
		}
		else
			testAssert(num_ob_indices == 0);
	}

	if(num_included == 0)
	{
		testAssert(results.combined_mesh_path.empty());
		testAssert(results.output_mat_infos.empty());
		return;
	}

	testAssert(!results.combined_mesh_path.empty());

	BatchedMeshRef chunk_mesh = BatchedMesh::readFromFile(results.combined_mesh_path, /*mem allocator=*/NULL);
	chunk_mesh->checkValidAndSanitiseMesh(); // Throws if any index is out of bounds.

	testAssert(chunk_mesh->numVerts() > 0);
	testAssert(chunk_mesh->numVerts() <= 24 * num_included);
	testAssert(results.output_mat_infos.size() == expected_num_mats);

	// Check every vertex references one of the output materials.
	const BatchedMesh::VertAttribute& mat_index_attr = chunk_mesh->getAttribute(BatchedMesh::VertAttribute_MatIndex);
	testAssert(mat_index_attr.component_type == BatchedMesh::ComponentType_UInt32);
	for(size_t v=0; v<chunk_mesh->numVerts(); ++v)
	{
		uint32 mat_i;
		std::memcpy(&mat_i, &chunk_mesh->vertex_data[chunk_mesh->vertexSize() * v + mat_index_attr.offset_B], sizeof(uint32));
		testAssert(mat_i < results.output_mat_infos.size());
	}

	// Check each object's index ranges are in bounds, don't overlap with other objects' ranges, and only reference verts of that object's cube.
	const float pos_tolerance = 0.1f; // Chunk mesh positions are quantised.
	for(size_t i=0; i<test_obs.size(); ++i)
	{
		const ObjectBatchRanges& ranges = results.ob_batch_ranges[i];
		testAssert(ranges.batch0_end <= chunk_mesh->numIndices());
		testAssert(ranges.batch1_end <= chunk_mesh->numIndices());

		for(size_t z=0; z<test_obs.size(); ++z)
			if(z != i)
			{
				const ObjectBatchRanges& other = results.ob_batch_ranges[z];
				testAssert(!rangesOverlap(ranges.batch0_start, ranges.batch0_end, other.batch0_start, other.batch0_end));
				testAssert(!rangesOverlap(ranges.batch1_start, ranges.batch1_end, other.batch1_start, other.batch1_end));
				testAssert(!rangesOverlap(ranges.batch0_start, ranges.batch0_end, other.batch1_start, other.batch1_end));
			}

		const Vec4f cube_centre = test_obs[i].ob_info.ob_to_world.getColumn(3) + test_obs[i].expected_offset;
		const uint32 range_starts[2] = { ranges.batch0_start, ranges.batch1_start };
		const uint32 range_ends[2]   = { ranges.batch0_end,   ranges.batch1_end };
		for(int r=0; r<2; ++r)
			for(uint32 z=range_starts[r]; z<range_ends[r]; ++z)
			{
				const Vec4f pos = chunk_mesh->getVertexPosition(chunk_mesh->getIndexAsUInt32(z));
				for(int c=0; c<3; ++c)
					testAssert(std::fabs(pos[c] - cube_centre[c]) <= test_cube_half_w + pos_tolerance);
			}
	}

	FileUtils::deleteFile(results.combined_mesh_path);
	FileUtils::deleteFile(results.optimised_mesh_path);
}


static void test()
{
	conPrint("ChunkGenThread::test()");

	glare::TaskManager task_manager;

	TestCubeSpec good_spec; // PackedNormal normals, float UVs

	TestCubeSpec good_no_normals_spec;
	good_no_normals_spec.have_normals = false;
	good_no_normals_spec.uv0_type = BatchedMesh::ComponentType_Half;

	TestCubeSpec good_no_uvs_spec;
	good_no_uvs_spec.have_uv0 = false;

	TestCubeSpec good_two_mats_spec;
	good_two_mats_spec.num_mats = 2;

	// Unsupported normal types.  These throw after the object's indices have been appended to the combined index lists.
	TestCubeSpec float_normals_spec;
	float_normals_spec.normal_type = BatchedMesh::ComponentType_Float;

	TestCubeSpec oct16_normals_spec;
	oct16_normals_spec.normal_type = BatchedMesh::ComponentType_Oct16;

	// Unsupported UV type.  Throws after normals have been written as well.
	TestCubeSpec uint16_uvs_spec;
	uint16_uvs_spec.uv0_type = BatchedMesh::ComponentType_UInt16;

	// Unsupported UV type, after geometric normals have been computed from the combined vertex data.
	TestCubeSpec no_normals_uint16_uvs_spec;
	no_normals_uint16_uvs_spec.have_normals = false;
	no_normals_uint16_uvs_spec.uv0_type = BatchedMesh::ComponentType_UInt16;

	// Unsupported UV type, with an opaque and a transparent batch, so both combined index lists have indices appended.
	TestCubeSpec two_mats_uint16_uvs_spec;
	two_mats_uint16_uvs_spec.num_mats = 2;
	two_mats_uint16_uvs_spec.uv0_type = BatchedMesh::ComponentType_UInt16;

	const std::string good_path						= writeTestCubeMesh("good", good_spec);
	const std::string good_no_normals_path			= writeTestCubeMesh("good_no_normals", good_no_normals_spec);
	const std::string good_no_uvs_path				= writeTestCubeMesh("good_no_uvs", good_no_uvs_spec);
	const std::string good_two_mats_path			= writeTestCubeMesh("good_two_mats", good_two_mats_spec);
	const std::string float_normals_path			= writeTestCubeMesh("float_normals", float_normals_spec);
	const std::string oct16_normals_path			= writeTestCubeMesh("oct16_normals", oct16_normals_spec);
	const std::string uint16_uvs_path				= writeTestCubeMesh("uint16_uvs", uint16_uvs_spec);
	const std::string no_normals_uint16_uvs_path	= writeTestCubeMesh("no_normals_uint16_uvs", no_normals_uint16_uvs_spec);
	const std::string two_mats_uint16_uvs_path		= writeTestCubeMesh("two_mats_uint16_uvs", two_mats_uint16_uvs_spec);
	const std::string missing_path					= PlatformUtils::getTempDirPath() + "/chunkgen_test_nonexistent_file.bmesh";

	TestCubeSpec uint32_indices_spec;
	uint32_indices_spec.index_type = BatchedMesh::ComponentType_UInt32;
	uint32_indices_spec.num_mats = 2;

	TestCubeSpec uint32_indices_float_normals_spec;
	uint32_indices_float_normals_spec.index_type = BatchedMesh::ComponentType_UInt32;
	uint32_indices_float_normals_spec.normal_type = BatchedMesh::ComponentType_Float;

	const std::string uint32_indices_path				= writeTestCubeMesh("uint32_indices", uint32_indices_spec);
	const std::string uint32_indices_float_normals_path	= writeTestCubeMesh("uint32_indices_float_normals", uint32_indices_float_normals_spec);

	TestCubeSpec quantised_spec;
	quantised_spec.quantise = true;

	const std::string quantised_path = writeTestCubeMesh("quantised", quantised_spec);
	testAssert(LODGeneration::loadModel(quantised_path)->getAttribute(BatchedMesh::VertAttribute_Position).component_type == BatchedMesh::ComponentType_UInt16);

	TestCubeSpec skinned_uint8_spec;
	skinned_uint8_spec.skinned = true;

	TestCubeSpec skinned_uint16_spec;
	skinned_uint16_spec.skinned = true;
	skinned_uint16_spec.joints_type = BatchedMesh::ComponentType_UInt16;
	skinned_uint16_spec.weights_type = BatchedMesh::ComponentType_UInt16;

	TestCubeSpec skinned_float_weights_spec;
	skinned_float_weights_spec.skinned = true;
	skinned_float_weights_spec.weights_type = BatchedMesh::ComponentType_Float;
	skinned_float_weights_spec.num_mats = 2;

	TestCubeSpec skinned_float_normals_spec;
	skinned_float_normals_spec.skinned = true;
	skinned_float_normals_spec.normal_type = BatchedMesh::ComponentType_Float;

	const std::string skinned_uint8_path			= writeTestCubeMesh("skinned_uint8", skinned_uint8_spec);
	const std::string skinned_uint16_path			= writeTestCubeMesh("skinned_uint16", skinned_uint16_spec);
	const std::string skinned_float_weights_path	= writeTestCubeMesh("skinned_float_weights", skinned_float_weights_spec);
	const std::string skinned_float_normals_path	= writeTestCubeMesh("skinned_float_normals", skinned_float_normals_spec);

	// Check the skin data survives writing and loading, so that the skinning code in buildChunkForObInfo() is what gets tested.
	{
		BatchedMeshRef mesh = LODGeneration::loadModel(skinned_uint16_path);
		testAssert(mesh->animation_data.joint_nodes.size() == 1);
		testAssert(mesh->getAttribute(BatchedMesh::VertAttribute_Joints).component_type == BatchedMesh::ComponentType_UInt16);
		testAssert(mesh->getAttribute(BatchedMesh::VertAttribute_Weights).component_type == BatchedMesh::ComponentType_UInt16);
	}

	// Check the uint32 index type survives loading and simplification, so that the uint32 index branch in buildChunkForObInfo() is what gets tested.
	{
		LRUCache<std::string, BatchedMeshRef> mesh_cache;
		Matrix4f voxel_scale_matrix;
		BatchedMeshRef mesh = loadAndSimplifyGeometry(makeObInfo(uint32_indices_path, 1, 0, {}), mesh_cache, voxel_scale_matrix);
		testAssert(mesh.nonNull() && (mesh->index_type == BatchedMesh::ComponentType_UInt32));

		mesh = loadAndSimplifyGeometry(makeObInfo(good_path, 1, 0, {}), mesh_cache, voxel_scale_matrix);
		testAssert(mesh.nonNull() && (mesh->index_type == BatchedMesh::ComponentType_UInt16));
	}

	const std::vector<float> opaque{ 1.f };
	const std::vector<float> transparent{ 0.5f };
	const std::vector<float> opaque_and_transparent{ 1.f, 0.5f };

	int chunk_x = -1000; // Use a different chunk for each test so the output files don't collide.

	testBuildChunk("All objects valid, with various supported attribute types", {
		{ makeObInfo(good_path,				1, 0,  opaque),			true,  1 },
		{ makeObInfo(good_no_normals_path,	2, 20, opaque),			true,  1 },
		{ makeObInfo(good_no_uvs_path,		3, 40, transparent),	true,  1 },
		{ makeObInfo(good_two_mats_path,	4, 60, opaque),			true,  2 }, // Fewer materials than the mesh references, so a dummy material gets added.
	}, task_manager, chunk_x++);

	testBuildChunk("Unsupported float normals between valid objects", {
		{ makeObInfo(good_path,				1, 0,  opaque), true,  1 },
		{ makeObInfo(float_normals_path,	2, 20, opaque), false, 1 },
		{ makeObInfo(good_path,				3, 40, opaque), true,  1 },
	}, task_manager, chunk_x++);

	testBuildChunk("Unsupported oct16 normals on a transparent object", {
		{ makeObInfo(good_path,				1, 0,  opaque),			true,  1 },
		{ makeObInfo(oct16_normals_path,	2, 20, transparent),	false, 1 },
		{ makeObInfo(good_path,				3, 40, transparent),	true,  1 },
	}, task_manager, chunk_x++);

	testBuildChunk("Unsupported UVs", {
		{ makeObInfo(good_path,				1, 0,  opaque), true,  1 },
		{ makeObInfo(uint16_uvs_path,		2, 20, opaque), false, 1 },
		{ makeObInfo(good_path,				3, 40, opaque), true,  1 },
	}, task_manager, chunk_x++);

	testBuildChunk("Unsupported UVs after computing geometric normals", {
		{ makeObInfo(no_normals_uint16_uvs_path,	1, 0,  opaque), false, 1 },
		{ makeObInfo(good_no_normals_path,			2, 20, opaque), true,  1 },
	}, task_manager, chunk_x++);

	testBuildChunk("Unsupported UVs on an object with opaque and transparent batches", {
		{ makeObInfo(good_path,					1, 0,  transparent),			true,  1 },
		{ makeObInfo(two_mats_uint16_uvs_path,	2, 20, opaque_and_transparent),	false, 2 },
		{ makeObInfo(good_path,					3, 40, opaque),					true,  1 },
		{ makeObInfo(good_two_mats_path,		4, 60, opaque_and_transparent),	true,  2 },
	}, task_manager, chunk_x++);

	testBuildChunk("Failing objects first and last, sharing a cached mesh", {
		{ makeObInfo(float_normals_path,	1, 0,  opaque), false, 1 },
		{ makeObInfo(good_path,				2, 20, opaque), true,  1 },
		{ makeObInfo(float_normals_path,	3, 40, opaque), false, 1 },
	}, task_manager, chunk_x++);

	testBuildChunk("Model file fails to load", {
		{ makeObInfo(good_path,		1, 0,  opaque), true,  1 },
		{ makeObInfo(missing_path,	2, 20, opaque), false, 1 },
		{ makeObInfo(good_path,		3, 40, opaque), true,  1 },
	}, task_manager, chunk_x++);

	testBuildChunk("All objects fail", {
		{ makeObInfo(float_normals_path,	1, 0,  opaque),			false, 1 },
		{ makeObInfo(uint16_uvs_path,		2, 20, transparent),	false, 1 },
		{ makeObInfo(missing_path,			3, 40, opaque),			false, 1 },
	}, task_manager, chunk_x++);

	testBuildChunk("UInt32 indices", {
		{ makeObInfo(uint32_indices_path,	1, 0,  opaque_and_transparent),	true,  2 },
		{ makeObInfo(good_path,				2, 20, opaque),					true,  1 },
		{ makeObInfo(uint32_indices_path,	3, 40, opaque),					true,  2 }, // Second material is a dummy (transparent) material.
	}, task_manager, chunk_x++);

	testBuildChunk("UInt32 indices, failing object last", {
		{ makeObInfo(good_path,							1, 0,  opaque),	true,  1 },
		{ makeObInfo(uint32_indices_path,				2, 20, opaque),	true,  2 },
		{ makeObInfo(uint32_indices_float_normals_path,	3, 40, opaque),	false, 1 },
	}, task_manager, chunk_x++);

	testBuildChunk("Non-float positions", {
		{ makeObInfo(good_path,			1, 0,  opaque),			true,  1 },
		{ makeObInfo(quantised_path,	2, 20, transparent),	false, 1 },
		{ makeObInfo(good_path,			3, 40, transparent),	true,  1 },
		{ makeObInfo(quantised_path,	4, 60, opaque),			false, 1 },
	}, task_manager, chunk_x++);

	// Skinned cubes should end up offset by the skin's bind-pose transform.
	testBuildChunk("Skinned meshes", {
		{ makeObInfo(good_path,					1, 0,  opaque),					true,  1 },
		{ makeObInfo(skinned_uint8_path,		2, 20, opaque),					true,  1, test_skin_offset },
		{ makeObInfo(skinned_uint16_path,		3, 40, transparent),			true,  1, test_skin_offset },
		{ makeObInfo(skinned_float_weights_path,4, 60, opaque_and_transparent),	true,  2, test_skin_offset },
	}, task_manager, chunk_x++);

	testBuildChunk("Skinned meshes, failing object last", {
		{ makeObInfo(skinned_uint8_path,			1, 0,  opaque),	true,  1, test_skin_offset },
		{ makeObInfo(good_path,						2, 20, opaque),	true,  1 },
		{ makeObInfo(skinned_float_normals_path,	3, 40, opaque),	false, 1 },
	}, task_manager, chunk_x++);

	const std::string paths[] = { good_path, good_no_normals_path, good_no_uvs_path, good_two_mats_path, float_normals_path, oct16_normals_path, uint16_uvs_path,
		no_normals_uint16_uvs_path, two_mats_uint16_uvs_path, uint32_indices_path, uint32_indices_float_normals_path, quantised_path,
		skinned_uint8_path, skinned_uint16_path, skinned_float_weights_path, skinned_float_normals_path };
	for(size_t i=0; i<staticArrayNumElems(paths); ++i)
		FileUtils::deleteFile(paths[i]);

	conPrint("ChunkGenThread::test() done.");
}


} // end namespace ChunkGenThreadTests


void ChunkGenThread::test()
{
	ChunkGenThreadTests::test();
}


#endif // BUILD_TESTS
