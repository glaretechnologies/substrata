/*=====================================================================
LoadAudioTask.h
---------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "ThreadMessages.h"
#include "../shared/URLString.h"
#include "../shared/Resource.h" // For LoadedBuffer
#include "../audio/AudioEngine.h"
#include <Task.h>
#include <string>
class SharedMemMappedFile;


class AudioLoadedThreadMessage : public ThreadMessage
{
public:
	AudioLoadedThreadMessage() : ThreadMessage(Msg_AudioLoadedThreadMessage) {}
	URLString audio_url;

	glare::SoundFileRef sound_file;

	Reference<SharedMemMappedFile> mapped_file;
};


/*=====================================================================
LoadAudioTask
-------------

=====================================================================*/
class LoadAudioTask : public glare::Task
{
public:
	LoadAudioTask();
	virtual ~LoadAudioTask();

	virtual void run(size_t thread_index);

	bool mem_map_file; // If true, just mem-map the file at audio_path, and return it in AudioLoadedThreadMessage::mapped_file.  For streaming mp3s.
	// If false, load the audio file at audio_path completely into memory, and return in AudioLoadedThreadMessage::sound_file.

	Reference<LoadedBuffer> loaded_buffer; // For emscripten, load from memory buffer instead of from resource on disk.
	URLString audio_url;
	std::string audio_path; // Local abs path on disk
	ThreadSafeQueue<Reference<ThreadMessage> >* result_msg_queue;
};
