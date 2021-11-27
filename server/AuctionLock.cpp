/*=====================================================================
AuctionLock.cpp
---------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "AuctionLock.h"


#include <Exception.h>
#include <StringUtils.h>
#include <FileUtils.h>
#include <ConPrint.h>


AuctionLock::AuctionLock()
{
}


AuctionLock::~AuctionLock()
{

}


static const uint32 AUCTION_LOCK_SERIALISATION_VERSION = 1;


void writeToStream(const AuctionLock& auction_lock, OutStream& stream)
{
	// Write version
	stream.writeUInt32(AUCTION_LOCK_SERIALISATION_VERSION);

	auction_lock.created_time.writeToStream(stream);
	stream.writeUInt64(auction_lock.lock_duration);
	writeToStream(auction_lock.locking_user_id, stream);
}


void readFromStream(InStream& stream, AuctionLock& auction_lock)
{
	// Read version
	const uint32 v = stream.readUInt32();
	if(v > AUCTION_LOCK_SERIALISATION_VERSION)
		throw glare::Exception("AuctionLock: Unsupported version " + toString(v) + ", expected " + toString(AUCTION_LOCK_SERIALISATION_VERSION) + ".");

	auction_lock.created_time.readFromStream(stream);
	auction_lock.lock_duration = stream.readUInt64();
	auction_lock.locking_user_id = readUserIDFromStream(stream);
}
