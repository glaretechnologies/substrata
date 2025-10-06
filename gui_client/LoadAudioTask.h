/*=====================================================================
LoadAudioTask.h
---------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once

#include "../shared/Resource.h"
#include "../shared/URLString.h"
#include "../audio/AudioEngine.h"
#include <Task.h>
#include <ThreadMessage.h>
#include <string>
#include <vector>
class ResourceManager;
class SharedMemMappedFile;


class AudioLoadedThreadMessage : public ThreadMessage
{
public:
	URLString audio_source_url;

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

	bool mem_map_file;
	ResourceRef resource;
	Reference<LoadedBuffer> loaded_buffer; // For emscripten, load from memory buffer instead of from resource on disk.Reference<LoadedBuffer> loaded_buffer;
	URLString audio_source_url;
	std::string audio_source_path;
	Reference<ResourceManager> resource_manager;
	ThreadSafeQueue<Reference<ThreadMessage> >* result_msg_queue;
};
