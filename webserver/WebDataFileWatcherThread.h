/*=====================================================================
WebDataFileWatcherThread.h
--------------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "WebDataStore.h"
#include <MessageableThread.h>
#include <Reference.h>
#include <Vector.h>
#include <Mutex.h>
#include <string>
#include <set>
#include <map>


/*=====================================================================
WebDataFileWatcherThread
------------------------
Watches for changes to files that the webserver is serving, 
calls loadAndCompressFiles() when a file changes.
=====================================================================*/
class WebDataFileWatcherThread : public MessageableThread
{
public:
	WebDataFileWatcherThread(Reference<WebDataStore> web_data_store);
	virtual ~WebDataFileWatcherThread();

	virtual void doRun();

	Reference<WebDataStore> web_data_store;
};
