/*=====================================================================
User.h
------
Copyright Glare Technologies Limited 2017 -
=====================================================================*/
#pragma once


#include "PasswordReset.h"
#include "../shared/TimeStamp.h"
#include "../shared/UserID.h"
#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include <string>
#include <OutStream.h>
#include <InStream.h>


/*=====================================================================
User
----
User account
=====================================================================*/
class User : public ThreadSafeRefCounted
{
public:
	User();
	~User();


	// Return digest
	static std::string computePasswordHash(const std::string& password, const std::string& salt);

	// Does the password, when hashed, match the password disgest we have stored?
	bool isPasswordValid(const std::string& password) const;


	void sendPasswordResetEmail(); // throws glare::Exception on error

	bool isResetTokenHashValidForUser(const std::array<uint8, 32>& reset_token_hash) const;
	bool resetPasswordWithTokenHash(const std::array<uint8, 32>& reset_token_hash, const std::string& new_password);

	UserID id;

	TimeStamp created_time;

	std::string name;
	std::string email_address;

	std::string hashed_password; // SHA-256 hash, so 256/8 = 32 bytes
	std::string password_hash_salt; // Base-64 encoded 256 random bits.

	std::string current_eth_signing_nonce; // Doesn't need to be serialised, should be generated and used relatively quickly.
	std::string controlled_eth_address; // Eth address that user controls, in hex encoding with 0x prefix.  Empty if no such address.

	std::vector<PasswordReset> password_resets; // pending password reset tokens
};


typedef Reference<User> UserRef;


void writeToStream(const User& user, OutStream& stream);
void readFromStream(InStream& stream, User& userob);
