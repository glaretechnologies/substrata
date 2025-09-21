/*=====================================================================
MP3AudioFileReader.h
--------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "AudioFileReader.h"
#include <utils/ThreadSafeRefCounted.h>
#include <utils/BufferViewInStream.h>
#include <utils/ArrayRef.h>
#include <utils/MemMappedFile.h>


#define MINIMP3_FLOAT_OUTPUT
#ifdef _WIN32
#pragma warning(push, 0) // Suppress some warnings
#pragma warning(disable : 4706)
#endif
#include "../minimp3/minimp3_ex.h"
#ifdef _WIN32
#pragma warning(pop)
#endif



namespace glare
{


/*=====================================================================
MP3AudioStreamer
----------------
Streams an mp3 file - allows reading one mp3 frame at a time
=====================================================================*/
class MP3AudioStreamer : public ThreadSafeRefCounted
{
public:
	MP3AudioStreamer(const Reference<SharedMemMappedFile>& mem_mapped_file); // Takes ownership of mem_mapped_file
	MP3AudioStreamer(const Reference<MP3AudioStreamerDataSource>& source);
	~MP3AudioStreamer();

	// Returns true if reached EOF
	bool decodeFrame(js::Vector<float, 16>& samples_out, int& num_channels_out, int& sample_freq_hz_out);

	void seekToBeginningOfFile();

	void seekToApproxTimeWrapped(double time);

	mp3dec_t decoder;

	Reference<SharedMemMappedFile> mem_mapped_file;
	Reference<MP3AudioStreamerDataSource> source;
	BufferViewInStream in_stream;
};


/*=====================================================================
MP3AudioFileReader
------------------
Reads an entire mp3 file to a SoundFile.
=====================================================================*/
class MP3AudioFileReader
{
public:
	static SoundFileRef readAudioFile(const std::string& path);

	static SoundFileRef readAudioFileFromBuffer(const uint8* data, size_t len);

	static void test();
};


} // end namespace glare
