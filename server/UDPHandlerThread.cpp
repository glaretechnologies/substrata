/*=====================================================================
UDPHandlerThread.cpp
--------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "UDPHandlerThread.h"


#include "ServerWorldState.h"
#include "Server.h"
#include <ConPrint.h>
#include <StringUtils.h>
#include <PlatformUtils.h>


static const int server_UDP_port = 7601;


UDPHandlerThread::UDPHandlerThread(Server* server_)
:	server(server_)
{
}


UDPHandlerThread::~UDPHandlerThread()
{
}


void UDPHandlerThread::doRun()
{
	PlatformUtils::setCurrentThreadNameIfTestsEnabled("UDPHandlerThread");

	try
	{
		conPrint("UDPHandlerThread: Listening on UDP port " + toString(server_UDP_port) + "...");
		udp_socket = new UDPSocket();
		udp_socket->bindToPort(server_UDP_port, /*reuse_address=*/true);

		conPrint("UDPHandlerThread: Bound to port " + toString(server_UDP_port));

		std::vector<uint8> packet_buf(4096);

		while(1)
		{
			IPAddress sender_ip_addr;
			int sender_port;
			const size_t packet_len = udp_socket->readPacket(packet_buf.data(), (int)packet_buf.size(), sender_ip_addr, sender_port);

			conPrint("UDPHandlerThread: Received packet of length " + toString(packet_len) + " from " + sender_ip_addr.toString() + ", port " + toString(sender_port));

			if(packet_len >= sizeof(uint32))
			{
				uint32 type;
				std::memcpy(&type, packet_buf.data(), 4);
				if(type == 1) // If packet has voice type:
				{
					if(server->connected_clients_changed != 0)
					{
						// Rebuild our connected_clients vector.
						connected_clients.clear();

						{
							Lock lock(server->connected_clients_mutex);

							for(auto it = server->connected_clients.begin(); it != server->connected_clients.end(); ++it)
								if(it->second.client_UDP_port > 0) // If remote UDP port is known:
									connected_clients.push_back(ConnectedClientInfo({it->second.ip_addr, it->second.client_UDP_port}));

							server->connected_clients_changed = 0;
						}
					}

					// Broadcast packet to clients
					for(size_t i=0; i<connected_clients.size(); ++i)
					{
						conPrint("UDPHandlerThread: Sending packet to " + connected_clients[i].ip_addr.toString() + ", port " + toString(connected_clients[i].client_UDP_port) + " ...");
						udp_socket->sendPacket(packet_buf.data(), packet_len, connected_clients[i].ip_addr, connected_clients[i].client_UDP_port);
					}
				}
				else if(type == 2)
				{
					if(packet_len >= sizeof(uint32) + sizeof(UID))
					{
						UID client_avatar_uid;
						std::memcpy(&client_avatar_uid, packet_buf.data() + 4, sizeof(UID));

						server->clientUDPPortBecameKnown(client_avatar_uid, sender_ip_addr, sender_port);
					}
				}
			}
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("UDPHandlerThread: glare::Exception: " + e.what());
	}
	catch(std::bad_alloc&)
	{
		conPrint("UDPHandlerThread: Caught std::bad_alloc.");
	}

	udp_socket = NULL;

	conPrint("UDPHandlerThread: terminating.");
}
