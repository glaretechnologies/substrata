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


ResourceManager::ResourceManager(const std::string& base_resource_dir_)
:	base_resource_dir(base_resource_dir_)
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
		if(::isAlphaNumeric(s[i]) || s[i] == '.' || s[i] == '.')
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


const std::string ResourceManager::URLForPathAndHash(const std::string& path, uint64 hash)
{
	const std::string filename = FileUtils::getFilename(path);

	const std::string extension = ::getExtension(filename);
	
	return sanitiseString(filename) + "_" + toString(hash) + "." + extension;
}


bool ResourceManager::isValidURL(const std::string& URL)
{
	//for(size_t i=0; i<URL.size(); ++i)
	//	if(!(::isAlphaNumeric(URL[i]) || URL[i] == '_' || URL[i] == '.'))
	//		return false;
	return true;
}


ResourceRef ResourceManager::getResourceForURL(const std::string& URL) // Threadsafe
{
	Lock lock(mutex);

	auto res = resource_for_url.find(URL);
	if(res == resource_for_url.end())
	{
		// Insert it
		const std::string local_path = base_resource_dir + "/" + escapeString(URL);
		ResourceRef resource = new Resource(
			URL, 
			local_path,
			FileUtils::fileExists(local_path) ? Resource::State_Present : Resource::State_NotPresent
		);
		resource_for_url[URL] = resource;
		return resource;
	}
	else
	{
		return res->second;
	}
}


void ResourceManager::copyLocalFileToResourceDir(const std::string& local_path, const std::string& URL) // Threadsafe
{
	try
	{
		FileUtils::copyFile(local_path, this->pathForURL(URL));

		Lock lock(mutex);
		ResourceRef res = getResourceForURL(URL);
		res->setState(Resource::State_Present);
	}
	catch(FileUtils::FileUtilsExcep& e)
	{
		throw Indigo::Exception(e.what());
	}
}


const std::string ResourceManager::pathForURL(const std::string& URL)
{
	Lock lock(mutex);

	ResourceRef resource = this->getResourceForURL(URL);

	return resource->getLocalPath();

	//if(!isValidURL(URL))
	//	throw Indigo::Exception("Invalid URL '" + URL + "'");
	//return base_resource_dir + "/" + escapeString(URL);
}


bool ResourceManager::isFileForURLDownloaded(const std::string& URL) // Throws Indigo::Exception if URL is invalid.
{
	ResourceRef resource = this->getResourceForURL(URL);
	return resource->getState() == Resource::State_Present;
}
