/*=====================================================================
LoadScriptTask.cpp
------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "LoadScriptTask.h"


#include "ThreadMessages.h"
#if !defined(EMSCRIPTEN)
#include "WinterShaderEvaluator.h"
#endif
#include <ConPrint.h>
#include <PlatformUtils.h>


LoadScriptTask::LoadScriptTask()
{}


LoadScriptTask::~LoadScriptTask()
{}


void LoadScriptTask::run(size_t thread_index)
{
	// conPrint("LoadScriptTask: Loading script...");

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
}
