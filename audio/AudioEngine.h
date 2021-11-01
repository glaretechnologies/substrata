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
#include <vector>
#include <set>
#include <map>


class RtAudio;
namespace vraudio { class ResonanceAudioApi; }


namespace glare
{


class AudioEngine;


struct AudioBuffer : public ThreadSafeRefCounted
{
	js::Vector<float, 16> buffer;
};
typedef Reference<AudioBuffer> AudioBufferRef;


class AudioSource : public ThreadSafeRefCounted
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	enum SourceType
	{
		SourceType_Looping,
		SourceType_OneShot,
		SourceType_Streaming
	};

	AudioSource() : cur_read_i(0), type(SourceType_Looping), remove_on_finish(true), volume(1.f), pos(0,0,0,1), num_occlusions(0) {}
	
	int resonance_handle; // Set in AudioEngine::addSource().
	
	size_t cur_read_i;

	// Audio data can either be in buffer or shared_buffer.
	CircularBuffer<float> buffer; // Read from front, enqueue to back.

	AudioBufferRef shared_buffer;

	SourceType type;
	bool remove_on_finish; // for SourceType_OneShot

	float volume; // 1 = default

	Vec4f pos;

	float num_occlusions;
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
	float maxVal() const;
	float minVal() const;

	std::vector<float> buf;
	uint32 num_channels;
	uint32 sample_rate;
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

	AudioSourceRef addSourceFromSoundFile(const std::string& sound_file_path);

	void sourcePositionUpdated(AudioSource& source);

	void sourceVolumeUpdated(AudioSource& source);

	void sourceNumOcclusionsUpdated(AudioSource& source);

	void setHeadTransform(const Vec4f& head_pos, const Quatf& head_rot);

	void playOneShotSound(const std::string& sound_file_path, const Vec4f& pos);

	void setRoomEffectsEnabled(bool enabled);
	void setCurentRoomDimensions(const js::AABBox& room_aabb);
	

	static void test();
private:
	SoundFileRef loadWavFile(const std::string& sound_file_path);
	SoundFileRef loadSoundFile(const std::string& sound_file_path);

	RtAudio* audio;
	vraudio::ResonanceAudioApi* resonance;

	AudioCallbackData callback_data;

public:
	Mutex mutex; // Guards access to audio_sources, and resonance
	std::set<AudioSourceRef> audio_sources;

	ThreadManager resonance_thread_manager;

	std::map<std::string, SoundFileRef> sound_files;
};


} // end namespace glare
