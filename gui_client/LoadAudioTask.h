/*=====================================================================
LoadAudioTask.h
---------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once

#include "../shared/Resource.h"
#include "../audio/AudioEngine.h"
#include <Task.h>
#include <ThreadMessage.h>
#include <string>
#include <vector>
class ResourceManager;


class AudioLoadedThreadMessage : public ThreadMessage
{
public:
	std::string audio_source_url;

	glare::SoundFileRef sound_file;
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

	ResourceRef resource;
	std::string audio_source_url;
	std::string audio_source_path;
	Reference<ResourceManager> resource_manager;
	ThreadSafeQueue<Reference<ThreadMessage> >* result_msg_queue;
};
