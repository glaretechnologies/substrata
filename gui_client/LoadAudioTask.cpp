/*=====================================================================
LoadAudioTask.cpp
-----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "LoadAudioTask.h"


#include "ThreadMessages.h"
#include "../shared/ResourceManager.h"
#include "../audio/AudioFileReader.h"
#include <utils/ConPrint.h>
#include <utils/StringUtils.h>
#include <utils/PlatformUtils.h>
#include <utils/ThreadSafeQueue.h>
#include <utils/MemMappedFile.h>
#include <tracy/Tracy.hpp>


LoadAudioTask::LoadAudioTask()
{}


LoadAudioTask::~LoadAudioTask()
{}


void LoadAudioTask::run(size_t thread_index)
{
	// conPrint("LoadAudioTask: Loading '" + audio_source_path + "'...");
	ZoneScopedN("LoadAudioTask"); // Tracy profiler
	ZoneText(audio_source_path.c_str(), audio_source_path.size());

	try
	{
		if(mem_map_file)
		{
			SharedMemMappedFileRef file = new SharedMemMappedFile(audio_source_path);

			Reference<AudioLoadedThreadMessage> msg = new AudioLoadedThreadMessage();
			msg->audio_source_url = audio_source_url;
			msg->mapped_file = file;

			result_msg_queue->enqueue(msg);
		}
		else
		{
			runtimeCheck(resource.nonNull() && resource_manager.nonNull());
			ArrayRef<uint8> audio_data_buffer;
#if EMSCRIPTEN
			// Use the in-memory buffer that we loaded in EmscriptenResourceDownloader
			runtimeCheck(loaded_buffer.nonNull());
			audio_data_buffer = ArrayRef<uint8>((const uint8*)loaded_buffer->buffer, loaded_buffer->buffer_size);
#else
			MemMappedFile file(audio_source_path);
			audio_data_buffer = ArrayRef<uint8>((const uint8*)file.fileData(), file.fileSize());
#endif

			glare::SoundFileRef sound_file = glare::AudioFileReader::readAudioFileFromBuffer(audio_source_path, audio_data_buffer);

			Reference<AudioLoadedThreadMessage> msg = new AudioLoadedThreadMessage();
			msg->audio_source_url = audio_source_url;
			msg->mapped_file = nullptr;

		
			if(sound_file->num_channels == 1)
			{
				msg->sound_file = sound_file;
			}
			else if(sound_file->num_channels == 2)
			{
				// Mix down to mono
				const size_t num_mono_samples = sound_file->buf->buffer.size() / 2;
				if(sound_file->buf->buffer.size() != num_mono_samples * 2)
					throw glare::Exception("invalid number of samples for stereo file (not even)");

				glare::SoundFileRef mono_sound_file = new glare::SoundFile();
				mono_sound_file->num_channels = 1;
				mono_sound_file->sample_rate = sound_file->sample_rate;
				mono_sound_file->buf->buffer.resize(num_mono_samples);

				const float* const src = sound_file->buf->buffer.data();
				float* const dst = mono_sound_file->buf->buffer.data();
				for(size_t i=0; i<num_mono_samples; ++i)
					dst[i] = (src[i*2 + 0] + src[i*2 + 1]) * 0.5f; // Take average

				msg->sound_file = mono_sound_file;
			}
			else
				throw glare::Exception("Unhandled num channels " + toString(sound_file->num_channels));

			// conPrint("Loaded audio data '" + audio_source_path + "': " + toString(msg->data.size() * sizeof(float)) + " B");
			result_msg_queue->enqueue(msg);
		}
	}
	catch(glare::Exception& e)
	{
		result_msg_queue->enqueue(new LogMessage("Error while loading audio: " + e.what()));
	}
	catch(std::bad_alloc&)
	{
		result_msg_queue->enqueue(new LogMessage("Error while loading audio: failed to allocate mem (bad_alloc)"));
	}
}
