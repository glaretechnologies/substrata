/*=====================================================================
AudioEngine.cpp
---------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "AudioEngine.h"


#include "AudioFileReader.h"
#include "MP3AudioFileReader.h"
#include "StreamerThread.h"
#include "../rtaudio/RtAudio.h"
#include <resonance_audio/api/resonance_audio_api.h>
#include <utils/MessageableThread.h>
#include <utils/CircularBuffer.h>
#include <utils/ConPrint.h>
#include <utils/StringUtils.h>
#include <utils/PlatformUtils.h>
#include <utils/MemMappedFile.h>
#include <utils/FileInStream.h>


namespace glare
{


AudioSource::AudioSource()
:	cur_read_i(0), type(SourceType_Looping), spatial_type(SourceSpatialType_Spatial), remove_on_finish(true), volume(1.f), mute_volume_factor(1.f), mute_change_start_time(-2), mute_change_end_time(-1), mute_vol_fac_start(1.f),
	mute_vol_fac_end(1.f), pos(0,0,0,1), num_occlusions(0), userdata_1(0), doppler_factor(1)
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


// Gets buffered audio data from audio sources, copies it to resonance buffers.
// Then gets mixed data from resonance, puts on queue to rtAudioCallback.
class ResonanceThread : public MessageableThread
{
public:
	ResonanceThread() : buffers_processed(0) {}

	virtual void doRun() override
	{
		PlatformUtils::setCurrentThreadNameIfTestsEnabled("ResonanceThread");

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
					for(auto it = engine->audio_sources.begin(); it != engine->audio_sources.end(); )
					{
						AudioSource* source = it->ptr();
						bool remove_source = false;

						if(source->type == AudioSource::SourceType_Looping)
						{
							if(source->shared_buffer.nonNull()) // If we are reading from shared_buffer:
							{
								if(source->cur_read_i + frames_per_buffer <= source->shared_buffer->buffer.size()) // If we can just copy the current buffer range directly from source->buffer:
								{
									const float* bufptr = &source->shared_buffer->buffer[source->cur_read_i];
									resonance->SetPlanarBuffer(source->resonance_handle, &bufptr, /*num channels=*/1, frames_per_buffer);

									source->cur_read_i += frames_per_buffer;
									if(source->cur_read_i == source->shared_buffer->buffer.size()) // If reach end of buf:
										source->cur_read_i = 0; // wrap
								}
								else
								{
									// The data range we want to read from the shared buffer wraps.  So copy data to a temporary contiguous buffer first.
									size_t cur_i = source->cur_read_i;

									for(size_t i=0; i<frames_per_buffer; ++i)
									{
										buf[i] = source->shared_buffer->buffer[cur_i++];
										if(cur_i == source->shared_buffer->buffer.size()) // If reach end of buf:
											cur_i = 0; // TODO: optimise
									}

									source->cur_read_i = cur_i;

									const float* bufptr = buf;
									resonance->SetPlanarBuffer(source->resonance_handle, &bufptr, /*num channels=*/1, frames_per_buffer);
								}
							}
							else // Else if we are reading from a circular buffer:
							{
								assert(0); // SourceType_Looping sources should only read from shared buffers.

								// Just pass zeroes to resonance so as to not blow up the listener's ears.
								for(size_t i=0; i<frames_per_buffer; ++i)
									buf[i] = 0;
								const float* bufptr = buf;
								resonance->SetPlanarBuffer(source->resonance_handle, &bufptr, /*num channels=*/1, frames_per_buffer);
							}
						}
						if(source->type == AudioSource::SourceType_OneShot)
						{
							if(source->shared_buffer.nonNull()) // If we are reading from shared_buffer:
							{
								if(source->cur_read_i + frames_per_buffer <= source->shared_buffer->buffer.size()) // If we can just copy the current buffer range directly from source->buffer:
								{
									const float* bufptr = &source->shared_buffer->buffer[source->cur_read_i];
									resonance->SetPlanarBuffer(source->resonance_handle, &bufptr, /*num channels=*/1, frames_per_buffer);

									source->cur_read_i += frames_per_buffer;
								}
								else
								{
									// The data range we want to read from the shared buffer exceeds the shared buffer length.  Just read as much data as we can and then pad with zeroes.
									// Copy data to a temporary contiguous buffer
									size_t cur_i = source->cur_read_i;

									for(size_t i=0; i<frames_per_buffer; ++i)
									{
										if(cur_i < source->shared_buffer->buffer.size())
											buf[i] = source->shared_buffer->buffer[cur_i++];
										else
											buf[i] = 0;
									}

									source->cur_read_i = cur_i;

									remove_source = source->remove_on_finish && (cur_i >= source->shared_buffer->buffer.size()); // Remove the source if we reached the end of the buffer.

									const float* bufptr = buf;
									resonance->SetPlanarBuffer(source->resonance_handle, &bufptr, /*num channels=*/1, frames_per_buffer);
								}
							}
							else // Else if we are reading from a circular buffer:
							{
								assert(0); // SourceType_OneShot sources should only read from shared buffers.

								// Just pass zeroes to resonance so as to not blow up the listener's ears.
								for(size_t i=0; i<frames_per_buffer; ++i)
									buf[i] = 0;
								const float* bufptr = buf;
								resonance->SetPlanarBuffer(source->resonance_handle, &bufptr, /*num channels=*/1, frames_per_buffer);
							}
						}
						else if(source->type == AudioSource::SourceType_Streaming)
						{
							if(!source->mix_sources.empty())
							{
								// Mix together the audio sources, applying pitch shift factor and volume factor.
								for(size_t i=0; i<frames_per_buffer; ++i)
									buf[i] = 0.f;

								for(size_t z=0; z<source->mix_sources.size(); ++z)
								{
									MixSource& mix_source = source->mix_sources[z];
									const size_t src_buffer_size  = mix_source.soundfile->buf->buffer.size();
									const float* const src_buffer = mix_source.soundfile->buf->buffer.data();

									for(size_t i=0; i<frames_per_buffer; ++i)
									{
										mix_source.sound_file_i += mix_source.source_delta; // Advance floating-point read index (index into source buffer)

										const size_t index   = (size_t)mix_source.sound_file_i % src_buffer_size;
										const size_t index_1 = (index + 1)                     % src_buffer_size;
										const float frac = (float)(mix_source.sound_file_i - (size_t)mix_source.sound_file_i);

										const float sample = src_buffer[index] * (1 - frac) + src_buffer[index_1] * frac;
										buf[i] += sample * mix_source.mix_factor;
									}
								}

								const float* bufptr = buf;
								resonance->SetPlanarBuffer(source->resonance_handle, &bufptr, /*num channels=*/1, frames_per_buffer);
							}
							else
							{
								if(source->buffer.size() >= frames_per_buffer) // If there is sufficient data in the circular buffer:
								{
									if(source->buffer.getFirstSegmentSize() >= frames_per_buffer) // See if all the data we need is contiguous in the circular buffer (doesn't wrap)
									{
										// Directly copy data from the circular buffer to the resonance buffer.
										const float* bufptr = &(*source->buffer.beginIt());

										resonance->SetPlanarBuffer(source->resonance_handle, &bufptr, /*num channels=*/1, frames_per_buffer);

										source->buffer.popFrontNItems(frames_per_buffer);
									}
									else
									{
										// Copy from the circular buffer to a temporary contiguous buffer (temp_buf).
										source->buffer.popFrontNItems(temp_buf.data(), frames_per_buffer);

										const float* bufptr = temp_buf.data();
										resonance->SetPlanarBuffer(source->resonance_handle, &bufptr, /*num channels=*/1, frames_per_buffer);
									}
								}
								else
								{
									//conPrint("Ran out of data for streaming audio src!");

									for(size_t i=0; i<frames_per_buffer; ++i)
										temp_buf[i] = 0.f;

									const float* bufptr = temp_buf.data();
									resonance->SetPlanarBuffer(source->resonance_handle, &bufptr, /*num channels=*/1, frames_per_buffer);
								}
							}
						}

						if(remove_source)
						{
							resonance->DestroySource(source->resonance_handle);
							it = engine->audio_sources.erase(it); // Remove currently iterated to source, advance iterator.
						}
						else
							++it;
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

	std::vector<float> temp_buf;
};


void AudioEngine::init()
{
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
	unsigned int desired_sample_rate = 48000;// info.preferredSampleRate;

	RtAudio::StreamParameters parameters;
	parameters.deviceId = audio->getDefaultOutputDevice();
	parameters.nChannels = 2;
	parameters.firstChannel = 0;
	unsigned int buffer_frames = 256; // 256 sample frames. NOTE: might be changed by openStream() below.

	// conPrint("Using sample rate of " + toString(use_sample_rate) + " hz");

	callback_data.resonance = NULL;
	callback_data.engine = this;

	RtAudioErrorType rtaudio_res = audio->openStream(&parameters, /*input parameters=*/NULL, RTAUDIO_FLOAT32, desired_sample_rate, &buffer_frames, rtAudioCallback, /*userdata=*/&callback_data);
	if(rtaudio_res != RTAUDIO_NO_ERROR)
		throw glare::Exception("Error opening audio stream: code: " + toString((int)rtaudio_res));

	this->sample_rate = audio->getStreamSampleRate(); // Get actual sample rate used.

	conPrint("Using sample rate of " + toString(sample_rate) + " hz");
	

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

	rtaudio_res = audio->startStream();
	if(rtaudio_res != RTAUDIO_NO_ERROR)
		throw glare::Exception("Error opening audio stream: code: " + toString((int)rtaudio_res));

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


void AudioEngine::setMasterVolume(float volume)
{
	if(!initialised)
		return;

	resonance->SetMasterVolume(volume);
}


void AudioEngine::shutdown()
{
	thread_manager.killThreadsBlocking();
	
	if(audio)
	{
		if(audio->isStreamOpen())
		{
			if(audio->isStreamRunning())
				audio->stopStream();

			audio->closeStream();
		}
	}

	delete resonance;
	resonance = NULL;

	delete audio;
	audio = NULL;
}


void AudioEngine::addSource(AudioSourceRef source)
{
	if(!initialised)
		return;

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

	Lock lock(mutex);
	audio_sources.insert(source);
}


void AudioEngine::removeSource(AudioSourceRef source)
{
	if(!initialised)
		return;

	resonance->DestroySource(source->resonance_handle);

	{
		Lock lock(mutex);
		audio_sources.erase(source);

		// Remove audio source from sources_playing_streams (MP3AudioStreamer -> set<AudioSourceRef>) map.
		Reference<MP3AudioStreamer> streamer_to_remove;

		for(auto it = sources_playing_streams.begin(); it != sources_playing_streams.end(); ++it)
		{
			std::set<AudioSourceRef>& source_set = it->second;

			source_set.erase(source); // Remove from set if present in set.

			if(source_set.empty())
				streamer_to_remove = it->first; // No audio source is playing this stream now, so we can free the streamer.
		}

		// We removed the last audio source playing a stream, so remove the streamer.
		if(streamer_to_remove.nonNull())
		{
			// Remove from streams map
			for(auto it = streams.begin(); it != streams.end(); ++it)
				if(it->second == streamer_to_remove)
				{
					streams.erase(it);
					break;
				}

			// Remove from sources_playing_streams map
			sources_playing_streams.erase(streamer_to_remove);
		}
	} // End lock scope
}


void AudioEngine::sourcePositionUpdated(AudioSource& source)
{
	if(!initialised)
		return;

	if(!source.pos.isFinite())
		return; // Avoid crash in Resonance with NaN or Inf position coords.

	resonance->SetSourcePosition(source.resonance_handle, source.pos[0], source.pos[1], source.pos[2]);
}


void AudioEngine::sourceVolumeUpdated(AudioSource& source)
{
	if(!initialised)
		return;

	// conPrint("Setting volume to " + doubleToStringNSigFigs(source.volume, 4));
	resonance->SetSourceVolume(source.resonance_handle, source.volume * source.getMuteVolumeFactor());
}


void AudioEngine::sourceNumOcclusionsUpdated(AudioSource& source)
{
	if(!initialised)
		return;

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

	try
	{
		SoundFileRef sound = getOrLoadSoundFile(sound_file_path);

		// Make a new audio source
		AudioSourceRef source = new AudioSource();
		source->type = AudioSource::SourceType_OneShot;
		source->shared_buffer = sound->buf;
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


AudioSourceRef AudioEngine::addSourceFromStreamingSoundFile(const std::string& sound_file_path, const Vec4f& pos, double global_time)
{
	Lock lock(mutex);

	// Make a new audio source
	AudioSourceRef source = new AudioSource();
	source->type = AudioSource::SourceType_Streaming;
	source->remove_on_finish = false;
	source->pos = pos;

	auto res = streams.find(sound_file_path);
	if(res == streams.end())
	{
		// Load the sound
		Reference<MP3AudioStreamer> streamer = new MP3AudioStreamer(sound_file_path);

		streamer->seekToApproxTimeWrapped(global_time);

		streams.insert(std::make_pair(sound_file_path, streamer));

		sources_playing_streams[streamer].insert(source); // Add this audio source as a user of this stream.
	}
	else
	{
		// Streamer for this mp3 file already exists.
		Reference<MP3AudioStreamer> streamer = res->second;

		// If there is another source playing this stream, copy the other source's buffer in order to synchronise the audio sources in the audio stream.
		auto first_source_it = sources_playing_streams[streamer].begin();
		if(first_source_it != sources_playing_streams[streamer].end())
		{
			AudioSourceRef first_source = *first_source_it;

			source->buffer = first_source->buffer;
		}

		sources_playing_streams[streamer].insert(source); // Add this audio source as a user of this stream.
	}

	addSource(source);

	return source;
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
		
		// Make a new audio source
		//AudioSourceRef source = new AudioSource();
		//source->type = AudioSource::SourceType_Streaming;
		//source->remove_on_finish = false;
		//source->mp3_streamer = new glare::MP3AudioStreamer();
		//source->mp3_streamer->init(TestUtils::getTestReposDir() + "/testfiles/mp3s/sample-3s.mp3"); //  "D:\\files\\02___Sara_mp3_14061996302930432458.mp3");

		//engine.addSource(source);

		//AudioSourceRef source1 = engine.addSourceFromStreamingSoundFile("D:\\files\\Good_Gas___Live_A_Lil_ft__MadeinTYO__UnoTheActivist___FKi_1st__Lyrics__mp3_3425190382177260630.mp3", Vec4f(1, 0, 0, 1), /*global time=*/0.0);
		AudioSourceRef source1 = engine.addSourceFromStreamingSoundFile(TestUtils::getTestReposDir() + "/testfiles/mp3s/sample-3s.mp3", Vec4f(1, 0, 0, 1), /*global time=*/0.0);
		PlatformUtils::Sleep(1);
		AudioSourceRef source2 = engine.addSourceFromStreamingSoundFile(TestUtils::getTestReposDir() + "/testfiles/mp3s/sample-3s.mp3", Vec4f(1, 1, 0, 1), /*global time=*/0.0);

		testAssert(engine.streams.size() == 1);
		testAssert(engine.sources_playing_streams.size() == 1);
		testAssert(engine.sources_playing_streams.begin()->second.count(source1) == 1);
		testAssert(engine.sources_playing_streams.begin()->second.count(source2) == 1);

		for(int i=0; i<3000; ++i)
		{
			PlatformUtils::Sleep(1);
		}

		engine.removeSource(source1);
		engine.removeSource(source2);

		testAssert(engine.streams.empty());
		testAssert(engine.sources_playing_streams.empty());
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

