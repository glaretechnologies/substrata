/*=====================================================================
RLP.h
-----
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "UInt256.h"
#include "EthTransaction.h"
#include <Platform.h>
#include <string>
#include <vector>


/*=====================================================================
RLP
---
Recursive Length Prefix - a binary encoding used by Ethereum.
See https://eth.wiki/fundamentals/rlp
=====================================================================*/
namespace RLP 
{
	std::vector<uint8> encode(const std::string& s);

	std::vector<uint8> encode(const std::vector<uint8>& data);

	std::vector<uint8> transactionHash(EthTransaction& trans, const UInt256& chain_id);

	// Transaction should be signed with Signing::signTransaction(), e.g. v, r, and s fields should be set.
	std::vector<uint8> encodeSignedTransaction(const EthTransaction& trans);

	void test();
};
