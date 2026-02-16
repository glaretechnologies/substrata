/*=====================================================================
GestureSettings.cpp
-------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "GestureSettings.h"


#include <utils/RandomAccessInStream.h>
#include <utils/RandomAccessOutStream.h>
#include <utils/RuntimeCheck.h>


static const uint32 SINGLE_GESTURE_SETTINGS_SERIALISATION_VERSION = 1;


void SingleGestureSettings::writeToStream(RandomAccessOutStream& stream) const
{
	// Write to stream with a length prefix.  Do this by writing to the stream, them going back and writing the length of the data we wrote.
	// Writing a length prefix allows for adding more fields later, while retaining backwards compatibility with older code that can just skip over the new fields.

	const size_t initial_write_index = stream.getWriteIndex();

	stream.writeUInt32(SINGLE_GESTURE_SETTINGS_SERIALISATION_VERSION);
	stream.writeUInt32(0); // Size of buffer will be written here later

	stream.writeStringLengthFirst(friendly_name);
	stream.writeStringLengthFirst(anim_URL);
	stream.writeUInt32(flags);
	stream.writeFloat(anim_duration);

	// Go back and write size of buffer to buffer size field
	const uint32 buffer_size = (uint32)(stream.getWriteIndex() - initial_write_index);

	std::memcpy(stream.getWritePtrAtIndex(initial_write_index + sizeof(uint32)), &buffer_size, sizeof(uint32));
}


void readSingleGestureSettingsFromStream(RandomAccessInStream& stream, SingleGestureSettings& settings)
{
	const size_t initial_read_index = stream.getReadIndex();

	/*const uint32 version =*/ stream.readUInt32();
	const size_t buffer_size = stream.readUInt32();

	checkProperty(buffer_size >= 8ul, "readSingleGestureSettingsFromStream: buffer_size was too small");
	checkProperty(buffer_size <= 1'000'000ul, "readSingleGestureSettingsFromStream: buffer_size was too large");

	settings.friendly_name = stream.readStringLengthFirst(SingleGestureSettings::MAX_NAME_SIZE);
	settings.anim_URL = stream.readStringLengthFirst(SingleGestureSettings::MAX_NAME_SIZE);
	settings.flags = stream.readUInt32();
	settings.anim_duration = stream.readFloat();
	if(!isFinite(settings.anim_duration) || (settings.anim_duration <= 0.001f)) // Sanity-check anim_duration
		settings.anim_duration = 1.f;

	// Discard any remaining unread data
	const size_t read_B = stream.getReadIndex() - initial_read_index; // Number of bytes we have read so far
	if(read_B < buffer_size)
		stream.advanceReadIndex(buffer_size - read_B);
}


struct DefaultGestureInfo
{
	const char* name; // Animation name
	bool anim_head; // Should the animation data control the head (e.g. override the procedural lookat anim)?
	bool loop; // Should the animation automatically loop.
	float anim_duration; // Animation duration (from debug output in OpenGLEngine.cpp, conPrint("anim_datum_a..  etc..").  Used to uncheck button after some period.
};

static DefaultGestureInfo default_gestures[] = {
	DefaultGestureInfo({"Clapping",						/*anim_head=*/false,	/*loop=*/true,		1.0			}),
	DefaultGestureInfo({"Dancing",						/*anim_head=*/true,		/*loop=*/true,		1.0			}),
	DefaultGestureInfo({"Dancing 2",					/*anim_head=*/true,		/*loop=*/true,		1.0			}),
	DefaultGestureInfo({"Excited",						/*anim_head=*/true,		/*loop=*/true,		6.5666666	}),
	DefaultGestureInfo({"Looking",						/*anim_head=*/true,		/*loop=*/false,		8.016666	}),
	DefaultGestureInfo({"Quick Informal Bow",			/*anim_head=*/true,		/*loop=*/false,		2.75		}),
	DefaultGestureInfo({"Rejected",						/*anim_head=*/true,		/*loop=*/false,		4.8166666	}),
	DefaultGestureInfo({"Sit",							/*anim_head=*/false,	/*loop=*/true,		1.0			}),
	DefaultGestureInfo({"Sitting On Ground",			/*anim_head=*/false,	/*loop=*/true,		1.0			}),
	DefaultGestureInfo({"Sleeping Idle",				/*anim_head=*/true,		/*loop=*/true,		1.0			}),
	DefaultGestureInfo({"Standing React Death Forward",	/*anim_head=*/true,		/*loop=*/false,		3.6833334	}),
	DefaultGestureInfo({"Waving 1",						/*anim_head=*/false,	/*loop=*/true,		1.0			}),
	DefaultGestureInfo({"Waving 2",						/*anim_head=*/false,	/*loop=*/false,		3.1833334	}),
	DefaultGestureInfo({"Yawn",							/*anim_head=*/true,		/*loop=*/false,		8.35		})
};


GestureSettings GestureSettings::defaultGestureSettings()
{
	GestureSettings settings;
	for(size_t i=0; i<staticArrayNumElems(default_gestures); ++i)
	{
		SingleGestureSettings setting;
		setting.friendly_name = default_gestures[i].name;
		setting.anim_URL = URLString(default_gestures[i].name) + ".subanim";
		setting.flags = (default_gestures[i].anim_head ? SingleGestureSettings::FLAG_ANIMATE_HEAD : 0) | (default_gestures[i].loop ? SingleGestureSettings::FLAG_LOOP : 0);
		setting.anim_duration = default_gestures[i].anim_duration;

		settings.gesture_settings.push_back(setting);
	}
	return settings;
}


bool GestureSettings::isDefaultGestureName(const std::string& name)
{
	for(size_t i=0; i<staticArrayNumElems(default_gestures); ++i)
		if(default_gestures[i].name == name)
			return true;
	return false;
}


static const uint32 GESTURE_SETTINGS_SERIALISATION_VERSION = 1;

void GestureSettings::writeToStream(RandomAccessOutStream& stream) const
{
	// Write to stream with a length prefix.  Do this by writing to the stream, them going back and writing the length of the data we wrote.
	// Writing a length prefix allows for adding more fields later, while retaining backwards compatibility with older code that can just skip over the new fields.

	const size_t initial_write_index = stream.getWriteIndex();

	stream.writeUInt32(GESTURE_SETTINGS_SERIALISATION_VERSION);
	stream.writeUInt32(0); // Size of buffer will be written here later

	stream.writeUInt32((uint32)gesture_settings.size());
	for(size_t i=0; i<gesture_settings.size(); ++i)
		gesture_settings[i].writeToStream(stream);

	// Go back and write size of buffer to buffer size field
	const uint32 buffer_size = (uint32)(stream.getWriteIndex() - initial_write_index);

	std::memcpy(stream.getWritePtrAtIndex(initial_write_index + sizeof(uint32)), &buffer_size, sizeof(uint32));
}


void readGestureSettingsFromStream(RandomAccessInStream& stream, GestureSettings& settings)
{
	const size_t initial_read_index = stream.getReadIndex();

	/*const uint32 version =*/ stream.readUInt32();
	const size_t buffer_size = stream.readUInt32();

	checkProperty(buffer_size >= 8ul, "readGestureSettingsFromStream: buffer_size was too small");
	checkProperty(buffer_size <= 1000000ul, "readGestureSettingsFromStream: buffer_size was too large");

	const uint32 num_settings = stream.readUInt32();
	checkProperty(num_settings < (uint32)GestureSettings::MAX_GESTURE_SETTINGS_SIZE, "readGestureSettingsFromStream: num_settings was too large");
	settings.gesture_settings.resize(num_settings);
	for(size_t i=0; i<settings.gesture_settings.size(); ++i)
		readSingleGestureSettingsFromStream(stream, settings.gesture_settings[i]);

	// Discard any remaining unread data
	const size_t read_B = stream.getReadIndex() - initial_read_index; // Number of bytes we have read so far
	if(read_B < buffer_size)
		stream.advanceReadIndex(buffer_size - read_B);
}
