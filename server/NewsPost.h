/*=====================================================================
NewsPost.h
----------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include <TimeStamp.h>
#include "../shared/UserID.h"
#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include <OutStream.h>
#include <InStream.h>
#include <DatabaseKey.h>


/*=====================================================================
NewsPost
--------

=====================================================================*/
class NewsPost : public ThreadSafeRefCounted
{
public:
	NewsPost();
	~NewsPost();

	uint64 id;
	UserID creator_id;
	TimeStamp created_time;
	TimeStamp last_modified_time;
	std::string title;
	std::string content;
	std::string thumbnail_URL;

	enum State
	{
		State_draft = 0,
		State_published = 1,
		State_deleted = 2
	};
	State state;

	static std::string stateString(State s);

	DatabaseKey database_key;
};


typedef Reference<NewsPost> NewsPostRef;


void writeToStream(const NewsPost& post, OutStream& stream);
void readNewsPostFromStream(InStream& stream, NewsPost& post);


struct NewsPostRefHash
{
	size_t operator() (const NewsPostRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};
