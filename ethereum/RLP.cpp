/*=====================================================================
RLP.cpp
-------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "RLP.h"


#include <ConPrint.h>
#include <Exception.h>
#include <StringUtils.h>
#include <Keccak256.h>
#include <ContainerUtils.h>


namespace RLP // Recursive Length Prefix - an encoding used by Ethereum.  See https://eth.wiki/fundamentals/rlp
{


std::vector<uint8> operator + (const std::vector<uint8>& a, const std::vector<uint8>& b)
{
	std::vector<uint8> v = a;
	v.insert(v.end(), b.begin(), b.end());
	return v;
}


std::vector<uint8> chr(size_t x)
{
	assert(x < 256);
	return std::vector<uint8>(1, (uint8)x);
}


std::vector<uint8> toBinary(size_t x)
{
	if(x == 0)
		return std::vector<uint8>();
	else
		return toBinary(x / 256) + chr(x % 256);
}


std::vector<uint8> encodeLength(size_t len, size_t offset)
{
	if(len < 56)
		return chr(len + offset);
	else
	{
		std::vector<uint8> bl = toBinary(len);
		return chr(bl.size() + offset + 55) + bl;
	}
}


std::vector<uint8> stringToData(const std::string& s)
{
	std::vector<uint8> v(s.size());
	if(v.size() > 0)
		std::memcpy(&v[0], s.data(), s.size());
	return v;
}


std::vector<uint8> encode(const std::string& s)
{
	return encode(stringToData(s));
}


std::vector<uint8> encode(const std::vector<uint8>& data)
{
	if(data.empty())
		return std::vector<uint8>(1, 0x80);

	if(data.size() == 1 && (unsigned char)data[0] < 0x80) // For a single byte whose value is in the [0x00, 0x7f] range, that byte is its own RLP encoding.
		return data;
	else if(data.size() < 56)
	{
		std::vector<uint8> res;
		res.resize(1 + data.size());
		res[0] = (uint8)(0x80 + data.size());
		std::memcpy(&res[1], data.data(), data.size());
		return res;
	}
	else
	{
		std::vector<uint8> binary_len = toBinary(data.size()); // Encode data.size() into an integer with big endian byte ordering.

		std::vector<uint8> res;
		res.push_back((uint8)(0xb7 + binary_len.size()));
		ContainerUtils::append(res, binary_len);
		ContainerUtils::append(res, data);
		return res;
	}
	//return encodeLength(data.size(), 0x80) + data;
}


// Leading zeros should not be present in the encoding:
// "positive RLP integers must be represented in big endian binary form with no leading zeroes (thus making the integer value zero be equivalent to the empty byte array)" -- https://eth.wiki/fundamentals/rlp
std::vector<uint8> encode(const UInt256& x)
{
	// Find first non-zero byte
	int first_i = -1;
	for(int i=0; i<32; ++i)
		if(x.data[i] != 0)
		{
			first_i = i;
			break;
		}

	if(first_i == -1)
		return encode(std::vector<uint8>()); // value was zero, encode as empty byte array.
	else
		return encode(std::vector<uint8>(x.data + first_i, x.data + 32));
}


// See Signer::hash() in https://github.com/trustwallet/wallet-core/blob/master/src/Ethereum/Signer.cpp
std::vector<uint8> transactionHash(EthTransaction& trans, const UInt256& chain_id)
{
	// See https://github.com/ethereum/EIPs/blob/master/EIPS/eip-155.md
	std::vector<uint8> output;
	ContainerUtils::append(output, encode(trans.nonce));
	ContainerUtils::append(output, encode(trans.gas_price));
	ContainerUtils::append(output, encode(trans.gas_limit));
	ContainerUtils::append(output, encode(trans.to));
	ContainerUtils::append(output, encode(trans.value));
	ContainerUtils::append(output, encode(trans.data));

	//conPrint("trans.value: " + StringUtils::convertByteArrayToHexString(encode(trans.value)));
	//conPrint("trans.data: " + StringUtils::convertByteArrayToHexString(encode(trans.data)));
	//conPrint(StringUtils::convertByteArrayToHexString(output));

	ContainerUtils::append(output, encode(chain_id));
	ContainerUtils::append(output, encode(UInt256()));
	ContainerUtils::append(output, encode(UInt256()));

	const std::vector<uint8> signing_data = encodeLength(output.size(), 0xc0) + output;

	//conPrint(StringUtils::convertByteArrayToHexString(signing_data));

	return Keccak256::hash(signing_data);
}


// Transaction should be signed with Signing::signTransaction(), e.g. v, r, and s fields should be set.
// See https://ethereum.stackexchange.com/a/2097 for format
std::vector<uint8> encodeSignedTransaction(const EthTransaction& trans)
{
	std::vector<uint8> output;
	ContainerUtils::append(output, encode(trans.nonce));
	ContainerUtils::append(output, encode(trans.gas_price));
	ContainerUtils::append(output, encode(trans.gas_limit));
	ContainerUtils::append(output, encode(trans.to));
	ContainerUtils::append(output, encode(trans.value));
	ContainerUtils::append(output, encode(trans.data));
	ContainerUtils::append(output, encode(trans.v));
	ContainerUtils::append(output, encode(trans.r));
	ContainerUtils::append(output, encode(trans.s));

	return encodeLength(output.size(), 0xc0) + output;
}


}; // End namespace RLP


#if BUILD_TESTS


#include "../utils/TestUtils.h"


void RLP::test()
{
	{
		std::vector<uint8> target(4, 0x83);
		target[1] = 'd';
		target[2] = 'o';
		target[3] = 'g';
		testAssert(encode("dog") == target);
	}
	{
		testAssert(encode(std::vector<uint8>()) == std::vector<uint8>(1, 0x80));
		testAssert(encode("") == std::vector<uint8>(1, 0x80));
		testAssert(encode("\x15") == std::vector<uint8>(1, 0x15));
		testAssert(encode("Lorem ipsum dolor sit amet, consectetur adipisicing elit") == stringToData("\xb8\x38Lorem ipsum dolor sit amet, consectetur adipisicing elit"));

		testAssert(encode(UInt256(0)) == std::vector<uint8>(1, 0x80));
		testAssert(encode(UInt256(1)) == std::vector<uint8>(1, 1));
		testAssert(encode(UInt256(123)) == std::vector<uint8>(1, 123));

		{
			std::vector<uint8> target;
			target.push_back(0x82);
			target.push_back(0x04);
			target.push_back(0x00);
			testAssert(encode(UInt256(1024)) == target);
		}
	}

	{
		EthTransaction tx;
		tx.nonce = UInt256(0x1234);
		tx.gas_price = UInt256(100);
		tx.gas_limit = UInt256(200);
		//tx.to = "0x1234";
		tx.value = UInt256(10000);
		//tx.data = "0x1234";

		//std::vector<uint8> res = encode(tx);

		//conPrint(res);
	}
	
}


#endif // BUILD_TESTS
