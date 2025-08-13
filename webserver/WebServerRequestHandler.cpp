/*=====================================================================
WebServerRequestHandler.cpp
---------------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "WebServerRequestHandler.h"


#include "WebDataStore.h"
#include "AdminHandlers.h"
#include "MainPageHandlers.h"
#include "NewsPostHandlers.h"
#include "WebsiteExcep.h"
#include "Escaping.h"
#include "LoginHandlers.h"
#include "WorldHandlers.h"
#include "AccountHandlers.h"
#include "ResponseUtils.h"
#include "RequestHandler.h"
#include "ResourceHandlers.h"
#include "PhotoHandlers.h"
#include "SubEventHandlers.h"
#if USE_GLARE_PARCEL_AUCTION_CODE
#include <webserver/PayPalHandlers.h>
#include <webserver/CoinbaseHandlers.h>
#include <webserver/AuctionHandlers.h>
#include <webserver/OrderHandlers.h>
#endif
#include "ScreenshotHandlers.h"
#include "ParcelHandlers.h"
#include "../server/WorkerThread.h"
#include "../server/Server.h"
#include <StringUtils.h>
#include <Parser.h>
#include <MemMappedFile.h>
#include <ConPrint.h>
#include <FileUtils.h>
#include <Exception.h>
#include <Lock.h>
#include <WebSocket.h>


WebServerRequestHandler::WebServerRequestHandler()
:	dev_mode(false)
{}


WebServerRequestHandler::~WebServerRequestHandler()
{}


/*static bool isLetsEncryptFileQuerySafe(const std::string& s)
{
	for(size_t i=0; i<s.size(); ++i)
		if(!(::isAlphaNumeric(s[i]) || (s[i] == '-') || (s[i] == '_') || (s[i] == '.')))
			return false;
	return true;
}*/


/*static bool isFileNameSafe(const std::string& s)
{
	for(size_t i=0; i<s.size(); ++i)
		if(!(::isAlphaNumeric(s[i]) || (s[i] == '-') || (s[i] == '_') || (s[i] == '.')))
			return false;
	return true;
}*/


void WebServerRequestHandler::handleRequest(const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	if(!request.tls_connection)
	{
		// Redirect to https (unless the server is running on localhost, which we will allow to use non-https for testing)

		// Find the hostname the request was sent to - look through the headers for 'host'.
		std::string hostname;
		for(size_t i=0; i<request.headers.size(); ++i)
			if(StringUtils::equalCaseInsensitive(request.headers[i].key, "host"))
				hostname = toString(request.headers[i].value);
		if(hostname != "localhost")
		{
			const std::string response = 
				"HTTP/1.1 301 Redirect\r\n" // 301 = Moved Permanently
				"Location: https://" + hostname + request.path + "\r\n"
				"Content-Length: 0\r\n"
				"\r\n";
			reply_info.socket->writeData(response.c_str(), response.size());
			return;
		}
	}


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
		else if(request.path == "/reset_password_post")
		{
			LoginHandlers::handleResetPasswordPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/change_password_post")
		{
			LoginHandlers::handleChangePasswordPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/set_new_password_post")
		{
			LoginHandlers::handleSetNewPasswordPost(*this->world_state, request, reply_info);
		}
#if USE_GLARE_PARCEL_AUCTION_CODE
		else if(request.path == "/ipn_listener")
		{
			PayPalHandlers::handleIPNPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/coinbase_webhook")
		{
			CoinbaseHandlers::handleCoinbaseWebhookPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/buy_parcel_now_paypal")
		{
			AuctionHandlers::handleParcelBuyNowWithPayPal(*this->world_state, request, reply_info);
		}
		else if(request.path == "/buy_parcel_now_coinbase")
		{
			AuctionHandlers::handleParcelBuyNowWithCoinbase(*this->world_state, request, reply_info);
		}
		else if(request.path == "/buy_parcel_with_paypal_post")
		{
			AuctionHandlers::handleBuyParcelWithPayPalPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/buy_parcel_with_coinbase_post")
		{
			AuctionHandlers::handleBuyParcelWithCoinbasePost(*this->world_state, request, reply_info);
		}
#endif
		else if(request.path == "/admin_create_parcel_auction_post")
		{
			AdminHandlers::createParcelAuctionPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_set_parcel_owner_post")
		{
			AdminHandlers::handleSetParcelOwnerPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_regenerate_parcel_auction_screenshots")
		{
			AdminHandlers::handleRegenerateParcelAuctionScreenshots(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_regenerate_parcel_screenshots")
		{
			AdminHandlers::handleRegenerateParcelScreenshots(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_regenerate_multiple_parcel_screenshots")
		{
			AdminHandlers::handleRegenerateMultipleParcelScreenshots(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_terminate_parcel_auction")
		{
			AdminHandlers::handleTerminateParcelAuction(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_mark_parcel_as_nft_minted_post")
		{
			AdminHandlers::handleMarkParcelAsNFTMintedPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_mark_parcel_as_not_nft_post")
		{
			AdminHandlers::handleMarkParcelAsNotNFTPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_retry_parcel_mint_post")
		{
			AdminHandlers::handleRetryParcelMintPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_set_transaction_state_to_new_post")
		{
			AdminHandlers::handleSetTransactionStateToNewPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_set_transaction_state_to_completed_post")
		{
			AdminHandlers::handleSetTransactionStateToCompletedPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_set_transaction_state_hash")
		{
			AdminHandlers::handleSetTransactionHashPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_set_transaction_nonce")
		{
			AdminHandlers::handleSetTransactionNoncePost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_set_server_admin_message_post")
		{
			AdminHandlers::handleSetServerAdminMessagePost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_set_read_only_mode_post")
		{
			AdminHandlers::handleSetReadOnlyModePost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_set_feature_flag_post")
		{
			AdminHandlers::handleSetFeatureFlagPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_force_dyn_tex_update_post")
		{
			AdminHandlers::handleForceDynTexUpdatePost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_delete_transaction_post")
		{
			AdminHandlers::handleDeleteTransactionPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_regen_map_tiles_post")
		{
			AdminHandlers::handleRegenMapTilesPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_recreate_map_tiles_post")
		{
			AdminHandlers::handleRecreateMapTilesPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_set_min_next_nonce_post")
		{
			AdminHandlers::handleSetMinNextNoncePost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_set_user_as_world_gardener_post")
		{
			AdminHandlers::handleSetUserAsWorldGardenerPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_set_user_allow_dyn_tex_update_post")
		{
			AdminHandlers::handleSetUserAllowDynTexUpdatePost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_new_news_post")
		{
			AdminHandlers::handleNewNewsPostPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_rebuild_world_lod_chunks")
		{
			AdminHandlers::handleRebuildWorldLODChunks(*this->world_state, request, reply_info);
		}
		else if(request.path == "/regenerate_parcel_screenshots")
		{
			ParcelHandlers::handleRegenerateParcelScreenshots(*this->world_state, request, reply_info);
		}
		else if(request.path == "/edit_parcel_description_post")
		{
			ParcelHandlers::handleEditParcelDescriptionPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/add_parcel_writer_post")
		{
			ParcelHandlers::handleAddParcelWriterPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/remove_parcel_writer_post")
		{
			ParcelHandlers::handleRemoveParcelWriterPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/account_eth_sign_message_post")
		{
			AccountHandlers::handleEthSignMessagePost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/make_parcel_into_nft_post")
		{
			AccountHandlers::handleMakeParcelIntoNFTPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/claim_parcel_owner_by_nft_post")
		{
			AccountHandlers::handleClaimParcelOwnerByNFTPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/add_secret_post")
		{
			AccountHandlers::handleAddSecretPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/delete_secret_post")
		{
			AccountHandlers::handleDeleteSecretPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/edit_news_post_post")
		{
			NewsPostHandlers::handleEditNewsPostPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/delete_news_post")
		{
			NewsPostHandlers::handleDeleteNewsPostPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/create_event_post")
		{
			SubEventHandlers::handleCreateEventPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/edit_event_post")
		{
			SubEventHandlers::handleEditEventPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/delete_event_post")
		{
			SubEventHandlers::handleDeleteEventPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/create_world_post")
		{
			WorldHandlers::handleCreateWorldPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/edit_world_post")
		{
			WorldHandlers::handleEditWorldPost(*this->world_state, request, reply_info);
		}
		else if(request.path == "/delete_photo_post")
		{
			PhotoHandlers::handleDeletePhotoPost(*this->world_state, request, reply_info);
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
			MainPageHandlers::renderRootPage(*this->world_state, *this->data_store, request, reply_info);
		}
		else if(request.path == "/terms")
		{
			MainPageHandlers::renderTermsOfUse(*this->world_state, request, reply_info);
		}
		else if(request.path == "/about_parcel_sales")
		{
			MainPageHandlers::renderAboutParcelSales(*this->world_state, request, reply_info);
		}
		else if(request.path == "/about_scripting")
		{
			MainPageHandlers::renderAboutScripting(*this->world_state, *this->data_store, request, reply_info);
		}
		else if(request.path == "/about_substrata")
		{
			MainPageHandlers::renderAboutSubstrataPage(*this->world_state, *this->data_store, request, reply_info);
		}
		else if(request.path == "/running_your_own_server")
		{
			MainPageHandlers::renderRunningYourOwnServerPage(*this->world_state, *this->data_store, request, reply_info);
		}
		else if(request.path == "/bot_status")
		{
			MainPageHandlers::renderBotStatusPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/faq")
		{
			MainPageHandlers::renderFAQ(*this->world_state, request, reply_info);
		}
		else if(request.path == "/map")
		{
			MainPageHandlers::renderMapPage(*this->world_state, request, reply_info);
		}
#if USE_GLARE_PARCEL_AUCTION_CODE
		else if(request.path == "/pdt_landing")
		{
			PayPalHandlers::handlePayPalPDTOrderLanding(*this->world_state, request, reply_info);
		}
		else if(request.path == "/parcel_auction_list")
		{
			AuctionHandlers::renderParcelAuctionListPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/recent_parcel_sales")
		{
			AuctionHandlers::renderRecentParcelSalesPage(*this->world_state, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/parcel_auction/")) // parcel auction ID follows in URL
		{
			AuctionHandlers::renderParcelAuctionPage(*this->world_state, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/buy_parcel_with_paypal/")) // parcel ID follows in URL
		{
			AuctionHandlers::renderBuyParcelWithPayPalPage(*this->world_state, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/buy_parcel_with_coinbase/")) // parcel ID follows in URL
		{
			AuctionHandlers::renderBuyParcelWithCoinbasePage(*this->world_state, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/order/")) // Order ID follows in URL
		{
			OrderHandlers::renderOrderPage(*this->world_state, request, reply_info);
		}
#endif
		else if(::hasPrefix(request.path, "/parcel/")) // Parcel ID follows in URL
		{
			ParcelHandlers::renderParcelPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/edit_parcel_description")
		{
			ParcelHandlers::renderEditParcelDescriptionPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/add_parcel_writer")
		{
			ParcelHandlers::renderAddParcelWriterPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/remove_parcel_writer")
		{
			ParcelHandlers::renderRemoveParcelWriterPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin")
		{
			AdminHandlers::renderMainAdminPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_users")
		{
			AdminHandlers::renderUsersPage(*this->world_state, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/admin_user/")) // user ID follows in URL
		{
			AdminHandlers::renderAdminUserPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_parcels")
		{
			AdminHandlers::renderParcelsPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_parcel_auctions")
		{
			AdminHandlers::renderParcelAuctionsPage(*this->world_state, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/admin_parcel_auction/"))
		{
			AdminHandlers::renderAdminParcelAuctionPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_orders")
		{
			AdminHandlers::renderOrdersPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_sub_eth_transactions")
		{
			AdminHandlers::renderSubEthTransactionsPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_news_posts")
		{
			AdminHandlers::renderAdminNewsPostsPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_lod_chunks")
		{
			AdminHandlers::renderAdminLODChunksPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_worlds")
		{
			AdminHandlers::renderAdminWorldsPage(*this->world_state, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/admin_sub_eth_transaction/"))
		{
			AdminHandlers::renderAdminSubEthTransactionPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/admin_map")
		{
			AdminHandlers::renderMapPage(*this->world_state, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/admin_create_parcel_auction/")) // parcel ID follows in URL
		{
			AdminHandlers::renderCreateParcelAuction(*this->world_state, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/admin_set_parcel_owner/")) // parcel ID follows in URL
		{
			AdminHandlers::renderSetParcelOwnerPage(*this->world_state, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/admin_order/")) // order ID follows in URL
		{
			AdminHandlers::renderAdminOrderPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/login")
		{
			LoginHandlers::renderLoginPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/signup")
		{
			LoginHandlers::renderSignUpPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/reset_password")
		{
			LoginHandlers::renderResetPasswordPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/reset_password_email")
		{
			LoginHandlers::renderResetPasswordFromEmailPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/change_password")
		{
			LoginHandlers::renderChangePasswordPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/account")
		{
			AccountHandlers::renderUserAccountPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/prove_eth_address_owner")
		{
			AccountHandlers::renderProveEthAddressOwnerPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/prove_parcel_owner_by_nft")
		{
			AccountHandlers::renderProveParcelOwnerByNFT(*this->world_state, request, reply_info);
		}
		else if(request.path == "/make_parcel_into_nft")
		{
			AccountHandlers::renderMakeParcelIntoNFTPage(*this->world_state, request, reply_info);
		}
		else if(request.path == "/parcel_claim_succeeded")
		{
			AccountHandlers::renderParcelClaimSucceeded(*this->world_state, request, reply_info);
		}
		else if(request.path == "/parcel_claim_failed")
		{
			AccountHandlers::renderParcelClaimFailed(*this->world_state, request, reply_info);
		}
		else if(request.path == "/parcel_claim_invalid")
		{
			AccountHandlers::renderParcelClaimInvalid(*this->world_state, request, reply_info);
		}
		else if(request.path == "/making_parcel_into_nft")
		{
			AccountHandlers::renderMakingParcelIntoNFT(*this->world_state, request, reply_info);
		}
		else if(request.path == "/making_parcel_into_nft_failed")
		{
			AccountHandlers::renderMakingParcelIntoNFTFailed(*this->world_state, request, reply_info);
		}
		else if(request.path == "/script_log")
		{
			AccountHandlers::renderScriptLog(*this->world_state, request, reply_info);
		}
		else if(request.path == "/secrets")
		{
			AccountHandlers::renderSecretsPage(*this->world_state, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/p/")) // URL for parcel ERC 721 metadata JSON
		{
			ParcelHandlers::renderMetadata(*world_state, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/screenshot/")) // Screenshot ID follows
		{
			ScreenshotHandlers::handleScreenshotRequest(*world_state, *data_store, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/photo/")) // Photo ID follows
		{
			PhotoHandlers::handlePhotoPageRequest(*world_state, *data_store, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/photo_image/")) // Photo ID follows
		{
			PhotoHandlers::handlePhotoImageRequest(*world_state, *data_store, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/photo_midsize_image/")) // Photo ID follows
		{
			PhotoHandlers::handlePhotoMidSizeImageRequest(*world_state, *data_store, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/photo_thumb_image/")) // Photo ID follows
		{
			PhotoHandlers::handlePhotoThumbnailImageRequest(*world_state, *data_store, request, reply_info);
		}
		else if(request.path == "/tile")
		{
			ScreenshotHandlers::handleMapTileRequest(*world_state, *data_store, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/news_post/")) // News post ID follows
		{
			NewsPostHandlers::renderNewsPostPage(*world_state, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/edit_news_post"))
		{
			NewsPostHandlers::renderEditNewsPostPage(*world_state, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/news"))
		{
			NewsPostHandlers::renderAllNewsPage(*world_state, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/event/")) // Event ID follows
		{
			SubEventHandlers::renderEventPage(*world_state, request, reply_info);
		}
		else if(request.path == "/events")
		{
			SubEventHandlers::renderAllEventsPage(*world_state, request, reply_info);
		}
		else if(request.path == "/create_event")
		{
			SubEventHandlers::renderCreateEventPage(*world_state, request, reply_info);
		}
		else if(request.path == "/edit_event")
		{
			SubEventHandlers::renderEditEventPage(*world_state, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/world/")) // world name follows
		{
			WorldHandlers::renderWorldPage(*world_state, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/edit_world/")) // world name follows
		{
			WorldHandlers::renderEditWorldPage(*world_state, request, reply_info);
		}
		else if(request.path == "/create_world")
		{
			WorldHandlers::renderCreateWorldPage(*world_state, request, reply_info);
		}
		else if(::hasPrefix(request.path, "/files/"))
		{
			// Only serve files that are in the precomputed filename map.
			// One reason for this is to avoid directory traversal issues, where "../" or absolute paths are used to traverse out of the public_files_dir.
			const std::string filename = ::eatPrefix(request.path, "/files/");

			Reference<WebDataStoreFile> store_file;
			{
				Lock lock(data_store->mutex);
				const auto lookup_res = data_store->public_files.find(filename);
				if(lookup_res != data_store->public_files.end())
					store_file = lookup_res->second;
			}

			if(store_file.nonNull())
			{
				const std::string& content_type = store_file->content_type;
				const int max_age_s = 3600*24*14;
				if(request.zstd_accept_encoding && !store_file->zstd_compressed_data.empty())
				{
					web::ResponseUtils::writeHTTPOKHeaderWithCacheMaxAgeAndContentEncoding(reply_info, store_file->zstd_compressed_data.data(), store_file->zstd_compressed_data.size(), 
						content_type, "zstd", max_age_s);
				}
				else if(request.deflate_accept_encoding && !store_file->deflate_compressed_data.empty())
				{
					web::ResponseUtils::writeHTTPOKHeaderWithCacheMaxAgeAndContentEncoding(reply_info, store_file->deflate_compressed_data.data(), store_file->deflate_compressed_data.size(), 
						content_type, "deflate", max_age_s);
				}
				else
				{
					web::ResponseUtils::writeHTTPOKHeaderAndDataWithCacheMaxAge(reply_info, store_file->uncompressed_data.data(), store_file->uncompressed_data.size(), content_type, max_age_s);
				}
			}
			else
			{
				web::ResponseUtils::writeHTTPNotFoundHeaderAndData(reply_info, "No such file found or invalid filename");
				return;
			}
		}
		/* // Let's encrypt challenge response handling is disabled for now, as using godaddy certs
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
		}*/
		else if(::hasPrefix(request.path, "/resource/"))
		{
			ResourceHandlers::handleResourceRequest(*this->world_state, request, reply_info);
		}
		/*else if(request.path ==  "/list_resources") // Disabled for now, rsync resources to back up instead.
		{
			ResourceHandlers::listResources(*this->world_state, request, reply_info);
		}*/
		else if(dev_mode && ::hasPrefix(request.path, "/webclient/"))
		{
			try
			{
				// In development mode, serve any requested files directly from disk, out of the webclient dir.
				const std::string path_relative_to_webclient_dir = ::eatPrefix(request.path, "/webclient/");
				if(!FileUtils::isPathSafe(path_relative_to_webclient_dir))
					throw glare::Exception("request '" + request.path + "' is not safe.");

				try
				{
					std::string contents;
					FileUtils::readEntireFile(data_store->webclient_dir + "/" + path_relative_to_webclient_dir, contents);
					const std::string content_type = web::ResponseUtils::getContentTypeForPath(path_relative_to_webclient_dir);
					web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, contents.data(), contents.length(), content_type);
				}
				catch(FileUtils::FileUtilsExcep& e)
				{
					conPrint("Failed to load file: " + e.what());
					web::ResponseUtils::writeHTTPNotFoundHeaderAndData(reply_info, "Failed to load file");
				}
			}
			catch(glare::Exception& e)
			{
				// Since we're in dev mode, print out a nice error message to stdout.
				conPrint("Error while handling request with path '" + request.path + "': " + e.what());
				throw e; // rethrow
			}
				
		}
		else if(::hasPrefix(request.path, "/webclient/") || request.path == "/webclient" || request.path == "/gui_client.data")
		{
			std::string path;
			bool cache = true;
			if(request.path == "/webclient")
			{
				path = "webclient.html";
				cache = false; // /webclient html needs to be uncached, as it may change, especially with new cache-busting URLs for updated files like gui_client.wasm.
			}
			else if(request.path == "/gui_client.data") // gui_client.js fetches gui_client.data from this URL path.
				path = "gui_client.data";
			else
				path = ::eatPrefix(request.path, "/webclient/");

			Reference<WebDataStoreFile> store_file;
			{
				Lock lock(data_store->mutex);
				const auto lookup_res = data_store->webclient_dir_files.find(path);
				if(lookup_res != data_store->webclient_dir_files.end())
					store_file = lookup_res->second;
			}

			if(store_file.nonNull())
			{
				const std::string& content_type = store_file->content_type;

				// We are using cache-busting hashes in webclient.html, so can set a very long max age and use 'immutable' when caching.
				const char* cache_control_val = cache ? "max-age=1000000000, immutable" : "max-age=0"; 
				
				if(request.zstd_accept_encoding && !store_file->zstd_compressed_data.empty())
				{
					web::ResponseUtils::writeHTTPOKHeaderWithCacheControlAndContentEncoding(reply_info, store_file->zstd_compressed_data.data(), store_file->zstd_compressed_data.size(), content_type, cache_control_val, "zstd");
				}
				else if(request.deflate_accept_encoding && !store_file->deflate_compressed_data.empty())
				{
					web::ResponseUtils::writeHTTPOKHeaderWithCacheControlAndContentEncoding(reply_info, store_file->deflate_compressed_data.data(), store_file->deflate_compressed_data.size(), content_type, cache_control_val, "deflate");
				}
				else
				{
					web::ResponseUtils::writeHTTPOKHeaderAndDataWithCacheControl(reply_info, store_file->uncompressed_data.data(), store_file->uncompressed_data.size(), content_type, cache_control_val);
				}
			}
			else
			{
				web::ResponseUtils::writeHTTPNotFoundHeaderAndData(reply_info, "No such file found or invalid filename");
				return;
			}
		}
		else
		{
			// Look up in generic pages
			Reference<GenericPage> generic_page;
			{
				Lock lock(data_store->mutex);
				const auto lookup_res = data_store->generic_pages.find(request.path);
				if(lookup_res != data_store->generic_pages.end())
					generic_page = lookup_res->second;
			}

			if(generic_page.nonNull())
			{
				MainPageHandlers::renderGenericPage(*world_state, *data_store, *generic_page, request, reply_info);
			}
			else
			{
				// If there is no generic page found, return a 404 reponse.

				std::string page = "Unknown page";
				web::ResponseUtils::writeHTTPNotFoundHeaderAndData(reply_info, page);
			}
		}
	}
}


void WebServerRequestHandler::handleWebSocketConnection(const web::RequestInfo& request_info, Reference<SocketInterface>& socket)
{
	// Wrap socket in a websocket
	WebSocketRef websocket = new WebSocket(socket);

	// Handle the connection in a worker thread.
	Reference<WorkerThread> worker_thread = new WorkerThread(websocket, server, /*is_websocket_connection=*/true);

	worker_thread->websocket_request_info = request_info;

	try
	{
		server->worker_thread_manager.addThread(worker_thread);
	}
	catch(glare::Exception& e)
	{
		// Will get this when thread creation fails.
		conPrint("ListenerThread failed to launch worker thread: " + e.what());
	}
}
