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
#include "URLString.h"
#include <ThreadSafeRefCounted.h>
#include <STLArenaAllocator.h>
#if GUI_CLIENT
#include <opengl/OpenGLTexture.h>
#endif
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
	static const URLString URLForNameAndExtensionAndHash(const std::string& name, const std::string& extension, uint64 hash);

	// Used for importing resources into the cyberspace resource system.
	// For a path like "d:/audio/some.mp3", returns a URL like "some_473446464646.mp3"
	static const URLString URLForPathAndHash(const std::string& path, uint64 hash);

	static const URLString URLForPathAndHashAndEpoch(const std::string& path, uint64 hash, int epoch);

	static bool isValidURL(const URLString& URL);

	// Will create a new Resource object with state NotPresent if not already inserted.
	ResourceRef getOrCreateResourceForURL(const URLString& URL); // Threadsafe

	// Returns null reference if no resource object for URL inserted.
	ResourceRef getExistingResourceForURL(const URLString& URL); // Threadsafe

	// Copy a local file with given local path and corresponding URL into the resource dir, if there is no file in the
	// destination location.
	// Throws glare::Exception on failure.
	void copyLocalFileToResourceDir(const std::string& local_path, const URLString& URL); // Threadsafe

	// Returns URL
	URLString copyLocalFileToResourceDirAndReturnURL(const std::string& local_path); // Threadsafe

	void setResourceAsLocallyPresentForURL(const URLString& URL); // Threadsafe

	// Get local, absolute path for the URL.
	// NOTE: currently has the side-effect of adding a resource to the resource map if it was not already present.
	const std::string pathForURL(const URLString& URL); // Throws glare::Exception if URL is invalid.
#if GUI_CLIENT
	void getTexPathForURL(const URLString& URL, OpenGLTextureKey& path_out);
#endif

	const std::string computeDefaultRawLocalPathForURL(const URLString& URL); // Compute default local path for URL.

	// Computes a path that doesn't contain the filename, just uses a hash of the filename.
	static const std::string computeRawLocalPathFromURLHash(const URLString& URL, const std::string& extension);

	const std::string getLocalAbsPathForResource(const Resource& resource);

	bool isFileForURLPresent(const URLString& URL); // Throws glare::Exception if URL is invalid.

	void addToDownloadFailedURLs(const URLString& URL);
	void removeFromDownloadFailedURLs(const URLString& URL);
	bool isInDownloadFailedURLs(const URLString& URL) const;

	// Used for deserialising resource objects from serialised server state.
	void addResource(const ResourceRef& res);
	const std::unordered_map<URLString, ResourceRef>& getResourcesForURL() const REQUIRES(mutex) { return resource_for_url; }
	std::unordered_map<URLString, ResourceRef>& getResourcesForURL() REQUIRES(mutex) { return resource_for_url; }

	bool hasChanged() const { return changed != 0; }
	void clearChangedFlag() { changed = 0; }
	void markAsChanged(); // Thread-safe

	Mutex& getMutex() RETURN_CAPABILITY(mutex) { return mutex; }

	// Just used on client:
	void loadFromDisk(const std::string& path, bool force_check_if_resources_exist_on_disk);
	void saveToDisk(const std::string& path);

	std::string getDiagnostics() const;
private:
	std::string base_resource_dir;

	mutable Mutex mutex;
	std::unordered_map<URLString, ResourceRef> resource_for_url			GUARDED_BY(mutex); // Use unordered_map for now instead of HashMap so we don't need to specify an empty key.
	glare::AtomicInt changed;


	std::unordered_set<URLString> download_failed_URLs; // Ephemeral state, used to prevent trying to download the same resource over and over again in one client execution.

public:
	// Total amount of memory in LoadedBuffer objects that are not persistently used.
	glare::AtomicInt total_unused_loaded_buffer_size_B;
};


typedef Reference<ResourceManager> ResourceManagerRef;
