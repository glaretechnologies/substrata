/*=====================================================================
WorldDetails.cpp
----------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#include "WorldDetails.h"


#include <utils/RandomAccessInStream.h>
#include <utils/RandomAccessOutStream.h>


static const uint32 WORLD_DETAILS_SERIALISATON_VERSION = 1;


void WorldDetails::writeToNetworkStream(RandomAccessOutStream& stream) const
{
	// Write to stream with a length prefix.  Do this by writing to the stream, them going back and writing the length of the data we wrote.
	// Writing a length prefix allows for adding more fields later, while retaining backwards compatibility with older code that can just skip over the new fields.

	const size_t initial_write_index = stream.getWriteIndex();

	stream.writeUInt32(WORLD_DETAILS_SERIALISATON_VERSION);
	stream.writeUInt32(0); // Size of buffer will be written here later

	::writeToStream(owner_id, stream);
	created_time.writeToStream(stream);
	stream.writeStringLengthFirst(name);
	stream.writeStringLengthFirst(description);

	// Go back and write size of buffer to buffer size field
	const uint32 buffer_size = (uint32)(stream.getWriteIndex() - initial_write_index);

	std::memcpy(stream.getWritePtrAtIndex(initial_write_index + sizeof(uint32)), &buffer_size, sizeof(uint32));
}


void readWorldDetailsFromNetworkStream(RandomAccessInStream& stream, WorldDetails& details)
{
	const size_t initial_read_index = stream.getReadIndex();

	/*const uint32 version =*/ stream.readUInt32();
	const size_t buffer_size = stream.readUInt32();

	checkProperty(buffer_size >= 8ul, "readWorldDetailsFromStream: buffer_size was too small");
	checkProperty(buffer_size <= 1000000ul, "readWorldDetailsFromStream: buffer_size was too large");

	details.owner_id = readUserIDFromStream(stream);
	details.created_time.readFromStream(stream);
	details.name = stream.readStringLengthFirst(WorldDetails::MAX_NAME_SIZE);
	details.description = stream.readStringLengthFirst(WorldDetails::MAX_DESCRIPTION_SIZE);

	// Discard any remaining unread data
	const size_t read_B = stream.getReadIndex() - initial_read_index; // Number of bytes we have read so far
	if(read_B < buffer_size)
		stream.advanceReadIndex(buffer_size - read_B);
}
