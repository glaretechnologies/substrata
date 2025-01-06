/*=====================================================================
SubEvent.cpp
------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#include "SubEvent.h"


#include "../shared/WorldObject.h"
#include <utils/Exception.h>
#include <utils/StringUtils.h>
#include <utils/ConPrint.h>
#include <utils/BufferOutStream.h>
#include <utils/BufferInStream.h>
#include <utils/RuntimeCheck.h>


SubEvent::SubEvent()
{
	db_dirty = false;
}


SubEvent::~SubEvent()
{
}


static const uint32 SUBEVENT_CHUNK_SERIALISATION_VERSION = 2;


void SubEvent::writeToStream(RandomAccessOutStream& stream) const
{
	// Write to stream with a length prefix.  Do this by writing to the stream, them going back and writing the length of the data we wrote.
	// Writing a length prefix allows for adding more fields later, while retaining backwards compatibility with older code that can just skip over the new fields.

	const size_t initial_write_index = stream.getWriteIndex();

	stream.writeUInt32(SUBEVENT_CHUNK_SERIALISATION_VERSION);
	stream.writeUInt32(0); // Size of buffer will be written here later

	stream.writeUInt64(id);
	stream.writeStringLengthFirst(world_name);
	::writeToStream(parcel_id, stream);
	created_time.writeToStream(stream);
	last_modified_time.writeToStream(stream);
	start_time.writeToStream(stream);
	end_time.writeToStream(stream);
	::writeToStream(creator_id, stream);
	stream.writeStringLengthFirst(title);
	stream.writeStringLengthFirst(description);
	stream.writeUInt32((uint32)state);

	// Write attendee_ids
	stream.writeUInt64(attendee_ids.size());
	for(auto it=attendee_ids.begin(); it != attendee_ids.end(); ++it)
		::writeToStream(*it, stream);

	// Go back and write size of buffer to buffer size field
	const uint32 buffer_size = (uint32)(stream.getWriteIndex() - initial_write_index);

	std::memcpy(stream.getWritePtrAtIndex(initial_write_index + sizeof(uint32)), &buffer_size, sizeof(uint32));
}


std::string SubEvent::stateString(State s)
{
	switch(s)
	{
		case State_draft: return "Draft";
		case State_published: return "Published";
		case State_deleted: return "Deleted";
		default: return "[Unknown]";
	}
}


void readSubEventFromStream(RandomAccessInStream& stream, SubEvent& event)
{
	const size_t initial_read_index = stream.getReadIndex();

	const uint32 version = stream.readUInt32();
	const size_t buffer_size = stream.readUInt32();

	checkProperty(buffer_size >= 8ul, "readSubEventFromStream: buffer_size was too small");
	checkProperty(buffer_size <= 1000000ul, "readSubEventFromStream: buffer_size was too large");

	event.id = stream.readUInt64();
	event.world_name = stream.readStringLengthFirst(SubEvent::MAX_WORLD_NAME_SIZE);
	event.parcel_id = ::readParcelIDFromStream(stream);
	event.created_time.readFromStream(stream);
	event.last_modified_time.readFromStream(stream);
	event.start_time.readFromStream(stream);
	event.end_time.readFromStream(stream);
	event.creator_id = readUserIDFromStream(stream);
	event.title = stream.readStringLengthFirst(SubEvent::MAX_TITLE_SIZE);
	event.description = stream.readStringLengthFirst(SubEvent::MAX_DESCRIPTION_SIZE);
	event.state = (SubEvent::State)stream.readUInt32();

	if(version >= 2)
	{
		// Read attendee_ids
		const uint64 num_attendee_ids = stream.readUInt64();
		if(num_attendee_ids > 64 * 1024 * 1024)
			throw glare::Exception("invalid num attendee_ids");
	
		for(size_t i=0; i<num_attendee_ids; ++i)
			event.attendee_ids.insert(readUserIDFromStream(stream));
	}

	// Discard any remaining unread data
	const size_t read_B = stream.getReadIndex() - initial_read_index; // Number of bytes we have read so far
	if(read_B < buffer_size)
		stream.advanceReadIndex(buffer_size - read_B);
}


#if BUILD_TESTS


#include <utils/TestUtils.h>


void SubEvent::test()
{
	{
		SubEvent ev;
		ev.world_name = "a";
		ev.title = "b";
		ev.description = "c";
		ev.state = State_deleted;
		ev.attendee_ids.insert(UserID(100));
		ev.attendee_ids.insert(UserID(101));
		ev.attendee_ids.insert(UserID(102));

		BufferOutStream stream;
		ev.writeToStream(stream);

		BufferInStream instream(stream.buf);

		SubEvent ev2;
		readSubEventFromStream(instream, ev2);
		testAssert(ev2.world_name == ev.world_name);
		testAssert(ev2.title == ev.title);
		testAssert(ev2.description == ev.description);
		testAssert(ev2.state == ev.state);
		testAssert(ev2.attendee_ids == ev.attendee_ids);
	}

	{
		SubEvent ev;
		
		BufferOutStream stream;
		ev.writeToStream(stream);

		BufferInStream instream(stream.buf);

		SubEvent ev2;
		readSubEventFromStream(instream, ev2);
		testAssert(ev2.world_name == ev.world_name);
		testAssert(ev2.title == ev.title);
		testAssert(ev2.description == ev.description);
		testAssert(ev2.state == ev.state);
		testAssert(ev2.attendee_ids == ev.attendee_ids);
	}
}


#endif
