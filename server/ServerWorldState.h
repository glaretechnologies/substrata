/*=====================================================================
ServerWorldState.h
-------------------
Copyright Glare Technologies Limited 2018 -
Generated at 2016-01-12 12:22:34 +1300
=====================================================================*/
#pragma once


#include "../shared/Avatar.h"
#include "../shared/WorldObject.h"
#include "../shared/Parcel.h"
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
	
	UID getNextObjectUID(); // Gets and then increments next_object_uid
	UID getNextAvatarUID(); // Gets and then increments next_avatar_uid.  Locks mutex.


	bool changed; // Has changed since server state or since last save.

	std::map<UID, Reference<Avatar>> avatars;

	std::map<UID, Reference<WorldObject>> objects;

	std::map<UserID, Reference<User>> user_id_to_users;  // User id to user
	std::map<std::string, Reference<User>> name_to_users; // Username to user

	std::map<ParcelID, ParcelRef> parcels;
	
	::Mutex mutex;
private:
	INDIGO_DISABLE_COPY(ServerWorldState);

	UID next_object_uid;
	UID next_avatar_uid;
};
