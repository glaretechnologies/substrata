/*=====================================================================
LuaHTTPRequestManager.h
-----------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include <networking/HTTPClient.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/Reference.h>
#include <utils/ThreadManager.h>
#include <utils/WeakReference.h>
#include <vector>
#include <string>


class LuaScriptEvaluator;
class Server;


class LuaHTTPRequest : public ThreadSafeRefCounted
{
public:
	std::string request_type; // GET or POST
	std::string URL;
	std::vector<std::string> additional_headers;

	WeakReference<LuaScriptEvaluator> lua_script_evaluator;
	int onDone_ref;
	int onError_ref;
};


class LuaHTTPRequestResult : public ThreadSafeRefCounted
{
public:
	Reference<LuaHTTPRequest> request;
	HTTPClient::ResponseInfo response;
	std::vector<uint8> data;
	std::string exception_msg;
};


/*=====================================================================
LuaHTTPRequestManager
---------------------
=====================================================================*/
class LuaHTTPRequestManager : public ThreadSafeRefCounted
{
public:
	LuaHTTPRequestManager(Server* server);
	~LuaHTTPRequestManager();

	void think();

	// Called on main thread
	void enqueueHTTPRequest(Reference<LuaHTTPRequest> request);

	// Called from worker threads.
	void enqueueResult(Reference<LuaHTTPRequestResult> result);

	ThreadSafeQueue<Reference<LuaHTTPRequest>> request_queue;
private:
	ThreadManager thread_manager;
	ThreadSafeQueue<Reference<LuaHTTPRequestResult>> result_queue;
	Server* server;
};
