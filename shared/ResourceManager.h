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
#include <Reference.h>
#include <map>
#include <Mutex.h>


class Resource : public ThreadSafeRefCounted
{
public:
	enum State
	{
		State_NotPresent,
		State_Downloading,
		State_Present
	};

	Resource(const std::string& URL_, const std::string& local_path_, State s) : URL(URL_), local_path(local_path_), state(s) {}
	Resource() : state(State_NotPresent) {}
	
	const std::string getLocalPath() const { return local_path; }
	void setLocalPath(const std::string& p) { local_path = p; }

	State getState() const { return state; }
	void setState(State s) { state = s; }
	
	std::string URL;
private:
	State state; // May be protected by mutex soon.
	std::string local_path;
};

typedef Reference<Resource> ResourceRef;


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


	ResourceRef getResourceForURL(const std::string& URL); // Threadsafe

	// Copy a local file with given local path and corresponding URL into the resource dir
	// Throws Indigo::Exception on failure.
	void copyLocalFileToResourceDir(const std::string& local_path, const std::string& URL); // Threadsafe


	const std::string pathForURL(const std::string& URL); // Throws Indigo::Exception if URL is invalid.

	bool isFileForURLPresent(const std::string& URL); // Throws Indigo::Exception if URL is invalid.

private:
	std::string base_resource_dir;

	Mutex mutex;
	std::map<std::string, ResourceRef> resource_for_url;
};
