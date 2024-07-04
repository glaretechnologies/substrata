/*=====================================================================
UDPHandlerThread.h
------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include <MessageableThread.h>
#include <UDPSocket.h>
#include <IPAddress.h>
#include <vector>
class Server;


struct ConnectedClientInfo
{
	IPAddress ip_addr;
	int client_UDP_port;
};


/*=====================================================================
UDPHandlerThread
----------------
Handles UDP messages from clients, sends back to connected clients.
=====================================================================*/
class UDPHandlerThread : public MessageableThread
{
public:
	UDPHandlerThread(Server* server);
	~UDPHandlerThread();

	void doRun() override;

	virtual void kill() override;

private:
	std::vector<ConnectedClientInfo> connected_clients;
	Reference<UDPSocket> udp_socket;
	Server* server;
};
