/*=====================================================================
WebDataStore.cpp
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "WebDataStore.h"


#include <ResponseUtils.h>
#include <utils/MemMappedFile.h>
#include <utils/Exception.h>
#include <utils/StringUtils.h>
#include <utils/ConPrint.h>
#include <utils/FileUtils.h>
#include <utils/Lock.h>
#include <zlib.h>
#include <zstd.h>
#include <Timer.h>



WebDataStore::WebDataStore() {}


WebDataStore::~WebDataStore() {}



static void compressFile(Reference<WebDataStoreFile> file, const std::string& path)
{
	// Do deflate compression

	{
		Timer timer;

		const uLong bound = compressBound((uLong)file->uncompressed_data.size());

		file->deflate_compressed_data.resizeNoCopy(bound);
		uLong dest_len = bound;

		const int result = ::compress2(
			file->deflate_compressed_data.data(), // dest
			&dest_len, // dest len
			(Bytef*)file->uncompressed_data.data(), // source
			(uLong)file->uncompressed_data.size(), // source len
#if BUILD_TESTS
			Z_BEST_SPEED // Compression level (don't spend long compressing for debug modes)
#else
			Z_BEST_COMPRESSION // Compression level
#endif
		);

		if(result != Z_OK)
			throw glare::Exception("Compression failed.");

		file->deflate_compressed_data.resize(dest_len);

		conPrint("Compressed file '" + path + "' from " + toString(file->uncompressed_data.size()) + " B to " + toString((uint64)dest_len) + " B with deflate (" + 
			doubleToStringNSigFigs((double)dest_len / file->uncompressed_data.size(), 4) + " dest/src size ratio).  Elapsed: " + timer.elapsedStringNPlaces(3));
	}

	// Do zstd compression
	{
		Timer timer;

		const size_t compressed_bound = ZSTD_compressBound(file->uncompressed_data.size());

		file->zstd_compressed_data.resizeNoCopy(compressed_bound);

		// Chrome seems to not be able to decompress Zstd data with compression levels >= 20 ('ultra' compression levels), see https://issues.chromium.org/issues/41493659
		// So use 19.

		const size_t compressed_size = ZSTD_compress(
			/*dest=*/file->zstd_compressed_data.data(), /*dest capacity=*/file->zstd_compressed_data.size(), 
			/*src=*/file->uncompressed_data.data(), /*src size=*/file->uncompressed_data.size(),
#if BUILD_TESTS
			1 //  (don't spend long compressing for debug modes)
#else
			19 // compression level
#endif
		);
		if(ZSTD_isError(compressed_size))
			throw glare::Exception(std::string("Compression failed: ") + ZSTD_getErrorName(compressed_size));

		// Trim compressed_data
		file->zstd_compressed_data.resize(compressed_size);

		conPrint("Compressed file '" + path + "' from " + toString(file->uncompressed_data.size()) + " B to " + toString(compressed_size) + " B with zstd (" + 
			doubleToStringNSigFigs((double)compressed_size / file->uncompressed_data.size(), 4) + " dest/src size ratio).  Elapsed: " + timer.elapsedStringNPlaces(3));
	}
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
	file->uncompressed_data = readFile(path);

	const bool should_compresss = shouldCompressFile(path);
	if(should_compresss)
		compressFile(file, path);
	
	file->content_type = web::ResponseUtils::getContentTypeForPath(path);
	return file;
}


void WebDataStore::loadAndCompressFiles()
{
	conPrint("WebDataStore::loadAndCompressFiles");

	//-------------- Load (HTML) fragment files --------------
	const std::vector<std::string> fragment_filenames = FileUtils::getFilesInDir(this->fragments_dir);

	for(auto it = fragment_filenames.begin(); it != fragment_filenames.end(); ++it)
	{
		const std::string filename = *it;
		const std::string path = fragments_dir + "/" + filename;

		try
		{
			conPrint("Loading fragment from '" + path + "'...");
			Reference<WebDataStoreFile> file = loadAndMaybeCompressFile(path);

			{
				Lock lock(mutex);
				fragment_files[filename] = file;
			}
		}
		catch(glare::Exception& e)
		{
			conPrint("WebDataStore::loadAndCompressFiles: warning: " + e.what());
		}
	}


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


Reference<WebDataStoreFile> WebDataStore::getFragmentFile(const std::string& path) // Returns NULL if not found
{
	Lock lock(mutex);
	const auto lookup_res = fragment_files.find(path);
	if(lookup_res != fragment_files.end())
		return lookup_res->second;
	else
		return Reference<WebDataStoreFile>();
}
