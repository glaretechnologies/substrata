#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "dxt_compress.h"
#include "stb_dxt.h"

int min(int A, int B) { return A < B ? A : B; }

void downSampleToNextMipMapLevel(size_t prev_W, size_t prev_H, size_t N, const uint8_t* prev_level_image_data, float alpha_scale, size_t level_W, size_t level_H,
	uint8_t* data_out, float* alpha_coverage_out)
{
	uint8_t* const dst_data = data_out;
	const uint8_t* const src_data = prev_level_image_data;
	const size_t src_W = prev_W;
	const size_t src_H = prev_H;

	// Because the new width is max(1, floor(old_width/2)), we have old_width >= new_width*2 in all cases apart from when old_width = 1.
	// If old_width >= new_width*2, then
	// old_width >= (new_width-1)*2 + 2
	// old_width > (new_width-1)*2 + 1
	// (new_width-1)*2 + 1 < old_width
	// in other words the support pixels of the box filter are guaranteed to be in range (< old_width)
	// Likewise for old height etc..
	assert((src_W == 1) || ((level_W - 1) * 2 + 1 < src_W));
	assert((src_H == 1) || ((level_H - 1) * 2 + 1 < src_H));

	assert(N == 3 || N == 4);
	if(N == 3)
	{
		if(src_W == 1)
		{
			// This is 1xN texture being downsized to 1xfloor(N/2)
			assert(level_W == 1 && src_H > 1);
			assert((level_H - 1) * 2 + 1 < src_H);

			for(int y=0; y<(int)level_H; ++y)
			{
				int val[3] = { 0, 0, 0 };
				size_t sx = 0;
				size_t sy = y*2;
				{
					const uint8_t* src = src_data + (sx + src_W * sy) * N;
					val[0] += src[0];
					val[1] += src[1];
					val[2] += src[2];
				}
				sy = y*2 + 1;
				{
					const uint8_t* src = src_data + (sx + src_W * sy) * N;
					val[0] += src[0];
					val[1] += src[1];
					val[2] += src[2];
				}
				uint8_t* const dest_pixel = dst_data + (0 + level_W * y) * N;
				dest_pixel[0] = (uint8_t)(val[0] / 2);
				dest_pixel[1] = (uint8_t)(val[1] / 2);
				dest_pixel[2] = (uint8_t)(val[2] / 2);
			}
		}
		else if(src_H == 1)
		{
			// This is Nx1 texture being downsized to floor(N/2)x1
			assert(level_H == 1 && src_W > 1);
			assert((level_W - 1) * 2 + 1 < src_W);

			for(int x=0; x<(int)level_W; ++x)
			{
				int val[3] = { 0, 0, 0 };
				int sx = x*2;
				int sy = 0;
				{
					const uint8_t* src = src_data + (sx + src_W * sy) * N;
					val[0] += src[0];
					val[1] += src[1];
					val[2] += src[2];
				}
				sx = x*2 + 1;
				{
					const uint8_t* src = src_data + (sx + src_W * sy) * N;
					val[0] += src[0];
					val[1] += src[1];
					val[2] += src[2];
				}
				uint8_t* const dest_pixel = dst_data + (x + level_W * 0) * N;
				dest_pixel[0] = (uint8_t)(val[0] / 2);
				dest_pixel[1] = (uint8_t)(val[1] / 2);
				dest_pixel[2] = (uint8_t)(val[2] / 2);
			}
		}
		else
		{
			assert(src_W >= 2 && src_H >= 2);
			assert((level_W - 1) * 2 + 1 < src_W);
			assert((level_H - 1) * 2 + 1 < src_H);

			// In this case all reads should be in-bounds
			for(int y=0; y<(int)level_H; ++y)
				for(int x=0; x<(int)level_W; ++x)
				{
					int val[3] = { 0, 0, 0 };
					int sx = x*2;
					int sy = y*2;
					{
						const uint8_t* src = src_data + (sx + src_W * sy) * N;
						val[0] += src[0];
						val[1] += src[1];
						val[2] += src[2];
					}
					sx = x*2 + 1;
					{
						const uint8_t* src = src_data + (sx + src_W * sy) * N;
						val[0] += src[0];
						val[1] += src[1];
						val[2] += src[2];
					}
					sx = x*2;
					sy = y*2 + 1;
					{
						const uint8_t* src = src_data + (sx + src_W * sy) * N;
						val[0] += src[0];
						val[1] += src[1];
						val[2] += src[2];
					}
					sx = x*2 + 1;
					{
						const uint8_t* src = src_data + (sx + src_W * sy) * N;
						val[0] += src[0];
						val[1] += src[1];
						val[2] += src[2];
					}

					uint8_t* const dest_pixel = dst_data + (x + level_W * y) * N;
					dest_pixel[0] = (uint8_t)(val[0] / 4);
					dest_pixel[1] = (uint8_t)(val[1] / 4);
					dest_pixel[2] = (uint8_t)(val[2] / 4);
				}
		}
	}
	else // else if(N == 4):
	{
		int num_opaque_px = 0;
		if(src_W == 1)
		{
			// This is 1xN texture being downsized to 1xfloor(N/2)
			assert(level_W == 1 && src_H > 1);
			assert((level_H - 1) * 2 + 1 < src_H);

			for(int y=0; y<(int)level_H; ++y)
			{
				int val[4] = { 0, 0, 0, 0 };
				int sx = 0;
				int sy = y*2;
				{
					const uint8_t* src = src_data + (sx + src_W * sy) * N;
					val[0] += src[0];
					val[1] += src[1];
					val[2] += src[2];
					val[3] += src[3];
				}
				sy = y*2 + 1;
				{
					const uint8_t* src = src_data + (sx + src_W * sy) * N;
					val[0] += src[0];
					val[1] += src[1];
					val[2] += src[2];
					val[3] += src[3];
				}
				uint8_t* const dest_pixel = dst_data + (0 + level_W * y) * N;
				dest_pixel[0] = (uint8_t)(val[0] / 2);
				dest_pixel[1] = (uint8_t)(val[1] / 2);
				dest_pixel[2] = (uint8_t)(val[2] / 2);
				dest_pixel[3] = (uint8_t)(min(255.f, alpha_scale * (val[3] / 2)));

				if(dest_pixel[3] >= 186)
					num_opaque_px++;
			}
		}
		else if(src_H == 1)
		{
			// This is Nx1 texture being downsized to floor(N/2)x1
			assert(level_H == 1 && src_W > 1);
			assert((level_W - 1) * 2 + 1 < src_W);

			for(int x=0; x<(int)level_W; ++x)
			{
				int val[4] = { 0, 0, 0, 0 };
				int sx = x*2;
				int sy = 0;
				{
					const uint8_t* src = src_data + (sx + src_W * sy) * N;
					val[0] += src[0];
					val[1] += src[1];
					val[2] += src[2];
					val[3] += src[3];
				}
				sx = x*2 + 1;
				{
					const uint8_t* src = src_data + (sx + src_W * sy) * N;
					val[0] += src[0];
					val[1] += src[1];
					val[2] += src[2];
					val[3] += src[3];
				}
				uint8_t* const dest_pixel = dst_data + (x + level_W * 0) * N;
				dest_pixel[0] = (uint8_t)(val[0] / 2);
				dest_pixel[1] = (uint8_t)(val[1] / 2);
				dest_pixel[2] = (uint8_t)(val[2] / 2);
				dest_pixel[3] = (uint8_t)(min(255.f, alpha_scale * (val[3] / 2)));

				if(dest_pixel[3] >= 186)
					num_opaque_px++;
			}
		}
		else
		{
			assert(src_W >= 2 && src_H >= 2);
			assert((level_W - 1) * 2 + 1 < src_W);
			assert((level_H - 1) * 2 + 1 < src_H);

			// In this case all reads should be in-bounds
			for(int y=0; y<(int)level_H; ++y)
				for(int x=0; x<(int)level_W; ++x)
				{
					int val[4] = { 0, 0, 0, 0 };
					int sx = x*2;
					int sy = y*2;
					{
						const uint8_t* src = src_data + (sx + src_W * sy) * N;
						val[0] += src[0];
						val[1] += src[1];
						val[2] += src[2];
						val[3] += src[3];
					}
					sx = x*2 + 1;
					{
						const uint8_t* src = src_data + (sx + src_W * sy) * N;
						val[0] += src[0];
						val[1] += src[1];
						val[2] += src[2];
						val[3] += src[3];
					}
					sx = x*2;
					sy = y*2 + 1;
					{
						const uint8_t* src = src_data + (sx + src_W * sy) * N;
						val[0] += src[0];
						val[1] += src[1];
						val[2] += src[2];
						val[3] += src[3];
					}
					sx = x*2 + 1;
					{
						const uint8_t* src = src_data + (sx + src_W * sy) * N;
						val[0] += src[0];
						val[1] += src[1];
						val[2] += src[2];
						val[3] += src[3];
					}

					uint8_t* const dest_pixel = dst_data + (x + level_W * y) * N;
					dest_pixel[0] = (uint8_t)(val[0] / 4);
					dest_pixel[1] = (uint8_t)(val[1] / 4);
					dest_pixel[2] = (uint8_t)(val[2] / 4);
					dest_pixel[3] = (uint8_t)(min(255.f, alpha_scale * (val[3] / 4)));

					if(dest_pixel[3] >= 186) // 186 = floor(256 * (0.5 ^ (1/2.2))), e.g. the value that when divided by 256 and then raised to the power of 2.2 (~ sRGB gamma), is 0.5.
						num_opaque_px++;
				}
		}

		*alpha_coverage_out = num_opaque_px / (float)(level_W * level_H);
	}
}

// The output buffer must already be correctly sized (allocate in JS land...)
void compress_dxt1 (uint8_t* out_buffer, const uint8_t* src_buffer, int32_t width, int32_t height, int32_t ch) {
    const int32_t Wm1 = width - 1; const int32_t Hm1 = height - 1;
    uint8_t inputBlock[64];
    int32_t write_i = 0;

	for (int32_t by = 0; by < height; by += 4) {
		for (int32_t bx = 0; bx < width; bx += 4) {
			int32_t z = 0;
			for (int32_t y = by; y < by + 4; ++y) {
				const int32_t useY = min(y, Hm1);
				for (int32_t x = bx; x < bx + 4; ++x) {
					const int32_t useX = min(x, Wm1);
					const uint8_t* src_ptr = src_buffer + (useY * width + useX) * 3;
					inputBlock[z++] = *src_ptr++;
					inputBlock[z++] = *src_ptr++;
					inputBlock[z++] = *src_ptr;
					inputBlock[z++] = 0;
				}
			}

            stb_compress_dxt_block((uint8_t*)(out_buffer + write_i), inputBlock, 0, STB_DXT_HIGHQUAL);
			write_i += 8;
		}
	}
}

void compress_dxt5 (uint8_t* out_buffer, const uint8_t* src_buffer, int32_t width, int32_t height, int32_t ch) {
    const int32_t Wm1 = width - 1; const int32_t Hm1 = height - 1;
    uint8_t inputBlock[64];
    int32_t write_i = 0;

	for (int32_t by = 0; by < height; by += 4) {
		for (int32_t bx = 0; bx < width; bx += 4) {
			int32_t z = 0;
			for (int32_t y = by; y < by + 4; ++y) {
				const int32_t useY = min(y, Hm1);
				for (int32_t x = bx; x < bx + 4; ++x) {
					const int32_t useX = min(x, Wm1);
					const uint8_t* src_ptr = src_buffer + (useY * width + useX) * 4;
					inputBlock[z++] = *src_ptr++;
					inputBlock[z++] = *src_ptr++;
					inputBlock[z++] = *src_ptr++;
                    inputBlock[z++] = *src_ptr;
				}
			}

            stb_compress_dxt_block((uint8_t*)(out_buffer + write_i), inputBlock, 1, STB_DXT_HIGHQUAL);
			write_i += 16;
		}
	}
}

float compute_alpha_coverage(const uint8_t* level_image_data, size_t level_W, size_t level_H) {
	const int N = 4;
	int num_opaque_px = 0;
	for(int y=0; y<(int)level_H; ++y)
	for(int x=0; x<(int)level_W; ++x)
	{
		const uint8_t alpha = level_image_data[(x + level_W * y) * N + 3];
		if(alpha >= 186) // 186 = floor(256 * (0.5 ^ (1/2.2))), e.g. the value that when divided by 256 and then raised to the power of 2.2 (~ sRGB gamma), is 0.5.
			num_opaque_px++;
	}
	return num_opaque_px / (float)(level_W * level_H);
}

// Flip a level0 texture in the Y axis
void flipY (uint8_t* src_buffer, int32_t width, int32_t height, int32_t channels) {
    const int32_t half = height / 2;
    const int32_t row = width * channels;
    for(int y = 0, s = height - 1; y < half; ++y, --s) {
        const int32_t top = s * row; const int32_t bottom = y * row;
        for(int x = 0; x < row; ++x) {
           const int32_t bl = bottom + x; const int32_t tl = top + x;
           uint8_t t = src_buffer[bl];
           src_buffer[bl] = src_buffer[tl];
           src_buffer[tl] = t;
        }
    }
}

const int32_t LVL_W = 0;
const int32_t LVL_H = 1;
const int32_t CHANNELS = 2;
const int32_t LVL_SIZE = 3;
const int32_t CMP_SIZE = 4;
const int32_t CMP_OFFSET = 5;
const int32_t LVL_STRIDE = 6;

/*
The levels input array contains six integers per level, defined by the constants above.
The src_buffer contains the level0 of the image to mipmap and compress.  src_buffer also contains sufficient space to
generate the next two mipmap levels (up to a max of 4096 x 4096 * (4 + 2 + 1) (level0 + level1 + level2)
The out_buffer contains enough space for all mipmap levels of the above texture compressed at a 4 : 1 ratio.
TODO: move channels to parm
TODO: Reduce src_buffer to level0 + level1 only (sufficient)
*/

void build_mipmaps(uint8_t* out_buffer, uint8_t* src_buffer, int32_t levelsCount, const int32_t* levels) {
    if(levelsCount < 1) return;

    int32_t level_W = levels[LVL_W], level_H = levels[LVL_H], channels = levels[CHANNELS], level_size = levels[LVL_SIZE],
        compressed_data_size = levels[CMP_SIZE], data_offset = levels[CMP_OFFSET];
    float levelOAlphaCoverage = 0.f;

    assert(channels == 4 || channels == 3);

    uint8_t* buffer_A = src_buffer + level_size; // Only guaranteed to be half of level size
    uint8_t* buffer_B = buffer_A + level_size / 2;
    uint8_t* curr_buffer = src_buffer;
    uint8_t* prev_buffer = src_buffer;

    int32_t prev_W, prev_H;

    // Flip level 0
    flipY(src_buffer, level_W, level_H, channels);

    for(int i = 0; i != levelsCount; ++i) {
        const int32_t* ptr = (int32_t*)(levels + LVL_STRIDE * i);

        prev_W = level_W; prev_H = level_H;
        level_W = ptr[LVL_W]; level_H = ptr[LVL_H]; level_size = ptr[LVL_SIZE];
        compressed_data_size = ptr[CMP_SIZE]; data_offset = ptr[CMP_OFFSET];

        if(i == 0) {
            curr_buffer = src_buffer;
            if(channels == 4) levelOAlphaCoverage = compute_alpha_coverage(curr_buffer, level_W, level_H);
        } else {
            prev_buffer = curr_buffer;
            curr_buffer = i % 2 == 0 ? buffer_B : buffer_A;

            float alpha_scale = 1.f;
            if(channels == 4) {
                for(int i = 0; i < 8; ++i) {
                    float coverage;
                    downSampleToNextMipMapLevel(prev_W, prev_H, channels, prev_buffer, alpha_scale, level_W, level_H,
                        curr_buffer, &coverage);
                    if(coverage >= .9 * levelOAlphaCoverage) break;
                    alpha_scale *= 1.1f;
                }
            } else {
                float coverage;
                downSampleToNextMipMapLevel(prev_W, prev_H, channels, prev_buffer, alpha_scale, level_W, level_H,
                    curr_buffer, &coverage);
            }
        }

        if(channels == 4) {
            compress_dxt5((uint8_t*)(out_buffer + data_offset), curr_buffer, level_W, level_H, channels);
        } else {
            compress_dxt1((uint8_t*)(out_buffer + data_offset), curr_buffer, level_W, level_H, channels);
        }
    }
}
