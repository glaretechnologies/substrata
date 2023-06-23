/*=====================================================================
AudioResampler.cpp
------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "AudioResampler.h"


#include <utils/ConPrint.h>
#include <utils/StringUtils.h>
#include <utils/PlatformUtils.h>


namespace glare
{
	

AudioResampler::AudioResampler()
{
	init(48000, 48000);
}


void AudioResampler::init(int src_rate_, int dest_rate_)
{
	src_rate = src_rate_;
	dest_rate = dest_rate_;
	prev_dest_dst_coords = -1;

	prev_samples_0_src_coords = -2;
	prev_samples[0] = 0;
	prev_samples[1] = 0;
}


// Use coordinates x such that:
// Src samples are spaced uniformly at x=0, x=1, x=2 etc.
// Dest samples are at x=dest_x_0, dest_x_0+dest_w, dest_x_0+2*dest_w  etc.
//
// Currently just doing linear interpolation between source samples (e.g. tent filter).
static void resampleFromContiguousBuffer(float* dest_samples, size_t num_dest, const float* src_samples, size_t num_src, double dest_x_0, double dest_w)
{
	assert(dest_x_0 >= 0);
	assert((dest_x_0 + (num_dest - 1) * dest_w) <= num_src);
	double dest_x_i = dest_x_0; // x coordinate (source coordinate relative to source sample 0) of destination sample i.
	const int max_src_i = (int)num_src - 1;
	for(size_t i=0; i<num_dest; ++i)
	{
		const int d_i = (int)dest_x_i;
		const float frac = (float)(dest_x_i - d_i);

		const int d_i_plus_1 = d_i + 1;

		dest_x_i += dest_w;
		const float val = src_samples[myClamp(d_i, 0, max_src_i)] * (1 - frac) + src_samples[myClamp(d_i_plus_1, 0, max_src_i)] * frac;
		dest_samples[i] = val;
	}
}


size_t AudioResampler::numSrcSamplesNeeded(size_t dest_num_samples)
{
	const int64 max_dest_dst_coords = prev_dest_dst_coords + dest_num_samples; // Get destination coordinate for largest destination sample to be computed
	const double max_dest_x_src_coords  = max_dest_dst_coords * (double)src_rate / (double)dest_rate; // Source coordinates for the new rightmost destination sample.

	const int64 prev_samples_1_src_coords = this->prev_samples_0_src_coords + 1; // Source coordinates for prev_samples[1]

	const size_t num_needed = (size_t)std::ceil(max_dest_x_src_coords) - prev_samples_1_src_coords;
	return num_needed;
}


void AudioResampler::resample(float* dest_samples, size_t dest_samples_size, const float* src_samples, size_t src_samples_size, js::Vector<float, 16>& temp_buf)
{
	// Form a contiguous buffer of source samples
	temp_buf.resizeNoCopy(src_samples_size + 2);
	temp_buf[0] = prev_samples[0];
	temp_buf[1] = prev_samples[1];
	for(size_t i=0; i<src_samples_size; ++i)
	{
		temp_buf[2 + i] = src_samples[i];
	}

	const int64 dest_0_dest_coords = prev_dest_dst_coords + 1;
	const double dest_0_src_coords = dest_0_dest_coords * (double)src_rate / (double)dest_rate;
	const double dest_0_x = dest_0_src_coords - prev_samples_0_src_coords;
	resampleFromContiguousBuffer(dest_samples, dest_samples_size, temp_buf.data(), temp_buf.size(), 
		dest_0_x, // dest x_0 (in source coords where source coord 0 has x = 0)
		(double)src_rate / (double)dest_rate // dest_w (in source coords where source coord 0 has x = 0)
	);

	prev_dest_dst_coords += dest_samples_size;
	prev_samples_0_src_coords += src_samples_size;
	prev_samples[0] = temp_buf[temp_buf.size() - 2];
	prev_samples[1] = temp_buf[temp_buf.size() - 1];
}


} // end namespace glare


#if BUILD_TESTS


#include <utils/TestUtils.h>


static void testResamplingLinearRamp(int src_sample_rate, int dest_sample_rate)
{
	const int N = 10000;
	std::vector<float> src_data(N);


	// Test with linear ramp
	for(int i=0; i<N; ++i)
		src_data[i] = (float)i;

	glare::AudioResampler resampler;
	resampler.init(src_sample_rate, dest_sample_rate);

	const int dest_chunk_size = 4;
	const int dest_N = dest_chunk_size * 10;

	js::Vector<float, 16> temp_buf;

	std::vector<float> resampled(dest_N);
	size_t src_i = 0;
	for(int i=0; i<dest_N; i += dest_chunk_size)
	{
		const size_t num_src_needed = resampler.numSrcSamplesNeeded(dest_chunk_size);
		testAssert(src_i + num_src_needed <= src_data.size());
		resampler.resample(&resampled[i], dest_chunk_size, &src_data[src_i], num_src_needed, temp_buf);

		src_i += num_src_needed;
	}

	// Check resampled samples
	for(int i=0; i<dest_N; ++i)
	{
		const double expected = (double)i * (double)src_sample_rate / (double)dest_sample_rate;
		testMathsApproxEq((double)resampled[i], expected);
	}

}


void glare::AudioResampler::test()
{
	testResamplingLinearRamp(/*src rate=*/8000, /*dest rate=*/48000);
	testResamplingLinearRamp(/*src rate=*/12000, /*dest rate=*/48000);
	testResamplingLinearRamp(/*src rate=*/16000, /*dest rate=*/48000);
	testResamplingLinearRamp(/*src rate=*/24000, /*dest rate=*/48000);
	testResamplingLinearRamp(/*src rate=*/48000, /*dest rate=*/48000);

	// Test an integer ratio not handled as a special case:
	testResamplingLinearRamp(/*src rate=*/6000, /*dest rate=*/48000);

	testResamplingLinearRamp(/*src rate=*/44100, /*dest rate=*/48000);


	testResamplingLinearRamp(/*src rate=*/8000, /*dest rate=*/44100);
	testResamplingLinearRamp(/*src rate=*/12000, /*dest rate=*/44100);
	testResamplingLinearRamp(/*src rate=*/16000, /*dest rate=*/44100);
	testResamplingLinearRamp(/*src rate=*/24000, /*dest rate=*/44100);
	testResamplingLinearRamp(/*src rate=*/48000, /*dest rate=*/44100);
	
	testResamplingLinearRamp(/*src rate=*/44100, /*dest rate=*/44100);


	{
		
		//// Test with 1 hz sine tone
		//for(int i=0; i<N; ++i)
		//{
		//	const double x = i / 44100.0;
		//	const double val = std::sin(1.0 * Maths::get2Pi<double>() * x);
		//	src_data[i] = val;
		//}

	}

}


#endif // BUILD_TESTS