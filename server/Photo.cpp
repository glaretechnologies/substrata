/*=====================================================================
Photo.cpp
---------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#include "Photo.h"


#include <utils/RandomAccessInStream.h>
#include <utils/RandomAccessOutStream.h>
#include <utils/Exception.h>
#include <utils/StringUtils.h>


Photo::Photo()
{
	flags = 0;
	state = State_published;
}


Photo::~Photo()
{}


static const uint32 PHOTO_SERIALISATION_VERSION = 2;
// v2: added state


void Photo::writeToStream(RandomAccessOutStream& stream) const
{
	// Write to stream with a length prefix.  Do this by writing to the stream, them going back and writing the length of the data we wrote.
	// Writing a length prefix allows for adding more fields later, while retaining backwards compatibility with older code that can just skip over the new fields.

	const size_t initial_write_index = stream.getWriteIndex();

	stream.writeUInt32(PHOTO_SERIALISATION_VERSION);
	stream.writeUInt32(0); // Size of buffer will be written here later

	
	stream.writeUInt64(id);
	::writeToStream(creator_id, stream);
	::writeToStream(parcel_id, stream);
	created_time.writeToStream(stream);
	stream.writeData(&cam_pos, sizeof(cam_pos));
	stream.writeData(&cam_angles, sizeof(cam_angles));
	runtimeCheck(caption.size() <= 10000);
	stream.writeStringLengthFirst(caption);
	stream.writeUInt32(flags);

	runtimeCheck(world_name.size() <= 10000);
	stream.writeStringLengthFirst(world_name);
	
	runtimeCheck(local_filename.size() <= 10000);
	stream.writeStringLengthFirst(local_filename);
	
	runtimeCheck(local_thumbnail_filename.size() <= 10000);
	stream.writeStringLengthFirst(local_thumbnail_filename);

	runtimeCheck(local_midsize_filename.size() <= 10000);
	stream.writeStringLengthFirst(local_midsize_filename);

	stream.writeUInt32((uint32)state);

	
	// Go back and write size of buffer to buffer size field
	const uint32 buffer_size = (uint32)(stream.getWriteIndex() - initial_write_index);

	std::memcpy(stream.getWritePtrAtIndex(initial_write_index + sizeof(uint32)), &buffer_size, sizeof(uint32));
}


void readPhotoFromStream(RandomAccessInStream& stream, Photo& photo)
{
	const size_t initial_read_index = stream.getReadIndex();

	const uint32 version = stream.readUInt32();
	const size_t buffer_size = stream.readUInt32();

	checkProperty(buffer_size >= 8ul, "readPhotoFromStream: buffer_size was too small");
	checkProperty(buffer_size <= 1000000ul, "readPhotoFromStream: buffer_size was too large");

	photo.id = stream.readUInt64();
	photo.creator_id = ::readUserIDFromStream(stream);
	photo.parcel_id = ::readParcelIDFromStream(stream);
	photo.created_time.readFromStream(stream);
	stream.readData(&photo.cam_pos, sizeof(photo.cam_pos));
	stream.readData(&photo.cam_angles, sizeof(photo.cam_angles));
	photo.caption = stream.readStringLengthFirst(10000);
	photo.flags = stream.readUInt32();
	photo.world_name = stream.readStringLengthFirst(10000);

	photo.local_filename = stream.readStringLengthFirst(10000);
	photo.local_thumbnail_filename = stream.readStringLengthFirst(10000);
	photo.local_midsize_filename = stream.readStringLengthFirst(10000);

	if(version >= 2)
	{
		const uint32 s = stream.readUInt32();
		if(s > Photo::State_deleted)
			throw glare::Exception("Invalid state");
		photo.state = (Photo::State)s;
	}

	// Discard any remaining unread data
	const size_t read_B = stream.getReadIndex() - initial_read_index; // Number of bytes we have read so far
	if(read_B < buffer_size)
		stream.advanceReadIndex(buffer_size - read_B);
}
