/*=====================================================================
WavAudioFileReader.h
--------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "AudioFileReader.h"


namespace glare
{


/*=====================================================================
MP3AudioFileReader
------------------

=====================================================================*/
class WavAudioFileReader
{
public:
	static SoundFileRef readAudioFile(const std::string& path);

	static void test();
};


} // end namespace glare
