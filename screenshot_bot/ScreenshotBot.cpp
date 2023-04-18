/*=====================================================================
ScreenshotBot.h
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/


#include "../shared/Protocol.h"
#include <networking/networking.h>
#include <networking/TLSSocket.h>
#include <networking/url.h>
#include <utils/SocketBufferOutStream.h>
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
#include <IndigoXMLDoc.h>
#include <XMLParseUtils.h>
#include <tls.h>


struct ScreenshotBotConfig
{
	std::string screenshot_bot_password;
};


static ScreenshotBotConfig parseScreenshotBotConfig(const std::string& config_path)
{
	IndigoXMLDoc doc(config_path);
	pugi::xml_node root_elem = doc.getRootElement();

	ScreenshotBotConfig config;
	config.screenshot_bot_password = XMLParseUtils::parseString(root_elem, "screenshot_bot_password");
	return config;
}


int main(int argc, char* argv[])
{
	try
	{
		Clock::init();
		Networking::createInstance();
		PlatformUtils::ignoreUnixSignals();
		TLSSocket::initTLS();

		const ScreenshotBotConfig config = parseScreenshotBotConfig(PlatformUtils::getAppDataDirectory("Cyberspace") + "/screenshot_bot_config.xml");

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
				socket->writeStringLengthFirst(config.screenshot_bot_password); // Write password

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


				glare::Process* process = NULL;
				MySocketRef to_gui_socket;
				Timer time_since_last_shot_request;

				while(1)
				{
					conPrint("Waiting for ScreenShotRequest message from '" + server_hostname + "'...");

					if(socket->readable(/*timeout_s=*/5.0))
					{
						const uint32 request_type = socket->readUInt32();
						if(request_type == Protocol::ScreenShotRequest || request_type == Protocol::TileScreenShotRequest)
						{
							// Get screenshot request
							if(request_type == Protocol::ScreenShotRequest)
								conPrint("Received screenshot request from server.");
							else
								conPrint("Received map tile screenshot request from server.");

							// Generate some random bytes for the screenshot path
							const int NUM_BYTES = 16;
							uint8 data[NUM_BYTES];
							CryptoRNG::getRandomBytes(data, NUM_BYTES);

							std::string screenshot_path;

							SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder); // We will write the command to the GUI process in this.

							if(request_type == Protocol::ScreenShotRequest)
							{
								// Read cam position
								const double cam_x = socket->readDouble();
								const double cam_y = socket->readDouble();
								const double cam_z = socket->readDouble();

								// Read cam angles
								double angles_0 = socket->readDouble();
								const double angles_1 = socket->readDouble();
								const double angles_2 = socket->readDouble();

								const int32 screenshot_width_px = socket->readInt32();
								const int32 highlight_parcel_id = socket->readInt32();


								conPrint("highlight_parcel_id: " + toString(highlight_parcel_id));
								printVar(cam_x);
								printVar(cam_y);
								printVar(cam_z);
								printVar(angles_0);
								printVar(angles_1);
								printVar(angles_2);

								if(!isFinite(cam_x) || cam_x < -1.0e6 || cam_x > 1.0e6)
									throw glare::Exception("Invalid cam position x: " + toString(cam_x));

								if(angles_0 == 0)
									angles_0 = Maths::pi_2<double>(); // TEMP HACK

								const std::string screenshot_filename = "screenshot_" + StringUtils::convertByteArrayToHexString(data, NUM_BYTES) + ".jpg";
								screenshot_path = PlatformUtils::getTempDirPath() + "/" + screenshot_filename;

								packet.writeStringLengthFirst("takescreenshot");
								packet.writeDouble(cam_x);
								packet.writeDouble(cam_y);
								packet.writeDouble(cam_z);
								packet.writeDouble(angles_0);
								packet.writeDouble(angles_1);
								packet.writeDouble(angles_2);
								packet.writeInt32(screenshot_width_px);
								packet.writeInt32(highlight_parcel_id);
								packet.writeStringLengthFirst(screenshot_path);
							}
							else if(request_type == Protocol::TileScreenShotRequest)
							{
								const int tile_x = socket->readInt32();
								const int tile_y = socket->readInt32();
								const int tile_z = socket->readInt32();

								conPrint("tile: (" + toString(tile_x) + ", " + toString(tile_y) + ", " + toString(tile_z) + ")");

								const std::string screenshot_filename = "tile_" + toString(tile_x) + "_" + toString(tile_y) + "_" + toString(tile_z) + "_" + StringUtils::convertByteArrayToHexString(data, NUM_BYTES) + ".jpg";
								screenshot_path = PlatformUtils::getTempDirPath() + "/" + screenshot_filename;

								packet.writeStringLengthFirst("takemapscreenshot");
								packet.writeInt32(tile_x);
								packet.writeInt32(tile_y);
								packet.writeInt32(tile_z);
								packet.writeStringLengthFirst(screenshot_path);
							}
							else
								throw glare::Exception("invalid request type.");

							time_since_last_shot_request.reset();

							if(process == NULL)
							{
								// Command a gui_client process to take the screenshot

								const std::string gui_client_path = FileUtils::getDirectory(PlatformUtils::getFullPathToCurrentExecutable()) + "/gui_client.exe";

								std::vector<std::string> command_line_args;
								command_line_args.push_back(gui_client_path);
								command_line_args.push_back("-h");
								command_line_args.push_back(server_hostname);
								command_line_args.push_back("--screenshotslave");

								process = new glare::Process(gui_client_path, command_line_args);

								conPrint("Connecting to Substrata GUI process via socket...");

								// Establish socket connection to gui process
								while(1)
								{
									try
									{
										to_gui_socket = new MySocket("localhost", 34534);
										to_gui_socket->setUseNetworkByteOrder(false);
										break;
									}
									catch(glare::Exception& e)
									{
										conPrint("Excep while connecting to local GUI process: " + e.what() + " waiting to try again...");
										PlatformUtils::Sleep(1000);
									}
								}

								conPrint("Connected to Substrata GUI process via socket.");
							}

							to_gui_socket->write(packet.buf.data(), packet.buf.size());

							// Wait for response from GUI (will be sent when screenshot is done)
							bool got_result = false;
							std::string from_gui_error_msg;
							int result = 0;
							while(!got_result)
							{
								if(to_gui_socket->readable(1.0))
								{
									result = to_gui_socket->readInt32(); // 0 = success
									from_gui_error_msg = to_gui_socket->readStringLengthFirst(10000);
									conPrint("Received result " + toString(result) + " from GUI process.");
									got_result = true;
								}

								// Print out any stdout from GUI client
								while(process->isStdOutReadable())
								{
									const std::string output = process->readStdOut();
									std::vector<std::string> lines = ::split(output, '\n');
									for(size_t i=0; i<lines.size(); ++i)
										if(!isAllWhitespace(lines[i]))
											conPrint("\tGUI_CLIENT> " + lines[i]);
								}
							}

							if(result == 0) // If success:
							{
								conPrint("GUI process took screenshot successfully.");

								// Load generated screenshot
								const std::string screenshot_data = FileUtils::readEntireFile(screenshot_path);

								socket->writeUInt32(Protocol::ScreenShotSucceeded);

								// Send it to the server
								conPrint("Sending screenshot " + screenshot_path + " to server.");
								socket->writeUInt64(screenshot_data.length());
								socket->writeData(screenshot_data.data(), screenshot_data.length());
								conPrint("Sent screenshot to server.");
							}
							else
							{
								conPrint("GUI process failed to take screenshot: " + from_gui_error_msg);

								socket->writeUInt32(12341234); // Just write some value != Protocol::ScreenShotSucceeded
							}

							time_since_last_shot_request.reset();
						}
						else if(request_type == Protocol::KeepAlive)
						{
							conPrint("Received keepalive");
						}
						else
						{
							throw glare::Exception("unknown request type: " + toString(request_type));
						}
					} // end if socket was readable

					// If it has been a while since we got a screenshot request from the server, terminate the GUI process and close the socket connection to it.
					if(time_since_last_shot_request.elapsed() > 60.0)
					{
						if(process != NULL)
						{
							conPrint("A while has elapsed since receiving a screenshot command, terminating GUI process.");

							conPrint("Closing socket..");
							to_gui_socket->ungracefulShutdown();
							to_gui_socket = NULL;
							conPrint("Closed socket..");

							conPrint("Terminating GUI process..");
							process->terminateProcess();

							std::string output, err_output;
							process->readAllRemainingStdOutAndStdErr(output, err_output);
							//conPrint("GUI_CLIENT> " + output);
							//conPrint("GUI_CLIENT> " + err_output);
							conPrint("Gui client process terminated.");

							delete process;
							process = NULL;
						}
					}
				} // End while(1) loop
			}
			catch(glare::Exception& e)
			{
				// Connection failed.
				conPrint("Error: " + e.what());
				PlatformUtils::Sleep(10000);
			}
		} // End while screenshot bot should keep running:

		return 0;
	}
	catch(glare::Exception& e)
	{
		stdErrPrint("Error: " + e.what());
		return 1;
	}
}
