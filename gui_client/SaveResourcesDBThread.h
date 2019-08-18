/*=====================================================================
SaveResourcesDBThread.h
-----------------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#pragma once


#include <MessageableThread.h>
#include <Platform.h>
#include <string>
class ResourceManager;


/*=====================================================================
SaveResourcesDBThread
---------------------
Saves resources list to resources database on disk, if the resource manager 
has changed.
=====================================================================*/
class SaveResourcesDBThread : public MessageableThread
{
public:
	SaveResourcesDBThread(const Reference<ResourceManager>& resource_manager_, const std::string& path_);
	virtual ~SaveResourcesDBThread();

	virtual void doRun();
private:
	Reference<ResourceManager> resource_manager;
	const std::string path;
};
