/*=====================================================================
LuaHTTPWorkerThread.cpp
-----------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "LuaHTTPWorkerThread.h"


#include "LuaHTTPRequestManager.h"
#include <networking/HTTPClient.h>
#include <utils/PlatformUtils.h>
#include <utils/ConPrint.h>
#include <utils/KillThreadMessage.h>


LuaHTTPWorkerThread::LuaHTTPWorkerThread(LuaHTTPRequestManager* manager_)
:	manager(manager_)
{
}


LuaHTTPWorkerThread::~LuaHTTPWorkerThread()
{
}


void LuaHTTPWorkerThread::doRun()
{
	PlatformUtils::setCurrentThreadName("LuaHTTPWorkerThread");

	try
	{
		while(1)
		{
			Reference<LuaHTTPRequest> request = manager->request_queue.dequeue();
			if(!request) // A null request means the thread should quit.
				return;

			Reference<LuaHTTPRequestResult> result = new LuaHTTPRequestResult();
			result->request = request;

			try
			{
				conPrint("Doing Lua HTTP Request to '" + request->URL + "'...");

				http_client = new HTTPClient();
				http_client->max_data_size = 1 << 24; // 16 MB
				http_client->max_socket_buffer_size = 1 << 16;

				if(request->request_type == "GET")
				{
					result->response = http_client->downloadFile(request->URL, /*data_out=*/result->data);
				}
				else if(request->request_type == "POST")
				{
					result->response = http_client->sendPost(request->URL, request->post_content, request->content_type, /*data_out=*/result->data);
				}
				else
					runtimeCheckFailed("invalid request type");

				conPrint("Lua HTTP Request to '" + request->URL + "' done.");
			}
			catch(glare::Exception& e)
			{
				conPrint("Exception while doing Lua HTTP Request to '" + request->URL + "': " + e.what());

				result->error_code = LuaHTTPRequestResult::ErrorCode_Other;
				result->exception_msg = e.what();
				result->data.clear();
			}

			http_client = nullptr;

			manager->enqueueResult(result);
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("LuaHTTPWorkerThread: glare::Exception: " + e.what());
	}
	catch(std::exception& e) // catch std::bad_alloc etc..
	{
		conPrint(std::string("LuaHTTPWorkerThread: Caught std::exception: ") + e.what());
	}
}


// Called from other threads
void LuaHTTPWorkerThread::kill()
{
	Reference<HTTPClient> http_client_ = this->http_client;
	if(http_client_)
		http_client_->kill();
}
