/*=====================================================================
WorldState.h
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-12 12:22:34 +1300
=====================================================================*/
#pragma once


#include "Avatar.h"
#include <ThreadSafeRefCounted.h>
#include <map>
#include <Mutex.h>


/*=====================================================================
WorldState
-------------------

=====================================================================*/
class WorldState : public ThreadSafeRefCounted
{
public:
	WorldState();
	~WorldState();


	std::map<UID, Reference<Avatar>> avatars;

	Mutex mutex;
private:

};
