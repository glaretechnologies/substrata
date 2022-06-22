/*=====================================================================
StreamerThread.h
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include <utils/MessageableThread.h>
#include <utils/AtomicInt.h>
#include <utils/Vector.h>


namespace glare
{


class AudioEngine;


/*=====================================================================
StreamerThread
--------------
Streams data from mp3 decoders to AudioSource buffers.
=====================================================================*/
class StreamerThread : public MessageableThread
{
public:
	StreamerThread(AudioEngine* audio_engine);

	virtual void doRun() override;

	virtual void kill() override { die = 1; }


	AudioEngine* audio_engine;
	glare::AtomicInt die;

	// Temp buffers
	js::Vector<float, 16> samples;
	js::Vector<float, 16> mono_samples;
};


} // end namespace glare
