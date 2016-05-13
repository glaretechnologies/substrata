/*=====================================================================
ServerWorldState.h
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-12 12:22:34 +1300
=====================================================================*/
#pragma once


#include "../shared/Avatar.h"
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


	std::map<UID, Reference<Avatar>> avatars;


	UID next_avatar_uid;

	::Mutex mutex;
private:
	INDIGO_DISABLE_COPY(ServerWorldState);
};
