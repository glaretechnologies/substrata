/*=====================================================================
NewsPost.cpp
------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "NewsPost.h"


#include <Exception.h>
#include <RuntimeCheck.h>
#include <BufferOutStream.h>
#include <BufferInStream.h>
#include <mathstypes.h>


NewsPost::NewsPost()
{
	id = 0;
	created_time = TimeStamp(0);
	last_modified_time = TimeStamp(0);
	state = State_draft;
}


NewsPost::~NewsPost()
{}


std::string NewsPost::stateString(State s)
{
	switch(s)
	{
		case State_draft: return "Draft";
		case State_published: return "Published";
		case State_deleted: return "Deleted";
		default: return "[Unknown]";
	}
};


static const uint32 NEWS_POST_SERIALISATION_VERSION = 1;


void writeToStream(const NewsPost& post, OutStream& stream)
{
	stream.writeUInt32(NEWS_POST_SERIALISATION_VERSION);
	stream.writeUInt64(post.id);
	writeToStream(post.creator_id, stream);
	post.created_time.writeToStream(stream);
	post.last_modified_time.writeToStream(stream);
	stream.writeStringLengthFirst(post.title);
	stream.writeStringLengthFirst(post.content);
	stream.writeStringLengthFirst(post.thumbnail_URL);
	stream.writeUInt32((uint32)post.state);
}


void readNewsPostFromStream(InStream& stream, NewsPost& post)
{
	/*const uint32 version =*/ stream.readUInt32();
	post.id = stream.readUInt64();
	post.creator_id = readUserIDFromStream(stream);
	post.created_time.readFromStream(stream);
	post.last_modified_time.readFromStream(stream);
	post.title = stream.readStringLengthFirst(100000);
	post.content = stream.readStringLengthFirst(100000);
	post.thumbnail_URL = stream.readStringLengthFirst(100000);

	uint32 s = stream.readUInt32();
	if(s > NewsPost::State_deleted)
		throw glare::Exception("Invalid state");
	post.state = (NewsPost::State)s;
}
