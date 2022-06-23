/*=====================================================================
AudioEngine.h
-------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


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


namespace glare
{


class AudioEngine;
class MP3AudioStreamer;


struct AudioBuffer : public ThreadSafeRefCounted
{
	js::Vector<float, 16> buffer;
};
typedef Reference<AudioBuffer> AudioBufferRef;
typedef VRef<AudioBuffer> AudioBufferVRef;


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

	AudioSource();
	~AudioSource();


	void startMuting(double cur_time, double transition_period);
	void startUnmuting(double cur_time, double transition_period);
	void updateCurrentMuteVolumeFactor(double cur_time);

	inline float getMuteVolumeFactor() const { return mute_volume_factor; }

	int resonance_handle; // Set in AudioEngine::addSource().
	
	// Audio data can either be in buffer or shared_buffer.
	CircularBuffer<float> buffer; // Read from front, enqueue to back.  Used for type SourceType_Streaming only.

	AudioBufferRef shared_buffer; // Used for SourceType_Looping and SourceType_OneShot types only.
	size_t cur_read_i; // Current read index in shared_buffer.

	SourceType type;
	bool remove_on_finish; // for SourceType_OneShot

	float volume; // 1 = default

private:
	float mute_volume_factor; // default = 1.  Final volume is volume * mute_volume_factor.  Use startMuting() and startUnmuting() to change this.
	double mute_change_start_time;
	double mute_change_end_time;
	float mute_vol_fac_start; // mute_volume_factor at start of transition period.
	float mute_vol_fac_end; // mute_volume_factor at end of transition period.
public:

	Vec4f pos;

	float num_occlusions;

	uint32 userdata_1;

	std::string debugname;
};
typedef Reference<AudioSource> AudioSourceRef;


struct AudioCallbackData
{
	Mutex buffer_mutex; // protects buffer
	CircularBuffer<float> buffer;

	//ThreadSafeQueue<Reference<AudioBuffer>> audio_buffer_queue;
	vraudio::ResonanceAudioApi* resonance;
	AudioEngine* engine;

	std::vector<float> temp_buf;
};


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

	void addSource(AudioSourceRef source);

	void removeSource(AudioSourceRef source);

	//AudioSourceRef addSourceFromSoundFile(const std::string& sound_file_path);

	AudioSourceRef addSourceFromStreamingSoundFile(const std::string& sound_file_path, const Vec4f& pos, double global_time);

	void sourcePositionUpdated(AudioSource& source);

	void sourceVolumeUpdated(AudioSource& source);

	void sourceNumOcclusionsUpdated(AudioSource& source);

	void setHeadTransform(const Vec4f& head_pos, const Quatf& head_rot);

	void playOneShotSound(const std::string& sound_file_path, const Vec4f& pos);

	void setRoomEffectsEnabled(bool enabled);
	void setCurentRoomDimensions(const js::AABBox& room_aabb);

	uint32 getSampleRate() const { return sample_rate; }
	

	static void test();
private:
	SoundFileRef loadSoundFile(const std::string& sound_file_path);

	RtAudio* audio;
	vraudio::ResonanceAudioApi* resonance;

	AudioCallbackData callback_data;

	bool initialised;

public:
	Mutex mutex; // Guards access to audio_sources, and resonance
	std::set<AudioSourceRef> audio_sources;

	ThreadManager thread_manager; // Manages: ResonanceThread, StreamerThread

	std::map<std::string, SoundFileRef> sound_files;

	std::map<std::string, Reference<MP3AudioStreamer>> streams; // Map from mp3 file path to MP3AudioStreamer for that mp3.

	std::map<Reference<MP3AudioStreamer>, std::set<AudioSourceRef>> sources_playing_streams; // Map from MP3AudioStreamer to audio sources playing that stream.

private:
	uint32 sample_rate;
};


} // end namespace glare
