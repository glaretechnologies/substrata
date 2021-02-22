/*=====================================================================
UserWebSession.cpp
------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "UserWebSession.h"


#include <Exception.h>
#include <Base64.h>
#include <CryptoRNG.h>


UserWebSession::UserWebSession()
{}


UserWebSession::~UserWebSession()
{}


std::string UserWebSession::generateRandomKey() // throws glare::Exception on failure
{
	const int NUM_BYTES = 16;
	uint8 data[NUM_BYTES];

	CryptoRNG::getRandomBytes(data, NUM_BYTES); // throws glare::Exception on failure

	std::string key;
	Base64::encode(data, NUM_BYTES, key);
	return key;
}


static const uint32 USER_WEB_SESSION_SERIALISATION_VERSION = 1;


void writeToStream(const UserWebSession& session, OutStream& stream)
{
	// Write version
	stream.writeUInt32(USER_WEB_SESSION_SERIALISATION_VERSION);

	stream.writeStringLengthFirst(session.id);
	writeToStream(session.user_id, stream);
	session.created_time.writeToStream(stream);
}


void readFromStream(InStream& stream, UserWebSession& session)
{
	// Read version
	const uint32 v = stream.readUInt32();
	if(v > USER_WEB_SESSION_SERIALISATION_VERSION)
		throw glare::Exception("Unsupported version " + toString(v) + ", expected " + toString(USER_WEB_SESSION_SERIALISATION_VERSION) + ".");

	session.id = stream.readStringLengthFirst(128);
	session.user_id = readUserIDFromStream(stream);
	session.created_time.readFromStream(stream);
}
