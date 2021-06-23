/*=====================================================================
Infura.h
----------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include <MessageableThread.h>
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


	static std::string makeUInt256BigEndianString(uint32 x);
	
	// token_id is a 256 bit unsigned integer, encoded in a big-endian order as a 32 byte binary string.
	// Throws glare::Exception on failure.
	static std::string getOwnerOfERC721Token(const std::string& contract_address, const std::string& token_id);


	static void test();

};
