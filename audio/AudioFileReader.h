/*=====================================================================
AudioFileReader.h
-----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "AudioEngine.h"
#include <vector>
#include <string>


namespace glare
{


/*=====================================================================
AudioFileReader
---------------
Read the contents of a possibly-compressed audio file into memory.
=====================================================================*/
class AudioFileReader
{
public:
	static SoundFileRef readAudioFile(const std::string& path);

	static void test();
};


}
