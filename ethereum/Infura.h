/*=====================================================================
Infura.h
----------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "UInt256.h"
#include <Platform.h>
#include <MyThread.h>
#include <EventFD.h>
#include <ThreadManager.h>
#include <SocketInterface.h>
#include <set>
#include <string>
#include <vector>
class PrintOutput;
class ThreadMessageSink;
class DataStore;
class ServerAllWorldsState;


/*=====================================================================
Infura
------

=====================================================================*/
class Infura 
{
public:

	static std::string functionSelector(const std::string& s);

	//static std::vector<uint8> encodeFunctionCall(const std::string& sig)

	//static std::string makeUInt256BigEndianString(uint32 x);
	
	// token_id is a 256 bit unsigned integer, encoded in a big-endian order as a 32 byte binary string.
	// Throws glare::Exception on failure.
	static std::string getOwnerOfERC721Token(const std::string& contract_address, const UInt256& token_id);

	// network should be one of "mainnet" etc..
	// Returns transaction hash
	static UInt256 sendRawTransaction(const std::string& network, const std::vector<uint8>& pre_signed_transaction);


	static uint64 getCurrentGasPrice(const std::string& network); // In wei (1 ETH = 10^-18 wei)

	static void test();

};
