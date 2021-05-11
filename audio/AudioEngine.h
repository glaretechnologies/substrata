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
#include <utils/CircularBuffer.h>
#include <utils/Mutex.h>
#include <utils/ThreadManager.h>
#include <vector>
#include <set>


class RtAudio;
class AudioEngine;
namespace vraudio { class ResonanceAudioApi; }


class AudioSource : public ThreadSafeRefCounted
{
public:
	enum SourceType
	{
		SourceType_Looping,
		SourceType_OneShot,
		SourceType_Streaming
	};

	AudioSource() : cur_read_i(0), type(SourceType_Looping) {}
	int resonance_handle;
	//std::vector<float> buffer;
	size_t cur_read_i;
	CircularBuffer<float> buffer; // Read from front, enqueue to back.

	SourceType type;
};
typedef Reference<AudioSource> AudioSourceRef;


struct AudioBuffer : public ThreadSafeRefCounted
{
	std::vector<float> buf;
};


struct AudioCallbackData
{
	Mutex buffer_mutex; // protects buffer
	CircularBuffer<float> buffer;

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

	void addSource(AudioSourceRef source);

	void setSourcePosition(AudioSourceRef source, const Vec4f& pos);

	void setHeadTransform(const Vec4f& head_pos, const Quatf& head_rot);

	RtAudio* audio;
	vraudio::ResonanceAudioApi* resonance;

	AudioCallbackData callback_data;

	Mutex mutex; // Guards access to audio_sources, and resonance
	std::set<AudioSourceRef> audio_sources;

	ThreadManager resonance_thread_manager;
};
