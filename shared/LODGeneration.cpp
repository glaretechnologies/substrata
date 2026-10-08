/*=====================================================================
LODGeneration.cpp
-----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "LODGeneration.h"


#include "ImageDecoding.h"
#include "../server/ServerWorldState.h"
#include <ConPrint.h>
#include <Exception.h>
#include <Lock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <RuntimeCheck.h>
#include <KillThreadMessage.h>
#include <FileUtils.h>
#include <Timer.h>
#include <TaskManager.h>
#include <graphics/MeshSimplification.h>
#include <graphics/formatdecoderobj.h>
#include <graphics/FormatDecoderSTL.h>
#include <graphics/FormatDecoderGLTF.h>
#include <graphics/GifDecoder.h>
#include <graphics/jpegdecoder.h>
#include <graphics/PNGDecoder.h>
#include <graphics/Map2D.h>
#include <graphics/ImageMap.h>
#include <graphics/ImageMapSequence.h>
#include <graphics/TextureProcessing.h>
#include <graphics/KTXDecoder.h>
#include <graphics/WebPDecoder.h>
#include <dll/include/IndigoMesh.h>
#include <dll/include/IndigoException.h>
#include <dll/IndigoStringUtils.h>
#include <encoder/basisu_comp.h>
#if !GUI_CLIENT
//#include <encoder/basisu_comp.h>
#include <bc7enc_rdo/rgbcx.h>
#include <bc7enc_rdo/ert.h>
#endif
#include <Task.h>

namespace LODGeneration
{


BatchedMeshRef loadModel(const std::string& model_path)
{
	MemMappedFile file(model_path);

	return loadModelFromBuffer(model_path, file.fileData(), file.fileSize());
}


BatchedMeshRef loadModelFromBuffer(const std::string& model_path, const void* data, const size_t datalen)
{
	BatchedMeshRef batched_mesh;

	if(hasExtension(model_path, "obj"))
	{
		Indigo::MeshRef mesh = new Indigo::Mesh();

		MLTLibMaterials mats;
		FormatDecoderObj::loadModelFromBuffer((const uint8*)data, datalen, model_path, *mesh, 1.f, /*parse mtllib=*/false, mats); // Throws glare::Exception on failure.

		batched_mesh = BatchedMesh::buildFromIndigoMesh(*mesh);
	}
	else if(hasExtension(model_path, "stl"))
	{
		Indigo::MeshRef mesh = new Indigo::Mesh();

		FormatDecoderSTL::loadModelFromBuffer((const uint8*)data, datalen, *mesh, 1.f);

		batched_mesh = BatchedMesh::buildFromIndigoMesh(*mesh);
	}
	else if(hasExtension(model_path, "gltf"))
	{
		const std::string gltf_base_dir = FileUtils::getDirectory(model_path);
		GLTFLoadedData gltf_data;
		batched_mesh = FormatDecoderGLTF::loadGLTFFileFromData(data, datalen, gltf_base_dir, /*write_images_to_disk=*/false, /*restrict_uris_to_base_dir=*/true, gltf_data);
	}
	else if(hasExtension(model_path, "igmesh"))
	{
		Indigo::MeshRef mesh = new Indigo::Mesh();

		try
		{
			Indigo::Mesh::readFromBuffer((const uint8*)data, datalen, *mesh);
		}
		catch(Indigo::IndigoException& e)
		{
			throw glare::Exception(toStdString(e.what()));
		}

		batched_mesh = BatchedMesh::buildFromIndigoMesh(*mesh);
	}
	else if(hasExtension(model_path, "bmesh"))
	{
		batched_mesh = BatchedMesh::readFromData(data, datalen, /*mem allocator=*/NULL);
	}
	else
		throw glare::Exception("Format not supported: " + getExtension(model_path));

	batched_mesh->checkValidAndSanitiseMesh();

	return batched_mesh;
}


static BatchedMeshRef simplerMesh(BatchedMeshRef a, BatchedMeshRef b)
{
	return a->numIndices() < b->numIndices() ? a : b;
}


/*
	 |\        /|
	 | \     /  |
	w|  \  /    |
	 |   /      |h
	 | /  \     |
	 /      \   |
	          \ |
	     |
	 < l ><-- 1 ->


	w = sensor width
	l = lens-sensor dist
	h = projected length = object length at distance 1.
	w/l = h/1 = h

	For a render resolution of 2560 x 1282 pixels,

	pixel/h = 2560 / (w/l) = 1828.571428 pixels/projected_len_h

	Consider the projected length at the lod 0 / lod 1 transition: 0.16.  (See WorldObject::getLODLevel() for the 0.16 threshold)
	projected length h = 0.16 gives 0.16 * pixel/h = 0.16 * 1828.57142 = 292.57 pixels

	So a relative error (error relative to size of object) of 0.004 corresponds to 
	0.004 * 292.57 = 1.17 pixel error

	For the sloppy case, a relative error of 0.034 corresponds to
	0.034 * 292.571 = 9.947 pixel error

	Note that for the sloppy case, the algorithm is completely different, and the error thresholds are not really comparable in terms of visual error.
	The error thresholds for sloppy need to be *much* larger to look similar to the non-sloppy result.


	At lod -1 / lod 0 transition, projected length = 0.4 (See WorldObject::getLODLevel())
	projected length h = 0.4 gives 0.4 * pixel/h = 0.4 * 1828.57142 = 731 pixels

	So a relative error (error relative to size of object) of 0.0008 corresponds to 
	0.0008 * 731  = 0.58 pixel error
*/
BatchedMeshRef computeLODModel(BatchedMeshRef batched_mesh, int lod_level)
{
	float target_error_rel;
	float target_error_rel_sloppy;
	size_t sloppy_tri_threshold; // Number of tris in the non-sloppy simplified mesh at which we should also try using sloppy simplification.
	
	if(lod_level == 0)
	{
		target_error_rel        = 0.0008f; // Eyeballed as about right for the lod-1/lod0 transition, gives ~0.58 pixel error at 2560 pixel resolution.
		target_error_rel_sloppy = 0.01f;
		sloppy_tri_threshold    = 100000;
	}
	else if(lod_level == 1)
	{
		target_error_rel        = 0.004f; // Eyeballed as about right for the lod0/lod1 transition, gives ~1.17 pixel error at 2560 pixel resolution.
		target_error_rel_sloppy = 0.034f;
		sloppy_tri_threshold    = 20000;
	}
	else
	{
		assert(lod_level == 2);
		target_error_rel        = 0.004f * 5.33f; // = 0.021.   lod2/lod1 threshold is 5.33 times further away than lod0/lod1 threshold.
		target_error_rel_sloppy = 0.08f;
		sloppy_tri_threshold    = 1500;
	}

	if(!(batched_mesh->aabb_os.min_.isFinite() && batched_mesh->aabb_os.max_.isFinite()))
		throw glare::Exception("computeLODModel(): Invalid mesh aabb_os: " + batched_mesh->aabb_os.toString());

	const float target_error_abs = batched_mesh->aabb_os.longestLength() * target_error_rel;
	BatchedMeshRef simplified_mesh = MeshSimplification::buildSimplifiedMesh(*batched_mesh, /*target_reduction_ratio=*/100000.f, /*target_error=*/target_error_abs, /*sloppy=*/false);

	// If the simplified mesh is still quite complex, try again with sloppy simplification.
	if((simplified_mesh->numIndices()/3) > sloppy_tri_threshold)
	{
		// Note that since sloppy is true here, the target error we need to pass in is relative to the object extents.
		BatchedMeshRef sloppy_mesh = MeshSimplification::buildSimplifiedMesh(*batched_mesh, /*target_reduction_ratio=*/100000.f, /*target_error=*/target_error_rel_sloppy, /*sloppy=*/true);

		return simplerMesh(sloppy_mesh, simplerMesh(simplified_mesh, batched_mesh)); // Return the mesh that actually ended up the most simple.
	}
	else
		return simplerMesh(simplified_mesh, batched_mesh);  // Return the mesh that actually ended up the most simple.
}


void generateLODModel(BatchedMeshRef batched_mesh, int lod_level, const std::string& LOD_model_path)
{
	BatchedMeshRef simplified_mesh = computeLODModel(batched_mesh, lod_level);
	simplified_mesh->writeToFile(LOD_model_path);
}


void generateLODModel(const std::string& model_path, int lod_level, const std::string& LOD_model_path)
{
	BatchedMeshRef batched_mesh = loadModel(model_path);

	generateLODModel(batched_mesh, lod_level, LOD_model_path);
}


bool isMeshQuantised(BatchedMeshRef batched_mesh)
{
	const BatchedMesh::VertAttribute& pos_attr = batched_mesh->getAttribute(BatchedMesh::VertAttribute_Position);
	return pos_attr.component_type != BatchedMesh::ComponentType_Float;
}


// Writes to test_out_stream if non-null, otherwise writes to disk at optimised_mesh_path.
void generateOptimisedMesh(const std::string& source_mesh_abs_path, const void* mesh_buffer, size_t mesh_buffer_size, int min_lod_level, int lod_level, const std::string& optimised_mesh_path, OutStream* test_out_stream)
{
	assert(min_lod_level == -1 || min_lod_level == 0);
	assert(min_lod_level <= lod_level);

	BatchedMeshRef batched_mesh = LODGeneration::loadModelFromBuffer(source_mesh_abs_path, mesh_buffer, mesh_buffer_size);

	if(lod_level > min_lod_level)
		batched_mesh = LODGeneration::computeLODModel(batched_mesh, lod_level);

	if(!isMeshQuantised(batched_mesh))
	{
		BatchedMesh::QuantiseOptions quantise_options;
		quantise_options.pos_bits = (lod_level == min_lod_level) ? 16 : 12;
		quantise_options.uv_bits  = (lod_level == min_lod_level) ? 16 : 10;
		batched_mesh = batched_mesh->buildQuantisedMesh(quantise_options);
	}

	batched_mesh->doMeshOptimizerOptimisations();

	BatchedMesh::WriteOptions options;
	options.use_meshopt = true;
	options.compression_level = 19;

	if(test_out_stream)
		batched_mesh->writeToOutStream(*test_out_stream, options);
	else
		batched_mesh->writeToFile(optimised_mesh_path, options);
}


bool textureHasAlphaChannel(const std::string& tex_path)
{
	if(hasExtension(tex_path, "gif") || hasExtension(tex_path, "jpg"))
		return false;
	else
	{
		Reference<Map2D> map = ImageDecoding::decodeImage(".", tex_path); // Load texture from disk and decode it.

		return map->hasAlphaChannel() && !map->isAlphaChannelAllWhite();
	}
}


bool textureHasAlphaChannel(const std::string& tex_path, Map2DRef map)
{
	if(hasExtension(tex_path, "gif") || hasExtension(tex_path, "jpg")) // Some formats can't have alpha at all, so just check the extension to cover those.
		return false;
	else
	{
		return map->hasAlphaChannel() && !map->isAlphaChannelAllWhite();
	}
}


// From TextureProcessing.cpp
static Reference<ImageMapUInt8> convertUInt16ToUInt8ImageMap(const ImageMap<uint16, UInt16ComponentValueTraits>& map)
{
	Reference<ImageMapUInt8> new_map = new ImageMapUInt8(map.getWidth(), map.getHeight(), map.getN());
	for(size_t i=0; i<map.getDataSize(); ++i)
		new_map->getData()[i] = (uint8)(map.getData()[i] / 256);
	return new_map;
}


void generateLODTexture(const std::string& base_tex_path, int lod_level, const std::string& LOD_tex_path, glare::TaskManager& task_manager)
{
	const int new_max_w_h = (lod_level == 0) ? 1024 : ((lod_level == 1) ? 256 : 64);
	const int min_w_h = 1;

	Reference<Map2D> map;
	if(hasExtension(base_tex_path, "gif"))
	{
		GIFDecoder::resizeGIF(base_tex_path, LOD_tex_path, new_max_w_h);
	}
	else
	{
		map = ImageDecoding::decodeImage(".", base_tex_path); // Load texture from disk and decode it.

		// If the map is a 16-bit image, convert to 8-bit first.
		if(dynamic_cast<const ImageMap<uint16, UInt16ComponentValueTraits>*>(map.ptr()))
		{
			map = convertUInt16ToUInt8ImageMap(static_cast<const ImageMap<uint16, UInt16ComponentValueTraits>&>(*map));
		}

		if((map->getMapWidth() == 0) || (map->getMapHeight() == 0) || (map->numChannels() == 0))
			throw glare::Exception("Invalid image dimensions (zero)");

		if(dynamic_cast<const ImageMapUInt8*>(map.ptr()))
		{
			int new_w, new_h;
			if(map->getMapWidth() > map->getMapHeight())
			{
				new_w = myMin((int)map->getMapWidth(), new_max_w_h);
				new_h = myMax(min_w_h, (int)((float)new_w * (float)map->getMapHeight() / (float)map->getMapWidth()));
			}
			else
			{
				new_h = myMin((int)map->getMapHeight(), new_max_w_h);
				new_w = myMax(min_w_h, (int)((float)new_h * (float)map->getMapWidth() / (float)map->getMapHeight()));
			}

			conPrint("\tMaking LOD texture with dimensions " + toString(new_w) + " * " + toString(new_h) + " for LOD level " + toString(lod_level));

			const ImageMapUInt8* imagemap = map.downcastToPtr<ImageMapUInt8>();

			Reference<Map2D> resized_map = imagemap->resizeMidQuality(new_w, new_h, &task_manager);
			assert(resized_map.isType<ImageMapUInt8>());

			// Save as a JPEG or PNG depending if there is an alpha channel.
			if(hasExtension(LOD_tex_path, "jpg"))
			{
				if(resized_map->numChannels() > 3)
				{
					// Convert to a 3 channel image
					resized_map = resized_map.downcast<ImageMapUInt8>()->extract3ChannelImage();
				}


				JPEGDecoder::SaveOptions options;
				options.quality = 90;
				JPEGDecoder::save(resized_map.downcast<ImageMapUInt8>(), LOD_tex_path, options);
			}
			else if(hasExtension(LOD_tex_path, "png"))
			{
				PNGDecoder::write(*resized_map.downcastToPtr<ImageMapUInt8>(), LOD_tex_path);
			}
			else
			{
				throw glare::Exception("not saving basis files in generateLODTexture().");
			}
		}
		else
			throw glare::Exception("Unhandled image type (not ImageMapUInt8): " + base_tex_path);
	}
}


void generateBasisTexture(const std::string& src_tex_path, int base_lod_level, int lod_level, const std::string& basis_tex_path, glare::TaskManager& task_manager)
{
#if GUI_CLIENT
	throw glare::Exception("generateBasisTexture not supported.");
#else

	int new_max_w_h;
	if(lod_level == base_lod_level)
		new_max_w_h = 4096; // Basis compression can get pretty slow for large textures, so limit the texture size.
	else
		new_max_w_h = (lod_level == 0) ? 1024 : ((lod_level == 1) ? 256 : 64);

	const int min_w_h = 1;

	Reference<Map2D> map;
	if(hasExtension(src_tex_path, "gif"))
	{
		map = GIFDecoder::decodeImageSequence(src_tex_path);
	}
	else if(hasExtension(src_tex_path, "webp"))
	{
		map = WebPDecoder::decodeImageOrSequence(src_tex_path);
	}
	else
	{
		//Timer timer;
		map = ImageDecoding::decodeImage(".", src_tex_path); // Load texture from disk and decode it.
		//conPrint("Decoding took " + timer.elapsedString());
	}

	// If the map is a 16-bit image, convert to 8-bit first.
	if(dynamic_cast<const ImageMap<uint16, UInt16ComponentValueTraits>*>(map.ptr()))
	{
		map = convertUInt16ToUInt8ImageMap(static_cast<const ImageMap<uint16, UInt16ComponentValueTraits>&>(*map));
	}

	if((map->getMapWidth() == 0) || (map->getMapHeight() == 0) || (map->numChannels() == 0))
		throw glare::Exception("Invalid image dimensions (zero)");

	int new_w, new_h;
	if(map->getMapWidth() > map->getMapHeight())
	{
		new_w = myMin((int)map->getMapWidth(), new_max_w_h);
		new_h = myMax(min_w_h, (int)((float)new_w * (float)map->getMapHeight() / (float)map->getMapWidth()));
	}
	else
	{
		new_h = myMin((int)map->getMapHeight(), new_max_w_h);
		new_w = myMax(min_w_h, (int)((float)new_h * (float)map->getMapWidth() / (float)map->getMapHeight()));
	}


	new_w = Maths::roundUpToMultipleOfPowerOf2(new_w, 4); // There seems to be a WebGL / 3.js limitation where the texture dimensions must be a multiple of 4.
	new_h = Maths::roundUpToMultipleOfPowerOf2(new_h, 4);

	int quality_level = 255;
	if(lod_level >= 1)
		quality_level = 128;

	conPrint("\tMaking basis file with dimensions " + toString(new_w) + " * " + toString(new_h) + ", quality " + toString(quality_level) + " for LOD level " + toString(lod_level));

	if(dynamic_cast<const ImageMapUInt8*>(map.ptr()))
	{
		const ImageMapUInt8* imagemap = map.downcastToPtr<ImageMapUInt8>();

		Reference<Map2D> resized_map = imagemap->resizeMidQuality(new_w, new_h, &task_manager);
		runtimeCheck(resized_map.isType<ImageMapUInt8>());

		writeBasisUniversalFile(*resized_map.downcast<ImageMapUInt8>(), basis_tex_path, quality_level);
	}
	else if(dynamic_cast<const ImageMapSequenceUInt8*>(map.ptr()))
	{
		const ImageMapSequenceUInt8* seq = map.downcastToPtr<ImageMapSequenceUInt8>();

		conPrint("\t\tFile is image sequence with " + toString(seq->images.size()) + " images.");

		Reference<Map2D> resized_seq = seq->resizeMidQuality(new_w, new_h, &task_manager);
		runtimeCheck(resized_seq.isType<ImageMapSequenceUInt8>());

		writeBasisUniversalFileForSequence(*resized_seq.downcast<ImageMapSequenceUInt8>(), basis_tex_path, quality_level);
	}
	else
		throw glare::Exception("Unhandled image type: " + src_tex_path);
	
#endif
}


#if !GUI_CLIENT

namespace
{

// RDO (rate distortion optimisation) quality/size tradeoffs.  Higher = smaller zstd-compressed files, lower quality.  0 = no RDO.
// The LOD level 1 and 2 textures are only seen from a distance, so use a higher value for them, as for the basis quality level in generateBasisTexture().
static const float BC13_RDO_LAMBDA = 2.f;
static const float BC13_LOD_RDO_LAMBDA = 8.f; // For LOD levels >= base level.  Measured on 256 px textures: ~15-65% smaller than lambda 2 depending on content, for 1-3 dB lower PSNR.
static const int BC13_ZSTD_LEVEL = 19; // Slower to compress than lower levels, but not much slower to decompress (probably).


static Mutex rgbcx_init_mutex;
static bool rgbcx_initialised GUARDED_BY(rgbcx_init_mutex) = false;

static void initRGBCX()
{
	Lock lock(rgbcx_init_mutex);
	if(!rgbcx_initialised)
	{
		rgbcx::init(rgbcx::bc1_approx_mode::cBC1Ideal);
		rgbcx_initialised = true;
	}
}


// Block unpackers for the RDO post-process (ert::reduce_entropy), as in bc7enc_rdo's rdo_bc_encoder.  They return false for blocks the RDO process isn't allowed to produce.
static bool unpackBC1BlockForRDO(const void* block, ert::color_rgba* pixels, uint32_t /*block_index*/, void* user_data)
{
	const bool allow_3_colour_mode = user_data != nullptr;
	const bool used_3_colour_mode = rgbcx::unpack_bc1(block, pixels, /*set_alpha=*/true, rgbcx::bc1_approx_mode::cBC1Ideal);
	if(used_3_colour_mode)
	{
		if(!allow_3_colour_mode)
			return false;

		// Don't allow selector 3 in 3-colour mode (which decodes as transparent black in RGBA BC1, and as black in RGB BC1), as the encoder doesn't use it either.
		const rgbcx::bc1_block* bc1_block = (const rgbcx::bc1_block*)block;
		for(uint32_t y=0; y<4; ++y)
		for(uint32_t x=0; x<4; ++x)
			if(bc1_block->get_selector(x, y) == 3)
				return false;
	}
	return true;
}


static bool unpackBC4BlockForRDO(const void* block, ert::color_rgba* pixels, uint32_t /*block_index*/, void* /*user_data*/)
{
	std::memset(pixels, 0, sizeof(ert::color_rgba) * 16);
	rgbcx::unpack_bc4(block, (uint8_t*)pixels, /*stride=*/4);
	return true;
}


struct BC13EncodeClosure
{
	const ert::color_rgba* block_pixels; // 16 RGBA pixels per block
	uint8* blocks_out;
	bool use_bc3;
	float rdo_lambda;
};


// Encodes blocks [begin, end), then does the RDO post-process on them.  The RDO process only looks back within the range of blocks for each task.
class BC13EncodeTask : public glare::Task
{
public:
	BC13EncodeTask(const BC13EncodeClosure& closure_, size_t begin_, size_t end_) : closure(closure_), begin(begin_), end(end_) {}

	virtual void run(size_t /*thread_index*/)
	{
		const size_t block_size = closure.use_bc3 ? 16 : 8;
		const uint32_t num_blocks = (uint32_t)(end - begin);
		uint8* const blocks = closure.blocks_out + begin * block_size;
		const ert::color_rgba* const block_pixels = closure.block_pixels + begin * 16;

		for(size_t i=0; i<num_blocks; ++i)
		{
			if(closure.use_bc3)
				rgbcx::encode_bc3(rgbcx::MAX_LEVEL, blocks + i * 16, (const uint8_t*)&block_pixels[i * 16]);
			else
				rgbcx::encode_bc1(rgbcx::MAX_LEVEL, blocks + i * 8, (const uint8_t*)&block_pixels[i * 16], /*allow_3color=*/true, /*use_transparent_texels_for_black=*/false);
		}

		if(closure.rdo_lambda <= 0)
			return;

		// RDO parameters, as in bc7enc_rdo's rdo_bc_encoder::postprocess_rdo().
		ert::reduce_entropy_params rgb_params;
		rgb_params.m_lambda = closure.rdo_lambda;
		rgb_params.m_lookback_window_size = 128;
		rgb_params.m_try_two_matches = true;
		rgb_params.m_smooth_block_max_mse_scale = 15.f + (50.f - 15.f) * myMin(1.f, closure.rdo_lambda / 8.f);
		rgb_params.m_color_weights[3] = 0;
		uint32_t num_modified = 0;

		if(closure.use_bc3)
		{
			// Alpha block (BC4) followed by colour block (BC1, which must use 4-colour mode in BC3).
			std::vector<ert::color_rgba> alpha_pixels(num_blocks * 16);
			for(size_t i=0; i<num_blocks * 16; ++i)
			{
				alpha_pixels[i].m_c[0] = block_pixels[i].m_c[3];
				alpha_pixels[i].m_c[1] = alpha_pixels[i].m_c[2] = alpha_pixels[i].m_c[3] = 0;
			}

			ert::reduce_entropy_params alpha_params = rgb_params;
			alpha_params.m_lookback_window_size = myMax(16u, rgb_params.m_lookback_window_size);
			alpha_params.m_smooth_block_max_mse_scale = 10.f + (30.f - 10.f) * myMin(1.f, closure.rdo_lambda / 4.f);
			alpha_params.m_color_weights[1] = alpha_params.m_color_weights[2] = alpha_params.m_color_weights[3] = 0;

			ert::reduce_entropy(blocks, num_blocks, /*total_block_stride_in_bytes=*/16, /*block_size_to_optimize_in_bytes=*/8, 4, 4, /*num_comps=*/1,
				alpha_pixels.data(), alpha_params, num_modified, unpackBC4BlockForRDO, /*user data=*/nullptr);

			ert::reduce_entropy(blocks + 8, num_blocks, /*total_block_stride_in_bytes=*/16, /*block_size_to_optimize_in_bytes=*/8, 4, 4, /*num_comps=*/3,
				block_pixels, rgb_params, num_modified, unpackBC1BlockForRDO, /*user data (allow 3-colour mode)=*/nullptr);
		}
		else
		{
			ert::reduce_entropy(blocks, num_blocks, /*total_block_stride_in_bytes=*/8, /*block_size_to_optimize_in_bytes=*/8, 4, 4, /*num_comps=*/3,
				block_pixels, rgb_params, num_modified, unpackBC1BlockForRDO, /*user data (allow 3-colour mode)=*/(void*)1);
		}
	}

	const BC13EncodeClosure& closure;
	size_t begin, end;
};


// Encodes an RGB or RGBA 8-bit image (N = 3 or 4) to BC1 or BC3 blocks.  Edge blocks are padded by clamping to the image edge.
static void encodeBC13Level(const uint8* src, size_t W, size_t H, size_t N, bool use_bc3, float rdo_lambda, glare::TaskManager& task_manager, std::vector<uint8>& blocks_out)
{
	const size_t blocks_x = (W + 3) / 4;
	const size_t blocks_y = (H + 3) / 4;
	const size_t num_blocks = blocks_x * blocks_y;

	std::vector<ert::color_rgba> block_pixels(num_blocks * 16);
	for(size_t by=0; by<blocks_y; ++by)
	for(size_t bx=0; bx<blocks_x; ++bx)
		for(size_t y=0; y<4; ++y)
		for(size_t x=0; x<4; ++x)
		{
			const size_t sx = myMin(bx * 4 + x, W - 1);
			const size_t sy = myMin(by * 4 + y, H - 1);
			const uint8* p = src + (sx + sy * W) * N;
			ert::color_rgba& c = block_pixels[(bx + by * blocks_x) * 16 + y * 4 + x];
			c.m_c[0] = p[0];
			c.m_c[1] = p[1];
			c.m_c[2] = p[2];
			c.m_c[3] = (N == 4) ? p[3] : 255;
		}

	blocks_out.resize(num_blocks * (use_bc3 ? 16 : 8));

	BC13EncodeClosure closure;
	closure.block_pixels = block_pixels.data();
	closure.blocks_out = blocks_out.data();
	closure.use_bc3 = use_bc3;
	closure.rdo_lambda = rdo_lambda;

	// Use fairly large chunks of blocks per task, as the RDO process can only find matches within a task's blocks.
	const size_t MIN_BLOCKS_PER_TASK = 4096;
	const size_t num_tasks = myMax<size_t>(1, myMin(task_manager.getConcurrency(), num_blocks / MIN_BLOCKS_PER_TASK));
	const size_t blocks_per_task = Maths::roundedUpDivide(num_blocks, num_tasks);

	glare::TaskGroupRef group = new glare::TaskGroup();
	for(size_t t=0; t<num_tasks; ++t)
	{
		const size_t begin = myMin(t * blocks_per_task, num_blocks);
		const size_t end   = myMin((t + 1) * blocks_per_task, num_blocks);
		if(begin < end)
			group->tasks.push_back(new BC13EncodeTask(closure, begin, end));
	}
	task_manager.runTaskGroup(group);
}

} // end anonymous namespace

#endif // !GUI_CLIENT


void generateBC13KTX2Texture(const std::string& src_tex_path, int base_lod_level, int lod_level, const std::string& ktx2_tex_path, glare::TaskManager& task_manager)
{
#if GUI_CLIENT
	throw glare::Exception("generateBC13KTX2Texture not supported.");
#else
	initRGBCX();

	// Use the same dimensions as generateBasisTexture(), so that the basis and BC1/BC3 textures for a given LOD level are interchangeable.
	int new_max_w_h;
	if(lod_level == base_lod_level)
		new_max_w_h = 4096;
	else
		new_max_w_h = (lod_level == 0) ? 1024 : ((lod_level == 1) ? 256 : 64);

	const int min_w_h = 1;

	// Load texture from disk and decode it.  Animated gifs and webps are decoded to an image sequence.  Generated for the same textures as generateBasisTexture(), so decode in the same way.
	Reference<Map2D> map;
	if(hasExtension(src_tex_path, "gif"))
		map = GIFDecoder::decodeImageSequence(src_tex_path);
	else if(hasExtension(src_tex_path, "webp"))
		map = WebPDecoder::decodeImageOrSequence(src_tex_path);
	else
		map = ImageDecoding::decodeImage(".", src_tex_path);

	// If the map is a 16-bit image, convert to 8-bit first.
	if(dynamic_cast<const ImageMap<uint16, UInt16ComponentValueTraits>*>(map.ptr()))
		map = convertUInt16ToUInt8ImageMap(static_cast<const ImageMap<uint16, UInt16ComponentValueTraits>&>(*map));

	if(!map.isType<ImageMapUInt8>() && !map.isType<ImageMapSequenceUInt8>())
		throw glare::Exception("generateBC13KTX2Texture: unhandled image type (not an 8-bit image or image sequence): " + src_tex_path);

	if((map->getMapWidth() == 0) || (map->getMapHeight() == 0) || (map->numChannels() == 0))
		throw glare::Exception("Invalid image dimensions (zero)");

	int new_w, new_h;
	if(map->getMapWidth() > map->getMapHeight())
	{
		new_w = myMin((int)map->getMapWidth(), new_max_w_h);
		new_h = myMax(min_w_h, (int)((float)new_w * (float)map->getMapHeight() / (float)map->getMapWidth()));
	}
	else
	{
		new_h = myMin((int)map->getMapHeight(), new_max_w_h);
		new_w = myMax(min_w_h, (int)((float)new_h * (float)map->getMapWidth() / (float)map->getMapHeight()));
	}

	new_w = Maths::roundUpToMultipleOfPowerOf2(new_w, 4); // As for basis textures: WebGL requires the dimensions of block-compressed textures to be a multiple of 4.
	new_h = Maths::roundUpToMultipleOfPowerOf2(new_h, 4);

	// Use BC3 if any frame has any non-opaque alpha, BC1 otherwise.
	// This is determined from the source image(s), since resizing can change alpha values slightly from 255.
	auto imageHasNonOpaqueAlpha = [](const ImageMapUInt8& im)
	{
		if((im.getN() != 2) && (im.getN() != 4))
			return false;
		const size_t alpha_i = im.getN() - 1;
		for(size_t i=0; i<im.numPixels(); ++i)
			if(im.getPixel(i)[alpha_i] != 255)
				return true;
		return false;
	};
	bool has_alpha = false;
	if(map.isType<ImageMapSequenceUInt8>())
	{
		const ImageMapSequenceUInt8* seq = map.downcastToPtr<ImageMapSequenceUInt8>();
		for(size_t f=0; (f<seq->images.size()) && !has_alpha; ++f)
			has_alpha = imageHasNonOpaqueAlpha(*seq->images[f]);
	}
	else
		has_alpha = imageHasNonOpaqueAlpha(*map.downcastToPtr<ImageMapUInt8>());

	// Get the frames: a single image, or the images of an animated image sequence.
	std::vector<ImageMapUInt8Ref> frames;
	double frame_duration_s = 0;
	if(map.isType<ImageMapSequenceUInt8>())
	{
		Reference<Map2D> resized_seq = map.downcastToPtr<ImageMapSequenceUInt8>()->resizeMidQuality(new_w, new_h, &task_manager);
		runtimeCheck(resized_seq.isType<ImageMapSequenceUInt8>());
		const ImageMapSequenceUInt8* seq = resized_seq.downcastToPtr<ImageMapSequenceUInt8>();
		runtimeCheck(!seq->images.empty() && (seq->frame_durations.size() == seq->images.size()));
		frames = seq->images;
		frame_duration_s = seq->frame_durations[0]; // NOTE: just use frame 0 duration, as for basis files (see writeBasisUniversalFileForSequence()).
	}
	else
	{
		Reference<Map2D> resized_map = map.downcastToPtr<ImageMapUInt8>()->resizeMidQuality(new_w, new_h, &task_manager);
		runtimeCheck(resized_map.isType<ImageMapUInt8>());
		frames.push_back(resized_map.downcast<ImageMapUInt8>());
	}

	for(size_t f=0; f<frames.size(); ++f)
	{
		// Convert greyscale and greyscale + alpha images to RGB and RGBA.
		if((frames[f]->getN() == 1) || (frames[f]->getN() == 2))
		{
			const size_t src_N = frames[f]->getN();
			const size_t dst_N = src_N + 2;
			ImageMapUInt8Ref converted = new ImageMapUInt8(frames[f]->getWidth(), frames[f]->getHeight(), dst_N);
			for(size_t i=0; i<frames[f]->numPixels(); ++i)
			{
				const uint8* src = frames[f]->getPixel(i);
				uint8* dst = converted->getPixel(i);
				dst[0] = dst[1] = dst[2] = src[0];
				if(dst_N == 4)
					dst[3] = src[1];
			}
			frames[f] = converted;
		}
		if(((frames[f]->getN() != 3) && (frames[f]->getN() != 4)) || (frames[f]->getN() != frames[0]->getN()))
			throw glare::Exception("generateBC13KTX2Texture: unhandled number of channels: " + toString(frames[f]->getN()));
	}

	const float rdo_lambda = (lod_level > base_lod_level) ? BC13_LOD_RDO_LAMBDA : BC13_RDO_LAMBDA;

	conPrint("\tMaking " + std::string(has_alpha ? "BC3" : "BC1") + " KTX2 file with dimensions " + toString(new_w) + " * " + toString(new_h) + ", " + toString(frames.size()) + " frame(s), RDO lambda " + doubleToStringNSigFigs(rdo_lambda, 2) +
		" for LOD level " + toString(lod_level));

	Timer timer;

	// level_data[k] holds MIP level k of all frames: frame 0, then frame 1, etc.  Compressing each level of all frames together lets zstd exploit the redundancy between frames.
	std::vector<std::vector<uint8> > level_data;
	std::vector<uint8> frame_level_blocks;
	for(size_t f=0; f<frames.size(); ++f)
	{
		// Build the uncompressed MIP chain, as the client would for an uncompressed texture.
		Reference<TextureData> texture_data = TextureProcessing::buildTextureData(frames[f].ptr(), /*general_mem_allocator=*/nullptr, &task_manager, /*allow_compression=*/false, /*build_mipmaps=*/true, /*convert_float_to_half=*/false);
		const size_t N = frames[f]->getN();
		runtimeCheck(!texture_data->isCompressed() && (texture_data->numChannels() == N));

		if(f == 0)
			level_data.resize(texture_data->numMipLevels());
		runtimeCheck(texture_data->numMipLevels() == level_data.size());

		for(size_t k=0; k<texture_data->numMipLevels(); ++k)
		{
			const size_t level_W = myMax<size_t>(1, texture_data->W >> k);
			const size_t level_H = myMax<size_t>(1, texture_data->H >> k);
			const size_t offset = texture_data->level_offsets.empty() ? 0 : texture_data->level_offsets[k].offset;
			runtimeCheck((texture_data->level_offsets.empty() || (texture_data->level_offsets[k].level_size == level_W * level_H * N)) && (offset + level_W * level_H * N <= texture_data->mipmap_data.size()));

			encodeBC13Level(&texture_data->mipmap_data[offset], level_W, level_H, N, has_alpha, rdo_lambda, task_manager, frame_level_blocks);
			level_data[k].insert(level_data[k].end(), frame_level_blocks.begin(), frame_level_blocks.end());
		}
	}

	KTXDecoder::writeKTX2File(has_alpha ? KTXDecoder::Format_BC3 : KTXDecoder::Format_BC1, /*supercompression=*/true, new_w, new_h, (int)frames.size(), frame_duration_s, level_data, ktx2_tex_path, BC13_ZSTD_LEVEL);

	conPrint("\tBC1/BC3 encoding and writing KTX2 file took " + timer.elapsedStringNSigFigs(3));
#endif
}



void writeBasisUniversalFile(const ImageMapUInt8& imagemap, const std::string& path, int quality_level)
{
#if GUI_CLIENT
	throw glare::Exception("writeBasisUniversalFile not supported.");
#else

	basisu::basisu_encoder_init(); // Can be called multiple times harmlessly.

	Timer timer;

	basisu::image img(imagemap.getData(), (uint32)imagemap.getWidth(), (uint32)imagemap.getHeight(), (uint32)imagemap.getN());

	basisu::basis_compressor_params params;

	params.m_source_images.push_back(img);
	params.m_perceptual = true;
	params.m_status_output = false;
	
	params.m_write_output_basis_or_ktx2_files = true;
	params.m_out_filename = path;
	params.m_create_ktx2_file = false;

	params.m_mip_gen = true; // Generate mipmaps for each source image
	params.m_mip_srgb = true; // Convert image to linear before filtering, then back to sRGB

	params.m_etc1s_quality_level = quality_level;

	//Timer timer2;
	//printVar(PlatformUtils::getNumLogicalProcessors());
	basisu::job_pool jpool(PlatformUtils::getNumLogicalProcessors() / 2); // TODO: don't recreate this for each image.
	params.m_pJob_pool = &jpool;
	//conPrint("Creating job pool took " + timer2.elapsedString());

	basisu::basis_compressor basisCompressor;
	basisu::enable_debug_printf(false);

	const bool res = basisCompressor.init(params);
	if(!res)
		throw glare::Exception("Failed to create basisCompressor");

	basisu::basis_compressor::error_code result = basisCompressor.process();

	if(result != basisu::basis_compressor::cECSuccess)
		throw glare::Exception("basisCompressor.process() failed.");

	conPrint("Basisu compression and writing of file took " + timer.elapsedStringNSigFigs(3));
#endif
}


void writeBasisUniversalFileForSequence(const ImageMapSequenceUInt8& imagemapseq, const std::string& path, int quality_level)
{
#if GUI_CLIENT
	throw glare::Exception("writeBasisUniversalFileForSequence not supported.");
#else
	runtimeCheck(imagemapseq.images.size() >= 1);

	basisu::basisu_encoder_init(); // Can be called multiple times harmlessly.

	Timer timer;

	basisu::basis_compressor_params params;

	params.m_tex_type = basist::cBASISTexTypeVideoFrames;
	params.m_us_per_frame = (uint32)(imagemapseq.frame_durations[0] * 1.0e6); // NOTE: just use frame 0 duration.

	params.m_source_images.resize(imagemapseq.images.size());
	for(size_t i=0; i<imagemapseq.images.size(); ++i)
		params.m_source_images[i] = basisu::image(imagemapseq.images[i]->getData(), (uint32)imagemapseq.images[i]->getWidth(), (uint32)imagemapseq.images[i]->getHeight(), (uint32)imagemapseq.images[i]->getN());

	params.m_perceptual = true;
	params.m_status_output = false;
	
	params.m_write_output_basis_or_ktx2_files = true;
	params.m_out_filename = path;
	params.m_create_ktx2_file = false;

	params.m_mip_gen = true; // Generate mipmaps for each source image
	params.m_mip_srgb = true; // Convert image to linear before filtering, then back to sRGB

	params.m_etc1s_quality_level = quality_level;

	//Timer timer2;
	basisu::job_pool jpool(PlatformUtils::getNumLogicalProcessors() / 2); // TODO: don't recreate this for each image.
	params.m_pJob_pool = &jpool;
	//conPrint("Creating job pool took " + timer2.elapsedString());

	basisu::basis_compressor basisCompressor;
	basisu::enable_debug_printf(false);

	const bool res = basisCompressor.init(params);
	if(!res)
		throw glare::Exception("Failed to create basisCompressor");

	basisu::basis_compressor::error_code result = basisCompressor.process();

	if(result != basisu::basis_compressor::cECSuccess)
		throw glare::Exception("basisCompressor.process() failed.");

	conPrint("Basisu compression and writing of file took " + timer.elapsedStringNSigFigs(3));
#endif
}


// Look up from cache or recompute.
// returns false if could not load tex.
bool texHasAlpha(const std::string& tex_path, std::map<std::string, bool>& tex_has_alpha)
{
	if(tex_has_alpha.find(tex_path) != tex_has_alpha.end())
	{
		return tex_has_alpha[tex_path];
	}
	else
	{
		bool has_alpha = false;
		try
		{
			has_alpha = textureHasAlphaChannel(tex_path);
		}
		catch(glare::Exception& e)
		{
			conPrint("Excep while calling textureHasAlphaChannel(): " + e.what());
		}
		tex_has_alpha[tex_path] = has_alpha;
		return has_alpha;
	}
}


//void generateLODTexturesForTexURL(const std::string& base_tex_URL, bool texture_has_alpha, WorldMaterial* mat, ResourceManager& resource_manager, glare::TaskManager& task_manager)
//{
//	const int start_lod_level = mat->minLODLevel() + 1;
//
//	for(int lvl = start_lod_level; lvl <= 2; ++lvl)
//	{
//		const std::string lod_URL = mat->getLODTextureURLForLevel(base_tex_URL, lvl, texture_has_alpha, /*use basis=*/false);
//
//		if((lod_URL != base_tex_URL) && !resource_manager.isFileForURLPresent(lod_URL)) // If the LOD URL is actually different, and if the LOD'd texture has not already been created:
//		{
//			const std::string local_base_path = resource_manager.pathForURL(base_tex_URL); // Path of the original source texture.
//			const std::string local_lod_path  = resource_manager.pathForURL(lod_URL); // Path where we will write the LOD texture.
//
//			conPrint("Generating LOD texture '" + local_lod_path + "'...");
//			try
//			{
//				LODGeneration::generateLODTexture(local_base_path, lvl, local_lod_path, task_manager);
//
//				resource_manager.setResourceAsLocallyPresentForURL(lod_URL); // Mark as present
//			}
//			catch(glare::Exception& e)
//			{
//				conPrint("Warning: Error while generating LOD texture: " + e.what());
//			}
//		}
//	}
//}


// Generate LOD and KTX textures for materials, if not already present on disk.
//void generateLODTexturesForMaterialsIfNotPresent(std::vector<WorldMaterialRef>& materials, ResourceManager& resource_manager, glare::TaskManager& task_manager)
//{
//	for(size_t z=0; z<materials.size(); ++z)
//	{
//		WorldMaterial* mat = materials[z].ptr();
//
//		if(!mat->colour_texture_url.empty())
//			generateLODTexturesForTexURL(mat->colour_texture_url, mat->colourTexHasAlpha(), mat, resource_manager, task_manager);
//
//		if(!mat->roughness.texture_url.empty())
//			generateLODTexturesForTexURL(mat->roughness.texture_url, /*texture_has_alpha=*/false, mat, resource_manager, task_manager);
//
//		if(!mat->emission_texture_url.empty())
//			generateLODTexturesForTexURL(mat->emission_texture_url, /*texture_has_alpha=*/false, mat, resource_manager, task_manager);
//
//		if(!mat->normal_map_url.empty())
//			generateLODTexturesForTexURL(mat->normal_map_url, /*texture_has_alpha=*/false, mat, resource_manager, task_manager);
//	}
//}


} // end namespace LODGeneration


#ifdef BUILD_TESTS


#include "../utils/TestUtils.h"


#if 0
// Fuzzing of generateOptimisedMesh(), including model loading with loadModelFromBuffer().
//
// Fuzz input format:
//   byte 0: bits 0-2: format index (mod 5) into fuzz_formats
//           bits 3-4: lod_level (mod 3)
//           bit 5:    min_lod_level: 0 if set, else -1
//   rest of input: the model file data.
//
// The generated mesh is read back and checked with checkValidAndSanitiseMesh(), since clients download it.
//
// Seeds can be written with writeFuzzSeeds() below.
//
// Command line:
// C:\fuzz_corpus\lod_gen C:\code\substrata\testfiles\fuzz_seeds\lod_gen -max_len=1000000


#include <utils/BufferOutStream.h>
#include <utils/MemMappedFile.h>


static const char* fuzz_formats[] = { "bmesh", "obj", "stl", "gltf", "igmesh" };


static uint8 makeFuzzHeaderByte(int format_i, int lod_level, int min_lod_level)
{
	return (uint8)(format_i | (lod_level << 3) | ((min_lod_level == 0) ? (1 << 5) : 0));
}


// Writes some test repo models, one per format, after header bytes for a few LOD level combinations.
static void writeFuzzSeeds(const std::string& dir)
{
	FileUtils::createDirIfDoesNotExist(dir);

	const std::string testfiles_dir = TestUtils::getTestReposDir() + "/testfiles";
	const std::vector<std::pair<std::string, int>> models = {
		{ "bmesh/Cube_obj_11907297875084081315.bmesh",	0 },
		{ "bmesh/float_uv_0_meshopt.bmesh",				0 },
		{ "a_test_mesh.obj",							1 },
		{ "stl/cube.stl",								2 },
		{ "stl/cube_binary.stl",						2 },
		{ "gltf/duck_with_embedded_texture.gltf",		3 },
		{ "igmesh/cuboid.igmesh",						4 },
	};

	const int lod_combos[][2] = { { 0, -1 }, { 1, -1 }, { 2, 0 }, { 0, 0 } }; // (lod_level, min_lod_level)

	for(size_t m=0; m<models.size(); ++m)
	{
		MemMappedFile file(testfiles_dir + "/" + models[m].first);

		for(size_t c=0; c<staticArrayNumElems(lod_combos); ++c)
		{
			std::vector<uint8> seed(1 + file.fileSize());
			seed[0] = makeFuzzHeaderByte(models[m].second, lod_combos[c][0], lod_combos[c][1]);
			if(file.fileSize() > 0)
				std::memcpy(&seed[1], file.fileData(), file.fileSize());

			FileUtils::writeEntireFile(dir + "/" + FileUtils::getFilename(models[m].first) + "_" + toString(c), (const char*)seed.data(), seed.size());
		}
	}
}


extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
	Clock::init();

	if(false)
		writeFuzzSeeds("C:\\code\\substrata\\testfiles\\fuzz_seeds/lod_gen");
	return 0;
}


extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	if(size < 1)
		return 0;

	const int format_i = (data[0] & 7) % staticArrayNumElems(fuzz_formats);
	const int lod_level = ((data[0] >> 3) & 3) % 3;
	const int min_lod_level = ((data[0] >> 5) & 1) ? 0 : -1;

	// The path is only used for its extension, and for the glTF base dir, which is a non-existent dir so external glTF buffers fail to load.
	const std::string model_path = "fuzz_nonexistent_dir/fuzz_model." + std::string(fuzz_formats[format_i]);

	BufferOutStream out_stream;
	try
	{
		LODGeneration::generateOptimisedMesh(model_path, data + 1, size - 1, min_lod_level, lod_level, /*optimised mesh path=*/"", /*test out stream=*/&out_stream);
	}
	catch(glare::Exception&)
	{
		return 0; // Invalid model
	}

	// The generated mesh should load and be valid.
	try
	{
		BatchedMeshRef generated_mesh = BatchedMesh::readFromData(out_stream.buf.data(), out_stream.buf.size(), /*mem allocator=*/NULL);
		generated_mesh->checkValidAndSanitiseMesh();
	}
	catch(glare::Exception& e)
	{
		failTest("Generated mesh failed to load: " + e.what());
	}

	return 0;
}


#endif // Fuzzing


#if !GUI_CLIENT

#include <graphics/CompressedImage.h>


// Makes an image with smooth gradients in each channel, which BC1 and BC3 can represent well.
// Alpha (for N = 2 or 4) is a vertical gradient if varying_alpha is true, otherwise 255.
static ImageMapUInt8Ref makeBC13TestImage(size_t W, size_t H, size_t N, bool varying_alpha)
{
	ImageMapUInt8Ref im = new ImageMapUInt8(W, H, N);
	for(size_t y=0; y<H; ++y)
	for(size_t x=0; x<W; ++x)
	{
		const uint8 c[3] = { (uint8)(x * 255 / myMax<size_t>(1, W - 1)), (uint8)(y * 255 / myMax<size_t>(1, H - 1)), (uint8)((x + y) * 255 / myMax<size_t>(1, W + H - 2)) };
		const uint8 alpha = varying_alpha ? (uint8)(y * 255 / myMax<size_t>(1, H - 1)) : 255;
		uint8* p = im->getPixel(x, y);
		if(N == 1)      { p[0] = c[0]; }
		else if(N == 2) { p[0] = c[0]; p[1] = alpha; }
		else            { p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; if(N == 4) p[3] = alpha; }
	}
	return im;
}


// Checks invariants of BC1 or BC3 blocks produced by encodeBC13Level(), which the RDO post-process must preserve:
// * BC1 blocks in 3-colour mode (color0 <= color1) don't use selector 3, which decodes as black (or transparent black for RGBA BC1).  The encoder is called with
//   use_transparent_texels_for_black = false, so doesn't use it.
// * The colour blocks of BC3 blocks decode the same in 3-colour and 4-colour mode: GPUs decode BC3 colour blocks in 4-colour mode, but some old GPUs (and
//   rgbcx::unpack_bc3) use 3-colour mode if color0 <= color1.  So a BC3 colour block in 3-colour mode must have color0 == color1, and not use selector 3.
static void checkBC13BlockInvariants(const uint8* blocks, size_t num_blocks, bool use_bc3)
{
	const size_t bytes_per_block = use_bc3 ? 16 : 8;
	for(size_t i=0; i<num_blocks; ++i)
	{
		const rgbcx::bc1_block* colour_block = (const rgbcx::bc1_block*)(blocks + i * bytes_per_block + (use_bc3 ? 8 : 0)); // In BC3, the BC4 alpha block comes first.
		if(colour_block->is_3color())
		{
			if(use_bc3)
				testAssert(colour_block->get_low_color() == colour_block->get_high_color());

			for(uint32_t y=0; y<4; ++y)
			for(uint32_t x=0; x<4; ++x)
				testAssert(colour_block->get_selector(x, y) != 3);
		}
	}
}


// Decodes BC1 or BC3 blocks for a W x H image to an RGBA image.
static ImageMapUInt8Ref decodeBC13Blocks(const uint8* blocks, size_t W, size_t H, bool use_bc3)
{
	const size_t bytes_per_block = use_bc3 ? 16 : 8;
	const size_t blocks_x = (W + 3) / 4;
	const size_t blocks_y = (H + 3) / 4;

	ImageMapUInt8Ref im = new ImageMapUInt8(W, H, 4);
	for(size_t by=0; by<blocks_y; ++by)
	for(size_t bx=0; bx<blocks_x; ++bx)
	{
		uint8 pixels[16 * 4];
		const uint8* block = blocks + (bx + by * blocks_x) * bytes_per_block;
		if(use_bc3)
			rgbcx::unpack_bc3(block, pixels);
		else
			rgbcx::unpack_bc1(block, pixels, /*set_alpha=*/true);

		for(size_t y=0; y<4; ++y)
		for(size_t x=0; x<4; ++x)
			if((bx * 4 + x < W) && (by * 4 + y < H))
				std::memcpy(im->getPixel(bx * 4 + x, by * 4 + y), &pixels[(x + y * 4) * 4], 4);
	}
	return im;
}


// Decodes MIP level 0 of a frame of a BC1 or BC3 texture to an RGBA image.  Also checks the block invariants.
static ImageMapUInt8Ref decodeBC13Frame(const TextureData& texture_data, size_t frame_i)
{
	const bool bc3 = texture_data.format == OpenGLTextureFormat::Format_Compressed_DXT_SRGBA_Uint8;
	testAssert(bc3 || (texture_data.format == OpenGLTextureFormat::Format_Compressed_DXT_SRGB_Uint8));
	const size_t bytes_per_block = bc3 ? 16 : 8;
	const size_t num_blocks = ((texture_data.W + 3) / 4) * ((texture_data.H + 3) / 4);
	testAssert(texture_data.level_offsets[0].level_size == num_blocks * bytes_per_block);

	const uint8* level_0 = texture_data.mipmap_data.data() + frame_i * texture_data.frame_size_B + texture_data.level_offsets[0].offset;
	checkBC13BlockInvariants(level_0, num_blocks, bc3);
	return decodeBC13Blocks(level_0, texture_data.W, texture_data.H, bc3);
}


// RMS error, in 0-255 levels, of the decoded RGBA image vs the reference image, over the colour channels (if alpha is false) or the alpha channel (if alpha is true).
// Greyscale reference images are compared against each of R, G and B.  A reference without alpha has alpha 255.
static double computeBC13RMSError(const ImageMapUInt8& decoded, const ImageMapUInt8& ref, bool alpha)
{
	testAssert(decoded.getWidth() == ref.getWidth() && decoded.getHeight() == ref.getHeight());
	const size_t ref_N = ref.getN();
	double sum_sqr_err = 0;
	size_t num_values = 0;
	for(size_t i=0; i<ref.numPixels(); ++i)
	{
		const uint8* d = decoded.getPixel(i);
		const uint8* r = ref.getPixel(i);
		if(alpha)
		{
			const int ref_alpha = (ref_N == 2 || ref_N == 4) ? r[ref_N - 1] : 255;
			sum_sqr_err += Maths::square((double)d[3] - ref_alpha);
			num_values++;
		}
		else
		{
			for(size_t c=0; c<3; ++c)
			{
				const int ref_val = (ref_N <= 2) ? r[0] : r[c];
				sum_sqr_err += Maths::square((double)d[c] - ref_val);
				num_values++;
			}
		}
	}
	return std::sqrt(sum_sqr_err / num_values);
}


static void checkBC13RMSError(const ImageMapUInt8& decoded, const ImageMapUInt8& ref, bool alpha, double max_rms_error)
{
	const double rms_error = computeBC13RMSError(decoded, ref, alpha);
	conPrint(std::string("	") + (alpha ? "alpha" : "colour") + " RMS error: " + doubleToStringNSigFigs(rms_error, 3) + " levels");
	testAssert(rms_error < max_rms_error);
}


// Runs generateBC13KTX2Texture, then decodes the result and checks its properties.  Returns the decoded texture data.
static Reference<TextureData> generateAndCheckBC13Texture(const std::string& src_path, int base_lod_level, int lod_level, size_t expected_W, size_t expected_H, bool expect_bc3,
	size_t expected_num_frames, glare::TaskManager& task_manager)
{
	const std::string ktx2_path = PlatformUtils::getTempDirPath() + "/bc13_test_output.ktx2";
	LODGeneration::generateBC13KTX2Texture(src_path, base_lod_level, lod_level, ktx2_path, task_manager);

	Reference<Map2D> im = KTXDecoder::decodeKTX2(ktx2_path);
	testAssert(im.isType<CompressedImage>());
	Reference<TextureData> texture_data = im.downcastToPtr<CompressedImage>()->texture_data;
	testAssert(texture_data->W == expected_W && texture_data->H == expected_H);
	testAssert(texture_data->format == (expect_bc3 ? OpenGLTextureFormat::Format_Compressed_DXT_SRGBA_Uint8 : OpenGLTextureFormat::Format_Compressed_DXT_SRGB_Uint8));
	testAssert(texture_data->numFrames() == expected_num_frames);
	testAssert(texture_data->numMipLevels() == TextureData::computeNumMipLevels(expected_W, expected_H)); // Full MIP chain
	testAssert(expected_W % 4 == 0 && expected_H % 4 == 0); // Dimensions are rounded up to a multiple of 4, for WebGL.
	return texture_data;
}


static void testGenerateBC13KTX2Texture(glare::TaskManager& task_manager)
{
	conPrint("testGenerateBC13KTX2Texture()");

	const std::string temp_dir = PlatformUtils::getTempDirPath();
	const double max_rms_error = 4.5; // In 0-255 levels.  The test images are smooth gradients, so should be represented well.

	//---------------- RGB PNG, base level, no resize: BC1 ----------------
	{
		ImageMapUInt8Ref src = makeBC13TestImage(64, 48, 3, /*varying_alpha=*/false);
		PNGDecoder::write(*src, temp_dir + "/bc13_test_rgb.png");
		Reference<TextureData> texture_data = generateAndCheckBC13Texture(temp_dir + "/bc13_test_rgb.png", /*base_lod_level=*/0, /*lod_level=*/0, 64, 48, /*expect_bc3=*/false, /*expected_num_frames=*/1, task_manager);
		checkBC13RMSError(*decodeBC13Frame(*texture_data, 0), *src, /*alpha=*/false, /*max_rms_error=*/max_rms_error);
	}

	//---------------- RGBA PNG with varying alpha: BC3 ----------------
	{
		ImageMapUInt8Ref src = makeBC13TestImage(64, 64, 4, /*varying_alpha=*/true);
		PNGDecoder::write(*src, temp_dir + "/bc13_test_rgba.png");
		Reference<TextureData> texture_data = generateAndCheckBC13Texture(temp_dir + "/bc13_test_rgba.png", /*base_lod_level=*/0, /*lod_level=*/0, 64, 64, /*expect_bc3=*/true, /*expected_num_frames=*/1, task_manager);
		ImageMapUInt8Ref decoded = decodeBC13Frame(*texture_data, 0);
		checkBC13RMSError(*decoded, *src, /*alpha=*/false, /*max_rms_error=*/max_rms_error);
		checkBC13RMSError(*decoded, *src, /*alpha=*/true, /*max_rms_error=*/max_rms_error);
	}

	//---------------- Opaque RGBA PNG, resized for LOD level 1: should be BC1, as alpha is checked before resizing ----------------
	{
		ImageMapUInt8Ref src = makeBC13TestImage(1000, 600, 4, /*varying_alpha=*/false);
		PNGDecoder::write(*src, temp_dir + "/bc13_test_opaque_rgba.png");
		// Max dimension 256 for LOD level 1: 1000 x 600 -> 256 x 153, rounded up to 256 x 156.
		generateAndCheckBC13Texture(temp_dir + "/bc13_test_opaque_rgba.png", /*base_lod_level=*/0, /*lod_level=*/1, 256, 156, /*expect_bc3=*/false, /*expected_num_frames=*/1, task_manager);
		// Max dimension 64 for LOD level 2.
		generateAndCheckBC13Texture(temp_dir + "/bc13_test_opaque_rgba.png", /*base_lod_level=*/0, /*lod_level=*/2, 64, 40, /*expect_bc3=*/false, /*expected_num_frames=*/1, task_manager);
		// Max dimension 1024 for LOD level 0 if it isn't the base level: not resized here.
		generateAndCheckBC13Texture(temp_dir + "/bc13_test_opaque_rgba.png", /*base_lod_level=*/-1, /*lod_level=*/0, 1000, 600, /*expect_bc3=*/false, /*expected_num_frames=*/1, task_manager);
	}

	//---------------- Dimensions not a multiple of 4: rounded up ----------------
	{
		ImageMapUInt8Ref src = makeBC13TestImage(30, 18, 3, /*varying_alpha=*/false);
		PNGDecoder::write(*src, temp_dir + "/bc13_test_non_mult_4.png");
		generateAndCheckBC13Texture(temp_dir + "/bc13_test_non_mult_4.png", /*base_lod_level=*/0, /*lod_level=*/0, 32, 20, /*expect_bc3=*/false, /*expected_num_frames=*/1, task_manager);
	}

	//---------------- Greyscale PNG: BC1 ----------------
	{
		ImageMapUInt8Ref src = makeBC13TestImage(32, 32, 1, /*varying_alpha=*/false);
		PNGDecoder::write(*src, temp_dir + "/bc13_test_grey.png");
		Reference<TextureData> texture_data = generateAndCheckBC13Texture(temp_dir + "/bc13_test_grey.png", /*base_lod_level=*/0, /*lod_level=*/0, 32, 32, /*expect_bc3=*/false, /*expected_num_frames=*/1, task_manager);
		checkBC13RMSError(*decodeBC13Frame(*texture_data, 0), *src, /*alpha=*/false, /*max_rms_error=*/max_rms_error);
	}

	//---------------- Greyscale + alpha PNG: BC3 ----------------
	{
		ImageMapUInt8Ref src = makeBC13TestImage(32, 32, 2, /*varying_alpha=*/true);
		PNGDecoder::write(*src, temp_dir + "/bc13_test_grey_alpha.png");
		Reference<TextureData> texture_data = generateAndCheckBC13Texture(temp_dir + "/bc13_test_grey_alpha.png", /*base_lod_level=*/0, /*lod_level=*/0, 32, 32, /*expect_bc3=*/true, /*expected_num_frames=*/1, task_manager);
		ImageMapUInt8Ref decoded = decodeBC13Frame(*texture_data, 0);
		checkBC13RMSError(*decoded, *src, /*alpha=*/false, /*max_rms_error=*/max_rms_error);
		checkBC13RMSError(*decoded, *src, /*alpha=*/true, /*max_rms_error=*/max_rms_error);
	}

	//---------------- 16-bit RGB PNG: converted to 8-bit, BC1 ----------------
	{
		ImageMapUInt8Ref src = makeBC13TestImage(64, 48, 3, /*varying_alpha=*/false);
		ImageMap<uint16, UInt16ComponentValueTraits> src_16(64, 48, 3);
		for(size_t i=0; i<src->getDataSize(); ++i)
			src_16.getData()[i] = (uint16)(src->getData()[i] * 257);
		PNGDecoder::write(src_16, temp_dir + "/bc13_test_16_bit.png");
		Reference<TextureData> texture_data = generateAndCheckBC13Texture(temp_dir + "/bc13_test_16_bit.png", /*base_lod_level=*/0, /*lod_level=*/0, 64, 48, /*expect_bc3=*/false, /*expected_num_frames=*/1, task_manager);
		checkBC13RMSError(*decodeBC13Frame(*texture_data, 0), *src, /*alpha=*/false, /*max_rms_error=*/max_rms_error);
	}

	//---------------- JPEG: BC1 ----------------
	{
		ImageMapUInt8Ref src = makeBC13TestImage(128, 64, 3, /*varying_alpha=*/false);
		JPEGDecoder::SaveOptions options;
		JPEGDecoder::save(src, temp_dir + "/bc13_test.jpg", options);
		Reference<TextureData> texture_data = generateAndCheckBC13Texture(temp_dir + "/bc13_test.jpg", /*base_lod_level=*/0, /*lod_level=*/0, 128, 64, /*expect_bc3=*/false, /*expected_num_frames=*/1, task_manager);
		checkBC13RMSError(*decodeBC13Frame(*texture_data, 0), *src, /*alpha=*/false, /*max_rms_error=*/8.0); // Higher threshold, as JPEG is lossy too.
	}

	//---------------- Animated gif: one frame per gif frame, with the gif frame 0 duration ----------------
	{
		const std::string gif_path = TestUtils::getTestReposDir() + "/testfiles/gifs/fire.gif";
		Reference<Map2D> gif = GIFDecoder::decodeImageSequence(gif_path);
		testAssert(gif.isType<ImageMapSequenceUInt8>());
		const ImageMapSequenceUInt8* seq = gif.downcastToPtr<ImageMapSequenceUInt8>();
		testAssert(seq->images.size() > 1);
		const size_t expected_W = Maths::roundUpToMultipleOfPowerOf2<size_t>(gif->getMapWidth(), 4);
		const size_t expected_H = Maths::roundUpToMultipleOfPowerOf2<size_t>(gif->getMapHeight(), 4);

		Reference<TextureData> texture_data = generateAndCheckBC13Texture(gif_path, /*base_lod_level=*/0, /*lod_level=*/0, expected_W, expected_H, /*expect_bc3=*/false, /*expected_num_frames=*/seq->images.size(), task_manager);
		testAssert(texture_data->isMultiFrame());
		testAssert(texture_data->frame_durations_equal);
		testEpsEqual(texture_data->recip_frame_duration, 1.0 / seq->frame_durations[0]);

		// Check each frame is close to its gif frame, if the gif wasn't resized.
		if(expected_W == gif->getMapWidth() && expected_H == gif->getMapHeight())
			for(size_t f=0; f<seq->images.size(); ++f)
				checkBC13RMSError(*decodeBC13Frame(*texture_data, f), *seq->images[f], /*alpha=*/false, /*max_rms_error=*/14.0); // Gif frames have hard edges, so a higher threshold.

		// LOD level 2 of the gif: max dimension 64.  Images aren't scaled up, so a small gif stays the same size.
		const size_t W = gif->getMapWidth();
		const size_t H = gif->getMapHeight();
		size_t lod2_W, lod2_H;
		if(W > H)
		{
			lod2_W = myMin<size_t>(W, 64);
			lod2_H = myMax<size_t>(1, (size_t)((float)lod2_W * H / W));
		}
		else
		{
			lod2_H = myMin<size_t>(H, 64);
			lod2_W = myMax<size_t>(1, (size_t)((float)lod2_H * W / H));
		}
		generateAndCheckBC13Texture(gif_path, /*base_lod_level=*/0, /*lod_level=*/2, Maths::roundUpToMultipleOfPowerOf2<size_t>(lod2_W, 4), Maths::roundUpToMultipleOfPowerOf2<size_t>(lod2_H, 4),
			/*expect_bc3=*/false, /*expected_num_frames=*/seq->images.size(), task_manager);
	}

	//---------------- Invalid source file: throws ----------------
	{
		FileUtils::writeEntireFileTextMode(temp_dir + "/bc13_test_invalid.png", "not a png");
		try
		{
			LODGeneration::generateBC13KTX2Texture(temp_dir + "/bc13_test_invalid.png", 0, 0, temp_dir + "/bc13_test_output.ktx2", task_manager);
			failTest("Expected exception");
		}
		catch(glare::Exception&)
		{}
	}

	conPrint("testGenerateBC13KTX2Texture() done.");
}


// Tests encodeBC13Level() on images smaller than a block, and with dimensions that aren't multiples of 4, where edge blocks are padded by clamping to the image edge.
// The output should be identical to the output for the image explicitly padded to a multiple of 4 in this way.
// (Quality isn't checked, as these small gradient images have more colours in a block than BC1 can represent well.)
static void testEncodeBC13LevelSmallImages(glare::TaskManager& task_manager)
{
	conPrint("testEncodeBC13LevelSmallImages()");

	LODGeneration::initRGBCX();

	const size_t dims[][2] = { { 1, 1 }, { 2, 3 }, { 3, 5 }, { 5, 3 }, { 4, 4 }, { 7, 1 }, { 1, 9 } };
	for(size_t d=0; d<staticArrayNumElems(dims); ++d)
	for(int use_bc3=0; use_bc3<2; ++use_bc3)
	for(int lambda_i=0; lambda_i<2; ++lambda_i)
	{
		const size_t W = dims[d][0];
		const size_t H = dims[d][1];
		const size_t N = use_bc3 ? 4 : 3;
		const float rdo_lambda = (lambda_i == 0) ? 0.f : 8.f;
		ImageMapUInt8Ref src = makeBC13TestImage(W, H, N, /*varying_alpha=*/use_bc3 != 0);

		std::vector<uint8> blocks;
		LODGeneration::encodeBC13Level(src->getData(), W, H, N, use_bc3 != 0, rdo_lambda, task_manager, blocks);

		const size_t num_blocks = ((W + 3) / 4) * ((H + 3) / 4);
		testAssert(blocks.size() == num_blocks * (use_bc3 ? 16 : 8));
		checkBC13BlockInvariants(blocks.data(), num_blocks, use_bc3 != 0);

		// Make the image padded to a multiple of 4 by clamping to the image edge, and check its encoding is the same.
		const size_t padded_W = Maths::roundUpToMultipleOfPowerOf2<size_t>(W, 4);
		const size_t padded_H = Maths::roundUpToMultipleOfPowerOf2<size_t>(H, 4);
		ImageMapUInt8 padded(padded_W, padded_H, N);
		for(size_t y=0; y<padded_H; ++y)
		for(size_t x=0; x<padded_W; ++x)
			std::memcpy(padded.getPixel(x, y), src->getPixel(myMin(x, W - 1), myMin(y, H - 1)), N);

		std::vector<uint8> padded_blocks;
		LODGeneration::encodeBC13Level(padded.getData(), padded_W, padded_H, N, use_bc3 != 0, rdo_lambda, task_manager, padded_blocks);
		testAssert(padded_blocks == blocks);
	}

	conPrint("testEncodeBC13LevelSmallImages() done.");
}


#if 0
// Fuzzing of encodeBC13Level(), in particular the RDO post-process (ert::reduce_entropy), with arbitrary pixel data.
//
// Fuzz input format:
//   byte 0: image width - 1
//   byte 1: image height - 1
//   byte 2: bit 0: use BC3 (else BC1)
//           bit 1: 4 channels (RGBA), else 3 (RGB)
//           bits 2-3: RDO lambda index into fuzz_rdo_lambdas
//   rest of input: pixel data, repeated as needed to fill the image.  If empty, the image is black.
//
// The output is checked for the right size, and with checkBC13BlockInvariants().
// If the image is opaque, BC3 output is checked to decode as fully opaque, since the RDO process could change alpha blocks.
//
// Seeds can be written with writeFuzzSeeds() below.
//
// Command line:
// C:\fuzz_corpus\bc13_encode C:\code\substrata\testfiles\fuzz_seeds\bc13_encode


static const float fuzz_rdo_lambdas[] = { 0.f, 1.f, 2.f, 8.f };

static glare::TaskManager* fuzz_task_manager = nullptr;


static void writeFuzzSeeds(const std::string& dir)
{
	FileUtils::createDirIfDoesNotExist(dir);

	const size_t dims[][2] = { { 1, 1 }, { 5, 3 }, { 16, 16 }, { 64, 48 } };
	for(size_t d=0; d<staticArrayNumElems(dims); ++d)
	for(int flags=0; flags<16; flags += 3) // A selection of BC1/BC3, RGB/RGBA and lambda combinations.
	{
		const size_t N = (flags & 2) ? 4 : 3;
		ImageMapUInt8Ref src = makeBC13TestImage(dims[d][0], dims[d][1], N, /*varying_alpha=*/true);

		std::vector<uint8> seed(3 + src->getDataSize());
		seed[0] = (uint8)(dims[d][0] - 1);
		seed[1] = (uint8)(dims[d][1] - 1);
		seed[2] = (uint8)flags;
		std::memcpy(&seed[3], src->getData(), src->getDataSize());

		FileUtils::writeEntireFile(dir + "/seed_" + toString(dims[d][0]) + "x" + toString(dims[d][1]) + "_" + toString(flags), (const char*)seed.data(), seed.size());
	}
}


extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
	Clock::init();

	fuzz_task_manager = new glare::TaskManager(/*num threads=*/1);
	LODGeneration::initRGBCX();

	if(false)
		writeFuzzSeeds("C:\\code\\substrata\\testfiles\\fuzz_seeds/bc13_encode");
	return 0;
}


extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	if(size < 3)
		return 0;

	const size_t W = (size_t)data[0] + 1;
	const size_t H = (size_t)data[1] + 1;
	const bool use_bc3 = (data[2] & 1) != 0;
	const size_t N = (data[2] & 2) ? 4 : 3;
	const float rdo_lambda = fuzz_rdo_lambdas[(data[2] >> 2) & 3];

	// Fill the image with the pixel data, repeated as needed.
	const uint8* pixel_data = data + 3;
	const size_t pixel_data_size = size - 3;
	std::vector<uint8> image(W * H * N);
	bool opaque = true;
	for(size_t i=0; i<image.size(); ++i)
	{
		image[i] = (pixel_data_size > 0) ? pixel_data[i % pixel_data_size] : 0;
		if((N == 4) && (i % 4 == 3) && (image[i] != 255))
			opaque = false;
	}

	std::vector<uint8> blocks;
	LODGeneration::encodeBC13Level(image.data(), W, H, N, use_bc3, rdo_lambda, *fuzz_task_manager, blocks);

	const size_t num_blocks = ((W + 3) / 4) * ((H + 3) / 4);
	testAssert(blocks.size() == num_blocks * (use_bc3 ? 16 : 8));
	checkBC13BlockInvariants(blocks.data(), num_blocks, use_bc3);

	if(use_bc3 && opaque)
	{
		ImageMapUInt8Ref decoded = decodeBC13Blocks(blocks.data(), W, H, /*use_bc3=*/true);
		for(size_t i=0; i<decoded->numPixels(); ++i)
			testAssert(decoded->getPixel(i)[3] == 255);
	}

	return 0;
}

#endif // Fuzzing

#endif // !GUI_CLIENT


void LODGeneration::test()
{
	conPrint("LODGeneration::test()");

	glare::TaskManager task_manager;

	try
	{

#if !GUI_CLIENT  // generateBasisTexture is disabled in gui_client.
		testGenerateBC13KTX2Texture(task_manager);
		testEncodeBC13LevelSmallImages(task_manager);

		// Test generateBasisTexture on an animated gif.
		{
			generateBasisTexture(TestUtils::getTestReposDir() + "/testfiles/gifs/fire.gif", // src tex path
				0, // base lod level
				0, // lod level
				"d:/files/fire_gif.basis", // basis_tex_path
				task_manager);
		}
		
		// Test generateBasisTexture on a larger animated gif.
		{
			generateBasisTexture(TestUtils::getTestReposDir() + "/testfiles/gifs/https_58_47_47media.giphy.com_47media_47X93e1eC2J2hjy_47giphy.gif", // src tex path
				0, // base lod level
				0, // lod level
				"d:/files/cow_gif.basis", // basis_tex_path
				task_manager);
		}

		/*
		MeshLODGenThread: (ktx 606 / 34604): Generating KTX texture with URL QueenPalmTree_BaseColor_png_9712663273203237448.basis
				Making basis file with dimensions 2048 * 2048 for LOD level -1
		Basisu compression and writing of KTX file took 1.48 s
		*/
		if(false)
		for(int i=0; i<10; ++i)
		{
			generateBasisTexture(/*src tex path=*/"C:\\Users\\nick\\AppData\\Roaming\\Substrata\\server_data\\server_resources\\QueenPalmTree_BaseColor_png_9712663273203237448.png",
				0, // base lod level
				0, // lod level
				"d:/files/QueenPalmTree_BaseColor_png_9712663273203237448.basis", // basis_tex_path
				task_manager);

			/*Reference<Map2D> lod_map = ImageDecoding::decodeImage(".", lod_tex_path);
			testAssert(lod_map.isType<ImageMapUInt8>());
			ImageMapUInt8Ref lod_map_uint8 = lod_map.downcast<ImageMapUInt8>();
			testAssert(lod_map_uint8->getWidth() == 32);
			testAssert(lod_map_uint8->getHeight() == 32);
			testAssert(lod_map_uint8->getN() == 3);*/
		}

		return;
#endif




		//------------------------------------------- Test LOD texture generation -------------------------------------------

		// Test writing an 8 bit RGB LOD image.
		{
			const std::string lod_tex_path = PlatformUtils::getTempDirPath() + "/basn2c08_lod.jpg";
			generateLODTexture(TestUtils::getTestReposDir() + "/testfiles/pngs/PngSuite-2013jan13/basn2c08.png", /*lod level=*/1, lod_tex_path, task_manager);

			Reference<Map2D> lod_map = ImageDecoding::decodeImage(".", lod_tex_path);
			testAssert(lod_map.isType<ImageMapUInt8>());
			ImageMapUInt8Ref lod_map_uint8 = lod_map.downcast<ImageMapUInt8>();
			testAssert(lod_map_uint8->getWidth() == 32);
			testAssert(lod_map_uint8->getHeight() == 32);
			testAssert(lod_map_uint8->getN() == 3);
		}

		// Test with a 16-bit png base texture.   basn2c16 has 3x16 bits rgb color (see http://www.schaik.com/pngsuite/pngsuite_bas_png.html)
		{
			const std::string lod_tex_path = PlatformUtils::getTempDirPath() + "/basn2c16_lod.jpg";
			generateLODTexture(TestUtils::getTestReposDir() + "/testfiles/pngs/PngSuite-2013jan13/basn2c16.png", /*lod level=*/1, lod_tex_path, task_manager);

			Reference<Map2D> lod_map = ImageDecoding::decodeImage(".", lod_tex_path);
			testAssert(lod_map.isType<ImageMapUInt8>());
			ImageMapUInt8Ref lod_map_uint8 = lod_map.downcast<ImageMapUInt8>();
			testAssert(lod_map_uint8->getWidth() == 32);
			testAssert(lod_map_uint8->getHeight() == 32);
			testAssert(lod_map_uint8->getN() == 3);
		}

		// Test converting an 8 bit RGBA image to both jpg and png.   basn6a08 is 3x8 bits rgb color + 8 bit alpha-channel

		// Write to JPG
		{
			const std::string lod_tex_path = PlatformUtils::getTempDirPath() + "/basn6a08_lod.jpg";
			generateLODTexture(TestUtils::getTestReposDir() + "/testfiles/pngs/PngSuite-2013jan13/basn6a08.png", /*lod level=*/1, lod_tex_path, task_manager);

			Reference<Map2D> lod_map = ImageDecoding::decodeImage(".", lod_tex_path);
			testAssert(lod_map.isType<ImageMapUInt8>());
			ImageMapUInt8Ref lod_map_uint8 = lod_map.downcast<ImageMapUInt8>();
			testAssert(lod_map_uint8->getWidth() == 32);
			testAssert(lod_map_uint8->getHeight() == 32);
			testAssert(lod_map_uint8->getN() == 3);
		}
		// Write to PNG
		{
			const std::string lod_tex_path = PlatformUtils::getTempDirPath() + "/basn6a08_lod.png";
			generateLODTexture(TestUtils::getTestReposDir() + "/testfiles/pngs/PngSuite-2013jan13/basn6a08.png", /*lod level=*/1, lod_tex_path, task_manager);

			Reference<Map2D> lod_map = ImageDecoding::decodeImage(".", lod_tex_path);
			testAssert(lod_map.isType<ImageMapUInt8>());
			ImageMapUInt8Ref lod_map_uint8 = lod_map.downcast<ImageMapUInt8>();
			testAssert(lod_map_uint8->getWidth() == 32);
			testAssert(lod_map_uint8->getHeight() == 32);
			testAssert(lod_map_uint8->getN() == 4); // Should have alpha
		}


#if 0 // !GUI_CLIENT  // generateKTXTexture is disabled in gui_client.
		//------------------------------------------- Test KTX texture generation -------------------------------------------
		// Test writing an 8 bit RGB KTX image.
		{
			const std::string lod_tex_path = PlatformUtils::getTempDirPath() + "/basn2c08_lod.ktx2";
			generateKTXTexture(TestUtils::getTestReposDir() + "/testfiles/pngs/PngSuite-2013jan13/basn2c08.png", /*base lod level=*/0, /*lod level=*/1, lod_tex_path, task_manager);

			testAssert(FileUtils::fileExists(lod_tex_path));
			//Reference<Map2D> lod_map = ImageDecoding::decodeImage(".", lod_tex_path);
			//testAssert(lod_map.isType<CompressedImage>());
			//testAssert(lod_map->getMapWidth() == 32);
			//testAssert(lod_map->getMapHeight() == 32);
			//testAssert(lod_map->numChannels() == 3);
		}

		// Test with a 16-bit png base texture.   basn2c16 has 3x16 bits rgb color (see http://www.schaik.com/pngsuite/pngsuite_bas_png.html)
		{
			const std::string lod_tex_path = PlatformUtils::getTempDirPath() + "/basn2c16_lod.ktx2";
			generateKTXTexture(TestUtils::getTestReposDir() + "/testfiles/pngs/PngSuite-2013jan13/basn2c16.png", /*base lod level=*/0, /*lod level=*/1, lod_tex_path, task_manager);

			testAssert(FileUtils::fileExists(lod_tex_path));
		}

		// Test converting an 8 bit RGBA image to KTX.   basn6a08 is 3x8 bits rgb color + 8 bit alpha-channel
		{
			const std::string lod_tex_path = PlatformUtils::getTempDirPath() + "/basn6a08_lod.ktx2";
			generateKTXTexture(TestUtils::getTestReposDir() + "/testfiles/pngs/PngSuite-2013jan13/basn6a08.png", /*base lod level=*/0, /*lod level=*/1, lod_tex_path, task_manager);

			testAssert(FileUtils::fileExists(lod_tex_path));
		}

		// Test with a very small (1x1) texture.
		{
			const std::string lod_tex_path = PlatformUtils::getTempDirPath() + "/1x1.ktx2";
			generateKTXTexture(TestUtils::getTestReposDir() + "/testfiles/pngs/1x1.png", /*base lod level=*/0, /*lod level=*/1, lod_tex_path, task_manager);

			testAssert(FileUtils::fileExists(lod_tex_path));
		}
#endif

	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}




	{
//		generateKTXTexture(TestUtils::getTestReposDir() + "/testfiles/italy_bolsena_flag_flowers_stairs_01.jpg",
//			/*base lod level=*/0, /*lod level=*/0, "D:/files/basisu/italy_bolsena_flag_flowers_stairs_01.ktx2", allocator, task_manager);
//
//		generateKTXTexture("N:\\substrata\\trunk\\resources\\obstacle.png",
//			/*base lod level=*/0, /*lod level=*/0, "N:\\substrata\\trunk\\resources\\obstacle.ktx2", allocator, task_manager);

	//	generateKTXTexture("d:/art/Tokyo-M3RA0J.jpg", /*base lod level=*/0, /*lod level=*/0, "d:/files/basisu/Tokyo-M3RA0J.ktx2", allocator, task_manager);

		//generateLODTexture("C:\\Users\\nick\\Downloads\\front_lit.png", 1, "C:\\Users\\nick\\Downloads\\front_lit_lod1.png", task_manager);
	}
	//{
	//	BatchedMeshRef original_mesh = loadModel(TestUtils::getTestReposDir() + "/testfiles/bmesh/voxcarROTATE_glb_9223594900774194301.bmesh");
	//	printVar(original_mesh->numVerts());
	//	printVar(original_mesh->numIndices());
	//
	//	const std::string lod_model_path = "D:\\tempfiles\\car_lod1.bmesh"; // PlatformUtils::getTempDirPath() + "/lod.bmesh";
	//	generateLODModel(original_mesh, /*lod level=*/1, lod_model_path);
	//
	//
	//	BatchedMeshRef lod_mesh = loadModel(lod_model_path);
	//	printVar(lod_mesh->numVerts());
	//	printVar(lod_mesh->numIndices());
	//}

	conPrint("LODGeneration::test() done");
}


#endif // BUILD_TESTS
