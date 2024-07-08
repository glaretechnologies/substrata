/*=====================================================================
WebServerRequestHandlerTests.cpp
--------------------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "WebServerRequestHandlerTests.h"


#if BUILD_TESTS


#include "WebServerRequestHandler.h"
#include "RequestHandler.h"
#include "../server/ServerWorldState.h"
#include "../server/WorldCreation.h"
#include "WebDataStore.h"
#include <StringUtils.h>
#include <Parser.h>
#include <MemMappedFile.h>
#include <ConPrint.h>
#include <FileUtils.h>
#include <Exception.h>
#include <Lock.h>
#include <Clock.h>
#include <WebSocket.h>
#include <PlatformUtils.h>
#include <WebWorkerThread.h>
#include <networking/Networking.h>


#if 0

// Command line:
// C:\fuzz_corpus\webserverrequesthandler N:\substrata\trunk\testfiles\fuzz_seeds\webserverrequesthandler


static Reference<ServerAllWorldsState> test_world_state;
static std::string test_server_state_dir;
static Reference<WebDataStore> test_web_data_store;


extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	Clock::init();

	// Without initialising networking, we can't make any network connections during fuzzing, which is probably a good thing.
	// Networking::createInstance();

	try
	{
		const std::string substrata_appdata_dir = PlatformUtils::getOrCreateAppDataDirectory("Substrata");
		test_server_state_dir = substrata_appdata_dir + "/server_data";
		const std::string server_resource_dir = test_server_state_dir + "/server_resources";

		test_world_state = new ServerAllWorldsState();
		test_world_state->resource_manager = new ResourceManager(server_resource_dir);
		//test_world_state->readFromDisk(test_server_state_dir + "/server_state - Copy (56).bin");
		test_world_state->readFromDisk(test_server_state_dir + "/server_state.bin");

		// Insert a UserWebSession so we can test while being logged in.
		Reference<UserWebSession> session = new UserWebSession();
		session->created_time = TimeStamp::currentTime();
		session->user_id = UserID(0); // Admin user
		test_world_state->user_web_sessions["AAA"] = session;

		test_world_state->server_credentials.creds["coinbase_shared_secret_key"] = "AAA";
		test_world_state->server_credentials.creds["paypal_sandbox_business_email"] = "AAA";
		test_world_state->server_credentials.creds["paypal_sandbox_PDT_token"] = "AAA";

		WorldCreation::createParcelsAndRoads(test_world_state);

		test_web_data_store = new WebDataStore();
		test_web_data_store->public_files_dir = test_server_state_dir + "/webserver_public_files";
		test_web_data_store->webclient_dir = test_server_state_dir + "/webclient";
		test_web_data_store->screenshot_dir = test_server_state_dir + "/screenshots";
		test_web_data_store->loadAndCompressFiles();
	}
	catch(glare::Exception& )
	{
		assert(false);
	}

	return 0;
}


extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	try
	{
		web::RequestInfo request_info;
		request_info.tls_connection = true; // Set to true to avoid just getting the 302 redirect response for every request.
		request_info.fuzzing = true; // This will suppress some console output and some network requests.


		Parser parser((const char*)data, size);

		if(parser.parseCString("POST "))
			request_info.verb = "POST";
		else if(parser.parseCString("GET "))
			request_info.verb = "GET";
		else
			return 0;

		// Parse until we get to '|' or '!' - this will be the URI query.
		std::string URI;
		URI.reserve(64);
		while(!parser.eof() && parser.current() != '|' && parser.current() != '!')
		{
			URI.push_back(parser.current());
			parser.advance();
		}


		// Parse URI
		{
			Parser uri_parser(URI);

			// Get part of URI before CGI string (before ?)
			string_view path;
			uri_parser.parseToCharOrEOF('?', path);
			request_info.path = std::string(path);

			if(uri_parser.currentIsChar('?')) // Advance past '?' if present.
				uri_parser.advance();

			// Parse URL parameters (stuff after '?')
			while(!uri_parser.eof())
			{
				request_info.URL_params.resize(request_info.URL_params.size() + 1);

				// Parse key
				string_view escaped_key;
				if(!uri_parser.parseToChar('=', escaped_key))
					throw glare::Exception("Parser error while parsing URL params");
				request_info.URL_params.back().key = std::string(escaped_key);
				uri_parser.consume('=');

				// Parse value
				string_view escaped_value;
				uri_parser.parseToCharOrEOF('&', escaped_value);
				request_info.URL_params.back().value = std::string(escaped_value);

				if(uri_parser.currentIsChar('&'))
					uri_parser.consume('&');
				else
					break; // Finish parsing URL params.
			}
		}


		// Parse headers - has syntax like 
		// GET /somepath|header1:value1|header2:value2
		while(parser.currentIsChar('|'))
		{
			parser.consume('|');

			string_view key, value;
			parser.parseToCharOrEOF(':', key);
			if(parser.eof())
				return 0;
			parser.consume(':');

			parser.parseToOneOfCharsOrEOF('|', '!', value);

			web::Header header;
			header.key = key;
			header.value = value;
			request_info.headers.push_back(header);

			if(key == "Range")
				web::WorkerThread::parseRanges(value, request_info.ranges);
		}


		// Parse form content (when doing a POST), has syntax like
		// POST /someformpost!key1=value1&key2=value2
		if(request_info.verb == "POST")
		{
			if(parser.currentIsChar('!'))
			{
				parser.consume('!');
				const size_t post_content_start = parser.currentPos();

				// Parse form data
				while(!parser.eof())
				{
					request_info.post_fields.resize(request_info.post_fields.size() + 1);

					// Parse key
					string_view escaped_key;
					if(!parser.parseToChar('=', escaped_key))
						throw glare::Exception("Parser error while parsing URL params");
					request_info.post_fields.back().key = std::string(escaped_key);

					parser.consume('=');

					// Parse value
					string_view escaped_value;
					parser.parseToCharOrEOF('&', escaped_value);
					request_info.post_fields.back().value = std::string(escaped_value);

					if(parser.currentIsChar('&'))
						parser.consume('&');
					else
						break; // Finish parsing URL params.
				}

				request_info.post_content = std::string((const char*)data + post_content_start, (const char*)data + parser.currentPos());
			}
		}
		
		web::ReplyInfo reply_info;
		BufferOutStream out_stream;
		reply_info.socket = &out_stream;

		WebServerRequestHandler handler;
		handler.data_store = test_web_data_store.getPointer();
		handler.server = NULL; // NOTE: only used for websocket connections
		handler.world_state = test_world_state.getPointer();

		// handler.handleRequest(request_info, reply_info);

		// Handle the request, this time logged in as admin user
		web::Cookie cookie;
		cookie.key = "site-b"; // login session cookie key
		cookie.value = "AAA";
		request_info.cookies.push_back(cookie);

		if(false)
		{
			conPrint("--------------------------");
			conPrint(request_info.verb + " " + request_info.path);

			for(size_t i=0; i<request_info.URL_params.size(); ++i)
				conPrint("URL param: " + toString(request_info.URL_params[i].key) + ": " + request_info.URL_params[i].value.str());

			for(size_t i=0; i<request_info.headers.size(); ++i)
				conPrint("Header: " + toString(request_info.headers[i].key) + ": " + toString(request_info.headers[i].value));

			for(size_t i=0; i<request_info.post_fields.size(); ++i)
				conPrint("Post field: " + toString(request_info.post_fields[i].key) + ": " + request_info.post_fields[i].value.str());
			conPrint("--------------------------");
		}

		handler.handleRequest(request_info, reply_info);
	}
	catch(glare::Exception&)
	{
	}

	return 0;  // Non-zero return values are reserved for future use.
}
#endif // end if fuzzing


void WebServerRequestHandlerTests::test()
{
}


#endif // BUILD_TESTS
