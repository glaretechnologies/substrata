/*=====================================================================
ResourceManager.cpp
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-12 12:22:34 +1300
=====================================================================*/
#include "ResourceManager.h"


#include <ConPrint.h>
#include <StringUtils.h>
#include <FileUtils.h>
#include <Exception.h>
#include <FileChecksum.h>
#include <Lock.h>
#include <Timer.h>
#include <FileInStream.h>
#include <FileOutStream.h>
#include <IncludeXXHash.h>


ResourceManager::ResourceManager(const std::string& base_resource_dir_)
:	base_resource_dir(base_resource_dir_), changed(0), total_present_resources_size_B(0)
{
}


ResourceManager::~ResourceManager()
{
}


static std::string sanitiseString(const std::string& s)
{
	std::string res = s;
	for(size_t i=0; i<s.size(); ++i)
		if(!::isAlphaNumeric(s[i]))
			res[i] = '_';
	return res;
}


static std::string escapeString(const std::string& s)
{
	std::string res;
	res.reserve(s.size() * 2);
	for(size_t i=0; i<s.size(); ++i)
	{
		if(::isAlphaNumeric(s[i]) || s[i] == '_' || s[i] == '.')
			res.push_back(s[i]);
		else
		{
			res.push_back('_');
			res += toString((int)s[i]);
		}
	}
	return res;
}


const std::string ResourceManager::URLForNameAndExtensionAndHash(const std::string& name, const std::string& extension, uint64 hash)
{
	return sanitiseString(name) + "_" + toString(hash) + "." + extension;
}


// For a path like "d:/audio/some.mp3", returns a URL like "some_473446464646.mp3"
const std::string ResourceManager::URLForPathAndHash(const std::string& path, uint64 hash)
{
	const std::string filename = FileUtils::getFilename(path);

	const std::string extension = ::getExtension(filename);
	
	// NOTE: should really removeDotAndExtension() on filename below, but will change the file -> URL mapping, which will probably break something, or cause redundant uploads etc.
	return sanitiseString(filename) + "_" + toString(hash) + "." + extension;
}


bool ResourceManager::isValidURL(const std::string& URL)
{
	//for(size_t i=0; i<URL.size(); ++i)
	//	if(!(::isAlphaNumeric(URL[i]) || URL[i] == '_' || URL[i] == '.'))
	//		return false;
	return true;
}


// Compute default local path for URL.
const std::string ResourceManager::computeDefaultRawLocalPathForURL(const std::string& URL)
{
	const std::string raw_path = escapeString(URL);

	const std::string abs_path = this->base_resource_dir + "/" + raw_path;
#ifdef WIN32
	if(abs_path.size() >= MAX_PATH) // path length seems to include null terminator, e.g. paths of length 260 fail as well.
	{
		// Path is too long for windows.  Use a hash of the URL instead
		return computeRawLocalPathFromURLHash(URL, ::getExtension(raw_path));
	}
#endif

	return raw_path;
}


// Computes a path that doesn't contain the filename, just uses a hash of the filename.
const std::string ResourceManager::computeRawLocalPathFromURLHash(const std::string& URL, const std::string& extension)
{
	const uint64 hash = XXH64(URL.data(), URL.size(), /*seed=*/1);

	return toHexString(hash) + "." + extension;
}


const std::string ResourceManager::getLocalAbsPathForResource(const Resource& resource)
{
	return resource.getLocalAbsPath(base_resource_dir);
}


ResourceRef ResourceManager::getOrCreateResourceForURL(const std::string& URL) // Threadsafe
{
	Lock lock(mutex);

	auto res = resource_for_url.find(URL);
	if(res == resource_for_url.end())
	{
		// Insert it
		const std::string raw_local_path = computeDefaultRawLocalPathForURL(URL);
		const std::string abs_path = this->base_resource_dir + "/" + raw_local_path;

		ResourceRef resource = new Resource(
			URL, 
			raw_local_path,
			(!raw_local_path.empty() && FileUtils::fileExists(abs_path)) ? Resource::State_Present : Resource::State_NotPresent,
			UserID::invalidUserID()
		);
		resource_for_url[URL] = resource;
		this->changed = 1;
		return resource;
	}
	else
	{
		return res->second;
	}
}


// Returns null reference if no resource object for URL inserted.
ResourceRef ResourceManager::getExistingResourceForURL(const std::string& URL) // Threadsafe
{
	Lock lock(mutex);

	auto res = resource_for_url.find(URL);
	if(res == resource_for_url.end())
		return ResourceRef();
	else
		return res->second;
}


void ResourceManager::copyLocalFileToResourceDir(const std::string& local_path, const std::string& URL) // Threadsafe
{
	try
	{
		// Copy to destination path in resources dir, if not already present.
		const std::string dest_path = this->pathForURL(URL);
		if(!FileUtils::fileExists(dest_path))
			FileUtils::copyFile(local_path, dest_path);

		Lock lock(mutex);

		ResourceRef res = getExistingResourceForURL(URL);
		const bool already_exists = res.nonNull();
		if(res.isNull())
			res = getOrCreateResourceForURL(URL);

		const Resource::State prev_state = res->getState();
		res->setState(Resource::State_Present);

		if(!already_exists || (prev_state != Resource::State_Present))
			this->changed = 1;
	}
	catch(FileUtils::FileUtilsExcep& e)
	{
		throw glare::Exception(e.what());
	}
}


std::string ResourceManager::copyLocalFileToResourceDirIfNotPresent(const std::string& local_path) // Threadsafe
{
	try
	{
		const uint64 hash = FileChecksum::fileChecksum(local_path);
		const std::string URL = ResourceManager::URLForPathAndHash(local_path, hash);

		const std::string dest_path = this->pathForURL(URL);
		if(!FileUtils::fileExists(dest_path))
			FileUtils::copyFile(local_path, dest_path);

		ResourceRef res = getExistingResourceForURL(URL);
		const bool already_exists = res.nonNull();
		if(res.isNull())
			res = getOrCreateResourceForURL(URL);

		const Resource::State prev_state = res->getState();
		res->setState(Resource::State_Present);

		if(!already_exists || (prev_state != Resource::State_Present))
			this->changed = 1;

		return URL;
	}
	catch(FileUtils::FileUtilsExcep& e)
	{
		throw glare::Exception(e.what());
	}
}


void ResourceManager::setResourceAsLocallyPresentForURL(const std::string& URL) // Threadsafe
{
	try
	{
		assert(FileUtils::fileExists(this->pathForURL(URL)));

		Lock lock(mutex);
		ResourceRef res = getOrCreateResourceForURL(URL);
		res->setState(Resource::State_Present);

		this->changed = 1;
	}
	catch(FileUtils::FileUtilsExcep& e)
	{
		throw glare::Exception(e.what());
	}
}


const std::string ResourceManager::pathForURL(const std::string& URL)
{
	Lock lock(mutex);

	ResourceRef resource = this->getOrCreateResourceForURL(URL);

	return resource->getLocalAbsPath(this->base_resource_dir);

	//if(!isValidURL(URL))
	//	throw glare::Exception("Invalid URL '" + URL + "'");
	//return base_resource_dir + "/" + escapeString(URL);
}


bool ResourceManager::isFileForURLPresent(const std::string& URL) // Throws glare::Exception if URL is invalid.
{
	ResourceRef resource = this->getExistingResourceForURL(URL);
	return resource.nonNull() && (resource->getState() == Resource::State_Present);
}


// Used when running in a browser under Emscripten, since we use MEMFS currently, e.g. an in-memory filesystem.
// So we delete resources to free up RAM.
void ResourceManager::deleteResourceLocally(const ResourceRef& resource)
{
	Lock lock(mutex);

	if(resource.nonNull())
	{
		const std::string local_abs_path = resource->getLocalAbsPath(this->base_resource_dir);
		// conPrint("Deleting local resource '" + local_abs_path + "'...");
		FileUtils::deleteFile(local_abs_path);

		resource->setState(Resource::State_NotPresent);

		resource->locally_deleted = true;

		assert(this->total_present_resources_size_B >= (int64)resource->file_size_B);
		this->total_present_resources_size_B -= (int64)resource->file_size_B;

		this->changed = 1;
	}
}


void ResourceManager::addResource(ResourceRef& res)
{
	Lock lock(mutex);

	resource_for_url[res->URL] = res;

	this->changed = 1;
}


void ResourceManager::markAsChanged() // Thread-safe
{
	this->changed = 1;
}


void ResourceManager::addToDownloadFailedURLs(const std::string& URL)
{
	// conPrint("addToDownloadFailedURLs: " + URL);
	Lock lock(mutex);
	download_failed_URLs.insert(URL);
}


bool ResourceManager::isInDownloadFailedURLs(const std::string& URL) const
{
	Lock lock(mutex);
	return download_failed_URLs.count(URL) >= 1;
}


std::string ResourceManager::getDiagnostics() const
{
	Lock lock(mutex);

	size_t num_not_present = 0;
	size_t num_transferring = 0;
	size_t num_present = 0;
	size_t num_locally_deleted = 0;
	for(auto it = resource_for_url.begin(); it != resource_for_url.end(); ++it)
	{
		if(it->second->getState() == Resource::State_Present)
			num_present++;
		else if(it->second->getState() == Resource::State_Transferring)
			num_transferring++;
		else
			num_not_present++;

		if(it->second->locally_deleted)
			num_locally_deleted++;
	}

	std::string s;
	s += "Num resources:       " + toString(resource_for_url.size()) + "\n";
	s += "num_not_present:     " + toString(num_not_present) + "\n";
	s += "num_transferring:    " + toString(num_transferring) + "\n";
	s += "num_present:         " + toString(num_present) + "\n";
	s += "present total size:  " + getMBSizeString(total_present_resources_size_B) + "\n";
	s += "num_locally_deleted: " + toString(num_locally_deleted) + "\n";

	s += "Present resources:\n";
	const int max_num_to_display = 16;
	int i = 0;
	for(auto it = resource_for_url.begin(); (it != resource_for_url.end()) && (i < max_num_to_display); ++it)
	{
		if(it->second->getState() == Resource::State_Present)
		{
			s += "  '" + it->second->URL + "'\n";
			i++;
		}
	}
	if(num_present > max_num_to_display)
		s += "  ...\n";
	return s;
}


static const uint32 RESOURCE_MANAGER_MAGIC_NUMBER = 587732371;
static const uint32 RESOURCE_MANAGER_SERIALISATION_VERSION = 2;
static const uint32 RESOURCE_CHUNK = 103;
static const uint32 EOS_CHUNK = 1000;
/*
Version history:
2: Serialising resource state
*/

void ResourceManager::loadFromDisk(const std::string& path, bool force_check_if_resources_exist_on_disk)
{
	conPrint("Reading resource info from '" + path + "'...");

	Lock lock(mutex);

	Timer timer;

	FileInStream stream(path);

	// Read magic number
	const uint32 m = stream.readUInt32();
	if(m != RESOURCE_MANAGER_MAGIC_NUMBER)
		throw glare::Exception("Invalid magic number " + toString(m) + ", expected " + toString(RESOURCE_MANAGER_MAGIC_NUMBER) + ".");

	// Read version
	const uint32 version = stream.readUInt32();
	if(version > RESOURCE_MANAGER_SERIALISATION_VERSION)
		throw glare::Exception("Unknown version " + toString(version) + ", expected " + toString(RESOURCE_MANAGER_SERIALISATION_VERSION) + ".");
	
	// From version 2, we save the resource state with the resources, so we don't have to recompute it when loading the resources.
	const bool check_resources_present_on_disk = (version == 1) || force_check_if_resources_exist_on_disk;

	size_t num_resources_present = 0;
	while(1)
	{
		const uint32 chunk = stream.readUInt32();
		if(chunk == RESOURCE_CHUNK)
		{
			// Deserialise resource
			ResourceRef resource = new Resource();
			readFromStream(stream, *resource); // NOTE: for old resource versions (< 4), will convert absolute local paths to relative local paths.

			// conPrint("Loaded resource:\n  URL: '" + resource->URL + "'\n  local_path: '" + resource->getLocalPath() + "'\n  owner_id: " + resource->owner_id.toString());

			resource_for_url[resource->URL] = resource;

			//TEMP:
			//if(resource->getLocalPath().size() >= 260)
			//	resource->setLocalPath(this->computeLocalPathFromURLHash(resource->URL, ::getExtension(resource->getLocalPath())));

			const Resource::State prev_resource_state = resource->getState();

			if(check_resources_present_on_disk)
			{
				if(FileUtils::fileExists(resource->getLocalAbsPath(this->base_resource_dir)))
				{
					resource->setState(Resource::State_Present);
					num_resources_present++;
				}
				else
				{
					resource->setState(Resource::State_NotPresent);
				}
			}
			else
			{
				if(resource->getState() == Resource::State_Present)
				{
					num_resources_present++;
				}
				else if(resource->getState() == Resource::State_Transferring)
				{
					// Any resources that were transferring when the resources database was last saved, may not have been completely downloaded.
					// Mark them as NotPresent so they will be re-downloaded.
					resource->setState(Resource::State_NotPresent);
				}
			}

			if(resource->getState() != prev_resource_state) // Set changed flag for DB if we changed a resource state, so the DB gets saved to disk.
				this->changed = 1;
		}
		else if(chunk == EOS_CHUNK)
		{
			break;
		}
		else
		{
			throw glare::Exception("Unknown chunk type '" + toString(chunk) + "'");
		}
	}

	conPrint("Loaded info on " + toString(resource_for_url.size()) + " resource(s). (check_resources_present_on_disk: " + boolToString(check_resources_present_on_disk) + ", " + 
		toString(num_resources_present) + " present on disk, changed: " + boolToString(changed) + ")  Elapsed: " + timer.elapsedStringNSigFigs(3) + "");
}


void ResourceManager::saveToDisk(const std::string& path)
{
	conPrint("Saving resources to disk...");
	Timer timer;

	Lock lock(mutex);

	try
	{
		const std::string temp_path = path + "_temp";

		{
			FileOutStream stream(temp_path);

			// Write magic number
			stream.writeUInt32(RESOURCE_MANAGER_MAGIC_NUMBER);

			// Write version
			stream.writeUInt32(RESOURCE_MANAGER_SERIALISATION_VERSION);

			// Write resource objects
			{
				for(auto i=resource_for_url.begin(); i != resource_for_url.end(); ++i)
				{
					stream.writeUInt32(RESOURCE_CHUNK);
					i->second->writeToStream(stream);
				}
			}

			stream.writeUInt32(EOS_CHUNK); // Write end-of-stream chunk
		}

		FileUtils::moveFile(temp_path, path);

		conPrint("\tDone saving resources to disk.  (Elapsed: " + timer.elapsedStringNSigFigs(3) + ")");
	}
	catch(FileUtils::FileUtilsExcep& e)
	{
		throw glare::Exception(e.what());
	}
}


void ResourceManager::addResourceSizeToTotalPresent(ResourceRef& res)
{
	Lock lock(mutex);
	total_present_resources_size_B += (int64)res->file_size_B;
}


int64 ResourceManager::getTotalPresentResourcesSizeB() const
{
	Lock lock(mutex);
	return total_present_resources_size_B;
}
