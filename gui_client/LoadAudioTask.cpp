/*=====================================================================
LoadAudioTask.cpp
-----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "LoadAudioTask.h"


#include "MainWindow.h"
#include <ConPrint.h>
#include <PlatformUtils.h>
#include <audio/AudioFileReader.h>


LoadAudioTask::LoadAudioTask()
{}


LoadAudioTask::~LoadAudioTask()
{}


void LoadAudioTask::run(size_t thread_index)
{
	//const bool just_inserted = main_window->checkAddAudioToProcessedSet(audio_source_url); // Mark audio as being processed so another LoadTextureTask doesn't try and process it also.
	//if(!just_inserted)
	//	return;

	conPrint("LoadAudioTask: Loading '" + audio_source_path + "'...");

	try
	{
		AudioFileReader::AudioFileContent content;
		AudioFileReader::readAudioFile(audio_source_path, content);

		Reference<AudioLoadedThreadMessage> msg = new AudioLoadedThreadMessage();
		msg->audio_source_url = audio_source_url;

		// Mix down to mono
		if(content.num_channels == 1)
		{
			const size_t num_mono_samples = content.data.size();
			msg->data.resize(num_mono_samples);
			for(size_t i=0; i<num_mono_samples; ++i)
				msg->data[i] = (float)content.data[i] * (1.f / 32768.f);
		}
		else if(content.num_channels == 2)
		{
			const size_t num_mono_samples = content.data.size() / 2;
			msg->data.resize(num_mono_samples);

			for(size_t i=0; i<num_mono_samples; ++i)
				msg->data[i] = ((float)content.data[i*2 + 0] + (float)content.data[i*2 + 1]) * (0.5f / 32768.f); // Take average, scale to [-1, 1].
		}
		else
			throw glare::Exception("Unhandled num channels " + toString(content.num_channels));

		conPrint("Loaded audio data '" + audio_source_path + "': " + toString(msg->data.size() * sizeof(float)) + " B");

		main_window->msg_queue.enqueue(msg);
	}
	catch(glare::Exception& e)
	{
		conPrint("Error while loading audio: " + e.what());
	}
}
