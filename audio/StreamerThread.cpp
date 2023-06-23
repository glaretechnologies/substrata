/*=====================================================================
StreamerThread.cpp
------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "StreamerThread.h"


#include "AudioEngine.h"
#include "MP3AudioFileReader.h"
#include <utils/CircularBuffer.h>
#include <utils/ConPrint.h>
#include <utils/StringUtils.h>
#include <utils/PlatformUtils.h>
#include <utils/MemMappedFile.h>
#include <utils/FileInStream.h>


namespace glare
{
	

StreamerThread::StreamerThread(AudioEngine* audio_engine_) : audio_engine(audio_engine_)
{}


void StreamerThread::doRun()
{
	PlatformUtils::setCurrentThreadNameIfTestsEnabled("Audio StreamerThread");

	while(die == 0)
	{
		{
			Lock lock(audio_engine->mutex);

			for(auto it = audio_engine->sources_playing_streams.begin(); it != audio_engine->sources_playing_streams.end(); ++it)
			{
				MP3AudioStreamer* streamer = it->first.ptr();

				const std::set<AudioSourceRef>& sources_playing_stream = it->second;

				if(!sources_playing_stream.empty())
				{
					// Get the smallest buffer size over all sources playing this stream.
					size_t min_src_buffer_size = std::numeric_limits<size_t>::max();
					for(auto src_it = sources_playing_stream.begin(); src_it != sources_playing_stream.end(); ++src_it)
						min_src_buffer_size = myMin(min_src_buffer_size, (*src_it)->buffer.size());


					/*
					Each mp3 frame should supply us with 1152 sample-frames.
					If we read up to 4 frames at a time, then that should be sufficient to fill up the 4096 sized buffer.
					*/
					const int max_num_iters = 4;
					int iter = 0;
					while(min_src_buffer_size < 4096) // 4096 samples / 44100 samples/s  = 0.092 s = 92 ms of audio, which should be sufficient to avoid stuttering due to buffer underflows.
					{
						int num_channels, sample_freq_hz;
						const bool is_EOF = streamer->decodeFrame(/*samples out=*/samples, num_channels, sample_freq_hz);

						// Convert samples to mono_samples.
						if(num_channels == 1)
						{
							mono_samples = samples;
						}
						else if(num_channels == 2)
						{
							const size_t num_frames = samples.size() / 2;
							if(samples.size() == num_frames * 2)
							{
								mono_samples.resizeNoCopy(num_frames);
								for(size_t i=0; i<num_frames; ++i)
									mono_samples[i] = (samples[i*2 + 0] + samples[i*2 + 1]) * 0.5f;
							}
						}
						else
							mono_samples.resize(0);

						if(is_EOF)
							streamer->seekToBeginningOfFile();

						// If the source file is not a valid mp3 file, then we won't get samples from it.  Avoid looping forever in that case.
						iter++;
						if(iter > max_num_iters)
							break;

						// Pass the mono_samples onto the audio sources playing this stream.
						for(auto src_it = sources_playing_stream.begin(); src_it != sources_playing_stream.end(); ++src_it)
						{
							AudioSource* source = src_it->ptr();
							source->buffer.pushBackNItems(mono_samples.data(), mono_samples.size());
							if(sample_freq_hz != 0)
							{
								// If we have read a sample rate from the mp3 file and it differs from the default, re-init the resampler.
								if(source->sampling_rate != sample_freq_hz)
								{
									source->sampling_rate = sample_freq_hz;
									source->resampler.init(/*src rate=*/sample_freq_hz, /*dest rate*/audio_engine->getSampleRate());
								}
							}
						}

						min_src_buffer_size += mono_samples.size();
					}
				}
			}
		} // End lock scope
		
		PlatformUtils::Sleep(2);
	}
}


} // end namespace glare
