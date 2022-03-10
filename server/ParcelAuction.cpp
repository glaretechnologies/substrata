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
#include <limits.h>


ParcelAuction::ParcelAuction()
{
	sold_price = 0;
	auction_sold_time = TimeStamp(0);
	order_id = std::numeric_limits<uint64>::max();
	last_locked_time = TimeStamp(0);
	lock_duration = 0;
}


ParcelAuction::~ParcelAuction()
{}


// Compute auction price at the given time
double ParcelAuction::computeAuctionPrice(TimeStamp time) const
{
	// Compute total unlocked time, from the auction start time to 'time', as price doesn't decrease while auction is locked.
	// Locks are ordered by increasing time, and the lock periods are disjoint.

	/*
	
	      |----------------|======================|-------------|=====================|-------------------|
	start_time        lock_0_start           lock_0_end     lock_1_start         lock_1_end            time
	
	
	or an example where the auction is locked at time 'time'
	

	     |----------------|======================|-------------|=====================|---------------|====================|
	start_time        lock_0_start         lock_0_end      lock_1_start          lock_1_end       lock_2_start           time
	*/

	const double cur_time = (double)time.time;

	double last_resume_time = (double)auction_start_time.time; // end time of lock[i-1], or auction start time if i = 0.
	double sum_unlocked_time = 0;
	for(size_t i=0; i<auction_locks.size(); ++i)
	{
		const AuctionLock& lock = auction_locks[i];

		if(lock.created_time.time > time.time) // If lock time value is in the future relative to 'time', we are done iterating over locks.
			break;

		// Add time from last_resume_time to when this lock started
		sum_unlocked_time += (double)lock.created_time.time - last_resume_time;

		last_resume_time = (double)(lock.created_time.time + lock.lock_duration);
	}

	// If the last lock expired before cur_time, add the time from the last lock expiry to cur_time.
	if(last_resume_time < cur_time)
		sum_unlocked_time += cur_time - last_resume_time;

	const double t = sum_unlocked_time / ((double)auction_end_time.time - (double)auction_start_time.time); // Fraction of time through auction
	//const double t = ((double)time.time - (double)auction_start_time.time) / ((double)auction_end_time.time - (double)auction_start_time.time);

	const float A = 2.5;
	const double current_price_exact = auction_end_price + (auction_start_price - auction_end_price) * (std::exp(-A * t) - std::exp(-A)) / (1 - std::exp(-A));

	//const double current_price_exact = Maths::uncheckedLerp(auction_start_price, auction_end_price, t);

	const double current_price_rounded = (int)(current_price_exact * 100) / 100.0;
	return current_price_rounded;
}


double ParcelAuction::computeCurrentAuctionPrice() const
{
	return computeAuctionPrice(TimeStamp::currentTime());
}


TimeStamp ParcelAuction::getAuctionEndOrSoldTime() const
{
	if(auction_state == AuctionState_Sold)
		return auction_sold_time;
	else
		return auction_end_time;
}


static const uint64 PAYPAL_LOCK_TIME_S   = 60 * 5; // 5 mins
static const uint64 COINBASE_LOCK_TIME_S = 60 * 8; // 8 mins

static const int MAX_NUM_AUCTION_LOCKS_PER_USER = 2;
static const int MAX_TOTAL_NUM_AUCTION_LOCKS = 10;


// Returns true if locked, false if user was not allowed to lock auction due to too many locks already.
bool ParcelAuction::lockForPayPalBid(UserID locking_user_id)
{
	// User is only allowed to lock the auction twice, to avoid repeatedly locking it.
	// We will also cap the total number of locks over all users, to avoid people creating new user accounts

	// Count number of times the user has already locked this auction
	int user_num_locks = 0;
	for(size_t i=0; i<auction_locks.size(); ++i)
		if(auction_locks[i].locking_user_id == locking_user_id)
			user_num_locks++;

	if((user_num_locks < MAX_NUM_AUCTION_LOCKS_PER_USER) && ((int)auction_locks.size() < MAX_TOTAL_NUM_AUCTION_LOCKS))
	{
		last_locked_time = TimeStamp::currentTime();
		lock_duration = PAYPAL_LOCK_TIME_S;

		AuctionLock auction_lock;
		auction_lock.created_time = last_locked_time;
		auction_lock.lock_duration = lock_duration;
		auction_lock.locking_user_id = locking_user_id;

		auction_locks.push_back(auction_lock);
		return true;
	}
	else
		return false;
}


bool ParcelAuction::lockForCoinbaseBid(UserID locking_user_id)
{
	// User is only allowed to lock the auction twice, to avoid repeatedly locking it.

	// Count number of times the user has already locked this auction
	int user_num_locks = 0;
	for(size_t i=0; i<auction_locks.size(); ++i)
		if(auction_locks[i].locking_user_id == locking_user_id)
			user_num_locks++;

	if((user_num_locks < MAX_NUM_AUCTION_LOCKS_PER_USER) && ((int)auction_locks.size() < MAX_TOTAL_NUM_AUCTION_LOCKS))
	{
		last_locked_time = TimeStamp::currentTime();
		lock_duration = COINBASE_LOCK_TIME_S;

		AuctionLock auction_lock;
		auction_lock.created_time = last_locked_time;
		auction_lock.lock_duration = lock_duration;
		auction_lock.locking_user_id = locking_user_id;

		auction_locks.push_back(auction_lock);
		return true;
	}
	else
		return false;
}


bool ParcelAuction::isLocked() const
{
	if(last_locked_time.time == 0)
		return false;
	else
	{
		const int64 secs_since_last_locked = (int64)TimeStamp::currentTime().time - (int64)last_locked_time.time;

		return secs_since_last_locked <= (int64)lock_duration;
	}
}


TimeStamp ParcelAuction::lockExpiryTime() const
{
	return TimeStamp(last_locked_time.time + lock_duration);
}


bool ParcelAuction::currentlyForSale(TimeStamp now) const
{
	return (auction_state == AuctionState_ForSale) && (auction_start_time <= now) && (now <= auction_end_time);
}


static const uint32 PARCEL_AUCTION_SERIALISATION_VERSION = 6;
// v2: added screenshot_id
// v3: changed to screenshot_ids
// v4: added sold_price, auction_sold_time, order_id
// v5: added last_locked_time and lock_duration.
// v6: added auction_locks


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
	stream.writeDouble(a.sold_price);
	a.auction_sold_time.writeToStream(stream);
	stream.writeUInt64(a.order_id);

	a.last_locked_time.writeToStream(stream);
	stream.writeUInt64(a.lock_duration);

	stream.writeUInt64(a.screenshot_ids.size());
	for(size_t i=0; i<a.screenshot_ids.size(); ++i)
		stream.writeUInt64(a.screenshot_ids[i]);

	stream.writeUInt32((uint32)a.auction_locks.size());
	for(size_t i=0; i<a.auction_locks.size(); ++i)
		writeToStream(a.auction_locks[i], stream);
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
	if(auction_state > (uint32)ParcelAuction::AuctionState_Sold)
		throw glare::Exception("invalid auction_state");
	a.auction_state = (ParcelAuction::AuctionState)auction_state;

	a.auction_start_time.readFromStream(stream);
	a.auction_end_time  .readFromStream(stream);
	a.auction_start_price = stream.readDouble();
	a.auction_end_price   = stream.readDouble();
	if(v >= 4)
	{
		a.sold_price   = stream.readDouble();
		a.auction_sold_time.readFromStream(stream);
		a.order_id = stream.readUInt64();
	}

	if(v >= 5)
	{
		a.last_locked_time.readFromStream(stream);
		a.lock_duration = stream.readUInt64();
	}

	if(v == 2)
	{
		a.screenshot_ids.resize(1);
		a.screenshot_ids[0] = stream.readUInt64();
	}
	else if(v >= 3)
	{
		const uint64 num = stream.readUInt64();
		if(num > 1000)
			throw glare::Exception("invalid num screenshots");
		a.screenshot_ids.resize(num);
		for(size_t i=0; i<num; ++i)
			a.screenshot_ids[i] = stream.readUInt64();
	}

	if(v >= 6)
	{
		// Read auction_locks
		const uint32 num = stream.readUInt32();
		if(num > 10000)
			throw glare::Exception("invalid num auction_locks");
		a.auction_locks.resize(num);
		for(size_t i=0; i<num; ++i)
			readFromStream(stream, a.auction_locks[i]);
	}
}
