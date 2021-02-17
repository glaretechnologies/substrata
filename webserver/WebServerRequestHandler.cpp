/*=====================================================================
WebServerRequestHandler.cpp
---------------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "WebServerRequestHandler.h"


//#include "User.h"
//#include "Post.h"
#include "WebDataStore.h"
#include "MainPageHandlers.h"
#include "WebsiteExcep.h"
#include "Escaping.h"
//#include "PostHandlers.h"
//#include "RSSHandlers.h"
//#include "LoginHandlers.h"
#include "ResponseUtils.h"
#include "RequestHandler.h"
//#include "BlogHandlers.h"
//#include "PageHandlers.h"
#include <StringUtils.h>
#include <Parser.h>
#include <MemMappedFile.h>
#include <ConPrint.h>
#include <FileUtils.h>
#include <Exception.h>


WebServerRequestHandler::WebServerRequestHandler()
{
}


WebServerRequestHandler::~WebServerRequestHandler()
{
}


static bool isLetsEncryptFileQuerySafe(const std::string& s)
{
	for(size_t i=0; i<s.size(); ++i)
		if(!(::isAlphaNumeric(s[i]) || (s[i] == '-') || (s[i] == '_') || (s[i] == '.')))
			return false;
	return true;
}


static bool isFileNameSafe(const std::string& s)
{
	for(size_t i=0; i<s.size(); ++i)
		if(!(::isAlphaNumeric(s[i]) || (s[i] == '-') || (s[i] == '_') || (s[i] == '.')))
			return false;
	return true;
}


void WebServerRequestHandler::handleRequest(const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(request.verb == "POST")
	{
		// Route PUT request
		//if(request.path == "/login_post")
		//{
		//	LoginHandlers::handleLoginPost(*data_store, request, reply_info);
		//}
		//else if(request.path == "/logout_post")
		//{
		//	LoginHandlers::handleLogoutPost(*data_store, request, reply_info);
		//}
		{
			const std::string page = "Unknown post URL";
			web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
		}
	}
	else if(request.verb == "GET")
	{
		// conPrint("path: " + request.path); //TEMP

		// Look up static assets
		//{
		//	//TEMP: NO LOCK as static_asset_manager won't be getting updated Lock lock(data_store->mutex);
		//	const std::map<std::string, StaticAssetRef>::const_iterator res = data_store->static_asset_manager.getStaticAssets().find(request.path);
		//	if(res != data_store->static_asset_manager.getStaticAssets().end())
		//	{
		//		const StaticAsset& asset = *res->second;

		//		// NOTE: this entire string could be precomputed.
		//		ResponseUtils::writeRawString(reply_info,
		//			"HTTP/1.1 200 OK\r\n"
		//			"Content-Type: " + asset.mime_type + "\r\n" +
		//			"Connection: Keep-Alive\r\n"
		//			"Content-Length: " + toString(asset.file->fileSize()) + "\r\n"
		//			"Cache-Control: max-age=3600" + "\r\n"
		//			//"Expires: Thu, 15 Apr 2020 20:00:00 GMT\r\n"
		//			//"ETag: \"" + asset.etag + "\"\r\n"
		//			"\r\n"
		//		);

		//		// Write out the actual file data.
		//		reply_info.socket->writeData(asset.file->fileData(), asset.file->fileSize());
		//	
		//		return;
		//	}
		//}

		// Route GET request
		if(request.path == "/")
		{
			MainPageHandlers::renderRootPage(request, reply_info);
		}
		/*else if(request.path == "/login")
		{
			LoginHandlers::renderLoginPage(*data_store, request, reply_info);
		}*/
		else if(::hasPrefix(request.path, "/files/"))
		{
			const std::string filename = ::eatPrefix(request.path, "/files/");
			if(isFileNameSafe(filename))
			{
				try
				{
					MemMappedFile file(data_store->public_files_dir + "/" + filename);
					std::string content_type;
					if(::hasExtension(filename, "png"))
						content_type = "image/png";
					else if(::hasExtension(filename, "jpg"))
						content_type = "image/jpeg";
					else if(::hasExtension(filename, "pdf"))
						content_type = "application/pdf";
					else
						content_type = "bleh";

					web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, file.fileData(), file.fileSize(), content_type.c_str());
				}
				catch(glare::Exception&)
				{
					web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Failed to load file '" + filename + "'.");
				}
			}
			else
			{
				web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "invalid/unsafe filename");
				return;
			}
		}
		else if(::hasPrefix(request.path, "/.well-known/acme-challenge/")) // Support for Let's encrypt: Serve up challenge response file.
		{
			const std::string filename = ::eatPrefix(request.path, "/.well-known/acme-challenge/");
			if(!isLetsEncryptFileQuerySafe(filename))
			{
				web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "invalid/unsafe file query");
				return;
			}

			// Serve up the file
			try
			{
				std::string contents;
				FileUtils::readEntireFile(data_store->letsencrypt_webroot + "/.well-known/acme-challenge/" + filename, contents);
				web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, contents);
			}
			catch(FileUtils::FileUtilsExcep& e)
			{
				web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "Failed to load file '" + filename + "': " + e.what());
			}
		}
		else
		{
			std::string page = "Unknown page";
			web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
		}
	}
}


void WebServerRequestHandler::handleWebsocketTextMessage(const std::string& msg, Reference<SocketInterface>& socket, const Reference<WorkerThread>& worker_thread)
{}


void WebServerRequestHandler::websocketConnectionClosed(Reference<SocketInterface>& socket, const Reference<WorkerThread>& worker_thread)
{}
