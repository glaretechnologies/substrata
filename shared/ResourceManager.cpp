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


ResourceManager::ResourceManager(const std::string& base_resource_dir_)
:	base_resource_dir(base_resource_dir_)
{
}


ResourceManager::~ResourceManager()
{
}


const std::string ResourceManager::URLForPathAndHash(const std::string& path, uint64 hash)
{
	const std::string filename = FileUtils::getFilename(path);

	const std::string extension = ::getExtension(filename);
	
	std::string URL = filename;
	for(size_t i=0; i<URL.size(); ++i)
		if(!::isAlphaNumeric(URL[i]))
			URL[i] = '_';

	return URL + "_" + toString(hash) + "." + extension;
}


bool ResourceManager::isValidURL(const std::string& URL)
{
	for(size_t i=0; i<URL.size(); ++i)
		if(!(::isAlphaNumeric(URL[i]) || URL[i] == '_' || URL[i] == '.'))
			return false;
	return true;
}


const std::string ResourceManager::pathForURL(const std::string& URL)
{
	if(!isValidURL(URL))
		throw Indigo::Exception("Invalid URL '" + URL + "'");
	return base_resource_dir + "/" + URL;
}
