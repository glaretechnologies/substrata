/*=====================================================================
ParcelAuction.h
----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "AuctionLock.h"
#include "../shared/TimeStamp.h"
#include "../shared/ParcelID.h"
#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include <OutStream.h>
#include <InStream.h>
#include <DatabaseKey.h>


/*=====================================================================
ParcelAuction
--------------

=====================================================================*/
class ParcelAuction : public ThreadSafeRefCounted
{
public:
	ParcelAuction();
	~ParcelAuction();

	double computeAuctionPrice(TimeStamp time) const;
	double computeCurrentAuctionPrice() const;

	// Get the time the auction ended, or if it was sold, when it was sold.
	TimeStamp getAuctionEndOrSoldTime() const;

	// Returns true if locked, false if user was not allowed to lock auction due to too many locks already.
	bool lockForPayPalBid(UserID locking_user_id);
	bool lockForCoinbaseBid(UserID locking_user_id);

	bool isLocked() const;

	TimeStamp lockExpiryTime() const;

	bool currentlyForSale(TimeStamp now = TimeStamp::currentTime()) const;

	// An auction is for sale if it has state AuctionState_ForSale and current time is <= auction_end_time.
	// If current time is > auction_end_time then it didn't meet reserve price and is not for sale.
	enum AuctionState
	{
		//AuctionState_NotForSale = 0, // Not currently on auction
		AuctionState_ForSale = 0, // on auction
		AuctionState_Sold = 1 // sold at the auction
		//AuctionState_NotSold = 2 // didn't meet reserve price
	};

	uint32 id;
	ParcelID parcel_id;

	AuctionState auction_state;
	TimeStamp auction_start_time;
	TimeStamp auction_end_time;
	double auction_start_price;
	double auction_end_price;
	double sold_price; // Set if state = AuctionState_Sold.
	TimeStamp auction_sold_time; // Set if state = AuctionState_Sold.
	uint64 order_id; // Order which bought the parcel. Set if state = AuctionState_Sold.

	TimeStamp last_locked_time; // Most recent time when the auction was locked (due to a bid in progress), or 0 if never locked.
	uint64 lock_duration; // seconds

	std::vector<uint64> screenshot_ids;

	std::vector<AuctionLock> auction_locks; // History of locks

	DatabaseKey database_key;
};


typedef Reference<ParcelAuction> ParcelAuctionRef;


void writeToStream(const ParcelAuction& a, OutStream& stream);
void readFromStream(InStream& stream, ParcelAuction& a);


struct ParcelAuctionRefHash
{
	size_t operator() (const ParcelAuctionRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};

