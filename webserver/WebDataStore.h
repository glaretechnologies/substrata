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
	js::Vector<uint8, 16> uncompressed_data;
	js::Vector<uint8, 16> deflate_compressed_data;
	js::Vector<uint8, 16> zstd_compressed_data;
	std::string content_type;
};


class GenericPage : public ThreadSafeRefCounted
{
public:
	std::string page_title; // e.g. "Luau Scripting in Substrata"
	std::string fragment_path; // e.g. "about_luau_scripting.htmlfrag"
	std::string url_path; // e.g. "/about_luau_scripting"
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


	void parseGenericPageConfig();

	//std::string letsencrypt_webroot;
	std::string fragments_dir; // For HTML fragments
	std::string public_files_dir; // Holds stuff like main.css, map.js, logo_main_page.png etc. from webserver_public_files
	std::string webclient_dir; // Dir that webclient files are in - client.html, webclient.js etc..
	std::string screenshot_dir; // Dir generated screenshots (taken by the screenshot bot) will be saved to.
	std::string photo_dir; // Dir uploaded photos taken by users will be saved to.
	std::string public_images_dir; // Images served from the URLs /images/<filename>, e.g. images for news posts.  Served from disk, not held in memory like public_files.

	std::map<std::string, Reference<WebDataStoreFile>> fragment_files		GUARDED_BY(mutex);

	std::map<std::string, Reference<WebDataStoreFile>> public_files			GUARDED_BY(mutex);

	std::map<std::string, Reference<WebDataStoreFile>> webclient_dir_files	GUARDED_BY(mutex);

	std::map<std::string, Reference<GenericPage>> generic_pages				GUARDED_BY(mutex); // Map from URL path to page

	std::set<std::string> public_image_filenames							GUARDED_BY(mutex); // Names of the files in public_images_dir.  Only these files are served from /images/.

	Mutex mutex;


	std::string main_css_hash GUARDED_BY(hash_mutex);
	Mutex hash_mutex;
};
