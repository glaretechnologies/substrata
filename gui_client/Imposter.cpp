/*=====================================================================
Imposter.cpp
------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "Imposter.h"


#include "../shared/ImageDecoding.h"
#include <graphics/ImageMap.h>
#include <graphics/PNGDecoder.h>
#include <AESEncryption.h>
#include <Base64.h>
#include <Exception.h>


void Imposter::floodFillColourInTransparentRegions(const std::string& image_path_in, const std::string& image_path_out)
{
	Reference<Map2D> map = ImageDecoding::decodeImage(".", image_path_in);

	if(!map.isType<ImageMapUInt8>())
		throw glare::Exception("not ImageMapUInt8");

	ImageMapUInt8& imagemap = *map.downcast<ImageMapUInt8>();

	const int xres = (int)imagemap.getWidth();
	const int yres = (int)imagemap.getHeight();

	const uint8 alpha_threshold = 10; // Pixels with alpha values >= this threshold will have their colour values used.

	ImageMapUInt8 prev_imagemap;// = imagemap;

	// Do the actual 'flood fill'
	for(int i=0; i<50; ++i) // The number of iterations gives the effective distance/radius of the flood fill.  More leaves fewer gaps but is slower to compute.
	{
		prev_imagemap = imagemap; // Copy the buffer, we don't want to modify it while reading from it.

		for(int y=0; y<yres; ++y)
			for(int x=0; x<xres; ++x)
			{
				// Pixels with an alpha value < this threshold will have their colour values set to the nearest pixel (in the search area) with alpha over this threshold.
				const uint8 alpha = imagemap.getPixel(x, y)[3];
				if(alpha < alpha_threshold)
				{
					// Find a nearby pixel that has some colour values in it (alpha > alpha_threshold).
					const int r = 1; // search radius in pixels.
					float closest_d2 = std::numeric_limits<float>::max();
					uint8 closest_src_col[4];
					for(int sy=myMax(0, y - r); sy<myMin<int>(yres, y + r + 1); ++sy)
						for(int sx=myMax(0, x - r); sx<myMin<int>(xres, x + r + 1); ++sx) // For example when r=2, we want to read from: x-2, x-1, x, x+1, x+2
						{
							uint8 src_col[4];
							for(int c=0; c<4; ++c)
								src_col[c] = prev_imagemap.getPixel(sx, sy)[c];
							if((src_col[3] > alpha_threshold) || (src_col[0] > 0))
							{
								const float d2 = (float)(Maths::square(x - sx) + Maths::square(y - sy));
								if(d2 < closest_d2)
								{
									closest_d2 = d2;
									for(int c=0; c<4; ++c)
										closest_src_col[c] = src_col[c];
								}
							}
						}

					if(closest_d2 < std::numeric_limits<float>::max()) // If found a source pixel to copy colour from:
					{
						for(int c=0; c<3; ++c) // Update RGB, leave alpha.
							imagemap.getPixel(x, y)[c] = closest_src_col[c];
					}
				}
			}
	}

	PNGDecoder::write(imagemap, image_path_out);
}