/*=====================================================================
ScreenshotBot.h
----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/


#include "../shared/Protocol.h"
#include <networking/networking.h>
#include <networking/TLSSocket.h>
#include <networking/url.h>
#include <PlatformUtils.h>
#include <Clock.h>
#include <Timer.h>
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


	while(1) // While screenshot bot should keep running:
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
			socket->writeUInt32(Protocol::ConnectionTypeScreenshotBot); // Write connection type

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

			while(1)
			{
				conPrint("Waiting for ScreenShotRequest...");

				const uint32 request_type = socket->readUInt32();
				if(request_type == Protocol::ScreenShotRequest || request_type == Protocol::TileScreenShotRequest)
				{
					// Get screenshot request
					if(request_type == Protocol::ScreenShotRequest)
						conPrint("Received screenshot request from server.");
					else
						conPrint("Received map tile screenshot request from server.");

					const int NUM_BYTES = 16;
					uint8 data[NUM_BYTES];
					CryptoRNG::getRandomBytes(data, NUM_BYTES);

					const std::string gui_client_path = FileUtils::getDirectory(PlatformUtils::getFullPathToCurrentExecutable()) + "/gui_client.exe";

					std::vector<std::string> command_line_args;

					std::string screenshot_path;

					if(request_type == Protocol::ScreenShotRequest)
					{
						// Read cam position
						const double cam_x = socket->readDouble();
						const double cam_y = socket->readDouble();
						const double cam_z = socket->readDouble();

						// Read cam angles
						const double angles_0 = socket->readDouble();
						const double angles_1 = socket->readDouble();
						const double angles_2 = socket->readDouble();

						const int32 screenshot_width_px = socket->readInt32();
						const int32 highlight_parcel_id = socket->readInt32();

						//conPrint("highlight_parcel_id: " + toString(highlight_parcel_id));

						const std::string screenshot_filename = "screenshot_" + StringUtils::convertByteArrayToHexString(data, NUM_BYTES) + ".jpg";
						screenshot_path = "D:/tempfiles/screenshots/" + screenshot_filename;

						command_line_args.push_back(gui_client_path);
						command_line_args.push_back("-h");
						command_line_args.push_back(server_hostname);
						command_line_args.push_back("--takescreenshot");
						command_line_args.push_back(toString(cam_x));
						command_line_args.push_back(toString(cam_y));
						command_line_args.push_back(toString(cam_z));
						command_line_args.push_back(toString(angles_0));
						command_line_args.push_back(toString(angles_1));
						command_line_args.push_back(toString(angles_2));
						command_line_args.push_back(toString(screenshot_width_px));
						command_line_args.push_back(toString(highlight_parcel_id));
						command_line_args.push_back(screenshot_path);
					}
					else if(request_type == Protocol::TileScreenShotRequest)
					{
						const int tile_x = socket->readInt32();
						const int tile_y = socket->readInt32();
						const int tile_z = socket->readInt32();

						conPrint("tile: (" + toString(tile_x) + ", " + toString(tile_y) + ", " + toString(tile_z) + ")");

						const std::string screenshot_filename = "tile_" + toString(tile_x) + "_" + toString(tile_y) + "_" + toString(tile_z) + "_" + StringUtils::convertByteArrayToHexString(data, NUM_BYTES) + ".jpg";
						screenshot_path = "D:/tempfiles/screenshots/" + screenshot_filename;

						command_line_args.push_back(gui_client_path);
						command_line_args.push_back("-h");
						command_line_args.push_back(server_hostname);
						command_line_args.push_back("--takemapscreenshot");
						command_line_args.push_back(toString(tile_x));
						command_line_args.push_back(toString(tile_y));
						command_line_args.push_back(toString(tile_z));
						command_line_args.push_back(screenshot_path);
					}
					else
						throw glare::Exception("invalid request type.");

					// Command a gui_client process to take the screenshot
					glare::Process process(gui_client_path, command_line_args);

					Timer timer;
					while(1)
					{
						while(process.isStdOutReadable())
						{
							const std::string output = process.readStdOut();
							std::vector<std::string> lines = ::split(output, '\n');
							for(size_t i=0; i<lines.size(); ++i)
								if(!isAllWhitespace(lines[i]))
									conPrint("GUI_CLIENT> " + lines[i]);
						}

						if(!process.isProcessAlive())
							break;

						PlatformUtils::Sleep(10);

						if(timer.elapsed() > 100)
						{
							conPrint("gui client process seems stuck, terminating it...");
							process.terminateProcess();
							break;
						}
					}

					std::string output, err_output;
					process.readAllRemainingStdOutAndStdErr(output, err_output);
					conPrint("GUI_CLIENT> " + output);
					conPrint("GUI_CLIENT> " + err_output);
					conPrint("Gui client process terminated.");

					if(process.getExitCode() != 0)
						throw glare::Exception("Return code from gui_client was non-zero: " + toString(process.getExitCode()));

					// Load generated screenshot
					const std::string screenshot_data = FileUtils::readEntireFile(screenshot_path);

					socket->writeUInt32(Protocol::ScreenShotSucceeded);

					// Send it to the server
					socket->writeUInt64(screenshot_data.length());
					socket->writeData(screenshot_data.data(), screenshot_data.length());
				}
				else
				{
					throw glare::Exception("unknown request type: " + toString(request_type));
				}
			}
		}
		catch(glare::Exception& e)
		{
			// Connection failed.
			conPrint("Error: " + e.what());
			PlatformUtils::Sleep(1000);
		}
	}

	return 0;
}
