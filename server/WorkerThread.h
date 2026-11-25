/*=====================================================================
WorkerThread.h
--------------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#pragma once


#include "../shared/URLString.h"
#include <RequestInfo.h>
#include <MessageableThread.h>
#include <Platform.h>
#include <MyThread.h>
#include <EventFD.h>
#include <MySocket.h>
#include <SocketBufferOutStream.h>
#include <Vector.h>
#include "../shared/UserID.h"
#include <BufferInStream.h>
#include <AtomicInt.h>
#include <string>
#include <atomic>
class Server;
class ServerAllWorldsState;
class ServerWorldState;
class BuilderAISession;


/*=====================================================================
WorkerThread
------------
This thread runs on the server, and handles communication with a single client.
=====================================================================*/
class WorkerThread : public MessageableThread
{
public:
	// May throw glare::Exception from constructor if EventFD init fails.
	WorkerThread(const Reference<SocketInterface>& socket, Server* server, bool is_websocket_connection);
	virtual ~WorkerThread();

	virtual void doRun() override;

	virtual void kill() override;

	void enqueueDataToSend(const SocketBufferOutStream& packet); // threadsafe
	void enqueueDataToSend(const ArrayRef<uint8> data); // threadsafe

	web::RequestInfo websocket_request_info; // If the client connected via a websocket, this the HTTP request data.  Is used for accessing the login cookie.

	// Copies of state from doRun(), for reading from other threads (e.g. by Server::enqueuePacketToUser()).  Only written by this thread.
	std::atomic<ServerWorldState*> m_cur_world_state; // Same as cur_world_state.ptr().  Null until the client has connected to a world.
	std::atomic<uint32> m_client_user_id; // Value of the UserID the client is logged in as, UserID::invalidUserID() if not logged in.
	std::atomic<uint32> m_client_protocol_version; // 0 until the client has sent its protocol version.

private:
	void sendGetFileMessageIfNeeded(const URLString& resource_URL);
	void handleResourceUploadConnection();
	void handleResourceDownloadConnection();
	void handleScreenshotBotConnection();
	void handleEthBotConnection();
	void conPrintIfNotFuzzing(const std::string& msg);
	void sendPerWorldInitialDataToClient(ServerAllWorldsState* world_state, uint32 client_protocol_version);
	void enqueuePacketToBroadcast(const SocketBufferOutStream& packet_buffer);
	void handleBuilderAIUserMessage(UserID client_user_id, const std::string& client_user_name);

	Reference<ServerWorldState> cur_world_state; // World the client is connected to.  Only use from this thread, other threads should use m_cur_world_state.

	Reference<SocketInterface> socket;
	Server* server;
	EventFD event_fd;	

	Mutex data_to_send_mutex;
	js::Vector<uint8, 16> data_to_send			GUARDED_BY(data_to_send_mutex);
	js::Vector<uint8, 16> temp_data_to_send;

	js::Vector<uint8, 16> m_temp_buf;

	SocketBufferOutStream scratch_packet;

	BufferInStream msg_buffer;
public:
	bool fuzzing; // Are we currently doing fuzz-testing?
private:
	bool write_trace; // Should we write a record of network traffic to disk for fuzz seeding?

	glare::AtomicInt should_quit;

	bool is_websocket_connection;

	Reference<BuilderAISession> builder_ai_session; // The in-world Builder AI session for this connection.  Created lazily on the first Builder AI message.
};
