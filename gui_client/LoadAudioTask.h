/*=====================================================================
LoadAudioTask.h
---------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include <Task.h>
#include <ThreadMessage.h>
#include <string>
#include <vector>
class MainWindow;


class AudioLoadedThreadMessage : public ThreadMessage
{
public:
	std::string audio_source_url;

	std::vector<float> data;
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

	std::string audio_source_url;
	std::string audio_source_path;
	MainWindow* main_window;
};
