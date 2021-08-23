/*=====================================================================
UInt256.cpp
-----------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "UInt256.h"


// Without 0x prefix.
const std::string UInt256::toHexString() const
{
	return StringUtils::convertByteArrayToHexString(data, 32);
}


// hex_string_ optionally has a "0x" prefix.
UInt256 UInt256::parseFromHexString(const std::string& hex_string_)
{
	std::string hex_string = hex_string_;

	if(hasPrefix(hex_string, "0x"))
		hex_string = hex_string.substr(2); // Remove 0x prefix

	if(hex_string.size() != 64) // 256 bits = 32 bytes = 64 hex chars
		throw glare::Exception("hex_string had invalid number of characters.");

	const std::vector<uint8> v = StringUtils::convertHexToBinary(hex_string);
	assert(v.size() == 32);

	UInt256 x;
	std::memcpy(x.data, v.data(), 32);
	return x;
}


#if BUILD_TESTS


#include "../utils/TestUtils.h"


void UInt256::test()
{
	try
	{
		testAssert(UInt256(0).toHexString()           == "0000000000000000000000000000000000000000000000000000000000000000");
		testAssert(UInt256(1).toHexString()           == "0000000000000000000000000000000000000000000000000000000000000001");
		testAssert(UInt256(0x123456789).toHexString() == "0000000000000000000000000000000000000000000000000000000123456789");
		testAssert(UInt256::parseFromHexString("0xf941c72383c1a552c579e299007979cd90df0afeac0b41f89c9947b1f018c0ab").toHexString() == "f941c72383c1a552c579e299007979cd90df0afeac0b41f89c9947b1f018c0ab");
		testAssert(UInt256::parseFromHexString(  "f941c72383c1a552c579e299007979cd90df0afeac0b41f89c9947b1f018c0ab").toHexString() == "f941c72383c1a552c579e299007979cd90df0afeac0b41f89c9947b1f018c0ab");
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}
}


#endif // BUILD_TESTS
