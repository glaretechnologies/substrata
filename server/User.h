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

	bool resetPasswordWithToken(const std::string& reset_token, const std::string& new_password);

	UserID id;

	TimeStamp created_time;

	std::string name;
	std::string email_address;

	std::string hashed_password; // SHA-256 hash, so 256/8 = 32 bytes
	std::string password_hash_salt; // Base-64 encoded SHA-256 hash

	std::vector<PasswordReset> password_resets; // pending password reset tokens
};


typedef Reference<User> UserRef;


void writeToStream(const User& user, OutStream& stream);
void readFromStream(InStream& stream, User& userob);
