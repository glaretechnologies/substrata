/*=====================================================================
LuaHTTPRequestManager.h
-----------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "../shared/UserID.h"
#include "../shared/RateLimiter.h"
#include "../lua/LuaVM.h"
#include <networking/HTTPClient.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/Reference.h>
#include <utils/ThreadManager.h>
#include <utils/WeakReference.h>
#include <utils/Vector.h>
#include <utils/CircularBuffer.h>
#include <vector>
#include <string>
#include <unordered_map>


class SubstrataLuaScript;
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

	WeakReference<SubstrataLuaScript> sub_lua_script;
	LuaVM::LuaValueRef onDone_ref;
	LuaVM::LuaValueRef onError_ref;
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

	double return_time;

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

	// Called on main thread
	void think();

	// Called on main thread
	void enqueueHTTPRequest(Reference<LuaHTTPRequest> request);

	// Called from worker threads.
	void enqueueResult(Reference<LuaHTTPRequestResult> result);

	// Set before submitting requests; existing per-user RateLimiters retain their original allowance.
	int max_requests_per_period;
	int max_delayed_results;

	ThreadSafeQueue<Reference<LuaHTTPRequest>> request_queue;
private:
	ThreadManager thread_manager;
	ThreadSafeQueue<Reference<LuaHTTPRequestResult>> result_queue;
	Server* server;

	std::unordered_map<UserID, Reference<RateLimiter>, UserIDHasher> rate_limiters;

	js::Vector<Reference<LuaHTTPRequestResult>, 16> temp_results;

	CircularBuffer<Reference<LuaHTTPRequestResult>> delayed_result_queue;
};
