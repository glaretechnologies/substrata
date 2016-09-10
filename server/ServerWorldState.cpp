/*=====================================================================
ServerWorldState.cpp
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-12 12:22:34 +1300
=====================================================================*/
#include "ServerWorldState.h"


#include <FileInStream.h>
#include <FileOutStream.h>
#include <Exception.h>
#include <StringUtils.h>
#include <ConPrint.h>
#include <FileUtils.h>


ServerWorldState::ServerWorldState()
{
	next_avatar_uid = UID(0);
	next_object_uid = UID(0);
}


ServerWorldState::~ServerWorldState()
{
}


static const uint32 WORLD_STATE_MAGIC_NUMBER = 487173571;
static const uint32 WORLD_STATE_SERIALISATION_VERSION = 1;
static const uint32 WORLD_OBJECT_CHUNK = 100;
static const uint32 EOS_CHUNK = 1000;


void ServerWorldState::readFromDisk(const std::string& path)
{
	conPrint("Reading world state from '" + path + "'...");

	FileInStream stream(path);

	// Read magic number
	const uint32 m = stream.readUInt32();
	if(m != WORLD_STATE_MAGIC_NUMBER)
		throw Indigo::Exception("Invalid magic number " + toString(m) + ", expected " + toString(WORLD_STATE_MAGIC_NUMBER) + ".");

	// Read version
	const uint32 v = stream.readUInt32();
	if(v != WORLD_STATE_SERIALISATION_VERSION)
		throw Indigo::Exception("Unknown version " + toString(v) + ", expected " + toString(WORLD_STATE_SERIALISATION_VERSION) + ".");

	while(1)
	{
		const uint32 chunk = stream.readUInt32();
		if(chunk == WORLD_OBJECT_CHUNK)
		{
			// Derserialise object
			WorldObjectRef world_ob = new WorldObject();
			readFromStream(stream, *world_ob);

			objects[world_ob->uid] = world_ob; // Add to object map

			next_object_uid = UID(myMax(world_ob->uid.value() + 1, next_object_uid.value()));
		}
		else if(chunk == EOS_CHUNK)
		{
			break;
		}
		else
		{
			throw Indigo::Exception("Unknown chunk type '" + toString(chunk) + "'");
		}
	}

	conPrint("Loaded " + toString(objects.size()) + " object(s).");
}


void ServerWorldState::serialiseToDisk(const std::string& path)
{
	conPrint("Saving world state to disk...");
	try
	{

		const std::string temp_path = path + "_temp";
		{
			FileOutStream stream(temp_path);

			// Write magic number
			stream.writeUInt32(WORLD_STATE_MAGIC_NUMBER);

			// Write version
			stream.writeUInt32(WORLD_STATE_SERIALISATION_VERSION);

			// Write objects
			{
				for(auto i=objects.begin(); i != objects.end(); ++i)
				{
					stream.writeUInt32(WORLD_OBJECT_CHUNK);
					writeToStream(*i->second, stream);
				}
			}

			stream.writeUInt32(EOS_CHUNK); // Write end-of-stream chunk
		}

		FileUtils::moveFile(temp_path, path);

		conPrint("Saved " + toString(objects.size()) + " object(s).");
	}
	catch(FileUtils::FileUtilsExcep& e)
	{
		throw Indigo::Exception(e.what());
	}
}


UID ServerWorldState::getNextObjectUID()
{
	UID next = next_object_uid;
	next_object_uid = UID(next_object_uid.value() + 1);
	return next;
}
