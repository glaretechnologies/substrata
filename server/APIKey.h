/*=====================================================================
APIKey.h
--------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <utils/TimeStamp.h>
#include "../shared/UserID.h"
#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include <OutStream.h>
#include <InStream.h>
#include <DatabaseKey.h>


/*=====================================================================
APIKey
------
An API key that a user creates on their account page, used to authenticate
requests to the MCP endpoint (see MCPHandlers).

Only a hash of the raw key is stored, so that the raw key cannot be recovered
from the database.  The raw key is shown to the user exactly once, when it is
created.
=====================================================================*/
class APIKey : public ThreadSafeRefCounted
{
public:
	APIKey();
	~APIKey();

	// Returns a new random raw API key string (with an "sbsk_" prefix).  throws glare::Exception on failure.
	static std::string generateRawAPIKey();

	// Returns the hex-encoded SHA-256 hash of a raw API key.  Used both to compute key_hash when creating a key,
	// and to look up a key when authenticating a request.
	static std::string hashAPIKey(const std::string& raw_key);

	std::string key_hash; // Hex-encoded SHA-256 hash of the raw key.  The raw key itself is never stored.

	std::string name; // User-provided label for the key.

	UserID user_id; // The user that owns this key, and as which MCP requests using it will act.

	TimeStamp created_time;

	DatabaseKey database_key;

	static const size_t MAX_NAME_SIZE = 1000;
};


typedef Reference<APIKey> APIKeyRef;


void writeToStream(const APIKey& key, OutStream& stream);
void readFromStream(InStream& stream, APIKey& key);


struct APIKeyRefHash
{
	size_t operator() (const APIKeyRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};
