/*=====================================================================
CVBot.h
-------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/



#include "CryptoVoxelsLoader.h"
#include "../shared/Protocol.h"
#include "../shared/ResourceManager.h"
#include "../gui_client/ClientThread.h"
#include <networking/networking.h>
#include <networking/TLSSocket.h>
#include <PlatformUtils.h>
#include <Clock.h>
#include <ConPrint.h>
#include <OpenSSL.h>
#include <JSONParser.h>
#include <Exception.h>


const int server_port = 7600;


int main(int argc, char* argv[])
{
	Clock::init();
	Networking::createInstance();
	PlatformUtils::ignoreUnixSignals();
	OpenSSL::init();
	TLSSocket::initTLS();

	ThreadSafeQueue<Reference<ThreadMessage> > msg_queue;

	Reference<WorldState> world_state = new WorldState();

	Reference<ClientThread> client_thread = new ClientThread(
		&msg_queue,
		//"substrata.info",
		"localhost",
		server_port, // port
		"sdfsdf", // avatar URL
		"cryptovoxels" // world name
	);
	client_thread->world_state = world_state;

	ThreadManager client_thread_manager;
	client_thread_manager.addThread(client_thread);

	const std::string appdata_path = PlatformUtils::getOrCreateAppDataDirectory("Cyberspace");
	const std::string resources_dir = appdata_path + "/resources";
	conPrint("resources_dir: " + resources_dir);
	Reference<ResourceManager> resource_manager = new ResourceManager(resources_dir);


	// Make LogInMessage packet and enqueue to send
	{
		SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
		packet.writeUInt32(Protocol::LogInMessage);
		packet.writeStringLengthFirst("cryptovoxels");
		packet.writeStringLengthFirst("MQqpGu9L");

		client_thread->enqueueDataToSend(packet);
	}

	// Wait until we have received parcel data.  This means we have received all objects
	conPrint("Waiting for initial data to be received");
	while(!client_thread->initial_state_received)
	{
		PlatformUtils::Sleep(200);
		conPrintStr(".");
	}

	conPrint("Received objects.  world_state->objects.size(): " + toString(world_state->objects.size()));

	conPrint("===================== Loading CryptoVoxels data =====================");
	CryptoVoxelsLoader::loadCryptoVoxelsData(*world_state, client_thread, resource_manager);
	conPrint("===================== Done Loading CryptoVoxels data. =====================");

	while(1)
	{
		PlatformUtils::Sleep(10);

		// Check messages back from queue
		//conPrint("world_state->objects.size(): " + toString(world_state->objects.size()));
	}
	return 0;
}
