/*=====================================================================
UInt256.h
----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include <Platform.h>
#include <string>
#include <vector>


// See https://github.com/kvhnuke/Ethereum-Arduino/blob/master/Ethereum-Arduino/TX.h


struct UInt256
{
	UInt256()
	{
		std::memset(data, 0, 32);
	}

	/*UInt256(uint32 x)
	{
		std::memset(data, 0, 32);
		
		std::memcpy(data + 28, (const uint8*)&x + 3, 1);
		std::memcpy(data + 29, (const uint8*)&x + 2, 1);
		std::memcpy(data + 30, (const uint8*)&x + 1, 1);
		std::memcpy(data + 31, (const uint8*)&x + 0, 1);
	}*/

	UInt256(uint64 x)
	{
		std::memset(data, 0, 32);

		std::memcpy(data + 24, (const uint8*)&x + 7, 1);
		std::memcpy(data + 25, (const uint8*)&x + 6, 1);
		std::memcpy(data + 26, (const uint8*)&x + 5, 1);
		std::memcpy(data + 27, (const uint8*)&x + 4, 1);
		std::memcpy(data + 28, (const uint8*)&x + 3, 1);
		std::memcpy(data + 29, (const uint8*)&x + 2, 1);
		std::memcpy(data + 30, (const uint8*)&x + 1, 1);
		std::memcpy(data + 31, (const uint8*)&x + 0, 1);
	}

	std::string toDecimalString()
	{
		/*
		x = data[0] * 2^(31*8) + data[1] * 2^(30*8) + ...  + data[30] * 2^(1*8) + data[31] * 2^0

		*/
		
	}

	uint8 data[32]; // big endian order
};

