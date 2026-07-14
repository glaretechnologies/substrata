/*=====================================================================
APIKey.cpp
----------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "APIKey.h"


#include <Exception.h>
#include <StringUtils.h>
#include <SHA256.h>
#include <CryptoRNG.h>


APIKey::APIKey()
{}


APIKey::~APIKey()
{}


std::string APIKey::generateRawAPIKey() // throws glare::Exception on failure
{
	const int NUM_BYTES = 32;
	uint8 data[NUM_BYTES];

	CryptoRNG::getRandomBytes(data, NUM_BYTES); // throws glare::Exception on failure

	// Use a hex encoding (rather than base64) so the key is safe to use in HTTP headers without escaping.
	return "sbsk_" + StringUtils::convertByteArrayToHexString(data, NUM_BYTES);
}


std::string APIKey::hashAPIKey(const std::string& raw_key)
{
	const std::vector<unsigned char> digest = SHA256::hash(raw_key);
	return StringUtils::convertByteArrayToHexString(digest.data(), digest.size());
}


static const uint32 API_KEY_SERIALISATION_VERSION = 1;


void writeToStream(const APIKey& key, OutStream& stream)
{
	// Write version
	stream.writeUInt32(API_KEY_SERIALISATION_VERSION);

	stream.writeStringLengthFirst(key.key_hash);
	stream.writeStringLengthFirst(key.name);
	writeToStream(key.user_id, stream);
	key.created_time.writeToStream(stream);
}


void readFromStream(InStream& stream, APIKey& key)
{
	// Read version
	const uint32 v = stream.readUInt32();
	if(v > API_KEY_SERIALISATION_VERSION)
		throw glare::Exception("Unsupported version " + toString(v) + ", expected " + toString(API_KEY_SERIALISATION_VERSION) + ".");

	key.key_hash = stream.readStringLengthFirst(/*max length=*/1000);
	key.name = stream.readStringLengthFirst(APIKey::MAX_NAME_SIZE);
	key.user_id = readUserIDFromStream(stream);
	key.created_time.readFromStream(stream);
}
