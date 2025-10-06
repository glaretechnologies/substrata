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
#include <tracy/Tracy.hpp>


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


const URLString ResourceManager::URLForNameAndExtensionAndHash(const std::string& name, const std::string& extension, uint64 hash)
{
	return toURLString(sanitiseString(name) + "_" + toString(hash) + "." + extension);
}


// For a path like "d:/audio/some.mp3", returns a URL like "some_473446464646.mp3"
const URLString ResourceManager::URLForPathAndHash(const std::string& path, uint64 hash)
{
	const std::string filename = FileUtils::getFilename(path);

	const std::string extension = ::getExtension(filename);
	
	// NOTE: should really removeDotAndExtension() on filename below, but will change the file -> URL mapping, which will probably break something, or cause redundant uploads etc.
	return toURLString(sanitiseString(filename) + "_" + toString(hash) + "." + extension);
}


const URLString ResourceManager::URLForPathAndHashAndEpoch(const std::string& path, uint64 hash, int epoch)
{
	const std::string filename = FileUtils::getFilename(path);

	const std::string extension = ::getExtension(filename);

	return toURLString(sanitiseString(removeDotAndExtension(filename)) + "_" + toString(hash) + "_" + toString(epoch) + "." + extension);
}


bool ResourceManager::isValidURL(const URLString& URL)
{
	//for(size_t i=0; i<URL.size(); ++i)
	//	if(!(::isAlphaNumeric(URL[i]) || URL[i] == '_' || URL[i] == '.'))
	//		return false;
	return true;
}


// Compute default local path for URL.
const std::string ResourceManager::computeDefaultRawLocalPathForURL(const URLString& URL)
{
	const std::string raw_path = escapeString(toStdString(URL));

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
const std::string ResourceManager::computeRawLocalPathFromURLHash(const URLString& URL, const std::string& extension)
{
	const uint64 hash = XXH64(URL.data(), URL.size(), /*seed=*/1);

	return toHexString(hash) + "." + extension;
}


const std::string ResourceManager::getLocalAbsPathForResource(const Resource& resource)
{
	return resource.getLocalAbsPath(base_resource_dir);
}


ResourceRef ResourceManager::getOrCreateResourceForURL(const URLString& URL) // Threadsafe
{
	Lock lock(mutex);

	auto res = resource_for_url.find(URL);
	if(res == resource_for_url.end())
	{
		const URLString URL_copy(URL.begin(), URL.end()); // Copy to avoid allocating from arena allocator.
		assert(URL_copy.get_allocator().arena_allocator == nullptr);
		// Insert it
		const std::string raw_local_path = computeDefaultRawLocalPathForURL(URL);
		const std::string abs_path = this->base_resource_dir + "/" + raw_local_path;

		ResourceRef resource = new Resource(
			URL_copy, 
			raw_local_path,
			Resource::State_NotPresent,
			UserID::invalidUserID(),
			/*external_resource=*/false
		);
		resource_for_url[URL_copy] = resource;
		this->changed = 1;
		return resource;
	}
	else
	{
		return res->second;
	}
}


// Returns null reference if no resource object for URL inserted.
ResourceRef ResourceManager::getExistingResourceForURL(const URLString& URL) // Threadsafe
{
	Lock lock(mutex);

	auto res = resource_for_url.find(URL);
	if(res == resource_for_url.end())
		return ResourceRef();
	else
		return res->second;
}


void ResourceManager::copyLocalFileToResourceDir(const std::string& local_path, const URLString& URL) // Threadsafe
{
	std::string dest_path; // local resource path to copy to if resource is not present locally in resource dir.

	{
		Lock lock(mutex);
		ResourceRef resource = getOrCreateResourceForURL(URL);
		if(resource->getState() != Resource::State_Present)
			dest_path = resource->getLocalAbsPath(this->base_resource_dir);
	}

	if(!dest_path.empty()) // If resource was not present:
	{
		// Do copy without holding lock
		FileUtils::copyFile(local_path, dest_path);

		// Now mark resource as present and set changed flag.
		{
			Lock lock(mutex);
			ResourceRef resource = getOrCreateResourceForURL(URL);
			resource->setState(Resource::State_Present);
			this->changed = 1;
		}
	}
}


URLString ResourceManager::copyLocalFileToResourceDirAndReturnURL(const std::string& local_path) // Threadsafe
{
	const uint64 hash = FileChecksum::fileChecksum(local_path);
	const URLString URL = ResourceManager::URLForPathAndHash(local_path, hash);

	copyLocalFileToResourceDir(local_path, URL);

	return URL;
}


void ResourceManager::setResourceAsLocallyPresentForURL(const URLString& URL) // Threadsafe
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


const std::string ResourceManager::pathForURL(const URLString& URL)
{
	Lock lock(mutex);

	ResourceRef resource = this->getOrCreateResourceForURL(URL);

	return resource->getLocalAbsPath(this->base_resource_dir);

	//if(!isValidURL(URL))
	//	throw glare::Exception("Invalid URL '" + URL + "'");
	//return base_resource_dir + "/" + escapeString(URL);
}

void ResourceManager::getTexPathForURL(const URLString& URL, OpenGLTextureKey& path_out)
{
	Lock lock(mutex);

	ResourceRef resource = this->getOrCreateResourceForURL(URL);

	resource->getLocalAbsTexPath(this->base_resource_dir, path_out);
}


bool ResourceManager::isFileForURLPresent(const URLString& URL) // Throws glare::Exception if URL is invalid.
{
	ResourceRef resource = this->getExistingResourceForURL(URL);
	return resource.nonNull() && (resource->getState() == Resource::State_Present);
}


void ResourceManager::addResource(const ResourceRef& res)
{
	Lock lock(mutex);

	resource_for_url[res->URL] = res;

	this->changed = 1;
}


void ResourceManager::markAsChanged() // Thread-safe
{
	this->changed = 1;
}


void ResourceManager::addToDownloadFailedURLs(const URLString& URL)
{
	// conPrint("addToDownloadFailedURLs: " + URL);
	Lock lock(mutex);
	download_failed_URLs.insert(URL);
}


void ResourceManager::removeFromDownloadFailedURLs(const URLString& URL)
{
	Lock lock(mutex);
	download_failed_URLs.erase(URL);
}


bool ResourceManager::isInDownloadFailedURLs(const URLString& URL) const
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
	for(auto it = resource_for_url.begin(); it != resource_for_url.end(); ++it)
	{
		if(it->second->getState() == Resource::State_Present)
			num_present++;
		else if(it->second->getState() == Resource::State_Transferring)
			num_transferring++;
		else
			num_not_present++;
	}

	std::string s;
	s += "Num resources:       " + toString(resource_for_url.size()) + "\n";
	s += "num_not_present:     " + toString(num_not_present) + "\n";
	s += "num_transferring:    " + toString(num_transferring) + "\n";
	s += "num_present:         " + toString(num_present) + "\n";
	s += "total_unused_loaded_buffer_size_B:  " + getMBSizeString(total_unused_loaded_buffer_size_B) + "\n";

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
	ZoneScoped; // Tracy profiler
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
					const Resource* resource = i->second.ptr();
					if(!resource->external_resource) // Don't save external resources to disk.
					{
						stream.writeUInt32(RESOURCE_CHUNK);
						resource->writeToStream(stream);
					}
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
