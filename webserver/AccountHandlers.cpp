/*=====================================================================
AccountHandlers.cpp
-------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "AccountHandlers.h"


#include <ConPrint.h>
#include "RequestInfo.h"
#include <AESEncryption.h>
#include <Exception.h>
#include <MySocket.h>
#include <Lock.h>
#include <Clock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <KillThreadMessage.h>
#include <Parser.h>
#include <SHA256.h>
#include <Base64.h>
#include <Keccak256.h>
#include <CryptoRNG.h>
#include <MemMappedFile.h>
#include "RequestInfo.h"
#include "Response.h"
#include "WebsiteExcep.h"
#include "Escaping.h"
#include "ResponseUtils.h"
#include "WebServerResponseUtils.h"
#include "LoginHandlers.h"
#include "../server/ServerWorldState.h"
#include "../server/UserWebSession.h"
#include "../server/SubEthTransaction.h"
#include "../ethereum/Signing.h"
#include "../ethereum/Infura.h"


namespace AccountHandlers
{


void renderUserAccountPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	std::string page;

	{ // lock scope
		Lock lock(world_state.mutex);

		const User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
		if(logged_in_user == NULL)
		{
			page += WebServerResponseUtils::standardHTMLHeader(*world_state.web_data_store, request, "User Account");
			page += "You must be logged in to view your user account page.";
			page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);
			web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
			return;
		}

		page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/logged_in_user->name);
		page += "<div class=\"main\">   \n";


		// Display any messages for the user
		const std::string msg_for_user = world_state.getAndRemoveUserWebMessage(logged_in_user->id);
		if(!msg_for_user.empty())
			page += "<div class=\"msg\">" + web::Escaping::HTMLEscape(msg_for_user) + "</div>  \n";


		//-------------------------------- List parcels owned by user --------------------------------

		page += "<h2>Parcels</h2>\n";

		Reference<ServerWorldState> root_world = world_state.getRootWorldState();

		for(auto it = root_world->parcels.begin(); it != root_world->parcels.end(); ++it)
		{
			const Parcel* parcel = it->second.ptr();

			// Look up owner
			if(parcel->owner_id == logged_in_user->id)
			{
				page += "<p>\n";
				page += "<a href=\"/parcel/" + parcel->id.toString() + "\">Parcel " + parcel->id.toString() + "</a><br/>" +
					"description: " + web::Escaping::HTMLEscape(parcel->description);// +"<br/>" +
					//"created " + parcel->created_time.timeAgoDescription();
				if(parcel->nft_status == Parcel::NFTStatus_NotNFT)
					page += "<br/><a href=\"/make_parcel_into_nft?parcel_id=" + parcel->id.toString() + "\">Mint as a NFT</a>";
				page += "</p>\n";
				//page += "<br/>  \n";
			}
		}

		//-------------------------------- List events created by user --------------------------------
		page += "<h2>Events</h2>\n";
		for(auto it = world_state.events.begin(); it != world_state.events.end(); ++it)
		{
			const SubEvent* event = it->second.ptr();
			if(event->creator_id == logged_in_user->id)
			{
				page += "<div><a href=\"/event/" + toString(event->id) + "\">" + web::Escaping::HTMLEscape(event->title) + "</a></div>";
			}
		}


		page += "<h2>Ethereum</h2>\n";

		page += "Linked Ethereum address: ";

		if(logged_in_user->controlled_eth_address.empty())
			page += "No address linked.";
		else
		{
			page += "<span class=\"eth-address\">" + web::Escaping::HTMLEscape(logged_in_user->controlled_eth_address) + "</span>";
			page += "<br/>";
			page += "This is an address that you control, and for which control of the address has been proven to the Substrata server.";
		}


		page += "<br/>";
		page += "<br/>";
		page += "<a href=\"/prove_eth_address_owner\">Link an Ethereum address and prove you own it by signing a message</a>";
		
		page += "<br/>";
		page += "<br/>";
		page += "<a href=\"/prove_parcel_owner_by_nft\">Claim ownership of a parcel on substrata.info based on NFT ownership</a>";


		page += "<h2>Account</h2>\n";
		page += "<a href=\"/change_password\">Change password</a>";


		page += "<h2>Developer</h2>\n";
		page += "<p><a href=\"/script_log\">Show script log</a></p>";

		page += "<p><a href=\"/secrets\">Show secrets (for Lua scripting)</a></p>";
	}

	page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderProveEthAddressOwnerPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	std::string page;

	{ // lock scope
		Lock lock(world_state.mutex);

		User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
		if(logged_in_user == NULL)
		{
			page += WebServerResponseUtils::standardHTMLHeader(*world_state.web_data_store, request, "User Account");
			page += "You must be logged in to view your user account page.";
			page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);
			web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
			return;
		}

		// Generate a new random current_eth_signing_nonce
		try
		{
			const int NUM_BYTES = 16;
			uint8 data[NUM_BYTES];

			CryptoRNG::getRandomBytes(data, NUM_BYTES); // throws glare::Exception on failure

			logged_in_user->current_eth_signing_nonce = StringUtils::convertByteArrayToHexString(data, NUM_BYTES);
		}
		catch(glare::Exception& )
		{
			return;
		}

		page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/logged_in_user->name);
		page += "<div class=\"main\">   \n";
	
		

		page += "<br/>";
		page += "Step 1: connect to Ethereum/MetaMask";
		page += "<div><button class=\"enableEthereumButton\">Connect to Ethereum/MetaMask</button></div>";

		page += "<br/>";
		page += "<div>Ethereum/MetaMask connection status: <div class=\"metamask-status-div\"></div></div>";

		page += "<br/>";
		page += "<br/>";
		page += "Step 2: Prove you own the Ethereum address by signing a message";
		page += "<div><button class=\"signEthereumButton\">Sign a message</button></div>";

		page += "</div>   \n"; // End main div

		page += "<div id=\"current_eth_signing_nonce\" class=\"hidden\">" + logged_in_user->current_eth_signing_nonce + "</div>\n";

		page += "<script src=\"/files/account.js\"></script>";
	}

	page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderProveParcelOwnerByNFT(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	std::string page;

	{ // lock scope
		Lock lock(world_state.mutex);

		User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
		if(logged_in_user == NULL)
		{
			page += WebServerResponseUtils::standardHTMLHeader(*world_state.web_data_store, request, "User Account");
			page += "You must be logged in to view this page.";
			page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);
			web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
			return;
		}

		page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Prove parcel ownership via NFT ownership");
		page += "<div class=\"main\">   \n";

		if(logged_in_user->controlled_eth_address.empty())
		{
			page += "You must link an ethereum address to your account first.  You can do that <a href=\"/prove_eth_address_owner\">here</a>.";
		}
		else
		{
			page += "<p>If your Ethereum account is the owner of a Substrata Parcel NFT, you can claim ownership of the parcel on the substrata server on this page.</p>";

			page += "<p>Your linked Ethereum address: <span class=\"eth-address\">" +
				logged_in_user->controlled_eth_address + "</span> (The parcel NFT must be owned by this address)</p>";

			page +=
				"	<form action=\"/claim_parcel_owner_by_nft_post\" method=\"post\">																\n"
				"		<label for=\"parcel-id\">Parcel number:</label><br>																					 \n"
				"		<input id=\"parcel-id\" type=\"number\" name=\"parcel_id\" />			\n"
				"		<button type=\"submit\" id=\"claim-parcel-button\" class=\"button-link\">Claim Parcel</button>			\n"
				"	</form>";

			page += "<p>Please don't spam this button, it does an Ethereum query!  It may take a few seconds to return a response.</p>";
		}

		page += "</div>   \n"; // End main div
	}

	page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void handleEthSignMessagePost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	 const web::UnsafeString address = request_info.getURLParam("address");
	 const web::UnsafeString sig = request_info.getURLParam("sig");

	 { // lock scope
		 Lock lock(world_state.mutex);

		 User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request_info);
		 if(logged_in_user == NULL)
		 {
			// page += WebServerResponseUtils::standardHTMLHeader(request, "User Account");
			// page += "You must be logged in to view your user account page.";
			 web::ResponseUtils::writeRedirectTo(reply_info, "/account");
			 return;
		 }

		 if(logged_in_user->current_eth_signing_nonce == "") // current_eth_signing_nonce must be non-empty.  This should be the case if submitted in the usual way.
			 return;

		 const std::string message = "Please sign this message to confirm you own the Ethereum account.\n(Unique string: " + logged_in_user->current_eth_signing_nonce + ")";
		 
		 try
		 {
			 const EthAddress recovered_address = Signing::ecrecover(sig.str(), message);

			 if(recovered_address.toHexStringWith0xPrefix() == address.str())
			 {
				 // The user has proved that they control the account with the given address.

				 logged_in_user->controlled_eth_address = recovered_address.toHexStringWith0xPrefix();
				 world_state.addUserAsDBDirty(logged_in_user);

				 web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "{\"msg\":\"Congrats, you have sucessfully proven you control the Ethereum address " + recovered_address.toHexStringWith0xPrefix() + 
					 ". You will now be redirected to your account page.\", \"redirect_URL\":\"/account\"}");
			 }
			 else
			 {
				 web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "{\"msg\":\"Sorry, we could not confirm you control the Ethereum address.\"}");
			 }
		 }
		 catch(glare::Exception& e)
		 {
			 if(!request_info.fuzzing)
				conPrint("Excep while calling ecrecover(): " + e.what());
			 web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "{\"msg\":\"Sorry, we could not confirm you control the Ethereum address.\"}");
		 }
	 }
}



void renderMakeParcelIntoNFTPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	std::string page;

	const ParcelID parcel_id(request.getURLIntParam("parcel_id"));

	{ // lock scope
		Lock lock(world_state.mutex);

		User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
		if(logged_in_user == NULL)
		{
			page += WebServerResponseUtils::standardHTMLHeader(*world_state.web_data_store, request, "User Account");
			page += "You must be logged in to view this page.";
			page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);
			web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
			return;
		}

		// Lookup parcel
		auto res = world_state.getRootWorldState()->parcels.find(parcel_id);
		if(res == world_state.getRootWorldState()->parcels.end())
			throw glare::Exception("No such parcel");
		
		const Parcel* parcel = res->second.ptr();

		if(parcel->owner_id != logged_in_user->id)
			throw glare::Exception("Parcel must be owned by user");

		page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Convert parcel " + parcel_id.toString() + " to a NFT");
		page += "<div class=\"main\">   \n";

		if(parcel->nft_status == Parcel::NFTStatus_NotNFT)
		{
			if(logged_in_user->controlled_eth_address.empty())
			{
				page += "You must link an ethereum address to your account first.  You can do that <a href=\"/prove_eth_address_owner\">here</a>.";
			}
			else
			{
				page += "<p>Are you sure you want to make this parcel an ERC721 NFT on the Ethereum blockchain?  This cannot currently be reversed.</p>";

				page += "<p>If you make this parcel an NFT, then the Substrata server will consider the owner of the parcel NFT to be the owner "
					" of the parcel.</p>";

				page += "<p>Ownership of the NFT will be assigned to your Ethereum address: <span class=\"eth-address\">" +
					logged_in_user->controlled_eth_address + "</span></p>";

				page +=
					"	<form action=\"/make_parcel_into_nft_post\" method=\"post\">																\n"
					"		<input type=\"hidden\" name=\"parcel_id\" value=\"" + parcel_id.toString() + "\"  />			\n"
					"		<button type=\"submit\" id=\"button-make-parcel-nft\" class=\"button-link\">Make parcel into NFT</button>			\n"
					"	</form>";
			}
		}
		else
		{
			page += "Parcel is already a NFT, or is currently being minted as a NFT.";
		}

		page += "</div>   \n"; // End main div
	}

	page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderParcelClaimSucceeded(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	std::string page;
	page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Successfully claimed ownership of parcel");

	page += "<div class=\"main\">   \n";
	// page += "<p>TEMP NO OWNERSHIP CHANGE DURING TESTING.  NO OWNERSHIP CHANGE WILL ACTUALLY TAKE PLACE.</p>";
	page += "<p>You have successfully claimed ownership of a parcel.  The parcel will now be listed on your <a href=\"/account\">account page</a>.</p>";
	page += "</div>   \n"; // End main div

	page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderParcelClaimFailed(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	std::string page;
	page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Ownership claim of parcel failed.");

	page += "<div class=\"main\">   \n";
	page += "<p>Sorry, the ownership claim for the parcel failed.</p>";

	page += "<p>This is either because the Ethereum address associated with your Substrata account is not the owner of the parcel NFT, the parcel was not minted as an NFT, or because there was an internal error in the query of the Ethereum block chain.</p>";
	
	page += "<p>Please visit the Discord channel (see homepage) or email contact@glaretechnologies.com for more info.</p>";
	
	page += "<p>You can try again via your <a href=\"/account\">account page</a>.</p>";
	page += "</div>   \n"; // End main div

	page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderParcelClaimInvalid(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	std::string page;
	page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Ownership claim of parcel was invalid");

	page += "<div class=\"main\">   \n";
	page += "<p>Sorry, the ownership claim for the parcel was not valid.</p>";

	page += "<p>This is either because you do not have an Ethereum address associated with your Substrata account, or you already own the parcel, or the parcel does not exist.</p>";
	
	page += "<p>Please visit the Discord channel (see homepage) or email contact@glaretechnologies.com for more info.</p>";
	
	page += "</div>   \n"; // End main div

	page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderMakingParcelIntoNFT(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	const int parcel_id = request.getURLIntParam("parcel_id");

	std::string page;
	page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Making parcel NFT");

	page += "<div class=\"main\">   \n";
	page += "<p>The transaction to mint your parcel as an NFT is queued.</p>";

	page += "<p>This may take a while to complete (Up to 24 hours).</p>";

	page += "<p>Once the transaction is complete, the minting transaction will be shown on the <a href=\"/parcel/" + toString(parcel_id) + "\">parcel page</a>.</p>";

	page += "<p>If the transaction does not complete in that time, please visit the Discord channel (see homepage) or email contact@glaretechnologies.com for more info.</p>";

	page += "</div>   \n"; // End main div

	page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderMakingParcelIntoNFTFailed(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	std::string page;
	page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Making parcel NFT failed.");

	page += "<div class=\"main\">   \n";
	page += "<p>Sorry, we failed to make the parcel into an NFT.</p>";

	page += "<p>Please visit the Discord channel (see homepage) or email contact@glaretechnologies.com for more info.</p>";

	page += "<p>You can try again via your <a href=\"/account\">account page</a>.</p>";
	page += "</div>   \n"; // End main div

	page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void handleMakeParcelIntoNFTPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	try
	{ // lock scope

		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const ParcelID parcel_id(request_info.getPostIntField("parcel_id"));

		WorldStateLock lock(world_state.mutex);

		User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request_info);
		if(logged_in_user == NULL)
		{
			// page += WebServerResponseUtils::standardHTMLHeader(request, "User Account");
			// page += "You must be logged in to view your user account page.";
			web::ResponseUtils::writeRedirectTo(reply_info, "/account");
			return;
		}

		if(logged_in_user->controlled_eth_address.empty())
			throw glare::Exception("controlled eth address must be valid.");

		// Lookup parcel
		auto res = world_state.getRootWorldState()->parcels.find(parcel_id);
		if(res == world_state.getRootWorldState()->parcels.end())
			throw glare::Exception("No such parcel");

		Parcel* parcel = res->second.ptr();

		if(parcel->owner_id != logged_in_user->id)
			throw glare::Exception("Parcel must be owned by user");

		if(parcel->nft_status != Parcel::NFTStatus_NotNFT)
			throw glare::Exception("Parcel must not already be a NFT");


		// Transition the parcel into 'minting' state
		parcel->nft_status = Parcel::NFTStatus_MintingNFT;


		// Make an Eth transaction to mint the parcel
		SubEthTransactionRef transaction = new SubEthTransaction();
		transaction->id = world_state.getNextSubEthTransactionUID();
		transaction->created_time = TimeStamp::currentTime();
		transaction->state = SubEthTransaction::State_New;
		transaction->initiating_user_id = logged_in_user->id;
		transaction->parcel_id = parcel->id;
		transaction->user_eth_address = logged_in_user->controlled_eth_address;
		world_state.addSubEthTransactionAsDBDirty(transaction);

		parcel->minting_transaction_id = transaction->id;
		world_state.getRootWorldState()->addParcelAsDBDirty(parcel, lock);

		world_state.sub_eth_transactions[transaction->id] = transaction;

		world_state.markAsChanged();
	
	} // End lock scope
	catch(web::WebsiteExcep&)
	{
		web::ResponseUtils::writeRedirectTo(reply_info, "/making_parcel_into_nft_failed");
		return;
	}
	catch(glare::Exception&)
	{
		web::ResponseUtils::writeRedirectTo(reply_info, "/making_parcel_into_nft_failed");
		return;
	}

	web::ResponseUtils::writeRedirectTo(reply_info, "/making_parcel_into_nft?parcel_id=" + toString(request_info.getPostIntField("parcel_id")));
	return;
}


void handleClaimParcelOwnerByNFTPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	ParcelID parcel_id;
	std::string user_controlled_eth_address;

	// Check user is logged in, check they have an eth address, check parcel exists.
	try
	{ // lock scope

		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		parcel_id = ParcelID(request_info.getPostIntField("parcel_id"));

		Lock lock(world_state.mutex);

		User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request_info);
		if(logged_in_user == NULL)
			throw glare::Exception("You must be logged in.");

		if(logged_in_user->controlled_eth_address.empty())
			throw glare::Exception("controlled eth address must be valid.");

		user_controlled_eth_address = logged_in_user->controlled_eth_address;

		// Lookup parcel
		auto res = world_state.getRootWorldState()->parcels.find(parcel_id);
		if(res == world_state.getRootWorldState()->parcels.end())
			throw glare::Exception("No such parcel");

		Parcel* parcel = res->second.ptr();

		if(parcel->owner_id == logged_in_user->id)
			throw glare::Exception("parcel already owned by user.");

	} // End lock scope
	catch(web::WebsiteExcep&)
	{
		web::ResponseUtils::writeRedirectTo(reply_info, "/parcel_claim_invalid");
		return;
	}
	catch(glare::Exception&)
	{
		web::ResponseUtils::writeRedirectTo(reply_info, "/parcel_claim_invalid");
		return;
	}


	// Do infura lookup
	bool succeeded = false;
	try
	{
		InfuraCredentials infura_credentials;
		infura_credentials.infura_project_id		= world_state.getCredential("infura_project_id");
		infura_credentials.infura_project_secret	= world_state.getCredential("infura_project_secret");

		const std::string network = "mainnet";
		const EthAddress substrata_smart_contact_addr = EthAddress::parseFromHexString("0xa4535F84e8D746462F9774319E75B25Bc151ba1D"); // This should be address of the Substrata parcel smart contract

		const EthAddress eth_parcel_owner = Infura::getOwnerOfERC721Token(infura_credentials, network, substrata_smart_contact_addr, UInt256(parcel_id.value()));

		if(eth_parcel_owner == EthAddress::parseFromHexString(user_controlled_eth_address))
		{
			// The logged in user does indeed own the parcel NFT.  So assign ownership of the parcel.

			{ // lock scope
				WorldStateLock lock(world_state.mutex);

				User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request_info);
				if(logged_in_user == NULL)
					throw glare::Exception("logged_in_user == NULL.");

				// Lookup parcel
				auto res = world_state.getRootWorldState()->parcels.find(parcel_id);
				if(res == world_state.getRootWorldState()->parcels.end())
					throw glare::Exception("No such parcel");

				Parcel* parcel = res->second.ptr();

				parcel->owner_id = logged_in_user->id;

				// Set parcel admins and writers to the new user as well.
				parcel->admin_ids  = std::vector<UserID>(1, UserID(logged_in_user->id));
				parcel->writer_ids = std::vector<UserID>(1, UserID(logged_in_user->id));
				
				world_state.getRootWorldState()->addParcelAsDBDirty(parcel, lock);

				// TODO: Log ownership change?

				world_state.denormaliseData();
				world_state.markAsChanged();

				succeeded = true;

			} // End lock scope
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("Error while executing Infura::getOwnerOfERC721Token(): " + e.what());
	}

	if(succeeded)
		web::ResponseUtils::writeRedirectTo(reply_info, "/parcel_claim_succeeded");
	else
		web::ResponseUtils::writeRedirectTo(reply_info, "/parcel_claim_failed");
}


void renderScriptLog(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	std::string page;

	// Get a reference to the script log for this user.
	Reference<UserScriptLog> log;

	{ // lock scope
		Lock lock(world_state.mutex);

		User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
		if(logged_in_user == NULL)
		{
			page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Script log");
			page += "You must be logged in to view this page.";
			page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);
			web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
			return;
		}

		page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Script log");
		page += "<div class=\"main\">   \n";

		page += "<p>Showing output and error messages for all scripts created by <a href=\"/account\">" + web::Escaping::HTMLEscape(logged_in_user->name) + "</a>.</p>";

		auto res = world_state.user_script_log.find(logged_in_user->id);
		if(res != world_state.user_script_log.end())
			log = res->second;
	}

	

	// Do the conversion of the logs into the HTML without holding the main world_state mutex, since this could take a little while.
	if(log.nonNull())
	{
		Lock lock(log->mutex);

		page += "<pre class=\"script-log\">";

		for(auto it = log->messages.beginIt(); it != log->messages.endIt(); ++it)
		{
			const UserScriptLogMessage& msg = *it;

			page += "<span class=\"log-timestamp\">" + msg.time.RFC822FormatedString() + "</span>: <span class=\"log-ob\">ob " + msg.script_ob_uid.toString() + "</span>: ";
			if(msg.msg_type == UserScriptLogMessage::MessageType_error)
				page += "<span class=\"log-error\">";
			else
				page += "<span class=\"log-print\">";
			page += web::Escaping::HTMLEscape(msg.msg);
			page += "</span>\n";
		}

		page += "</pre>";
	}
	else
	{
		page += "[No messages]";
	}

	page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderSecretsPage(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	std::string page;


	{ // lock scope
		Lock lock(world_state.mutex);

		User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request);
		if(logged_in_user == NULL)
		{
			page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Secrets");
			page += "You must be logged in to view this page.";
			page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);
			web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
			return;
		}

		page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/"Secrets");
		page += "<div class=\"main\">   \n";

		// Display any messages for the user
		const std::string msg_for_user = world_state.getAndRemoveUserWebMessage(logged_in_user->id);
		if(!msg_for_user.empty())
			page += "<div class=\"msg\">" + web::Escaping::HTMLEscape(msg_for_user) + "</div>  \n";

		page += "<p>Showing secrets that are accessible from Lua scripts.  Use for storing API keys etc.</p>";

		page += "<p>Note: the server administrator can see these values - only store information here that you are happy with the server administrator seeing.</p>";

		int num_secrets_displayed = 0;
		for(auto it = world_state.user_secrets.begin(); it != world_state.user_secrets.end(); ++it)
		{
			const UserSecret* secret = it->second.ptr();
			if(secret->key.user_id == logged_in_user->id)
			{
				page += "<p>" + web::Escaping::HTMLEscape(secret->key.secret_name) + ": " + web::Escaping::HTMLEscape(secret->value) + "</p>";
				num_secrets_displayed++;

				page +=
				"	<form action=\"/delete_secret_post\" method=\"post\">									\n"
				"		<input id=\"secret_name-id\" type=\"hidden\" name=\"secret_name\" value=\"" + web::Escaping::HTMLEscape(secret->key.secret_name) + "\" />			\n"
				"		<button type=\"submit\" id=\"delete-secret-button\" class=\"button-link\">Delete Secret</button>			\n"
				"	</form>																					\n"
				"	<br/>";
			}
		}

		if(num_secrets_displayed == 0)
			page += "[No secrets]";

		page +=
			"	<br/><br/>		\n"
			"	<form action=\"/add_secret_post\" method=\"post\">									\n"
			"		<label for=\"secret_name-id\">Secret name:</label><br/>							\n"
			"		<input id=\"secret_name-id\" type=\"string\" name=\"secret_name\" /><br/>			\n"

			"		<label for=\"secret_value-id\">Secret value:</label><br/>						\n"
			"		<input id=\"secret_value-id\" type=\"string\" name=\"secret_value\" /><br>			\n"

			"		<button type=\"submit\" id=\"add-secret-button\" class=\"button-link\">Add Secret</button>			\n"
			"	</form>";
	}

	page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void handleAddSecretPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	try
	{ // lock scope

		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const web::UnsafeString secret_name  = request_info.getPostField("secret_name");
		const web::UnsafeString secret_value = request_info.getPostField("secret_value");

		WorldStateLock lock(world_state.mutex);

		User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request_info);
		if(logged_in_user == NULL)
		{
			// page += WebServerResponseUtils::standardHTMLHeader(request, "User Account");
			// page += "You must be logged in to view your user account page.";
			web::ResponseUtils::writeRedirectTo(reply_info, "/account");
			return;
		}

		UserSecretRef secret = new UserSecret();
		secret->key = UserSecretKey(/*user_id=*/logged_in_user->id, /*secret_name=*/secret_name.str());
		secret->value = secret_value.str();

		if(world_state.user_secrets.count(secret->key) > 0)
		{
			world_state.setUserWebMessage(logged_in_user->id, "Secret already exists with that name");
			web::ResponseUtils::writeRedirectTo(reply_info, "/secrets");
			return;
		}

		if(secret_name.str().size() > UserSecret::MAX_SECRET_NAME_SIZE)
		{
			world_state.setUserWebMessage(logged_in_user->id, "Secret name too long");
			web::ResponseUtils::writeRedirectTo(reply_info, "/secrets");
			return;
		}

		if(secret_value.str().size() > UserSecret::MAX_VALUE_SIZE)
		{
			world_state.setUserWebMessage(logged_in_user->id, "Secret value too long");
			web::ResponseUtils::writeRedirectTo(reply_info, "/secrets");
			return;
		}
		
		world_state.user_secrets.insert(std::make_pair(secret->key, secret));
		world_state.db_dirty_user_secrets.insert(secret);

		world_state.markAsChanged();

		world_state.setUserWebMessage(logged_in_user->id, "Secret added.");
	
	} // End lock scope
	catch(web::WebsiteExcep&)
	{
		web::ResponseUtils::writeRedirectTo(reply_info, "/secrets");
		return;
	}
	catch(glare::Exception&)
	{
		web::ResponseUtils::writeRedirectTo(reply_info, "/secrets");
		return;
	}

	web::ResponseUtils::writeRedirectTo(reply_info, "/secrets");
	return;
}


void handleDeleteSecretPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	try
	{ // lock scope

		if(world_state.isInReadOnlyMode())
			throw glare::Exception("Server is in read-only mode, editing disabled currently.");

		const web::UnsafeString secret_name  = request_info.getPostField("secret_name");

		WorldStateLock lock(world_state.mutex);

		User* logged_in_user = LoginHandlers::getLoggedInUser(world_state, request_info);
		if(logged_in_user == NULL)
		{
			// page += WebServerResponseUtils::standardHTMLHeader(request, "User Account");
			// page += "You must be logged in to view your user account page.";
			web::ResponseUtils::writeRedirectTo(reply_info, "/account");
			return;
		}

		const UserSecretKey key(/*user_id = */logged_in_user->id, secret_name.str());

		auto res = world_state.user_secrets.find(key);
		if(res == world_state.user_secrets.end())
			throw glare::Exception("No secret with given secret name exists.");
		

		UserSecretRef secret = res->second;
		
		world_state.db_dirty_user_secrets.erase(secret); // Remove from dirty-set, so it's not updated in DB.

		world_state.db_records_to_delete.insert(secret->database_key);

		world_state.user_secrets.erase(key);

		world_state.markAsChanged();

		world_state.setUserWebMessage(logged_in_user->id, "Secret deleted.");
	
	} // End lock scope
	catch(web::WebsiteExcep&)
	{
		web::ResponseUtils::writeRedirectTo(reply_info, "/secrets");
		return;
	}
	catch(glare::Exception&)
	{
		web::ResponseUtils::writeRedirectTo(reply_info, "/secrets");
		return;
	}

	web::ResponseUtils::writeRedirectTo(reply_info, "/secrets");
	return;
}


} // end namespace AccountHandlers


#if BUILD_TESTS


#include "../utils/TestUtils.h"


void AccountHandlers::test()
{
}


#endif // BUILD_TESTS
