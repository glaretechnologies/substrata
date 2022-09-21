/*=====================================================================
Infura.cpp
----------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "Infura.h"


#include "EthTransaction.h"
#include "Signing.h"
#include "RLP.h"
#include <ConPrint.h>
#include <Clock.h>
#include <AESEncryption.h>
#include <SHA256.h>
#include <Base64.h>
#include <Exception.h>
#include <MySocket.h>
#include <Lock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <KillThreadMessage.h>
#include <Parser.h>
#include <MemMappedFile.h>
#include <HTTPClient.h>
#include <JSONParser.h>
#include <Keccak256.h>
#include <FileUtils.h>


static std::string base64Encode(const std::string& s)
{
	std::string res;
	Base64::encode(s.data(), s.size(), res);
	return res;
}


static std::string stringToHexWith0xPrefix(const std::string& s)
{
	return "0x" + StringUtils::convertByteArrayToHexString((const uint8_t*)s.data(), s.size());
}


static std::string toHexWith0xPrefix(const std::vector<uint8>& s)
{
	return "0x" + StringUtils::convertByteArrayToHexString(s.data(), s.size());
}


// See https://docs.soliditylang.org/en/develop/abi-spec.html#function-selector
std::string Infura::functionSelector(const std::string& s)
{
	uint8 digest[32];
	Keccak256::hash(s, digest);
	return std::string(digest, digest + 4); // Take first 4 bytes
}


// Queries the Ethereum blockchain via Infura APIs, to get the owning address of an ERC721 token,
// or any smart contract that implements the ownerOf method.
EthAddress Infura::getOwnerOfERC721Token(const InfuraCredentials& credentials, const std::string& network, const EthAddress& contract_address, const UInt256& token_id)
{
	return doEthCallReturningAddress(credentials, network, contract_address, "ownerOf", std::vector<UInt256>(1, token_id));
}


EthAddress Infura::getContractOwner(const InfuraCredentials& credentials, const std::string& network, const EthAddress& contract_address)
{
	return doEthCallReturningAddress(credentials, network, contract_address, "owner", std::vector<UInt256>());
}


UInt256 Infura::transferContractOwnership(const InfuraCredentials& credentials, const std::string& network, const EthAddress& contract_address, const EthAddress& new_owner_addr, int nonce, const std::vector<uint8>& priv_key)
{
	int chain_id = -1;
	if(network == "ropsten")
		chain_id = Signing::ropstenChainID();
	else if(network == "mainnet")
		chain_id = Signing::mainnetChainID();

	// Compute function selector for the mint method.
	// transferOwnership(address to)
	const std::string func_selector = Infura::functionSelector("transferOwnership(address)");

	// Compute the entire encoded function call (in binary)
	//
	// selector (4 bytes)
	// to address (20 bytes padded to 32 bytes)
	std::vector<uint8> func_call_encoded(/*count=*/4 + 32, 0);
	std::memcpy(&func_call_encoded[0], &func_selector[0], 4); // Function selector is first 4 bytes
	std::memcpy(&func_call_encoded[4 + 12], &new_owner_addr.data, 20); // to address.  Addresses are 20 bytes, so we left pad with 12 zero bytes.

	conPrint("func_call_encoded: " + StringUtils::convertByteArrayToHexString(func_call_encoded));


	const uint64 cur_gas_price_wei = Infura::getCurrentGasPrice(credentials, network);

	const uint64 use_gas_price = (uint64)((double)cur_gas_price_wei * 1.2); // Offer more than the going rate, for faster transactions

	// Sanity check out gas price.
	// Gas price currently is 18000000000 wei (18 GWEI)
	if(use_gas_price > 180000000000ULL)
		throw glare::Exception("Gas price is too high: limit: " + toString((uint64)180000000000ULL) + ", use_gas_price: " + toString(use_gas_price));


	const uint64 gas_limit = 100000;

	// const uint64 limit_price_wei = gas_limit * cur_gas_price_wei;

	EthTransaction trans;
	trans.nonce = UInt256(nonce);
	trans.gas_price = UInt256(use_gas_price);
	trans.gas_limit = UInt256(gas_limit);
	trans.to = contract_address;
	trans.value = UInt256(0);
	trans.data = func_call_encoded; // Encoded Eth function call.


	Signing::signTransaction(trans, priv_key, chain_id);

	const std::vector<uint8> signed_transaction_encoded = RLP::encodeSignedTransaction(trans);

	conPrint("signed_transaction_encoded: " + StringUtils::convertByteArrayToHexString(signed_transaction_encoded));

	// Submit via Infura API
	const UInt256 transaction_hash = Infura::sendRawTransaction(credentials, network, signed_transaction_encoded);

	return transaction_hash;
}


// Execute an ethereum function call on the given smart contract.
// The function must take zero or more uint256 args, and should return an eth address.
//
// See https://infura.io/docs/ethereum/json-rpc/eth-call
// and https://docs.soliditylang.org/en/latest/abi-spec.html
EthAddress Infura::doEthCallReturningAddress(const InfuraCredentials& credentials, const std::string& network, const EthAddress& contract_address, const std::string& func_name, const std::vector<UInt256>& uint256_args)
{
	try
	{
		HTTPClient client;
		client.additional_headers.push_back("Authorization: Basic " + base64Encode(":" + credentials.infura_project_secret));

		// Compute function selector for the method.
		std::string sig = func_name + "(";
		for(size_t i=0; i<uint256_args.size(); ++i)
		{
			sig += "uint256";
			if(i + 1 < uint256_args.size())
				sig += ",";
		}
		sig += ")";

		const std::string func_selector = functionSelector(sig);
		

		// Compute the entire encoded function call (in binary)
		std::string func_call_encoded(4 + 32 * uint256_args.size(), '\0');
		std::memcpy(&func_call_encoded[0], &func_selector[0], 4); // Function selector is first 4 bytes
		for(size_t i=0; i<uint256_args.size(); ++i)
			std::memcpy(&func_call_encoded[4 + 32 * i], uint256_args[i].data, 32); // Arg i is next 32 bytes

		const std::string hex_func_call_encoded = stringToHexWith0xPrefix(func_call_encoded); // Convert binary to hex

		const std::string post_content = 
			"{"
			"\"jsonrpc\":\"2.0\","
			"\"method\":\"eth_call\","
			"\"params\": [{\"to\":\"" + contract_address.toHexStringWith0xPrefix() + "\", \"data\":\"" + hex_func_call_encoded + "\"}, \"latest\"]," // to = smart contract address
			"\"id\":1"
			"}";

		//conPrint(post_content);

		std::string data;
		HTTPClient::ResponseInfo response = client.sendPost("https://" + network + ".infura.io/v3/" + credentials.infura_project_id, post_content, /*content_type=*/"application/json", data);
		if(!(response.response_code >= 200 && response.response_code < 300))
			throw glare::Exception("HTTP Response was not a 2xx.  Response code: " + toString(response.response_code) + ", msg: " + response.response_message);

		// Example response: data = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0x000000000000000000000000290fbe4d4745f6b5267c209c92c8d81cebb5e9f0\"}";

		// Parse result
		JSONParser parser;
		parser.parseBuffer(data.data(), data.size());

		if(parser.nodes[0].getChildStringValue(parser, "jsonrpc") != "2.0")
			throw glare::Exception("Invalid jsonrpc response.");

		if(parser.nodes[0].getChildDoubleValue(parser, "id") != 1.0)
			throw glare::Exception("Invalid id response.");

		if(parser.nodes[0].hasChild("result"))
		{
			const std::string result = parser.nodes[0].getChildStringValue(parser, "result");

			if(result.size() != 2 + 64)
				throw glare::Exception("Unexpected result length.");

			// Address is last 20 bytes (40 hex chars) of the result value
			const std::string result_address = "0x" + result.substr(2 + 24);

			return EthAddress::parseFromHexString(result_address);
		}
		else if(parser.nodes[0].hasChild("error"))
		{
			const JSONNode& error_node = parser.nodes[0].getChildObject(parser, "error");

			const std::string message = error_node.getChildStringValue(parser, "message");

			throw glare::Exception("Transaction was submitted, but then reverted: " + message);
		}
		else
			throw glare::Exception("Neither result nor error found in response JSON.");
		
	}
	catch(glare::Exception& e)
	{
		throw e;
	}
}


UInt256 Infura::doEthCallReturningTransactionHash(const InfuraCredentials& credentials, const std::string& network, const EthAddress& contract_address, const std::string& func_name, const std::vector<EthAddress>& address_args)
{
	try
	{
		HTTPClient client;
		client.additional_headers.push_back("Authorization: Basic " + base64Encode(":" + credentials.infura_project_secret));

		// Compute function selector for the method.
		std::string sig = func_name + "(";
		for(size_t i=0; i<address_args.size(); ++i)
		{
			sig += "address";
			if(i + 1 < address_args.size())
				sig += ",";
		}
		sig += ")";

		const std::string func_selector = functionSelector(sig);

		// Compute the entire encoded function call (in binary)
		std::string func_call_encoded(4 + 32 * address_args.size(), '\0');
		std::memcpy(&func_call_encoded[0], &func_selector[0], 4); // Function selector is first 4 bytes
		for(size_t i=0; i<address_args.size(); ++i)
			std::memcpy(&func_call_encoded[4 + 32 * i + 12], address_args[i].data, 20); // Arg i is next 32 bytes, left pad with 12 zero bytes

		const std::string hex_func_call_encoded = stringToHexWith0xPrefix(func_call_encoded); // Convert binary to hex

		const std::string post_content = 
			"{"
			"\"jsonrpc\":\"2.0\","
			"\"method\":\"eth_call\","
			"\"params\": [{\"to\":\"" + contract_address.toHexStringWith0xPrefix() + "\", \"data\":\"" + hex_func_call_encoded + "\"}, \"latest\"]," // to = smart contract address
			"\"id\":1"
			"}";

		//conPrint(post_content);

		std::string data;
		HTTPClient::ResponseInfo response = client.sendPost("https://" + network + ".infura.io/v3/" + credentials.infura_project_id, post_content, /*content_type=*/"application/json", data);
		if(!(response.response_code >= 200 && response.response_code < 300))
			throw glare::Exception("HTTP Response was not a 2xx.  Response code: " + toString(response.response_code) + ", msg: " + response.response_message);

		// Example response: data = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0x000000000000000000000000290fbe4d4745f6b5267c209c92c8d81cebb5e9f0\"}";

		// Parse result
		JSONParser parser;
		parser.parseBuffer(data.data(), data.size());

		if(parser.nodes[0].getChildStringValue(parser, "jsonrpc") != "2.0")
			throw glare::Exception("Invalid jsonrpc response.");

		if(parser.nodes[0].getChildDoubleValue(parser, "id") != 1.0)
			throw glare::Exception("Invalid id response.");

		if(parser.nodes[0].hasChild("result"))
		{
			const std::string result = parser.nodes[0].getChildStringValue(parser, "result");

			if(result.size() != 2 + 64)
				throw glare::Exception("Unexpected result length.");

			return UInt256::parseFromHexString(result.substr(2));
		}
		else if(parser.nodes[0].hasChild("error"))
		{
			const JSONNode& error_node = parser.nodes[0].getChildObject(parser, "error");

			const std::string message = error_node.getChildStringValue(parser, "message");

			throw glare::Exception("Transaction was submitted, but then reverted: " + message);
		}
		else
			throw glare::Exception("Neither result nor error found in response JSON.");

	}
	catch(glare::Exception& e)
	{
		throw e;
	}
}


// See https://infura.io/docs/ethereum/json-rpc/eth-sendRawTransaction
UInt256 Infura::sendRawTransaction(const InfuraCredentials& credentials, const std::string& network, const std::vector<uint8>& pre_signed_transaction)
{
	try
	{
		HTTPClient client;
		client.additional_headers.push_back("Authorization: Basic " + base64Encode(":" + credentials.infura_project_secret));

		const std::string pre_signed_transaction_encoded = toHexWith0xPrefix(pre_signed_transaction); // Convert binary to hex

		const std::string post_content = 
			"{"
			"\"jsonrpc\":\"2.0\","
			"\"method\":\"eth_sendRawTransaction\","
			"\"params\": [\"" + pre_signed_transaction_encoded + "\"],"
			"\"id\":1"
			"}";

		std::string data;
		HTTPClient::ResponseInfo response = client.sendPost("https://" + network + ".infura.io/v3/" + credentials.infura_project_id, post_content, /*content_type=*/"application/json", data);
		if(!(response.response_code >= 200 && response.response_code < 300))
			throw glare::Exception("HTTP Response was not a 2xx.  Response code: " + toString(response.response_code) + ", msg: " + response.response_message);

		// Parse result
		JSONParser parser;
		parser.parseBuffer(data.data(), data.size());

		if(parser.nodes[0].getChildStringValue(parser, "jsonrpc") != "2.0")
			throw glare::Exception("Invalid jsonrpc response.");

		if(parser.nodes[0].getChildDoubleValue(parser, "id") != 1.0)
			throw glare::Exception("Invalid id response.");

		if(parser.nodes[0].hasChild("result"))
		{
			const std::string result = parser.nodes[0].getChildStringValue(parser, "result");

			if(result.size() != 2 + 64)
				throw glare::Exception("Unexpected result length.");

			return UInt256::parseFromHexString(result.substr(2));
		}
		else if(parser.nodes[0].hasChild("error"))
		{
			const JSONNode& error_node = parser.nodes[0].getChildObject(parser, "error");

			const std::string message = error_node.getChildStringValue(parser, "message");

			throw glare::Exception("Transaction was submitted, but then reverted: " + message);
		}
		else
			throw glare::Exception("Neither result nor error found in response JSON.");
	}
	catch(glare::Exception& e)
	{
		throw e;
	}
}


uint64 Infura::getCurrentGasPrice(const InfuraCredentials& credentials, const std::string& network) // In wei
{
	HTTPClient client;
	client.additional_headers.push_back("Authorization: Basic " + base64Encode(":" + credentials.infura_project_secret));

	const std::string post_content = 
		"{"
		"\"jsonrpc\":\"2.0\","
		"\"method\":\"eth_gasPrice\","
		"\"params\": [],"
		"\"id\":1"
		"}";

	std::string data;
	HTTPClient::ResponseInfo response = client.sendPost("https://" + network + ".infura.io/v3/" + credentials.infura_project_id, post_content, /*content_type=*/"application/json", data);
	if(!(response.response_code >= 200 && response.response_code < 300))
		throw glare::Exception("HTTP Response was not a 2xx.  Response code: " + toString(response.response_code) + ", msg: " + response.response_message);

	// Parse result
	JSONParser parser;
	parser.parseBuffer(data.data(), data.size());

	if(parser.nodes[0].getChildStringValue(parser, "jsonrpc") != "2.0")
		throw glare::Exception("Invalid jsonrpc response.");

	if(parser.nodes[0].getChildDoubleValue(parser, "id") != 1.0)
		throw glare::Exception("Invalid id response.");

	const std::string result = parser.nodes[0].getChildStringValue(parser, "result");
	if(!hasPrefix(result, "0x"))
		throw glare::Exception("result did not have 0x prefix.");

	const uint64 gas_price = ::hexStringToUInt64(result.substr(2)); // Remove 0x prefix
	return gas_price;
}


UInt256 Infura::deployContract(const InfuraCredentials& credentials, const std::string& network, const std::vector<uint8>& compiled_contract, const std::vector<uint8>& priv_key)
{
	int chain_id = -1;
	if(network == "ropsten")
		chain_id = Signing::ropstenChainID();
	else if(network == "mainnet")
		chain_id = Signing::mainnetChainID();

	const uint64 cur_gas_price_wei = Infura::getCurrentGasPrice(credentials, network);
	const uint64 use_gas_price = (uint64)((double)cur_gas_price_wei * 1.2); // Offer more than the going rate, for faster transactions

	const uint64 gas_limit = 1000000;

	EthTransaction trans;
	trans.nonce = UInt256(0);
	trans.gas_price = UInt256(use_gas_price);
	trans.gas_limit = UInt256(gas_limit);
	trans.to = EthAddress();
	trans.value = UInt256(0);
	trans.data = compiled_contract;


	Signing::signTransaction(trans, priv_key, chain_id);

	const std::vector<uint8> signed_transaction_encoded = RLP::encodeSignedTransaction(trans);

	conPrint("signed_transaction_encoded: " + StringUtils::convertByteArrayToHexString(signed_transaction_encoded));

	// Submit via Infura API
	const UInt256 transaction_hash = Infura::sendRawTransaction(credentials, network, signed_transaction_encoded);
	return transaction_hash;
}


Infura::TransactionReceipt Infura::getTransactionReceipt(const InfuraCredentials& credentials, const std::string& network, const UInt256& transaction_hash)
{
	HTTPClient client;
	client.additional_headers.push_back("Authorization: Basic " + base64Encode(":" + credentials.infura_project_secret));

	const std::string post_content = 
		"{"
		"\"jsonrpc\":\"2.0\","
		"\"method\":\"eth_getTransactionReceipt\","
		"\"params\": [\"0x" + transaction_hash.toHexString() + "\"], "
		"\"id\":1"
		"}";

	std::string data;
	HTTPClient::ResponseInfo response = client.sendPost("https://" + network + ".infura.io/v3/" + credentials.infura_project_id, post_content, /*content_type=*/"application/json", data);
	if(!(response.response_code >= 200 && response.response_code < 300))
		throw glare::Exception("HTTP Response was not a 2xx.  Response code: " + toString(response.response_code) + ", msg: " + response.response_message);

	// Parse result
	JSONParser parser;
	parser.parseBuffer(data.data(), data.size());

	if(parser.nodes[0].getChildStringValue(parser, "jsonrpc") != "2.0")
		throw glare::Exception("Invalid jsonrpc response.");

	if(parser.nodes[0].getChildDoubleValue(parser, "id") != 1.0)
		throw glare::Exception("Invalid id response.");

	const JSONNode& result_node = parser.nodes[0].getChildObject(parser, "result");

	const std::string contractAddress = result_node.getChildStringValue(parser, "contractAddress");

	Infura::TransactionReceipt receipt;
	receipt.contract_address = EthAddress::parseFromHexString(contractAddress);
	return receipt;
}



#if BUILD_TESTS


#include "../utils/TestUtils.h"
#include "../utils/FileUtils.h"


void Infura::test()
{
	InfuraCredentials credentials;
	// TODO: load for testing

	// Test deploying a contract
	/* {
		try
		{
			const std::string network = "ropsten";
			deployContract(network, compiled_contract);
		}
		catch(glare::Exception& e)
		{
			failTest(e.what());
		}
	}*/


	//================================================ Test transferContractOwnership ================================================ 
	if(false)
	{
		// On mainnet:
		/*try
		{
			const std::string network = "mainnet";

			const UInt256 transaction_hash = transferContractOwnership(network, 
				EthAddress::parseFromHexString("0xa4535F84e8D746462F9774319E75B25Bc151ba1D"), // contract address
				EthAddress::parseFromHexString("0x4E70020D55BE6D7c409307471B07417CFc20A775"), // new owner substrata address
				2 // nonce
			); 

			conPrint("Transaction hash: " + StringUtils::convertByteArrayToHexString(transaction_hash.data, 32));
		}
		catch(glare::Exception& e)
		{
			failTest(e.what());
		}*/
		
		// On Ropsten:
		/*try
		{
			const std::string network = "ropsten";

			const UInt256 transaction_hash = transferContractOwnership(network, 
				EthAddress::parseFromHexString("0x91324C50e2bc053A2A05AC09cFCEF64723CB07cB"), // contract address
				EthAddress::parseFromHexString("0x4E70020D55BE6D7c409307471B07417CFc20A775") // new owner substrata address
			); 

			conPrint("Transaction hash: " + StringUtils::convertByteArrayToHexString(transaction_hash.data, 32));
		}
		catch(glare::Exception& e)
		{
			failTest(e.what());
		}*/
	}


	//================================================ Test doEthCallReturningAddress ================================================ 
	// Test executing an ethereum call against the Substrata parcel smart contract.
	/*try
	{
		const std::string network = "ropsten";
		const EthAddress owner_addr =  doEthCallReturningAddress(network, EthAddress::parseFromHexString("0x91324C50e2bc053A2A05AC09cFCEF64723CB07cB"), "owner", std::vector<UInt256>());

		conPrint("owner_addr: " + owner_addr.toHexStringWith0xPrefix());
		//testAssert(owner_addr == EthAddress::parseFromHexString("0xe5A994bE9e94513bCB1A0a5991470d9fDe380d26")); 
		testAssert(owner_addr == EthAddress::parseFromHexString("0a047f219e8c2cabe5c9ad31ed1727742d419746")); 
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}*/

	//================================================ Test a transaction ================================================ 
	if(false)
	{
		try
		{
			const std::string network = "ropsten";
			const int chain_id = Signing::ropstenChainID();

			const std::vector<uint8> priv_key;// = readPrivKey();

			EthTransaction trans;
			trans.nonce = UInt256(6); // NOTE: has to be incremented each test.
			trans.gas_price = UInt256(18000000000);
			trans.gas_limit = UInt256(100000);
			trans.to = EthAddress::parseFromHexString("0xe5A994bE9e94513bCB1A0a5991470d9fDe380d26"); // to address
			trans.value = UInt256((uint64)1.0e18); // Send 1 ETH

			Signing::signTransaction(trans, priv_key, /*chain_id=*/chain_id);

			const std::vector<uint8> signed_transaction_encoded = RLP::encodeSignedTransaction(trans);

			conPrint("signed_transaction_encoded: " + StringUtils::convertByteArrayToHexString(signed_transaction_encoded));

			// Submit via Infura API
			const UInt256 transaction_hash = Infura::sendRawTransaction(credentials, network, signed_transaction_encoded);

			conPrint("Transaction hash: " + StringUtils::convertByteArrayToHexString(transaction_hash.data, 32));
		}
		catch(glare::Exception& e)
		{
			failTest(e.what());
		}
	}

	//================================================ Test getContractOwner ================================================ 
	try
	{
		// Query owner of token 123 of Substrata parcel smart contract
		const std::string network = "mainnet";
		const EthAddress substrata_smart_contact_addr = EthAddress::parseFromHexString("0xa4535F84e8D746462F9774319E75B25Bc151ba1D"); // This should be address of the Substrata parcel smart contract on mainnet
		const EthAddress owner_addr = getContractOwner(credentials, network, substrata_smart_contact_addr);

		conPrint("contract owner_addr: " + owner_addr.toHexStringWith0xPrefix());

		testAssert(owner_addr == EthAddress::parseFromHexString("0x4E70020D55BE6D7c409307471B07417CFc20A775"));
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}

	//================================================ Test getOwnerOfERC721Token ================================================ 
	try
	{
		// Query owner of token 123 of Substrata parcel smart contract
		const std::string network = "ropsten";
		const EthAddress substrata_smart_contact_addr = EthAddress::parseFromHexString("0x91324C50e2bc053A2A05AC09cFCEF64723CB07cB"); // This should be address of the Substrata parcel smart contract
		const EthAddress owner_addr = getOwnerOfERC721Token(credentials, network, substrata_smart_contact_addr, UInt256(123));

		conPrint("owner_addr: " + owner_addr.toHexStringWith0xPrefix());

		testAssert(owner_addr == EthAddress::parseFromHexString("0xe5A994bE9e94513bCB1A0a5991470d9fDe380d26"));
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}

	try
	{
		// Query owner of token 1 of Cryptovoxels parcel smart contract
		const std::string network = "mainnet";
		const EthAddress owner_addr = getOwnerOfERC721Token(credentials, network, EthAddress::parseFromHexString("0x79986aF15539de2db9A5086382daEdA917A9CF0C"), UInt256(1));

		conPrint("owner_addr: " + owner_addr.toHexStringWith0xPrefix());

		testAssert(owner_addr.toHexStringWith0xPrefix() == "0x290fbe4d4745f6b5267c209c92c8d81cebb5e9f0");
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}

	//================================================ Test getCurrentGasPrice ================================================ 
	try
	{
		const uint64 gas_price = getCurrentGasPrice(credentials, "mainnet");
		printVar(gas_price);
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}
}



#endif // BUILD_TESTS
