/*=====================================================================
WorldState.h
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-12 12:22:34 +1300
=====================================================================*/
#pragma once


#include "../shared/Avatar.h"
#include "../shared/WorldObject.h"
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

	std::map<UID, Reference<WorldObject>> objects;

	std::set<Reference<WorldObject> > instances; // Objects created by the intancing command in scripts.

	Mutex mutex;
private:

};
