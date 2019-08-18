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
#include <IndigoAtomic.h>


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

	// Will create a new Resource object if not already inserted.
	ResourceRef getResourceForURL(const std::string& URL); // Threadsafe

	// Copy a local file with given local path and corresponding URL into the resource dir
	// Throws Indigo::Exception on failure.
	void copyLocalFileToResourceDir(const std::string& local_path, const std::string& URL); // Threadsafe

	const std::string pathForURL(const std::string& URL); // Throws Indigo::Exception if URL is invalid.

	const std::string computeDefaultLocalPathForURL(const std::string& URL); // Compute default local path for URL.

	const std::string computeLocalPathFromURLHash(const std::string& URL, const std::string& extension);

	bool isFileForURLPresent(const std::string& URL); // Throws Indigo::Exception if URL is invalid.

	// Used for deserialising resource objects from serialised server state.
	void addResource(ResourceRef& res);
	const std::map<std::string, ResourceRef>& getResourcesForURL() const { return resource_for_url; }
	std::map<std::string, ResourceRef>& getResourcesForURL() { return resource_for_url; }

	bool hasChanged() const { return changed != 0; }
	void clearChangedFlag() { changed = 0; }
	void markAsChanged(); // Thread-safe

	Mutex& getMutex() { return mutex; }

	// Just used on client:
	void loadFromDisk(const std::string& path);
	void saveToDisk(const std::string& path);
private:
	std::string base_resource_dir;

	Mutex mutex;
	std::map<std::string, ResourceRef> resource_for_url;
	IndigoAtomic changed;
};
