/*=====================================================================
ParcelAuction.cpp
------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "ParcelAuction.h"


#include <Exception.h>
#include <Base64.h>
#include <CryptoRNG.h>
#include <mathstypes.h>


ParcelAuction::ParcelAuction()
{}


ParcelAuction::~ParcelAuction()
{}


double ParcelAuction::computeCurrentAuctionPrice() const
{
	const double t = ((double)TimeStamp::currentTime().time - (double)auction_start_time.time) / ((double)auction_end_time.time - (double)auction_start_time.time);
	const double current_price_exact = Maths::uncheckedLerp(auction_start_price, auction_end_price, t);

	const double current_price_rounded = (int)(current_price_exact * 100) / 100.0;
	return current_price_rounded;
}


static const uint32 PARCEL_AUCTION_SERIALISATION_VERSION = 1;


void writeToStream(const ParcelAuction& a, OutStream& stream)
{
	// Write version
	stream.writeUInt32(PARCEL_AUCTION_SERIALISATION_VERSION);

	stream.writeUInt32(a.id);
	writeToStream(a.parcel_id, stream);

	stream.writeUInt32((uint32)(a.auction_state));
	a.auction_start_time.writeToStream(stream);
	a.auction_end_time.writeToStream(stream);
	stream.writeDouble(a.auction_start_price);
	stream.writeDouble(a.auction_end_price);
}


void readFromStream(InStream& stream, ParcelAuction& a)
{
	// Read version
	const uint32 v = stream.readUInt32();
	if(v > PARCEL_AUCTION_SERIALISATION_VERSION)
		throw glare::Exception("Unsupported version " + toString(v) + ", expected " + toString(PARCEL_AUCTION_SERIALISATION_VERSION) + ".");

	a.id = stream.readUInt32();
	a.parcel_id = readParcelIDFromStream(stream);

	const uint32 auction_state = stream.readUInt32();
	if(auction_state > (uint32)ParcelAuction::AuctionState_NotSold)
		throw glare::Exception("invalid auction_state");
	a.auction_state = (ParcelAuction::AuctionState)auction_state;

	a.auction_start_time.readFromStream(stream);
	a.auction_end_time  .readFromStream(stream);
	a.auction_start_price = stream.readDouble();
	a.auction_end_price   = stream.readDouble();
}
