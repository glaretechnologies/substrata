/*=====================================================================
Resource.cpp
------------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#include "Resource.h"


#include <Exception.h>


static const uint32 RESOURCE_SERIALISATION_VERSION = 2;


static void writeToStreamCommon(const Resource& resource, OutStream& stream)
{
	stream.writeStringLengthFirst(resource.URL); 
	stream.writeStringLengthFirst(resource.getLocalPath());
	writeToStream(resource.owner_id, stream);
}


static void readFromStreamCommon(InStream& stream, uint32 version, Resource& resource) // UID will have been read already
{
	resource.URL = stream.readStringLengthFirst(10000);
	if(version >= 2)
		resource.setLocalPath(stream.readStringLengthFirst(10000));
	resource.owner_id = readUserIDFromStream(stream);
}


void writeToStream(const Resource& resource, OutStream& stream)
{
	// Write version
	stream.writeUInt32(RESOURCE_SERIALISATION_VERSION);

	writeToStreamCommon(resource, stream);
}


void readFromStream(InStream& stream, Resource& resource)
{
	// Read version
	const uint32 v = stream.readUInt32();
	if(v > RESOURCE_SERIALISATION_VERSION)
		throw Indigo::Exception("Resource readFromStream: Unsupported version " + toString(v) + ", expected " + toString(RESOURCE_SERIALISATION_VERSION) + ".");

	readFromStreamCommon(stream, v, resource);
}


//void writeToNetworkStream(const Resource& resource, OutStream& stream)
//{
//	writeToStreamCommon(resource, stream);
//}
//
//
//void readFromNetworkStream/*GivenID*/(InStream& stream, Resource& resource) // UID will have been read already
//{
//	readFromStreamCommon(stream, resource);
//}
