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
#include <BufferInStream.h>
#include <AtomicInt.h>
#include <string>
class Server;


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

	std::string connected_world_name;

	void enqueueDataToSend(const std::string& data); // threadsafe
	void enqueueDataToSend(const SocketBufferOutStream& packet); // threadsafe

	web::RequestInfo websocket_request_info; // If the client connected via a websocket, this the HTTP request data.  Is used for accessing the login cookie.

private:
	void sendGetFileMessageIfNeeded(const URLString& resource_URL);
	void handleResourceUploadConnection();
	void handleResourceDownloadConnection();
	void handleScreenshotBotConnection();
	void handleEthBotConnection();
	void conPrintIfNotFuzzing(const std::string& msg);

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
};
