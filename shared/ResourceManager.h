/*=====================================================================
ResourceManager.h
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
ResourceManager
-------------------

=====================================================================*/
class ResourceManager : public ThreadSafeRefCounted
{
public:
	ResourceManager(const std::string& base_resource_dir);
	~ResourceManager();

	// Used for importing resources into the cyberspace resource system.
	static const std::string URLForPathAndHash(const std::string& path, uint64 hash);

	static bool isValidURL(const std::string& URL);

	const std::string pathForURL(const std::string& URL); // Throws Indigo::Exception if URL is invalid.

private:
	std::string base_resource_dir;
};
