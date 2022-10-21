/*=====================================================================
WorkerThreadTests.cpp
---------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "WorkerThreadTests.h"


#if BUILD_TESTS


#include "WorkerThread.h"
#include "Server.h"
#include "TestUtils.h"
#include "WebListenerThread.h"
#include "ResponseUtils.h"
#include "RequestInfo.h"
#include "WebsiteExcep.h"
#include "Escaping.h"
#include "RequestHandler.h"
#include <mathstypes.h>
#include <ConPrint.h>
#include <Clock.h>
#include <AESEncryption.h>
#include <SHA256.h>
#include <Base64.h>
#include <Exception.h>
#include <MySocket.h>
#include <HTTPClient.h>
#include <Lock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <TestSocket.h>
#include <KillThreadMessage.h>
#include <Parser.h>
#include <MemMappedFile.h>
#include <PCG32.h>
#include "WorldCreation.h"


#if 0


// Fuzzing of WorkerThread::doRun()
// 
// See N:\substrata\trunk\webserver\WebServerRequestHandlerTests.cpp for similar code.
// 
// Command line:
// C:\fuzz_corpus\server_worker_thread N:\substrata\trunk\testfiles\fuzz_seeds\server_worker_thread -max_len=1000000 -dict="N:\substrata\trunk\testfiles\fuzz_seeds\server_worker_thread_dictionary.txt"


static Server* test_server;


extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	Clock::init();

	// Without initialising networking, we can't make any network connections during fuzzing, which is probably a good thing.
	// Networking::createInstance();

	try
	{
		const std::string substrata_appdata_dir = PlatformUtils::getOrCreateAppDataDirectory("Substrata");
		const std::string test_server_state_dir = substrata_appdata_dir + "/server_data";
		const std::string server_resource_dir = test_server_state_dir + "/server_resources";

		test_server = new Server();

		test_server->world_state = new ServerAllWorldsState();
		test_server->world_state->resource_manager = new ResourceManager(server_resource_dir);
		test_server->world_state->readFromDisk(test_server_state_dir + "/server_state.bin");

		WorldCreation::createParcelsAndRoads(test_server->world_state);
	}
	catch(glare::Exception& )
	{
		assert(false);
	}

	return 0;
}



// We will use the '!' character to break apart the input buffer into different 'packets'.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	try
	{
		TestSocketRef test_socket = new TestSocket();
		test_socket->setUseNetworkByteOrder(false);

#if 1
		test_socket->buffers.push_back(std::vector<uint8>());
		test_socket->buffers.back().resize(size);
		if(size > 0)
			std::memcpy(test_socket->buffers.back().data(), data, size);
#else
		std::vector<uint8> buf;
		buf.reserve(2048);

		size_t i = 0;
		while(i < size)
		{
			if(data[i] == '!')
			{
				// Break
				test_socket->buffers.push_back(buf);
				buf.resize(0);
			}
			else
			{
				buf.push_back(data[i]);
			}

			i++;
		}

		if(!buf.empty())
			test_socket->buffers.push_back(buf);
#endif

		
		Reference<WorkerThread> worker = new WorkerThread(test_socket, test_server);
		worker->fuzzing = true;
		test_socket = NULL;
		worker->doRun();

		test_server->world_state->world_states.clear();

		// Create a parcel
		const ParcelID parcel_id(0);
		ParcelRef parcel = new Parcel();
		parcel->state = Parcel::State_Alive;
		parcel->id = parcel_id;
		parcel->owner_id = UserID(0);
		parcel->admin_ids.push_back(UserID(0));
		parcel->writer_ids.push_back(UserID(0));
		parcel->created_time = TimeStamp::currentTime();
		parcel->zbounds = Vec2d(-2, 20);
		parcel->verts[0] = Vec2d(-30, -30);
		parcel->verts[1] = Vec2d(30, -30);
		parcel->verts[2] = Vec2d(30, 30);
		parcel->verts[3] = Vec2d(-30, 30);

		parcel->build();

		test_server->world_state->world_states[""] = new ServerWorldState();
		test_server->world_state->getRootWorldState()->parcels[parcel_id] = parcel;

		//test_server->world_state->user_id_to_users.clear();
		//test_server->world_state->name_to_users.clear();
		test_server->world_state->clearAndReset();
	}
	catch(glare::Exception&)
	{
	}
	
	return 0;  // Non-zero return values are reserved for future use.
}
#endif


void WorkerThreadTests::test()
{
	
}


#endif // BUILD_TESTS
