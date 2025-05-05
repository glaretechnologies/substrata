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
#include <unordered_set>
#include <unordered_map>
#include <Mutex.h>
#include <AtomicInt.h>


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
	// For a path like "d:/audio/some.mp3", returns a URL like "some_473446464646.mp3"
	static const std::string URLForPathAndHash(const std::string& path, uint64 hash);

	static const std::string URLForPathAndHashAndEpoch(const std::string& path, uint64 hash, int epoch);

	static bool isValidURL(const std::string& URL);

	// Will create a new Resource object if not already inserted.
	ResourceRef getOrCreateResourceForURL(const std::string& URL); // Threadsafe

	// Returns null reference if no resource object for URL inserted.
	ResourceRef getExistingResourceForURL(const std::string& URL); // Threadsafe

	// Copy a local file with given local path and corresponding URL into the resource dir, if there is no file in the
	// destination location.
	// Throws glare::Exception on failure.
	void copyLocalFileToResourceDir(const std::string& local_path, const std::string& URL); // Threadsafe

	// Returns URL
	std::string copyLocalFileToResourceDirIfNotPresent(const std::string& local_path); // Threadsafe

	void setResourceAsLocallyPresentForURL(const std::string& URL); // Threadsafe

	// Get local, absolute path for the URL.
	// NOTE: currently has the side-effect of adding a resource to the resource map if it was not already present.
	const std::string pathForURL(const std::string& URL); // Throws glare::Exception if URL is invalid.

	const std::string computeDefaultRawLocalPathForURL(const std::string& URL); // Compute default local path for URL.

	// Computes a path that doesn't contain the filename, just uses a hash of the filename.
	static const std::string computeRawLocalPathFromURLHash(const std::string& URL, const std::string& extension);

	const std::string getLocalAbsPathForResource(const Resource& resource);

	bool isFileForURLPresent(const std::string& URL); // Throws glare::Exception if URL is invalid.

	void addToDownloadFailedURLs(const std::string& URL);
	void removeFromDownloadFailedURLs(const std::string& URL);
	bool isInDownloadFailedURLs(const std::string& URL) const;

	// Used for deserialising resource objects from serialised server state.
	void addResource(ResourceRef& res);
	const std::unordered_map<std::string, ResourceRef>& getResourcesForURL() const { return resource_for_url; }
	std::unordered_map<std::string, ResourceRef>& getResourcesForURL() { return resource_for_url; }

	bool hasChanged() const { return changed != 0; }
	void clearChangedFlag() { changed = 0; }
	void markAsChanged(); // Thread-safe

	Mutex& getMutex() { return mutex; }

	// Just used on client:
	void loadFromDisk(const std::string& path, bool force_check_if_resources_exist_on_disk);
	void saveToDisk(const std::string& path);

	std::string getDiagnostics() const;
private:
	std::string base_resource_dir;

	mutable Mutex mutex;
	std::unordered_map<std::string, ResourceRef> resource_for_url			GUARDED_BY(mutex); // Use unordered_map for now instead of HashMap so we don't need to specify an empty key.
	glare::AtomicInt changed;


	std::unordered_set<std::string> download_failed_URLs; // Ephemeral state, used to prevent trying to download the same resource over and over again in one client execution.

public:
	// Total amount of memory in LoadedBuffer objects that are not persistently used.
	glare::AtomicInt total_unused_loaded_buffer_size_B;
};


typedef Reference<ResourceManager> ResourceManagerRef;
