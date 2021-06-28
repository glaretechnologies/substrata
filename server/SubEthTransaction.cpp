/*=====================================================================
SubEthTransaction.cpp
---------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "SubEthTransaction.h"


#include <Exception.h>
#include <Base64.h>
#include <CryptoRNG.h>
#include <mathstypes.h>
#include <limits.h>


SubEthTransaction::SubEthTransaction()
{
	nonce = 0;
}


SubEthTransaction::~SubEthTransaction()
{}


std::string SubEthTransaction::statestring(State s)
{
	switch(s)
	{
	case State_New: return "New";
	case State_Submitted: return "Submitted";
	case State_Completed: return "Completed";
	};
	assert(0);
	return "[Unknown]";
}


static const uint32 SUB_ETH_TRANSACTION_SERIALISATION_VERSION = 2;
// 2: Added submission_error_message, transaction_hash, nonce


void writeToStream(const SubEthTransaction& trans, OutStream& stream)
{
	// Write version
	stream.writeUInt32(SUB_ETH_TRANSACTION_SERIALISATION_VERSION);

	stream.writeUInt64(trans.id);
	trans.created_time.writeToStream(stream);
	stream.writeUInt32((uint32)trans.state);
	writeToStream(trans.initiating_user_id, stream);
	stream.writeUInt64(trans.nonce);
	stream.writeStringLengthFirst(trans.submission_error_message);
	writeToStream(trans.transaction_hash, stream);

	writeToStream(trans.parcel_id, stream);
	stream.writeStringLengthFirst(trans.user_eth_address);
}


void readFromStream(InStream& stream, SubEthTransaction& trans)
{
	// Read version
	const uint32 v = stream.readUInt32();
	if(v > SUB_ETH_TRANSACTION_SERIALISATION_VERSION)
		throw glare::Exception("Unsupported version " + toString(v) + ", expected " + toString(SUB_ETH_TRANSACTION_SERIALISATION_VERSION) + ".");

	trans.id = stream.readUInt64();
	trans.created_time.readFromStream(stream);
	trans.state = (SubEthTransaction::State)stream.readUInt32();
	trans.initiating_user_id = readUserIDFromStream(stream);
	if(v >= 2)
	{
		trans.nonce = stream.readUInt64();
		trans.submission_error_message = stream.readStringLengthFirst(10000);
		trans.transaction_hash = readUInt256FromStream(stream);
	}
	trans.parcel_id = readParcelIDFromStream(stream);
	trans.user_eth_address = stream.readStringLengthFirst(10000);
}
