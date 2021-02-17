/*=====================================================================
WebDataStore.cpp
-------------------
Copyright Glare Technologies Limited 2013 -
Generated at 2013-04-16 22:29:52 +0100
=====================================================================*/
#include "WebDataStore.h"


#include "WebsiteExcep.h"
#include <Lock.h>
#include <FileOutStream.h>
#include <FileInStream.h>
#include <Exception.h>
#include <ConPrint.h>
#include <StringUtils.h>


WebDataStore::WebDataStore(/*const std::string& path_*/)
{
	// TEMP HACK:
	/*User nick_user;
	nick_user.name = "nick";
	nick_user.email_address = "nick@indigorenderer.com";
	nick_user.password_hash_salt = "&GH5gTuD*(HnFDuGFj";
	nick_user.hashed_password = User::computePasswordHash("jxzqchjg&%$Fi^%d(^K", nick_user.password_hash_salt);

	users.push_back(nick_user);*/
}


WebDataStore::~WebDataStore()
{

}

//
//static const unsigned int POSTS_UID = 1000;
//static const unsigned int PAGES_UID = 1001;
//static const unsigned int EOF_MARKER = 10000;

void WebDataStore::loadFromDisk()
{
	//try
	//{
	//	Lock lock(mutex);

	//	FileInStream file(path);

	//	while(!file.endOfStream())
	//	{
	//		//conPrint(toString(file.file.tellg()));

	//		const uint32 uid = file.readUInt32();
	//		if(uid == POSTS_UID)
	//		{
	//			// Read num posts
	//			const uint32 num = file.readUInt32();

	//			for(size_t i=0; i<num; ++i)
	//			{
	//				posts.push_back(Post());
	//				posts.back().readFromStream(file);
	//			}
	//		}
	//		else if(uid == PAGES_UID)
	//		{
	//			// Read num pages
	//			const uint32 num = file.readUInt32();

	//			for(size_t i=0; i<num; ++i)
	//			{
	//				pages.push_back(Page());
	//				pages.back().readFromStream(file);
	//			}
	//		}
	//		//else if(uid == EOF_MARKER)
	//		//	break;
	//	}

	//	updateAssetManagerAndPageURLMap();
	//}
	//catch(glare::Exception& e)
	//{
	//	throw WebsiteExcep("Failed to load datastore: " + e.what());
	//}
}


void WebDataStore::writeToDisk()
{
	//try
	//{
	//	FileOutStream file(path);

	//	Lock lock(mutex);

	//	//-------------- Write posts --------------
	//	file.writeUInt32(POSTS_UID);

	//	// Write num posts
	//	file.writeUInt32((uint32)posts.size());

	//	for(size_t i=0; i<posts.size(); ++i)
	//		posts[i].writeToStream(file);


	//	//-------------- Write pages --------------
	//	file.writeUInt32(PAGES_UID);

	//	// Write num pages
	//	file.writeUInt32((uint32)pages.size());

	//	for(size_t i=0; i<pages.size(); ++i)
	//		pages[i].writeToStream(file);
	//}
	//catch(glare::Exception& e)
	//{
	//	throw WebsiteExcep("Failed to write datastore to disk: " + e.what());
	//}
}


//void WebDataStore::updateAssetManagerAndPageURLMap()
//{
//	Lock lock(mutex);
//	
//	static_asset_manager.addStaticAsset("/favicon.ico", new StaticAsset(resources_dir + "/favicon.png", "image/png"));
//	static_asset_manager.addStaticAsset("/site.css", new StaticAsset(resources_dir + "/site.css", "text/css")); // TEMP
//	static_asset_manager.addStaticAsset("/squares_crop_600.png", new StaticAsset(resources_dir + "/squares_crop_600.png", "image/png"));
//	static_asset_manager.addStaticAsset("/title-image_600.png", new StaticAsset(resources_dir + "/title-image_600.png", "image/png"));
//
//	//static_asset_manager.addAllFilesInDir("/images/", "images");
//
//
//	// Build page_url_map
//	page_url_map.clear();
//	for(size_t i=0; i<pages.size(); ++i)
//	{
//		if(!pages[i].url_title.empty())
//			page_url_map[pages[i].url_title.str()] = (int)i;
//	}
//}
