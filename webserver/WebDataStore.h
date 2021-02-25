/*=====================================================================
WebDataStore.h
-------------------
Copyright Glare Technologies Limited 2013 -
Generated at 2013-04-16 22:29:52 +0100
=====================================================================*/
#pragma once


#include "StaticAssetManager.h"
#include <Mutex.h>
#include <ThreadSafeRefCounted.h>
#include <vector>


/*=====================================================================
DataStore
-------------------

=====================================================================*/
class WebDataStore : public ThreadSafeRefCounted
{
public:
	WebDataStore(/*const std::string& path*/);
	~WebDataStore();

	void loadFromDisk();
	void writeToDisk();

	//void updateAssetManagerAndPageURLMap();


	//mutable Mutex mutex;

	//std::string path;

	//StaticAssetManager static_asset_manager;

	//std::map<std::string, int> page_url_map;

	std::string letsencrypt_webroot;
	std::string public_files_dir;
	std::string screenshot_dir;
	//std::string resources_dir;
};
