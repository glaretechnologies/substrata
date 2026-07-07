/*=====================================================================
LLMThread.h
-----------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <ai/LLMClient.h>
#include <MessageableThread.h>
#include <Platform.h>
#include <WeakReference.h>
#include <AtomicInt.h>
#include <string>
class Server;
class ChatBot;
class SimpleCredentials;


// Append a user chat message to the chat history, and send to LLM server.
class SendAIChatPostContent : public ThreadMessage
{
public:
	SendAIChatPostContent() : should_send_to_server_immediately(true)  {}

	std::string message; // Unescaped.

	bool should_send_to_server_immediately;
};


// Append a tool call function result to the chat history, and send to LLM server.
class SendAIChatToolCallResult : public ThreadMessage
{
public:
	SendAIChatToolCallResult() : should_send_to_server_immediately(true)  {}

	ToolCallResult tool_call_result;

	bool should_send_to_server_immediately;
};


// The LLM has responded with a message.
class AIChatResponseDataMessage : public ThreadMessage
{
public:
	std::string message;
	WeakReference<ChatBot> chatbot;
};


// The LLM has responded with a tool function-call message.
class AIToolFunctionCallMessage : public ThreadMessage
{
public:
	Reference<AIToolFunctionCalls> calls;
	WeakReference<ChatBot> chatbot;
};


// The LLM has finished streaming a response.
class AIChatResponseDoneMessage : public ThreadMessage
{
public:
	WeakReference<ChatBot> chatbot;
};


/*=====================================================================
LLMThread
---------
Handles the client side of communication with a LLM cloud server.
=====================================================================*/
class LLMThread : public LLMClientHandlerInterface, public MessageableThread
{
public:
	LLMThread(const std::string& AI_model_id, const ToolFunctionsSpec& tool_functions, const std::string& base_prompt);
	virtual ~LLMThread();

	virtual void doRun() override;

	virtual void kill() override;


	// LLMClientHandlerInterface interface:
	virtual void responseDataReceived(const std::string& data) override;
	virtual void toolFunctionCallsReceived(const Reference<AIToolFunctionCalls>& function_calls) override;
	virtual void responseDone() override;

	ThreadSafeQueue<ThreadMessageRef>* out_msg_queue;

	const SimpleCredentials* credentials;
	WeakReference<ChatBot> chatbot; // ChatBot has a strong reference to this ob, so use a weak reference to avoid cycles.

private:
	std::string AI_model_id;
	ToolFunctionsSpec tool_functions;
	std::string base_prompt;

	glare::AtomicInt should_quit;
};
