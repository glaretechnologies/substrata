/*=====================================================================
ParcelAuction.h
----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "../shared/TimeStamp.h"
#include "../shared/ParcelID.h"
#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include <OutStream.h>
#include <InStream.h>


/*=====================================================================
ParcelAuction
--------------

=====================================================================*/
class ParcelAuction : public ThreadSafeRefCounted
{
public:
	ParcelAuction();
	~ParcelAuction();

	double computeCurrentAuctionPrice() const;

	enum AuctionState
	{
		//AuctionState_NotForSale = 0, // Not currently on auction
		AuctionState_ForSale = 0, // on auction
		AuctionState_Sold = 1, // sold at the auction
		AuctionState_NotSold = 2 // didn't meet reserve price
	};

	uint32 id;
	ParcelID parcel_id;

	AuctionState auction_state;
	TimeStamp auction_start_time;
	TimeStamp auction_end_time;
	double auction_start_price;
	double auction_end_price;

	std::vector<uint64> screenshot_ids;
};


typedef Reference<ParcelAuction> ParcelAuctionRef;


void writeToStream(const ParcelAuction& a, OutStream& stream);
void readFromStream(InStream& stream, ParcelAuction& a);
