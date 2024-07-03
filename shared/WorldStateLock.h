/*=====================================================================
WorldStateLock.h
----------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include <utils/ThreadSafetyAnalysis.h>
#include <utils/Lock.h>
#include <utils/Mutex.h>


class WorldStateMutex : public Mutex
{

};


/*=====================================================================
WorldStateLock
--------------
=====================================================================*/
class SCOPED_CAPABILITY WorldStateLock : public Lock
{
public:
	WorldStateLock(WorldStateMutex& mutex_) ACQUIRE(mutex_) // blocking
	:	Lock(mutex_)
	{}

	~WorldStateLock() RELEASE()
	{}
private:
	GLARE_DISABLE_COPY(WorldStateLock);
};
