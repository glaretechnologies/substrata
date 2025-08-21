/*=====================================================================
ChunkGenThread.cpp
------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "ChunkGenThread.h"


#include "ServerWorldState.h"
#include "../shared/LODGeneration.h"
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
#if !GUI_CLIENT
#include <encoder/basisu_comp.h>
#endif
#include <zstd.h>
#include <FileChecksum.h>


static const float chunk_w = 128;


ChunkGenThread::ChunkGenThread(ServerAllWorldsState* all_worlds_state_)
:	all_worlds_state(all_worlds_state_)
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
				conPrint("Loading '" + ob_info.model_path + "'...");
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
		// conPrint("Simplifying mesh..");

		const size_t original_num_verts = mesh->numVerts();

		const float error_threshold_ws = 0.4f;
		const float relative_err = 0.08f;
		const float global_error_threshold_os = error_threshold_ws / (ob_info.ob_to_world_scale * voxel_scale);
		const float per_ob_error_threshold_os = mesh->aabb_os.longestLength() * relative_err;

		const float error_threshold_os = myMax(global_error_threshold_os, per_ob_error_threshold_os);
		//printVar(error_threshold_ws);
		//printVar(error_threshold_os);

		mesh = MeshSimplification::removeSmallComponents(mesh, error_threshold_os);
		if(mesh->numIndices() == 0)
			return mesh;

		mesh = MeshSimplification::buildSimplifiedMesh(*mesh, /*target_reduction_ratio=*/1000.f, /*target_error=*/error_threshold_os, /*sloppy=*/false);
		
		// If we achieved less than a 4x reduction in the number of vertices (and this is a med/large mesh), try again with sloppy simplification
		if((mesh->numVerts() > 1024) && // if this is a med/large mesh
			((float)mesh->numVerts() > (original_num_verts / 4.f)))
		{
			mesh = MeshSimplification::buildSimplifiedMesh(*mesh, /*target_reduction_ratio=*/1000.f, 
				/*target_error (relative)=*/relative_err, /*sloppy=*/true);
		}
	}
	return mesh;
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

				conPrint("Loading '" + tex_path + "'...");
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
				conPrint("Error while loading image: " + e.what());
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

			conPrint("Basisu compression and writing of file to '" + params.m_out_filename + "' took " + timer.elapsedStringNSigFigs(3));

			// Compute hash over it
			const uint64 hash = FileChecksum::fileChecksum(params.m_out_filename);

			combined_texture_path_out = params.m_out_filename;
			combined_texture_hash_out = hash;
		}
		else
			conPrint("Not writing texture array, no textures to process.");
	}
	else
		conPrint("Not writing texture array, no textures to process.");
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

		const size_t initial_combined_mesh_vert_data_size = combined_mesh->vertex_data.size();

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
					conPrint("Warning: ob_to_world not invertible.");
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


				results.ob_batch_ranges[ob_i].ob_uid = ob_info.ob_uid;
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
			conPrint("ChunkGenThread error while processing ob: " + e.what());

			// If an exception was thrown after space was allocated for the mesh verts, we want to trim that off.
			combined_mesh->vertex_data.resize(initial_combined_mesh_vert_data_size);
		}
	} // End for each ob

	
	js::Vector<uint32> combined_indices = combined_opaque_indices;
	combined_indices.append(combined_trans_indices);

	for(size_t z=0; z<results.ob_batch_ranges.size(); ++z)
	{
		results.ob_batch_ranges[z].batch1_start += (uint32)combined_opaque_indices.size();
		results.ob_batch_ranges[z].batch1_end   += (uint32)combined_opaque_indices.size();
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
				conPrint("Raw combined mesh num materials: " + toString(combined_mat_infos.size()));

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

				conPrint("Used combined mesh num materials: " + toString(new_mat_infos.size()));
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
			conPrint("Writing combined mesh to disk...");
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

			printVar(num_obs_combined);
			printVar(num_batches_combined);
			conPrint("Wrote chunk mesh to '" + path + "'.");

			// Compute hash over it
			const uint64 hash = FileChecksum::fileChecksum(path);


			//--------------------------- Build optimised mesh ---------------------------
			// Can't do meshopt optimisations because they reorder indices, which we need to preserve for object index ranges.

			BatchedMesh::QuantiseOptions quantise_options;
			quantise_options.pos_bits = 13;
			quantise_options.uv_bits  = 8;
			combined_mesh = combined_mesh->buildQuantisedMesh(quantise_options);

			const std::string opt_mesh_path = path + "_opt"; // The final optimised mesh URL will be computed later.

			// Write optimised mesh (using quantised position etc.)
			{
				BatchedMesh::WriteOptions options;
				options.compression_level = 19;
				options.use_meshopt = true;
				combined_mesh->writeToFile(opt_mesh_path, options);
			}

			conPrint("Wrote optimised chunk mesh to '" + opt_mesh_path + "'.");
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
		return true;

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
		if(extension > 6.f)
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
			conPrint("Updating EXCLUDE_FROM_LOD_CHUNK_MESH flag for ob to " + toString(should_exclude));
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
				conPrint("Adding new LODChunk with coords " + chunk_coords.toString());

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
				conPrint("Object " + ob->uid.toString() + " exclude-from-chunk changed to " + boolToString(should_exclude) + ", marking chunk " + chunk_coords.toString() + " as needs-rebuild.");
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
			Reference<ServerWorldState> world_state = all_worlds_state->getRootWorldState();
			//Reference<ServerWorldState> world_state = all_worlds_state->world_states["cryptovoxels"];
			
			//for(int x=-10; x<10; ++x)
			//for(int y=-10; y<10; ++y)
			int x = 0;
			int y = 0;
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

				conPrint("================================= Building chunk " + toString(x) + ", " + toString(y) + " (" + toString(i) + "/" + toString(dirty_chunks.size()) + " dirty chunks) =================================");

				const ChunkBuildResults results = buildChunk(all_worlds_state, dirty_chunks[i].world_state, chunk_aabb, x, y, task_manager);

				conPrint("====== chunk " + toString(x) + ", " + toString(y) + " built. ======");

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

				const int MESH_EPOCH = 2; // This can be bumped to punch through caches, in particular if the optimised mesh needs to be rebuilt.
				// Note that because we store mesh_url in the LodChunk object, which is sent to clients, they will automatically pick up a new epoch version if it's incremented.

				std::string mesh_URL;
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
					const std::string optimised_mesh_URL = removeDotAndExtension(ResourceManager::URLForPathAndHashAndEpoch(results.combined_mesh_path, results.combined_mesh_hash, MESH_EPOCH)) + "_opt" + toString(Protocol::OPTIMISED_MESH_VERSION) + ".bmesh";

					if(!all_worlds_state->resource_manager->isFileForURLPresent(optimised_mesh_URL))
					{
						all_worlds_state->resource_manager->copyLocalFileToResourceDir(results.optimised_mesh_path, optimised_mesh_URL);

						WorldStateLock lock(all_worlds_state->mutex);
						all_worlds_state->addResourceAsDBDirty(all_worlds_state->resource_manager->getOrCreateResourceForURL(optimised_mesh_URL));
					}
				}

				std::string tex_URL;
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
				}
				

				// TODO: Send out a chunk-updated message to clients
			}

			if(!dirty_chunks.empty())
				conPrint("---------Finished building " + toString(dirty_chunks.size()) + " dirty chunks.---------");

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
