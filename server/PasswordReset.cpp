/*=====================================================================
PasswordReset.cpp
-----------------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#include "PasswordReset.h"


#include <Exception.h>
#include <StringUtils.h>
#include <FileUtils.h>
#include <ConPrint.h>
#include <SHA256.h>


PasswordReset::PasswordReset()
{
}


PasswordReset::~PasswordReset()
{

}


static const uint32 PASSWORD_RESET_SERIALISATION_VERSION = 2;
// Note: changed token to uint8 token_hash[32].


void writeToStream(const PasswordReset& password_reset, OutStream& stream)
{
	// Write version
	stream.writeUInt32(PASSWORD_RESET_SERIALISATION_VERSION);

	password_reset.created_time.writeToStream(stream);
	stream.writeData(password_reset.token_hash.data(), 32);
}


void readFromStream(InStream& stream, PasswordReset& password_reset)
{
	// Read version
	const uint32 v = stream.readUInt32();
	if(v > PASSWORD_RESET_SERIALISATION_VERSION)
		throw glare::Exception("Unsupported version " + toString(v) + ", expected " + toString(PASSWORD_RESET_SERIALISATION_VERSION) + ".");

	password_reset.created_time.readFromStream(stream);
	if(v == 1)
	{
		const std::string token = stream.readStringLengthFirst(10000);
		std::memset(password_reset.token_hash.data(), 0, 32);
		password_reset.created_time = TimeStamp(0); // Mark reset token invalid by setting created time to 0 (far in the past)
	}
	else
	{
		stream.readData(password_reset.token_hash.data(), 32);
	}
}
