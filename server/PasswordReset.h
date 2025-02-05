/*=====================================================================
PasswordReset.h
---------------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#pragma once


#include <TimeStamp.h>
#include "../shared/UserID.h"
#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include <string>
#include <OutStream.h>
#include <InStream.h>
#include <vector>
#include <array>


/*=====================================================================
PasswordReset
-------------
Pending password reset
=====================================================================*/
class PasswordReset
{
public:
	PasswordReset();
	~PasswordReset();

	TimeStamp created_time;

	//std::string token_hash; // SHA 256 hash of the token
	//std::vector<uint8> token_hash; // SHA 256 hash of the token
	//uint8 token_hash[32];
	std::array<uint8, 32> token_hash;
};


//typedef Reference<PasswordReset> PasswordResetRef;


void writeToStream(const PasswordReset& user, OutStream& stream);
void readFromStream(InStream& stream, PasswordReset& userob);
