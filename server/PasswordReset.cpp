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


//bool PasswordReset::isPasswordValid(const std::string& password_attempt) const
//{
//	const std::string attempt_digest = computePasswordHash(password_attempt, this->password_hash_salt);
//	return attempt_digest == this->hashed_password;
//}
//



static const uint32 PASSWORD_RESET_SERIALISATION_VERSION = 1;


void writeToStream(const PasswordReset& password_reset, OutStream& stream)
{
	// Write version
	stream.writeUInt32(PASSWORD_RESET_SERIALISATION_VERSION);

	password_reset.created_time.writeToStream(stream);
	stream.writeStringLengthFirst(password_reset.token);
}


void readFromStream(InStream& stream, PasswordReset& password_reset)
{
	// Read version
	const uint32 v = stream.readUInt32();
	if(v > PASSWORD_RESET_SERIALISATION_VERSION)
		throw glare::Exception("Unsupported version " + toString(v) + ", expected " + toString(PASSWORD_RESET_SERIALISATION_VERSION) + ".");

	password_reset.created_time.readFromStream(stream);
	password_reset.token = stream.readStringLengthFirst(10000);
}
