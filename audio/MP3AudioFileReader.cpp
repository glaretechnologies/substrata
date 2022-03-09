/*=====================================================================
MP3AudioFileReader.cpp
----------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "MP3AudioFileReader.h"


#include "../utils/StringUtils.h"
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
	mp3dec_t mp3d;
	mp3dec_file_info_t info;
	std::memset(&info, 0, sizeof(info));
#ifdef _WIN32
	const int res = mp3dec_load_w(&mp3d, StringUtils::UTF8ToPlatformUnicodeEncoding(path).c_str(), &info, NULL, NULL);
#else
	const int res = mp3dec_load  (&mp3d, StringUtils::UTF8ToPlatformUnicodeEncoding(path).c_str(), &info, NULL, NULL);
#endif
	if(res != 0)
	{
		// TODO: need to free buffer here?
		throw glare::Exception("Error while loading MP3 file. (res: " + toString(res) + ")");
	}

	SoundFileRef sound_file = new SoundFile();
	sound_file->num_channels = info.channels;
	sound_file->sample_rate = info.hz;

	static_assert(sizeof(mp3d_sample_t) == sizeof(float), "sizeof(mp3d_sample_t) == sizeof(float)");
	sound_file->buf->buffer.resize(info.samples);
	std::memcpy(sound_file->buf->buffer.data(), info.buffer, info.samples * sizeof(mp3d_sample_t));

	free(info.buffer);

	return sound_file;
}


#if BUILD_TESTS


#include "../utils/TestUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/Timer.h"
#include "../utils/FileUtils.h"


void glare::MP3AudioFileReader::test()
{
	conPrint("MP3AudioFileReader::test()");

	try
	{
		// Mp3
		{
			SoundFileRef sound_file = AudioFileReader::readAudioFile(TestUtils::getTestReposDir() + "/testfiles/mp3s/mono.mp3");
			testAssert(sound_file->num_channels == 1);
			testAssert(sound_file->sample_rate == 44100);
		}
		{
			SoundFileRef sound_file = AudioFileReader::readAudioFile(TestUtils::getTestReposDir() + "/testfiles/mp3s/sample-3s.mp3");
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
