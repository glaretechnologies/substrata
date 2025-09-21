/*=====================================================================
AudioEngine.cpp
---------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "AudioEngine.h"


#include "AudioFileReader.h"
#include "MP3AudioFileReader.h"
#include "StreamerThread.h"
#include <utils/MessageableThread.h>
#include <utils/CircularBuffer.h>
#include <utils/ConPrint.h>
#include <utils/StringUtils.h>
#include <utils/PlatformUtils.h>
#include <utils/MemMappedFile.h>
#include <utils/FileInStream.h>
#include <utils/RuntimeCheck.h>
#include <utils/MemMappedFile.h>
#include <utils/Timer.h>
#include <resonance_audio/api/resonance_audio_api.h>
#include <tracy/Tracy.hpp>
#include <limits>


#define USE_MINIAUDIO 1

#if USE_MINIAUDIO

#define MA_NO_RESOURCE_MANAGER		// Disable playing sounds from files etc.
#define MA_NO_DECODING				// Disables decoding APIs.
#define MA_NO_ENCODING				// Disables encoding APIs.
#define MA_NO_GENERATION			// Disables generation APIs such a ma_waveform and ma_noise.
#define MA_COINIT_VALUE		COINIT_APARTMENTTHREADED	// This is needed otherwise Qt's OpenFileDialog hangs.

#if EMSCRIPTEN

#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS // Disables all backends by default and requires MA_ENABLE_* to enable specific backends.
#define MA_ENABLE_WEBAUDIO

#elif defined(__APPLE__)

#define MA_NO_RUNTIME_LINKING // Disables runtime linking. This is useful for passing Apple's notarization process.

#endif

#ifdef _WIN32
#include <objbase.h> // For COINIT_APARTMENTTHREADED
#endif

#define MINIAUDIO_IMPLEMENTATION
#include "../miniaudio/miniaudio.h"

#else // else if !USE_MINIAUDIO:

#include "../rtaudio/RtAudio.h"

#endif


static const int INVALID_RESONANCE_HANDLE = std::numeric_limits<int>::max();


namespace glare
{


AudioSource::AudioSource()
:	resonance_handle(INVALID_RESONANCE_HANDLE), cur_read_i(0), type(SourceType_NonStreaming), spatial_type(SourceSpatialType_Spatial), paused(false), looping(true), remove_on_finish(false), volume(1.f), mute_volume_factor(1.f), mute_change_start_time(-2), mute_change_end_time(-1), mute_vol_fac_start(1.f),
	mute_vol_fac_end(1.f), pos(0,0,0,1), num_occlusions(0), userdata_1(0), doppler_factor(1), smoothed_cur_level(0), sampling_rate(44100), EOF_marker_position(std::numeric_limits<int64>::max())
{}


AudioSource::~AudioSource()
{}


void AudioSource::startMuting(double cur_time, double transition_period)
{
	if(mute_vol_fac_end != 0) // If we have not already started muting, and are not already muted
	{
		mute_change_start_time = cur_time;
		mute_change_end_time = cur_time + transition_period * mute_volume_factor;
		mute_vol_fac_start = mute_volume_factor;
		mute_vol_fac_end = 0;

		// conPrint("Started muting, current mute_volume_factor: " + toString(mute_volume_factor));
	}
}


void AudioSource::startUnmuting(double cur_time, double transition_period)
{
	if(mute_vol_fac_end != 1) // If we have not already started unmuting, and are not already unmuted
	{
		mute_change_start_time = cur_time;
		mute_change_end_time = cur_time + transition_period * (1.f - mute_volume_factor);
		mute_vol_fac_start = mute_volume_factor;
		mute_vol_fac_end = 1;
		
		// conPrint("Started unmuting");
	}
}


void AudioSource::updateCurrentMuteVolumeFactor(double cur_time)
{
	// If we are still in the mute volume factor change transition period,
	// and the transition period isn't near to zero (avoid divide by zero)
	if((cur_time < mute_change_end_time) && ((mute_change_end_time - mute_change_start_time) > 1.0e-4))
	{
		const double frac = (cur_time - mute_change_start_time) / (mute_change_end_time - mute_change_start_time); // Fraction of time through transition period

		mute_volume_factor = mute_vol_fac_start + (float)frac * (mute_vol_fac_end - mute_vol_fac_start);
	}
	else
	{
		mute_volume_factor = mute_vol_fac_end;
	}
}


void AudioSource::setMuteVolumeFactorImmediately(float factor)
{ 
	mute_change_end_time = -1;
	mute_volume_factor = factor;
}


void AudioSource::updateDopplerEffectFactor(const Vec4f& source_linear_vel, const Vec4f& listener_linear_vel, const Vec4f& listener_pos)
{
	const Vec4f source_to_listener = listener_pos - pos;
	assert(source_to_listener[3] == 0);

	const Vec4f projected_source_vel = projectOntoDir(source_linear_vel, source_to_listener);

	const Vec4f projected_listener_vel = projectOntoDir(listener_linear_vel, source_to_listener);

	const float v_s = projected_source_vel.length()   * Maths::sign(dot(projected_source_vel,   source_to_listener));
	const float v_l = projected_listener_vel.length() * Maths::sign(dot(projected_listener_vel, source_to_listener));

	const float c = 343.f; // Speed of sound in air.

	this->doppler_factor = (c - v_l) / (c - v_s);
}


bool AudioSource::isPlaying()
{
	return !paused && (looping || (EOF_marker_position > 0)); // !stream_reached_EOF;
}


float SoundFile::maxVal() const
{
	float m = -std::numeric_limits<float>::infinity();
	for(float x : buf->buffer)
		m = myMax(m, x);
	return m;
}


float SoundFile::minVal() const
{
	float m = std::numeric_limits<float>::infinity();
	for(float x : buf->buffer)
		m = myMin(m, x);
	return m;
}


AudioEngine::AudioEngine()
:	audio(NULL),
	resonance(NULL),
	initialised(false)
{

}


AudioEngine::~AudioEngine()
{
	shutdown();
}


#if USE_MINIAUDIO

// This function will be called when miniaudio needs more data.
static void miniAudioCallBack(ma_device* device, void* output_buffer, const void* /*input_buffer*/, ma_uint32 frame_count)
{
	float* output_buffer_f = (float*)output_buffer;
	AudioCallbackData* data = (AudioCallbackData*)(device->pUserData);
	const size_t num_samples_needed = frame_count * 2; // We always use stereo output so each frame is 2 samples.

	if(false)
	{
		size_t data_buffer_size;
		{
			Lock lock(data->buffer_mutex);
			data_buffer_size = data->buffer.size();
		}
		conPrint("miniAudioCallBack(): num_samples_needed: " + toString(num_samples_needed) + ", data->buffer.size(): " + toString(data_buffer_size));
	}

	{
		Lock lock(data->buffer_mutex);

		const size_t num_samples_to_dequeue = myMin(num_samples_needed, data->buffer.size());

		data->buffer.popFrontNItems(output_buffer_f, num_samples_to_dequeue);

		// clamp data
		for(size_t z=0; z<num_samples_to_dequeue; ++z)
			output_buffer_f[z] = myClamp(output_buffer_f[z], -1.f, 1.f);


		//if(num_samples_to_dequeue < num_samples_needed)
		//	conPrint("miniAudioCallBack(): underflow: not enough samples in queue."); // Note that this will also happen if there are no audio sources.

		// Pad with zeroes if there wasn't enough data in the queue
		for(size_t i=num_samples_to_dequeue; i<num_samples_needed; ++i)
			output_buffer_f[i] = 0.f;
	}
}

#else // else if !USE_MINIAUDIO:

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

			// clamp data
			for(unsigned int z=0; z<n_buffer_frames*2; ++z)
				output_buffer_f[z] = myClamp(output_buffer_f[z], -1.f, 1.f);
		}
		else
		{
			//conPrint("rtAudioCallback: not enough data in queue.");
			// Just write zeroes to output_buffer
			for(unsigned int i=0; i<n_buffer_frames*2; ++i)
				output_buffer_f[i] = 0.f;
		}
	}
#endif

	return 0;
}
#endif // end if !USE_MINIAUDIO


static void zeroBuffer(js::Vector<float, 16>& v)
{
	for(size_t i=0; i<v.size(); ++i)
		v[i] = 0;
}


// Gets buffered audio data from audio sources, copies it to resonance buffers.
// Then gets mixed data from resonance, puts on queue to rtAudioCallback.
class ResonanceThread : public MessageableThread
{
public:
	ResonanceThread() : buffers_processed(0) {}

	virtual void doRun() override
	{
		PlatformUtils::setCurrentThreadNameIfTestsEnabled("ResonanceThread");
		
		try
		{
			while(die == 0)
			{
				size_t num_samples_buffered;
				{
					Lock lock(callback_data->buffer_mutex);
					num_samples_buffered = callback_data->buffer.size();
				}

				// Consider individual buffers of 256 frames = 512 samples (each frame being 2 samples for stereo output).
				// 256 frames per buffer / 48000.0 frames/s = 0.00533 s / buffer.
				// So we will aim for N of these buffers being queued, resulting in N * 0.00533 s latency.
				// For N = 4 this gives 0.0213 s = 21.3 ms of latency.
				while(num_samples_buffered < (frames_per_buffer * 2) * 4)
				{
					bool filled_valid_buffer = false;
					{
						Lock lock(engine->mutex);

						// Set resonance audio buffers for all audio sources
						for(auto it = engine->active_audio_sources.begin(); it != engine->active_audio_sources.end(); )
						{
							AudioSource* source = it->ptr();
							bool remove_source = false;

							const int source_sampling_rate = source->sampling_rate;
							const int resonance_sampling_rate = engine->getSampleRate();

							size_t src_samples_needed;
							if(source_sampling_rate == resonance_sampling_rate)
								src_samples_needed = frames_per_buffer;
							else
								src_samples_needed = (int)source->resampler.numSrcSamplesNeeded(frames_per_buffer);

							temp_buf.resizeNoCopy(src_samples_needed);

							const float* contiguous_data_ptr = temp_buf.data(); // Pointer to a buffer of contiguous source samples.
							// Will either point into an existing shared buffer, or at the start of temp_buf if we need to use it.

							if(source->type == AudioSource::SourceType_NonStreaming)
							{
								if(source->shared_buffer.nonNull()) // If we are reading from shared_buffer:
								{
									if(source->cur_read_i + src_samples_needed <= source->shared_buffer->buffer.size()) // If we can just copy the current buffer range directly from source->buffer:
									{
										contiguous_data_ptr = &source->shared_buffer->buffer[source->cur_read_i];
										
										source->cur_read_i += src_samples_needed;
										if(source->looping && (source->cur_read_i == source->shared_buffer->buffer.size())) // If reached end of buf, and this is a looping audio source:
											source->cur_read_i = 0; // wrap
									}
									else
									{
										// The data range we want to read from the shared buffer exceeds the shared buffer length.  Just read as much data as we can and then pad with zeroes, or wrap the source index.
										// Copy data to a temporary contiguous buffer
										size_t cur_i = source->cur_read_i;

										const size_t source_buffer_size = source->shared_buffer->buffer.size();
										for(size_t i=0; i<src_samples_needed; ++i)
										{
											// TODO: optimise: do simple copy in 2 sections.
											if(cur_i < source_buffer_size)
											{
												temp_buf[i] = source->shared_buffer->buffer[cur_i++];

												if(source->looping && (cur_i == source_buffer_size)) // If reached end of buf, and this is a looping audio source:
													cur_i = 0; // Reset read index back to start of buffer.
											}
											else
											{
												temp_buf[i] = 0;
											}
										}
										source->cur_read_i = cur_i;
										remove_source = source->remove_on_finish && (cur_i >= source_buffer_size); // Remove the source if we reached the end of the buffer.
									}
								}
								else // Else if we are reading from a circular buffer:
								{
									assert(0); // SourceType_OneShot sources should only read from shared buffers.
									zeroBuffer(temp_buf); // Just pass zeroes to resonance so as to not blow up the listener's ears.
								}
							}
							else if(source->type == AudioSource::SourceType_Streaming)
							{
								if(!source->mix_sources.empty())
								{
									// Mix together the audio sources, applying pitch shift factor and volume factor.
									zeroBuffer(temp_buf);

									for(size_t z=0; z<source->mix_sources.size(); ++z)
									{
										MixSource& mix_source = source->mix_sources[z];
										const size_t src_buffer_size  = mix_source.soundfile->buf->buffer.size();
										const float* const src_buffer = mix_source.soundfile->buf->buffer.data();

										for(size_t i=0; i<src_samples_needed; ++i)
										{
											mix_source.sound_file_i += mix_source.source_delta; // Advance floating-point read index (index into source buffer)

											const size_t index   = (size_t)mix_source.sound_file_i % src_buffer_size;
											const size_t index_1 = (index + 1)                     % src_buffer_size;
											const float frac = (float)(mix_source.sound_file_i - (size_t)mix_source.sound_file_i);

											const float sample = src_buffer[index] * (1 - frac) + src_buffer[index_1] * frac;
											temp_buf[i] += sample * mix_source.mix_factor;
										}
									}
								}
								else
								{
									if(source->buffer.size() >= src_samples_needed) // If there is sufficient data in the circular buffer:
									{
										// Copy data to temp_buf before we pop it from the source buffer.  NOTE: Could optimise by popping later, after we process the data, to avoid copying to temp_buf.
										source->buffer.popFrontNItems(/*dest=*/temp_buf.data(), src_samples_needed);
									}
									else
									{
										//conPrint("Ran out of data for streaming audio src!");
										const size_t source_buf_size = source->buffer.size();
										source->buffer.popFrontNItems(/*dest=*/temp_buf.data(), source_buf_size); // Copy the data that there is to temp_buf
										for(size_t i=source_buf_size; i<src_samples_needed; ++i) // Pad the rest with zeroes.
											temp_buf[i] = 0.f;
									}
								}
							}
							else
								runtimeCheckFailed("Invalid source->type");

							runtimeCheck(contiguous_data_ptr != NULL);
							if(source_sampling_rate == resonance_sampling_rate)
							{
								const float* bufptr = contiguous_data_ptr;
								resonance->SetPlanarBuffer(source->resonance_handle, &bufptr, /*num channels=*/1, frames_per_buffer);
							}
							else
							{
								// Resample audio to the audio engine and Resonance sampling rate.
								resampled_buf.resizeNoCopy(frames_per_buffer);
								
								source->resampler.resample(resampled_buf.data(), frames_per_buffer, contiguous_data_ptr, src_samples_needed, temp_resampling_buf);

								const float* bufptr = resampled_buf.data();
								resonance->SetPlanarBuffer(source->resonance_handle, &bufptr, /*num channels=*/1, frames_per_buffer);
							}

							source->EOF_marker_position -= (int64)src_samples_needed;
							const bool stream_reached_EOF = source->EOF_marker_position <= 0;

							const bool stop_source_playing = remove_source || (!source->looping && stream_reached_EOF);

							if(remove_source)
							{
								// conPrint("ResonanceThread: removing audio source.");
								engine->audio_sources.erase(*it);
							}

							if(stop_source_playing)
							{
								// Remove the audio source from Resonance and remove from active_audio_sources.
								// conPrint("ResonanceThread: stopping source playing: removing from resonance and active_audio_sources.");
								resonance->DestroySource(source->resonance_handle);
								source->resonance_handle = INVALID_RESONANCE_HANDLE;
								
								it = engine->active_audio_sources.erase(it);  // Remove currently iterated to source, advance iterator.
							}
							else
								++it;
						} // End for each audio source

						// Get mixed/filtered data from Resonance.
						temp_buf.resizeNoCopy(frames_per_buffer * 2); // We will receive stereo data
						filled_valid_buffer = resonance->FillInterleavedOutputBuffer(
							2, // num channels
							frames_per_buffer, // num frames
							temp_buf.data()
						);

						buffers_processed++;

						//for(size_t i=0; i<10; ++i)
						//	conPrint("buf[" + toString(i) + "]: " + toString(buf[i]));
						if(buffers_processed * frames_per_buffer < 48000) // Ignore first second or so of sound because resonance seems to fill it with garbage.
							filled_valid_buffer = false;

						if(!filled_valid_buffer)
							break; // break while loop
					} // End engine->mutex scope

					// Push mixed/filtered data onto back of buffer that feeds to rtAudioCallback / miniaudioCallBack.
					if(filled_valid_buffer)
					{
						Lock lock(callback_data->buffer_mutex);
						callback_data->buffer.pushBackNItems(temp_buf.data(), frames_per_buffer * 2);
						num_samples_buffered = callback_data->buffer.size();
					}
				}

				PlatformUtils::Sleep(1);
			}
		}
		catch(glare::Exception& e)
		{
			conPrint("ResonanceThread excep: " + e.what());
		}
	}

	virtual void kill() override
	{
		die = 1;
	}

	AudioEngine* engine;
	vraudio::ResonanceAudioApi* resonance;
	AudioCallbackData* callback_data;
	glare::AtomicInt die;

	uint64 frames_per_buffer; // e.g. 256, with 2 samples per frame = 512 samples.
	uint64 buffers_processed;

	js::Vector<float, 16> temp_buf;
	js::Vector<float, 16> temp_resampling_buf;
	js::Vector<float, 16> resampled_buf;
};


void AudioEngine::init()
{
	const unsigned int desired_sample_rate = 48000;

#if EMSCRIPTEN
	unsigned int buffer_frames = 1024; // Chrome seems to need more frames to avoid underflow.
#else
	unsigned int buffer_frames = 256; // Request 256 frames per buffer. NOTE: might be changed by openStream() etc. below.
#endif

	callback_data.resonance = NULL;
	callback_data.engine = this;

#if USE_MINIAUDIO

	ma_device_config config = ma_device_config_init(ma_device_type_playback);
	config.playback.format           = ma_format_f32;
	config.playback.channels         = 2;
	config.sampleRate                = desired_sample_rate;
	config.periodSizeInFrames        = buffer_frames; // Without this, Firefox tries to use 2048 frames per buffer, which is too large for our purposes.
	config.dataCallback              = miniAudioCallBack;   // This function will be called when miniaudio needs more data.
	config.pUserData                 = &callback_data;   // Can be accessed from the device object (device.pUserData).
	config.noPreSilencedOutputBuffer = true; // We will zero the output buffer ourself if needed.
	config.noClip                    = true; // Disable clipping to [-1, 1], we do this ourself.
	config.noDisableDenormals        = true; // We don't need denormals flushed during the callback
	config.noFixedSizedCallback      = true; // We don't need fixed size callback buffers.


	device = new ma_device();
	const ma_result res = ma_device_init(NULL, &config, device);
	if(res != MA_SUCCESS)
		throw glare::Exception("Failed to initialise miniaudio device.  Error code: " + toString(res));

	this->sample_rate = device->sampleRate; // Get actual sample rate used.
	buffer_frames = device->playback.internalPeriodSizeInFrames; // Get actual buffer size used.

#else // else if !USE_MINIAUDIO:

#if defined(_WIN32)
	const RtAudio::Api rtaudio_api = RtAudio::WINDOWS_DS;
#elif defined(OSX)
	const RtAudio::Api rtaudio_api = RtAudio::MACOSX_CORE;
#else // else linux:
	const RtAudio::Api rtaudio_api = RtAudio::LINUX_PULSE;
#endif

	audio = new RtAudio(rtaudio_api);
	
	// Determine the number of devices available
	const unsigned int devices = audio->getDeviceCount();
	if(devices == 0)
		throw glare::Exception("No audio devices found");

	// Scan through devices for various capabilities
	RtAudio::DeviceInfo info;
	//for(unsigned int i=0; i<devices; i++ )
	//{
	//	info = audio->getDeviceInfo(i);
	//	//if(info.probed)
	//	{
	//		// Print, for example, the maximum number of output channels for each device
	//
	//		// conPrint("name = " + info.name);
	//		// conPrint("maximum output channels = " + toString(info.outputChannels));
	//		// conPrint("maximum input channels = " + toString(info.inputChannels));
	//		// conPrint("supported sample rates = " + toString(info.inputChannels));
	//		// for(auto r : info.sampleRates)
	//		// 	conPrint(toString(r) + " hz");
	//		// conPrint("preferredSampleRate = " + toString(info.preferredSampleRate));
	//	}
	//}


	const unsigned int default_output_dev = audio->getDefaultOutputDevice();
	// conPrint("default_output_dev: " + toString(default_output_dev));

	info = audio->getDeviceInfo(default_output_dev);
	if(!info.isDefaultOutput)
		throw glare::Exception("Failed to find output audio device");

	//unsigned int desired_sample_rate = 44100;// info.preferredSampleRate;

	RtAudio::StreamParameters parameters;
	parameters.deviceId = audio->getDefaultOutputDevice();
	parameters.nChannels = 2;
	parameters.firstChannel = 0;

	RtAudioErrorType rtaudio_res = audio->openStream(&parameters, /*input parameters=*/NULL, RTAUDIO_FLOAT32, desired_sample_rate, &buffer_frames, rtAudioCallback, /*userdata=*/&callback_data);
	if(rtaudio_res != RTAUDIO_NO_ERROR)
		throw glare::Exception("Error opening audio stream: code: " + toString((int)rtaudio_res));

	this->sample_rate = audio->getStreamSampleRate(); // Get actual sample rate used.
	
#endif // end if !USE_MINIAUDIO

	conPrint("Using sample rate: " + toString(sample_rate) + " hz, buffer_frames: " + toString(buffer_frames));

	// Resonance audio
	resonance = vraudio::CreateResonanceAudioApi(
		2, // num channels
		buffer_frames, // frames per buffer
		sample_rate // sample rate, hz
	);

	/*vraudio::ReflectionProperties refl_props;

	// (7.6, 51.8, 0),   (22.5, 69.2, 5.4)
	refl_props.room_dimensions[0] = 22.5f - 7.6f;
	refl_props.room_dimensions[1] = 69.2f - 51.8f;
	refl_props.room_dimensions[2] = 5.4f;

	
	refl_props.room_position[0] = (7.6f + 22.5f)/2;
	refl_props.room_position[1] = (51.8f + 69.2f)/2;
	refl_props.room_position[2] = (5.4f)/2;

	for(int i=0; i<6; ++i)
		refl_props.coefficients[i] = 0.8f;
	refl_props.coefficients[5] = 0.2f;
	refl_props.coefficients[2] = 0.3f;
	refl_props.gain = 0.7f;
	resonance->SetReflectionProperties(refl_props);

	vraudio::ReverbProperties reverb_props;
	reverb_props.gain = 0.02f;
	for(int i=0; i<9; ++i)
		reverb_props.rt60_values[i] = 0.5f;
	resonance->SetReverbProperties(reverb_props);

	resonance->EnableRoomEffects(true);*/

	callback_data.resonance = resonance;

	{
		Reference<ResonanceThread> t = new ResonanceThread();
		t->engine = this;
		t->resonance = this->resonance;
		t->callback_data = &this->callback_data;
		t->frames_per_buffer = buffer_frames;
		t->temp_buf.resize(buffer_frames * 2);
		thread_manager.addThread(t);
	}

	{
		Reference<StreamerThread> t = new StreamerThread(this);
		thread_manager.addThread(t);
	}

#if USE_MINIAUDIO
	ma_device_start(device); // The device is sleeping by default so you'll need to start it manually.
#else
	rtaudio_res = audio->startStream();
	if(rtaudio_res != RTAUDIO_NO_ERROR)
		throw glare::Exception("Error opening audio stream: code: " + toString((int)rtaudio_res));
#endif

	this->initialised = true;
}


void AudioEngine::setRoomEffectsEnabled(bool enabled)
{
	if(!initialised)
		return;
	resonance->EnableRoomEffects(enabled);
}


void AudioEngine::setCurentRoomDimensions(const js::AABBox& room_aabb)
{
	if(!initialised)
		return;

	vraudio::ReflectionProperties refl_props;

	refl_props.room_dimensions[0] = room_aabb.axisLength(0);
	refl_props.room_dimensions[1] = room_aabb.axisLength(1);
	refl_props.room_dimensions[2] = room_aabb.axisLength(2);


	refl_props.room_position[0] = room_aabb.centroid()[0];
	refl_props.room_position[1] = room_aabb.centroid()[1];
	refl_props.room_position[2] = room_aabb.centroid()[2];

	for(int i=0; i<6; ++i)
		refl_props.coefficients[i] = 0.8f;
	refl_props.coefficients[5] = 0.2f;
	refl_props.coefficients[2] = 0.3f;
	refl_props.gain = 0.7f;
	resonance->SetReflectionProperties(refl_props);
}


void AudioEngine::seekToStartAndUnpauseAudio(AudioSource& source)
{
	if(!initialised)
		return;

	Lock lock(mutex);

	source.cur_read_i = 0;
	source.paused = false;
	//source.stream_reached_EOF = false;
	source.EOF_marker_position = std::numeric_limits<int64>::max();
	source.buffer.clear();

	if(source.resonance_handle == INVALID_RESONANCE_HANDLE)
	{
		createResonanceObAndResamplerForSource(source);
	}

	AudioSourceRef ref(&source);

	// If there is a streamer for this source, then restart the stream.
	// TODO: handle the case where the stream is streaming for other sources as well. (make new streamer?)
	for(auto it = stream_to_source_map.begin(); it != stream_to_source_map.end(); ++it)
	{
		const std::set<AudioSourceRef>& source_set = it->second;
		if(source_set.count(ref) != 0)
			it->first->seekToBeginningOfFile();
	}
	
	if(active_audio_sources.count(ref) == 0)
		active_audio_sources.insert(ref);
}


void AudioEngine::setMasterVolume(float volume)
{
	if(!initialised)
		return;

	resonance->SetMasterVolume(volume);
}


void AudioEngine::shutdown()
{
	thread_manager.killThreadsBlocking();
	
#if USE_MINIAUDIO
	if(device)
	{
		ma_device_uninit(device);
		device = NULL;
	}
#else
	if(audio)
	{
		if(audio->isStreamOpen())
		{
			if(audio->isStreamRunning())
				audio->stopStream();

			audio->closeStream();
		}
	}
#endif

	delete resonance;
	resonance = NULL;

#if USE_MINIAUDIO
#else
	delete audio;
#endif
	audio = NULL;
}


void AudioEngine::createResonanceObAndResamplerForSource(AudioSource& source_)
{
	if(!initialised)
		return;

	AudioSource* source = &source_;

	if(source->spatial_type == AudioSource::SourceSpatialType_Spatial)
	{
		source->resonance_handle = resonance->CreateSoundObjectSource(vraudio::RenderingMode::kBinauralHighQuality);

		if(source->pos.isFinite()) // Avoid crash in Resonance with NaN or Inf position coords.
		{
			resonance->SetSourcePosition(source->resonance_handle, source->pos[0], source->pos[1], source->pos[2]);
			resonance->SetSourceVolume(source->resonance_handle, source->volume * source->getMuteVolumeFactor());
		}
	}
	else if(source->spatial_type == AudioSource::SourceSpatialType_NonSpatial)
	{
		source->resonance_handle = resonance->CreateStereoSource(/*num channels=*/2);
		resonance->SetSourceVolume(source->resonance_handle, source->volume * source->getMuteVolumeFactor());
	}
	else
	{
		assert(0);
	}

	source->resampler.init(/*src rate=*/source->sampling_rate, this->sample_rate);
}


void AudioEngine::addSource(AudioSourceRef source)
{
	if(!initialised)
		return;

	if(source->sampling_rate < 8000 || source->sampling_rate > 48000)
		throw glare::Exception("Unsupported sampling rate for audio source: " + toString(source->sampling_rate));

	if(!source->paused)
	{
		createResonanceObAndResamplerForSource(*source);
	}

	Lock lock(mutex);
	audio_sources.insert(source);

	if(!source->paused)
		active_audio_sources.insert(source);
}


void AudioEngine::removeSource(AudioSourceRef source)
{
	ZoneScoped; // Tracy profiler

	if(!initialised)
		return;

	if(source->resonance_handle != INVALID_RESONANCE_HANDLE)
		resonance->DestroySource(source->resonance_handle);

	{
		Lock lock(mutex);
		audio_sources.erase(source);
		active_audio_sources.erase(source);

		// Remove audio source from sources_playing_streams (MP3AudioStreamer -> set<AudioSourceRef>) map.
		Reference<MP3AudioStreamer> streamer_to_remove;

		for(auto it = stream_to_source_map.begin(); it != stream_to_source_map.end(); ++it)
		{
			std::set<AudioSourceRef>& source_set = it->second;

			source_set.erase(source); // Remove from set if present in set.

			if(source_set.empty())
				streamer_to_remove = it->first; // No audio source is playing this stream now, so we can free the streamer.
		}

		// We removed the last audio source playing a stream, so remove the streamer.
		if(streamer_to_remove)
		{
			// Remove from unpaused_streams map if present
			for(auto it = unpaused_streams.begin(); it != unpaused_streams.end(); ++it)
				if(it->second == streamer_to_remove)
				{
					unpaused_streams.erase(it);
					break;
				}

			// Remove from stream_to_source_map map
			stream_to_source_map.erase(streamer_to_remove);
		}
	} // End lock scope
}


void AudioEngine::sourcePositionUpdated(AudioSource& source)
{
	if(!initialised)
		return;

	if(!source.pos.isFinite())
		return; // Avoid crash in Resonance with NaN or Inf position coords.

	if(source.resonance_handle != INVALID_RESONANCE_HANDLE)
		resonance->SetSourcePosition(source.resonance_handle, source.pos[0], source.pos[1], source.pos[2]);
}


void AudioEngine::sourceVolumeUpdated(AudioSource& source)
{
	if(!initialised)
		return;
	// conPrint("Setting volume to " + doubleToStringNSigFigs(source.volume, 4));
	if(source.resonance_handle != INVALID_RESONANCE_HANDLE)
		resonance->SetSourceVolume(source.resonance_handle, source.volume * source.getMuteVolumeFactor());
}


void AudioEngine::sourceNumOcclusionsUpdated(AudioSource& source)
{
	if(!initialised)
		return;
	if(source.resonance_handle != INVALID_RESONANCE_HANDLE)
		resonance->SetSoundObjectOcclusionIntensity(source.resonance_handle, source.num_occlusions);
}


void AudioEngine::setHeadTransform(const Vec4f& head_pos, const Quatf& head_rot)
{
	if(!initialised)
		return;

	//Lock lock(mutex);
	
	if(head_pos.isFinite() && head_rot.v.isFinite()) // Avoid crash in Resonance with NaN or Inf position coords.
	{
		resonance->SetHeadPosition(head_pos[0], head_pos[1], head_pos[2]);
		resonance->SetHeadRotation(head_rot.v[0], head_rot.v[1], head_rot.v[2], head_rot.v[3]);
	}
}


SoundFileRef AudioEngine::loadSoundFile(const std::string& sound_file_path)
{
	return AudioFileReader::readAudioFile(sound_file_path);
}


void AudioEngine::insertSoundFile(const std::string& sound_file_path, SoundFileRef sound)
{
	sound_files.insert(std::make_pair(sound_file_path, sound));
}


SoundFileRef AudioEngine::getOrLoadSoundFile(const std::string& sound_file_path)
{
	auto res = sound_files.find(sound_file_path);
	if(res == sound_files.end())
	{
		// Load the sound
		SoundFileRef sound = loadSoundFile(sound_file_path);
		sound_files.insert(std::make_pair(sound_file_path, sound));
		return sound;
	}
	else
	{
		return res->second;
	}
}


void AudioEngine::playOneShotSound(const std::string& sound_file_path, const Vec4f& pos)
{
	if(!initialised)
		return;

	if(!pos.isFinite()) // Avoid crash in Resonance with NaN or Inf position coords.
		return;

	ZoneScopedN("playOneShotSound"); // Tracy profiler

	try
	{
		SoundFileRef sound = getOrLoadSoundFile(sound_file_path);

		// Make a new audio source
		AudioSourceRef source = new AudioSource();
		source->type = AudioSource::SourceType_NonStreaming;
		source->sampling_rate = sound->sample_rate;
		source->shared_buffer = sound->buf;
		source->paused = false;
		source->looping = false;
		source->remove_on_finish = true;
		source->pos = pos;

		addSource(source);
	}
	catch(glare::Exception& e)
	{
		conPrint("Warning: Error while trying to play sound: " + e.what());
	}
}


//AudioSourceRef AudioEngine::addSourceFromSoundFile(const std::string& sound_file_path)
//{
//	SoundFileRef sound;
//	auto res = sound_files.find(sound_file_path);
//	if(res == sound_files.end())
//	{
//		// Load the sound
//		sound = loadSoundFile(sound_file_path);
//		sound_files.insert(std::make_pair(sound_file_path, sound));
//	}
//	else
//		sound = res->second;
//
//	// Make a new audio source
//	AudioSourceRef source = new AudioSource();
//	source->type = AudioSource::SourceType_OneShot;
//	source->remove_on_finish = false;
//	source->shared_buffer = sound->buf;
//	source->cur_read_i = source->buffer.size(); // Position read_i at end of buffer so doesn't play yet.
//
//	addSource(source);
//
//	return source;
//}


AudioSourceRef AudioEngine::addSourceFromStreamingSoundFile(AddSourceFromStreamingSoundFileParams& params, const Vec4f& pos)
{
	ZoneScoped; // Tracy profiler

	Lock lock(mutex);

	// Make a new audio source
	AudioSourceRef source = new AudioSource();
	source->type = AudioSource::SourceType_Streaming;
	source->remove_on_finish = false;
	source->pos = pos;
	source->volume = params.source_volume;
	source->looping = params.looping;
	source->paused = params.paused;

	// We want to use a new MP3AudioStreamer if either
	// a) The source is paused, since it may be unpaused by a script at any time independently of other sources, so shouldn't use the same stream as other sources
	// b) No MP3AudioStreamer for the given sound file path exists yet.

	auto res = unpaused_streams.find(params.sound_file_path);
	if((res == unpaused_streams.end()) || params.paused)
	{
		// Create a new MP3AudioStreamer from the given data source
		Reference<MP3AudioStreamer> streamer;
		if(params.sound_data_source)
			streamer = new MP3AudioStreamer(params.sound_data_source);
		else
			streamer = new MP3AudioStreamer(params.mem_mapped_sound_file);

		if(!params.paused) // if not paused, we can use the same stream as other sources.
		{
			streamer->seekToApproxTimeWrapped(params.global_time);

			unpaused_streams.insert(std::make_pair(params.sound_file_path, streamer));
		}

		stream_to_source_map[streamer].insert(source); // Add this audio source as a user of this stream.
	}
	else
	{
		// Streamer for this mp3 file already exists.
		Reference<MP3AudioStreamer> streamer = res->second;

		// If there is another source playing this stream, copy the other source's buffer in order to synchronise the audio sources in the audio stream.
		auto first_source_it = stream_to_source_map[streamer].begin();
		if(first_source_it != stream_to_source_map[streamer].end())
		{
			AudioSourceRef first_source = *first_source_it;

			source->buffer = first_source->buffer;
		}

		stream_to_source_map[streamer].insert(source); // Add this audio source as a user of this stream.
	}

	addSource(source);

	return source;
}


bool AudioEngine::needNewStreamerForPath(const std::string& sound_file_path, bool new_source_paused)
{
	auto res = unpaused_streams.find(sound_file_path);
	return (res == unpaused_streams.end()) || new_source_paused;
}


} // end namespace glare


#if BUILD_TESTS


#include "MyThread.h"
#include "ConPrint.h"
#include "PlatformUtils.h"
#include "Timer.h"
#include "../utils/TestUtils.h"


void glare::AudioEngine::test()
{
	try
	{
		AudioEngine engine;
		engine.init();

		PlatformUtils::Sleep(1000); // We mute the first second or so of output from Resonance due to a Resonance bug.

		//------------------------------- Test playOneShotSound(), loads sound completely as a non-streaming source. ------------------------------------
		// Play a single non-looping WAV sound, check it gets removed properly at the end
		{
			conPrint("playOneShotSound 1_second_chirp.wav");
			engine.playOneShotSound(TestUtils::getTestReposDir() + "/testfiles/WAVs/1_second_chirp.wav", Vec4f(1,0,0,1));

			testAssert(engine.audio_sources.size() == 1);
			testAssert(engine.active_audio_sources.size() == 1);

			for(int i=0; i<100; ++i)
				PlatformUtils::Sleep(2000 / 100);

			testAssert(engine.audio_sources.empty());
			testAssert(engine.active_audio_sources.empty());

			testAssert(engine.unpaused_streams.empty());
			testAssert(engine.stream_to_source_map.empty());
		}
		

		// Play a single non-looping mp3 sound, check it gets removed properly at the end
		{
			conPrint("playOneShotSound 1_second_chirp.mp3");
			engine.playOneShotSound(TestUtils::getTestReposDir() + "/testfiles/mp3s/1_second_chirp.mp3", Vec4f(1,0,0,1));

			testAssert(engine.audio_sources.size() == 1);
			testAssert(engine.active_audio_sources.size() == 1);

			for(int i=0; i<100; ++i)
				PlatformUtils::Sleep(2000 / 100);

			testAssert(engine.audio_sources.empty());
			testAssert(engine.active_audio_sources.empty());
			testAssert(engine.unpaused_streams.empty());
			testAssert(engine.stream_to_source_map.empty());
		}

		//------------------------------------ Test looping WAV -----------------------
		{
			conPrint("Test looping WAV");
			
			/*glare::AudioEngine::AddSourceFromStreamingSoundFileParams params;
			params.sound_file_path = TestUtils::getTestReposDir() + "/testfiles/WAVs/1_second_chirp.wav";
			params.sound_data_source = nullptr;
			params.source_volume = 1.f;
			params.global_time = 0.0;
			params.looping = true;
			AudioSourceRef source1 = new AudioSource();
			source1->
			source1 = engine.addSource(source1);*/

			SoundFileRef sound = engine.getOrLoadSoundFile(TestUtils::getTestReposDir() + "/testfiles/WAVs/1_second_chirp.wav");

			// Make a new audio source
			AudioSourceRef source = new AudioSource();
			source->type = AudioSource::SourceType_NonStreaming;
			source->sampling_rate = sound->sample_rate;
			source->shared_buffer = sound->buf;
			source->paused = false;
			source->looping = true;
			source->remove_on_finish = false;
			//source->pos = pos;

			engine.addSource(source);

			PlatformUtils::Sleep(2000);

			engine.removeSource(source);

			testAssert(engine.audio_sources.empty());
			testAssert(engine.active_audio_sources.empty());
			testAssert(engine.unpaused_streams.empty());
			testAssert(engine.stream_to_source_map.empty());
		}

		//------------------------------------ Test looping MP3 -----------------------
		{
			conPrint("Test looping MP3");
			
			/*glare::AudioEngine::AddSourceFromStreamingSoundFileParams params;
			params.sound_file_path = TestUtils::getTestReposDir() + "/testfiles/WAVs/1_second_chirp.wav";
			params.sound_data_source = nullptr;
			params.source_volume = 1.f;
			params.global_time = 0.0;
			params.looping = true;
			AudioSourceRef source1 = new AudioSource();
			source1->
			source1 = engine.addSource(source1);*/

			SoundFileRef sound = engine.getOrLoadSoundFile(TestUtils::getTestReposDir() + "/testfiles/mp3s/1_second_chirp.mp3");

			// Make a new audio source
			AudioSourceRef source = new AudioSource();
			source->type = AudioSource::SourceType_NonStreaming;
			source->sampling_rate = sound->sample_rate;
			source->shared_buffer = sound->buf;
			source->paused = false;
			source->looping = true;
			source->remove_on_finish = false;
			//source->pos = pos;

			engine.addSource(source);

			PlatformUtils::Sleep(2000);

			engine.removeSource(source);

			testAssert(engine.audio_sources.empty());
			testAssert(engine.active_audio_sources.empty());
			testAssert(engine.unpaused_streams.empty());
			testAssert(engine.stream_to_source_map.empty());
		}

		//------------------------------------ Test addSourceFromStreamingSoundFile() -----------------------
		{
			conPrint("Test addSourceFromStreamingSoundFile() with no looping");
			
			glare::AudioEngine::AddSourceFromStreamingSoundFileParams params;
			params.sound_file_path = TestUtils::getTestReposDir() + "/testfiles/mp3s/1_second_chirp.mp3";
			params.looping = false;

			AudioSourceRef source = engine.addSourceFromStreamingSoundFile(params, Vec4f(1,0,0,1));

			PlatformUtils::Sleep(2000);

			// Source should have finished playing by now.
			testAssert(!source->isPlaying());
			testAssert(engine.active_audio_sources.empty()); // Should have been removed from active_audio_sources
			engine.removeSource(source);

			testAssert(engine.audio_sources.empty());
			testAssert(engine.active_audio_sources.empty());
			testAssert(engine.unpaused_streams.empty());
			testAssert(engine.stream_to_source_map.empty());
		}
		return;
		// Test with looping
		{
			conPrint("Test addSourceFromStreamingSoundFile() with looping");
			
			glare::AudioEngine::AddSourceFromStreamingSoundFileParams params;
			params.sound_file_path = TestUtils::getTestReposDir() + "/testfiles/mp3s/1_second_chirp.mp3";
			params.looping = true;

			AudioSourceRef source = engine.addSourceFromStreamingSoundFile(params, Vec4f(1,0,0,1));

			PlatformUtils::Sleep(2000);

			testAssert(source->isPlaying());
			engine.removeSource(source);

			testAssert(engine.audio_sources.empty());
			testAssert(engine.active_audio_sources.empty());
			testAssert(engine.unpaused_streams.empty());
			testAssert(engine.stream_to_source_map.empty());
		}

		//------------------------------------ Test seekToStartAndUnpauseAudio() -----------------------
		// Test inserting a paused, non-looping file, then calling seekToStartAndUnpauseAudio on it
		{
			conPrint("Test seekToStartAndUnpauseAudio() with no looping");
			
			glare::AudioEngine::AddSourceFromStreamingSoundFileParams params;
			params.sound_file_path = TestUtils::getTestReposDir() + "/testfiles/mp3s/1_second_chirp.mp3";
			params.paused = true;
			params.looping = false;

			AudioSourceRef source = engine.addSourceFromStreamingSoundFile(params, Vec4f(1,0,0,1));

			engine.seekToStartAndUnpauseAudio(*source);

			PlatformUtils::Sleep(2000);

			// Source should have finished playing by now.
			testAssert(!source->isPlaying());
			testAssert(engine.active_audio_sources.empty()); // Should have been removed from active_audio_sources

			engine.removeSource(source);

			testAssert(engine.audio_sources.empty());
			testAssert(engine.active_audio_sources.empty());
			testAssert(engine.unpaused_streams.empty());
			testAssert(engine.stream_to_source_map.empty());
		}

		// Test inserting a paused, non-looping file
		{
			conPrint("Test seekToStartAndUnpauseAudio() with looping");
			
			glare::AudioEngine::AddSourceFromStreamingSoundFileParams params;
			params.sound_file_path = TestUtils::getTestReposDir() + "/testfiles/mp3s/1_second_chirp.mp3";
			params.paused = true;
			params.looping = true;

			AudioSourceRef source = engine.addSourceFromStreamingSoundFile(params, Vec4f(1,0,0,1));

			engine.seekToStartAndUnpauseAudio(*source);

			PlatformUtils::Sleep(2000);

			testAssert(source->isPlaying());
			engine.removeSource(source);

			testAssert(engine.audio_sources.empty());
			testAssert(engine.active_audio_sources.empty());
			testAssert(engine.unpaused_streams.empty());
			testAssert(engine.stream_to_source_map.empty());
		}

		//return;




		
		// Make a new audio source
		//AudioSourceRef source = new AudioSource();
		//source->type = AudioSource::SourceType_Streaming;
		//source->remove_on_finish = false;
		//source->mp3_streamer = new glare::MP3AudioStreamer();
		//source->mp3_streamer->init(TestUtils::getTestReposDir() + "/testfiles/mp3s/sample-3s.mp3"); //  "D:\\files\\02___Sara_mp3_14061996302930432458.mp3");

		//engine.addSource(source);

		//AudioSourceRef source1 = engine.addSourceFromStreamingSoundFile("D:\\files\\Good_Gas___Live_A_Lil_ft__MadeinTYO__UnoTheActivist___FKi_1st__Lyrics__mp3_3425190382177260630.mp3", Vec4f(1, 0, 0, 1), /*global time=*/0.0);
		AudioSourceRef source1, source2;
		{
			glare::AudioEngine::AddSourceFromStreamingSoundFileParams params;
			params.sound_file_path = TestUtils::getTestReposDir() + "/testfiles/mp3s/sample-3s.mp3";
			params.sound_data_source = nullptr;
			params.source_volume = 1.f;
			params.global_time = 0.0;
			source1 = engine.addSourceFromStreamingSoundFile(params, Vec4f(1, 0, 0, 1));
		}
		PlatformUtils::Sleep(1);
		{
			glare::AudioEngine::AddSourceFromStreamingSoundFileParams params;
			params.sound_file_path = TestUtils::getTestReposDir() + "/testfiles/mp3s/sample-3s.mp3";
			params.sound_data_source = nullptr;
			params.source_volume = 1.f;
			params.global_time = 0.0;
			source2 = engine.addSourceFromStreamingSoundFile(params, Vec4f(1, 0, 0, 1));
		}

		testAssert(engine.unpaused_streams.size() == 1);
		testAssert(engine.stream_to_source_map.size() == 1);
		testAssert(engine.stream_to_source_map.begin()->second.count(source1) == 1);
		testAssert(engine.stream_to_source_map.begin()->second.count(source2) == 1);

		for(int i=0; i<3000; ++i)
		{
			PlatformUtils::Sleep(1);
		}

		engine.removeSource(source1);
		engine.removeSource(source2);

		testAssert(engine.unpaused_streams.empty());
		testAssert(engine.stream_to_source_map.empty());
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}


	try
	{
		AudioEngine engine;
		SoundFileRef sound;
		/*sound = engine.loadWavFile("D:\\audio\\sound_effects\\171697__nenadsimic__menu-selection-click.wav"); // 24-bit
		testAssert(sound->num_channels == 2);
		testAssert(sound->sample_rate == 44100);
		testAssert(sound->minVal() >= -1.f && sound->maxVal() <= 1.f);

		sound = engine.loadWavFile("D:\\audio\\sound_effects\\366102__original-sound__confirmation-upward.wav"); // 16-bit
		testAssert(sound->num_channels == 2);
		testAssert(sound->sample_rate == 44100);
		testAssert(sound->minVal() >= -1.f && sound->maxVal() <= 1.f);*/
		
		//sound = engine.loadSoundFile("D:\\audio\\sound_effects\\select_mono.wav"); // 16-bit mono
		//testAssert(sound->num_channels == 1);
		//testAssert(sound->sample_rate == 44100);
		//testAssert(sound->minVal() >= -1.f && sound->maxVal() <= 1.f);
		//
		//sound = engine.loadSoundFile("D:\\audio\\sound_effects\\462089__newagesoup__ethereal-woosh.wav"); // 16-bit stero
		//testAssert(sound->num_channels == 1); // mixed down to 1 channel
		//testAssert(sound->sample_rate == 44100);
		//testAssert(sound->minVal() >= -1.f && sound->maxVal() <= 1.f);
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}
}


#endif // BUILD_TESTS

