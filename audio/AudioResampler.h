/*=====================================================================
AudioResampler.h
----------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include <utils/MessageableThread.h>
#include <utils/AtomicInt.h>
#include <utils/Vector.h>


namespace glare
{


/*=====================================================================
AudioResampler
--------------

=====================================================================*/
class AudioResampler
{
public:
	AudioResampler();

	void init(int src_rate, int dest_rate);

	size_t numSrcSamplesNeeded(size_t dest_num_samples);

	void resample(float* dest_samples, size_t dest_samples_size, const float* src_samples, size_t src_samples_size, js::Vector<float, 16>& temp_buf);

	static void test();

private:
	int src_rate, dest_rate;
	int64 prev_dest_dst_coords; // Destination coord of largest dest sample we have returned.
	
	int64 prev_samples_0_src_coords; // Source coordinates for prev_samples[0]
	float prev_samples[2];
};


} // end namespace glare
