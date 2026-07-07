/*=====================================================================
LLMThread.cpp
-------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "LLMThread.h"


#include <ConPrint.h>
#include <Exception.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <KillThreadMessage.h>
#include <RuntimeCheck.h>
#include <SimpleCredentials.h>


LLMThread::LLMThread(const std::string& AI_model_id_, const ToolFunctionsSpec& tool_functions_, const std::string& base_prompt_)
:	AI_model_id(AI_model_id_),
	tool_functions(tool_functions_),
	base_prompt(base_prompt_)
{
}


LLMThread::~LLMThread()
{
}


void LLMThread::doRun()
{
	try
	{
		PlatformUtils::setCurrentThreadNameIfTestsEnabled("LLMThread");

		std::vector<AIModel> models;
		{
			AIModel model;
			model.id_string = "openai/gpt-4o";
			model.api_id_string = "gpt-4o";
			model.name = "GPT-4o";
			model.description = "GPT-4o is OpenAI's versatile, high-intelligence flagship model.";
			model.api_domain = "api.openai.com";
			model.api_key_credential_name = "openai_api_key";
			models.push_back(model);
		}
		{
			AIModel model;
			model.id_string = "openai/gpt-4o-mini";
			model.api_id_string = "gpt-4o-mini";
			model.name = "GPT-4o mini";
			model.description = "GPT-4o mini is OpenAI's fast, affordable small model for focused tasks.";
			model.api_domain = "api.openai.com";
			model.api_key_credential_name = "openai_api_key";
			models.push_back(model);
		}
		{
			AIModel model;
			model.id_string = "xai/grok-4";
			model.api_id_string = "grok-4";
			model.name = "Grok 4";
			model.description = "X.AI's latest and greatest flagship model, offering unparalleled performance in natural language, math and reasoning - the perfect jack of all trades.";
			model.api_domain = "api.x.ai";
			model.api_key_credential_name = "xai_api_key";
			models.push_back(model);
		}
	
		{
			AIModel model;
			model.id_string = "xai/grok-4-1-fast-non-reasoning";
			model.api_id_string = "grok-4-1-fast-non-reasoning";
			model.name = "Grok 4.1 Fast (Non-Reasoning)";
			model.description = "From xAI. A frontier multimodal model optimized specifically for high-performance agentic tool calling.";
			model.api_domain = "api.x.ai";
			model.api_key_credential_name = "xai_api_key";
			models.push_back(model);
		}

		AIModel cur_ai_model;
		for(size_t i=0; i<models.size(); ++i)
			if(models[i].id_string == this->AI_model_id)
				cur_ai_model = models[i];

		if(cur_ai_model.id_string.empty())
			throw glare::Exception("Failed to find AI model with id '" + this->AI_model_id + "'");

		Reference<LLMClient> llm_client = new LLMClient(cur_ai_model, tool_functions, base_prompt, credentials, /*handler=*/this);
		llm_client->max_num_messages = 50;

		js::Vector<ThreadMessageRef, 16> temp_messages;

		while(!should_quit)
		{
			try
			{
				this->getMessageQueue().dequeueAllQueuedItemsBlocking(temp_messages);

				// Handle messages
				for(size_t i=0; i<temp_messages.size(); ++i)
				{
					ThreadMessageRef msg_ = temp_messages[i];

					if(dynamic_cast<KillThreadMessage*>(msg_.ptr()))
					{
						should_quit = true;
						break;
					}
					else if(dynamic_cast<SendAIChatPostContent*>(msg_.ptr()))
					{
						// Append a user chat message to the chat history, and send to LLM server.
						SendAIChatPostContent* msg = static_cast<SendAIChatPostContent*>(msg_.ptr());

						conPrint("LLMThread: received SendAIChatPostContent with message '" + msg->message + "'.");

						llm_client->appendChatMessage(msg->message, msg->should_send_to_server_immediately);
					}
					else if(dynamic_cast<SendAIChatToolCallResult*>(msg_.ptr()))
					{
						// Append a tool call function result to the chat history, and send to LLM server.
						SendAIChatToolCallResult* msg = static_cast<SendAIChatToolCallResult*>(msg_.ptr());

						conPrint("LLMThread: received SendAIChatToolCallResult.  (should_send_to_server_immediately=" + boolToString(msg->should_send_to_server_immediately) + ")");

						llm_client->appendToolCallResult(msg->tool_call_result, msg->should_send_to_server_immediately);
					}
				} // end for each message
			}
			catch(glare::Exception& e)
			{
				conPrint("LLMThread: glare::Exception: " + e.what());
				PlatformUtils::Sleep(1000);
			}
			catch(std::bad_alloc&)
			{
				conPrint("LLMThread: Caught std::bad_alloc.");
				PlatformUtils::Sleep(1000);
			}
		} // end while(!should_quit)
	}
	catch(glare::Exception& e)
	{
		conPrint("LLMThread: glare::Exception: " + e.what());
	}
}


void LLMThread::kill()
{
	should_quit = true;
}


void LLMThread::responseDataReceived(const std::string& data)
{
	// Send AIChatResponseDataMessage message
	AIChatResponseDataMessage* msg = new AIChatResponseDataMessage();
	msg->message = data;
	msg->chatbot = chatbot;
	out_msg_queue->enqueue(msg);
}


void LLMThread::toolFunctionCallsReceived(const Reference<AIToolFunctionCalls>& function_calls)
{
	// Send AIToolFunctionCallMessage message
	Reference<AIToolFunctionCallMessage> call_msg = new AIToolFunctionCallMessage();
	call_msg->chatbot = chatbot;
	call_msg->calls = function_calls;
	out_msg_queue->enqueue(call_msg);
}


void LLMThread::responseDone()
{
	// Send AIChatResponseDoneMessage message
	AIChatResponseDoneMessage* done_msg = new AIChatResponseDoneMessage();
	done_msg->chatbot = chatbot;
	out_msg_queue->enqueue(done_msg);
}
