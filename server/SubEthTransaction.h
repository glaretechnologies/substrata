/*=====================================================================
SubEthTransaction.h
-------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "../shared/TimeStamp.h"
#include "../shared/ParcelID.h"
#include "../shared/UserID.h"
#include "../ethereum/UInt256.h"
#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include <OutStream.h>
#include <InStream.h>


/*=====================================================================
SubEthTransaction
-----------------
Substrata ethereum transaction
=====================================================================*/
class SubEthTransaction : public ThreadSafeRefCounted
{
public:
	SubEthTransaction();
	~SubEthTransaction();

	enum State
	{
		State_New = 0,
		State_Submitted = 1,
		State_Completed = 2
	};

	static std::string statestring(State s);

	uint64 id;

	TimeStamp created_time;

	State state;

	UserID initiating_user_id;

	uint64 nonce; // A scalar value equal to the number of (successful) transactions sent from this address

	std::string submission_error_message;
	UInt256 transaction_hash;

	// For minting transaction:
	ParcelID parcel_id;
	std::string user_eth_address; // Address that token ownership will be assigned to.  In hex form with 0x prefix.
};


typedef Reference<SubEthTransaction> SubEthTransactionRef;


void writeToStream(const SubEthTransaction& t, OutStream& stream);
void readFromStream(InStream& stream, SubEthTransaction& t);
