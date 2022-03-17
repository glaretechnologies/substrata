/*=====================================================================
WavAudioFileReader.cpp
----------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "WavAudioFileReader.h"


#include "../utils/StringUtils.h"
#include "../utils/FileInStream.h"
#include "../utils/BufferViewInStream.h"
#include "../utils/Check.h"


static inline int32 signExtend24BitValue(int32 x)
{
	if((x & 0x800000) != 0)
	{
		return x | 0xFF000000;
	}
	else
	{
		// Sign bit (left bit) was 0
		return x;
	}
}


glare::SoundFileRef glare::WavAudioFileReader::readAudioFile(const std::string& sound_file_path)
{
	FileInStream file(sound_file_path);

	return readAudioFileFromBuffer((const uint8*)file.fileData(), file.fileSize());
}


glare::SoundFileRef glare::WavAudioFileReader::readAudioFileFromBuffer(const uint8* data, size_t len)
{
	try
	{
		BufferViewInStream file(ArrayRef<uint8>(data, len));

		SoundFileRef sound = new SoundFile();

		const uint32 riff_chunk_id = file.readUInt32();
		if(riff_chunk_id != 0x46464952) // big endian: 0x52494646
			throw glare::Exception("invalid header, expected RIFF");

		/*const uint32 riff_chunk_size =*/ file.readUInt32();
		/*const uint32 riff_chunk_format =*/ file.readUInt32();

		const uint32 fmt_chunk_id = file.readUInt32();
		if(fmt_chunk_id != 0x20746d66) // big endian: 0x666d7420
			throw glare::Exception("invalid fmt chunk id, expected fmt");
		/*const uint32 fmt_chunk_size =*/ file.readUInt32();
		const uint16 audio_format = file.readUInt16();
		if(audio_format != 1)
			throw glare::Exception("Unhandled audio format, only 1 (PCM) handled.");
		const uint16 wav_num_channels = file.readUInt16();
		const uint32 sample_rate = file.readUInt32(); // TODO: handle this by resampling to target rate.
		/*const uint32 byte_rate =*/ file.readUInt32();
		/*const uint16 block_align =*/ file.readUInt16(); // TODO: handle this?
		const uint16 bits_per_sample = file.readUInt16();

		sound->num_channels = 1; // mix down to mono
		sound->sample_rate = sample_rate;
		sound->buf = new AudioBuffer();

		while(!file.endOfStream())
		{
			// Read chunk
			const uint32 chunk_id = file.readUInt32();
			const uint32 chunk_size = file.readUInt32();

			if(chunk_id == 0x61746164) // "data", big endian: 0x64617461 
			{
				const uint32 bytes_per_sample = bits_per_sample / 8;

				// Avoid divide by zeroes
				if(wav_num_channels == 0)
					throw glare::Exception("Invalid num channels");
				if(bytes_per_sample == 0)
					throw glare::Exception("Invalid bytes per sample");

				// Read actual data, convert to float and resample to target sample rate
				const uint32 num_samples = chunk_size / bytes_per_sample;

				const uint32 MAX_NUM_SAMPLES = 1 << 27; // ~536 MB
				if(num_samples > MAX_NUM_SAMPLES)
					throw glare::Exception("too many samples: " + toString(num_samples));

				//const uint32 num_samples = num_samples / wav_num_channels;
				sound->buf->buffer.resize(num_samples / wav_num_channels); // TEMP: mix down to mono

				const size_t expected_remaining = bytes_per_sample * num_samples;
				if(file.getReadIndex() + expected_remaining > file.size())
					throw glare::Exception("not enough data in file.");

				if(bytes_per_sample == 2)
				{
					if(wav_num_channels == 1)
					{
						std::vector<int16> temp(num_samples);
						file.readData(temp.data(), num_samples * bytes_per_sample);

						for(uint32 i=0; i<num_samples / wav_num_channels; ++i)
							sound->buf->buffer[i] = temp[i] * (1.f / 32768);
					}
					else if(wav_num_channels == 2)
					{
						// Mix down to mono
						std::vector<int16> temp(num_samples);
						file.readData(temp.data(), num_samples * bytes_per_sample);

						for(uint32 i=0; i<num_samples / wav_num_channels; ++i)
						{
							// val = (left/32768.f + right/32768)*0.5 = ((left + right)/32768)*0.5 = left + right)/65536
							sound->buf->buffer[i] = ((int32)temp[i * 2 + 0] + (int32)temp[i * 2 + 1])  * (1.f / 65536);
						}
					}
				}
				else if(bytes_per_sample == 3)
				{
					if(wav_num_channels == 1)
					{
						doRuntimeCheck(file.getReadIndex() + num_samples * 3 <= file.size());
						const uint8* src = (const uint8*)file.currentReadPtr();
						for(uint32 i=0; i<num_samples; ++i)
						{
							int32 val = 0;
							std::memcpy(&val, &src[i * 3], 3);
							val = signExtend24BitValue(val);
							assert(val >= -8388608 && val < 8388608);
							sound->buf->buffer[i] = val * (1.f / 8388608);
						}
						file.setReadIndex(file.getReadIndex() + expected_remaining);
					}
					else if(wav_num_channels == 2)
					{
						// Mix down to mono
						doRuntimeCheck(file.getReadIndex() + num_samples * 3 <= file.size());
						const uint8* src = (const uint8*)file.currentReadPtr();
						for(uint32 i=0; i<num_samples / wav_num_channels; ++i)
						{
							// NOTE: a much faster way to do this would be to handle blocks of 4 3-byte values, and use bitwise ops on them.
							int32 left = 0;
							int32 right = 0;
							std::memcpy(&left, &src[i * 3 * 2 + 0], 3);
							std::memcpy(&right, &src[i * 3 * 2 + 3], 3);
							//sound->buf[i] = ((left + right) - 16777216) * (1.f / 16777216); // values seem to be in [0, 16777216), so map to [-8388608, 8388607)
							sound->buf->buffer[i] = (signExtend24BitValue(left) + signExtend24BitValue(right)) * (1.f / 16777216); // values seem to be in [0, 16777216), so map to [-8388608, 8388607)
						}
						file.setReadIndex(file.getReadIndex() + expected_remaining);
					}
				}
				else if(bytes_per_sample == 4)
				{
					if(wav_num_channels == 1)
					{
						std::vector<int32> temp(num_samples);
						file.readData(temp.data(), num_samples * bytes_per_sample);

						for(uint32 i=0; i<num_samples; ++i)
							sound->buf->buffer[i] = temp[i] * (1.f / 2147483648.f);
					}
					else if(wav_num_channels == 2)
					{
						// Mix down to mono
						std::vector<int32> temp(num_samples);
						file.readData(temp.data(), num_samples * bytes_per_sample);

						for(uint32 i=0; i<num_samples / wav_num_channels; ++i)
						{
							// val = (left/2147483648.f + right/2147483648)*0.5 = ((left + right)/2147483648)*0.5 = left + right)/4294967296
							sound->buf->buffer[i] = ((int32)temp[i * 2 + 0] + (int32)temp[i * 2 + 1])  * (1.f / 4294967296.f);
						}
					}
				}
				else
					throw glare::Exception("Unhandled bytes_per_sample: " + toString(bytes_per_sample));
			}
			else
			{
				// Unknown chunk, skip it
				file.setReadIndex(file.getReadIndex() + chunk_size);
			}
		}

		if(sound->buf->buffer.empty())
			throw glare::Exception("Didn't find data chunk");

		return sound;
	}
	catch(std::bad_alloc&)
	{
		throw glare::Exception("bad alloc");
	}
}


#if BUILD_TESTS


#include "../utils/TestUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/Timer.h"
#include "../utils/FileUtils.h"


#if 0
// Command line:
// C:\fuzz_corpus\wav -max_len=1000000 -seed=1

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	return 0;
}


// We will use the '!' character to break apart the input buffer into different 'packets'.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	try
	{
		glare::WavAudioFileReader::readAudioFileFromBuffer(data, size);
	}
	catch(glare::Exception&)
	{
	}
	
	return 0;  // Non-zero return values are reserved for future use.
}
#endif


static void checkSound(glare::SoundFileRef sound_file)
{
	testAssert(sound_file->buf->buffer.size() > 0);
	testAssert(sound_file->minVal() >= -1.f);
	testAssert(sound_file->maxVal() <= 1.f);
}


void glare::WavAudioFileReader::test()
{
	conPrint("WavAudioFileReader::test()");

	try
	{
		// 16-bit
		{
			SoundFileRef sound_file = WavAudioFileReader::readAudioFile(TestUtils::getTestReposDir() + "/testfiles/WAVs/mono.wav");
			testAssert(sound_file->num_channels == 1);
			testAssert(sound_file->sample_rate == 44100);
			checkSound(sound_file);
		}
		{
			SoundFileRef sound_file = WavAudioFileReader::readAudioFile(TestUtils::getTestReposDir() + "/testfiles/WAVs/stereo.wav");
			testAssert(sound_file->num_channels == 1); // We're mixing down to mono
			testAssert(sound_file->sample_rate == 44100);
			checkSound(sound_file);
		}

		// 24-bit
		{
			SoundFileRef sound_file = WavAudioFileReader::readAudioFile(TestUtils::getTestReposDir() + "/testfiles/WAVs/mono_24bit.wav");
			testAssert(sound_file->num_channels == 1);
			testAssert(sound_file->sample_rate == 44100);
			checkSound(sound_file);
		}

		{
			SoundFileRef sound_file = WavAudioFileReader::readAudioFile(TestUtils::getTestReposDir() + "/testfiles/WAVs/stereo_24bit.wav");
			testAssert(sound_file->num_channels == 1); // We're mixing down to mono
			testAssert(sound_file->sample_rate == 44100);
			checkSound(sound_file);
		}

		// 32-bit
		{
			SoundFileRef sound_file = WavAudioFileReader::readAudioFile(TestUtils::getTestReposDir() + "/testfiles/WAVs/mono_32bit.wav");
			testAssert(sound_file->num_channels == 1);
			testAssert(sound_file->sample_rate == 44100);
			checkSound(sound_file);
		}

		{
			SoundFileRef sound_file = WavAudioFileReader::readAudioFile(TestUtils::getTestReposDir() + "/testfiles/WAVs/stereo_32bit.wav");
			testAssert(sound_file->num_channels == 1); // We're mixing down to mono
			testAssert(sound_file->sample_rate == 44100);
			checkSound(sound_file);
		}
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}

	conPrint("WavAudioFileReader::test() done.");
}


#endif // BUILD_TESTS
