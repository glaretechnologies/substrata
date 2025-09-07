/*=====================================================================
GarbageDeleterThread.cpp
------------------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#include "GarbageDeleterThread.h"


#include <utils/ConPrint.h>
#include <utils/Exception.h>
#include <utils/KillThreadMessage.h>
#include <utils/PlatformUtils.h>
#include <tracy/Tracy.hpp>


GarbageDeleterThread::GarbageDeleterThread()
{}


GarbageDeleterThread::~GarbageDeleterThread()
{}


void GarbageDeleterThread::doRun()
{
	PlatformUtils::setCurrentThreadNameIfTestsEnabled("GarbageDeleterThread");
	
	while(1)
	{
		ThreadMessageRef msg = this->getMessageQueue().dequeue();
		if(dynamic_cast<KillThreadMessage*>(msg.ptr()))
		{
			return;
		}
		else if(dynamic_cast<DeleteGarbageMessage*>(msg.ptr()))
		{
			try
			{
				//conPrint("GarbageDeleterThread: deleting garbage...");
				ZoneScoped; // Tracy profiler

				DeleteGarbageMessage* ptr = msg.downcastToPtr<DeleteGarbageMessage>();

				ptr->garbage.uint8_data.clearAndFreeMem();
				ptr->garbage.uint8_data2.clearAndFreeMem();
				ptr->garbage.uint32_data.clearAndFreeMem();
				ptr->garbage.uint16_data.clearAndFreeMem();
			}
			catch(glare::Exception& e)
			{
				conPrint("GarbageDeleterThread glare::Exception: " + e.what());
			}
		}
	}
}
