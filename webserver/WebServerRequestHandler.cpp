/*=====================================================================
WebServerRequestHandler.cpp
---------------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "WebServerRequestHandler.h"


#include "WebDataStore.h"
#include "AdminHandlers.h"
#include "MainPageHandlers.h"
#include "WebsiteExcep.h"
#include "Escaping.h"
#include "LoginHandlers.h"
#include "ResponseUtils.h"
#include "RequestHandler.h"
#include "PayPalHandlers.h"
#include "AuctionHandlers.h"
#include <StringUtils.h>
#include <Parser.h>
#include <MemMappedFile.h>
#include <ConPrint.h>
#include <FileUtils.h>
#include <Exception.h>


WebServerRequestHandler::WebServerRequestHandler()
{}


WebServerRequestHandler::~WebServerRequestHandler()
{}


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
		if(request.path == "/login_post")
		{
			LoginHandlers::handleLoginPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/logout_post")
		{
			LoginHandlers::handleLogoutPost(request, reply_info);
		}
		else if(request.path == "/signup_post")
		{
			LoginHandlers::handleSignUpPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/ipn_listener")
		{
			PayPalHandlers::handleIPNPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/buy_parcel_now")
		{
			AuctionHandlers::handleParcelBuyNow(*this->world_state, request, reply_info);
		}
		else if(request.path == "/buy_parcel_with_paypal_post")
		{
			AuctionHandlers::handleParcelWithPayPalPost(*this->world_state, request, reply_info);
		}
		else
		{
			const std::string page = "Unknown post URL";
			web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
		}
	}
	else if(request.verb == "GET")
	{
		// Route GET request
		if(request.path == "/")
		{
			MainPageHandlers::renderRootPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/pdt_landing")
		{
			PayPalHandlers::handlePayPalPDTOrderLanding(*this->world_state, request, reply_info);
		}
		else if(request.path == "/parcel_auction_list")
		{
			AuctionHandlers::renderParcelAuctionListPage(*this->world_state, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/parcel_auction/")) // parcel ID follows in URL
		{
			AuctionHandlers::renderParcelAuctionPage(*this->world_state, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/buy_parcel_with_paypal/")) // parcel ID follows in URL
		{
			AuctionHandlers::renderBuyParcelWithPayPalPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin")
		{
			AdminHandlers::renderMainAdminPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/login")
		{
			LoginHandlers::renderLoginPage(request, reply_info);
		}
		else if(request.path == "/signup")
		{
			LoginHandlers::renderSignUpPage(request, reply_info);
		}
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
