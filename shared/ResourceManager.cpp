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
:	base_resource_dir(base_resource_dir_), changed(0)
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
const std::string ResourceManager::computeDefaultLocalPathForURL(const std::string& URL)
{
	const std::string path = base_resource_dir + "/" + escapeString(URL);
#ifdef WIN32
	if(path.size() >= MAX_PATH) // path length seems to include null terminator, e.g. paths of length 260 fail as well.
	{
		// Path is too long for windows.  Use a hash of the URL instead
		const uint64 hash = XXH64(URL.data(), URL.size(), /*seed=*/1);

		return base_resource_dir + "/" + toHexString(hash) + "." + ::getExtension(path);
	}
#endif

	return path;
}


const std::string ResourceManager::computeLocalPathFromURLHash(const std::string& URL, const std::string& extension)
{
	const uint64 hash = XXH64(URL.data(), URL.size(), /*seed=*/1);

	return base_resource_dir + "/" + toHexString(hash) + "." + extension;// ::getExtension(URL);
}


ResourceRef ResourceManager::getOrCreateResourceForURL(const std::string& URL) // Threadsafe
{
	Lock lock(mutex);

	auto res = resource_for_url.find(URL);
	if(res == resource_for_url.end())
	{
		// Insert it
		const std::string local_path = computeDefaultLocalPathForURL(URL);
		ResourceRef resource = new Resource(
			URL, 
			local_path,
			FileUtils::fileExists(local_path) ? Resource::State_Present : Resource::State_NotPresent,
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


std::string ResourceManager::copyLocalFileToResourceDir(const std::string& local_path) // Threadsafe
{
	try
	{
		const uint64 hash = FileChecksum::fileChecksum(local_path);
		const std::string URL = ResourceManager::URLForPathAndHash(local_path, hash);

		FileUtils::copyFile(local_path, this->pathForURL(URL));

		Lock lock(mutex);
		ResourceRef res = getOrCreateResourceForURL(URL);
		res->setState(Resource::State_Present);

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

	return resource->getLocalPath();

	//if(!isValidURL(URL))
	//	throw glare::Exception("Invalid URL '" + URL + "'");
	//return base_resource_dir + "/" + escapeString(URL);
}


bool ResourceManager::isFileForURLPresent(const std::string& URL) // Throws glare::Exception if URL is invalid.
{
	ResourceRef resource = this->getExistingResourceForURL(URL);
	return resource.nonNull() && (resource->getState() == Resource::State_Present);
}


void ResourceManager::addResource(ResourceRef& res)
{
	Lock lock(mutex);

	resource_for_url[res->URL] = res;
	res->setState(Resource::State_Present); // Assume present for now.

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


static const uint32 RESOURCE_MANAGER_MAGIC_NUMBER = 587732371;
static const uint32 RESOURCE_MANAGER_SERIALISATION_VERSION = 1;
static const uint32 RESOURCE_CHUNK = 103;
static const uint32 EOS_CHUNK = 1000;


void ResourceManager::loadFromDisk(const std::string& path)
{
	conPrint("Reading resources from '" + path + "'...");

	Lock lock(mutex);

	Timer timer;

	FileInStream stream(path);

	// Read magic number
	const uint32 m = stream.readUInt32();
	if(m != RESOURCE_MANAGER_MAGIC_NUMBER)
		throw glare::Exception("Invalid magic number " + toString(m) + ", expected " + toString(RESOURCE_MANAGER_MAGIC_NUMBER) + ".");

	// Read version
	const uint32 v = stream.readUInt32();
	if(v != RESOURCE_MANAGER_SERIALISATION_VERSION)
		throw glare::Exception("Unknown version " + toString(v) + ", expected " + toString(RESOURCE_MANAGER_SERIALISATION_VERSION) + ".");
	
	size_t num_resources_present = 0;
	while(1)
	{
		const uint32 chunk = stream.readUInt32();
		if(chunk == RESOURCE_CHUNK)
		{
			// Deserialise resource
			ResourceRef resource = new Resource();
			readFromStream(stream, *resource);

			// conPrint("Loaded resource:\n  URL: '" + resource->URL + "'\n  local_path: '" + resource->getLocalPath() + "'\n  owner_id: " + resource->owner_id.toString());

			resource_for_url[resource->URL] = resource;

			//TEMP:
			//if(resource->getLocalPath().size() >= 260)
			//	resource->setLocalPath(this->computeLocalPathFromURLHash(resource->URL, ::getExtension(resource->getLocalPath())));

			if(FileUtils::fileExists(resource->getLocalPath()))
			{
				resource->setState(Resource::State_Present);
				num_resources_present++;
			}
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

	conPrint("Loaded " + toString(resource_for_url.size()) + " resource(s).  (" + toString(num_resources_present) + " present on disk)  Elapsed: " + timer.elapsedStringNSigFigs(3) + "");
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
					writeToStream(*i->second, stream);
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
