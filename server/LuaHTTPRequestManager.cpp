/*=====================================================================
LuaHTTPRequestManager.cpp
-------------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "LuaHTTPRequestManager.h"


#include "LuaHTTPWorkerThread.h"
#include "Server.h"
#include "../shared/SubstrataLuaScript.h"


LuaHTTPRequestManager::LuaHTTPRequestManager(Server* server_)
:	max_requests_per_period(10),
	max_delayed_results(1000),
	server(server_)
{
	if(!server->config.do_lua_http_request_rate_limiting)
		conPrint("Lua HTTP request rate limiting is disabled.");

	for(int i=0; i<4; ++i)
	{
		thread_manager.addThread(new LuaHTTPWorkerThread(this));
	}
}


LuaHTTPRequestManager::~LuaHTTPRequestManager()
{
	const size_t num_threads = thread_manager.getNumThreads();
	for(size_t i=0; i<num_threads; ++i)
		request_queue.enqueue(nullptr); // Send null request to tell thread to quit.

	thread_manager.killThreadsBlocking();
}


// Called on main thread
void LuaHTTPRequestManager::think()
{
	// Remove all queued items and then process them, so we don't get caught in an infinite loop if handlers add new requests.
	result_queue.dequeueAnyQueuedItems(/*items out=*/temp_results);

	for(size_t i=0; i<temp_results.size(); ++i)
	{
		Reference<LuaHTTPRequestResult> result = temp_results[i];

		Reference<LuaHTTPRequest> request = result->request;

		Reference<SubstrataLuaScript> sub_lua_script = request->sub_lua_script.upgradeToStrongRef();
		if(sub_lua_script)
		{
			WorldStateLock world_state_lock(server->world_state->mutex);

			if(!result->exception_msg.empty())
			{
				// Call the script onError function
				sub_lua_script->doOnError(request->onError_ref, 
					/*error code=*/result->error_code,
					result->exception_msg, // error description
					world_state_lock
				);
			}
			else
			{
				// Call the script onDone function
				sub_lua_script->doOnDone(request->onDone_ref, result, world_state_lock);
			}

			// We are done with the request now, so free references to the Lua handler functions.
			sub_lua_script->lua_script->freeRefToFunction(request->onDone_ref);
			sub_lua_script->lua_script->freeRefToFunction(request->onError_ref);
		}
	}
	temp_results.clear();


	// Process delayed_result_queue
	const double cur_time = Clock::getCurTimeRealSec();
	while(delayed_result_queue.nonEmpty() && (cur_time > delayed_result_queue.front()->return_time))
	{
		Reference<LuaHTTPRequestResult> result = delayed_result_queue.popAndReturnFront();
		Reference<SubstrataLuaScript> sub_lua_script = result->request->sub_lua_script.upgradeToStrongRef();
		if(sub_lua_script)
		{
			WorldStateLock world_state_lock(server->world_state->mutex);

			assert(!result->exception_msg.empty()); // Only rate-limit failures should be pushed onto the delayed_result_queue.
			if(!result->exception_msg.empty())
			{
				// Call the script onError function
				sub_lua_script->doOnError(result->request->onError_ref, 
					/*error code=*/result->error_code,
					result->exception_msg, // error description
					world_state_lock
				);
			}

			// We are done with the request now, so free references to the Lua handler functions.
			sub_lua_script->lua_script->freeRefToFunction(result->request->onDone_ref);
			sub_lua_script->lua_script->freeRefToFunction(result->request->onError_ref);
		}
	}
}


// Called on main thread
void LuaHTTPRequestManager::enqueueHTTPRequest(Reference<LuaHTTPRequest> request)
{
	bool can_enqueue_request;
	if(server->config.do_lua_http_request_rate_limiting)
	{
		// Look up rate limiter for this request
		RateLimiter* rate_limiter;
		auto res = rate_limiters.find(request->script_user_id);
		if(res == rate_limiters.end())
		{
			rate_limiter = new RateLimiter(/*period=*/300.0, /*max num in period=*/max_requests_per_period);
			rate_limiters.insert(std::make_pair(request->script_user_id, rate_limiter));
		}
		else
			rate_limiter = res->second.ptr();

		can_enqueue_request = rate_limiter->checkAddEvent(Clock::getCurTimeRealSec());
	}
	else // Else if rate limiting disabled:
		can_enqueue_request = true;

	if(can_enqueue_request)
	{
		request_queue.enqueue(request);
	}
	else
	{
		// Enqueue a ErrorCode_RateLimited result onto the delayed_result_queue, to be processed (onError_ref called) later - in 2 seconds.
		// We don't want to call onError_ref immediately or it can result in a busy loop if the onError function calls doHTTPGetRequestAsync again.

		// TODO: Limit pending rate-limited results per script_user_id so one user can't fill the global queue and cause other users' requests to fail.
		
		// Sanity check the queue size, throw an excep immediately if getting too big.
		if(delayed_result_queue.size() >= (size_t)max_delayed_results)
		{
			// Free the handler refs, since this request won't be queued.
			SubstrataLuaScript* sub_lua_script = request->sub_lua_script.getPtrIfAlive(); // Don't use upgradeToStrongRef as it can crash if this is called during SubstrataLuaScript constructor when refcount is zero.
			// Instead use getPtrIfAlive since this function is called on main thread from lua code.
			if(sub_lua_script)
			{
				sub_lua_script->lua_script->freeRefToFunction(request->onDone_ref);
				sub_lua_script->lua_script->freeRefToFunction(request->onError_ref);
			}
			conPrint("Too many rate-limited HTTP requests, not enqueuing result into delayed_result_queue");
			throw glare::Exception("Too many rate-limited HTTP requests");
		}

		Reference<LuaHTTPRequestResult> result = new LuaHTTPRequestResult();
		result->request = request;
		result->error_code = LuaHTTPRequestResult::ErrorCode_RateLimited;
		result->exception_msg = "Rate limited: too many HTTP requests in too short a period of time.";
		result->return_time = Clock::getCurTimeRealSec() + 2.0;
		delayed_result_queue.push_back(result);
	}
}


void LuaHTTPRequestManager::enqueueResult(Reference<LuaHTTPRequestResult> result)
{
	result_queue.enqueue(result);
}
