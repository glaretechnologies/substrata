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
	
	// Throws glare::Exception on failure.
	static const std::string getOwnerOfERC721Token(const std::string& contract_address, uint32 token_id);


	static void test();

};
