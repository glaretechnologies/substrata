/*=====================================================================
SubEvent.h
----------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#pragma once


#include "../shared/TimeStamp.h"
#include "../shared/ParcelID.h"
#include "../shared/UserID.h"
#include "TimeStamp.h"
#include <utils/RandomAccessInStream.h>
#include <utils/RandomAccessOutStream.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/DatabaseKey.h>
#include <utils/Reference.h>
#include <unordered_set>


/*=====================================================================
SubEvent
--------
Substrata social event
=====================================================================*/
class SubEvent : public ThreadSafeRefCounted
{
public:
	SubEvent();
	~SubEvent();

	void writeToStream(RandomAccessOutStream& stream) const;

	uint64 id;
	std::string world_name;
	ParcelID parcel_id;
	UserID creator_id;
	TimeStamp created_time;
	TimeStamp last_modified_time;
	TimeStamp start_time;
	TimeStamp end_time;
	std::string title;
	std::string description;
	std::unordered_set<UserID, UserIDHasher> attendee_ids;

	enum State
	{
		State_draft = 0,
		State_published = 1,
		State_deleted = 2
	};
	State state;


	static const size_t MAX_WORLD_NAME_SIZE                 = 1000;
	static const size_t MAX_TITLE_SIZE                      = 1000;
	static const size_t MAX_DESCRIPTION_SIZE                = 10000;

	static std::string stateString(State s);

	static void test();

	DatabaseKey database_key;
	bool db_dirty; // If true, there is a change that has not been saved to the DB.

private:
	GLARE_DISABLE_COPY(SubEvent)
};

typedef Reference<SubEvent> SubEventRef;


void readSubEventFromStream(RandomAccessInStream& stream, SubEvent& event);


struct SubEventRefHash
{
	size_t operator() (const SubEventRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};
