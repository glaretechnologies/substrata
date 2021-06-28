/*=====================================================================
Infura.cpp
------------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "Infura.h"


#include <ResponseUtils.h>
#include "../server/ServerWorldState.h"
#include "RequestInfo.h"
#include "WebsiteExcep.h"
#include "Escaping.h"
#include "Log.h"
#include "RequestHandler.h"
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


static const std::string INFURA_PROJECT_ID = "2886dd8d54c34b74af9ed0c11181affc";
static const std::string INFURA_PROJECT_SECRET = "05ec9009a94843b5af4146aad8d05ee8";


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


// See https://docs.soliditylang.org/en/develop/abi-spec.html#function-selector
static std::string functionSelector(const std::string& s)
{
	uint8 digest[32];
	Keccak256::hash(s, digest);
	return std::string(digest, digest + 4); // Take first 4 bytes
}


std::string Infura::makeUInt256BigEndianString(uint32 x)
{
	std::string x_little_endian(4, '\0');
	std::memcpy(&x_little_endian[0], &x, 4);

	std::string x_big_endian(32, '\0');
	x_big_endian[28] = x_little_endian[3];
	x_big_endian[29] = x_little_endian[2];
	x_big_endian[30] = x_little_endian[1];
	x_big_endian[31] = x_little_endian[0];
	return x_big_endian;
}


// Queries the Ethereum blockchain via Infura APIs, to get the owning address of an ERC721 token,
// or any smart contract that implements the ownerOf method.
// Returns a hex address string like "0x290FbE4d4745f6B5267c209C92C8D81CebB5E9f0"
//
// See https://infura.io/docs/ethereum/json-rpc/eth-call
// and https://docs.soliditylang.org/en/latest/abi-spec.html
std::string Infura::getOwnerOfERC721Token(const std::string& contract_address, const std::string& token_id)
{
	if(token_id.size() != 32) // token_id should be a 32 byte string.
		throw glare::Exception("Invalid token_id size.");

	try
	{
		HTTPClient client;
		client.additional_headers.push_back("Authorization: Basic " + base64Encode(":" + INFURA_PROJECT_SECRET));
	
		// Compute function selector for the ownerOf method.
		const std::string func_selector = functionSelector("ownerOf(uint256)");

		

		// Compute the entire encoded function call (in binary)
		std::string func_call_encoded(4 + 32, '\0');
		std::memcpy(&func_call_encoded[0], &func_selector[0], 4); // Function selector is first 4 bytes
		std::memcpy(&func_call_encoded[4], &token_id[0], 32); // Arg 0 is next 32 bytes

		const std::string hex_func_call_encoded = stringToHexWith0xPrefix(func_call_encoded); // Convert binary to hex
		
		const std::string post_content = 
			"{"
				"\"jsonrpc\":\"2.0\","
				"\"method\":\"eth_call\","
				"\"params\": [{\"to\":\"" + contract_address + "\", \"data\":\"" + hex_func_call_encoded + "\"}, \"latest\"]," // to = smart contract address
				"\"id\":1"
			"}";

		//conPrint(post_content);

		std::string data;
		HTTPClient::ResponseInfo response = client.sendPost("https://mainnet.infura.io/v3/" + INFURA_PROJECT_ID, post_content, /*content_type=*/"application/json", data);
		if(!(response.response_code >= 200 && response.response_code < 300))
			throw glare::Exception("HTTP Response was not a 2xx.  Response code: " + toString(response.response_code) + ", msg: " + response.response_message);

		//Example response: data = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0x000000000000000000000000290fbe4d4745f6b5267c209c92c8d81cebb5e9f0\"}";

		// Parse result
		JSONParser parser;
		parser.parseBuffer(data.data(), data.size());

		if(parser.nodes[0].getChildStringValue(parser, "jsonrpc") != "2.0")
			throw glare::Exception("Invalid jsonrpc response.");
		
		if(parser.nodes[0].getChildDoubleValue(parser, "id") != 1.0)
			throw glare::Exception("Invalid id response.");

		const std::string result = parser.nodes[0].getChildStringValue(parser, "result");
		
		if(result.size() != 2 + 64)
			throw glare::Exception("Unexpected result length.");

		// Address is last 20 bytes (40 hex chars) of the result value
		const std::string result_address = "0x" + result.substr(2 + 24);

		return result_address;
	}
	catch(glare::Exception& e)
	{
		throw e;
	}
}


// See https://infura.io/docs/ethereum/json-rpc/eth-sendRawTransaction
void mintParcel(const std::string& contract_address, const std::string& parcel_owner_addr, const std::string& token_id)
{
	if(token_id.size() != 32) // token_id should be a 32 byte string.
		throw glare::Exception("Invalid token_id size.");

	try
	{
		HTTPClient client;
		client.additional_headers.push_back("Authorization: Basic " + base64Encode(":" + INFURA_PROJECT_SECRET));

		// Compute function selector for the mint method.
		// mint(address to, uint256 tokenId)
		const std::string func_selector = functionSelector("mint(address, uint256)");


		// Compute the entire encoded function call (in binary)
		std::string func_call_encoded(4 + 2*32, '\0');
		std::memcpy(&func_call_encoded[0], &func_selector[0], 4); // Function selector is first 4 bytes
		std::memcpy(&func_call_encoded[4], &parcel_owner_addr[0], 32); // Arg 0 is next 32 bytes (parcel_ownder_addr)
		std::memcpy(&func_call_encoded[4 + 32], &token_id[0], 32); // Arg 1 is next 32 bytes (token_id)

		const std::string hex_func_call_encoded = stringToHexWith0xPrefix(func_call_encoded); // Convert binary to hex

		const std::string post_content = 
			"{"
			"\"jsonrpc\":\"2.0\","
			"\"method\":\"eth_sendRawTransaction\","
			"\"params\": [{\"to\":\"" + contract_address + "\", \"data\":\"" + hex_func_call_encoded + "\"}, \"latest\"]," // to = smart contract address
			"\"id\":1"
			"}";

		//conPrint(post_content);

		std::string data;
		HTTPClient::ResponseInfo response = client.sendPost("https://mainnet.infura.io/v3/" + INFURA_PROJECT_ID, post_content, /*content_type=*/"application/json", data);
		if(!(response.response_code >= 200 && response.response_code < 300))
			throw glare::Exception("HTTP Response was not a 2xx.  Response code: " + toString(response.response_code) + ", msg: " + response.response_message);

		//Example response: data = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0x000000000000000000000000290fbe4d4745f6b5267c209c92c8d81cebb5e9f0\"}";

		// Parse result
		JSONParser parser;
		parser.parseBuffer(data.data(), data.size());

		if(parser.nodes[0].getChildStringValue(parser, "jsonrpc") != "2.0")
			throw glare::Exception("Invalid jsonrpc response.");

		if(parser.nodes[0].getChildDoubleValue(parser, "id") != 1.0)
			throw glare::Exception("Invalid id response.");

		const std::string result = parser.nodes[0].getChildStringValue(parser, "result");

		if(result.size() != 2 + 64)
			throw glare::Exception("Unexpected result length.");

		// Address is last 20 bytes (40 hex chars) of the result value
		const std::string result_address = "0x" + result.substr(2 + 24);
	}
	catch(glare::Exception& e)
	{
		throw e;
	}

}



#if BUILD_TESTS


#include "../utils/TestUtils.h"


void Infura::test()
{
	testAssert(stringToHexWith0xPrefix(makeUInt256BigEndianString(0)) == "0x0000000000000000000000000000000000000000000000000000000000000000");
	testAssert(stringToHexWith0xPrefix(makeUInt256BigEndianString(1)) == "0x0000000000000000000000000000000000000000000000000000000000000001");
	testAssert(stringToHexWith0xPrefix(makeUInt256BigEndianString(15)) == "0x000000000000000000000000000000000000000000000000000000000000000f");
	testAssert(stringToHexWith0xPrefix(makeUInt256BigEndianString(31)) == "0x000000000000000000000000000000000000000000000000000000000000001f");
	testAssert(stringToHexWith0xPrefix(makeUInt256BigEndianString(1234567890)) == "0x00000000000000000000000000000000000000000000000000000000499602d2");

	try
	{
		// Query owner of token 1 of Cryptovoxels parcel smart contract
		const std::string owner_addr = getOwnerOfERC721Token("0x79986aF15539de2db9A5086382daEdA917A9CF0C", makeUInt256BigEndianString(1));

		conPrint("owner_addr: " + owner_addr);

		testAssert(owner_addr == "0x290fbe4d4745f6b5267c209c92c8d81cebb5e9f0");
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}
}



#endif // BUILD_TESTS
