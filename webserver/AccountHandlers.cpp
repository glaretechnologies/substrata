/*=====================================================================
AccountHandlers.cpp
-------------------
Copyright Glare Technologies Limited 2021 -
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

#include "../secp256k1-master/include/secp256k1.h"
#include "../secp256k1-master/include/secp256k1_recovery.h"


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
			page += WebServerResponseUtils::standardHTMLHeader(request, "User Account");
			page += "You must be logged in to view your user account page.";
			page += WebServerResponseUtils::standardFooter(request, /*include_email_link=*/true);
			web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
			return;
		}

		page += WebServerResponseUtils::standardHeader(world_state, request, /*page title=*/logged_in_user->name);
		page += "<div class=\"main\">   \n";

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
				page += "<a href=\"/make_parcel_into_nft?parcel_id=" + parcel->id.toString() + "\">Mint as a NFT</a>";
				page += "</p>\n";
				//page += "<br/>  \n";
			}
		}

		page += "<h2>Ethereum</h2>\n";

		page += "Linked Ethereum address: ";

		if(logged_in_user->controlled_eth_address.empty())
			page += "No address linked.";
		else
		{
			page += "<span style=\"color: grey;\">" + web::Escaping::HTMLEscape(logged_in_user->controlled_eth_address) + "</span>";
			page += "<br/>";
			page += "This is an address that you control, and for which control of the address has been proven to the Substrata server.";
		}


		page += "<br/>";
		page += "<br/>";
		page += "<a href=\"/prove_eth_address_owner\">Link an Ethereum address and prove you own it by signing a message</a>";
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
			page += WebServerResponseUtils::standardHTMLHeader(request, "User Account");
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
	
		page += "<script src=\"/files/account.js\"></script>";

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

		page += "<script>"
			"current_eth_signing_nonce = \"" + logged_in_user->current_eth_signing_nonce + "\";		\n"
			"const ethereumButton = document.querySelector('.enableEthereumButton');				\n"
			"																						\n"
			"ethereumButton.addEventListener('click', connectToMetaMaskAccount);					\n"

			"const signButton = document.querySelector('.signEthereumButton');				\n"
			"																						\n"
			"signButton.addEventListener('click', requestSignMessage);					\n"
			"signButton.disabled = true;"

			"</script>";

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
			 const std::string recovered_address = ecrecover(sig.str(), message);

			 if(recovered_address == address.str())
			 {
				 // The user has proved that they control the account with the given address.

				 logged_in_user->controlled_eth_address = recovered_address;
				 world_state.markAsChanged();

				 web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "{\"msg\":\"Congrats, you have sucessfully proven you control the Ethereum address " + recovered_address + 
					 ". You will now be redirected to your account page.\", \"redirect_URL\":\"/account\"}");
			 }
			 else
			 {
				 web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "{\"msg\":\"Sorry, we could not confirm you control the Ethereum address.\"}");
			 }
		 }
		 catch(glare::Exception& e)
		 {
			 conPrint("Excep while calling ecrecover(): " + e.what());
			 web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, "{\"msg\":\"Sorry, we could not confirm you control the Ethereum address.\"}");
		 }
	 }
}


static std::string hex_to_string(const std::string& s)
{
	const std::vector<unsigned char> v = StringUtils::convertHexToBinary(s);
	std::string res(v.size(), '\0');
	if(v.size() > 0)
		std::memcpy(&res[0], &v[0], v.size());
	return res;
}


static std::string bytes_to_hex_string(const uint8_t* data, size_t size)
{
	return StringUtils::convertByteArrayToHexString(data, size);
}


// Recover an Ethereum address from a signature and message
// Adapted from https://ethereum.stackexchange.com/questions/75903/ecrecover-in-c
std::string ecrecover(const std::string& sig, const std::string& msg) // hex-encoded sig, plain text msg
{
	std::string _sig = hex_to_string(sig.substr(2)); // strip 0x

	if(_sig.size() != 65)
		throw glare::Exception("Invalid sig size");

	int v = _sig[64];
	_sig = _sig.substr(0, 64);

	if(v > 3)
		v -= 27;

	auto* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);

	secp256k1_ecdsa_recoverable_signature rawSig;
	if(!secp256k1_ecdsa_recoverable_signature_parse_compact(ctx, &rawSig, (const unsigned char*)_sig.data(), v))
		throw glare::Exception("Cannot parse compact");

	const int HASH_SIZE = 32;
	uint8 hash[HASH_SIZE];

	const std::string wrapped_msg = std::string("\x19") + "Ethereum Signed Message:\n" + toString(msg.size()) + msg;

	Keccak256::hash(wrapped_msg, hash); // hash wrapped message

	secp256k1_pubkey rawPubkey;
	if(!secp256k1_ecdsa_recover(ctx, &rawPubkey, &rawSig, hash))
		throw glare::Exception("Cannot recover key");

	uint8_t pubkey[65];
	size_t biglen = 65;

	secp256k1_ec_pubkey_serialize(ctx, pubkey, &biglen, &rawPubkey, SECP256K1_EC_UNCOMPRESSED);

	std::string out = std::string(pubkey, pubkey + 65).substr(1);

	Keccak256::hash(out, hash);
	// 32 bytes = 64 hex chars.
	// We want the last 20 bytes = last 40 hex chars.

	const std::string hashed_pubkey = bytes_to_hex_string(hash, HASH_SIZE);

	return "0x" + hashed_pubkey.substr(24);
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
			page += WebServerResponseUtils::standardHTMLHeader(request, "User Account");
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
				page += "You must link an ethereum address to your account first.";
			}
			else
			{
				page += "<p>Are you sure you want to make this parcel an ERC721 NFT on the Ethereum blockchain?  This cannot currently be reversed.</p>";

				page += "<p>If you make this parcel an NFT, then the Substrata server will consider the owner of the parcel NFT to be the owner "
					" of the parcel.</p>";

				page += "<p>Ownership of the NFT will be assigned to your Ethereum address: <span style=\"color: grey;\">" +
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


void handleMakeParcelIntoNFTPost(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	const ParcelID parcel_id(request_info.getPostIntField("parcel_id"));

	std::string user_eth_address;

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

		world_state.markAsChanged();

		user_eth_address = logged_in_user->controlled_eth_address;
	
	} // End lock scope

	// Make an Eth transaction to mint the parcel

}


} // end namespace AccountHandlers


#if BUILD_TESTS


#include "../utils/TestUtils.h"


void AccountHandlers::test()
{
	// Test ecrecover()
	try
	{
		// This signature was generated by MetaMask
		const std::string signature = "0x7e2c6f41b4249cdd7a59c4fdda6e377c08d2f24631aefb5316521a69e691a2df57e7c7099b6b1f7b651ee9f0e0c6558b4687098a242eaebdaa53b52240cac8e41b";
		const std::string msg = "Please sign this message to confirm you own the Ethereum account";
		
		const std::string address = ecrecover(signature, msg);
		
		conPrint(address);
		testAssert(address == "0xe5a994be9e94513bcb1a0a5991470d9fde380d26");
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}

	// Test that a different signature does not result in the same address
	try
	{
		// This signature was generated by MetaMask
		const std::string signature = "0x8e2c6f41b4249cdd7a59c4fdda6e377c08d2f24631aefb5316521a69e691a2df57e7c7099b6b1f7b651ee9f0e0c6558b4687098a242eaebdaa53b52240cac8e41b";
		const std::string msg = "Please sign this message to confirm you own the Ethereum account";
		const std::string address = ecrecover(signature, msg);
		conPrint(address);
		testAssert(address != "0xe5a994be9e94513bcb1a0a5991470d9fde380d26");
	}
	catch(glare::Exception& e)
	{
		conPrint(e.what());
	}

	// Test that a different message does not result in the same address
	try
	{
		// This signature was generated by MetaMask
		const std::string signature = "0x7e2c6f41b4249cdd7a59c4fdda6e377c08d2f24631aefb5316521a69e691a2df57e7c7099b6b1f7b651ee9f0e0c6558b4687098a242eaebdaa53b52240cac8e41b";
		const std::string msg = "Xlease sign this message to confirm you own the Ethereum account";
		const std::string address = ecrecover(signature, msg);
		conPrint(address);
		testAssert(address != "0xe5a994be9e94513bcb1a0a5991470d9fde380d26");
	}
	catch(glare::Exception& e)
	{
		conPrint(e.what());
	}


	//{
	//	// From https://web3js.readthedocs.io/en/v1.2.11/web3-eth-personal.html
	//	const std::string signature = "0x30755ed65396facf86c53e6217c52b4daebe72aa4941d89635409de4c9c7f9466d4e9aaec7977f05e923889b33c0d0dd27d7226b6e6f56ce737465c5cfd04be400";
	//	const std::string msg = "Hello world";
	//	//const std::string hexmsg = "0x" + string_to_hex(msg);
	//	std::string address = ecrecover(signature, msg);
	//	// should be "0x11f4d0A3c12e86B4b5F39B213F7E19D048276DAe"

	//	conPrint(address);
	//}
}


#endif // BUILD_TESTS
