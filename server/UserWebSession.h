/*=====================================================================
UserWebSession.h
----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "../shared/TimeStamp.h"
#include "../shared/UserID.h"
#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include <OutStream.h>
#include <InStream.h>


/*=====================================================================
UserWebSession
--------------
A logged-in web session.
Looked up by a cookie which stores the ID.
=====================================================================*/
class UserWebSession : public ThreadSafeRefCounted
{
public:
	UserWebSession();
	~UserWebSession();

	static std::string generateRandomKey(); // throws glare::Exception on failure

	std::string id;

	UserID user_id;

	TimeStamp created_time;
};


typedef Reference<UserWebSession> UserWebSessionRef;


void writeToStream(const UserWebSession& u, OutStream& stream);
void readFromStream(InStream& stream, UserWebSession& u);
