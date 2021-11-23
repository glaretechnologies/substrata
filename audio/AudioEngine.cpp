/*=====================================================================
AudioEngine.cpp
---------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "AudioEngine.h"


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

float SoundFile::maxVal() const
{
	float m = -std::numeric_limits<float>::infinity();
	for(float x : buf)
		m = myMax(m, x);
	return m;
}


float SoundFile::minVal() const
{
	float m = std::numeric_limits<float>::infinity();
	for(float x : buf)
		m = myMin(m, x);
	return m;
}


AudioEngine::AudioEngine()
:	audio(NULL),
	resonance(NULL)
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


// Gets mixed data from resonance, puts on queue to rtAudioCallback.
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
									// Copy data to a temporary contiguous buffer
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
							else
							{
								if(source->cur_read_i + frames_per_buffer <= source->buffer.size()) // If we can just copy the current buffer range directly from source->buffer:
								{
									const float* bufptr = &source->buffer[0]/*source->buffer.data()*/ + source->cur_read_i; // NOTE: bit of a hack here, assuming layout of circular buffer (e.g. no wrapping)
									resonance->SetPlanarBuffer(source->resonance_handle, &bufptr, /*num channels=*/1, frames_per_buffer);

									source->cur_read_i += frames_per_buffer;
									if(source->cur_read_i == source->buffer.size()) // If reach end of buf:
										source->cur_read_i = 0; // wrap
								}
								else
								{
									// Copy data to a temporary contiguous buffer
									size_t cur_i = source->cur_read_i;

									for(size_t i=0; i<frames_per_buffer; ++i)
									{
										buf[i] = source->buffer[cur_i++];
										if(cur_i == source->buffer.size()) // If reach end of buf:
											cur_i = 0; // TODO: optimise
									}

									source->cur_read_i = cur_i;

									const float* bufptr = buf;
									resonance->SetPlanarBuffer(source->resonance_handle, &bufptr, /*num channels=*/1, frames_per_buffer);
								}
							}
						}
						if(source->type == AudioSource::SourceType_OneShot)
						{
							if(source->cur_read_i + frames_per_buffer <= source->buffer.size()) // If we can just copy the current buffer range directly from source->buffer:
							{
								const float* bufptr = &source->buffer[0]/*source->buffer.data()*/ + source->cur_read_i; // NOTE: bit of a hack here, assuming layout of circular buffer (e.g. no wrapping)
								resonance->SetPlanarBuffer(source->resonance_handle, &bufptr, /*num channels=*/1, frames_per_buffer);

								source->cur_read_i += frames_per_buffer;
							}
							else
							{
								// Copy data to a temporary contiguous buffer
								size_t cur_i = source->cur_read_i;

								for(size_t i=0; i<frames_per_buffer; ++i)
								{
									if(cur_i < source->buffer.size())
										buf[i] = source->buffer[cur_i++];
									else
										buf[i] = 0;
								}

								source->cur_read_i = cur_i;

								remove_source = source->remove_on_finish && (cur_i >= source->buffer.size()); // Remove the source if we reached the end of the buffer.

								const float* bufptr = buf;
								resonance->SetPlanarBuffer(source->resonance_handle, &bufptr, /*num channels=*/1, frames_per_buffer);
							}
						}
						else if(source->type == AudioSource::SourceType_Streaming)
						{
							if(source->buffer.size() >= frames_per_buffer)
							{
								source->buffer.popFrontNItems(temp_buf.data(), frames_per_buffer);

								const float* bufptr = temp_buf.data();
								resonance->SetPlanarBuffer(source->resonance_handle, &bufptr, /*num channels=*/1, frames_per_buffer);
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
	audio = new RtAudio();
	
	// Determine the number of devices available
	const unsigned int devices = audio->getDeviceCount();
	if(devices == 0)
		throw glare::Exception("No audio devices found");

	// Scan through devices for various capabilities
	RtAudio::DeviceInfo info;
	for ( unsigned int i=0; i<devices; i++ ) {
		info = audio->getDeviceInfo( i );
		if ( info.probed == true ) {
			// Print, for example, the maximum number of output channels for each device

			// conPrint("name = " + info.name);
			// conPrint("maximum output channels = " + toString(info.outputChannels));
			// conPrint("maximum input channels = " + toString(info.inputChannels));
			// conPrint("supported sample rates = " + toString(info.inputChannels));
			// for(auto r : info.sampleRates)
			// 	conPrint(toString(r) + " hz");
			// conPrint("preferredSampleRate = " + toString(info.preferredSampleRate));
		}
	}


	const unsigned int default_output_dev = audio->getDefaultOutputDevice();
	// conPrint("default_output_dev: " + toString(default_output_dev));

	info = audio->getDeviceInfo(default_output_dev);
	if(!info.isDefaultOutput)
		throw glare::Exception("Failed to find output audio device");

	unsigned int use_sample_rate = 44100;// info.preferredSampleRate;

	RtAudio::StreamParameters parameters;
	parameters.deviceId = audio->getDefaultOutputDevice();
	parameters.nChannels = 2;
	parameters.firstChannel = 0;
	unsigned int sample_rate = use_sample_rate;
	unsigned int buffer_frames = 256; // 256 sample frames. NOTE: might be changed by openStream() below.

	// conPrint("Using sample rate of " + toString(use_sample_rate) + " hz");

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


void AudioEngine::setRoomEffectsEnabled(bool enabled)
{
	resonance->EnableRoomEffects(enabled);
}


void AudioEngine::setCurentRoomDimensions(const js::AABBox& room_aabb)
{
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


void AudioEngine::shutdown()
{
	resonance_thread_manager.killThreadsBlocking();
	
	try
	{
		if(audio)
		{
			if(audio->isStreamOpen())
			{
				if(audio->isStreamRunning())
					audio->stopStream();

				audio->closeStream();
			}
		}
	}
	catch(RtAudioError& e)
	{
		conPrint(std::string("AudioEngine::shutdown(): RtAudioError: ") + e.what());
	}

	delete resonance;
	resonance = NULL;

	delete audio;
	audio = NULL;
}


void AudioEngine::addSource(AudioSourceRef source)
{
	source->resonance_handle = resonance->CreateSoundObjectSource(vraudio::RenderingMode::kBinauralHighQuality);

	if(source->pos.isFinite()) // Avoid crash in Resonance with NaN position coords.
	{
		resonance->SetSourcePosition(source->resonance_handle, source->pos[0], source->pos[1], source->pos[2]);
	}

	Lock lock(mutex);
	audio_sources.insert(source);
}


void AudioEngine::removeSource(AudioSourceRef source)
{
	resonance->DestroySource(source->resonance_handle);

	Lock lock(mutex);
	audio_sources.erase(source);
}


void AudioEngine::sourcePositionUpdated(AudioSource& source)
{
	if(!source.pos.isFinite())
		return; // Avoid crash in Resonance with NaN position coords.

	resonance->SetSourcePosition(source.resonance_handle, source.pos[0], source.pos[1], source.pos[2]);
}


void AudioEngine::sourceVolumeUpdated(AudioSource& source)
{
	// conPrint("Setting volume to " + doubleToStringNSigFigs(source.volume, 4));
	resonance->SetSourceVolume(source.resonance_handle, source.volume);
}


void AudioEngine::sourceNumOcclusionsUpdated(AudioSource& source)
{
	resonance->SetSoundObjectOcclusionIntensity(source.resonance_handle, source.num_occlusions);
}


void AudioEngine::setHeadTransform(const Vec4f& head_pos, const Quatf& head_rot)
{
	//Lock lock(mutex);

	resonance->SetHeadPosition(head_pos[0], head_pos[1], head_pos[2]);
	resonance->SetHeadRotation(head_rot.v[0], head_rot.v[1], head_rot.v[2], head_rot.v[3]);
}


inline int32 signExtend24BitValue(int32 x)
{
	if((x & 0x800000) != 0)
	{
		return x | 0xFF000000;
	}
	else
	{
		// Sign bit (left bit) was 0
		return x;
	}
}


// See http://soundfile.sapp.org/doc/WaveFormat/
SoundFileRef AudioEngine::loadWavFile(const std::string& sound_file_path)
{
	try
	{
		SoundFileRef sound = new SoundFile();

		FileInStream file(sound_file_path);

		const uint32 riff_chunk_id = file.readUInt32();
		if(riff_chunk_id != 0x46464952) // big endian: 0x52494646
			throw glare::Exception("invalid header, expected RIFF");

		/*const uint32 riff_chunk_size =*/ file.readUInt32();
		/*const uint32 riff_chunk_format =*/ file.readUInt32();

		const uint32 fmt_chunk_id = file.readUInt32();
		if(fmt_chunk_id != 0x20746d66) // big endian: 0x666d7420
			throw glare::Exception("invalid fmt chunk id, expected fmt");
		/*const uint32 fmt_chunk_size =*/ file.readUInt32();
		const uint16 audio_format = file.readUInt16();
		if(audio_format != 1)
			throw glare::Exception("Unhandled audio format, only 1 (PCM) handled.");
		const uint16 wav_num_channels = file.readUInt16();
		const uint32 sample_rate = file.readUInt32();
		const uint32 byte_rate = file.readUInt32(); // TODO: handle this by resampling to target rate.
		/*const uint16 block_align =*/ file.readUInt16(); // TODO: handle this?
		const uint16 bits_per_sample = file.readUInt16();

		sound->num_channels = 1; // mix down to mono
		sound->sample_rate = sample_rate;

		while(!file.endOfStream())
		{
			// Read chunk
			const uint32 chunk_id = file.readUInt32();
			const uint32 chunk_size = file.readUInt32();

			if(chunk_id == 0x61746164) // "data", big endian: 0x64617461 
			{
				const uint32 bytes_per_sample = bits_per_sample / 8;

				// Avoid divide by zeroes
				if(wav_num_channels == 0)
					throw glare::Exception("Invalid num channels");
				if(bytes_per_sample == 0)
					throw glare::Exception("Invalid bytes per sample");

				// Read actual data, convert to float and resample to target sample rate
				const uint32 num_samples = chunk_size / bytes_per_sample;

				sound->buf.resize(num_samples / wav_num_channels); // TEMP: mix down to mono

				const size_t expected_remaining = bytes_per_sample * num_samples;
				if(file.getReadIndex() + expected_remaining > file.fileSize())
					throw glare::Exception("not enough data in file.");

				if(bytes_per_sample == 2)
				{
					if(wav_num_channels == 1)
					{
						std::vector<int16> temp(num_samples);
						std::memcpy(temp.data(), file.currentReadPtr(), num_samples * bytes_per_sample); // Copy to ensure 16-bit aligned.

						for(uint32 i=0; i<num_samples / wav_num_channels; ++i)
							sound->buf[i] = temp[i] * (1.f / 32768);
					}
					else if(wav_num_channels == 2)
					{
						// TEMP: mix down to mono
						std::vector<int16> temp(num_samples);
						std::memcpy(temp.data(), file.currentReadPtr(), num_samples * bytes_per_sample); // Copy to ensure 16-bit aligned.

						for(uint32 i=0; i<num_samples / wav_num_channels; ++i)
						{
							//const float left  = (int32)temp[i * 2 + 0] + (int32)temp[i * 2 + 1];
							//const float right = temp[i * 2 + 1] * (1.f / 32768.f);
							//sound->buf[i] = (left + right) * 0.5f; // TODO: optimise

							// val = (left/32768.f + right/32768)*0.5 = ((left + right)/32768)*0.5 = left + right)/65536
							sound->buf[i] = ((int32)temp[i * 2 + 0] + (int32)temp[i * 2 + 1])  * (1.f / 65536);
						}
					}
				}
				else if(bytes_per_sample == 3)
				{
					if(wav_num_channels == 1)
					{
						const uint8* src = (const uint8*)file.currentReadPtr();
						for(uint32 i=0; i<num_samples; ++i)
						{
							int32 val;
							std::memcpy(&val, &src[i * 3], 3);
							sound->buf[i] = val * (1.f / 8388608);
						}
					}
					else if(wav_num_channels == 2)
					{
						// TEMP: mix down to mono
						const uint8* src = (const uint8*)file.currentReadPtr();
						for(uint32 i=0; i<num_samples / wav_num_channels; ++i)
						{
							// NOTE: a much faster way to do this would be to handle blocks of 4 3-byte values, and use bitwise ops on them.
							int32 left = 0;
							int32 right = 0;
							std::memcpy(&left, &src[i * 3 * 2 + 0], 3);
							std::memcpy(&right, &src[i * 3 * 2 + 3], 3);
							//sound->buf[i] = ((left + right) - 16777216) * (1.f / 16777216); // values seem to be in [0, 16777216), so map to [-8388608, 8388607)
							sound->buf[i] = (signExtend24BitValue(left) + signExtend24BitValue(right)) * (1.f / 16777216); // values seem to be in [0, 16777216), so map to [-8388608, 8388607)
						}
					}
				}
				else if(bytes_per_sample == 4)
				{
					std::vector<int32> temp(num_samples);
					std::memcpy(temp.data(), file.currentReadPtr(), num_samples * bytes_per_sample); // Copy to ensure 32-bit aligned.

					for(uint32 i=0; i<num_samples; ++i)
						sound->buf[i] = temp[i] * (1.f / 2147483648.f);
				}
				else
					throw glare::Exception("Unhandled bytes_per_sample: " + toString(bytes_per_sample));

				file.setReadIndex(file.getReadIndex() + expected_remaining);
			}
			else
			{
				// Unknown chunk, skip it
				file.setReadIndex(file.getReadIndex() + chunk_size);
			}
		}

		if(sound->buf.empty())
			throw glare::Exception("Didn't find data chunk");

		return sound;
	}
	catch(glare::Exception& e)
	{
		throw e;
	}
}


SoundFileRef AudioEngine::loadSoundFile(const std::string& sound_file_path)
{
	if(hasExtension(sound_file_path, "wav"))
	{
		return loadWavFile(sound_file_path);
	}
	else
		throw glare::Exception("Unhandled sound file extension: " + getExtension(sound_file_path));
}


void AudioEngine::playOneShotSound(const std::string& sound_file_path, const Vec4f& pos)
{
	try
	{
		SoundFileRef sound;
		auto res = sound_files.find(sound_file_path);
		if(res == sound_files.end())
		{
			// Load the sound
			sound = loadSoundFile(sound_file_path);
			sound_files.insert(std::make_pair(sound_file_path, sound));
		}
		else
			sound = res->second;

		// Make a new audio source
		AudioSourceRef source = new AudioSource();
		source->type = AudioSource::SourceType_OneShot;
		source->buffer.pushBackNItems(sound->buf.data(), sound->buf.size());
		source->remove_on_finish = true;

		addSource(source);
		resonance->SetSourcePosition(source->resonance_handle, pos[0], pos[1], pos[2]);
	}
	catch(glare::Exception& e)
	{
		conPrint("Warning: Error while trying to play sound: " + e.what());
	}
}


AudioSourceRef AudioEngine::addSourceFromSoundFile(const std::string& sound_file_path)
{
	SoundFileRef sound;
	auto res = sound_files.find(sound_file_path);
	if(res == sound_files.end())
	{
		// Load the sound
		sound = loadSoundFile(sound_file_path);
		sound_files.insert(std::make_pair(sound_file_path, sound));
	}
	else
		sound = res->second;

	// Make a new audio source
	AudioSourceRef source = new AudioSource();
	source->type = AudioSource::SourceType_OneShot;
	source->remove_on_finish = false;
	source->buffer.pushBackNItems(sound->buf.data(), sound->buf.size());
	source->cur_read_i = source->buffer.size(); // Position read_i at end of buffer so doesn't play yet.

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
		SoundFileRef sound;
		/*sound = engine.loadWavFile("D:\\audio\\sound_effects\\171697__nenadsimic__menu-selection-click.wav"); // 24-bit
		testAssert(sound->num_channels == 2);
		testAssert(sound->sample_rate == 44100);
		testAssert(sound->minVal() >= -1.f && sound->maxVal() <= 1.f);

		sound = engine.loadWavFile("D:\\audio\\sound_effects\\366102__original-sound__confirmation-upward.wav"); // 16-bit
		testAssert(sound->num_channels == 2);
		testAssert(sound->sample_rate == 44100);
		testAssert(sound->minVal() >= -1.f && sound->maxVal() <= 1.f);*/
		
		sound = engine.loadWavFile("D:\\audio\\sound_effects\\select_mono.wav"); // 16-bit mono
		testAssert(sound->num_channels == 1);
		testAssert(sound->sample_rate == 44100);
		testAssert(sound->minVal() >= -1.f && sound->maxVal() <= 1.f);

		sound = engine.loadWavFile("D:\\audio\\sound_effects\\462089__newagesoup__ethereal-woosh.wav"); // 16-bit stero
		testAssert(sound->num_channels == 1); // mixed down to 1 channel
		testAssert(sound->sample_rate == 44100);
		testAssert(sound->minVal() >= -1.f && sound->maxVal() <= 1.f);
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}
}


#endif // BUILD_TESTS

