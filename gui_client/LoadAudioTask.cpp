/*=====================================================================
LoadAudioTask.cpp
-----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "LoadAudioTask.h"


#include "MainWindow.h"
#include <ConPrint.h>
#include <PlatformUtils.h>
#include "../audio/AudioFileReader.h"


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
		glare::SoundFileRef sound_file = glare::AudioFileReader::readAudioFile(audio_source_path);

		Reference<AudioLoadedThreadMessage> msg = new AudioLoadedThreadMessage();
		msg->audio_source_url = audio_source_url;

		
		if(sound_file->num_channels == 1)
		{
			msg->audio_buffer = sound_file->buf;
		}
		else if(sound_file->num_channels == 2)
		{
			// Mix down to mono
			const size_t num_mono_samples = sound_file->buf->buffer.size() / 2;
			if(sound_file->buf->buffer.size() != num_mono_samples * 2)
				throw glare::Exception("invalid number of samples for stereo file (not even)");

			glare::AudioBufferRef buffer = new glare::AudioBuffer();
			msg->audio_buffer = buffer;
			buffer->buffer.resize(num_mono_samples);

			const float* const src = sound_file->buf->buffer.data();
			float* const dst = buffer->buffer.data();
			for(size_t i=0; i<num_mono_samples; ++i)
				dst[i] = (src[i*2 + 0] + src[i*2 + 1]) * 0.5f; // Take average
		}
		else
			throw glare::Exception("Unhandled num channels " + toString(sound_file->num_channels));

		// conPrint("Loaded audio data '" + audio_source_path + "': " + toString(msg->data.size() * sizeof(float)) + " B");

		main_window->msg_queue.enqueue(msg);
	}
	catch(glare::Exception& e)
	{
		conPrint("Error while loading audio: " + e.what());
	}
}
