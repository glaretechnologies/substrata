/*=====================================================================
ResourceManager.h
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-12 12:22:34 +1300
=====================================================================*/
#pragma once


#include "Resource.h"
#include "Avatar.h"
#include "WorldObject.h"
#include <ThreadSafeRefCounted.h>
#include <Reference.h>
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
	static const std::string URLForNameAndExtensionAndHash(const std::string& name, const std::string& extension, uint64 hash);

	// Used for importing resources into the cyberspace resource system.
	static const std::string URLForPathAndHash(const std::string& path, uint64 hash);

	static bool isValidURL(const std::string& URL);

	// Will create a new Resource ob if not already inserted.
	ResourceRef getResourceForURL(const std::string& URL); // Threadsafe

	// Copy a local file with given local path and corresponding URL into the resource dir
	// Throws Indigo::Exception on failure.
	void copyLocalFileToResourceDir(const std::string& local_path, const std::string& URL); // Threadsafe

	const std::string pathForURL(const std::string& URL); // Throws Indigo::Exception if URL is invalid.

	const std::string computeLocalPathForURL(const std::string& URL);

	bool isFileForURLPresent(const std::string& URL); // Throws Indigo::Exception if URL is invalid.

	// Used for deserialising resource objects from serialised server state.
	void addResource(ResourceRef& res);
	const std::map<std::string, ResourceRef>& getResourcesForURL() const { return resource_for_url; }
	std::map<std::string, ResourceRef>& getResourcesForURL() { return resource_for_url; }
private:
	std::string base_resource_dir;

	Mutex mutex;
	std::map<std::string, ResourceRef> resource_for_url;
};
