/*=====================================================================
ResourceProcessing.cpp
----------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "ResourceProcessing.h"


#include "../graphics/PNGDecoder.h"
#include "../graphics/KTXDecoder.h"
#include "../graphics/PerlinNoise.h"
#include "../graphics/SRGBUtils.h"
#include "../graphics/TextureProcessing.h"
#include "../graphics/DXTCompression.h"
#include "../utils/TestUtils.h"
#include "../utils/RuntimeCheck.h"
#include "../utils/GlareAllocator.h"
#include "../utils/PlatformUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/StringUtils.h"
#include "../maths/vec2.h"


#if BUILD_TESTS


static void convertTextureToCompressedKTX2File(KTXDecoder::Format ktx_format, const std::string& path)
{
	const std::string save_path = ::eatExtension(path) + "ktx2";

	glare::MallocAllocator allocator;
	allocator.incRefCount();

	try
	{
		

		Reference<Map2D> im = PNGDecoder::decode(path);
			
		Reference<TextureData> tex_data = TextureProcessing::buildTextureData(im.ptr(), &allocator, NULL, /*allow compression=*/true, /*build mipmaps=*/true);

		std::vector<std::vector<uint8> > level_image_data(tex_data->level_offsets.size());
		for(size_t k=0; k<level_image_data.size(); ++k)
		{
			const size_t level_W = myMax((size_t)1, im->getMapWidth()  / ((size_t)1 << k));
			const size_t level_H = myMax((size_t)1, im->getMapHeight() / ((size_t)1 << k));
			const size_t level_compressed_size = DXTCompression::getCompressedSizeBytes(level_W, level_H, tex_data->numChannels());

			level_image_data[k].resize(level_compressed_size);

			runtimeCheck(tex_data->level_offsets[k].offset + level_compressed_size <= tex_data->frames[0].mipmap_data.size());

			std::memcpy(level_image_data[k].data(), &tex_data->frames[0].mipmap_data[tex_data->level_offsets[k].offset], level_compressed_size);
		}

		KTXDecoder::writeKTX2File(ktx_format, /*supercompress=*/false, (int)im->getMapWidth(), (int)im->getMapHeight(), level_image_data, save_path);

		conPrint("Saved to '" + save_path + "'.");

		
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}

	allocator.decRefCount();
}


static void convertTextures()
{
	
	convertTextureToCompressedKTX2File(KTXDecoder::Format_BC3, "C:\\programming\\cyberspace\\output\\vs2022\\cyberspace_x64\\Debug\\foam_windowed.png");

	convertTextureToCompressedKTX2File(KTXDecoder::Format_BC3, "d:\\models\\foam_sprite_top.png");
	convertTextureToCompressedKTX2File(KTXDecoder::Format_BC3, "d:\\models\\foam_sprite_bottom.png");
	convertTextureToCompressedKTX2File(KTXDecoder::Format_BC3, "d:\\models\\foam_sprite_left.png");
	convertTextureToCompressedKTX2File(KTXDecoder::Format_BC3, "d:\\models\\foam_sprite_right.png");
	convertTextureToCompressedKTX2File(KTXDecoder::Format_BC3, "d:\\models\\foam_sprite_rear.png");
	convertTextureToCompressedKTX2File(KTXDecoder::Format_BC3, "d:\\models\\foam_sprite_front.png");

	convertTextureToCompressedKTX2File(KTXDecoder::Format_BC3, "d:\\models\\smoke_sprite_top.png");
	convertTextureToCompressedKTX2File(KTXDecoder::Format_BC3, "d:\\models\\smoke_sprite_bottom.png");
	convertTextureToCompressedKTX2File(KTXDecoder::Format_BC3, "d:\\models\\smoke_sprite_left.png");
	convertTextureToCompressedKTX2File(KTXDecoder::Format_BC3, "d:\\models\\smoke_sprite_right.png");
	convertTextureToCompressedKTX2File(KTXDecoder::Format_BC3, "d:\\models\\smoke_sprite_rear.png");
	convertTextureToCompressedKTX2File(KTXDecoder::Format_BC3, "d:\\models\\smoke_sprite_front.png");

	// Build caustic textures
	for(int im_i=0; im_i<32; ++im_i)
		convertTextureToCompressedKTX2File(KTXDecoder::Format_BC1, "caus3 (1)\\save." + ::leftPad(toString(1 + im_i), '0', 2) + ".png");
	
	// Modify foam image
	{
		ImageMapUInt8Ref foam_map = PNGDecoder::decode("foam.png").downcast<ImageMapUInt8>();

		const int W = (int)foam_map->getWidth();
		const int H = (int)foam_map->getHeight();
		ImageMapUInt8 map(W, H, 4);

		for(int y=0; y<H; ++y)
		for(int x=0; x<W; ++x)
		{
			Vec2f p((float)x / W, (float)y / W);

			Vec4f offset = PerlinNoise::FBM4Valued(10*p.x, 10*p.y, 10);
			p += Vec2f(offset[0], offset[1]) * 0.07;

			const Vec2f centre(0.5f);
			const float rad_frac = p.getDist(centre) / 0.5f;

			const float window = 1 - Maths::smoothStep(0.5f,1.f, rad_frac);

			const uint8 pixval = (uint8)myClamp<int>((int)((float)foam_map->getPixel(x, y)[3] * window), 0, 255);

			map.getPixel(x, y)[0] = 255;
			map.getPixel(x, y)[1] = 255;
			map.getPixel(x, y)[2] = 255;
			map.getPixel(x, y)[3] = pixval;
		}

		PNGDecoder::write(map, "foam_windowed.png");
	}

	// Make foam texture
	{
		const int W = 512;
		ImageMapUInt8 map(W, W, 4);

		for(int y=0; y<W; ++y)
		for(int x=0; x<W; ++x)
		{
			Vec2f p((float)x / W, (float)y / W);

			Vec4f offset = PerlinNoise::FBM4Valued(10*p.x, 10*p.y, 10);

			p += Vec2f(offset[0], offset[1]) * 0.07;

			const float fbm_val = myMax(0.f, 0.3f + 0.7f * PerlinNoise::FBM(
				Vec4f(
					40 * p.x, 
					40 * p.y, 
					0, 1), 10));

			const Vec2f centre(0.5f);
			const float rad_frac = p.getDist(centre) / 0.5f;
			//const float window = Maths::eval2DGaussian(p.getDist2(centre), /*standard_dev=*/0.2f);
			const float window = 1 - Maths::smoothStep(0.5f,1.f, rad_frac);

			const float val = fbm_val * window;

			const uint8 pixval = (uint8)myClamp<int>((int)(val * 255.f), 0, 255);
			map.getPixel(x, y)[0] = 255;
			map.getPixel(x, y)[1] = 255;
			map.getPixel(x, y)[2] = 255;
			map.getPixel(x, y)[3] = pixval;
		}

		PNGDecoder::write(map, "foam.png");
	}

	
#if 0
	{
		Reference<Map2D> map = ImageDecoding::decodeImage(".", "D:\\terrain\\demo\\heightfield_with_deposited_sed_0_0.exr");

		ImageMapFloatRef imagemap = map.downcast<ImageMapFloat>();

		for(size_t x=0; x<imagemap->getWidth(); ++x)
		for(size_t y=0; y<imagemap->getHeight(); ++y)
		{
			const float p_x = (x / 1024.f - 0.5f) * 8192.f;
			const float p_y = (y / 1024.f - 0.5f) * 8192.f;

			//if(p_x >= -450 && 
			const float flatten_factor_x = Maths::smoothPulse<float>(-500, -450, 665, 665 + 50, p_x);
			const float flatten_factor_y = Maths::smoothPulse<float>(-335, -300, 535, 600, p_y);
			const float flatten_factor = flatten_factor_x * flatten_factor_y;

			const float new_height = Maths::lerp(imagemap->getPixel(x, y)[0], 0.0f, flatten_factor);

			for(int i=0; i<imagemap->getN(); ++i)
				imagemap->getPixel(x, y)[i] = new_height;
		}

		EXRDecoder::SaveOptions options;
		options.compression_method = EXRDecoder::CompressionMethod_DWAB;
		EXRDecoder::saveImageToEXR(*imagemap, "D:\\terrain\\demo\\heightfield_with_deposited_sed_0_0_flattened.exr", /*layer_name=*/"", options);
	}



	{
		Reference<Map2D> map = ImageDecoding::decodeImage(".", "D:\\terrain\\demo\\PACKED_1_Grass0066_5_S 2 yellow.jpg");

		ImageMapUInt8Ref imagemap = map.downcast<ImageMapUInt8>();

		for(size_t i=0; i<imagemap->numPixels(); ++i)
		{
			// vec4(0.4, 0.5, 0.4, 1.0);
			Colour3f col(imagemap->getPixel(i)[0] / 255.f, imagemap->getPixel(i)[1] / 255.f, imagemap->getPixel(i)[2] / 255.f);
			col = toLinearSRGB(col);
			col.r *= 0.4;
			col.g *= 0.5;
			col.b *= 0.4;
			col = toNonLinearSRGB(col);
			imagemap->getPixel(i)[0] = (uint8)(col.r * 255);
			imagemap->getPixel(i)[1] = (uint8)(col.g * 255);
			imagemap->getPixel(i)[2] = (uint8)(col.b * 255);
		}

		JPEGDecoder::save(imagemap, "D:\\terrain\\demo\\Grass0066_5_S 2 yellow_adjusted.jpg", JPEGDecoder::SaveOptions());
	}
#endif

	if(false)
	{

		//Reference<Map2D> map = PNGDecoder::decode("D:\\models\\grass clump billboard 2.png");
		//ImageMapUInt8Ref imagemap = map.downcast<ImageMapUInt8>();

		ImageMapUInt8 imagemap(400, 300, 4);
		imagemap.zero();

		imagemap.getPixel(100, 100)[0] = 255; // Add red pixel
		imagemap.getPixel(100, 100)[3] = 255;

		imagemap.getPixel(300, 100)[1] = 255; // Add green pixel
		imagemap.getPixel(300, 100)[3] = 255;

		PNGDecoder::write(imagemap, "D:\\models\\flood fill test pre.png");

		imagemap.floodFillFromOpaquePixels(100, 100, 100);
		PNGDecoder::write(imagemap, "D:\\models\\flood fill test filled.png");
		
	}


	if(false)
	{

		Reference<Map2D> map = PNGDecoder::decode("D:\\models\\grass clump billboard 2.png");

		ImageMapUInt8Ref imagemap = map.downcast<ImageMapUInt8>();

		ImageMapUInt8Ref non_alpha = imagemap->extract3ChannelImage();
		PNGDecoder::write(*non_alpha, "D:\\models\\grass clump billboard non_alpha.png");

		imagemap->floodFillFromOpaquePixels(100, 100, 100);
		
		non_alpha = imagemap->extract3ChannelImage();
		PNGDecoder::write(*non_alpha, "D:\\models\\grass clump billboard flood filled non alpha.png");

		PNGDecoder::write(*imagemap, "D:\\models\\grass clump billboard flood filled.png");
		
	}
}


static void processGrassColours()
{
	const Colour3f grass_refl_observed_sRGB(22.7 / 100, 33.5 / 100, 9.4 / 100);
	const Colour3f grass_refl_observed_linear = toLinearSRGB(grass_refl_observed_sRGB);

	const Colour3f grass_trans_observed_sRGB(47.0 / 100, 58.5 / 100, 1.6 / 100);
	const Colour3f grass_trans_observed_linear = toLinearSRGB(grass_trans_observed_sRGB);

	//conPrint("grass_refl_linear: " + grass_refl_linear.toString());
	//conPrint("grass_trans_linear: " + grass_trans_linear.toString());

	const Colour3f xrite_white_ref_sRGB(243 / 255.f, 243 / 255.f, 242 / 255.f);

	const Colour3f xrite_white_ref_linear = toLinearSRGB(xrite_white_ref_sRGB);
	conPrint("xrite_white_ref_linear: " + xrite_white_ref_linear.toString());
		

	const Colour3f xrite_white_observed_sRGB(89.2 / 100, 89.1 / 100, 89.9 / 100);

	const Colour3f xrite_white_observed_linear = toLinearSRGB(xrite_white_observed_sRGB);
	conPrint("xrite_white_observed_linear: " + xrite_white_observed_linear.toString());

	const Colour3f observed_to_actual_factor(
		xrite_white_ref_linear.r / xrite_white_observed_linear.r, 
		xrite_white_ref_linear.g / xrite_white_observed_linear.g,
		xrite_white_ref_linear.b / xrite_white_observed_linear.b
	);

	conPrint("observed_to_actual_factor: " + observed_to_actual_factor.toString());

	const Colour3f grass_refl_linear_actual  = grass_refl_observed_linear  * observed_to_actual_factor;
	const Colour3f grass_trans_linear_actual = grass_trans_observed_linear * observed_to_actual_factor;

	conPrint("grass_refl_linear_actual:  " + grass_refl_linear_actual.toString());
	conPrint("grass_trans_linear_actual: " + grass_trans_linear_actual.toString());

	const Colour3f grass_refl_actual_sRGB  = toNonLinearSRGB(grass_refl_linear_actual);
	const Colour3f grass_trans_actual_sRGB = toNonLinearSRGB(grass_trans_linear_actual);

	conPrint("grass_refl_actual_sRGB:  " + grass_refl_actual_sRGB.toString());
	conPrint("grass_trans_actual_sRGB: " + grass_trans_actual_sRGB.toString());

	const float reflect_frac = 1.f / 3.f;
	const float transmit_frac = 1 - reflect_frac;

	const Colour3f grass_refl_linear_submaterial  = grass_refl_linear_actual  / reflect_frac;
	const Colour3f grass_trans_linear_submaterial = grass_trans_linear_actual / transmit_frac;

	const Colour3f grass_refl_sRGB_submaterial  = toNonLinearSRGB(grass_refl_linear_submaterial);
	const Colour3f grass_trans_sRGB_submaterial = toNonLinearSRGB(grass_trans_linear_submaterial);

	conPrint("grass_refl_sRGB_submaterial:  " + grass_refl_sRGB_submaterial.toString());
	conPrint("grass_trans_sRGB_submaterial: " + grass_trans_sRGB_submaterial.toString());
	
}


void ResourceProcessing::run(const std::string& appdata_path)
{
#if BUILD_TESTS

	//OpenGLEngineTests::buildData();
	//EnvMapProcessing::run(base_dir_path);

	if(false)
		convertTextures();
	if(false)
		processGrassColours();
	
#endif
}


#endif // BUILD_TESTS
