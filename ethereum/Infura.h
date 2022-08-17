/*=====================================================================
Infura.h
--------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "UInt256.h"
#include "EthAddress.h"
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


struct InfuraCredentials
{
	std::string infura_project_id;
	std::string infura_project_secret;
};


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
	static EthAddress getOwnerOfERC721Token(const InfuraCredentials& credentials, const std::string& network, const EthAddress& contract_address, const UInt256& token_id);

	static EthAddress getContractOwner(const InfuraCredentials& credentials, const std::string& network, const EthAddress& contract_address);


	static UInt256 transferContractOwnership(const InfuraCredentials& credentials, const std::string& network, const EthAddress& contract_address, const EthAddress& new_owner, int nonce);

	// Execute an ethereum function call on the given smart contract.
	// The function must take zero or more uint256 args, and should return an eth address.
	static EthAddress doEthCallReturningAddress(const InfuraCredentials& credentials, const std::string& network, const EthAddress& contract_address, const std::string& func_name, const std::vector<UInt256>& uint256_args);

	static UInt256 doEthCallReturningTransactionHash(const InfuraCredentials& credentials, const std::string& network, const EthAddress& contract_address, const std::string& func_name, const std::vector<EthAddress>& address_args);

	// network should be one of "mainnet" etc..
	// Returns transaction hash
	static UInt256 sendRawTransaction(const InfuraCredentials& credentials, const std::string& network, const std::vector<uint8>& pre_signed_transaction);

	// Returns transaction hash
	static UInt256 deployContract(const InfuraCredentials& credentials, const std::string& network, const std::vector<uint8>& compiled_contract);

	struct TransactionReceipt
	{
		EthAddress contract_address;
	};
	static TransactionReceipt getTransactionReceipt(const InfuraCredentials& credentials, const std::string& network, const UInt256& transaction_hash);


	static uint64 getCurrentGasPrice(const InfuraCredentials& credentials, const std::string& network); // In wei (1 ETH = 10^-18 wei)

	static void test();

};
