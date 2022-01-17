/*=====================================================================
BackupBot.h
-----------
Copyright Glare Technologies Limited 2021 -

Backs up resources from the substrata server.
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
#include <Exception.h>
#include <networking/HTTPClient.h>
#include <tls.h>


int main(int argc, char* argv[])
{
	Clock::init();
	Networking::createInstance();
	PlatformUtils::ignoreUnixSignals();
	OpenSSL::init();
	TLSSocket::initTLS();



	try
	{

		// Create and init TLS client config
		struct tls_config* client_tls_config = tls_config_new();
		if(!client_tls_config)
			throw glare::Exception("Failed to initialise TLS (tls_config_new failed)");
		tls_config_insecure_noverifycert(client_tls_config); // TODO: Fix this, check cert etc..
		tls_config_insecure_noverifyname(client_tls_config);

		//const std::string server_hostname = "localhost";
		const std::string server_hostname = "substrata.info";
		const int server_port = 7600;
		
		HTTPClient http_client;

		std::string response_data;
		HTTPClient::ResponseInfo response_info = http_client.downloadFile(server_hostname + "/list_resources", response_data);
		if(response_info.response_code != 200)
			throw glare::Exception("response_info.response_code was not 200.");

		//conPrint("Got resource list: " + response_data);
		FileUtils::writeEntireFileTextMode("resource_urls.txt", response_data);

		const std::vector<std::string> URL_and_filenames_on_server = StringUtils::splitIntoLines(response_data);
		if(URL_and_filenames_on_server.size() % 2 != 0)
			throw glare::Exception("Expected even number of entries in URL_and_filenames.");



		MySocketRef plain_socket = new MySocket(server_hostname, server_port);
		plain_socket->setUseNetworkByteOrder(false);

		TLSSocketRef socket = new TLSSocket(plain_socket, client_tls_config, server_hostname);

		conPrint("DownloadResourcesThread: Connected to " + server_hostname + ":" + toString(server_port) + "!");

		socket->writeUInt32(Protocol::CyberspaceHello); // Write hello
		socket->writeUInt32(Protocol::CyberspaceProtocolVersion); // Write protocol version
		socket->writeUInt32(Protocol::ConnectionTypeDownloadResources); // Write connection type

		// Read hello response from server
		const uint32 hello_response = socket->readUInt32();
		if(hello_response != Protocol::CyberspaceHello)
			throw glare::Exception("Invalid hello from server: " + toString(hello_response));

		const int MAX_STRING_LEN = 10000;

		// Read protocol version response from server
		const uint32 protocol_response = socket->readUInt32();
		if(protocol_response == Protocol::ClientProtocolTooOld)
		{
			const std::string msg = socket->readStringLengthFirst(MAX_STRING_LEN);
			throw glare::Exception(msg);
		}
		else if(protocol_response == Protocol::ClientProtocolOK)
		{}
		else
			throw glare::Exception("Invalid protocol version response from server: " + toString(protocol_response));


		size_t start_i = 0;
		while(start_i <= URL_and_filenames_on_server.size())
		{
			conPrint(toString(start_i) + " / " + toString(URL_and_filenames_on_server.size()));
			std::vector<std::string> URL_and_filenames_to_get;
			for(; start_i<URL_and_filenames_on_server.size(); start_i += 2)
			{
				const std::string URL      = URL_and_filenames_on_server[start_i];
				const std::string filename = URL_and_filenames_on_server[start_i + 1];

				if(URL_and_filenames_to_get.size() > 200)
					break;

				if(FileUtils::isPathSafe(filename))
				{
					const std::string local_path = "d:/substrata_stuff/resources_backup/" + filename;

					if(!FileUtils::fileExists(local_path))
					{
						URL_and_filenames_to_get.push_back(URL);
						URL_and_filenames_to_get.push_back(filename);
					}
				}
			}

			if(URL_and_filenames_to_get.empty())
				break;



			socket->writeUInt32(Protocol::GetFiles);
			socket->writeUInt64(URL_and_filenames_to_get.size() / 2); // Write number of files to get

			for(size_t i=0; i<URL_and_filenames_to_get.size(); i += 2)
			{
				const std::string URL      = URL_and_filenames_to_get[i];
				socket->writeStringLengthFirst(URL);
			}

			// Read reply, which has an error code for each resource download.
			for(size_t i=0; i<URL_and_filenames_to_get.size(); i += 2)
			{
				const std::string URL      = URL_and_filenames_to_get[i];
				const std::string filename = URL_and_filenames_to_get[i + 1];

				const uint32 result = socket->readUInt32();
				if(result == 0) // If OK:
				{
					// Download resource
					const uint64 file_len = socket->readUInt64();
					if(file_len > 0)
					{
						// TODO: cap length in a better way
						if(file_len > 1000000000)
							throw glare::Exception("downloaded file too large (len=" + toString(file_len) + ").");

						std::vector<uint8> buffer(file_len);
						socket->readData(buffer.data(), file_len); // Just read entire file.
					
						const std::string local_path = "d:/substrata_stuff/resources_backup/" + filename;

						// Write downloaded file to disk, clear in-mem buffer.
						try
						{
							FileUtils::writeEntireFile(local_path, buffer);


							conPrint("Wrote downloaded file to '" + local_path + "'. (len=" + toString(file_len) + ") ");
						}
						catch(glare::Exception& e)
						{
							conPrint("Error while writing file to '" + local_path + "': " + e.what());
						}
					}
				}
				else
				{
					conPrint("DownloadResourcesThread: Server couldn't send file '" + URL + "' (Result=" + toString(result) + ")");
				}
			}
		}




		//	try
		//	{
		//		const std::string URL      = URL_and_filenames[i];
		//		const std::string filename = URL_and_filenames[i + 1];

		//		if(!FileUtils::isPathSafe(filename))
		//			throw glare::Exception("Filename '" + filename + "' is not safe.");

		//		const std::string local_path = "d:/substrata_stuff/resources_backup/" + filename;

		//		if(!FileUtils::fileExists(local_path))
		//		{
		//			conPrint(toString(i) + "/" + toString(URL_and_filenames.size() / 2) + ": Downloading " +  URL + "...");

		//			HTTPClient http_client2;
		//			response_info = http_client2.downloadFile(server_hostname + "/resource/" + URL, response_data);
		//			if(response_info.response_code == 200)
		//			{
		//				FileUtils::writeEntireFile(local_path, response_data);
		//				conPrint("Saved '" + URL + "'.");
		//			}
		//			else
		//			{
		//				conPrint("Received non-200 code for URL '" + URL + "'.");
		//			}
		//		}
		//	}
		//	catch(glare::Exception& e)
		//	{
		//		conPrint("Error while downloading resource: " + e.what());
		//	}
		//}
	}
	catch(glare::Exception& e)
	{
		conPrint("Error: " + e.what());
		return 1;
	}

	return 0;
}
