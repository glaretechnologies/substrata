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
#include <Timer.h>
#include <TaskManager.h>
#include <GeneralMemAllocator.h>
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
#include <dll/include/IndigoMesh.h>
#include <dll/include/IndigoException.h>
#include <dll/IndigoStringUtils.h>
#include <dll/IndigoStringUtils.h>
#if !GUI_CLIENT
#include <encoder/basisu_comp.h>
#endif

namespace LODGeneration
{


BatchedMeshRef loadModel(const std::string& model_path)
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
		GLTFLoadedData data;
		batched_mesh = FormatDecoderGLTF::loadGLTFFile(model_path, data);
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

	batched_mesh->checkValidAndSanitiseMesh();

	return batched_mesh;
}


void generateLODModel(BatchedMeshRef batched_mesh, int lod_level, const std::string& LOD_model_path)
{
	BatchedMeshRef simplified_mesh;
	if(lod_level == 1)
	{
		simplified_mesh = MeshSimplification::buildSimplifiedMesh(*batched_mesh, /*target_reduction_ratio=*/10.f, /*target_error=*/0.02f, /*sloppy=*/false);
		
		// If we acheived less than a 4x reduction in the number of vertices (and this is a med/large mesh), try again with sloppy simplification
		if((batched_mesh->numVerts() > 1024) && // if this is a med/large mesh
			((float)simplified_mesh->numVerts() > (batched_mesh->numVerts() / 4.f)))
		{
			simplified_mesh = MeshSimplification::buildSimplifiedMesh(*batched_mesh, /*target_reduction_ratio=*/10.f, /*target_error=*/0.02f, /*sloppy=*/true);
		}
	}
	else
	{
		assert(lod_level == 2);
		simplified_mesh = MeshSimplification::buildSimplifiedMesh(*batched_mesh, /*target_reduction_ratio=*/100.f, /*target_error=*/0.08f, /*sloppy=*/true);
	}

	simplified_mesh->writeToFile(LOD_model_path);
}


void generateLODModel(const std::string& model_path, int lod_level, const std::string& LOD_model_path)
{
	BatchedMeshRef batched_mesh = loadModel(model_path);

	generateLODModel(batched_mesh, lod_level, LOD_model_path);
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
			else
			{
				assert(hasExtension(LOD_tex_path, "png"));

				PNGDecoder::write(*resized_map.downcastToPtr<ImageMapUInt8>(), LOD_tex_path);
			}
		}
		else
			throw glare::Exception("Unhandled image type (not ImageMapUInt8): " + base_tex_path);
	}
}


void generateKTXTexture(const std::string& src_tex_path, int base_lod_level, int lod_level, const std::string& ktx_tex_path, glare::TaskManager& task_manager)
{
#if GUI_CLIENT
	throw glare::Exception("generateKTXTexture not supported.");
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
		throw glare::Exception("Not handling KTX encoding of GIFs yet");
	}
	else
	{
		map = ImageDecoding::decodeImage(".", src_tex_path); // Load texture from disk and decode it.

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


			new_w = Maths::roundUpToMultipleOfPowerOf2(new_w, 4); // There seems to be a WebGL / 3.js limitation where the texture dimensions must be a multiple of 4.
			new_h = Maths::roundUpToMultipleOfPowerOf2(new_h, 4);

			conPrint("\tMaking basis file with dimensions " + toString(new_w) + " * " + toString(new_h) + " for LOD level " + toString(lod_level));

			const ImageMapUInt8* imagemap = map.downcastToPtr<ImageMapUInt8>();

			Reference<Map2D> resized_map = imagemap->resizeMidQuality(new_w, new_h, &task_manager);
			runtimeCheck(resized_map.isType<ImageMapUInt8>());

			writeBasisUniversalKTXFile(*resized_map.downcast<ImageMapUInt8>(), ktx_tex_path);
		}
		else
			throw glare::Exception("Unhandled image type: " + src_tex_path);
	}
#endif
}


void writeBasisUniversalKTXFile(const ImageMapUInt8& imagemap, const std::string& path)
{
#if GUI_CLIENT
	throw glare::Exception("writeBasisUniversalKTXFile not supported.");
#else

	basisu::basisu_encoder_init(); // Can be called multiple times harmlessly.

	Timer timer;

	basisu::image img(imagemap.getData(), (uint32)imagemap.getWidth(), (uint32)imagemap.getHeight(), (uint32)imagemap.getN());

	basisu::basis_compressor_params params;

	params.m_source_images.push_back(img);
	params.m_perceptual = true;
	
	params.m_write_output_basis_files = true;
	params.m_out_filename = path;
	params.m_create_ktx2_file = true;

	params.m_mip_gen = true; // Generate mipmaps for each source image
	params.m_mip_srgb = true; // Convert image to linear before filtering, then back to sRGB

	params.m_quality_level = 255;

	// Need to be set if m_quality_level is not explicitly set.
	//params.m_max_endpoint_clusters = 16128;
	//params.m_max_selector_clusters = 16128;

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

	conPrint("Basisu compression and writing of KTX file took " + timer.elapsedStringNSigFigs(3));
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


void generateLODTexturesForTexURL(const std::string& base_tex_URL, bool texture_has_alpha, WorldMaterial* mat, ResourceManager& resource_manager, glare::TaskManager& task_manager)
{
	const int start_lod_level = mat->minLODLevel() + 1;

	for(int lvl = start_lod_level; lvl <= 2; ++lvl)
	{
		const std::string lod_URL = mat->getLODTextureURLForLevel(base_tex_URL, lvl, texture_has_alpha);

		if((lod_URL != base_tex_URL) && !resource_manager.isFileForURLPresent(lod_URL)) // If the LOD URL is actually different, and if the LOD'd texture has not already been created:
		{
			const std::string local_base_path = resource_manager.pathForURL(base_tex_URL); // Path of the original source texture.
			const std::string local_lod_path  = resource_manager.pathForURL(lod_URL); // Path where we will write the LOD texture.

			conPrint("Generating LOD texture '" + local_lod_path + "'...");
			try
			{
				LODGeneration::generateLODTexture(local_base_path, lvl, local_lod_path, task_manager);

				resource_manager.setResourceAsLocallyPresentForURL(lod_URL); // Mark as present
			}
			catch(glare::Exception& e)
			{
				conPrint("Warning: Error while generating LOD texture: " + e.what());
			}
		}
	}
}


// Generate LOD and KTX textures for materials, if not already present on disk.
void generateLODTexturesForMaterialsIfNotPresent(std::vector<WorldMaterialRef>& materials, ResourceManager& resource_manager, glare::TaskManager& task_manager)
{
	for(size_t z=0; z<materials.size(); ++z)
	{
		WorldMaterial* mat = materials[z].ptr();

		if(!mat->colour_texture_url.empty())
			generateLODTexturesForTexURL(mat->colour_texture_url, mat->colourTexHasAlpha(), mat, resource_manager, task_manager);

		if(!mat->roughness.texture_url.empty())
			generateLODTexturesForTexURL(mat->roughness.texture_url, /*texture_has_alpha=*/false, mat, resource_manager, task_manager);

		if(!mat->emission_texture_url.empty())
			generateLODTexturesForTexURL(mat->emission_texture_url, /*texture_has_alpha=*/false, mat, resource_manager, task_manager);

		if(!mat->normal_map_url.empty())
			generateLODTexturesForTexURL(mat->normal_map_url, /*texture_has_alpha=*/false, mat, resource_manager, task_manager);
	}
}


} // end namespace LODGeneration


#ifdef BUILD_TESTS


#include "../utils/TestUtils.h"
#include "../utils/FileUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/PlatformUtils.h"
#include "../utils/Exception.h"
#include "../utils/Timer.h"


void LODGeneration::test()
{
	conPrint("LODGeneration::test()");

	glare::TaskManager task_manager;

	try
	{
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
