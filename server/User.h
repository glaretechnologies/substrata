/*=====================================================================
User.h
------
Copyright Glare Technologies Limited 2017 -
=====================================================================*/
#pragma once


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


	UserID id;

	TimeStamp created_time;

	std::string name;
	std::string email_address;

	std::string hashed_password;
	std::string password_hash_salt;
};


typedef Reference<User> UserRef;


void writeToStream(const User& user, OutStream& stream);
void readFromStream(InStream& stream, User& userob);
