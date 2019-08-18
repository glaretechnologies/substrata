/*=====================================================================
SaveResourcesDBThread.cpp
-------------------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#include "SaveResourcesDBThread.h"


#include "../shared/ResourceManager.h"
#include <ConPrint.h>
#include <Exception.h>
#include <PlatformUtils.h>
#include <KillThreadMessage.h>


SaveResourcesDBThread::SaveResourcesDBThread(const Reference<ResourceManager>& resource_manager_, const std::string& path_)
:	resource_manager(resource_manager_), path(path_)
{}


SaveResourcesDBThread::~SaveResourcesDBThread()
{}


void SaveResourcesDBThread::doRun()
{
	PlatformUtils::setCurrentThreadNameIfTestsEnabled("SaveResourcesDBThread");

	while(1)
	{
		{
			Lock lock(resource_manager->getMutex());
			if(resource_manager->hasChanged())
			{
				resource_manager->clearChangedFlag();

				try
				{
					resource_manager->saveToDisk(path);
				}
				catch(Indigo::Exception& e)
				{
					conPrint("WARNING: Failed to save resources db: " + e.what());
				}
			}
		}

		// Wait for N seconds or until we get a KillThreadMessage.
		ThreadMessageRef message;
		const bool got_message = getMessageQueue().dequeueWithTimeout(/*wait time (s)=*/30.0, message);
		if(got_message)
			if(dynamic_cast<KillThreadMessage*>(message.getPointer()))
				return;
	}
}
