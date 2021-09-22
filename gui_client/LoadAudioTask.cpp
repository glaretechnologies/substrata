/*=====================================================================
LoadAudioTask.cpp
-----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "LoadAudioTask.h"


#include "MainWindow.h"
#include <ConPrint.h>
#include <PlatformUtils.h>
//#include <audiodecoder.h>
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

	//AudioDecoder* audio_decoder = NULL;
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


#if 0
		audio_decoder = new AudioDecoder(audio_source_path);

		if(audio_decoder->open() != AUDIODECODER_OK)
			throw glare::Exception("Audio decoder: Failed to open " + audio_source_path);

		const int num_samples_estimate = audio_decoder->numSamples(); // numSamples() is an estimate
		const int num_channels = audio_decoder->channels();
		if(num_channels <= 0 || num_channels > 2)
			throw glare::Exception("Audio decoder: Only 1 or 2 channels supported.");

		const int buf_num_samples = (int)((float)num_samples_estimate * 1.2f);

		std::vector<float> buf((size_t)buf_num_samples/* * (size_t)num_channels*/); // stereo samples so x2

		int buf_write_pos = 0;
		while(true)
		{
			const int remaining = (int)buf.size() - buf_write_pos;
			if(remaining < 0)
				throw glare::Exception("Error while reading audio samples.");
			const int read_samples = audio_decoder->read(myMin(8192, remaining), &buf[buf_write_pos]);
			buf_write_pos += read_samples;
			if(read_samples == 0)
				break;
		}

		const int num_samples = buf_write_pos;



		Reference<AudioLoadedThreadMessage> msg = new AudioLoadedThreadMessage();
		msg->audio_source_url = audio_source_url;
		
		
		// Mix down to mono
		if(num_channels == 1)
		{
			const int num_mono_samples = num_samples;
			msg->data.resize(num_mono_samples);
			for(size_t i=0; i<num_mono_samples; ++i)
				msg->data[i] = buf[i];
		}
		else
		{
			assert(num_channels == 2);
			const int num_mono_samples = num_samples / 2;
			msg->data.resize(num_mono_samples);

			for(size_t i=0; i<num_mono_samples; ++i)
				msg->data[i] = (buf[i*2 + 0] + buf[i*2 + 1]) * 0.5f;
		}
#endif

		conPrint("Loaded audio data '" + audio_source_path + "': " + toString(msg->data.size() * sizeof(float)) + " B");

		main_window->msg_queue.enqueue(msg);
	}
	catch(glare::Exception& e)
	{
		conPrint("Error while loading audio: " + e.what());
		//if(audio_decoder)
		//	delete audio_decoder;
	}
}
