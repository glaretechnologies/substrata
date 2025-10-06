/*=====================================================================
Resource.cpp
------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "Resource.h"


#include <Exception.h>
#include <ConPrint.h>
#include <FileUtils.h>


static const uint32 RESOURCE_SERIALISATION_VERSION = 4;
/*
Version history:
3: Serialising state
4: local_path is now path from base_resources_dir, instead of absolute path
*/


Resource::Resource(const URLString& URL_, const std::string& raw_local_path_, State s, const UserID& owner_id_, bool external_resource_)
:	URL(URL_), 
	local_path(raw_local_path_), 
	state(s), 
	owner_id(owner_id_)/*, num_buffer_readers(0)*/,
	external_resource(external_resource_),
	file_size_B(0)
{
	if(!external_resource)
	{
		assert(!FileUtils::isPathAbsolute(local_path));
	}
}


void Resource::writeToStreamCommon(OutStream& stream) const
{
	if(!external_resource)
	{
		assert(!FileUtils::isPathAbsolute(local_path));
	}

	stream.writeStringLengthFirst(URL); 
	stream.writeStringLengthFirst(local_path);
	::writeToStream(owner_id, stream);
	stream.writeUInt32((uint32)getState());
}


static void readFromStreamCommon(InStream& stream, uint32 version, Resource& resource) // UID will have been read already
{
	resource.URL = stream.readStringLengthFirst(20000);
	if(version >= 2)
		resource.setRawLocalPath(stream.readStringLengthFirst(20000));

	if(version < 4) // Prior to version 4, local path was an absolute path.
	{
		// Remove the base resources dir path prefix
		const std::string new_local_path = FileUtils::getFilename(resource.getRawLocalPath());

		// conPrint("Rewriting resource raw local path from '" + resource.getRawLocalPath() + "' to '" + new_local_path + "'");

		resource.setRawLocalPath(new_local_path);
	}

	resource.owner_id = readUserIDFromStream(stream);
	if(version >= 3)
		resource.setState((Resource::State)stream.readUInt32());
}


void Resource::writeToStream(OutStream& stream) const
{
	// Write version
	stream.writeUInt32(RESOURCE_SERIALISATION_VERSION);

	writeToStreamCommon(stream);
}


uint32 readFromStream(InStream& stream, Resource& resource)
{
	// Read version
	const uint32 v = stream.readUInt32();
	if(v > RESOURCE_SERIALISATION_VERSION)
		throw glare::Exception("Resource readFromStream: Unsupported version " + toString(v) + ", expected " + toString(RESOURCE_SERIALISATION_VERSION) + ".");

	readFromStreamCommon(stream, v, resource);
	return v;
}


/*void Resource::addDownloadListener(const Reference<ResourceDownloadListener>& listener)
{
	listeners.insert(listener);
}


void Resource::removeDownloadListener(const Reference<ResourceDownloadListener>& listener)
{
	listeners.erase(listener);
}*/


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
