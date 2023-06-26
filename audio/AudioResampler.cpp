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
// Dest samples are at x=dest_0_x, dest_0_x+dest_w, dest_0_x+2*dest_w  etc.
//
// Currently just doing linear interpolation between source samples (e.g. tent filter).
//
// In the example below, evaluating the signal at dest index 0, we interpolate between the value
// at src sample at x=1 and the src sample at x=2.
// If num_dest = 4, then we are evaluating up to dest_3, so should have src samples at x=0 to x=4. (e.g. num_src should be >= 5)
//
//                     dest index=0       1       2       3       4       5       6
//                                |-------|-------|-------|-------|-------|-------|-------|-> dest coords
//                    
//                             
//   |-----------------|----------|----|--|-----------|---|-----------|-------------|----->   src coords
//  x=0               x=1         |   x=2 |           x=3 |           x=4           x=5
//                     |          |       |               |
//                     d_i    dext_0_x   dest_1_x       dest_3_x
//                                |       |
//                                |<----->|
//                                 dest_w
static void resampleFromContiguousBuffer(float* dest_samples, size_t num_dest, const float* src_samples, size_t num_src, double dest_x_0, double dest_w)
{
	assert(dest_x_0 >= 0);
	assert((size_t)std::ceil(dest_x_0 + (num_dest - 1) * dest_w) <= num_src);
	double dest_i_x = dest_x_0; // x coordinate (source coordinate relative to source sample 0) of destination sample i.
	const int max_src_i = (int)num_src - 1;
	for(size_t i=0; i<num_dest; ++i)
	{
		const int d_i = (int)dest_i_x;
		const float frac = (float)(dest_i_x - d_i);

		const int d_i_plus_1 = d_i + 1;

		dest_i_x += dest_w;
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
#if 1
	// Simpler code that just forms a temporary buffer of source samples.  Less efficient due to copying of samples to the temp buffer.
	
	// Form a contiguous buffer of source samples
	temp_buf.resizeNoCopy(src_samples_size + 2);
	temp_buf[0] = prev_samples[0];
	temp_buf[1] = prev_samples[1];
	for(size_t i=0; i<src_samples_size; ++i)
		temp_buf[2 + i] = src_samples[i];

	const int64 dest_0_dest_coords = prev_dest_dst_coords + 1;
	const double dest_0_src_coords = dest_0_dest_coords * (double)src_rate / (double)dest_rate;
	const double dest_0_x = dest_0_src_coords - prev_samples_0_src_coords;
	resampleFromContiguousBuffer(dest_samples, dest_samples_size, temp_buf.data(), temp_buf.size(), 
		dest_0_x, // dest_0 x coord (in source coords where source sample 0 has x = 0)
		(double)src_rate / (double)dest_rate // dest_w (in source coords where source coord 0 has x = 0)
	);

	prev_dest_dst_coords += dest_samples_size;
	prev_samples_0_src_coords += src_samples_size;

	prev_samples[0] = temp_buf[temp_buf.size() - 2];
	prev_samples[1] = temp_buf[temp_buf.size() - 1];
#else
	// More effcient (in theory) but more complicated code.  Not sure worth the complexity.

// 
//  Suppose 200 was the last dest coord evaluated.
//  To evaluate it we need source sample at coord 100 and 101.  So we need to keep those around to evaluate dest coord 201 in the next call of resample.
// 
// --------prefix-------------------->|<---------------suffix-----------------------
//         200      201     202     203    204     205     206
//       ----|-------|-------|-------|-------|-------|-------|-------|-> dest coords
//                    
//                             
//   |-------|---------|--------------|-------------|--------------|-------------|----->   src coords
//  100      |        101            102            103            104           105
// prev_samples_0    prev_samples_1
//  x=0      |        x=1            x=2           x=3            x=4            x=5
//         dest_0_x
//          
// Lets say we have 2 prev_samples at src coords 100 and 101.  For destination samples to be a function of them, they must have src_coords <= 102, e.g src_coords <= prev_samples_0_src_coords + 2
// The largest dest sample meeting this requirement is 203.
// So if 200 was the last dest coord evaluated, we need to use a prefix of 3 samples (201, 202, 203) to evaluate all samples which are a function of src samples 100 and 101.
	
	const size_t prefix_num_src = (src_samples_size == 0) ? 2 : 3;

	temp_buf.resizeNoCopy(prefix_num_src);
	temp_buf[0] = prev_samples[0];
	temp_buf[1] = prev_samples[1];
	for(size_t i=0; i<prefix_num_src - 2; ++i)
		temp_buf[2 + i] = src_samples[i];


	const size_t prefix_max_src = prev_samples_0_src_coords + 2;
	const double prefix_max_dest = prefix_max_src * (double)dest_rate / (double)src_rate;
	const int64 prefix_max_dest_int = (int64)prefix_max_dest;
	
	const size_t prefix_num_dest = myMin<size_t>(prefix_max_dest_int - prev_dest_dst_coords, dest_samples_size);

	const double dest_w_src_coords = (double)src_rate / (double)dest_rate;

	if(prefix_num_dest > 0) // If prefix has non-zero length:
	{
		const int64 dest_0_dest_coords = prev_dest_dst_coords + 1;
		const double dest_0_src_coords = dest_0_dest_coords * dest_w_src_coords;
		const double dest_0_x = dest_0_src_coords - prev_samples_0_src_coords;
		resampleFromContiguousBuffer(dest_samples, prefix_num_dest, temp_buf.data(), temp_buf.size(), 
			dest_0_x, // dest_0 x coord (in source coords where source sample 0 has x = 0)
			dest_w_src_coords // dest_w (in source coords where source coord 0 has x = 0)
		);
	}
	
	if(dest_samples_size > prefix_num_dest) // If suffix has non-zero length:
	{
		// Do the suffix destination samples, that don't depend on prev_samples.
		// 
		//   ------prefix-------------------->|<---------------suffix-----------------------
		//         200      201     202     203    204     205     206
		//       ----|-------|-------|-------|-------|-------|-------|-------|-> dest coords
		//                    
		//                             
		//   |-----------------|--------------|-------------|--------------|-------------|----->   src coords
		//  100               101            102            103            104           105
		// prev_samples_0    prev_samples_1  x=0            x=1            x=2           x=3
		//                                           |
		//                                         dest_0_x
		const int64 suffix_dest_0_dst_coords = prefix_max_dest_int + 1;
		const double suffix_dest_0_src_coords = suffix_dest_0_dst_coords * dest_w_src_coords;
		const double suffix_dest_0_x = suffix_dest_0_src_coords - (prev_samples_0_src_coords + 2);
		resampleFromContiguousBuffer(dest_samples + prefix_num_dest, dest_samples_size - prefix_num_dest, src_samples, src_samples_size, 
			suffix_dest_0_x, // dest_0 x coord (in source coords where source sample 0 has x = 0)
			dest_w_src_coords
		);
	}

	prev_dest_dst_coords += dest_samples_size;
	prev_samples_0_src_coords += src_samples_size;

	if(src_samples_size >= 2)
	{
		prev_samples[0] = src_samples[src_samples_size - 2];
		prev_samples[1] = src_samples[src_samples_size - 1];
	}
	else if(src_samples_size >= 1)
	{
		prev_samples[0] = prev_samples[1];
		prev_samples[1] = src_samples[src_samples_size - 1];
	}
#endif
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