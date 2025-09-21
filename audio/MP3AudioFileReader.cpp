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


glare::MP3AudioStreamer::MP3AudioStreamer(const Reference<SharedMemMappedFile>& mem_mapped_file_)
:	mem_mapped_file(mem_mapped_file_),
	in_stream(ArrayRef<uint8>((const uint8*)mem_mapped_file_->fileData(), mem_mapped_file_->fileSize()))
{
	mp3dec_init(&decoder);
}


glare::MP3AudioStreamer::MP3AudioStreamer(const Reference<MP3AudioStreamerDataSource>& source_)
:	mem_mapped_file(NULL),
	source(source_),
	in_stream(ArrayRef<uint8>(source_->getData(), source_->getSize()))
{
	mp3dec_init(&decoder);
}


glare::MP3AudioStreamer::~MP3AudioStreamer()
{
}


bool glare::MP3AudioStreamer::decodeFrame(js::Vector<float, 16>& samples_out, int& num_channels_out, int& sample_freq_hz_out)
{
	samples_out.resizeNoCopy(MINIMP3_MAX_SAMPLES_PER_FRAME);

	// Limit the number of bytes we tell minimp3 are in the buffer, to limit the distance it will look through a malformed file looking for a frame.
	const size_t max_use_bytes_avail = 1 << 13;
	const size_t input_bytes_avail = myMin(in_stream.size() - in_stream.getReadIndex(), max_use_bytes_avail);

	mp3dec_frame_info_t frame_info;
	const int num_samples_decoded = mp3dec_decode_frame(&decoder, /*input buf=*/(const uint8*)in_stream.currentReadPtr(), /*input buf size=*/(int)input_bytes_avail, /*pcm data out=*/samples_out.data(), &frame_info);

	in_stream.advanceReadIndex(frame_info.frame_bytes);

	if(num_samples_decoded > 0 && frame_info.frame_bytes > 0)
	{
		// Successful decode, we have some new decoded samples
		num_channels_out = frame_info.channels;
		sample_freq_hz_out = frame_info.hz;
		samples_out.resize(num_samples_decoded * frame_info.channels);
		return false; // not EOF
	}
	else if(num_samples_decoded == 0 && frame_info.frame_bytes > 0)
	{
		// The decoder skipped ID3 or invalid data
		num_channels_out = 0;
		sample_freq_hz_out = 0;
		samples_out.resize(0);
		return false; // not EOF
	}
	else
	{
		// Insufficient data
		num_channels_out = 0;
		sample_freq_hz_out = 0;
		samples_out.resize(0);
		return true; // EOF
	}
}


void glare::MP3AudioStreamer::seekToBeginningOfFile()
{
	in_stream.setReadIndex(0);
}


void glare::MP3AudioStreamer::seekToApproxTimeWrapped(double time)
{
	if(in_stream.size() > 0)
	{
		const double bitrate = 192000; // NOTE: just guessing a bit rate here, this will obviously give a very approximate seek.
		const double bytes_per_sec = bitrate / 8;
		const size_t offset = (size_t)(time * bytes_per_sec) % in_stream.size();

		in_stream.setReadIndex(offset);
	}
}


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
		glare::MP3AudioStreamer streamer(ArrayRef<uint8>(data, size));

		js::Vector<float, 16> samples;
		while(1)
		{
			int num_channels, sample_freq_hz;
			const bool is_EOF = streamer.decodeFrame(samples, num_channels, sample_freq_hz);
			if(is_EOF)
				break;
		}
	}
	catch(glare::Exception&)
	{
	}
	return 0;  // Non-zero return values are reserved for future use.
}

/*extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	try
	{
		glare::MP3AudioFileReader::readAudioFileFromBuffer(data, size);
	}
	catch(glare::Exception&)
	{
	}
	return 0;  // Non-zero return values are reserved for future use.
}*/

#endif


static void testStreamingValidMP3File(const std::string& path)
{
	try
	{
		Reference<glare::MP3AudioStreamer> streamer = new glare::MP3AudioStreamer(new SharedMemMappedFile(path));

		int num_complete_read_iters = 3;
		for(int i=0; i<num_complete_read_iters; ++i)
		{
			Timer complete_read_timer;
			size_t total_samples_decoded = 0;
			js::Vector<float, 16> samples;
			while(1)
			{
				int num_channels, sample_freq_hz;
				const bool is_EOF = streamer->decodeFrame(samples, num_channels, sample_freq_hz);
				if(is_EOF)
					break;

				testAssert(num_channels == 2 || num_channels == 1 || num_channels == 0);
				testAssert(sample_freq_hz == 44100 || sample_freq_hz == 0);
				total_samples_decoded += samples.size();
			}

			testAssert(total_samples_decoded > 0);
			conPrint("Seeking...");
			streamer->seekToBeginningOfFile();
			conPrint(path + ": Read " + toString(total_samples_decoded) + " samples in " + complete_read_timer.elapsedStringNSigFigs(4));
		}
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}
}

void glare::MP3AudioFileReader::test()
{
	conPrint("MP3AudioFileReader::test()");


	// Test trying to stream an invalid file
	try
	{
		Reference<MP3AudioStreamer> streamer = new MP3AudioStreamer(new SharedMemMappedFile("D:/not_a_file_34324"));
		failTest("Expected exception");
	}
	catch(glare::Exception& )
	{
	}

	// Test trying to stream a malformed mp3 file.  In this case minimp3 will just scan through the entire file looking for a valid mp3 frame, and not finding one, returning no samples.
	try
	{
		const std::string path = TestUtils::getTestReposDir() + "/testfiles/gifs/https_58_47_47media.giphy.com_47media_47X93e1eC2J2hjy_47giphy.gif";
		Reference<MP3AudioStreamer> streamer = new MP3AudioStreamer(new SharedMemMappedFile(path));

		int num_complete_read_iters = 2;
		for(int i=0; i<num_complete_read_iters; ++i)
		{
			Timer complete_read_timer;
			size_t total_samples_decoded = 0;
			js::Vector<float, 16> samples;
			while(1)
			{
				int num_channels, sample_freq_hz;
				const bool is_EOF = streamer->decodeFrame(samples, num_channels, sample_freq_hz);
				if(is_EOF)
					break;

				testAssert(num_channels == 0);
				testAssert(sample_freq_hz == 0);
				total_samples_decoded += samples.size();
			}

			// Seek back to beginning of file
			conPrint("Seeking...");
			streamer->seekToBeginningOfFile();
			conPrint(path + ": Read " + toString(total_samples_decoded) + " samples in " + complete_read_timer.elapsedStringNSigFigs(4));
		}
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}

	
	// Test streaming some valid mp3 files.
	testStreamingValidMP3File(TestUtils::getTestReposDir() + "/testfiles/mp3s/mono.mp3");
	testStreamingValidMP3File(TestUtils::getTestReposDir() + "/testfiles/mp3s/sample-3s.mp3");


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
		
		//{
		//	SoundFileRef sound_file = MP3AudioFileReader::readAudioFile("D:\\files\\02___Sara_mp3_14061996302930432458.mp3");
		//	testAssert(sound_file->num_channels == 2);
		//	testAssert(sound_file->sample_rate == 44100);
		//}
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}

	conPrint("MP3AudioFileReader::test() done.");
}


#endif // BUILD_TESTS
