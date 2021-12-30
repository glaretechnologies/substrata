/*=====================================================================
ScreenshotBot.h
----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/


#include "../shared/Protocol.h"
#include "../shared/UID.h"
#include "../shared/Avatar.h"
#include <networking/networking.h>
#include <networking/TLSSocket.h>
#include <networking/url.h>
#include <utils/SocketBufferOutStream.h>
#include <utils/BufferInStream.h>
#include <utils/SocketBufferOutStream.h>
#include <maths/vec3.h>
#include <PlatformUtils.h>
#include <Clock.h>
#include <Timer.h>
#include <MyThread.h>
#include <PCG32.h>
#include <ConPrint.h>
#include <OpenSSL.h>
#include <Exception.h>
#include <FileUtils.h>
#include <StringUtils.h>
#include <GlareProcess.h>
#include <CryptoRNG.h>
#include <tls.h>


// TODO: do authentication
//static const std::string username = "screenshotbot";
//static const std::string password = "1NzpaaM3qN";


static void updatePacketLengthField(SocketBufferOutStream& packet)
{
	// length field is second uint32
	assert(packet.buf.size() >= sizeof(uint32) * 2);
	if(packet.buf.size() >= sizeof(uint32) * 2)
	{
		const uint32 len = (uint32)packet.buf.size();
		std::memcpy(&packet.buf[4], &len, 4);
	}
}


static void initPacket(SocketBufferOutStream& scratch_packet, uint32 message_id)
{
	scratch_packet.buf.resize(sizeof(uint32) * 2);
	std::memcpy(&scratch_packet.buf[0], &message_id, sizeof(uint32));
	std::memset(&scratch_packet.buf[4], 0, sizeof(uint32)); // Write dummy message length, will be updated later when size of message is known.
}


class StressTestBotThread : public MyThread
{
public:
	virtual void run()
	{
		// Connect to substrata server
		try
		{
			//const std::string server_hostname = "localhost";
			const std::string server_hostname = "substrata.info";
			const int server_port = 7600;

			conPrint("Connecting to " + server_hostname + ":" + toString(server_port) + "...");

			MySocketRef plain_socket = new MySocket();
			plain_socket->setUseNetworkByteOrder(false);
			plain_socket->connect(server_hostname, server_port);

			conPrint("Connected to " + server_hostname + ":" + toString(server_port) + "!");

			SocketInterfaceRef socket = new TLSSocket(plain_socket, client_tls_config, server_hostname);

			socket->writeUInt32(Protocol::CyberspaceHello); // Write hello
			socket->writeUInt32(Protocol::CyberspaceProtocolVersion); // Write protocol version
			socket->writeUInt32(Protocol::ConnectionTypeUpdates); // Write connection type

			socket->writeStringLengthFirst(""); // Write world name

			// Read hello response from server
			const uint32 hello_response = socket->readUInt32();
			if(hello_response != Protocol::CyberspaceHello)
				throw glare::Exception("Invalid hello from server: " + toString(hello_response));

			// Read protocol version response from server
			const uint32 protocol_response = socket->readUInt32();
			if(protocol_response == Protocol::ClientProtocolTooOld)
			{
				const std::string msg = socket->readStringLengthFirst(10000);
				throw glare::Exception(msg);
			}
			else if(protocol_response == Protocol::ClientProtocolTooNew)
			{
				const std::string msg = socket->readStringLengthFirst(10000);
				throw glare::Exception(msg);
			}
			else if(protocol_response == Protocol::ClientProtocolOK)
			{}
			else
				throw glare::Exception("Invalid protocol version response from server: " + toString(protocol_response));

			const uint32 peer_protocol_version = Protocol::CyberspaceProtocolVersion; // Just assume server is speaking the latest procotol version we know about, for now.
			// TODO: Send server protocol version from the server.


			// Read assigned client avatar UID
			const UID client_avatar_uid = readUIDFromStream(*socket);


			Vec3d cur_pos(0,0,1.67);
			Vec3d cur_vel(0.5,0,0);
			Vec3f cur_angles(0, Maths::pi_2<float>(), 0);


			PCG32 rng(seed);
			float heading = rng.unitRandom() * Maths::get2Pi<float>();
			cur_angles = Vec3f(0, Maths::pi_2<float>(), heading);
			cur_vel = Vec3d(cos(heading), sin(heading), 0) * 2;

			SocketBufferOutStream scratch_packet(SocketBufferOutStream::DontUseNetworkByteOrder);
			// Send CreateAvatar packet for this client's avatar
			{
				initPacket(scratch_packet, Protocol::CreateAvatar);

				Avatar avatar;
				avatar.uid = client_avatar_uid;
				avatar.pos = cur_pos;
				avatar.rotation = cur_angles;
				writeToNetworkStream(avatar, scratch_packet);

				updatePacketLengthField(scratch_packet);
				socket->writeData(scratch_packet.buf.data(), scratch_packet.buf.size());
			}

			
			Timer timer;
			Timer time_since_update_packet_sent;

			BufferInStream msg_buffer;

			
			Timer change_dir_timer;
			

			double last_think_time = Clock::getCurTimeRealSec();

			while(1)
			{
				if(socket->readable(/*timeout_s=*/0.05))
				{
					// Read msg type and length
					uint32 msg_type_and_len[2];
					socket->readData(msg_type_and_len, sizeof(uint32) * 2);
					const uint32 msg_type = msg_type_and_len[0];
					const uint32 msg_len = msg_type_and_len[1];

					// conPrint("ClientThread: Read message header: id: " + toString(msg_type) + ", len: " + toString(msg_len));

					if((msg_len < sizeof(uint32) * 2) || (msg_len > 1000000))
						throw glare::Exception("Invalid message size: " + toString(msg_len));

					// Read entire message
					msg_buffer.buf.resizeNoCopy(msg_len);
					msg_buffer.read_index = sizeof(uint32) * 2;

					socket->readData(msg_buffer.buf.data() + sizeof(uint32) * 2, msg_len - sizeof(uint32) * 2); // Read rest of message, store in msg_buffer.

					//conPrint("Read msg of type " + toString(msg_type));

					switch(msg_type)
					{
						case Protocol::AllObjectsSent:
						{
						}
					}
				} // end if socket was readable

				// Change heading to a random value every couple of seconds
				if(change_dir_timer.elapsed() > 2)
				{
					heading = rng.unitRandom() * Maths::get2Pi<float>();// (float)std::atan2(cur_vel.y, cur_vel.x);
					cur_angles = Vec3f(0, Maths::pi_2<float>(), heading);

					//cur_vel = normalise(Vec3d(rng.unitRandom(), rng.unitRandom(), 0));
					cur_vel = Vec3d(cos(heading), sin(heading), 0) * 2;

					change_dir_timer.reset();
				}

				const double cur_time = Clock::getCurTimeRealSec();
				const double dt = myMin(0.1, cur_time - last_think_time);
				last_think_time = cur_time;
				cur_pos += cur_vel * dt;
				

				// Send AvatarTransformUpdate packet every 0.1 s
				if(time_since_update_packet_sent.elapsed() > 0.1)
				{
					const uint32 anim_state = 0;

					initPacket(scratch_packet, Protocol::AvatarTransformUpdate);
					writeToStream(client_avatar_uid, scratch_packet);
					writeToStream(cur_pos, scratch_packet);
					writeToStream(cur_angles, scratch_packet);
					scratch_packet.writeUInt32(anim_state);

					updatePacketLengthField(scratch_packet);
					socket->writeData(scratch_packet.buf.data(), scratch_packet.buf.size());

					time_since_update_packet_sent.reset();
				}

			} // End while(1) loop
		}
		catch(glare::Exception& e)
		{
			// Connection failed.
			conPrint("Error: " + e.what());
			//PlatformUtils::Sleep(1000);
		}
	}

	struct tls_config* client_tls_config;
	int seed;
};


int main(int argc, char* argv[])
{
	Clock::init();
	Networking::createInstance();
	PlatformUtils::ignoreUnixSignals();
	OpenSSL::init();
	TLSSocket::initTLS();


	// Create and init TLS client config
	struct tls_config* client_tls_config = tls_config_new();
	if(!client_tls_config)
		throw glare::Exception("Failed to initialise TLS (tls_config_new failed)");
	tls_config_insecure_noverifycert(client_tls_config); // TODO: Fix this, check cert etc..
	tls_config_insecure_noverifyname(client_tls_config);


	const int NUM_THREADS = 300;
	std::vector<Reference<StressTestBotThread>> threads;
	for(int i=0; i<NUM_THREADS; ++i)
	{
		Reference<StressTestBotThread> t = new StressTestBotThread();
		t->client_tls_config = client_tls_config;
		t->seed = i;
		t->launch();
		threads.push_back(t);
	}

	while(1)
	{
		PlatformUtils::Sleep(100);
	}
	//while(1) // While stress-test bot should keep running:
	//{
	//	
	//} // End while screenshot bot should keep running:

	return 0;
}
