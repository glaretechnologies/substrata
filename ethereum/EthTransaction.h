/*=====================================================================
EthTransaction.h
----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "EthAddress.h"
#include "UInt256.h"
#include <vector>


// See https://ethereum.org/en/developers/docs/transactions/
struct EthTransaction
{
	UInt256 nonce; // How many confirmed transactions this account has sent previously
	UInt256 gas_price; // The fee the sender pays per unit of gas
	UInt256 gas_limit; // The maximum amount of gas units that can be consumed by the transaction. Units of gas represent computational steps
	EthAddress to; // the receiving address (if an externally-owned account, the transaction will transfer value. If a contract account, the transaction will execute the contract code)
	UInt256 value; // Amount of ETH to transfer from sender to recipient (in WEI, a denomination of ETH)
	std::vector<uint8> data; // Optional field to include arbitrary data

	// Signature data
	UInt256 v; // https://github.com/ethereum/EIPs/blob/master/EIPS/eip-155.md
	UInt256 r;
	UInt256 s;
};
