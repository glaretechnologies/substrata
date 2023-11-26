/*=====================================================================
User.cpp
--------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "User.h"


#include "ServerWorldState.h"
#include <Exception.h>
#include <StringUtils.h>
#include <FileUtils.h>
#include <ConPrint.h>
#include <SHA256.h>
#include <SMTPClient.h>
#include <Base64.h>
#include <Clock.h>
#include <CryptoRNG.h>


User::User()
:	flags(0)
{
}


User::~User()
{

}


bool User::isPasswordValid(const std::string& password_attempt) const
{
	const std::string attempt_digest = computePasswordHash(password_attempt, this->password_hash_salt);
	return attempt_digest == this->hashed_password;
}


std::string User::computePasswordHash(const std::string& password, const std::string& salt)
{
	const std::string message = "jdfrY%TFkj&Cg&------" + password + "------" + salt;

	std::vector<unsigned char> digest;
	SHA256::hash((const unsigned char*)&(*message.begin()), (const unsigned char*)&(*message.begin()) + message.size(), digest);

	std::string digest_str;
	digest_str.resize(digest.size());
	std::memcpy(&digest_str[0], digest.data(), digest.size());
	return digest_str;
}


void User::sendPasswordResetEmail(const EmailSendingInfo& sending_info)
{
	// Generate a random reset token
	uint8 random_buf[32];
	CryptoRNG::getRandomBytes(random_buf, sizeof(random_buf));

	std::string base64_encoded;
	Base64::encode(random_buf, sizeof(random_buf), base64_encoded);

	const std::string reset_token = base64_encoded.substr(0, 16); // Take first 16 chars for the reset token.  16 chars * 6 bits/char = 96 bits of randomness.

	const std::vector<unsigned char> token_hash = SHA256::hash(reset_token);

	SMTPClient::SendEmailArgs args;
	
	args.servername = sending_info.smtp_servername;
	args.username = sending_info.smtp_username;
	args.password = sending_info.smtp_password;

	args.from_name = sending_info.from_name;
	args.from_email_addr = sending_info.from_email_addr;

	args.to_name = "Substrata user";
	args.to_email_addr = this->email_address;

	args.subject = "Password Reset";
	args.contents = "To reset your Substrata password, please visit the following URL: https://" + sending_info.reset_webserver_hostname + "/reset_password_email?token=" + reset_token;

	SMTPClient::sendEmail(args);

	// Add reset token to list of reset tokens for user.
	PasswordReset reset;
	reset.created_time = TimeStamp::currentTime();
	std::memcpy(reset.token_hash.data(), token_hash.data(), 32);
	this->password_resets.push_back(reset);
}


static const int RESET_TOKEN_VALIDITY_DURATION_S = 3600;


bool User::isResetTokenHashValidForUser(const std::array<uint8, 32>& reset_token_hash) const
{
	for(size_t i=0; i<password_resets.size(); ++i)
		if(password_resets[i].token_hash == reset_token_hash)
		{
			// Check expiry date
			if(password_resets[i].created_time.numSecondsAgo() < RESET_TOKEN_VALIDITY_DURATION_S)
				return true;
		}
	return false;
}


bool User::resetPasswordWithTokenHash(const std::array<uint8, 32>& reset_token_hash, const std::string& new_password)
{
	for(size_t i=0; i<password_resets.size(); ++i)
	{
		if(password_resets[i].token_hash == reset_token_hash)
		{
			 // Check expiry date
			if(password_resets[i].created_time.numSecondsAgo() < RESET_TOKEN_VALIDITY_DURATION_S)
			{
				// Valid reset token - apply password reset
				this->hashed_password = computePasswordHash(new_password, this->password_hash_salt);

				// Remove this reset token
				password_resets.erase(password_resets.begin() + i);
				return true;
			}
		}
	}

	return false;
}


void User::setNewPasswordAndSalt(const std::string& new_password)
{
	// We need a random salt for the user.
	uint8 random_bytes[32];
	CryptoRNG::getRandomBytes(random_bytes, 32); // throws glare::Exception

	std::string user_salt;
	Base64::encode(random_bytes, 32, user_salt); // Convert random bytes to base-64.

	this->hashed_password = computePasswordHash(new_password, user_salt);
	this->password_hash_salt = user_salt;
}


static const uint32 USER_SERIALISATION_VERSION = 5;

// Version 5: Added flags
// Version 4: Added avatar_settings
// Version 3: Added controlled_eth_address


void writeToStream(const User& user, OutStream& stream, glare::Allocator& temp_allocator)
{
	// Write version
	stream.writeUInt32(USER_SERIALISATION_VERSION);

	writeToStream(user.id, stream);

	user.created_time.writeToStream(stream);

	stream.writeStringLengthFirst(user.name);
	stream.writeStringLengthFirst(user.email_address);

	stream.writeStringLengthFirst(user.hashed_password);
	stream.writeStringLengthFirst(user.password_hash_salt);

	stream.writeStringLengthFirst(user.controlled_eth_address);

	// Write password_resets
	stream.writeUInt32((uint32)user.password_resets.size());
	for(size_t i=0; i<user.password_resets.size(); ++i)
		writeToStream(user.password_resets[i], stream);

	writeToStream(user.avatar_settings, stream, temp_allocator);

	stream.writeUInt32(user.flags);
}


void readFromStream(InStream& stream, User& user, glare::BumpAllocator& bump_allocator)
{
	// Read version
	const uint32 v = stream.readUInt32();
	if(v > USER_SERIALISATION_VERSION)
		throw glare::Exception("Unsupported version " + toString(v) + ", expected " + toString(USER_SERIALISATION_VERSION) + ".");

	user.id = readUserIDFromStream(stream);

	user.created_time.readFromStream(stream);

	user.name = stream.readStringLengthFirst(10000);
	user.email_address = stream.readStringLengthFirst(10000);

	user.hashed_password = stream.readStringLengthFirst(10000);
	user.password_hash_salt = stream.readStringLengthFirst(10000);

	if(v >= 3)
		user.controlled_eth_address = stream.readStringLengthFirst(10000);

	// Read password_resets
	if(v >= 2)
	{
		const uint32 num = stream.readUInt32();
		if(num > 100000)
			throw glare::Exception("Too many password resets: " + toString(num));
		user.password_resets.resize(num);
		for(size_t i=0; i<num; ++i)
			readFromStream(stream, user.password_resets[i]);

		// TODO: remove expired reset tokens here?
	}

	if(v >= 4)
		readFromStream(stream, user.avatar_settings, bump_allocator);

	if(v >= 5)
		user.flags = stream.readUInt32();
}
