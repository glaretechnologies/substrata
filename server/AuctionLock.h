/*=====================================================================
AuctionLock.h
-------------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#pragma once


#include "../shared/TimeStamp.h"
#include "../shared/UserID.h"
#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include <string>
#include <OutStream.h>
#include <InStream.h>
#include <vector>
#include <array>


/*=====================================================================
AuctionLock
-----------

=====================================================================*/
class AuctionLock
{
public:
	AuctionLock();
	~AuctionLock();

	TimeStamp created_time;
	uint64 lock_duration; // seconds
	
	UserID locking_user_id; // ID of user who locked the auction

	// TODO: record IP address of locking user as well.
};



void writeToStream(const AuctionLock& auction_lock, OutStream& stream);
void readFromStream(InStream& stream, AuctionLock& auction_lock);
