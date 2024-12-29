/*=====================================================================
AudioEngine.h
-------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "AudioResampler.h"
#include "../maths/vec3.h"
#include "../maths/vec2.h"
#include "../maths/matrix3.h"
#include "../maths/Quat.h"
#include "../physics/jscol_aabbox.h"
#include <utils/CircularBuffer.h>
#include <utils/Mutex.h>
#include <utils/ThreadManager.h>
#include <utils/Vector.h>
#include <utils/VRef.h>
#include <vector>
#include <set>
#include <map>


class RtAudio;
namespace vraudio { class ResonanceAudioApi; }
struct ma_device;


namespace glare
{


class AudioEngine;
class MP3AudioStreamer;
struct SoundFile;

struct AudioBuffer : public ThreadSafeRefCounted
{
	js::Vector<float, 16> buffer;
};
typedef Reference<AudioBuffer> AudioBufferRef;
typedef VRef<AudioBuffer> AudioBufferVRef;


struct SoundFile : public ThreadSafeRefCounted
{
	SoundFile() : buf(new AudioBuffer()) {}

	float maxVal() const;
	float minVal() const;

	AudioBufferVRef buf;
	uint32 num_channels;
	uint32 sample_rate; // in hz
};
typedef Reference<SoundFile> SoundFileRef;


struct MixSource
{
	MixSource() : sound_file_i(0), source_delta(1), mix_factor(1) {}

	Reference<SoundFile> soundfile;
	double sound_file_i; // Current (floating-point) index into soundfile buffer.
	double source_delta; // Every sample, we advance this much in the soundfile buffer.
	float mix_factor; // Volume mixing factor.
};


class AudioSource : public ThreadSafeRefCounted
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	enum SourceType
	{
		SourceType_Looping,
		SourceType_OneShot,
		SourceType_Streaming // Audio data is streamed from e.g. a video, into the circular buffer.
	};

	enum SourceSpatialType
	{
		SourceSpatialType_Spatial,
		SourceSpatialType_NonSpatial
	};

	AudioSource();
	~AudioSource();

	void startMuting(double cur_time, double transition_period);
	void startUnmuting(double cur_time, double transition_period);
	void updateCurrentMuteVolumeFactor(double cur_time);

	void setMuteVolumeFactorImmediately(float factor);
	inline float getMuteVolumeFactor() const { return mute_volume_factor; }

	void updateDopplerEffectFactor(const Vec4f& source_linear_vel, const Vec4f& listener_linear_vel, const Vec4f& listener_pos);

	int resonance_handle; // Set in AudioEngine::addSource().

	int sampling_rate;
	
	// Audio data can either be in buffer or shared_buffer.
	CircularBuffer<float> buffer; // Read from front, enqueue to back.  Used for type SourceType_Streaming only.

	AudioBufferRef shared_buffer; // Used for SourceType_Looping and SourceType_OneShot types only.
	size_t cur_read_i; // Current read index in shared_buffer.

	std::vector<MixSource> mix_sources; // If this is non-empty, this audio source mixes pitch-shifted and volume-scaled sounds together.  Used for type SourceType_Streaming.

	SourceType type;
	SourceSpatialType spatial_type; // Default is SourceSpatialType_Spatial
	bool remove_on_finish; // for SourceType_OneShot

	float volume; // 1 = default

private:
	float mute_volume_factor; // default = 1.  Final volume is volume * mute_volume_factor.  Use startMuting() and startUnmuting() to change this.
	double mute_change_start_time;
	double mute_change_end_time;
	float mute_vol_fac_start; // mute_volume_factor at start of transition period.
	float mute_vol_fac_end; // mute_volume_factor at end of transition period.
public:
	float doppler_factor;

	Vec4f pos;

	float num_occlusions;

	uint32 userdata_1;

	std::string debugname;

	float smoothed_cur_level;

	AudioResampler resampler;
};
typedef Reference<AudioSource> AudioSourceRef;


struct AudioCallbackData
{
	Mutex buffer_mutex; // protects buffer
	CircularBuffer<float> buffer			GUARDED_BY(buffer_mutex);

	//ThreadSafeQueue<Reference<AudioBuffer>> audio_buffer_queue;
	vraudio::ResonanceAudioApi* resonance;
	AudioEngine* engine;

	std::vector<float> temp_buf;
};


/*=====================================================================
AudioEngine
-----------
=====================================================================*/
class AudioEngine
{
public:
	AudioEngine();
	~AudioEngine();

	void init();
	void shutdown();

	bool isInitialised() const { return initialised; }

	void addSource(AudioSourceRef source);

	void removeSource(AudioSourceRef source);

	//AudioSourceRef addSourceFromSoundFile(const std::string& sound_file_path);

	AudioSourceRef addSourceFromStreamingSoundFile(const std::string& sound_file_path, const Vec4f& pos, float source_volume, double global_time);

	void sourcePositionUpdated(AudioSource& source);

	void sourceVolumeUpdated(AudioSource& source);

	void sourceNumOcclusionsUpdated(AudioSource& source);

	void setHeadTransform(const Vec4f& head_pos, const Quatf& head_rot);

	void playOneShotSound(const std::string& sound_file_path, const Vec4f& pos);

	void setRoomEffectsEnabled(bool enabled);
	void setCurentRoomDimensions(const js::AABBox& room_aabb);

	uint32 getSampleRate() const { return sample_rate; }
	
	void setMasterVolume(float volume);

	SoundFileRef getOrLoadSoundFile(const std::string& sound_file_path);

	static void test();
private:
	SoundFileRef loadSoundFile(const std::string& sound_file_path);

	RtAudio* audio;
	ma_device* device; // Miniaudio device
	vraudio::ResonanceAudioApi* resonance;

	AudioCallbackData callback_data;

	bool initialised;

public:
	Mutex mutex; // Guards access to audio_sources, and resonance
	std::set<AudioSourceRef> audio_sources			GUARDED_BY(mutex);

	ThreadManager thread_manager; // Manages: ResonanceThread, StreamerThread

	std::map<std::string, SoundFileRef> sound_files;

	std::map<std::string, Reference<MP3AudioStreamer>> streams; // Map from mp3 file path to MP3AudioStreamer for that mp3.

	std::map<Reference<MP3AudioStreamer>, std::set<AudioSourceRef>> sources_playing_streams; // Map from MP3AudioStreamer to audio sources playing that stream.

private:
	uint32 sample_rate;
};


} // end namespace glare
