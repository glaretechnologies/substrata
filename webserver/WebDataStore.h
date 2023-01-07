/*=====================================================================
WebDataStore.h
--------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include <Vector.h>
#include <Mutex.h>
#include <string>
#include <set>
#include <map>



class WebDataStoreFile : public ThreadSafeRefCounted
{
public:
	js::Vector<uint8, 16> data;
	bool compressed;
	std::string content_type;
};


/*=====================================================================
WebDataStore
------------
=====================================================================*/
class WebDataStore : public ThreadSafeRefCounted
{
public:
	WebDataStore();
	~WebDataStore();

	void loadAndCompressFiles(); // Loads or reloads files.  Compresses files if needed.

	Reference<WebDataStoreFile> getFragmentFile(const std::string& path); // Returns NULL if not found


	//std::string letsencrypt_webroot;
	std::string fragments_dir; // For HTML fragments
	std::string public_files_dir;
	std::string webclient_dir; // Dir that webclient files are in - client.html, webclient.js etc..
	std::string screenshot_dir;

	std::map<std::string, Reference<WebDataStoreFile>> fragment_files		GUARDED_BY(mutex);

	std::map<std::string, Reference<WebDataStoreFile>> public_files			GUARDED_BY(mutex);

	std::map<std::string, Reference<WebDataStoreFile>> webclient_dir_files	GUARDED_BY(mutex);

	Mutex mutex;
};
