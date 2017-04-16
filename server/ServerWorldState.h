/*=====================================================================
ServerWorldState.h
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-12 12:22:34 +1300
=====================================================================*/
#pragma once


#include "../shared/Avatar.h"
#include "../shared/WorldObject.h"
#include "User.h"
#include <ThreadSafeRefCounted.h>
#include <Platform.h>
#include <Mutex.h>
#include <map>


/*=====================================================================
WorldState
-------------------

=====================================================================*/
class ServerWorldState : public ThreadSafeRefCounted
{
public:
	ServerWorldState();
	~ServerWorldState();


	void readFromDisk(const std::string& path);
	void serialiseToDisk(const std::string& path);
	UID getNextObjectUID();


	bool changed; // Has changed since server state or since last save.

	std::map<UID, Reference<Avatar>> avatars;

	std::map<UID, Reference<WorldObject>> objects;

	std::map<std::string, Reference<User>> users; // Username to user


	UID next_avatar_uid;


	::Mutex mutex;
private:
	INDIGO_DISABLE_COPY(ServerWorldState);

	UID next_object_uid;
};
