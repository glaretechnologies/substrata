/*=====================================================================
Signing.h
---------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "EthTransaction.h"
#include <string>
#include <vector>
class PrintOutput;
class ThreadMessageSink;
class DataStore;
class ServerAllWorldsState;


/*=====================================================================
Signing
-------
=====================================================================*/
namespace Signing 
{
	std::string ecrecover(const std::string& sig, const std::string& msg); // hex-encoded sig, plain text msg

	struct Signature
	{
		UInt256 r;
		UInt256 s;
		int v;
	};
	Signature sign(const std::vector<uint8>& msg_hash, const std::vector<uint8>& private_key);

	// Sets trans.v, r, s.
	void signTransaction(EthTransaction& trans, const std::vector<uint8>& private_key, int chain_id);

	void test();
};
