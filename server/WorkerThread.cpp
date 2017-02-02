/*=====================================================================
WorkerThread.cpp
------------------
File created by ClassTemplate on Thu May 05 01:07:24 2005
Code By Nicholas Chapman.
=====================================================================*/
#include "WorkerThread.h"


#include "../shared/UID.h"
#include <vec3.h>
#include <ConPrint.h>
#include <Clock.h>
#include <AESEncryption.h>
#include <SHA256.h>
#include <Base64.h>
#include <Exception.h>
#include <mysocket.h>
#include <Lock.h>
#include <StringUtils.h>
#include <SocketBufferOutStream.h>
#include <PlatformUtils.h>
#include <KillThreadMessage.h>
#include <ThreadShouldAbortCallback.h>
#include <Parser.h>
#include <FileUtils.h>
#include <MemMappedFile.h>
#include "ServerWorldState.h"
#include "Server.h"
#include "../shared/WorldObject.h"


static const bool VERBOSE = false;


WorkerThread::WorkerThread(int thread_id_, const Reference<MySocket>& socket_, Server* server_)
:	thread_id(thread_id_),
	socket(socket_),
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

	// See if we have this file on the server already
	{
		const std::string path = server->resource_manager->pathForURL(resource_URL);
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
			SocketBufferOutStream packet;
			packet.writeUInt32(GetFile);
			packet.writeStringLengthFirst(resource_URL);

			std::string packet_string(packet.buf.size(), '\0');
			std::memcpy(&packet_string[0], packet.buf.data(), packet.buf.size());

			this->enqueueDataToSend(packet_string);
		}
	}
}


void WorkerThread::doRun()
{
	PlatformUtils::setCurrentThreadNameIfTestsEnabled("WorkerThread");

	ServerWorldState* world_state = server->world_state.getPointer();

	UID client_avatar_uid = UID(0);

	try
	{
		socket->setNoDelayEnabled(true); // For websocket connections, we will want to send out lots of little packets with low latency.  So disable Nagle's algorithm, e.g. send coalescing.

		const uint32 connection_type = socket->readUInt32();

		// Write avatar UID assigned to the connected client.
		if(connection_type == ConnectionTypeUpdates)
		{
			{
				Lock lock(world_state->mutex);
				client_avatar_uid = world_state->next_avatar_uid;
				world_state->next_avatar_uid = UID(world_state->next_avatar_uid.value() + 1);
			}

			writeToStream(client_avatar_uid, *socket);


			// Send all current avatar data to client
			{
				Lock lock(world_state->mutex);
				for(auto it = world_state->avatars.begin(); it != world_state->avatars.end(); ++it)
				{
					const Avatar* avatar = it->second.getPointer();

					// Send AvatarCreated packet
					SocketBufferOutStream packet;
					packet.writeUInt32(AvatarCreated);
					writeToStream(avatar->uid, packet);
					packet.writeStringLengthFirst(avatar->name);
					packet.writeStringLengthFirst(avatar->model_url);
					writeToStream(avatar->pos, packet);
					writeToStream(avatar->axis, packet);
					packet.writeFloat(avatar->angle);

					socket->writeData(packet.buf.data(), packet.buf.size());
				}
			}

			// Send all current object data to client
			{
				Lock lock(world_state->mutex);
				for(auto it = world_state->objects.begin(); it != world_state->objects.end(); ++it)
				{
					const WorldObject* ob = it->second.getPointer();

					// Send AvatarCreated packet
					SocketBufferOutStream packet;
					packet.writeUInt32(ObjectCreated);
					writeToNetworkStream(*ob, packet);
					//writeToStream(ob->uid, packet);
					////packet.writeStringLengthFirst(ob->name);
					//packet.writeStringLengthFirst(ob->model_url);
					//writeToStream(ob->pos, packet);
					//writeToStream(ob->axis, packet);
					//packet.writeFloat(ob->angle);

					socket->writeData(packet.buf.data(), packet.buf.size());
				}
			}
		}

		const int MAX_STRING_LEN = 10000;

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

					if(connection_type == ConnectionTypeUpdates)
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
				case AvatarTransformUpdate:
					{
						//conPrint("AvatarTransformUpdate");
						const UID avatar_uid = readUIDFromStream(*socket);
						const Vec3d pos = readVec3FromStream<double>(*socket);
						const Vec3f axis = readVec3FromStream<float>(*socket);
						const float angle = socket->readFloat();

						// Look up existing avatar in world state
						{
							Lock lock(world_state->mutex);
							auto res = world_state->avatars.find(avatar_uid);
							if(res != world_state->avatars.end())
							{
								Avatar* avatar = res->second.getPointer();
								avatar->pos = pos;
								avatar->axis = axis;
								avatar->angle = angle;
								avatar->transform_dirty = true;

								//conPrint("updated avatar transform");
							}
						}
						break;
					}
				case AvatarFullUpdate:
					{
						conPrint("AvatarFullUpdate");
						const UID avatar_uid = readUIDFromStream(*socket);

						// Look up existing avatar in world state
						{
							Lock lock(world_state->mutex);
							auto res = world_state->avatars.find(avatar_uid);
							if(res != world_state->avatars.end())
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
				case AvatarCreated:
					{
						conPrint("AvatarCreated");
						const UID avatar_uid = readUIDFromStream(*socket);
						const std::string name = socket->readStringLengthFirst(MAX_STRING_LEN);
						const std::string model_url = socket->readStringLengthFirst(MAX_STRING_LEN);
						const Vec3d pos = readVec3FromStream<double>(*socket);
						const Vec3f axis = readVec3FromStream<float>(*socket);
						const float angle = socket->readFloat();

						// Look up existing avatar in world state
						{
							Lock lock(world_state->mutex);
							auto res = world_state->avatars.find(avatar_uid);
							if(res == world_state->avatars.end())
							{
								// Avatar for UID not already created, create it now.
								AvatarRef avatar = new Avatar();
								avatar->uid = avatar_uid;
								avatar->name = name;
								avatar->model_url = model_url;
								avatar->pos = pos;
								avatar->axis = axis;
								avatar->angle = angle;
								avatar->state = Avatar::State_JustCreated;
								avatar->other_dirty = true;
								world_state->avatars.insert(std::make_pair(avatar_uid, avatar));

								conPrint("created new avatar");
							}
						}

						sendGetFileMessageIfNeeded(model_url);

						conPrint("New Avatar creation: username: '" + name + "', model_url: '" + model_url + "'");

						break;
					}
				case AvatarDestroyed:
					{
						conPrint("AvatarDestroyed");
						const UID avatar_uid = readUIDFromStream(*socket);

						// Mark avatar as dead
						{
							Lock lock(world_state->mutex);
							auto res = world_state->avatars.find(avatar_uid);
							if(res != world_state->avatars.end())
							{
								Avatar* avatar = res->second.getPointer();
								avatar->state = Avatar::State_Dead;
								avatar->other_dirty = true;
							}
						}
						break;
					}
				case ObjectTransformUpdate:
					{
						//conPrint("ObjectTransformUpdate");
						const UID object_uid = readUIDFromStream(*socket);
						const Vec3d pos = readVec3FromStream<double>(*socket);
						const Vec3f axis = readVec3FromStream<float>(*socket);
						const float angle = socket->readFloat();

						// Look up existing object in world state
						{
							Lock lock(world_state->mutex);
							auto res = world_state->objects.find(object_uid);
							if(res != world_state->objects.end())
							{
								WorldObject* ob = res->second.getPointer();
								ob->pos = pos;
								ob->axis = axis;
								ob->angle = angle;
								ob->from_remote_transform_dirty = true;

								//conPrint("updated object transform");
							}
						}
						break;
					}
				case ObjectFullUpdate:
					{
						conPrint("ObjectFullUpdate");
						const UID object_uid = readUIDFromStream(*socket);

						// Look up existing object in world state
						{
							Lock lock(world_state->mutex);
							auto res = world_state->objects.find(object_uid);
							if(res != world_state->objects.end())
							{
								WorldObject* ob = res->second.getPointer();
								readFromNetworkStreamGivenUID(*socket, *ob);
								ob->from_remote_other_dirty = true;

								// Process resources
								std::set<std::string> URLs;
								ob->getDependencyURLSet(URLs);
								for(auto it = URLs.begin(); it != URLs.end(); ++it)
									sendGetFileMessageIfNeeded(*it);
							}
						}
						break;
					}
				case ObjectCreated:
					{
						conPrint("ObjectCreated");
						//const UID object_uid = readUIDFromStream(*socket);
						//const std::string name = socket->readStringLengthFirst(); //TODO: enforce max len
						const std::string model_url = socket->readStringLengthFirst(MAX_STRING_LEN);
						const uint64 model_hash = socket->readUInt64();
						const Vec3d pos = readVec3FromStream<double>(*socket);
						const Vec3f axis = readVec3FromStream<float>(*socket);
						const float angle = socket->readFloat();
						const Vec3f scale = readVec3FromStream<float>(*socket);

						conPrint("model_url: '" + model_url + "', pos: " + pos.toString() + ", model_hash: " + toString(model_hash));

						sendGetFileMessageIfNeeded(model_url);

						// Look up existing object in world state
						{
							::Lock lock(world_state->mutex);
							//auto res = world_state->objects.find(object_uid);
							//if(res == world_state->objects.end())
							{
								// Object for UID not already created, create it now.
								WorldObjectRef ob = new WorldObject();
								ob->uid = world_state->getNextObjectUID();
								//ob->name = name;
								ob->model_url = model_url;
								ob->pos = pos;
								ob->axis = axis;
								ob->angle = angle;
								ob->scale = scale;
								ob->state = WorldObject::State_JustCreated;
								ob->from_remote_other_dirty = true;
								world_state->objects.insert(std::make_pair(ob->uid, ob));

								conPrint("created new object");
							}
						}
						break;
					}
				case ObjectDestroyed:
					{
						conPrint("ObjectDestroyed");
						const UID object_uid = readUIDFromStream(*socket);

						// Mark object as dead
						{
							Lock lock(world_state->mutex);
							auto res = world_state->objects.find(object_uid);
							if(res != world_state->objects.end())
							{
								WorldObject* ob = res->second.getPointer();
								ob->state = WorldObject::State_Dead;
								ob->from_remote_other_dirty = true;
							}
						}
						break;
					}
				case ChatMessageID:
					{
						conPrint("ChatMessageID");
						const std::string name = socket->readStringLengthFirst(MAX_STRING_LEN);
						const std::string msg = socket->readStringLengthFirst(MAX_STRING_LEN);

						// Enqueue chat messages to worker threads to send
						{
							// Send AvatarTransformUpdate packet
							SocketBufferOutStream packet;
							packet.writeUInt32(ChatMessageID);
							packet.writeStringLengthFirst(name);
							packet.writeStringLengthFirst(msg);

							std::string packet_string(packet.buf.size(), '\0');
							std::memcpy(&packet_string[0], packet.buf.data(), packet.buf.size());

							Lock lock(server->worker_thread_manager.getMutex());
							for(auto i = server->worker_thread_manager.getThreads().begin(); i != server->worker_thread_manager.getThreads().end(); ++i)
							{
								assert(dynamic_cast<WorkerThread*>(i->getPointer()));
								static_cast<WorkerThread*>(i->getPointer())->enqueueDataToSend(packet_string);
							}
						}
						break;
					}
				case UploadResource:
					{
						conPrint("UploadResource");
						
						const std::string URL = socket->readStringLengthFirst(MAX_STRING_LEN);

						if(!ResourceManager::isValidURL(URL))
							throw Indigo::Exception("Invalid URL '" + URL + "'");

						const uint64 file_len = socket->readUInt64();
						if(file_len > 0)
						{
							// TODO: cap length in a better way
							if(file_len > 1000000000)
								throw Indigo::Exception("uploaded file too large.");

							std::vector<uint8> buf(file_len);
							socket->readData(buf.data(), file_len);

							conPrint("Received file with URL '" + URL + "' from client. (" + toString(file_len) + " B)");

							// Save to disk
							const std::string path = server->resource_manager->pathForURL(URL);
							FileUtils::writeEntireFile(path, (const char*)buf.data(), buf.size());

							conPrint("Written to disk at '" + path + "'.");
						}

						// Connection will be closed by the client after the client has uploaded the file.  Wait for the connection to close.
						//socket->waitForGracefulDisconnect();
						return;
						//break;
					}
				case GetFile:
					{
						conPrint("GetFile");
						
						const std::string URL = socket->readStringLengthFirst(MAX_STRING_LEN);

						conPrint("Requested URL: '" + URL + "'");

						if(!ResourceManager::isValidURL(URL))
						{
							socket->writeUInt32(1); // write error msg to client
						}
						else
						{
							const std::string path = server->resource_manager->pathForURL(URL); // TODO: sanitise

							try
							{
								// Load resource
								MemMappedFile file(path);
								conPrint("Sending OK to client.");
								socket->writeUInt32(0); // write OK msg to client
								socket->writeUInt64(file.fileSize()); // Write file size
								socket->writeData(file.fileData(), file.fileSize()); // Write file data

								conPrint("Sent file '" + path + "' to client. (" + toString(file.fileSize()) + " B)");
							}
							catch(Indigo::Exception&)
							{
								socket->writeUInt32(1); // write error msg to client
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
				if(VERBOSE) print("WorkerThread: event FD was signalled.");

				// The event FD was signalled, which means there is some data to send on the socket.
				// Reset the event fd by reading from it.
				event_fd.read();

				if(VERBOSE) print("WorkerThread: event FD has been reset.");
#endif
			}
		}
	}
	catch(MySocketExcep& e)
	{
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

	// Mark avatar corresponding to client as dead
	{
		Lock lock(world_state->mutex);
		if(world_state->avatars.count(client_avatar_uid) == 1)
		{
			world_state->avatars[client_avatar_uid]->state = Avatar::State_Dead;
			world_state->avatars[client_avatar_uid]->other_dirty = true;
		}
	}
}


void WorkerThread::enqueueDataToSend(const std::string& data)
{
	if(VERBOSE) conPrint("WorkerThread::enqueueDataToSend(), data: '" + data + "'");
	data_to_send.enqueue(data);
	event_fd.notify();
}
