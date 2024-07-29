/*=====================================================================
LuaHTTPRequestManager.h
-----------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "../shared/UserID.h"
#include "../shared/RateLimiter.h"
#include <networking/HTTPClient.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/Reference.h>
#include <utils/ThreadManager.h>
#include <utils/WeakReference.h>
#include <vector>
#include <string>
#include <unordered_map>


class LuaScriptEvaluator;
class Server;


class LuaHTTPRequest : public ThreadSafeRefCounted
{
public:
	UserID script_user_id;

	std::string request_type; // GET or POST
	std::string URL;
	std::string post_content; // For POST
	std::string content_type; // For POST
	std::vector<std::string> additional_headers;

	WeakReference<LuaScriptEvaluator> lua_script_evaluator;
	int onDone_ref;
	int onError_ref;
};


class LuaHTTPRequestResult : public ThreadSafeRefCounted
{
public:
	LuaHTTPRequestResult() : error_code(0) {}

	Reference<LuaHTTPRequest> request;
	HTTPClient::ResponseInfo response;
	std::vector<uint8> data;
	
	std::string exception_msg;
	int error_code;

	enum ErrorCode
	{
		ErrorCode_OK = 0,
		ErrorCode_Other = 1,
		ErrorCode_RateLimited = 2
	};
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

	std::unordered_map<UserID, Reference<RateLimiter>, UserIDHasher> rate_limiters;
};
