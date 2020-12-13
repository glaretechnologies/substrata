/*=====================================================================
WorkerThread.cpp
------------------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#include "WorkerThread.h"

#include "ServerWorldState.h"
#include "Server.h"
#include "../shared/Protocol.h"
#include "../shared/UID.h"
#include "../shared/WorldObject.h"
#include <vec3.h>
#include <ConPrint.h>
#include <Clock.h>
#include <AESEncryption.h>
#include <SHA256.h>
#include <Base64.h>
#include <Exception.h>
#include <mysocket.h>
#include <url.h>
#include <Lock.h>
#include <StringUtils.h>
#include <SocketBufferOutStream.h>
#include <PlatformUtils.h>
#include <KillThreadMessage.h>
#include <ThreadShouldAbortCallback.h>
#include <Parser.h>
#include <FileUtils.h>
#include <MemMappedFile.h>


static const bool VERBOSE = false;
static const int MAX_STRING_LEN = 10000;


WorkerThread::WorkerThread(int thread_id_, const Reference<MySocket>& socket_, Server* server_)
:	socket(socket_),
	server(server_)
{
	//if(VERBOSE) print("event_fd.efd: " + toString(event_fd.efd));
}


WorkerThread::~WorkerThread()
{
}


void WorkerThread::sendGetFileMessageIfNeeded(const std::string& resource_URL)
{
	if(!ResourceManager::isValidURL(resource_URL))
		throw Indigo::Exception("Invalid URL: '" + resource_URL + "'");

	try
	{
		URL parsed_url = URL::parseURL(resource_URL);

		// If this is a web URL, then we don't need to get it from the client.
		if(parsed_url.scheme == "http" || parsed_url.scheme == "https")
			return;
	}
	catch(Indigo::Exception&)
	{}

	// See if we have this file on the server already
	{
		const std::string path = server->world_state->resource_manager->pathForURL(resource_URL);
		if(FileUtils::fileExists(path))
		{
			// Check hash?
			conPrint("resource file with URL '" + resource_URL + "' already present on disk.");
		}
		else
		{
			conPrint("resource file with URL '" + resource_URL + "' not present on disk, sending get file message to client.");

			// We need the file from the client.
			// Send the client a 'get file' message
			SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
			packet.writeUInt32(Protocol::GetFile);
			packet.writeStringLengthFirst(resource_URL);

			std::string packet_string(packet.buf.size(), '\0');
			std::memcpy(&packet_string[0], packet.buf.data(), packet.buf.size());

			this->enqueueDataToSend(packet_string);
		}
	}
}


static void writeErrorMessageToClient(MySocketRef& socket, const std::string& msg)
{
	SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
	packet.writeUInt32(Protocol::ErrorMessageID);
	packet.writeStringLengthFirst(msg);
	socket->writeData(packet.buf.data(), packet.buf.size());
}


// Enqueues packet to WorkerThreads to send to clients connected to the server.
static void enqueuePacketToBroadcast(SocketBufferOutStream& packet_buffer, Server* server)
{
	assert(packet_buffer.buf.size() > 0);
	if(packet_buffer.buf.size() > 0)
	{
		std::string packet_string(packet_buffer.buf.size(), '\0');
		std::memcpy(&packet_string[0], packet_buffer.buf.data(), packet_buffer.buf.size());

		Lock lock(server->worker_thread_manager.getMutex());
		for(auto i = server->worker_thread_manager.getThreads().begin(); i != server->worker_thread_manager.getThreads().end(); ++i)
		{
			assert(dynamic_cast<WorkerThread*>(i->getPointer()));
			static_cast<WorkerThread*>(i->getPointer())->enqueueDataToSend(packet_string);
		}
	}
}


void WorkerThread::handleResourceUploadConnection()
{
	conPrint("handleResourceUploadConnection()");

	try
	{

		const std::string username = socket->readStringLengthFirst(MAX_STRING_LEN);
		const std::string password = socket->readStringLengthFirst(MAX_STRING_LEN);

		conPrint("\tusername: '" + username + "'");

		UserRef client_user;
		{
			Lock lock(server->world_state->mutex);
			auto res = server->world_state->name_to_users.find(username);
			if(res != server->world_state->name_to_users.end())
			{
				User* user = res->second.getPointer();
				if(user->isPasswordValid(password))
					client_user = user; // Password is valid, log user in.
			}
		}

		if(client_user.isNull())
		{
			conPrint("\tLogin failed.");
			socket->writeUInt32(Protocol::LogInFailure);
			socket->writeStringLengthFirst("Login failed.");
			return;
		}


		const std::string URL = socket->readStringLengthFirst(MAX_STRING_LEN);

		conPrint("\tURL: '" + URL + "'");

		/*if(!ResourceManager::isValidURL(URL))
		{
		conPrint("Invalid URL '" + URL + "'");
		throw Indigo::Exception("Invalid URL '" + URL + "'");
		}*/

		// See if we have a resource in the ResourceManager already
		ResourceRef resource = server->world_state->resource_manager->getResourceForURL(URL); // Will create a new Resource ob if not already inserted.
		if(resource->owner_id == UserID::invalidUserID())
		{
			// No such resource existed before, client may create this resource.
		}
		else // else if resource already existed:
		{
			if(resource->owner_id != client_user->id) // If this resource already exists and was created by someone else:
			{
				socket->writeUInt32(Protocol::NoWritePermissions);
				socket->writeStringLengthFirst("Not allowed to upload resource to URL '" + URL + ", someone else created a resource at this URL already.");
				return;
			}
		}
		
		// resource->setState(Resource::State_Transferring); // Don't set this (for now) or we will have to handle changing it on exceptions below.


		const uint64 file_len = socket->readUInt64();
		conPrint("\tfile_len: " + toString(file_len) + " B");
		if(file_len == 0)
		{
			socket->writeUInt32(Protocol::InvalidFileSize);
			socket->writeStringLengthFirst("Invalid file len of zero.");
			return;
		}

		// TODO: cap length in a better way
		if(file_len > 1000000000)
		{
			socket->writeUInt32(Protocol::InvalidFileSize);
			socket->writeStringLengthFirst("uploaded file too large.");
			return;
		}

		// Otherwise upload is allowed:
		socket->writeUInt32(Protocol::UploadAllowed);

		std::vector<uint8> buf(file_len);
		socket->readData(buf.data(), file_len);

		conPrint("\tReceived file with URL '" + URL + "' from client. (" + toString(file_len) + " B)");

		// Save to disk
		const std::string local_path = server->world_state->resource_manager->pathForURL(URL);

		conPrint("\tWriting to disk at '" + local_path + "'...");

		FileUtils::writeEntireFile(local_path, (const char*)buf.data(), buf.size());

		conPrint("\tWritten to disk.");

		resource->owner_id = client_user->id;
		resource->setState(Resource::State_Present);
		server->world_state->markAsChanged();


		// Send NewResourceOnServer message to connected clients
		{
			SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
			packet.writeUInt32(Protocol::NewResourceOnServer);
			packet.writeStringLengthFirst(URL);

			enqueuePacketToBroadcast(packet, server);
		}

		// Connection will be closed by the client after the client has uploaded the file.  Wait for the connection to close.
		//socket->waitForGracefulDisconnect();
	}
	catch(MySocketExcep& e)
	{
		if(e.excepType() == MySocketExcep::ExcepType_ConnectionClosedGracefully)
			conPrint("Resource upload client from " + IPAddress::formatIPAddressAndPort(socket->getOtherEndIPAddress(), socket->getOtherEndPort()) + " closed connection gracefully.");
		else
			conPrint("Socket error: " + e.what());
	}
	catch(Indigo::Exception& e)
	{
		conPrint("Indigo::Exception: " + e.what());
	}
	catch(FileUtils::FileUtilsExcep& e)
	{
		conPrint("FileUtils::FileUtilsExcep: " + e.what());
	}
	catch(std::bad_alloc&)
	{
		conPrint("WorkerThread: Caught std::bad_alloc.");
	}
}


void WorkerThread::handleResourceDownloadConnection()
{
	conPrint("handleResourceDownloadConnection()");

	try
	{

		while(1)
		{
			const uint32 msg_type = socket->readUInt32();
			if(msg_type == Protocol::GetFiles)
			{
				conPrint("------GetFiles-----");

				const uint64 num_resources = socket->readUInt64();
				conPrint("\tnum_resources requested: " + toString(num_resources));

				for(size_t i=0; i<num_resources; ++i)
				{
					const std::string URL = socket->readStringLengthFirst(MAX_STRING_LEN);

					conPrint("\tRequested URL: '" + URL + "'");

					if(!ResourceManager::isValidURL(URL))
					{
						conPrint("\tRequested URL was invalid.");
						socket->writeUInt32(1); // write error msg to client
					}
					else
					{
						conPrint("\tRequested URL was valid.");

						const ResourceRef resource = server->world_state->resource_manager->getResourceForURL(URL);
						if(resource->getState() != Resource::State_Present)
						{
							conPrint("\tRequested URL was not present on disk.");
							socket->writeUInt32(1); // write error msg to client
						}
						else
						{
							const std::string local_path = resource->getLocalPath();

							conPrint("\tlocal path: '" + local_path + "'");

							try
							{
								// Load resource off disk
								MemMappedFile file(local_path);
								conPrint("\tSending file to client.");
								socket->writeUInt32(0); // write OK msg to client
								socket->writeUInt64(file.fileSize()); // Write file size
								socket->writeData(file.fileData(), file.fileSize()); // Write file data

								conPrint("\tSent file '" + local_path + "' to client. (" + toString(file.fileSize()) + " B)");
							}
							catch(Indigo::Exception& e)
							{
								conPrint("\tException while trying to load file for URL: " + e.what());

								socket->writeUInt32(1); // write error msg to client
							}
						}
					}
				}
			}
			else
			{
				conPrint("handleResourceDownloadConnection(): Unhandled msg type: " + toString(msg_type));
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
	catch(Indigo::Exception& e)
	{
		conPrint("Indigo::Exception: " + e.what());
	}
	catch(FileUtils::FileUtilsExcep& e)
	{
		conPrint("FileUtils::FileUtilsExcep: " + e.what());
	}
	catch(std::bad_alloc&)
	{
		conPrint("WorkerThread: Caught std::bad_alloc.");
	}
}


static bool userHasObjectWritePermissions(const WorldObject& ob, const User& user, const std::string& connected_world_name)
{
	if(user.id.valid())
	{
		return (user.id == ob.creator_id) || // If the user created/owns the object
			isGodUser(user.id) || // or if the user is the god user (id 0)
			user.name == "lightmapperbot" || // lightmapper bot has full write permissions for now.
			((connected_world_name != "") && (user.name == connected_world_name)); // or if this is the user's personal world
	}
	else
		return false;
}


void WorkerThread::doRun()
{
	PlatformUtils::setCurrentThreadNameIfTestsEnabled("WorkerThread");

	ServerAllWorldsState* world_state = server->world_state.getPointer();

	UID client_avatar_uid(0);
	Reference<User> client_user; // Will be a null reference if client is not logged in, otherwise will refer to the user account the client is logged in to.
	Reference<ServerWorldState> cur_world_state; // World the client is connected to.

	try
	{
		// Read hello bytes
		const uint32 hello = socket->readUInt32();
		printVar(hello);
		if(hello != Protocol::CyberspaceHello)
			throw Indigo::Exception("Received invalid hello message (" + toString(hello) + ") from client.");
		
		// Write hello response
		socket->writeUInt32(Protocol::CyberspaceHello);

		// Read protocol version
		const uint32 client_version = socket->readUInt32();
		printVar(client_version);
		if(client_version < Protocol::CyberspaceProtocolVersion)
		{
			socket->writeUInt32(Protocol::ClientProtocolTooOld);
			socket->writeStringLengthFirst("Sorry, your client protocol version (" + toString(client_version) + ") is too old, require version " + 
				toString(Protocol::CyberspaceProtocolVersion) + ".  Please update your client.");
		}
		else if(client_version > Protocol::CyberspaceProtocolVersion)
		{
			socket->writeUInt32(Protocol::ClientProtocolTooNew);
			socket->writeStringLengthFirst("Sorry, your client protocol version (" + toString(client_version) + ") is too new, require version " + 
				toString(Protocol::CyberspaceProtocolVersion) + ".  Please use an older client.");
		}
		else
		{
			socket->writeUInt32(Protocol::ClientProtocolOK);
		}

		const uint32 connection_type = socket->readUInt32();
	
		if(connection_type == Protocol::ConnectionTypeUploadResource)
		{
			handleResourceUploadConnection();
			return;
		}
		else if(connection_type == Protocol::ConnectionTypeDownloadResources)
		{
			handleResourceDownloadConnection();
			return;
		}
		else if(connection_type == Protocol::ConnectionTypeUpdates)
		{
			// Read name of world to connect to
			const std::string world_name = socket->readStringLengthFirst(1000);
			this->connected_world_name = world_name;

			{
				Lock lock(world_state->mutex);
				// Create world if didn't exist before.
				// TODO: do this here? or restrict possible world names to those of users etc..?
				if(world_state->world_states[world_name].isNull())
					world_state->world_states[world_name] = new ServerWorldState();
				cur_world_state = world_state->world_states[world_name];
			}

			// Write avatar UID assigned to the connected client.
			client_avatar_uid = world_state->getNextAvatarUID();
			writeToStream(client_avatar_uid, *socket);

			// Send TimeSyncMessage packet to client
			{
				SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
				packet.writeUInt32(Protocol::TimeSyncMessage);
				packet.writeDouble(server->getCurrentGlobalTime());
				socket->writeData(packet.buf.data(), packet.buf.size());
			}

			// Send all current avatar state data to client
			{
				Lock lock(world_state->mutex);
				for(auto it = cur_world_state->avatars.begin(); it != cur_world_state->avatars.end(); ++it)
				{
					const Avatar* avatar = it->second.getPointer();

					// Send AvatarCreated packet
					SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
					packet.writeUInt32(Protocol::AvatarCreated);
					writeToStream(avatar->uid, packet);
					packet.writeStringLengthFirst(avatar->name);
					packet.writeStringLengthFirst(avatar->model_url);
					writeToStream(avatar->pos, packet);
					writeToStream(avatar->rotation, packet);

					socket->writeData(packet.buf.data(), packet.buf.size());
				}
			}

			// Send all current object data to client
			{
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
			}

			// Send all current parcel data to client
			{
				Lock lock(world_state->mutex);
				for(auto it = cur_world_state->parcels.begin(); it != cur_world_state->parcels.end(); ++it)
				{
					const Parcel* parcel = it->second.getPointer();

					// Send ParcelCreated packet
					SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
					packet.writeUInt32(Protocol::ParcelCreated);
					writeToNetworkStream(*parcel, packet);
					socket->writeData(packet.buf.data(), packet.buf.size());
				}
			}

			// Send a message saying we have sent all initial state
			{
				SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
				packet.writeUInt32(Protocol::InitialStateSent);
				socket->writeData(packet.buf.data(), packet.buf.size());
			}
		}

		assert(cur_world_state.nonNull());


		socket->setNoDelayEnabled(true); // We want to send out lots of little packets with low latency.  So disable Nagle's algorithm, e.g. send coalescing.

		
		while(1) // write to / read from socket loop
		{
			// See if we have any pending data to send in the data_to_send queue, and if so, send all pending data.
			if(VERBOSE) conPrint("WorkerThread: checking for pending data to send...");
			{
				Lock lock(data_to_send.getMutex());

				while(!data_to_send.unlockedEmpty())
				{
					std::string data;
					data_to_send.unlockedDequeue(data);

					if(connection_type == Protocol::ConnectionTypeUpdates)
					{
						// Write the data to the socket
						if(!data.empty())
						{
							if(VERBOSE) conPrint("WorkerThread: calling writeWebsocketTextMessage() with data '" + data + "'...");
							socket->writeData(data.data(), data.size());
						}
					}
				}
			}


#if defined(_WIN32) || defined(OSX)
			if(socket->readable(0.05)) // If socket has some data to read from it:
#else
			if(socket->readable(event_fd)) // Block until either the socket is readable or the event fd is signalled, which means we have data to write.
#endif
			{
				// Read msg type
				const uint32 msg_type = socket->readUInt32();
				switch(msg_type)
				{
				case Protocol::AvatarTransformUpdate:
					{
						//conPrint("AvatarTransformUpdate");
						const UID avatar_uid = readUIDFromStream(*socket);
						const Vec3d pos = readVec3FromStream<double>(*socket);
						const Vec3f rotation = readVec3FromStream<float>(*socket);

						// Look up existing avatar in world state
						{
							Lock lock(world_state->mutex);
							auto res = cur_world_state->avatars.find(avatar_uid);
							if(res != cur_world_state->avatars.end())
							{
								Avatar* avatar = res->second.getPointer();
								avatar->pos = pos;
								avatar->rotation = rotation;
								avatar->transform_dirty = true;

								//conPrint("updated avatar transform");
							}
						}
						break;
					}
				case Protocol::AvatarFullUpdate:
					{
						conPrint("AvatarFullUpdate");
						const UID avatar_uid = readUIDFromStream(*socket);

						// Look up existing avatar in world state
						{
							Lock lock(world_state->mutex);
							auto res = cur_world_state->avatars.find(avatar_uid);
							if(res != cur_world_state->avatars.end())
							{
								Avatar* avatar = res->second.getPointer();
								readFromNetworkStreamGivenUID(*socket, *avatar);
								avatar->other_dirty = true;

								//conPrint("updated avatar transform");
							}
							// TODO: read data even if no such avatar inserted.
						}
						break;
					}
				case Protocol::CreateAvatar:
					{
						conPrint("CreateAvatar");
						// Note: not reading name, name will come from user account
						// Also not reading UID, will use the client_avatar_uid that we assigned to the client
						const std::string model_url = socket->readStringLengthFirst(MAX_STRING_LEN);
						const Vec3d pos = readVec3FromStream<double>(*socket);
						const Vec3f rotation = readVec3FromStream<float>(*socket);

						const std::string use_avatar_name = client_user.isNull() ? "Anonymous" : client_user->name;

						const UID use_avatar_uid = client_avatar_uid;

						// Look up existing avatar in world state
						{
							Lock lock(world_state->mutex);
							auto res = cur_world_state->avatars.find(use_avatar_uid);
							if(res == cur_world_state->avatars.end())
							{
								// Avatar for UID not already created, create it now.
								AvatarRef avatar = new Avatar();
								avatar->uid = use_avatar_uid;
								avatar->name = use_avatar_name;
								avatar->model_url = model_url;
								avatar->pos = pos;
								avatar->rotation = rotation;
								avatar->state = Avatar::State_JustCreated;
								avatar->other_dirty = true;
								cur_world_state->avatars.insert(std::make_pair(use_avatar_uid, avatar));

								conPrint("created new avatar");
							}
						}

						sendGetFileMessageIfNeeded(model_url);

						conPrint("New Avatar creation: username: '" + use_avatar_name + "', model_url: '" + model_url + "'");

						break;
					}
				case Protocol::AvatarDestroyed:
					{
						conPrint("AvatarDestroyed");
						const UID avatar_uid = readUIDFromStream(*socket);

						// Mark avatar as dead
						{
							Lock lock(world_state->mutex);
							auto res = cur_world_state->avatars.find(avatar_uid);
							if(res != cur_world_state->avatars.end())
							{
								Avatar* avatar = res->second.getPointer();
								avatar->state = Avatar::State_Dead;
								avatar->other_dirty = true;
							}
						}
						break;
					}
				case Protocol::ObjectTransformUpdate:
					{
						//conPrint("ObjectTransformUpdate");
						const UID object_uid = readUIDFromStream(*socket);
						const Vec3d pos = readVec3FromStream<double>(*socket);
						const Vec3f axis = readVec3FromStream<float>(*socket);
						const float angle = socket->readFloat();

						// If client is not logged in, refuse object modification.
						if(client_user.isNull())
						{
							writeErrorMessageToClient(socket, "You must be logged in to modify an object.");
						}
						else
						{
							// Look up existing object in world state
							{
								Lock lock(world_state->mutex);
								auto res = cur_world_state->objects.find(object_uid);
								if(res != cur_world_state->objects.end())
								{
									WorldObject* ob = res->second.getPointer();

									// See if the user has permissions to alter this object:
									if(!userHasObjectWritePermissions(*ob, *client_user, this->connected_world_name))
										writeErrorMessageToClient(socket, "You must be the owner of this object to change it.");
									else
									{
										ob->pos = pos;
										ob->axis = axis;
										ob->angle = angle;
										ob->from_remote_transform_dirty = true;
										cur_world_state->dirty_from_remote_objects.insert(ob);
									}

									//conPrint("updated object transform");
								}
							}
						}

						break;
					}
				case Protocol::ObjectFullUpdate:
					{
						//conPrint("ObjectFullUpdate");
						const UID object_uid = readUIDFromStream(*socket);

						// If client is not logged in, refuse object modification.
						if(client_user.isNull())
						{
							writeErrorMessageToClient(socket, "You must be logged in to modify an object.");
							WorldObject dummy_ob;
							readFromNetworkStreamGivenUID(*socket, dummy_ob); // Read rest of ObjectFullUpdate message.
						}
						else
						{
							// Look up existing object in world state
							{
								Lock lock(world_state->mutex);
								auto res = cur_world_state->objects.find(object_uid);
								if(res != cur_world_state->objects.end())
								{
									WorldObject* ob = res->second.getPointer();

									// See if the user has permissions to alter this object:
									if(!userHasObjectWritePermissions(*ob, *client_user, this->connected_world_name))
									{
										writeErrorMessageToClient(socket, "You must be the owner of this object to change it.");
										WorldObject dummy_ob;
										readFromNetworkStreamGivenUID(*socket, dummy_ob); // Read rest of ObjectFullUpdate message.
									}
									else
									{
										readFromNetworkStreamGivenUID(*socket, *ob);
										ob->from_remote_other_dirty = true;
										cur_world_state->dirty_from_remote_objects.insert(ob);

										// Process resources
										std::set<std::string> URLs;
										ob->getDependencyURLSet(URLs);
										for(auto it = URLs.begin(); it != URLs.end(); ++it)
											sendGetFileMessageIfNeeded(*it);
									}
								}
							}
						}
						break;
					}
				case Protocol::ObjectLightmapURLChanged:
					{
						//conPrint("ObjectLightmapURLChanged");
						const UID object_uid = readUIDFromStream(*socket);
						const std::string new_lightmap_url = socket->readStringLengthFirst(10000);

						// Look up existing object in world state
						{
							Lock lock(world_state->mutex);
							auto res = cur_world_state->objects.find(object_uid);
							if(res != cur_world_state->objects.end())
							{
								WorldObject* ob = res->second.getPointer();
					
								ob->lightmap_url = new_lightmap_url;

								ob->from_remote_lightmap_url_dirty = true;
								cur_world_state->dirty_from_remote_objects.insert(ob);
							}
						}
						break;
					}
				case Protocol::ObjectFlagsChanged:
					{
						//conPrint("ObjectFlagsChanged");
						const UID object_uid = readUIDFromStream(*socket);
						const uint32 flags = socket->readUInt32();

						// Look up existing object in world state
						{
							Lock lock(world_state->mutex);
							auto res = cur_world_state->objects.find(object_uid);
							if(res != cur_world_state->objects.end())
							{
								WorldObject* ob = res->second.getPointer();

								ob->flags = flags; // Copy flags

								ob->from_remote_flags_dirty = true;
								cur_world_state->dirty_from_remote_objects.insert(ob);
							}
						}
						break;
					}
				case Protocol::CreateObject: // Client wants to create an object
					{
						conPrint("CreateObject");

						WorldObjectRef new_ob = new WorldObject();
						new_ob->uid = readUIDFromStream(*socket); // Read dummy UID
						readFromNetworkStreamGivenUID(*socket, *new_ob);

						conPrint("model_url: '" + new_ob->model_url + "', pos: " + new_ob->pos.toString());

						// If client is not logged in, refuse object creation.
						if(client_user.isNull())
						{
							conPrint("Creation denied, user was not logged in.");
							SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
							packet.writeUInt32(Protocol::ErrorMessageID);
							packet.writeStringLengthFirst("You must be logged in to create an object.");
							socket->writeData(packet.buf.data(), packet.buf.size());
						}
						else
						{
							new_ob->creator_id = client_user->id;
							new_ob->created_time = TimeStamp::currentTime();
							new_ob->creator_name = client_user->name;

							std::set<std::string> URLs;
							new_ob->getDependencyURLSet(URLs);
							for(auto it = URLs.begin(); it != URLs.end(); ++it)
								sendGetFileMessageIfNeeded(*it);

							// Look up existing object in world state
							{
								::Lock lock(world_state->mutex);

								// Object for UID not already created, create it now.
								new_ob->uid = world_state->getNextObjectUID();
								new_ob->state = WorldObject::State_JustCreated;
								new_ob->from_remote_other_dirty = true;
								cur_world_state->dirty_from_remote_objects.insert(new_ob);
								cur_world_state->objects.insert(std::make_pair(new_ob->uid, new_ob));
							}
						}

						break;
					}
				case Protocol::DestroyObject: // Client wants to destroy an object.
					{
						conPrint("DestroyObject");
						const UID object_uid = readUIDFromStream(*socket);

						// If client is not logged in, refuse object modification.
						if(client_user.isNull())
						{
							writeErrorMessageToClient(socket, "You must be logged in to destroy an object.");
						}
						else
						{
							Lock lock(world_state->mutex);
							auto res = cur_world_state->objects.find(object_uid);
							if(res != cur_world_state->objects.end())
							{
								WorldObject* ob = res->second.getPointer();

								// See if the user has permissions to alter this object:
								const bool have_delete_perms = userHasObjectWritePermissions(*ob, *client_user, this->connected_world_name);
								if(!have_delete_perms)
									writeErrorMessageToClient(socket, "You must be the owner of this object to destroy it.");
								else
								{
									// Mark object as dead
									ob->state = WorldObject::State_Dead;
									ob->from_remote_other_dirty = true;
									cur_world_state->dirty_from_remote_objects.insert(ob);
								}
							}
						}
						break;
					}
				case Protocol::QueryParcels:
					{
						conPrint("QueryParcels");
						// Send all current parcel data to client
						SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
						packet.writeUInt32(Protocol::ParcelList); // Write message ID
						
						{
							Lock lock(world_state->mutex);
							packet.writeUInt64(cur_world_state->parcels.size()); // Write num parcels
							for(auto it = cur_world_state->parcels.begin(); it != cur_world_state->parcels.end(); ++it)
								writeToNetworkStream(*it->second, packet); // Write parcel
						}

						socket->writeData(packet.buf.data(), packet.buf.size()); // Send the data
						break;
					}
				case Protocol::ParcelFullUpdate: // Client wants to update a parcel
					{
						conPrint("ParcelFullUpdate");
						const ParcelID parcel_id = readParcelIDFromStream(*socket);

						// Look up existing parcel in world state
						{
							bool read = false;

							// Only allow updating of parcels is this is a website connection.
							const bool have_permissions = connection_type == Protocol::ConnectionTypeWebsite;

							if(have_permissions)
							{
								Lock lock(world_state->mutex);
								auto res = cur_world_state->parcels.find(parcel_id);
								if(res != cur_world_state->parcels.end())
								{
									// TODO: Check if this client has permissions to update the parcel information.

									Parcel* parcel = res->second.getPointer();
									readFromNetworkStreamGivenID(*socket, *parcel);
									read = true;
									parcel->from_remote_dirty = true;
								}
							}

							// Make sure we have read the whole pracel from the network stream
							if(!read)
							{
								Parcel dummy;
								readFromNetworkStreamGivenID(*socket, dummy);
							}
						}
						break;
					}
				case Protocol::ChatMessageID:
					{
						//const std::string name = socket->readStringLengthFirst(MAX_STRING_LEN);
						const std::string msg = socket->readStringLengthFirst(MAX_STRING_LEN);

						conPrint("Received chat message: '" + msg + "'");

						if(client_user.isNull())
						{
							writeErrorMessageToClient(socket, "You must be logged in to chat.");
						}
						else
						{
							// Enqueue chat messages to worker threads to send
							// Send ChatMessageID packet
							SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
							packet.writeUInt32(Protocol::ChatMessageID);
							packet.writeStringLengthFirst(client_user->name);
							packet.writeStringLengthFirst(msg);

							enqueuePacketToBroadcast(packet, server);
						}
						break;
					}
				case Protocol::UserSelectedObject:
					{
						//conPrint("Received UserSelectedObject msg.");

						const UID object_uid = readUIDFromStream(*socket);

						// Send message to connected clients
						{
							SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
							packet.writeUInt32(Protocol::UserSelectedObject);
							writeToStream(client_avatar_uid, packet);
							writeToStream(object_uid, packet);

							enqueuePacketToBroadcast(packet, server);
						}
						break;
					}
				case Protocol::UserDeselectedObject:
					{
						//conPrint("Received UserDeselectedObject msg.");

						const UID object_uid = readUIDFromStream(*socket);

						// Send message to connected clients
						{
							SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
							packet.writeUInt32(Protocol::UserDeselectedObject);
							writeToStream(client_avatar_uid, packet);
							writeToStream(object_uid, packet);

							enqueuePacketToBroadcast(packet, server);
						}
						break;
					}
				case Protocol::LogInMessage: // Client wants to log in.
					{
						conPrint("LogInMessage");

						const std::string username = socket->readStringLengthFirst(MAX_STRING_LEN);
						const std::string password = socket->readStringLengthFirst(MAX_STRING_LEN);

						conPrint("username: '" + username + "'");
						
						bool logged_in = false;
						{
							Lock lock(world_state->mutex);
							auto res = world_state->name_to_users.find(username);
							if(res != world_state->name_to_users.end())
							{
								User* user = res->second.getPointer();
								const bool password_valid = user->isPasswordValid(password);
								conPrint("password_valid: " + boolToString(password_valid));
								if(password_valid)
								{
									// Password is valid, log user in.
									client_user = user;

									logged_in = true;
								}
							}
						}

						conPrint("logged_in: " + boolToString(logged_in));
						if(logged_in)
						{
							// Send logged-in message to client
							SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
							packet.writeUInt32(Protocol::LoggedInMessageID);
							writeToStream(client_user->id, packet);
							packet.writeStringLengthFirst(username);
							socket->writeData(packet.buf.data(), packet.buf.size());
						}
						else
						{
							// Login failed.  Send error message back to client
							SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
							packet.writeUInt32(Protocol::ErrorMessageID);
							packet.writeStringLengthFirst("Login failed: username or password incorrect.");
							socket->writeData(packet.buf.data(), packet.buf.size());
						}
					
						break;
					}
				case Protocol::LogOutMessage: // Client wants to log out.
					{
						conPrint("LogOutMessage");

						client_user = NULL; // Mark the client as not logged in.

						// Send logged-out message to client
						SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
						packet.writeUInt32(Protocol::LoggedOutMessageID);
						socket->writeData(packet.buf.data(), packet.buf.size());
						
						break;
					}
				case Protocol::SignUpMessage:
					{
						conPrint("SignUpMessage");

						const std::string username = socket->readStringLengthFirst(MAX_STRING_LEN);
						const std::string email    = socket->readStringLengthFirst(MAX_STRING_LEN);
						const std::string password = socket->readStringLengthFirst(MAX_STRING_LEN);

						conPrint("username: '" + username + "', email: '" + email + "'");

						bool signed_up = false;
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

								// We need a random salt for the user.
								// To generate this, we will hash the username, email address, current time in seconds, time since program started, and a hidden constant salt together.
								const std::string hash_input = username + " " + email + " " +
									toString((uint64)Clock::getSecsSince1970()) + " " + toString(Clock::getTimeSinceInit()) +
									"qySNdBWNbLG5mFt6NnRDHwYF345345"; // from random.org

								std::vector<unsigned char> binary_digest;
								SHA256::hash(hash_input, binary_digest);
								std::string user_salt;
								Base64::encode(&binary_digest[0], binary_digest.size(), user_salt);

								new_user->password_hash_salt = user_salt;
								new_user->hashed_password = User::computePasswordHash(password, user_salt);

								// Add new user to world state
								world_state->user_id_to_users.insert(std::make_pair(new_user->id, new_user));
								world_state->name_to_users   .insert(std::make_pair(username,     new_user));
								world_state->markAsChanged(); // Mark as changed so gets saved to disk.

								client_user = new_user; // Log user in as well.
								signed_up = true;
							}
						}

						conPrint("signed_up: " + boolToString(signed_up));
						if(signed_up)
						{
							conPrint("Sign up successful");
							// Send signed-up message to client
							SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
							packet.writeUInt32(Protocol::SignedUpMessageID);
							writeToStream(client_user->id, packet);
							packet.writeStringLengthFirst(username);
							socket->writeData(packet.buf.data(), packet.buf.size());
						}
						else
						{
							conPrint("Sign up failed.");

							// signup failed.  Send error message back to client
							SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
							packet.writeUInt32(Protocol::ErrorMessageID);
							packet.writeStringLengthFirst("Signup failed: username or password incorrect.");
							socket->writeData(packet.buf.data(), packet.buf.size());
						}

						break;
					}
				case Protocol::RequestPasswordReset:
					{
						conPrint("RequestPasswordReset");

						const std::string email    = socket->readStringLengthFirst(MAX_STRING_LEN);

						conPrint("email: " + email);

						// TEMP: Send password reset email in this thread for now. 
						// TODO: move to another thread (make some kind of background task?)
						{
							Lock lock(world_state->mutex);
							for(auto it = world_state->user_id_to_users.begin(); it != world_state->user_id_to_users.end(); ++it)
								if(it->second->email_address == email)
								{
									User* user = it->second.getPointer();
									try
									{
										user->sendPasswordResetEmail();
										world_state->markAsChanged(); // Mark as changed so gets saved to disk.
										conPrint("Sent user password reset email to '" + email + ", username '" + user->name + "'");
									}
									catch(Indigo::Exception& e)
									{
										conPrint("Sending password reset email failed: " + e.what());
									}
								}
						}
					
						break;
					}
				case Protocol::ChangePasswordWithResetToken:
					{
						conPrint("ChangePasswordWithResetToken");
						
						const std::string email			= socket->readStringLengthFirst(MAX_STRING_LEN);
						const std::string reset_token	= socket->readStringLengthFirst(MAX_STRING_LEN);
						const std::string new_password	= socket->readStringLengthFirst(MAX_STRING_LEN);

						conPrint("email: " + email);
						conPrint("reset_token: " + reset_token);
						//conPrint("new_password: " + new_password);

						{
							Lock lock(world_state->mutex);

							// Find user with the given email address:
							for(auto it = world_state->user_id_to_users.begin(); it != world_state->user_id_to_users.end(); ++it)
								if(it->second->email_address == email)
								{
									User* user = it->second.getPointer();
									const bool reset = user->resetPasswordWithToken(reset_token, new_password);
									if(reset)
									{
										world_state->markAsChanged(); // Mark as changed so gets saved to disk.
										conPrint("User password successfully updated.");
									}
								}
						}

						break;
					}
				default:			
					{
						conPrint("Unknown message id: " + toString(msg_type));
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
		}
	}
	catch(MySocketExcep& e)
	{
		if(e.excepType() == MySocketExcep::ExcepType_ConnectionClosedGracefully)
			conPrint("Updates client from " + IPAddress::formatIPAddressAndPort(socket->getOtherEndIPAddress(), socket->getOtherEndPort()) + " closed connection gracefully.");
		else
			conPrint("Socket error: " + e.what());
	}
	catch(Indigo::Exception& e)
	{
		conPrint("Indigo::Exception: " + e.what());
	}
	catch(FileUtils::FileUtilsExcep& e)
	{
		conPrint("FileUtils::FileUtilsExcep: " + e.what());
	}
	catch(std::bad_alloc&)
	{
		conPrint("WorkerThread: Caught std::bad_alloc.");
	}

	// Mark avatar corresponding to client as dead
	if(cur_world_state.nonNull())
	{
		Lock lock(world_state->mutex);
		if(cur_world_state->avatars.count(client_avatar_uid) == 1)
		{
			cur_world_state->avatars[client_avatar_uid]->state = Avatar::State_Dead;
			cur_world_state->avatars[client_avatar_uid]->other_dirty = true;
		}
	}
}


void WorkerThread::enqueueDataToSend(const std::string& data)
{
	if(VERBOSE) conPrint("WorkerThread::enqueueDataToSend(), data: '" + data + "'");
	data_to_send.enqueue(data);
	event_fd.notify();
}
