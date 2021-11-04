/*=====================================================================
BackupBot.h
-----------
Copyright Glare Technologies Limited 2021 -

Backs up resources from the substrata server.
=====================================================================*/


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


	// Connect to substrata server
	try
	{
		const std::string server_hostname = "localhost";
		//const std::string server_hostname = "substrata.info";
		
		HTTPClient http_client;
		const int server_port = 7600;

		std::string response_data;
		HTTPClient::ResponseInfo response_info = http_client.downloadFile(server_hostname + "/list_resources", response_data);
		if(response_info.response_code != 200)
			throw glare::Exception("response_info.response_code was not 200.");

		//conPrint("Got resource list: " + response_data);
		//FileUtils::writeEntireFileTextMode("resource_urls.txt", response_data);

		const std::vector<std::string> URL_and_filenames = StringUtils::splitIntoLines(response_data);
		if(URL_and_filenames.size() % 2 != 0)
			throw glare::Exception("Expected even number of entries in URL_and_filenames.");

		for(size_t i=0; i<URL_and_filenames.size(); i += 2)
		{
			try
			{
				const std::string URL      = URL_and_filenames[i];
				const std::string filename = URL_and_filenames[i + 1];

				if(!FileUtils::isPathSafe(filename))
					throw glare::Exception("Filename '" + filename + "' is not safe.");

				const std::string local_path = "d:/substrata_stuff/resources_backup/" + filename;

				if(!FileUtils::fileExists(local_path))
				{
					conPrint(toString(i) + "/" + toString(URL_and_filenames.size() / 2) + ": Downloading " +  URL + "...");

					HTTPClient http_client2;
					response_info = http_client2.downloadFile(server_hostname + "/resource/" + URL, response_data);
					if(response_info.response_code == 200)
					{
						FileUtils::writeEntireFile(local_path, response_data);
						conPrint("Saved '" + URL + "'.");
					}
					else
					{
						conPrint("Received non-200 code for URL '" + URL + "'.");
					}
				}
			}
			catch(glare::Exception& e)
			{
				conPrint("Error while downloading resource: " + e.what());
			}
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("Error: " + e.what());
		return 1;
	}

	return 0;
}
