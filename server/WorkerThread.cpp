/*=====================================================================
WorkerThread.cpp
------------------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#include "WorkerThread.h"


#include "ServerWorldState.h"
#include "Server.h"
#include "Screenshot.h"
#include "SubEthTransaction.h"
#include "MeshLODGenThread.h"
#include "../webserver/LoginHandlers.h"
#include "../shared/Protocol.h"
#include "../shared/ProtocolStructs.h"
#include "../shared/UID.h"
#include "../shared/WorldObject.h"
#include "../shared/MessageUtils.h"
#include "../shared/FileTypes.h"
#include "../shared/LuaScriptEvaluator.h"
#include "../shared/ObjectEventHandlers.h"
#include <vec3.h>
#include <ConPrint.h>
#include <Clock.h>
#include <AESEncryption.h>
#include <SHA256.h>
#include <Base64.h>
#include <Exception.h>
#include <MySocket.h>
#include <URL.h>
#include <Lock.h>
#include <StringUtils.h>
#include <CryptoRNG.h>
#include <SocketBufferOutStream.h>
#include <PlatformUtils.h>
#include <KillThreadMessage.h>
#include <Parser.h>
#include <FileUtils.h>
#include <MemMappedFile.h>
#include <FileOutStream.h>
#include <networking/RecordingSocket.h>
#include <maths/CheckedMaths.h>
#include <openssl/err.h>
#include <algorithm>
#include <RuntimeCheck.h>
#include <Timer.h>


static const bool VERBOSE = false;
static const int MAX_STRING_LEN = 10000;
static const bool CAPTURE_TRACES = false; // If true, records a trace of data read from the socket, for fuzz seeding.


WorkerThread::WorkerThread(const Reference<SocketInterface>& socket_, Server* server_)
:	socket(socket_),
	server(server_),
	scratch_packet(SocketBufferOutStream::DontUseNetworkByteOrder),
	fuzzing(false),
	write_trace(false)
{
	//if(VERBOSE) print("event_fd.efd: " + toString(event_fd.efd));

	if(CAPTURE_TRACES)
		socket = new RecordingSocket(socket);
}


WorkerThread::~WorkerThread()
{
}


// Checks if the resource is present on the server, if not, sends a GetFile message (or rather enqueues to send) to the client.
void WorkerThread::sendGetFileMessageIfNeeded(const std::string& resource_URL)
{
	if(!ResourceManager::isValidURL(resource_URL))
		throw glare::Exception("Invalid URL: '" + resource_URL + "'");

	// If this is a web URL, then we don't need to get it from the client.
	if(hasPrefix(resource_URL, "http://") || hasPrefix(resource_URL, "https://"))
		return;

	// See if we have this file on the server already
	{
		const ResourceRef resource = server->world_state->resource_manager->getExistingResourceForURL(resource_URL);
		if(resource.nonNull() && (resource->getState() == Resource::State_Present))
		{
			// Check hash?
			conPrintIfNotFuzzing("resource file with URL '" + resource_URL + "' already present on disk.");
		}
		else
		{
			conPrintIfNotFuzzing("resource file with URL '" + resource_URL + "' not present on disk, sending get file message to client.");

			// We need the file from the client.
			// Send the client a 'get file' message
			MessageUtils::initPacket(scratch_packet, Protocol::GetFile);
			scratch_packet.writeStringLengthFirst(resource_URL);
			MessageUtils::updatePacketLengthField(scratch_packet);

			this->enqueueDataToSend(scratch_packet);
		}
	}
}


static void writeErrorMessageToClient(SocketInterfaceRef& socket, const std::string& msg)
{
	SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
	MessageUtils::initPacket(packet, Protocol::ErrorMessageID);
	packet.writeStringLengthFirst(msg);
	MessageUtils::updatePacketLengthField(packet);

	socket->writeData(packet.buf.data(), packet.buf.size());
	socket->flush();
}


// Enqueues packet to all WorkerThreads to send to all clients connected to the server.
static void enqueuePacketToBroadcast(const SocketBufferOutStream& packet_buffer, Server* server)
{
	assert(packet_buffer.buf.size() > 0);
	if(packet_buffer.buf.size() > 0)
	{
		Lock lock(server->worker_thread_manager.getMutex());
		for(auto i = server->worker_thread_manager.getThreads().begin(); i != server->worker_thread_manager.getThreads().end(); ++i)
		{
			assert(dynamic_cast<WorkerThread*>(i->getPointer()));
			static_cast<WorkerThread*>(i->getPointer())->enqueueDataToSend(packet_buffer);
		}
	}
}


void WorkerThread::handleResourceUploadConnection()
{
	conPrintIfNotFuzzing("handleResourceUploadConnection()");

	try
	{
		const std::string username = socket->readStringLengthFirst(MAX_STRING_LEN);
		const std::string password = socket->readStringLengthFirst(MAX_STRING_LEN);

		conPrintIfNotFuzzing("\tusername: '" + username + "'");

		UserID client_user_id = UserID::invalidUserID();
		std::string client_user_name;
		{
			Lock lock(server->world_state->mutex);
			auto res = server->world_state->name_to_users.find(username);
			if(res != server->world_state->name_to_users.end())
			{
				User* user = res->second.getPointer();
				if(user->isPasswordValid(password))
				{
					// Password is valid, log user in.
					client_user_id = user->id;
					client_user_name = user->name;
				}
			}
		}

		if(!client_user_id.valid())
		{
			conPrintIfNotFuzzing("\tLogin failed.");
			socket->writeUInt32(Protocol::LogInFailure); // Note that this is not a framed message.
			socket->writeStringLengthFirst("Login failed.");

			socket->writeData(scratch_packet.buf.data(), scratch_packet.buf.size());
			return;
		}

		if(server->world_state->isInReadOnlyMode())
		{
			conPrint("\tin read only-mode..");
			socket->writeUInt32(Protocol::ServerIsInReadOnlyMode); // Note that this is not a framed message.
			socket->writeStringLengthFirst("Server is in read-only mode, can't upload files.");

			socket->writeData(scratch_packet.buf.data(), scratch_packet.buf.size());
			return;
		}


		const std::string URL = socket->readStringLengthFirst(MAX_STRING_LEN);

		conPrintIfNotFuzzing("\tURL: '" + URL + "'");

		/*if(!ResourceManager::isValidURL(URL))
		{
		conPrint("Invalid URL '" + URL + "'");
		throw glare::Exception("Invalid URL '" + URL + "'");
		}*/

		// See if we have a resource in the ResourceManager already
		ResourceRef resource = server->world_state->resource_manager->getOrCreateResourceForURL(URL); // Will create a new Resource ob if not already inserted.

		{
			Lock lock(server->world_state->mutex);
			server->world_state->addResourcesAsDBDirty(resource);
		}

		if(resource->owner_id == UserID::invalidUserID())
		{
			// No such resource existed before, client may create this resource.
		}
		else // else if resource already existed:
		{
			if(resource->owner_id != client_user_id) // If this resource already exists and was created by someone else:
			{
				socket->writeUInt32(Protocol::NoWritePermissions); // Note that this is not a framed message.
				socket->writeStringLengthFirst("Not allowed to upload resource to URL '" + URL + ", someone else created a resource at this URL already.");
				return;
			}
		}

		const bool valid_extension = FileTypes::hasSupportedExtension(URL);
		if(!valid_extension)
		{
			socket->writeUInt32(Protocol::InvalidFileType); // Note that this is not a framed message.
			socket->writeStringLengthFirst("Invalid file extension.");
			return;
		}
		
		// resource->setState(Resource::State_Transferring); // Don't set this (for now) or we will have to handle changing it on exceptions below.


		const uint64 file_len = socket->readUInt64();
		conPrintIfNotFuzzing("\tfile_len: " + toString(file_len) + " B");
		if(file_len == 0)
		{
			socket->writeUInt32(Protocol::InvalidFileSize); // Note that this is not a framed message.
			socket->writeStringLengthFirst("Invalid file len of zero.");
			return;
		}

		// TODO: cap length in a better way
		if(file_len > 1000000000)
		{
			socket->writeUInt32(Protocol::InvalidFileSize); // Note that this is not a framed message.
			socket->writeStringLengthFirst("uploaded file too large.");
			return;
		}

		// Otherwise upload is allowed:
		socket->writeUInt32(Protocol::UploadAllowed);

		// Save to disk
		const std::string local_path = server->world_state->resource_manager->pathForURL(URL);

		conPrintIfNotFuzzing("\tStreaming to disk at '" + local_path + "'...");

		{
			FileOutStream file(local_path, std::ios::binary | std::ios::trunc); // Remove any existing data in the file

			uint64 offset = 0;
			const uint64 MAX_CHUNK_SIZE = 1ull << 14;
			js::Vector<uint8, 16> temp_buf(MAX_CHUNK_SIZE);
			while(offset < file_len)
			{
				const uint64 chunk_size = myMin(file_len - offset, MAX_CHUNK_SIZE);
				runtimeCheck(offset + chunk_size <= file_len);
				runtimeCheck(chunk_size <= temp_buf.size());
				socket->readData(temp_buf.data(), chunk_size);

				if(!fuzzing) // Don't write to disk while fuzzing.
					file.writeData(temp_buf.data(), chunk_size);

				offset += chunk_size;
			}

			file.close(); // Manually call close, to check for any errors via failbit.
		} // End scope for FileOutStream


		conPrintIfNotFuzzing("\tReceived file with URL '" + URL + "' from client. (" + toString(file_len) + " B)");

		resource->owner_id = client_user_id;
		resource->setState(Resource::State_Present);

		{
			Lock lock(server->world_state->mutex);
			server->world_state->addResourcesAsDBDirty(resource);
		}

		// Send NewResourceOnServer message to connected clients
		{
			MessageUtils::initPacket(scratch_packet, Protocol::NewResourceOnServer);
			scratch_packet.writeStringLengthFirst(URL);
			MessageUtils::updatePacketLengthField(scratch_packet);

			enqueuePacketToBroadcast(scratch_packet, server);
		}


		// See if this is a resource that is used by an object.  If so, send a message to the MeshLodGenThread to generate LOD levels and KTX versions of it if applicable.
		{
			std::vector<UID> ob_uids; // UIDs of objects which use this resource
			{
				WorldStateLock lock(server->world_state->mutex);
				for(auto world_it = server->world_state->world_states.begin(); world_it != server->world_state->world_states.end(); ++world_it)
				{
					ServerWorldState* world = world_it->second.ptr();

					std::set<DependencyURL> URLs;
					const ServerWorldState::ObjectMapType& objects = world->getObjects(lock);
					for(auto it = objects.begin(); it != objects.end(); ++it)
					{
						const WorldObject* ob = it->second.ptr();
						URLs.clear();
						ob->getDependencyURLSetForAllLODLevels(URLs);

						if(URLs.count(DependencyURL(URL)) > 0) // If the object uses the resource with this URL:
						{
							ob_uids.push_back(ob->uid);
						}
					}
				}
			}

			for(size_t i=0; i<ob_uids.size(); ++i)
			{
				CheckGenResourcesForObject* msg = new CheckGenResourcesForObject();
				msg->ob_uid = ob_uids[i];
				server->enqueueMsgForLodGenThread(msg);
			}
		}


		// Connection will be closed by the client after the client has uploaded the file.  Wait for the connection to close.
		socket->startGracefulShutdown(); // Tell sockets lib to send a FIN packet to the client.
		socket->waitForGracefulDisconnect(); // Wait for a FIN packet from the client. (indicated by recv() returning 0).  We can then close the socket without going into a wait state.
	}
	catch(MySocketExcep& e)
	{
		if(e.excepType() == MySocketExcep::ExcepType_ConnectionClosedGracefully)
			conPrint("Resource upload client from " + IPAddress::formatIPAddressAndPort(socket->getOtherEndIPAddress(), socket->getOtherEndPort()) + " closed connection gracefully.");
		else
			conPrint("Socket error: " + e.what());
	}
	catch(glare::Exception& e)
	{
		conPrintIfNotFuzzing("glare::Exception: " + e.what());
	}
	catch(std::bad_alloc&)
	{
		conPrint("WorkerThread: Caught std::bad_alloc.");
	}
}


void WorkerThread::handleResourceDownloadConnection()
{
	conPrintIfNotFuzzing("handleResourceDownloadConnection()");

	try
	{

		while(!should_quit)
		{
			const uint32 msg_type = socket->readUInt32();
			if(msg_type == Protocol::GetFiles)
			{
				const uint64 num_resources = socket->readUInt64();
				
				conPrintIfNotFuzzing("Handling GetFiles:\tnum resources requested: " + toString(num_resources));

				for(size_t i=0; i<num_resources; ++i)
				{
					const std::string URL = socket->readStringLengthFirst(MAX_STRING_LEN);

					conPrintIfNotFuzzing("\tRequested URL: '" + URL + "'");

					if(!ResourceManager::isValidURL(URL))
					{
						conPrint("\tRequested URL was invalid.");
						socket->writeUInt32(1); // write error msg to client
					}
					else
					{
						// conPrint("\tRequested URL was valid.");

						const ResourceRef resource = server->world_state->resource_manager->getExistingResourceForURL(URL);
						if(resource.isNull() || (resource->getState() != Resource::State_Present))
						{
							conPrintIfNotFuzzing("\tRequested URL was not present on disk.");
							socket->writeUInt32(1); // write error msg to client
						}
						else
						{
							const std::string local_path = server->world_state->resource_manager->getLocalAbsPathForResource(*resource);

							// conPrint("\tlocal path: '" + local_path + "'");

							try
							{
								// Load resource off disk
								MemMappedFile file(local_path);
								// conPrint("\tSending file to client.");
								socket->writeUInt32(0); // write OK msg to client
								socket->writeUInt64(file.fileSize()); // Write file size
								socket->writeData(file.fileData(), file.fileSize()); // Write file data

								conPrintIfNotFuzzing("\tSent file '" + local_path + "' to client. (" + toString(file.fileSize()) + " B)");
							}
							catch(glare::Exception& e)
							{
								conPrintIfNotFuzzing("\tException while trying to load file for URL: " + e.what());

								socket->writeUInt32(1); // write error msg to client
							}
						}
					}
				}
			}
			else if(msg_type == Protocol::CyberspaceGoodbye)
			{
				socket->startGracefulShutdown(); // Tell sockets lib to send a FIN packet to the client.
				socket->waitForGracefulDisconnect(); // Wait for a FIN packet from the client. (indicated by recv() returning 0).  We can then close the socket without going into a wait state.
				return;
			}
			else
			{
				conPrintIfNotFuzzing("handleResourceDownloadConnection(): Unhandled msg type: " + toString(msg_type));
				return;
			}
		}
	}
	catch(MySocketExcep& e)
	{
		if(e.excepType() == MySocketExcep::ExcepType_ConnectionClosedGracefully)
			conPrint("Resource download client from " + IPAddress::formatIPAddressAndPort(socket->getOtherEndIPAddress(), socket->getOtherEndPort()) + " closed connection gracefully.");
		else
			conPrint("Socket error: " + e.what());
	}
	catch(glare::Exception& e)
	{
		conPrintIfNotFuzzing("glare::Exception: " + e.what());
	}
	catch(std::bad_alloc&)
	{
		conPrint("WorkerThread: Caught std::bad_alloc.");
	}
}


void WorkerThread::handleScreenshotBotConnection()
{
	conPrintIfNotFuzzing("handleScreenshotBotConnection()");

	const std::string password = socket->readStringLengthFirst(10000);
	if(password != server->world_state->getCredential("screenshot_bot_password"))
		throw glare::Exception("screenshot bot password was not correct.");

	try
	{
		while(!should_quit)
		{
			// Poll server state for a screenshot request
			ScreenshotRef screenshot;

			{ // lock scope
				Lock lock(server->world_state->mutex);

				server->world_state->last_screenshot_bot_contact_time = TimeStamp::currentTime();

				// Find first screenshot in screenshots map in ScreenshotState_notdone state.  NOTE: slow linear scan.
				for(auto it = server->world_state->screenshots.begin(); it != server->world_state->screenshots.end(); ++it)
				{
					if(it->second->state == Screenshot::ScreenshotState_notdone)
					{
						screenshot = it->second;
						break;
					}
				}

				if(screenshot.isNull())
				{
					// Find first screenshot in map_tile_info map in ScreenshotState_notdone state.  NOTE: slow linear scan.
					for(auto it = server->world_state->map_tile_info.info.begin(); it != server->world_state->map_tile_info.info.end(); ++it)
					{
						TileInfo& tile_info = it->second;
						if(tile_info.cur_tile_screenshot.nonNull() && tile_info.cur_tile_screenshot->state == Screenshot::ScreenshotState_notdone)
						{
							screenshot = tile_info.cur_tile_screenshot;
							break;
						}
					}
				}
			} // End lock scope

			if(screenshot.nonNull()) // If there is a screenshot to take:
			{
				if(!screenshot->is_map_tile)
				{
					socket->writeUInt32(Protocol::ScreenShotRequest);

					socket->writeDouble(screenshot->cam_pos.x);
					socket->writeDouble(screenshot->cam_pos.y);
					socket->writeDouble(screenshot->cam_pos.z);
					socket->writeDouble(screenshot->cam_angles.x);
					socket->writeDouble(screenshot->cam_angles.y);
					socket->writeDouble(screenshot->cam_angles.z);
					socket->writeInt32(screenshot->width_px);
					socket->writeInt32(screenshot->highlight_parcel_id);
				}
				else
				{
					socket->writeUInt32(Protocol::TileScreenShotRequest);

					socket->writeInt32(screenshot->tile_x);
					socket->writeInt32(screenshot->tile_y);
					socket->writeInt32(screenshot->tile_z);
				}

				// Read response
				const uint32 result = socket->readUInt32();
				if(result == Protocol::ScreenShotSucceeded)
				{
					// Read screenshot data
					const uint64 data_len = socket->readUInt64();
					if(data_len > 100000000) // ~100MB
						throw glare::Exception("data_len was too large");

					conPrint("Receiving screenshot of " + toString(data_len) + " B");
					std::vector<uint8> data(data_len);
					socket->readData(data.data(), data_len);

					conPrint("Received screenshot of " + toString(data_len) + " B");

					// Generate random path
					const int NUM_BYTES = 16;
					uint8 pathdata[NUM_BYTES];
					CryptoRNG::getRandomBytes(pathdata, NUM_BYTES);
					const std::string screenshot_filename = "screenshot_" + StringUtils::convertByteArrayToHexString(pathdata, NUM_BYTES) + ".jpg";
					const std::string screenshot_path = server->screenshot_dir + "/" + screenshot_filename;

					// Save screenshot to path
					if(!fuzzing) // Don't write to disk while fuzzing
						FileUtils::writeEntireFile(screenshot_path, data);

					conPrint("Saved to disk at " + screenshot_path);


					// Add map tile as a resource too, for access by embedded minimap on client.
					if(screenshot->is_map_tile)
					{
						// Copy screenshot into resource dir and add as a resource
						const std::string URL = screenshot_filename;
						ResourceRef resource = server->world_state->resource_manager->getOrCreateResourceForURL(URL); // Will create a new Resource ob if not already inserted.
						const std::string local_abs_path = server->world_state->resource_manager->getLocalAbsPathForResource(*resource);

						FileUtils::copyFile(screenshot_path, local_abs_path); 

						resource->owner_id = UserID::invalidUserID();
						resource->setState(Resource::State_Present);

						{
							Lock lock(server->world_state->mutex);
							server->world_state->addResourcesAsDBDirty(resource);
						}

						screenshot->URL = URL;
					}


					screenshot->state = Screenshot::ScreenshotState_done;
					screenshot->local_path = screenshot_path;

					{
						Lock lock(server->world_state->mutex);
						server->world_state->addScreenshotAsDBDirty(screenshot);

						if(screenshot->is_map_tile) // If we received a tile screenshot, mark map tile info as dirty to get it saved.
							server->world_state->map_tile_info.db_dirty = true;
					}
				}
				else
					throw glare::Exception("Client reported screenshot taking failed.");
			}
			else
			{
				socket->writeUInt32(Protocol::KeepAlive); // Send a keepalive message just to check the socket is still connected.

				// There is no current screenshot request, sleep for a while
				if(!fuzzing)
					PlatformUtils::Sleep(10000);
			}
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("handleScreenshotBotConnection: glare::Exception: " + e.what());
	}
	catch(std::exception& e)
	{
		conPrint(std::string("handleScreenshotBotConnection: Caught std::exception: ") + e.what());
	}
}


void WorkerThread::handleEthBotConnection()
{
	conPrintIfNotFuzzing("handleEthBotConnection()");

	try
	{
		// Do authentication
		const std::string password = socket->readStringLengthFirst(10000);
		if(password != server->world_state->getCredential("eth_bot_password"))
			throw glare::Exception("eth bot password was not correct.");

			
		while(!should_quit)
		{
			// Poll server state for a request
			SubEthTransactionRef trans;
			uint64 largest_nonce_used = 0; 
			{ // lock scope
				Lock lock(server->world_state->mutex);

				server->world_state->last_eth_bot_contact_time = TimeStamp::currentTime();

				// Find first transction in New state.  NOTE: slow linear scan.
				for(auto it = server->world_state->sub_eth_transactions.begin(); it != server->world_state->sub_eth_transactions.end(); ++it)
				{
					if(it->second->state == SubEthTransaction::State_New)
					{
						trans = it->second;
						break;
					}
				}

				// Work out nonce to use for this transaction.  First, work out largest nonce used for succesfully submitted transactions
				for(auto it = server->world_state->sub_eth_transactions.begin(); it != server->world_state->sub_eth_transactions.end(); ++it)
				{
					if(it->second->state == SubEthTransaction::State_Completed)
						largest_nonce_used = myMax(largest_nonce_used, it->second->nonce);
				}
			} // End lock scope

			const uint64 next_nonce = myMax((uint64)server->world_state->eth_info.min_next_nonce, largest_nonce_used + 1); // min_next_nonce is to reflect any existing transactions on account

			if(trans.nonNull()) // If there is a transaction to submit:
			{
				socket->writeUInt32(Protocol::SubmitEthTransactionRequest);

				// Update transaction nonce and submitted_time
				{ // lock scope
					Lock lock(server->world_state->mutex);

					trans->nonce = next_nonce; 
					trans->submitted_time = TimeStamp::currentTime();

					server->world_state->addSubEthTransactionAsDBDirty(trans);
				}
				
				writeToStream(*trans, *socket);

				// Read response
				const uint32 result = socket->readUInt32();
				if(result == Protocol::EthTransactionSubmitted)
				{
					const UInt256 transaction_hash = readUInt256FromStream(*socket);

					conPrint("Transaction was submitted.");

					// Mark parcel as minted as an NFT
					{ // lock scope
						WorldStateLock lock(server->world_state->mutex);

						trans->state = SubEthTransaction::State_Completed; // State_Submitted;
						trans->transaction_hash = transaction_hash;

						server->world_state->addSubEthTransactionAsDBDirty(trans);

						auto parcel_res = server->world_state->getRootWorldState()->getParcels(lock).find(trans->parcel_id);
						if(parcel_res != server->world_state->getRootWorldState()->getParcels(lock).end())
						{
							Parcel* parcel = parcel_res->second.ptr();
							parcel->nft_status = Parcel::NFTStatus_MintedNFT;
							server->world_state->getRootWorldState()->addParcelAsDBDirty(parcel, lock);
							server->world_state->markAsChanged();
						}
					} // End lock scope
				}
				else if(result == Protocol::EthTransactionSubmissionFailed)
				{
					conPrint("Transaction submission failed.");

					const std::string submission_error_message = socket->readStringLengthFirst(10000);

					{ // lock scope
						Lock lock(server->world_state->mutex);

						trans->state = SubEthTransaction::State_Submitted;
						trans->transaction_hash = UInt256(0);
						trans->submission_error_message = submission_error_message;

						server->world_state->addSubEthTransactionAsDBDirty(trans);
					}
				}
				else
					throw glare::Exception("Client reported transaction submission failed.");
			}
			else
			{
				socket->writeUInt32(Protocol::KeepAlive); // Send a keepalive message just to check the socket is still connected.

				// There is no current transaction to process, sleep for a while
				if(!fuzzing)
					PlatformUtils::Sleep(10000);
			}
		}
	}
	catch(glare::Exception& e)
	{
		conPrintIfNotFuzzing("handleEthBotConnection: glare::Exception: " + e.what());
	}
	catch(std::exception& e)
	{
		conPrint(std::string("handleEthBotConnection: Caught std::exception: ") + e.what());
	}
}


static bool objectIsInParcelForWhichLoggedInUserHasWritePerms(const WorldObject& ob, const UserID& user_id, ServerWorldState& world_state, WorldStateLock& lock)
{
	assert(user_id.valid());

	const Vec4f ob_pos = ob.pos.toVec4fPoint();

	ServerWorldState::ParcelMapType& parcels = world_state.getParcels(lock);
	for(ServerWorldState::ParcelMapType::iterator it = parcels.begin(); it != parcels.end(); ++it)
	{
		const Parcel* parcel = it->second.ptr();
		if(parcel->pointInParcel(ob_pos) && parcel->userHasWritePerms(user_id))
			return true;
	}

	return false;
}


// Is the client connected to their personal world?
// Precondition: user_name is valid
static bool connectedToUsersPersonalWorld(const std::string& user_name, const std::string& connected_world_name)
{
	assert(!user_name.empty());

	return !connected_world_name.empty() && (user_name == connected_world_name);
}


// NOTE: world state mutex should be locked before calling this method.
static bool userHasObjectWritePermissions(const WorldObject& ob, const UserID& user_id, const std::string& user_name, const std::string& connected_world_name, ServerWorldState& world_state, bool allow_light_mapper_bot_full_perms,
	WorldStateLock& lock)
{
	if(user_id.valid())
	{
		return (user_id == ob.creator_id) || // If the user created/owns the object
			isGodUser(user_id) || // or if the user is the god user (id 0)
			(allow_light_mapper_bot_full_perms && (user_name == "lightmapperbot")) || // lightmapper bot has full write permissions for now.
			connectedToUsersPersonalWorld(user_name, connected_world_name) || // or if this is the user's personal world
			objectIsInParcelForWhichLoggedInUserHasWritePerms(ob, user_id, world_state, lock); // Can modify objects owned by other people if they are in parcels you have write permissions for.
	}
	else
		return false;
}


static bool userConnectedToTheirPersonalWorldOrGodUser(const UserID& user_id, const std::string& user_name, const std::string& connected_world_name)
{
	return isGodUser(user_id) || // if the user is the god user (id 0)
		connectedToUsersPersonalWorld(user_name, connected_world_name); // or if this is the user's personal world
}


// Does the user have permission to create the given object with its current transformation?
// NOTE: world state mutex should be locked before calling this method.
static bool userHasObjectCreationPermissions(const WorldObject& ob, const UserID& user_id, const std::string& user_name, const std::string& connected_world_name, ServerWorldState& world_state, WorldStateLock& lock)
{
	if(user_id.valid())
	{
		return isGodUser(user_id) || // if the user is the god user
			connectedToUsersPersonalWorld(user_name, connected_world_name) || // or if this is the user's personal world
			objectIsInParcelForWhichLoggedInUserHasWritePerms(ob, user_id, world_state, lock); // Or this object is in a parcel we have write permissions for.
	}
	else
		return false;
}


// This is for editing the parcel itself.
// NOTE: world state mutex should be locked before calling this method.
static bool userHasParcelWritePermissions(const Parcel& parcel, const UserID& user_id, const std::string& connected_world_name, ServerWorldState& world_state)
{
	if(user_id.valid())
	{
		return (user_id == parcel.owner_id) || // If the user created/owns the object
			isGodUser(user_id); // or if the user is the god user (id 0)
		// TODO: Add if user is a parcel admin also?
	}
	else
		return false;
}


static float maxAudioVolumeForObject(const WorldObject& ob, const UserID& user_id, const std::string& user_name, const std::string& connected_world_name)
{
	return userConnectedToTheirPersonalWorldOrGodUser(user_id, user_name, connected_world_name) ? 1000.f : 4.f;
}


static const float chunk_w = 128;


static void markLODChunkAsNeedsRebuildForChangedObject(ServerWorldState* world_state, const WorldObject* ob, WorldStateLock& lock)
{
	const Vec4f centroid = ob->getCentroidWS();
	const int chunk_x = Maths::floorToInt(centroid[0] / chunk_w);
	const int chunk_y = Maths::floorToInt(centroid[1] / chunk_w);
	const Vec3i chunk_coords(chunk_x, chunk_y, 0);

	auto res = world_state->getLODChunks(lock).find(chunk_coords);
	if(res != world_state->getLODChunks(lock).end())
	{
		conPrint("Marking LODChunk " + chunk_coords.toString() + " as needs_rebuild=true");
		res->second->needs_rebuild = true;
	}
}


void WorkerThread::doRun()
{
	PlatformUtils::setCurrentThreadNameIfTestsEnabled("WorkerThread");


	if(CAPTURE_TRACES)
		socket.downcastToPtr<RecordingSocket>()->clearRecordBuf();

	ServerAllWorldsState* world_state = server->world_state.getPointer();

	UID client_avatar_uid(0);
	UserID client_user_id = UserID::invalidUserID(); // Will be an invalid reference if client is not logged in, otherwise will refer to the user account the client is logged in to.
	std::string client_user_name;
	AvatarSettings client_user_avatar_settings;
	uint32 client_user_flags = 0;

	Reference<ServerWorldState> cur_world_state; // World the client is connected to.
	bool logged_in_user_is_lightmapper_bot = false; // Just for updating the last_lightmapper_bot_contact_time.

	try
	{
		// Read hello bytes
		const uint32 hello = socket->readUInt32();
		if(hello != Protocol::CyberspaceHello)
			throw glare::Exception("Received invalid hello message (" + toString(hello) + ") from client.");
		
		// Write hello response
		socket->writeUInt32(Protocol::CyberspaceHello);

		// Read protocol version
		const uint32 client_protocol_version = socket->readUInt32();
		conPrintIfNotFuzzing("client protocol version: " + toString(client_protocol_version));
		if(client_protocol_version < 38) // We can't handle protocol versions < 38
		{
			socket->writeUInt32(Protocol::ClientProtocolTooOld);
			socket->writeStringLengthFirst("Sorry, your Substrata client is too old. Please download and install an updated client from https://substrata.info/.");

			//socket->writeStringLengthFirst("Sorry, your client protocol version (" + toString(client_protocol_version) + ") is too old, require version " + 
			//	toString(Protocol::CyberspaceProtocolVersion) + ".  Please install an updated client from https://substrata.info/.");
		}
		else
		{
			// For versions newer than our current version, consider them OK.  We will send back our current version below, which will then be used by the client.

			socket->writeUInt32(Protocol::ClientProtocolOK);
		}

		socket->writeUInt32(Protocol::CyberspaceProtocolVersion);

		const uint32 connection_type = socket->readUInt32();
	
		if(connection_type == Protocol::ConnectionTypeUploadResource)
		{
			handleResourceUploadConnection();
		}
		else if(connection_type == Protocol::ConnectionTypeDownloadResources)
		{
			handleResourceDownloadConnection();
		}
		else if(connection_type == Protocol::ConnectionTypeScreenshotBot)
		{
			handleScreenshotBotConnection();
		}
		else if(connection_type == Protocol::ConnectionTypeEthBot)
		{
			handleEthBotConnection();
		}
		else if(connection_type == Protocol::ConnectionTypeUpdates)
		{
			if(CAPTURE_TRACES)
				this->write_trace = true;

			// Read name of world to connect to
			const std::string world_name = socket->readStringLengthFirst(1000);
			conPrintIfNotFuzzing("Client connecting to world '" + world_name + "'...");
			

			{
				Lock lock(world_state->mutex);
				// Create world if didn't exist before.
				// For now only the main world ("") and personal worlds are allowed
				if(world_name == "")
				{}
				else if(world_state->name_to_users.find(world_name) != world_state->name_to_users.end()) // Else if world_name is a user name, it's valid
				{}
				else
					throw glare::Exception("Invalid world name '" + world_name + "'.");

				if(world_state->world_states[world_name].isNull())
					world_state->world_states[world_name] = new ServerWorldState();
				cur_world_state = world_state->world_states[world_name];
			}

			this->connected_world_name = world_name;

			// Write avatar UID assigned to the connected client.
			client_avatar_uid = world_state->getNextAvatarUID();
			writeToStream(client_avatar_uid, *socket);

			// If the client connected via a websocket, they can be logged in with a session cookie.
			// Note that this may only work if the websocket connects over TLS.
			{
				Lock lock(world_state->mutex);
				User* cookie_logged_in_user = LoginHandlers::getLoggedInUser(*world_state, this->websocket_request_info);
	
				if(cookie_logged_in_user != NULL)
				{
					client_user_id = cookie_logged_in_user->id;
					client_user_name = cookie_logged_in_user->name;
					client_user_avatar_settings = cookie_logged_in_user->avatar_settings; // TODO: clone materials?
					client_user_flags = cookie_logged_in_user->flags;
				}
			}

			if(client_user_id.valid())
			{
				// Send logged-in message to client
				MessageUtils::initPacket(scratch_packet, Protocol::LoggedInMessageID);
				writeToStream(client_user_id, scratch_packet);
				scratch_packet.writeStringLengthFirst(client_user_name);
				writeAvatarSettingsToStream(client_user_avatar_settings, scratch_packet);
				scratch_packet.writeUInt32(client_user_flags);
				MessageUtils::updatePacketLengthField(scratch_packet);

				socket->writeData(scratch_packet.buf.data(), scratch_packet.buf.size());
				socket->flush();
			}

			// Send TimeSyncMessage packet to client
			{
				MessageUtils::initPacket(scratch_packet, Protocol::TimeSyncMessage);
				scratch_packet.writeDouble(server->getCurrentGlobalTime());
				MessageUtils::updatePacketLengthField(scratch_packet);
				socket->writeData(scratch_packet.buf.data(), scratch_packet.buf.size());
			}

			// Send a ServerAdminMessage to client if we have a non-empty message.
			std::string server_admin_msg;
			{ // Lock scope
				Lock lock(world_state->mutex);
				server_admin_msg = world_state->server_admin_message;
			} // End lock scope
			if(!server_admin_msg.empty())
			{
				MessageUtils::initPacket(scratch_packet, Protocol::ServerAdminMessageID);
				scratch_packet.writeStringLengthFirst(server_admin_msg);
				MessageUtils::updatePacketLengthField(scratch_packet);

				socket->writeData(scratch_packet.buf.data(), scratch_packet.buf.size());
				socket->flush();
			}

			// Send world settings to client
			{
				MessageUtils::initPacket(scratch_packet, Protocol::WorldSettingsInitialSendMessage);

				{
					Lock lock(world_state->mutex);
					cur_world_state->world_settings.writeToStream(scratch_packet);
				}

				MessageUtils::updatePacketLengthField(scratch_packet);
				socket->writeData(scratch_packet.buf.data(), scratch_packet.buf.size());
			}


			// Send all current avatar state data to client
			{
				SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);

				{ // Lock scope
					WorldStateLock lock(world_state->mutex);
					const ServerWorldState::AvatarMapType& avatars = cur_world_state->getAvatars(lock);
					for(auto it = avatars.begin(); it != avatars.end(); ++it)
					{
						const Avatar* avatar = it->second.getPointer();

						// Write AvatarIsHere message
						MessageUtils::initPacket(scratch_packet, Protocol::AvatarIsHere);
						writeAvatarToNetworkStream(*avatar, scratch_packet);
						MessageUtils::updatePacketLengthField(scratch_packet);

						packet.writeData(scratch_packet.buf.data(), scratch_packet.buf.size());
					}
				} // End lock scope

				socket->writeData(packet.buf.data(), packet.buf.size());
			}

			// Send all current object data to client
			/*{
				Lock lock(world_state->mutex);
				for(auto it = cur_world_state->objects.begin(); it != cur_world_state->objects.end(); ++it)
				{
					const WorldObject* ob = it->second.getPointer();

					// Send ObjectCreated packet
					SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
					packet.writeUInt32(Protocol::ObjectCreated);
					ob->writeToNetworkStream(packet);
					socket->writeData(packet.buf.data(), packet.buf.size());
				}
			}*/

			// Send all current parcel data to client
			{
				SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);

				{ // Lock scope
					WorldStateLock lock(world_state->mutex);
					for(auto it = cur_world_state->getParcels(lock).begin(); it != cur_world_state->getParcels(lock).end(); ++it)
					{
						const Parcel* parcel = it->second.getPointer();

						// Send ParcelCreated message
						MessageUtils::initPacket(scratch_packet, Protocol::ParcelCreated);
						writeToNetworkStream(*parcel, scratch_packet, client_protocol_version);
						MessageUtils::updatePacketLengthField(scratch_packet);

						packet.writeData(scratch_packet.buf.data(), scratch_packet.buf.size());
					}
				} // End lock scope

				socket->writeData(packet.buf.data(), packet.buf.size());
				socket->flush();
			}

			// Send all current LOD chunk data to client, if they are using a sufficiently new protocol version.
			if(client_protocol_version >= 40)
			{
				SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
				{
					WorldStateLock lock(world_state->mutex);
					for(auto it = cur_world_state->getLODChunks(lock).begin(); it != cur_world_state->getLODChunks(lock).end(); ++it)
					{
						MessageUtils::initPacket(scratch_packet, Protocol::LODChunkInitialSend);
						it->second->writeToStream(scratch_packet);
						MessageUtils::updatePacketLengthField(scratch_packet);

						packet.writeData(scratch_packet.buf.data(), scratch_packet.buf.size()); // Append scratch_packet with LODChunkInitialSend message to packet.
					}
				}
				conPrint("Sending total of " + toString(packet.getWriteIndex()) + " B in LODChunkInitialSend messages");
				socket->writeData(packet.buf.data(), packet.buf.size()); // Send the data
				socket->flush();
			}


			// Send a message saying we have sent all initial state
			/*{
				SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
				packet.writeUInt32(Protocol::InitialStateSent);
				socket->writeData(packet.buf.data(), packet.buf.size());
			}*/


			assert(cur_world_state.nonNull());


			socket->setNoDelayEnabled(true); // We want to send out lots of little packets with low latency.  So disable Nagle's algorithm, e.g. send coalescing.

			while(!should_quit) // write to / read from socket loop
			{
				// See if we have any pending data to send in the data_to_send queue, and if so, send all pending data.
				if(VERBOSE) conPrint("WorkerThread: checking for pending data to send...");

				// We don't want to do network writes while holding the data_to_send_mutex.  So copy to temp_data_to_send.
				{
					Lock lock(data_to_send_mutex);
					temp_data_to_send = data_to_send;
					data_to_send.clear();
				}

				if(temp_data_to_send.nonEmpty())
				{
					socket->writeData(temp_data_to_send.data(), temp_data_to_send.size());
					socket->flush();
					temp_data_to_send.clear();
				}


				if(logged_in_user_is_lightmapper_bot)
				{
					Lock lock(server->world_state->mutex);
					server->world_state->last_lightmapper_bot_contact_time = TimeStamp::currentTime(); // bit of a hack
				}


#if defined(_WIN32) || defined(OSX)
				if(socket->readable(0.05)) // If socket has some data to read from it:
#else
				if(socket->readable(event_fd)) // Block until either the socket is readable or the event fd is signalled, which means we have data to write.
#endif
				{
					// Read msg type and length
					uint32 msg_type_and_len[2];
					socket->readData(msg_type_and_len, sizeof(uint32) * 2);
					const uint32 msg_type = msg_type_and_len[0];
					const uint32 msg_len = msg_type_and_len[1]; // Length of message, including the message type and length fields.

					if((msg_len < sizeof(uint32) * 2) || (msg_len > 1000000))
						throw glare::Exception("Invalid message size: " + toString(msg_len));

					// conPrint("WorkerThread: Read message header: id: " + toString(msg_type) + ", len: " + toString(msg_len));

					// Read entire message
					msg_buffer.buf.resizeNoCopy(msg_len);
					msg_buffer.read_index = sizeof(uint32) * 2;

					socket->readData(msg_buffer.buf.data() + sizeof(uint32) * 2, msg_len - sizeof(uint32) * 2); // Read rest of message, store in msg_buffer.

					switch(msg_type)
					{
					case Protocol::CyberspaceGoodbye:
						{
							conPrintIfNotFuzzing("WorkerThread: received CyberspaceGoodbye, starting graceful shutdown..");
							socket->startGracefulShutdown(); // Tell sockets lib to send a FIN packet to the client.
							socket->waitForGracefulDisconnect(); // Wait for a FIN packet from the client. (indicated by recv() returning 0).  We can then close the socket without going into a wait state.
							conPrintIfNotFuzzing("WorkerThread: waitForGracefulDisconnect done.");
							should_quit = 1;
							break;
						}
					case Protocol::ClientUDPSocketOpen:
						{
							conPrint("WorkerThread: received Protocol::ClientUDPSocketOpen");
							//const uint32 client_UDP_port = msg_buffer.readUInt32();
							server->clientUDPPortOpen(this, socket->getOtherEndIPAddress(), client_avatar_uid);
							break;
						}
					case Protocol::AudioStreamToServerStarted:
						{
							const uint32 sampling_rate = msg_buffer.readUInt32();
							const uint32 flags         = msg_buffer.readUInt32();
							const uint32 stream_id     = msg_buffer.readUInt32();

							if(!BitUtils::isBitSet(flags, 0x1u)) // If renew flag is not set:
								conPrint("WorkerThread: received Protocol::AudioStreamToServerStarted without renew flag");

							// Send message to all clients
							{
								MessageUtils::initPacket(scratch_packet, Protocol::AudioStreamToServerStarted);
								writeToStream(client_avatar_uid, scratch_packet); // Send client avatar UID as well.
								scratch_packet.writeUInt32(sampling_rate);
								scratch_packet.writeUInt32(flags);
								scratch_packet.writeUInt32(stream_id);
								MessageUtils::updatePacketLengthField(scratch_packet);

								enqueuePacketToBroadcast(scratch_packet, server);
							}

							break;
						}
					case Protocol::AudioStreamToServerEnded:
						{
							conPrint("WorkerThread: received Protocol::AudioStreamToServerEnded");

							// Send message to all clients
							{
								MessageUtils::initPacket(scratch_packet, Protocol::AudioStreamToServerEnded);
								writeToStream(client_avatar_uid, scratch_packet); // Send client avatar UID as well.
								MessageUtils::updatePacketLengthField(scratch_packet);

								enqueuePacketToBroadcast(scratch_packet, server);
							}

							break;
						}
					case Protocol::AvatarTransformUpdate:
						{
							//conPrint("AvatarTransformUpdate");
							const UID avatar_uid = readUIDFromStream(msg_buffer);
							const Vec3d pos = readVec3FromStream<double>(msg_buffer);
							const Vec3f rotation = readVec3FromStream<float>(msg_buffer);
							const uint32 anim_state = msg_buffer.readUInt32();

							// Look up existing avatar in world state
							{
								WorldStateLock lock(world_state->mutex);
								const ServerWorldState::AvatarMapType& avatars = cur_world_state->getAvatars(lock);
								auto res = avatars.find(avatar_uid);
								if(res != avatars.end())
								{
									Avatar* avatar = res->second.getPointer();
									avatar->pos = pos;
									avatar->rotation = rotation;
									avatar->anim_state = anim_state;
									avatar->transform_dirty = true;

									//conPrint("updated avatar transform");
								}
							}
							break;
						}
					case Protocol::AvatarPerformGesture:
						{
							//conPrint("AvatarPerformGesture");
							const UID avatar_uid = readUIDFromStream(msg_buffer);
							const std::string gesture_name = msg_buffer.readStringLengthFirst(10000);

							//conPrint("Received AvatarPerformGesture: '" + gesture_name + "'");

							//if(!client_user_id.valid())
							//{
							//	writeErrorMessageToClient(socket, "You must be logged in to perform a gesture.");
							//}
							//else
							//{
								// Enqueue AvatarPerformGesture messages to worker threads to send
								MessageUtils::initPacket(scratch_packet, Protocol::AvatarPerformGesture);
								writeToStream(avatar_uid, scratch_packet);
								scratch_packet.writeStringLengthFirst(gesture_name);
								MessageUtils::updatePacketLengthField(scratch_packet);

								enqueuePacketToBroadcast(scratch_packet, server);
							//}
							break;
						}
					case Protocol::AvatarStopGesture:
						{
							//conPrint("AvatarStopGesture");
							const UID avatar_uid = readUIDFromStream(msg_buffer);

							//if(!client_user_id.valid())
							//{
							//	writeErrorMessageToClient(socket, "You must be logged in to stop a gesture.");
							//}
							//else
							//{
								// Enqueue AvatarStopGesture messages to worker threads to send
								MessageUtils::initPacket(scratch_packet, Protocol::AvatarStopGesture);
								writeToStream(avatar_uid, scratch_packet);
								MessageUtils::updatePacketLengthField(scratch_packet);

								enqueuePacketToBroadcast(scratch_packet, server);
							//}
							break;
						}
					case Protocol::AvatarFullUpdate:
						{
							conPrintIfNotFuzzing("Protocol::AvatarFullUpdate");
							const UID avatar_uid = readUIDFromStream(msg_buffer);

							Avatar temp_avatar;
							readAvatarFromNetworkStreamGivenUID(msg_buffer, temp_avatar); // Read message data before grabbing lock

							// Look up existing avatar in world state
							{
								WorldStateLock lock(world_state->mutex);
								const ServerWorldState::AvatarMapType& avatars = cur_world_state->getAvatars(lock);
								auto res = avatars.find(avatar_uid);
								if(res != avatars.end())
								{
									Avatar* avatar = res->second.getPointer();
									avatar->copyNetworkStateFrom(temp_avatar);
									avatar->other_dirty = true;


									// Store avatar settings in the user data
									if(client_user_id.valid())
									{
										const bool avatar_settings_changed = !(client_user_avatar_settings == avatar->avatar_settings);

										if(avatar_settings_changed && !world_state->isInReadOnlyMode())
										{
											client_user_avatar_settings = avatar->avatar_settings;

											auto res2 = world_state->user_id_to_users.find(client_user_id);
											if(res2 != world_state->user_id_to_users.end())
											{
												Reference<User> client_user = res2->second;
												client_user->avatar_settings = avatar->avatar_settings;
												world_state->addUserAsDBDirty(client_user);

												conPrintIfNotFuzzing("Updated user avatar settings.  model_url: " + client_user->avatar_settings.model_url);
											}
										}
									}

									//conPrint("updated avatar transform");
								}
							}

							if(!temp_avatar.avatar_settings.model_url.empty())
								sendGetFileMessageIfNeeded(temp_avatar.avatar_settings.model_url);

							// Process resources
							std::set<DependencyURL> URLs;
							temp_avatar.getDependencyURLSetForAllLODLevels(URLs);
							for(auto it = URLs.begin(); it != URLs.end(); ++it)
								sendGetFileMessageIfNeeded(it->URL);

							break;
						}
					case Protocol::CreateAvatar:
						{
							conPrintIfNotFuzzing("received Protocol::CreateAvatar");
							// Note: name will come from user account
							// will use the client_avatar_uid that we assigned to the client
						
							Avatar temp_avatar;
							temp_avatar.uid = readUIDFromStream(msg_buffer); // Will be replaced.
							readAvatarFromNetworkStreamGivenUID(msg_buffer, temp_avatar); // Read message data before grabbing lock

							temp_avatar.name = client_user_id.valid() ? client_user_name : "Anonymous";

							const UID use_avatar_uid = client_avatar_uid;
							temp_avatar.uid = use_avatar_uid;

							// Look up existing avatar in world state
							{
								WorldStateLock lock(world_state->mutex);
								ServerWorldState::AvatarMapType& avatars = cur_world_state->getAvatars(lock);
								auto res = avatars.find(use_avatar_uid);
								if(res == avatars.end())
								{
									// Avatar for UID not already created, create it now.
									AvatarRef avatar = new Avatar();
									avatar->uid = use_avatar_uid;
									avatar->copyNetworkStateFrom(temp_avatar);
									avatar->state = Avatar::State_JustCreated;
									avatar->other_dirty = true;
									avatars.insert(std::make_pair(use_avatar_uid, avatar));

									conPrintIfNotFuzzing("created new avatar");
								}
							}

							if(!temp_avatar.avatar_settings.model_url.empty())
								sendGetFileMessageIfNeeded(temp_avatar.avatar_settings.model_url);

							// Process resources
							std::set<DependencyURL> URLs;
							temp_avatar.getDependencyURLSetForAllLODLevels(URLs);
							for(auto it = URLs.begin(); it != URLs.end(); ++it)
								sendGetFileMessageIfNeeded(it->URL);

							conPrintIfNotFuzzing("New Avatar creation: username: '" + temp_avatar.name + "', model_url: '" + temp_avatar.avatar_settings.model_url + "'");

							break;
						}
					case Protocol::AvatarDestroyed:
						{
							conPrintIfNotFuzzing("AvatarDestroyed");
							const UID avatar_uid = readUIDFromStream(msg_buffer);

							// Mark avatar as dead
							{
								WorldStateLock lock(world_state->mutex);
								const ServerWorldState::AvatarMapType& avatars = cur_world_state->getAvatars(lock);
								auto res = avatars.find(avatar_uid);
								if(res != avatars.end())
								{
									Avatar* avatar = res->second.getPointer();
									avatar->state = Avatar::State_Dead;
									avatar->other_dirty = true;
								}
							}
							break;
						}
					case Protocol::AvatarEnteredVehicle:
						{
							conPrintIfNotFuzzing("AvatarEnteredVehicle");

							const UID avatar_uid = readUIDFromStream(msg_buffer);
							const UID vehicle_ob_uid = readUIDFromStream(msg_buffer);
							const uint32 seat_index = msg_buffer.readUInt32();
							const uint32 flags = msg_buffer.readUInt32();

							// Mark avatar as in vehicle and execute any onUserEnteredVehicle event handlers.
							{
								WorldStateLock lock(world_state->mutex);
								const ServerWorldState::AvatarMapType& avatars = cur_world_state->getAvatars(lock);
								auto res = avatars.find(avatar_uid);
								if(res != avatars.end())
								{
									Avatar* avatar = res->second.getPointer();
									if(!avatar->vehicle_inside_uid.valid()) // If avatar wasn't in a vehicle before:
									{
										avatar->vehicle_inside_uid = vehicle_ob_uid;

										// Execute event handlers in any scripts that are listening for the onUserEnteredVehicle event from this object.
										auto ob_res = cur_world_state->getObjects(lock).find(vehicle_ob_uid); // Look up vehicle object
										if(ob_res != cur_world_state->getObjects(lock).end())
										{
											WorldObject* vehicle_ob = ob_res->second.ptr();
											if(vehicle_ob->event_handlers)
												vehicle_ob->event_handlers->executeOnUserEnteredVehicleHandlers(avatar_uid, vehicle_ob_uid, lock);
										}
									}
								}
							}
							
							// Enqueue AvatarEnteredVehicle messages to worker threads to send
							MessageUtils::initPacket(scratch_packet, Protocol::AvatarEnteredVehicle);
							writeToStream(avatar_uid, scratch_packet);
							writeToStream(vehicle_ob_uid, scratch_packet);
							scratch_packet.writeUInt32(seat_index);
							scratch_packet.writeUInt32(flags);
							MessageUtils::updatePacketLengthField(scratch_packet);
							enqueuePacketToBroadcast(scratch_packet, server);

							break;
						}
					case Protocol::AvatarExitedVehicle:
						{
							conPrintIfNotFuzzing("AvatarExitedVehicle");

							const UID avatar_uid = readUIDFromStream(msg_buffer);

							// Mark avatar as not in vehicle and execute any onUserExitedVehicle event handlers.
							{
								WorldStateLock lock(world_state->mutex);
								const ServerWorldState::AvatarMapType& avatars = cur_world_state->getAvatars(lock);
								auto res = avatars.find(avatar_uid);
								if(res != avatars.end())
								{
									Avatar* avatar = res->second.getPointer();
									if(avatar->vehicle_inside_uid.valid()) // If avatar was in a vehicle before:
									{
										// Execute event handlers in any scripts that are listening for the onUserExitedVehicle event from this object.
										auto ob_res = cur_world_state->getObjects(lock).find(avatar->vehicle_inside_uid); // Look up vehicle object
										if(ob_res != cur_world_state->getObjects(lock).end())
										{
											WorldObject* vehicle_ob = ob_res->second.ptr();
											if(vehicle_ob->event_handlers)
												vehicle_ob->event_handlers->executeOnUserExitedVehicleHandlers(avatar_uid, avatar->vehicle_inside_uid, lock);
										}

										avatar->vehicle_inside_uid = UID::invalidUID();
									}
								}
							}

							// Enqueue AvatarExitedVehicle messages to worker threads to send
							MessageUtils::initPacket(scratch_packet, Protocol::AvatarExitedVehicle);
							writeToStream(avatar_uid, scratch_packet);
							MessageUtils::updatePacketLengthField(scratch_packet);
							enqueuePacketToBroadcast(scratch_packet, server);

							break;
						}
					case Protocol::ObjectTransformUpdate:
						{
							//conPrint("received ObjectTransformUpdate");
							const UID object_uid = readUIDFromStream(msg_buffer);
							const Vec3d pos = readVec3FromStream<double>(msg_buffer);
							const Vec3f axis = readVec3FromStream<float>(msg_buffer);
							const float angle = msg_buffer.readFloat();
							const Vec3f scale = readVec3FromStream<float>(msg_buffer);

							// If client is not logged in, refuse object modification.
							if(!client_user_id.valid())
							{
								writeErrorMessageToClient(socket, "You must be logged in to modify an object.");
							}
							else if(world_state->isInReadOnlyMode())
							{
								writeErrorMessageToClient(socket, "Server is in read-only mode, you can't modify an object right now.");
							}
							else
							{
								std::string err_msg_to_client;
								// Look up existing object in world state
								{
									WorldStateLock lock(world_state->mutex);
									auto res = cur_world_state->getObjects(lock).find(object_uid);
									if(res != cur_world_state->getObjects(lock).end())
									{
										WorldObject* ob = res->second.getPointer();

										// See if the user has permissions to alter this object:
										if(!userHasObjectWritePermissions(*ob, client_user_id, client_user_name, this->connected_world_name, *cur_world_state, server->config.allow_light_mapper_bot_full_perms, lock))
											err_msg_to_client = "You must be the owner of this object to change it.";
										else
										{
											ob->pos = pos;
											ob->axis = axis;
											ob->angle = angle;
											ob->scale = scale;
											ob->last_transform_update_avatar_uid = (uint32)client_avatar_uid.value();
											ob->last_modified_time = TimeStamp::currentTime();

											ob->from_remote_transform_dirty = true;
											cur_world_state->addWorldObjectAsDBDirty(ob, lock);
											cur_world_state->getDirtyFromRemoteObjects(lock).insert(ob);

											markLODChunkAsNeedsRebuildForChangedObject(cur_world_state.ptr(), ob, lock);

											world_state->markAsChanged();
										}

										//conPrint("updated object transform");
									}
								} // End lock scope

								if(!err_msg_to_client.empty())
									writeErrorMessageToClient(socket, err_msg_to_client);
							}

							break;
						}
					case Protocol::SummonObject:
						{
							conPrint("received SummonObject");
							SummonObjectMessageClientToServer summon_msg;
							msg_buffer.readData(&summon_msg, sizeof(SummonObjectMessageClientToServer));

							// If client is not logged in, refuse object modification.
							if(!client_user_id.valid())
							{
								writeErrorMessageToClient(socket, "You must be logged in to summon an object.");
							}
							else if(world_state->isInReadOnlyMode())
							{
								writeErrorMessageToClient(socket, "Server is in read-only mode, you can't modify an object right now.");
							}
							else
							{
								std::string err_msg_to_client;
								bool send_summon_object_msg = false;
								{
									WorldStateLock lock(world_state->mutex);
									auto res = cur_world_state->getObjects(lock).find(summon_msg.object_uid); // Look up existing object in world state
									if(res != cur_world_state->getObjects(lock).end())
									{
										WorldObject* ob = res->second.getPointer();

										if(client_user_id != ob->creator_id)
											err_msg_to_client = "You must be the owner of this object to summon it.";
										else
										{
											// TODO: check that this object is the only vehicle object that can be summoned.
											if(!BitUtils::isBitSet(ob->flags, WorldObject::SUMMONED_FLAG))
												err_msg_to_client = "Object must have summoned flag set to summon it.";
											else
											{
												ob->pos   = summon_msg.pos;
												ob->axis  = summon_msg.axis;
												ob->angle = summon_msg.angle;
												ob->last_transform_update_avatar_uid = (uint32)client_avatar_uid.value();
												ob->last_modified_time = TimeStamp::currentTime();

												cur_world_state->addWorldObjectAsDBDirty(ob, lock); // Object state has changed, so save to DB.
												world_state->markAsChanged();

												send_summon_object_msg = true;
											}
										}
									}
								} // End lock scope

								if(!err_msg_to_client.empty())
									writeErrorMessageToClient(socket, err_msg_to_client);

								if(send_summon_object_msg)
								{
									// Enqueue SummonObject messages to worker threads to send
									conPrint("Broadcasting SummonObject message");
									MessageUtils::initPacket(scratch_packet, Protocol::SummonObject);
									scratch_packet.writeData(&summon_msg, sizeof(SummonObjectMessageClientToServer));
									scratch_packet.writeUInt32((uint32)client_avatar_uid.value()); // Write last_transform_update_avatar_uid
									MessageUtils::updatePacketLengthField(scratch_packet);
									enqueuePacketToBroadcast(scratch_packet, server);
								}
							}

							break;
						}
					case Protocol::ObjectPhysicsTransformUpdate:
						{
							//conPrint("received ObjectPhysicsTransformUpdate");
							const UID object_uid = readUIDFromStream(msg_buffer);
							const Vec3d pos = readVec3FromStream<double>(msg_buffer);
						
							Quatf rot;
							msg_buffer.readData(rot.v.x, sizeof(float) * 4);

							Vec4f linear_vel(0.f);
							Vec4f angular_vel(0.f);
							msg_buffer.readData(linear_vel.x, sizeof(float) * 3);
							msg_buffer.readData(angular_vel.x, sizeof(float) * 3);

							const double client_cur_time = msg_buffer.readDouble();

							// If client is not logged in, refuse object modification.
							/*if(!client_user_id.valid())
							{
								writeErrorMessageToClient(socket, "You must be logged in to modify an object.");
							}
							*/
							if(world_state->isInReadOnlyMode())
							{
								writeErrorMessageToClient(socket, "Server is in read-only mode, you can't modify an object right now.");
							}
							else
							{
								std::string err_msg_to_client;
								// Look up existing object in world state
								{
									WorldStateLock lock(world_state->mutex);
									auto res = cur_world_state->getObjects(lock).find(object_uid);
									if(res != cur_world_state->getObjects(lock).end())
									{
										WorldObject* ob = res->second.getPointer();

										// See if the user has permissions to alter this object:
										//if(!userHasObjectWritePermissions(*ob, client_user_id, client_user_name, this->connected_world_name, *cur_world_state, server->config.allow_light_mapper_bot_full_perms))
										//	err_msg_to_client = "You must be the owner of this object to change it.";
										if(ob->isDynamic()) // We will only allow clients to apply PhysicsTransformUpdates to objects it the object is a dynamic object.
										{
											ob->pos = pos;
											Vec4f axis;
											float angle;
											rot.toAxisAndAngle(axis, angle);
											ob->axis = Vec3f(axis);
											ob->angle = angle;

											ob->linear_vel = linear_vel;
											ob->angular_vel = angular_vel;

											ob->last_transform_update_avatar_uid = (uint32)client_avatar_uid.value();
											ob->last_transform_client_time = client_cur_time;

											ob->last_modified_time = TimeStamp::currentTime();

											ob->from_remote_physics_transform_dirty = true;
											cur_world_state->addWorldObjectAsDBDirty(ob, lock);
											cur_world_state->getDirtyFromRemoteObjects(lock).insert(ob);

											world_state->markAsChanged();
										}
									}
								} // End lock scope

								if(!err_msg_to_client.empty())
									writeErrorMessageToClient(socket, err_msg_to_client);
							}

							break;
						}
					case Protocol::ObjectFullUpdate:
						{
							//conPrint("received ObjectFullUpdate");
							const UID object_uid = readUIDFromStream(msg_buffer);

							WorldObject temp_ob;
							readWorldObjectFromNetworkStreamGivenUID(msg_buffer, temp_ob); // Read rest of ObjectFullUpdate message.

							// If client is not logged in, refuse object modification.
							if(!client_user_id.valid())
							{
								writeErrorMessageToClient(socket, "You must be logged in to modify an object.");
							}
							else if(world_state->isInReadOnlyMode())
							{
								writeErrorMessageToClient(socket, "Server is in read-only mode, you can't modify an object right now.");
							}
							else
							{
								// Look up existing object in world state
								bool send_must_be_owner_msg = false;
								{
									WorldStateLock lock(world_state->mutex);
									auto res = cur_world_state->getObjects(lock).find(object_uid);
									if(res != cur_world_state->getObjects(lock).end())
									{
										WorldObject* ob = res->second.getPointer();

										// See if the user has permissions to alter this object:
										if(!userHasObjectWritePermissions(*ob, client_user_id, client_user_name, this->connected_world_name, *cur_world_state, server->config.allow_light_mapper_bot_full_perms, lock))
										{
											send_must_be_owner_msg = true;
										}
										else
										{
											ob->copyNetworkStateFrom(temp_ob);
											
											// Clamp volume to the max allowed level
											ob->audio_volume = myClamp(ob->audio_volume, 0.f, maxAudioVolumeForObject(*ob, client_user_id, client_user_name, this->connected_world_name));

											ob->last_modified_time = TimeStamp::currentTime();

											ob->from_remote_other_dirty = true;
											cur_world_state->addWorldObjectAsDBDirty(ob, lock);
											cur_world_state->getDirtyFromRemoteObjects(lock).insert(ob);

											markLODChunkAsNeedsRebuildForChangedObject(cur_world_state.ptr(), ob, lock);

											world_state->markAsChanged();

											// Process resources
											std::set<DependencyURL> URLs;
											WorldObject::GetDependencyOptions options;
											ob->getDependencyURLSetBaseLevel(options, URLs);
											for(auto it = URLs.begin(); it != URLs.end(); ++it)
												sendGetFileMessageIfNeeded(it->URL);

											// Add script evaluator if needed
											if(hasPrefix(ob->script, "--lua") && BitUtils::isBitSet(world_state->feature_flag_info.feature_flags, ServerAllWorldsState::SERVER_SCRIPT_EXEC_FEATURE_FLAG))
											{
												ob->lua_script_evaluator = NULL;
												try
												{
													ob->lua_script_evaluator = new LuaScriptEvaluator(server->lua_vm.ptr(), /*script output handler=*/server, ob->script, ob, cur_world_state.ptr(), lock);
												}
												catch(LuaScriptExcepWithLocation& e)
												{
													conPrint("Error creating LuaScriptEvaluator for ob " + ob->uid.toString() + ": " + e.messageWithLocations());
													server->logLuaError("Error: " + e.messageWithLocations(), ob->uid, ob->creator_id);
												}
												catch(glare::Exception& e)
												{
													conPrint("Error creating LuaScriptEvaluator for ob " + ob->uid.toString() + ": " + e.what());
													server->logLuaError("Error: " + e.what(), ob->uid, ob->creator_id);
												}
											}
										}
									}
								} // End lock scope

								if(send_must_be_owner_msg)
									writeErrorMessageToClient(socket, "You must be the owner of this object to change it.");
							}
							break;
						}
					case Protocol::ObjectLightmapURLChanged:
						{
							//conPrint("ObjectLightmapURLChanged");
							const UID object_uid = readUIDFromStream(msg_buffer);
							const std::string new_lightmap_url = msg_buffer.readStringLengthFirst(WorldObject::MAX_URL_SIZE);

							// Look up existing object in world state
							{
								WorldStateLock lock(world_state->mutex);
								auto res = cur_world_state->getObjects(lock).find(object_uid);
								if(res != cur_world_state->getObjects(lock).end())
								{
									WorldObject* ob = res->second.getPointer();

									if(!world_state->isInReadOnlyMode())
									{
										ob->lightmap_url = new_lightmap_url;
										ob->last_modified_time = TimeStamp::currentTime();

										ob->from_remote_lightmap_url_dirty = true;
										cur_world_state->addWorldObjectAsDBDirty(ob, lock);
										cur_world_state->getDirtyFromRemoteObjects(lock).insert(ob);

										world_state->markAsChanged();
									}
								}
							}
							break;
						}
					case Protocol::ObjectModelURLChanged:
						{
							//conPrint("ObjectModelURLChanged");
							const UID object_uid = readUIDFromStream(msg_buffer);
							const std::string new_model_url = msg_buffer.readStringLengthFirst(WorldObject::MAX_URL_SIZE);

							// Look up existing object in world state
							{
								WorldStateLock lock(world_state->mutex);
								auto res = cur_world_state->getObjects(lock).find(object_uid);
								if(res != cur_world_state->getObjects(lock).end())
								{
									WorldObject* ob = res->second.getPointer();

									if(!world_state->isInReadOnlyMode())
									{
										ob->model_url = new_model_url;
										ob->last_modified_time = TimeStamp::currentTime();

										ob->from_remote_model_url_dirty = true;
										cur_world_state->addWorldObjectAsDBDirty(ob, lock);
										cur_world_state->getDirtyFromRemoteObjects(lock).insert(ob);

										markLODChunkAsNeedsRebuildForChangedObject(cur_world_state.ptr(), ob, lock);

										world_state->markAsChanged();
									}
								}
							}
							break;
						}
					case Protocol::ObjectFlagsChanged:
						{
							//conPrint("ObjectFlagsChanged");
							const UID object_uid = readUIDFromStream(msg_buffer);
							const uint32 flags = msg_buffer.readUInt32();

							// Look up existing object in world state
							{
								WorldStateLock lock(world_state->mutex);
								auto res = cur_world_state->getObjects(lock).find(object_uid);
								if(res != cur_world_state->getObjects(lock).end())
								{
									WorldObject* ob = res->second.getPointer();

									if(!world_state->isInReadOnlyMode())
									{
										ob->flags = flags; // Copy flags
										ob->last_modified_time = TimeStamp::currentTime();

										ob->from_remote_flags_dirty = true;
										cur_world_state->addWorldObjectAsDBDirty(ob, lock);
										cur_world_state->getDirtyFromRemoteObjects(lock).insert(ob);

										markLODChunkAsNeedsRebuildForChangedObject(cur_world_state.ptr(), ob, lock);

										world_state->markAsChanged();
									}
								}
							}
							break;
						}
					case Protocol::ObjectPhysicsOwnershipTaken:
						{
							// conPrint("ObjectPhysicsOwnershipTaken");
							const UID object_uid = readUIDFromStream(msg_buffer);
							const uint32 physics_owner_id = msg_buffer.readUInt32();
							const double client_global_time = msg_buffer.readDouble();
							const uint32 flags = msg_buffer.readUInt32();

							// Look up existing object in world state
							{
								WorldStateLock lock(world_state->mutex);
								auto res = cur_world_state->getObjects(lock).find(object_uid);
								if(res != cur_world_state->getObjects(lock).end())
								{
									WorldObject* ob = res->second.getPointer();

									if(!world_state->isInReadOnlyMode())
									{
										ob->physics_owner_id = physics_owner_id;
										ob->last_physics_ownership_change_global_time = client_global_time;

										// Consider physics_owner_id ephemeral state, so doesn't need to be written to DB.
									}
								}
							}

							// Enqueue ObjectPhysicsOwnershipTaken messages to worker threads to send
							MessageUtils::initPacket(scratch_packet, Protocol::ObjectPhysicsOwnershipTaken);
							writeToStream(object_uid, scratch_packet);
							scratch_packet.writeUInt32(physics_owner_id);
							scratch_packet.writeDouble(client_global_time);
							scratch_packet.writeUInt32(flags);
							MessageUtils::updatePacketLengthField(scratch_packet);
							enqueuePacketToBroadcast(scratch_packet, server);

							break;
						}
					case Protocol::CreateObject: // Client wants to create an object
						{
							conPrintIfNotFuzzing("CreateObject");

							WorldObjectRef new_ob = new WorldObject();
							new_ob->uid = readUIDFromStream(msg_buffer); // Read dummy UID
							readWorldObjectFromNetworkStreamGivenUID(msg_buffer, *new_ob);

							conPrintIfNotFuzzing("model_url: '" + new_ob->model_url + "', pos: " + new_ob->pos.toString());

							// If client is not logged in, refuse object creation.
							if(!client_user_id.valid())
							{
								conPrintIfNotFuzzing("Creation denied, user was not logged in.");
								MessageUtils::initPacket(scratch_packet, Protocol::ErrorMessageID);
								scratch_packet.writeStringLengthFirst("You must be logged in to create an object.");
								MessageUtils::updatePacketLengthField(scratch_packet);
								socket->writeData(scratch_packet.buf.data(), scratch_packet.buf.size());
								socket->flush();
							}
							else if(world_state->isInReadOnlyMode())
							{
								writeErrorMessageToClient(socket, "Server is in read-only mode, you can't create an object right now.");
							}
							else
							{
								// Check permissions
								bool have_permissions = false;
								{
									::WorldStateLock lock(world_state->mutex);
									have_permissions = userHasObjectCreationPermissions(*new_ob, client_user_id, client_user_name, this->connected_world_name, *cur_world_state, lock);
								}

								if(have_permissions)
								{
									new_ob->creator_id = client_user_id;
									new_ob->created_time = TimeStamp::currentTime();
									new_ob->last_modified_time = new_ob->created_time;
									new_ob->creator_name = client_user_name;

									std::set<DependencyURL> URLs;
									WorldObject::GetDependencyOptions options;
									new_ob->getDependencyURLSetBaseLevel(options, URLs);
									for(auto it = URLs.begin(); it != URLs.end(); ++it)
										sendGetFileMessageIfNeeded(it->URL);

									// Insert object into world state
									{
										::WorldStateLock lock(world_state->mutex);

										new_ob->uid = world_state->getNextObjectUID();
										new_ob->state = WorldObject::State_JustCreated;
										new_ob->from_remote_other_dirty = true;
										cur_world_state->addWorldObjectAsDBDirty(new_ob, lock);
										cur_world_state->getDirtyFromRemoteObjects(lock).insert(new_ob);
										cur_world_state->getObjects(lock).insert(std::make_pair(new_ob->uid, new_ob));

										markLODChunkAsNeedsRebuildForChangedObject(cur_world_state.ptr(), new_ob.ptr(), lock);

										world_state->markAsChanged();
									}
								}
								else // else if user doesn't have permissions to create objects:
								{
									writeErrorMessageToClient(socket, "You do not have the permissions to create the object with this position.");
								}
							}

							break;
						}
					case Protocol::DestroyObject: // Client wants to destroy an object.
						{
							conPrintIfNotFuzzing("DestroyObject");
							const UID object_uid = readUIDFromStream(msg_buffer);

							// If client is not logged in, refuse object modification.
							if(!client_user_id.valid())
							{
								writeErrorMessageToClient(socket, "You must be logged in to destroy an object.");
							}
							else if(world_state->isInReadOnlyMode())
							{
								writeErrorMessageToClient(socket, "Server is in read-only mode, you can't destroy an object right now.");
							}
							else
							{
								bool send_must_be_owner_msg = false;
								{
									WorldStateLock lock(world_state->mutex);
									auto res = cur_world_state->getObjects(lock).find(object_uid);
									if(res != cur_world_state->getObjects(lock).end())
									{
										WorldObject* ob = res->second.getPointer();

										// See if the user has permissions to alter this object:
										const bool have_delete_perms = userHasObjectWritePermissions(*ob, client_user_id, client_user_name, this->connected_world_name, *cur_world_state, server->config.allow_light_mapper_bot_full_perms, lock);
										if(!have_delete_perms)
											send_must_be_owner_msg = true;
										else
										{
											// Mark object as dead
											ob->state = WorldObject::State_Dead;
											ob->from_remote_other_dirty = true;
											cur_world_state->addWorldObjectAsDBDirty(ob, lock);
											cur_world_state->getDirtyFromRemoteObjects(lock).insert(ob);

											markLODChunkAsNeedsRebuildForChangedObject(cur_world_state.ptr(), ob, lock);

											world_state->markAsChanged();
										}
									}
								} // End lock scope

								if(send_must_be_owner_msg)
									writeErrorMessageToClient(socket, "You must be the owner of this object to destroy it.");
							}
							break;
						}
					case Protocol::GetAllObjects: // Client wants to get all objects in world
						{
							conPrintIfNotFuzzing("GetAllObjects");

							SocketBufferOutStream temp_buf(SocketBufferOutStream::DontUseNetworkByteOrder); // Will contain several messages

							{
								WorldStateLock lock(world_state->mutex);
								const ServerWorldState::ObjectMapType& objects = cur_world_state->getObjects(lock);
								for(auto it = objects.begin(); it != objects.end(); ++it)
								{
									const WorldObject* ob = it->second.getPointer();

									// Build ObjectInitialSend message
									MessageUtils::initPacket(scratch_packet, Protocol::ObjectInitialSend);
									ob->writeToNetworkStream(scratch_packet);
									MessageUtils::updatePacketLengthField(scratch_packet);

									temp_buf.writeData(scratch_packet.buf.data(), scratch_packet.buf.size());
								}
							}

							MessageUtils::initPacket(scratch_packet, Protocol::AllObjectsSent); // Terminate the buffer with an AllObjectsSent message.
							MessageUtils::updatePacketLengthField(scratch_packet);
							temp_buf.writeData(scratch_packet.buf.data(), scratch_packet.buf.size());

							socket->writeData(temp_buf.buf.data(), temp_buf.buf.size());
							socket->flush();

							break;
						}
					case Protocol::QueryObjects: // Client wants to query objects in certain grid cells
						{
							Vec3d cam_position;
							if(client_protocol_version >= 36) // position was introduced in protocol version 36.
								cam_position = readVec3FromStream<double>(msg_buffer);
							else
								cam_position = Vec3d(0.0);

							const uint32 num_cells = msg_buffer.readUInt32();
							if(num_cells > 100000)
								throw glare::Exception("QueryObjects: too many cells: " + toString(num_cells));

							//conPrint("QueryObjects, num_cells=" + toString(num_cells));
					
							// Read cell coords from network and make AABBs for cells
							js::Vector<js::AABBox, 16> cell_aabbs(num_cells);
							for(uint32 i=0; i<num_cells; ++i)
							{
								const int x = msg_buffer.readInt32();
								const int y = msg_buffer.readInt32();
								const int z = msg_buffer.readInt32();

								//if(i < 10)
								//	conPrint("cell " + toString(i) + " coords: " + toString(x) + ", " + toString(y) + ", " + toString(z));

								const float CELL_WIDTH = 200.f; // NOTE: has to be the same value as in gui_client/ProximityLoader.cpp.

								cell_aabbs[i] = js::AABBox(
									Vec4f(0,0,0,1) + Vec4f((float)x,     (float)y,     (float)z,     0)*CELL_WIDTH,
									Vec4f(0,0,0,1) + Vec4f((float)(x+1), (float)(y+1), (float)(z+1), 0)*CELL_WIDTH
								);
							}


							SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
							int num_obs_written = 0;

							{ // Lock scope
								WorldStateLock lock(world_state->mutex);
								const ServerWorldState::ObjectMapType& objects = cur_world_state->getObjects(lock);
								for(auto it = objects.begin(); it != objects.end(); ++it)
								{
									const WorldObject* ob = it->second.ptr();

									// See if the object is in any of the cell AABBs
									bool in_cell = false;
									for(uint32 i=0; i<num_cells; ++i)
										if(cell_aabbs[i].contains(ob->pos.toVec4fPoint()))
										{
											in_cell = true;
											break;
										}

									if(in_cell)
									{
										// Send ObjectInitialSend packet
										MessageUtils::initPacket(scratch_packet, Protocol::ObjectInitialSend);
										ob->writeToNetworkStream(scratch_packet);
										MessageUtils::updatePacketLengthField(scratch_packet);

										packet.writeData(scratch_packet.buf.data(), scratch_packet.buf.size()); 

										num_obs_written++;
									}
								}
							} // End lock scope

							if(!packet.buf.empty())
							{
								conPrintIfNotFuzzing("QueryObjects: Sending back info on " + toString(num_obs_written) + " object(s) (" + getNiceByteSize(packet.buf.size()) + ") ...");

								socket->writeData(packet.buf.data(), packet.buf.size()); // Write data to network
								socket->flush();
							}
						
							break;
						}
					case Protocol::QueryObjectsInAABB: // Client wants to query objects in a particular AABB
						{
							// This kind of query will be done when a client connects.
							// Because the AABB can be quite large (>= 1km on each side), the number of objects returned can be large.
							// Therefore we first work out the objects in the AABB, then sort by distance to camera, and send back the closer objects first.
							// This allows the client to start loading and displaying objects before all the queried objects are returned, which can take a while.
							//
							// For sending over websocket connections, we will also flush occasionally, which sends a websocket frame.
							// To do this we will record the offset of the start of chunks. (~= 4096 bytes)

							Vec3d cam_position;
							if(client_protocol_version >= 36) // position was introduced in protocol version 36.
							{
								cam_position = readVec3FromStream<double>(msg_buffer);
								if(!cam_position.isFinite())
									throw glare::Exception("Invalid cam_position");
							}
							else
								cam_position = Vec3d(0.0);

							const float lower_x = msg_buffer.readFloat();
							const float lower_y = msg_buffer.readFloat();
							const float lower_z = msg_buffer.readFloat();
							const float upper_x = msg_buffer.readFloat();
							const float upper_y = msg_buffer.readFloat();
							const float upper_z = msg_buffer.readFloat();

							const js::AABBox aabb(Vec4f(lower_x, lower_y, lower_z, 1.f), Vec4f(upper_x, upper_y, upper_z, 1.f));
					
							conPrintIfNotFuzzing("QueryObjectsInAABB, aabb: " + aabb.toStringNSigFigs(4) + ", cam_position: " + cam_position.toString());

							SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
							std::vector<size_t> chunk_begin_offsets; // Byte index of the start of a chunk (~= 4096 bytes).
							chunk_begin_offsets.reserve(512);
							chunk_begin_offsets.push_back(0);
							size_t last_chunk_begin_offset = 0;

							std::vector<const WorldObject*> obs;
							obs.reserve(16384);

							{ // Lock scope
								WorldStateLock lock(world_state->mutex);
								const ServerWorldState::ObjectMapType& objects = cur_world_state->getObjects(lock);
								for(auto it = objects.begin(); it != objects.end(); ++it)
								{
									const WorldObject* ob = it->second.ptr();
									const Vec4f ob_pos_vec4f = ob->pos.toVec4fPoint();
									if(ob_pos_vec4f.isFinite() && aabb.contains(ob_pos_vec4f)) // If the object position is valid, and if it's in the query AABB:
										obs.push_back(ob);
								}

								// Sort objects from near to far from camera.
								struct WorldObjectDistComparator
								{
									bool operator () (const WorldObject* a, const WorldObject* b)
									{
										const double a_dist2 = a->pos.getDist2(campos);
										const double b_dist2 = b->pos.getDist2(campos);
										return a_dist2 < b_dist2;
									}
									Vec3d campos;
								};

								WorldObjectDistComparator comparator;
								comparator.campos = cam_position;
								std::sort(obs.begin(), obs.end(), comparator);

								for(size_t i=0; i<obs.size(); ++i)
								{
									const WorldObject* ob = obs[i];

									// Create ObjectInitialSend packet
									MessageUtils::initPacket(scratch_packet, Protocol::ObjectInitialSend);
									ob->writeToNetworkStream(scratch_packet);
									MessageUtils::updatePacketLengthField(scratch_packet);

									packet.writeData(scratch_packet.buf.data(), scratch_packet.buf.size()); // Append scratch_packet with ObjectInitialSend message to packet.

									if(packet.buf.size() - last_chunk_begin_offset >= 4096) // If we have written more than X bytes since last chunk start:
									{
										last_chunk_begin_offset = packet.buf.size();
										chunk_begin_offsets.push_back(packet.buf.size()); // Record offset of start of chunk.
									}
								}
							} // End lock scope

							// Send back the data, now we have released the world lock.  Send it back in chunks instead of one big write. (better for websockets)
							if(!packet.buf.empty())
							{
								conPrintIfNotFuzzing("QueryObjectsInAABB: Sending back info on " + toString(obs.size()) + " object(s) (" + getNiceByteSize(packet.buf.size()) + ")...");
								Timer timer;

								for(size_t i=0; i<chunk_begin_offsets.size(); ++i)
								{
									const size_t chunk_offset = chunk_begin_offsets[i];
									if(chunk_offset < packet.buf.size())
									{
										const size_t chunk_end = ((i + 1) < chunk_begin_offsets.size()) ? chunk_begin_offsets[i + 1] : packet.buf.size();
										const size_t chunk_size = chunk_end - chunk_offset;
										runtimeCheck((chunk_offset < packet.buf.size()) && (CheckedMaths::addUnsignedInts(chunk_offset, chunk_size) <= packet.buf.size())); 
										socket->writeData(&packet.buf[chunk_offset], chunk_size); // Write data to network
										socket->flush(); // Will cause websockets to send a data frame.
									}
								}

								conPrintIfNotFuzzing("QueryObjectsInAABB: Sending back info on objects took " + timer.elapsedStringNSigFigs(4));
							}

							break;
						}
					case Protocol::QueryParcels:
						{
							conPrintIfNotFuzzing("QueryParcels");

							// Send all current parcel data to client
							MessageUtils::initPacket(scratch_packet, Protocol::ParcelList);
							{
								WorldStateLock lock(world_state->mutex);
								scratch_packet.writeUInt64(cur_world_state->getParcels(lock).size()); // Write num parcels
								for(auto it = cur_world_state->getParcels(lock).begin(); it != cur_world_state->getParcels(lock).end(); ++it)
									writeToNetworkStream(*it->second, scratch_packet, client_protocol_version); // Write parcel
							}
							MessageUtils::updatePacketLengthField(scratch_packet);
							socket->writeData(scratch_packet.buf.data(), scratch_packet.buf.size()); // Send the data
							socket->flush();
							break;
						}
					case Protocol::ParcelFullUpdate: // Client wants to update a parcel
						{
							conPrintIfNotFuzzing("ParcelFullUpdate");
							const ParcelID parcel_id = readParcelIDFromStream(msg_buffer);

							Parcel temp_parcel;
							readFromNetworkStreamGivenID(msg_buffer, temp_parcel, client_protocol_version);

							// If client is not logged in, refuse parcel modification.
							if(!client_user_id.valid())
							{
								writeErrorMessageToClient(socket, "You must be logged in to modify a parcel.");
							}
							else if(world_state->isInReadOnlyMode())
							{
								writeErrorMessageToClient(socket, "Server is in read-only mode, you can't modify a parcel right now.");
							}
							else
							{
								// Look up existing parcel in world state
								std::string error_msg;
								{
									WorldStateLock lock(world_state->mutex);
									auto res = cur_world_state->getParcels(lock).find(parcel_id);
									if(res != cur_world_state->getParcels(lock).end())
									{
										Parcel* parcel = res->second.getPointer();

										// See if the user has permissions to alter this object:
										if(!userHasParcelWritePermissions(*parcel, client_user_id, this->connected_world_name, *cur_world_state))
										{
											error_msg = "You must be the owner of this parcel (or have write permissions) to modify it";
										}
										else
										{
											parcel->copyNetworkStateFrom(temp_parcel, /*restrict_changes=*/true); // restrict changes to stuff clients are allowed to change

											//parcel->from_remote_other_dirty = true;
											cur_world_state->addParcelAsDBDirty(parcel, lock);
											//cur_world_state->dirty_from_remote_parcels.insert(ob);

											world_state->markAsChanged();
										}
									}
								} // End lock scope

								if(!error_msg.empty())
									writeErrorMessageToClient(socket, error_msg);
							}
							break;
						}
					case Protocol::QueryLODChunksMessage:
						{
							conPrintIfNotFuzzing("QueryLODChunksMessage");

							// TEMP: do nothing for now, we have already sent LODChunkInitialSend messages upon initial connection.

							// Send all current LOD chunk data to client
							//SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
							//{
							//	WorldStateLock lock(world_state->mutex);
							//	for(auto it = cur_world_state->getLODChunks(lock).begin(); it != cur_world_state->getLODChunks(lock).end(); ++it)
							//	{
							//		MessageUtils::initPacket(scratch_packet, Protocol::LODChunkInitialSend);
							//		it->second->writeToStream(scratch_packet);
							//		MessageUtils::updatePacketLengthField(scratch_packet);
							//
							//		packet.writeData(scratch_packet.buf.data(), scratch_packet.buf.size()); // Append scratch_packet with LODChunkInitialSend message to packet.
							//	}
							//}
							//socket->writeData(packet.buf.data(), packet.buf.size()); // Send the data
							//socket->flush();
							break;
						}
					case Protocol::ChatMessageID:
						{
							//const std::string name = msg_buffer.readStringLengthFirst(MAX_STRING_LEN);
							const std::string msg = msg_buffer.readStringLengthFirst(MAX_STRING_LEN);

							conPrintIfNotFuzzing("Received chat message: '" + msg + "'");

							if(!client_user_id.valid())
							{
								writeErrorMessageToClient(socket, "You must be logged in to chat.");
							}
							else
							{
								// Enqueue chat messages to worker threads to send
								// Send ChatMessageID packet
								MessageUtils::initPacket(scratch_packet, Protocol::ChatMessageID);
								scratch_packet.writeStringLengthFirst(client_user_name);
								scratch_packet.writeStringLengthFirst(msg);
								MessageUtils::updatePacketLengthField(scratch_packet);

								enqueuePacketToBroadcast(scratch_packet, server);
							}
							break;
						}
					case Protocol::UserSelectedObject:
						{
							//conPrint("Received UserSelectedObject msg.");

							const UID object_uid = readUIDFromStream(msg_buffer);

							// Send message to connected clients
							{
								MessageUtils::initPacket(scratch_packet, Protocol::UserSelectedObject);
								writeToStream(client_avatar_uid, scratch_packet);
								writeToStream(object_uid, scratch_packet);
								MessageUtils::updatePacketLengthField(scratch_packet);

								enqueuePacketToBroadcast(scratch_packet, server);
							}
							break;
						}
					case Protocol::UserDeselectedObject:
						{
							//conPrint("Received UserDeselectedObject msg.");

							const UID object_uid = readUIDFromStream(msg_buffer);

							// Send message to connected clients
							{
								MessageUtils::initPacket(scratch_packet, Protocol::UserDeselectedObject);
								writeToStream(client_avatar_uid, scratch_packet);
								writeToStream(object_uid, scratch_packet);
								MessageUtils::updatePacketLengthField(scratch_packet);

								enqueuePacketToBroadcast(scratch_packet, server);
							}
							break;
						}
					case Protocol::UserUsedObjectMessage:
						{
							conPrintIfNotFuzzing("Received UserUsedObjectMessage msg.");

							const UID object_uid = readUIDFromStream(msg_buffer);

							conPrintIfNotFuzzing("object_uid: " + object_uid.toString());

							Reference<UserUsedObjectThreadMessage> msg = new UserUsedObjectThreadMessage();
							msg->world = cur_world_state;
							msg->avatar_uid = client_avatar_uid;
							msg->object_uid = object_uid;
							server->enqueueMsg(msg);

							break;
						}
					case Protocol::UserTouchedObjectMessage:
						{
							conPrintIfNotFuzzing("Received UserTouchedObjectMessage msg.");

							const UID object_uid = readUIDFromStream(msg_buffer);

							Reference<UserTouchedObjectThreadMessage> msg = new UserTouchedObjectThreadMessage();
							msg->world = cur_world_state;
							msg->avatar_uid = client_avatar_uid;
							msg->object_uid = object_uid;
							server->enqueueMsg(msg);

							break;
						}
					case Protocol::UserMovedNearToObjectMessage:
						{
							conPrintIfNotFuzzing("Received UserMovedNearToObjectMessage msg.");

							const UID object_uid = readUIDFromStream(msg_buffer);

							Reference<UserMovedNearToObjectThreadMessage> msg = new UserMovedNearToObjectThreadMessage();
							msg->world = cur_world_state;
							msg->avatar_uid = client_avatar_uid;
							msg->object_uid = object_uid;
							server->enqueueMsg(msg);

							break;
						}
					case Protocol::UserMovedAwayFromObjectMessage:
						{
							conPrintIfNotFuzzing("Received UserMovedAwayFromObjectMessage msg.");

							const UID object_uid = readUIDFromStream(msg_buffer);

							Reference<UserMovedAwayFromObjectThreadMessage> msg = new UserMovedAwayFromObjectThreadMessage();
							msg->world = cur_world_state;
							msg->avatar_uid = client_avatar_uid;
							msg->object_uid = object_uid;
							server->enqueueMsg(msg);

							break;
						}
					case Protocol::UserEnteredParcelMessage:
						{
							conPrintIfNotFuzzing("Received UserEnteredParcelMessage msg.");

							const UID object_uid = readUIDFromStream(msg_buffer);
							const ParcelID parcel_id = readParcelIDFromStream(msg_buffer);

							Reference<UserEnteredParcelThreadMessage> msg = new UserEnteredParcelThreadMessage();
							msg->world = cur_world_state;
							msg->avatar_uid = client_avatar_uid;
							msg->client_user_id = client_user_id;
							msg->object_uid = object_uid;
							msg->parcel_id = parcel_id;
							server->enqueueMsg(msg);

							break;
						}
					case Protocol::UserExitedParcelMessage:
						{
							conPrintIfNotFuzzing("Received UserExitedParcelMessage msg.");

							const UID object_uid = readUIDFromStream(msg_buffer);
							const ParcelID parcel_id = readParcelIDFromStream(msg_buffer);

							Reference<UserExitedParcelThreadMessage> msg = new UserExitedParcelThreadMessage();
							msg->world = cur_world_state;
							msg->avatar_uid = client_avatar_uid;
							msg->object_uid = object_uid;
							msg->parcel_id = parcel_id;
							server->enqueueMsg(msg);

							break;
						}
					case Protocol::LogInMessage: // Client wants to log in.
						{
							conPrintIfNotFuzzing("LogInMessage");

							const std::string username = msg_buffer.readStringLengthFirst(MAX_STRING_LEN);
							const std::string password = msg_buffer.readStringLengthFirst(MAX_STRING_LEN);

							conPrintIfNotFuzzing("username: '" + username + "'");
						
							bool logged_in = false;
							{
								Lock lock(world_state->mutex);
								auto res = world_state->name_to_users.find(username);
								if(res != world_state->name_to_users.end())
								{
									User* user = res->second.getPointer();
									const bool password_valid = user->isPasswordValid(password);
									conPrintIfNotFuzzing("password_valid: " + boolToString(password_valid));
									if(password_valid)
									{
										// Password is valid, log user in.
										client_user_id = user->id;
										client_user_name = user->name;
										client_user_avatar_settings = user->avatar_settings;
										client_user_flags = user->flags;

										logged_in = true;
									}
								}
							}

							conPrintIfNotFuzzing("logged_in: " + boolToString(logged_in));
							if(logged_in)
							{
								if(username == "lightmapperbot")
									logged_in_user_is_lightmapper_bot = true;

								// Send logged-in message to client
								MessageUtils::initPacket(scratch_packet, Protocol::LoggedInMessageID);
								writeToStream(client_user_id, scratch_packet);
								scratch_packet.writeStringLengthFirst(username);
								writeAvatarSettingsToStream(client_user_avatar_settings, scratch_packet);
								scratch_packet.writeUInt32(client_user_flags);
								MessageUtils::updatePacketLengthField(scratch_packet);

								socket->writeData(scratch_packet.buf.data(), scratch_packet.buf.size());
								socket->flush();
							}
							else
							{
								// Login failed.  Send error message back to client
								MessageUtils::initPacket(scratch_packet, Protocol::ErrorMessageID);
								scratch_packet.writeStringLengthFirst("Login failed: username or password incorrect.");
								MessageUtils::updatePacketLengthField(scratch_packet);

								socket->writeData(scratch_packet.buf.data(), scratch_packet.buf.size());
								socket->flush();
							}
					
							break;
						}
					case Protocol::LogOutMessage: // Client wants to log out.
						{
							conPrintIfNotFuzzing("LogOutMessage");

							client_user_id = UserID::invalidUserID(); // Mark the client as not logged in.
							client_user_name = "";
							client_user_flags = 0;

							// Send logged-out message to client
							MessageUtils::initPacket(scratch_packet, Protocol::LoggedOutMessageID);
							MessageUtils::updatePacketLengthField(scratch_packet);

							socket->writeData(scratch_packet.buf.data(), scratch_packet.buf.size());
							socket->flush();
							break;
						}
					case Protocol::SignUpMessage:
						{
							conPrintIfNotFuzzing("SignUpMessage");

							const std::string username = msg_buffer.readStringLengthFirst(MAX_STRING_LEN);
							const std::string email    = msg_buffer.readStringLengthFirst(MAX_STRING_LEN);
							const std::string password = msg_buffer.readStringLengthFirst(MAX_STRING_LEN);

							try
							{
								conPrintIfNotFuzzing("username: '" + username + "', email: '" + email + "'");

								bool signed_up = false;

								std::string msg_to_client;
								if(world_state->isInReadOnlyMode())
								{
									msg_to_client = "Server is in read-only mode, you can't sign up right now.";
								}
								else
								{
									if(username.size() < 3)
										msg_to_client = "Username is too short, must have at least 3 characters";
									else
									{
										if(password.size() < 6)
											msg_to_client = "Password is too short, must have at least 6 characters";
										else
										{
											Lock lock(world_state->mutex);
											auto res = world_state->name_to_users.find(username);
											if(res == world_state->name_to_users.end())
											{
												Reference<User> new_user = new User();
												new_user->id = UserID((uint32)world_state->name_to_users.size());
												new_user->created_time = TimeStamp::currentTime();
												new_user->name = username;
												new_user->email_address = email;

												new_user->setNewPasswordAndSalt(password);

												world_state->addUserAsDBDirty(new_user);

												// Add new user to world state
												world_state->user_id_to_users.insert(std::make_pair(new_user->id, new_user));
												world_state->name_to_users   .insert(std::make_pair(username,     new_user));
												world_state->markAsChanged(); // Mark as changed so gets saved to disk.

												client_user_id = new_user->id; // Log user in as well.
												client_user_name = new_user->name;
												client_user_avatar_settings = new_user->avatar_settings;
												client_user_flags = new_user->flags;

												signed_up = true;
											}
										}
									}
								}

								conPrintIfNotFuzzing("signed_up: " + boolToString(signed_up));
								if(signed_up)
								{
									conPrintIfNotFuzzing("Sign up successful");
									// Send signed-up message to client
									MessageUtils::initPacket(scratch_packet, Protocol::SignedUpMessageID);
									writeToStream(client_user_id, scratch_packet);
									scratch_packet.writeStringLengthFirst(username);
									MessageUtils::updatePacketLengthField(scratch_packet);

									socket->writeData(scratch_packet.buf.data(), scratch_packet.buf.size());
									socket->flush();
								}
								else
								{
									conPrintIfNotFuzzing("Sign up failed.");

									// signup failed.  Send error message back to client
									MessageUtils::initPacket(scratch_packet, Protocol::ErrorMessageID);
									scratch_packet.writeStringLengthFirst(msg_to_client);
									MessageUtils::updatePacketLengthField(scratch_packet);

									socket->writeData(scratch_packet.buf.data(), scratch_packet.buf.size());
									socket->flush();
								}
							}
							catch(glare::Exception& e)
							{
								conPrint("Sign up failed, internal error: " + e.what());

								// signup failed.  Send error message back to client
								MessageUtils::initPacket(scratch_packet, Protocol::ErrorMessageID);
								scratch_packet.writeStringLengthFirst("Signup failed: internal error.");
								MessageUtils::updatePacketLengthField(scratch_packet);

								socket->writeData(scratch_packet.buf.data(), scratch_packet.buf.size());
								socket->flush();
							}

							break;
						}
					case Protocol::RequestPasswordReset:
						{
							conPrintIfNotFuzzing("RequestPasswordReset");

							const std::string email    = msg_buffer.readStringLengthFirst(MAX_STRING_LEN);

							// NOTE: This stuff is done via the website now instead.

							//conPrint("email: " + email);
							//
							//// TEMP: Send password reset email in this thread for now. 
							//// TODO: move to another thread (make some kind of background task?)
							//{
							//	Lock lock(world_state->mutex);
							//	for(auto it = world_state->user_id_to_users.begin(); it != world_state->user_id_to_users.end(); ++it)
							//		if(it->second->email_address == email)
							//		{
							//			User* user = it->second.getPointer();
							//			try
							//			{
							//				user->sendPasswordResetEmail();
							//				world_state->markAsChanged(); // Mark as changed so gets saved to disk.
							//				conPrint("Sent user password reset email to '" + email + ", username '" + user->name + "'");
							//			}
							//			catch(glare::Exception& e)
							//			{
							//				conPrint("Sending password reset email failed: " + e.what());
							//			}
							//		}
							//}
					
							break;
						}
					case Protocol::ChangePasswordWithResetToken:
						{
							conPrintIfNotFuzzing("ChangePasswordWithResetToken");
						
							const std::string email			= msg_buffer.readStringLengthFirst(MAX_STRING_LEN);
							const std::string reset_token	= msg_buffer.readStringLengthFirst(MAX_STRING_LEN);
							const std::string new_password	= msg_buffer.readStringLengthFirst(MAX_STRING_LEN);

							// NOTE: This stuff is done via the website now instead.
					
							//conPrint("email: " + email);
							//conPrint("reset_token: " + reset_token);
							////conPrint("new_password: " + new_password);
							//
							//{
							//	Lock lock(world_state->mutex);
							//
							//	// Find user with the given email address:
							//	for(auto it = world_state->user_id_to_users.begin(); it != world_state->user_id_to_users.end(); ++it)
							//		if(it->second->email_address == email)
							//		{
							//			User* user = it->second.getPointer();
							//			const bool reset = user->resetPasswordWithToken(reset_token, new_password);
							//			if(reset)
							//			{
							//				world_state->markAsChanged(); // Mark as changed so gets saved to disk.
							//				conPrint("User password successfully updated.");
							//			}
							//		}
							//}

							break;
						}
					case Protocol::WorldSettingsUpdate:
						{
							conPrintIfNotFuzzing("WorldSettingsUpdate");
						
							WorldSettings world_settings;
							readWorldSettingsFromStream(msg_buffer, world_settings);

							if(userConnectedToTheirPersonalWorldOrGodUser(client_user_id, client_user_name, this->connected_world_name))
							{
								{
									Lock lock(server->world_state->mutex);
									cur_world_state->world_settings.copyNetworkStateFrom(world_settings);
									cur_world_state->world_settings.db_dirty = true;
									world_state->markAsChanged();
								}

								// Process resources
								std::set<DependencyURL> URLs;
								world_settings.getDependencyURLSet(URLs);
								for(auto it = URLs.begin(); it != URLs.end(); ++it)
									sendGetFileMessageIfNeeded(it->URL);

								conPrintIfNotFuzzing("WorkerThread: Updated world settings.");

								// Send WorldSettingsUpdate message to all connected clients
								{
									MessageUtils::initPacket(scratch_packet, Protocol::WorldSettingsUpdate);
									world_settings.writeToStream(scratch_packet);
									MessageUtils::updatePacketLengthField(scratch_packet);

									enqueuePacketToBroadcast(scratch_packet, server);
								}
							}
							else
							{
								conPrintIfNotFuzzing("Client does not have pemissions to set world settings.");

								// Send error message back to client
								MessageUtils::initPacket(scratch_packet, Protocol::ErrorMessageID);
								scratch_packet.writeStringLengthFirst("You do not have permissions to set the world settings");
								MessageUtils::updatePacketLengthField(scratch_packet);

								socket->writeData(scratch_packet.buf.data(), scratch_packet.buf.size());
								socket->flush();
							}

							break;
						}
					case Protocol::QueryMapTiles:
						{
							conPrintIfNotFuzzing("QueryMapTiles");
						
							const uint32 num_tiles = msg_buffer.readUInt32();
							if(num_tiles > 1000)
								throw glare::Exception("QueryMapTiles: too many tiles: " + toString(num_tiles));

							// conPrint("QueryMapTiles, num_tiles=" + toString(num_tiles));
					
							// Read tile coords
							std::vector<Vec3i> tile_coords(num_tiles);
							msg_buffer.readData(tile_coords.data(), num_tiles * sizeof(Vec3i));

							std::vector<std::string> result_URLs(num_tiles);
							{
								Lock lock(world_state->mutex);

								for(size_t i=0; i<tile_coords.size(); ++i)
								{
									auto res = world_state->map_tile_info.info.find(tile_coords[i]);
									if(res != world_state->map_tile_info.info.end())
									{
										const TileInfo& tile_info = res->second;
										if(tile_info.cur_tile_screenshot.nonNull())
										{
											result_URLs[i] = tile_info.cur_tile_screenshot->URL;
										}
										else if(tile_info.prev_tile_screenshot.nonNull())
										{
											result_URLs[i] = tile_info.prev_tile_screenshot->URL;
										}

										// conPrint("QueryMapTiles: Found result_URLs[i]: " + result_URLs[i]);
									}
								}
							}

							// Send result URLs back
							MessageUtils::initPacket(scratch_packet, Protocol::MapTilesResult);
							scratch_packet.writeUInt32(num_tiles);

							// Write tile coords
							scratch_packet.writeData(tile_coords.data(), tile_coords.size() * sizeof(Vec3i));

							// Write URLS
							for(size_t i=0; i<result_URLs.size(); ++i)
								scratch_packet.writeStringLengthFirst(result_URLs[i]);

							MessageUtils::updatePacketLengthField(scratch_packet);

							socket->writeData(scratch_packet.buf.data(), scratch_packet.buf.size());
							socket->flush();

							break;
						}
					default:
						{
							//conPrint("Unknown message id: " + toString(msg_type));
							throw glare::Exception("Unknown message id: " + toString(msg_type));
						}
					}
				}
				else
				{
#if defined(_WIN32) || defined(OSX)
#else
					if(VERBOSE) conPrint("WorkerThread: event FD was signalled.");

					// The event FD was signalled, which means there is some data to send on the socket.
					// Reset the event fd by reading from it.
					event_fd.read();

					if(VERBOSE) conPrint("WorkerThread: event FD has been reset.");
#endif
				}
			} // End write to / read from socket loop
		} // End if(connection_type == Protocol::ConnectionTypeUpdates)
		else
		{
			throw glare::Exception("Unknown connection_type: " + toString(connection_type));
		}
	}
	catch(MySocketExcep& e)
	{
		if(e.excepType() == MySocketExcep::ExcepType_ConnectionClosedGracefully)
			conPrint("Updates client from " + IPAddress::formatIPAddressAndPort(socket->getOtherEndIPAddress(), socket->getOtherEndPort()) + " closed connection gracefully.");
		else
			conPrint("Socket error: " + e.what());
	}
	catch(glare::Exception& e)
	{
		conPrintIfNotFuzzing("glare::Exception: " + e.what());
	}
	catch(std::bad_alloc&)
	{
		conPrint("WorkerThread: Caught std::bad_alloc.");
	}


	if(write_trace)
		socket.downcastToPtr<RecordingSocket>()->writeRecordBufToDisk("traces/worker_thread_trace_" + ::toString(Clock::getTimeSinceInit()) + ".bin");

	server->clientDisconnected(this);
	
	// Mark avatar corresponding to client as dead.  Note that we want to do this after catching any exceptions, so avatar is removed on broken connections etc.
	if(cur_world_state.nonNull())
	{
		WorldStateLock lock(world_state->mutex);
		ServerWorldState::AvatarMapType& avatars = cur_world_state->getAvatars(lock);
		if(avatars.count(client_avatar_uid) == 1)
		{
			avatars[client_avatar_uid]->state = Avatar::State_Dead;
			avatars[client_avatar_uid]->other_dirty = true;
		}
	}

	// Remove thread-local OpenSSL error state, to avoid leaking it.
	// NOTE: have to destroy socket first, before calling ERR_remove_thread_state(), otherwise memory will just be reallocated.
	if(socket.nonNull())
	{
		assert(socket->getRefCount() == 1);
	}
	socket = NULL;
	ERR_remove_thread_state(/*thread id=*/NULL); // Set thread ID to null to use current thread.
}


void WorkerThread::kill()
{
	should_quit = true;

	Reference<SocketInterface> socket_ = socket;
	if(socket_)
		socket_->ungracefulShutdown();
}


void WorkerThread::enqueueDataToSend(const std::string& data)
{
	if(VERBOSE) conPrint("WorkerThread::enqueueDataToSend(), data: '" + data + "'");

	// Append data to data_to_send
	if(!data.empty())
	{
		Lock lock(data_to_send_mutex);
		const size_t write_i = data_to_send.size();
		data_to_send.resize(write_i + data.size());
		std::memcpy(&data_to_send[write_i], data.data(), data.size());
	}

	event_fd.notify();
}


void WorkerThread::enqueueDataToSend(const SocketBufferOutStream& packet) // threadsafe
{
	// Append data to data_to_send
	if(!packet.buf.empty())
	{
		Lock lock(data_to_send_mutex);
		const size_t write_i = data_to_send.size();
		data_to_send.resize(write_i + packet.buf.size());
		std::memcpy(&data_to_send[write_i], packet.buf.data(), packet.buf.size());
	}

	event_fd.notify();
}


void WorkerThread::conPrintIfNotFuzzing(const std::string& msg)
{
	if(!fuzzing)
		conPrint(msg);
}
