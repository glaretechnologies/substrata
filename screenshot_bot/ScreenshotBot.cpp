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


static const std::string username = "screenshotbot";
static const std::string password = "1NzpaaM3qN";


int main(int argc, char* argv[])
{
	Clock::init();
	Networking::createInstance();
	PlatformUtils::ignoreUnixSignals();
	OpenSSL::init();
	TLSSocket::initTLS();


	while(1) // While lightmapper bot should keep running:
	{
		// Connect to substrata server
		try
		{
			const std::string server_hostname = "localhost"; // "substrata.info"
			const int server_port = 7600;

			conPrint("Connecting to " + server_hostname + ":" + toString(server_port) + "...");

			MySocketRef socket = new MySocket();
			socket->setUseNetworkByteOrder(false);
			socket->connect(server_hostname, server_port);

			conPrint("Connected to " + server_hostname + ":" + toString(server_port) + "!");

			socket->writeUInt32(Protocol::CyberspaceHello); // Write hello
			socket->writeUInt32(Protocol::CyberspaceProtocolVersion); // Write protocol version
			socket->writeUInt32(Protocol::ConnectionTypeScreenShotBot); // Write connection type

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
				const uint32 request_type = socket->readUInt32();
				if(request_type == Protocol::ScreenShotRequest)
				{
					// Get screenshot request

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

					// Command a gui_client process to take the screenshot

					const int NUM_BYTES = 16;
					uint8 data[NUM_BYTES];
					CryptoRNG::getRandomBytes(data, NUM_BYTES);

					const std::string screenshot_filename = "screenshot_" + StringUtils::convertByteArrayToHexString(data, NUM_BYTES) + ".jpg";
					const std::string screenshot_path = "D:/tempfiles/screenshots/" + screenshot_filename;

					const std::string gui_client_path = "C:\\programming\\new_cyberspace\\output\\vs2019\\cyberspace_x64\\debug\\gui_client.exe";
					std::vector<std::string> command_line_args;
					command_line_args.push_back(gui_client_path);
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
					}

					std::string output, err_output;
					process.readAllRemainingStdOutAndStdErr(output, err_output);
					conPrint("GUI_CLIENT> " + output);
					conPrint("GUI_CLIENT> " + err_output);
					conPrint("Gui client process terminated.");

					// Load generated screenshot
					const std::string screenshot_data = FileUtils::readEntireFile(screenshot_path);

					socket->writeUInt32(Protocol::ScreenShotSucceeded);

					// Send it to the server
					socket->writeUInt64(screenshot_data.length());
					socket->writeData(screenshot_data.data(), screenshot_data.length());
				}
				else
				{
					throw glare::Exception("unknown protocol type: " + toString(request_type));
				}
			}
		}
		catch(glare::Exception& e)
		{
			// Connection failed.
			conPrint("Connection failed: " + e.what());
			PlatformUtils::Sleep(10000);
		}
	}

	return 0;
}
