/*=====================================================================
WebDataStore.cpp
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "WebDataStore.h"


#include <MemMappedFile.h>
#include <Exception.h>
#include <StringUtils.h>
#include <ConPrint.h>
#include <FileUtils.h>
#include <Lock.h>
#include <zlib.h>
#include <ResponseUtils.h>


WebDataStore::WebDataStore() {}


WebDataStore::~WebDataStore() {}



static js::Vector<uint8, 16> compressFile(const std::string& path)
{
	MemMappedFile file(path);
	
	const uLong bound = compressBound((uLong)file.fileSize());

	js::Vector<uint8, 16> compressed_data(bound);
	uLong dest_len = bound;

	const int result = ::compress2(
		compressed_data.data(), // dest
		&dest_len, // dest len
		(Bytef*)file.fileData(), // source
		(uLong)file.fileSize(), // source len
		Z_BEST_COMPRESSION // Compression level
	);

	if(result != Z_OK)
		throw glare::Exception("Compression failed.");

	compressed_data.resize(dest_len);

	// conPrint("Compressed file '" + path + "' from " + toString(file.fileSize()) + " B to " + toString((uint32)dest_len) + " B");

	return compressed_data;
}


static js::Vector<uint8, 16> readFile(const std::string& path)
{
	MemMappedFile file(path);

	js::Vector<uint8, 16> data(file.fileSize());
	BitUtils::checkedMemcpy(data.data(), file.fileData(), file.fileSize());
	return data;
}


static bool shouldCompressFile(const std::string& path)
{
	return 
		hasExtensionStringView(path, "js") || 
		hasExtensionStringView(path, "css") || 
		hasExtensionStringView(path, "wasm") || 
		hasExtensionStringView(path, "html");
}


static Reference<WebDataStoreFile> loadAndMaybeCompressFile(const std::string& path)
{
	Reference<WebDataStoreFile> file = new WebDataStoreFile();
	const bool should_compresss = shouldCompressFile(path);
	if(should_compresss)
	{
		file->data = compressFile(path);
		file->compressed = true;
	}
	else
	{
		file->data = readFile(path);
		file->compressed = false;
	}
	file->content_type = web::ResponseUtils::getContentTypeForPath(path);
	return file;
}


void WebDataStore::loadAndCompressFiles()
{
	conPrint("WebDataStore::loadAndCompressFiles");

	//-------------- Load public files --------------
	const std::vector<std::string> public_file_filenames = FileUtils::getFilesInDir(this->public_files_dir);

	for(auto it = public_file_filenames.begin(); it != public_file_filenames.end(); ++it)
	{
		const std::string filename = *it;
		const std::string path = public_files_dir + "/" + filename;

		try
		{
			//conPrint("Loading file '" + path + "'...");
			Reference<WebDataStoreFile> file = loadAndMaybeCompressFile(path);

			{
				Lock lock(mutex);
				public_files[filename] = file;
			}
		}
		catch(glare::Exception& e)
		{
			conPrint("WebDataStore::loadAndCompressFiles: warning: " + e.what());
		}
	}


	//-------------- Load webclient files --------------
	const std::vector<std::string> webclient_paths = FileUtils::getFilesInDirRecursive(this->webclient_dir);

	for(auto it = webclient_paths.begin(); it != webclient_paths.end(); ++it)
	{
		const std::string relative_path = *it; // path relative to webclient_dir.
		const std::string path = this->webclient_dir + "/" + relative_path;

		try
		{
			//conPrint("Loading file '" + path + "'...");
			Reference<WebDataStoreFile> file = loadAndMaybeCompressFile(path);

			const std::string use_relative_path = StringUtils::replaceCharacter(relative_path, '\\', '/'); // Replace backslashes with forward slashes.
			{
				Lock lock(mutex);
				webclient_dir_files[use_relative_path] = file;
			}
		}
		catch(glare::Exception& e)
		{
			conPrint("WebDataStore::loadAndCompressFiles: warning: " + e.what());
		}
	}

	//conPrint("WebDataStore::loadAndCompressFiles done.");
}
