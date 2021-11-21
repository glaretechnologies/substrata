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
#include <DatabaseKey.h>


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

	DatabaseKey database_key;
};


typedef Reference<UserWebSession> UserWebSessionRef;


void writeToStream(const UserWebSession& u, OutStream& stream);
void readFromStream(InStream& stream, UserWebSession& u);


struct UserWebSessionRefHash
{
	size_t operator() (const UserWebSessionRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};
