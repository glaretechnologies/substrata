/*=====================================================================
WorldDetails.h
--------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#pragma once


#include "UserID.h"
#include <utils/TimeStamp.h>
#include <string>
class RandomAccessInStream;
class RandomAccessOutStream;


/*=====================================================================
WorldDetails
------------

=====================================================================*/
class WorldDetails
{
public:
	void writeToNetworkStream(RandomAccessOutStream& stream) const;

	UserID owner_id;
	TimeStamp created_time;
	std::string name;
	std::string description;

	static const size_t MAX_NAME_SIZE               = 1000;
	static const size_t MAX_DESCRIPTION_SIZE        = 10000;
};


void readWorldDetailsFromNetworkStream(RandomAccessInStream& stream, WorldDetails& details);
