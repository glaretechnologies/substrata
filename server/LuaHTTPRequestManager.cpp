/*=====================================================================
LuaHTTPRequestManager.cpp
-------------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "LuaHTTPRequestManager.h"


#include "LuaHTTPWorkerThread.h"
#include "Server.h"
#include "../shared/LuaScriptEvaluator.h"


LuaHTTPRequestManager::LuaHTTPRequestManager(Server* server_)
:	server(server_)
{
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


void LuaHTTPRequestManager::think()
{
	Lock lock(result_queue.getMutex());
	
	while(result_queue.unlockedNonEmpty())
	{
		Reference<LuaHTTPRequestResult> result = result_queue.unlockedDequeue();

		Reference<LuaHTTPRequest> request = result->request;

		Reference<LuaScriptEvaluator> script_evaluator = request->lua_script_evaluator.upgradeToStrongRef();
		if(script_evaluator)
		{
			WorldStateLock world_state_lock(server->world_state->mutex);

			if(!result->exception_msg.empty())
			{
				// Call the script onError function
				script_evaluator->doOnError(request->onError_ref, 
					/*error code=*/1, 
					result->exception_msg, // error description
					world_state_lock
				);
			}
			else
			{
				// Call the script onDone function
				script_evaluator->doOnDone(request->onDone_ref, result, world_state_lock);
			}
		}
	}
}


void LuaHTTPRequestManager::enqueueHTTPRequest(Reference<LuaHTTPRequest> request)
{
	bool http_requests_enabled;
	{
		Lock lock(server->world_state->mutex);
		http_requests_enabled = BitUtils::isBitSet(server->world_state->feature_flag_info.feature_flags, ServerAllWorldsState::LUA_HTTP_REQUESTS_FEATURE_FLAG);
	}

	if(http_requests_enabled)
		request_queue.enqueue(request);
}


void LuaHTTPRequestManager::enqueueResult(Reference<LuaHTTPRequestResult> result)
{
	result_queue.enqueue(result);
}
