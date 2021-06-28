/*=====================================================================
EthTransaction.h
----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "UInt256.h"
#include <Platform.h>
#include <string>
#include <vector>


// See https://github.com/kvhnuke/Ethereum-Arduino/blob/master/Ethereum-Arduino/TX.h


struct EthTransaction
{
	UInt256 nonce;
	UInt256 gas_price;
	UInt256 gas_limit;
	//std::string to;
	std::vector<uint8> to;
	UInt256 value; // amount
	std::string data;

	// Signature data
	UInt256 v; // https://github.com/ethereum/EIPs/blob/master/EIPS/eip-155.md
	UInt256 r;
	UInt256 s;
};
