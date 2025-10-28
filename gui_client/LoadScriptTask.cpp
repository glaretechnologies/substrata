/*=====================================================================
LoadScriptTask.cpp
------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "LoadScriptTask.h"


#include "ThreadMessages.h"
#include "WinterShaderEvaluator.h"
#include <ConPrint.h>
#include <PlatformUtils.h>
#include <tracy/Tracy.hpp>


LoadScriptTask::LoadScriptTask()
{}


LoadScriptTask::~LoadScriptTask()
{}


void LoadScriptTask::run(size_t thread_index)
{
	// conPrint("LoadScriptTask: Loading script...");
	ZoneScopedN("LoadScriptTask"); // Tracy profiler

	try
	{
#if EMSCRIPTEN
		throw glare::Exception("Winter not supported in emscripten");
#else

		Reference<ScriptLoadedThreadMessage> msg = new ScriptLoadedThreadMessage();

		msg->script = script_content;
		msg->script_evaluator = new WinterShaderEvaluator(base_dir_path, script_content);

		result_msg_queue->enqueue(msg);
#endif
	}
	catch(glare::Exception& e)
	{
		result_msg_queue->enqueue(new LogMessage("Error while loading script: " + e.what()));
	}
	catch(std::bad_alloc&)
	{
		result_msg_queue->enqueue(new LogMessage("Error while loading script: failed to allocate mem (bad_alloc)"));
	}
}
