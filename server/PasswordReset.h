/*=====================================================================
PasswordReset.h
---------------
Copyright Glare Technologies Limited 2018 -
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

	std::string token;
};


//typedef Reference<PasswordReset> PasswordResetRef;


void writeToStream(const PasswordReset& user, OutStream& stream);
void readFromStream(InStream& stream, PasswordReset& userob);
