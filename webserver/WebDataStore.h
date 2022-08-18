/*=====================================================================
WebDataStore.h
--------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include <ThreadSafeRefCounted.h>
#include <string>
#include <set>


/*=====================================================================
WebDataStore
------------
Actually just stores a bunch of disk paths currently.
=====================================================================*/
class WebDataStore : public ThreadSafeRefCounted
{
public:
	WebDataStore();
	~WebDataStore();

	std::string letsencrypt_webroot;
	std::string public_files_dir;
	std::string webclient_dir; // Dir that webclient files are in - client.html, webclient.js etc..
	std::string screenshot_dir;

	// Filenames of files in public_files_dir
	std::set<std::string> public_file_filenames;
};
