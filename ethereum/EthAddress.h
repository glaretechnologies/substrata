/*=====================================================================
EthAddress.h
------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include <Platform.h>
#include <Exception.h>
#include <StringUtils.h>
#include <vector>
#include <cstring>


struct EthAddress
{
	EthAddress() { std::memset(data, 0, 20); }

	// Copy 20 bytes of data from p
	explicit EthAddress(const uint8* p) { std::memcpy(data, p, 20); }

	// hex_string_ optionally has a "0x" prefix.
	static EthAddress parseFromHexString(const std::string& hex_string_)
	{
		std::string hex_string = hex_string_;

		if(hasPrefix(hex_string, "0x"))
			hex_string = hex_string.substr(2); // Remove 0x prefix

		if(hex_string.size() != 40) // 20 bytes = 40 hex chars
			throw glare::Exception("hex_string had invalid number of characters.");

		const std::vector<uint8> v = StringUtils::convertHexToBinary(hex_string);
		assert(v.size() == 20);

		EthAddress addr;
		std::memcpy(addr.data, v.data(), 20);
		return addr;
	}

	std::string toHexStringWith0xPrefix() const
	{
		return "0x" + StringUtils::convertByteArrayToHexString(data, 20);
	}

	bool operator == (const EthAddress& other) const
	{
		for(int i = 0; i < 20; ++i)
			if(data[i] != other.data[i])
				return false;
		return true;
	}

	uint8 data[20]; // big endian order
};
