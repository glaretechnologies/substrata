/*=====================================================================
MP3AudioFileReader.cpp
----------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "MP3AudioFileReader.h"


#include "../utils/StringUtils.h"
#include "../utils/MemMappedFile.h"
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_FLOAT_OUTPUT
#ifdef _WIN32
#pragma warning(push, 0) // Suppress some warnings
#pragma warning(disable : 4706)
#endif
#include "../minimp3/minimp3_ex.h"
#ifdef _WIN32
#pragma warning(pop)
#endif


glare::SoundFileRef glare::MP3AudioFileReader::readAudioFile(const std::string& path)
{
	MemMappedFile file(path);

	return readAudioFileFromBuffer((const uint8*)file.fileData(), file.fileSize());
}


glare::SoundFileRef glare::MP3AudioFileReader::readAudioFileFromBuffer(const uint8* data, size_t len)
{
	mp3dec_t mp3d;
	mp3dec_file_info_t info;
	std::memset(&info, 0, sizeof(info));

	// NOTE: this MAX_ALLOC is stuff I added.  It's necessary for fuzzing to succeed, also should be good for preventing DOS attacks.
	const size_t MAX_ALLOC = 1 << 28;
	const int res = mp3dec_load_buf(&mp3d, data, len, &info, /*progress callback=*/NULL, /*user data=*/NULL, MAX_ALLOC);
	if(res != 0)
	{
		free(info.buffer);
		throw glare::Exception("Error while loading MP3 file. (res: " + toString(res) + ")");
	}

	SoundFileRef sound_file = new SoundFile();
	sound_file->num_channels = info.channels;
	sound_file->sample_rate = info.hz;

	static_assert(sizeof(mp3d_sample_t) == sizeof(float), "sizeof(mp3d_sample_t) == sizeof(float)");
	sound_file->buf->buffer.resize(info.samples);
	if(info.samples > 0)
		std::memcpy(sound_file->buf->buffer.data(), info.buffer, info.samples * sizeof(mp3d_sample_t));

	free(info.buffer);

	return sound_file;
}


#if BUILD_TESTS


#include "../utils/TestUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/Timer.h"
#include "../utils/FileUtils.h"


#if 0
// Command line:
// C:\fuzz_corpus\mp3 -max_len=1000000 -seed=1
// or
// C:\fuzz_corpus\mp3 -max_len=1000000 -jobs=16

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	try
	{
		glare::MP3AudioFileReader::readAudioFileFromBuffer(data, size);
	}
	catch(glare::Exception&)
	{
	}
	return 0;  // Non-zero return values are reserved for future use.
}
#endif


void glare::MP3AudioFileReader::test()
{
	conPrint("MP3AudioFileReader::test()");

	try
	{
		SoundFileRef sound_file = MP3AudioFileReader::readAudioFile(TestUtils::getTestReposDir() + "/testfiles/mp3s/crash-2711b1392452d49bb0022c34ff1b20cda8817bde");

		failTest("Expected excep");
	}
	catch(glare::Exception&)
	{}

	try
	{


		{
			SoundFileRef sound_file = MP3AudioFileReader::readAudioFile(TestUtils::getTestReposDir() + "/testfiles/mp3s/mono.mp3");
			testAssert(sound_file->num_channels == 1);
			testAssert(sound_file->sample_rate == 44100);
		}
		{
			SoundFileRef sound_file = MP3AudioFileReader::readAudioFile(TestUtils::getTestReposDir() + "/testfiles/mp3s/sample-3s.mp3");
			testAssert(sound_file->num_channels == 2);
			testAssert(sound_file->sample_rate == 44100);
		}
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}

	conPrint("MP3AudioFileReader::test() done.");
}


#endif // BUILD_TESTS
