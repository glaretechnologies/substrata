/*=====================================================================
AudioEngine.cpp
---------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "AudioEngine.h"


#include "../rtaudio/RtAudio.h"
#include <resonance_audio_api.h>
#include <utils/MessageableThread.h>
#include <utils/CircularBuffer.h>
#include <utils/ConPrint.h>
#include <utils/StringUtils.h>
#include <utils/PlatformUtils.h>


AudioEngine::AudioEngine()
{

}


AudioEngine::~AudioEngine()
{
	shutdown();
}


static int rtAudioCallback(void* output_buffer, void* input_buffer, unsigned int n_buffer_frames, double stream_time, RtAudioStreamStatus status, void* user_data)
{
	float* output_buffer_f = (float*)output_buffer;
	AudioCallbackData* data = (AudioCallbackData*)user_data;

	
#if 0
	// Getting data on demand from Resonance, instead of using a buffer:
	if(data->resonance)
	{
		//float buf[512];//TEMP
		data->temp_buf.resize(n_buffer_frames*2);
		float* buf = data->temp_buf.data();
		bool filled_valid_buffer = false;

		{
			Lock lock(data->engine->mutex);

			// Set resonance audio buffers for all audio sources
			for(auto it = data->engine->audio_sources.begin(); it != data->engine->audio_sources.end(); ++it)
			{
				AudioSource* source = it->ptr();
				size_t cur_i = source->cur_i;
				for(size_t i=0; i<n_buffer_frames; ++i)
				{
					buf[i] = source->buffer[cur_i++];
					if(cur_i == source->buffer.size()) // If reach end of buf:
						cur_i = 0; // TODO: optimise
				}
				source->cur_i = cur_i;

				const float* bufptr = buf;
				data->resonance->SetPlanarBuffer(source->resonance_handle, &bufptr, /*num channels=*/1, n_buffer_frames);

			}

			// Get mixed/filtered data from Resonance.
			filled_valid_buffer = data->resonance->FillInterleavedOutputBuffer(
				2, // num channels
				n_buffer_frames, // num frames
				buf
			);

			//buffers_processed++;

			//for(size_t i=0; i<10; ++i)
			//	conPrint("buf[" + toString(i) + "]: " + toString(buf[i]));
			//if(buffers_processed * frames_per_buffer < 48000) // Ignore first second or so of sound because resonance seems to fill it with garbage.
			//	filled_valid_buffer = false;

			printVar(filled_valid_buffer);
			//if(!filled_valid_buffer)
				//break; // break while loop
		} // End engine->mutex scope

		  // Push mixed/filtered data onto back of buffer that feeds to rtAudioCallback.
		if(filled_valid_buffer)
		{
			//Lock lock(callback_data->mutex);
			//callback_data->buffer.pushBackNItems(buf, frames_per_buffer * 2);
			//num_samples_buffered = callback_data->buffer.size();

			for(unsigned int i=0; i<n_buffer_frames*2; ++i)
				output_buffer_f[i] = buf[i];
		}
		else
		{
			conPrint("rtAudioCallback: not enough data in queue.");
			// Just write zeroes to output_buffer
			for(unsigned int i=0; i<n_buffer_frames*2; ++i)
				output_buffer_f[i] = 0.f;
		}
	}
#else
	{
		Lock lock(data->buffer_mutex);

		if(data->buffer.size() >= n_buffer_frames*2)
		{
			//conPrint("rtAudioCallback: got data.");
			data->buffer.popFrontNItems(output_buffer_f, n_buffer_frames*2);
		}
		else
		{
			conPrint("rtAudioCallback: not enough data in queue.");
			// Just write zeroes to output_buffer
			for(unsigned int i=0; i<n_buffer_frames*2; ++i)
				output_buffer_f[i] = 0.f;
		}
	}
#endif

	return 0;
}


// Gets mixed data from resonance, puts on queue to rtAudioCallback.
class ResonanceThread : public MessageableThread
{
public:
	ResonanceThread() : buffers_processed(0) {}

	virtual void doRun() override
	{
		while(die == 0)
		{
			size_t num_samples_buffered;
			{
				Lock lock(callback_data->buffer_mutex);
				num_samples_buffered = callback_data->buffer.size();
			}

			// 512/2 samples per buffer / 48000.0 samples/s = 0.00533 s / buffer.
			// So we will aim for N of these buffers being queued, resulting in N * 0.00533 s latency.
			// For N = 4 this gives 0.0213 s = 21.3 ms of latency.
			while(num_samples_buffered < 512 * 4)
			{
				assert(temp_buf.size() >= frames_per_buffer * 2);
				float* const buf = temp_buf.data();
				bool filled_valid_buffer = false;
				{
					Lock lock(engine->mutex);

					// Set resonance audio buffers for all audio sources
					for(auto it = engine->audio_sources.begin(); it != engine->audio_sources.end(); ++it)
					{
						AudioSource* source = it->ptr();

						if(source->cur_i + frames_per_buffer <= source->buffer.size()) // If we can just copy the current buffer range directly from source->buffer:
						{
							const float* bufptr = source->buffer.data() + source->cur_i;
							resonance->SetPlanarBuffer(source->resonance_handle, &bufptr, /*num channels=*/1, frames_per_buffer);

							source->cur_i += frames_per_buffer;
							if(source->cur_i == source->buffer.size()) // If reach end of buf:
								source->cur_i = 0; // wrap
						}
						else
						{
							// Copy data to a temporary contiguous buffer
							size_t cur_i = source->cur_i;

							for(size_t i=0; i<frames_per_buffer; ++i)
							{
								buf[i] = source->buffer[cur_i++];
								if(cur_i == source->buffer.size()) // If reach end of buf:
									cur_i = 0; // TODO: optimise
							}

							source->cur_i = cur_i;

							const float* bufptr = buf;
							resonance->SetPlanarBuffer(source->resonance_handle, &bufptr, /*num channels=*/1, frames_per_buffer);
						}

						//for(size_t i=0; i<frames_per_buffer; ++i)
						//	buf[i] = 0.5;
					}

					// Get mixed/filtered data from Resonance.
					filled_valid_buffer = resonance->FillInterleavedOutputBuffer(
						2, // num channels
						frames_per_buffer, // num frames
						buf
					);

					buffers_processed++;

					//for(size_t i=0; i<10; ++i)
					//	conPrint("buf[" + toString(i) + "]: " + toString(buf[i]));
					if(buffers_processed * frames_per_buffer < 48000) // Ignore first second or so of sound because resonance seems to fill it with garbage.
						filled_valid_buffer = false;

					//printVar(filled_valid_buffer);
					if(!filled_valid_buffer)
						break; // break while loop
				} // End engine->mutex scope

				// Push mixed/filtered data onto back of buffer that feeds to rtAudioCallback.
				if(filled_valid_buffer)
				{
					Lock lock(callback_data->buffer_mutex);
					callback_data->buffer.pushBackNItems(buf, frames_per_buffer * 2);
					num_samples_buffered = callback_data->buffer.size();
				}
			}

			PlatformUtils::Sleep(1);
		}
	}

	virtual void kill()
	{
		die = 1;
	}

	AudioEngine* engine;
	vraudio::ResonanceAudioApi* resonance;
	AudioCallbackData* callback_data;
	glare::AtomicInt die;

	uint64 frames_per_buffer; // e.g. 256, with 2 samples per frame = 512 samples.
	uint64 buffers_processed;

	std::vector<float> temp_buf;
};


void AudioEngine::init()
{
	audio = new RtAudio();
	
	// Determine the number of devices available
	unsigned int devices = audio->getDeviceCount();
	// Scan through devices for various capabilities
	RtAudio::DeviceInfo info;
	for ( unsigned int i=0; i<devices; i++ ) {
		info = audio->getDeviceInfo( i );
		if ( info.probed == true ) {
			// Print, for example, the maximum number of output channels for each device

			conPrint("name = " + info.name);
			conPrint("maximum output channels = " + toString(info.outputChannels));
			conPrint("maximum input channels = " + toString(info.inputChannels));
			conPrint("supported sample rates = " + toString(info.inputChannels));
			for(auto r : info.sampleRates)
				conPrint(toString(r) + " hz");
			conPrint("preferredSampleRate = " + toString(info.preferredSampleRate));
		}
	}


	unsigned int default_output_dev = audio->getDefaultOutputDevice();
	conPrint("default_output_dev: " + toString(default_output_dev));

	info = audio->getDeviceInfo(default_output_dev);
	unsigned int use_sample_rate = info.preferredSampleRate;

	RtAudio::StreamParameters parameters;
	parameters.deviceId = audio->getDefaultOutputDevice();
	parameters.nChannels = 2;
	parameters.firstChannel = 0;
	unsigned int sample_rate = use_sample_rate;
	unsigned int buffer_frames = 256; // 256 sample frames. NOTE: might be changed by openStream() below.

	conPrint("Using sample rate of " + toString(use_sample_rate) + " hz");

	callback_data.resonance = NULL;
	callback_data.engine = this;

	try
	{
		audio->openStream(&parameters, /*input parameters=*/NULL, RTAUDIO_FLOAT32, sample_rate, &buffer_frames, rtAudioCallback, /*userdata=*/&callback_data);
	}
	catch(RtAudioError& e)
	{
		throw glare::Exception(e.what());
	}
	

	// Resonance audio
	resonance = vraudio::CreateResonanceAudioApi(
		2, // num channels
		buffer_frames, // frames per buffer
		sample_rate // sample rate, hz
	);

	callback_data.resonance = resonance;

	Reference<ResonanceThread> t = new ResonanceThread();
	t->engine = this;
	t->resonance = this->resonance;
	t->callback_data = &this->callback_data;
	t->frames_per_buffer = buffer_frames;
	t->temp_buf.resize(buffer_frames * 2);
	resonance_thread_manager.addThread(t);

	try
	{
		audio->startStream();
	}
	catch(RtAudioError& e)
	{
		throw glare::Exception(e.what());
	}
}


void AudioEngine::shutdown()
{
	resonance_thread_manager.killThreadsBlocking();
	
	if(audio)
	{
		audio->stopStream();
		audio->closeStream();
	}

	delete resonance;
	resonance = NULL;

	delete audio;
	audio = NULL;
}


void AudioEngine::addSource(AudioSourceRef source)
{
	source->resonance_handle = resonance->CreateSoundObjectSource(vraudio::RenderingMode::kBinauralHighQuality);

	Lock lock(mutex);
	audio_sources.insert(source);
}


void AudioEngine::setHeadTransform(const Vec4f& head_pos, const Quatf& head_rot)
{
	//Lock lock(mutex);

	resonance->SetHeadPosition(head_pos[0], head_pos[1], head_pos[2]);
	resonance->SetHeadRotation(head_rot.v[0], head_rot.v[1], head_rot.v[2], head_rot.v[3]);
}
