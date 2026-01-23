/*=====================================================================
LLMThread.h
-----------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <MessageableThread.h>
#include <Platform.h>
#include <MyThread.h>
#include <WeakReference.h>
#include <EventFD.h>
#include <networking/MySocket.h>
#include <networking/HTTPClient.h>
#include <SocketBufferOutStream.h>
#include <Vector.h>
#include <BufferInStream.h>
#include <AtomicInt.h>
#include <Timer.h>
#include <string>
#include <deque>
class Server;
class ChatBot;
struct AIModel;
struct ServerCredentials;


struct AIModel
{
	std::string id_string; // A global id, e.g. "together.ai/meta-llama/Llama-3.3-70B-Instruct-Turbo"
	std::string api_id_string; // The ID passed to the API, e.g. "meta-llama/Llama-3.3-70B-Instruct-Turbo"
	std::string name; // Name shown to user, e.g. "Llama-3.3-70B-Instruct-Turbo [together.ai]"
	std::string description; // Description shown to user, e.g. "Meta's open source model, hosted by together.ai"

	std::string api_domain; // e.g. api.openai.com
	std::string api_key_credential_name; // e.g. "openai_api_key"
};


class ToolFunctionArg : public ThreadSafeRefCounted
{
public:
};


class ToolFunctionCall : public ThreadSafeRefCounted
{
public:
	std::string call_id;
	std::string function_name;
	std::vector<Reference<ToolFunctionArg>> args;
};



struct LLMChatMessage
{
	enum Role
	{
		Role_User = 0,
		Role_Assistant = 1,
		Role_Tool = 2
	};
	Role role;
	std::string tool_call_id; // For Role_Tool
	std::string tool_call_name; // For Role_Tool
	std::string content; // Unescaped.

	std::vector<Reference<ToolFunctionCall>> tool_calls; // For Role_Assistant
};


// Append a user chat message to the chat history, and send to LLM server.
class SendAIChatPostContent : public ThreadMessage
{
public:
	std::string message; // Unescaped.
};


// Append a tool call function result to the chat history, and send to LLM server.
class SendAIChatToolCallResult : public ThreadMessage
{
public:
	SendAIChatToolCallResult() : should_send_to_server_immediately(true)  {}

	std::string tool_call_id; // For Role_Tool
	std::string tool_call_name; // For Role_Tool
	std::string content; // Unescaped.

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
	std::vector<Reference<ToolFunctionCall>> calls;
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
class LLMThread : public MessageableThread, public HTTPClient::StreamingDataHandler
{
public:
	LLMThread(const std::string& AI_model_id);
	virtual ~LLMThread();

	virtual void doRun() override;

	virtual void kill() override;


	// HTTPClient::StreamingDataHandler interface
	virtual void handleData(ArrayRef<uint8> chunk, const HTTPClient::ResponseInfo& response_info) override;

	ThreadSafeQueue<ThreadMessageRef>* out_msg_queue;

	const ServerCredentials* credentials;
private:
	void sendChatRequestToLLMServer(const AIModel& cur_ai_model, Reference<HTTPClient>& http_client);
	Reference<HTTPClient> createHTTPClient(const AIModel& cur_ai_model);
	void trimChatMessageHistory();

	glare::AtomicInt should_quit;

	int next_nonempty_line_start;
	int newline_search_pos;
	std::vector<char> http_response_data; // We will stream http response data into here, and search for newlines with next_nonempty_line_start and newline_search_pos

	std::string AI_model_id;
	bool cur_ai_model_is_anthropic;

	std::deque<LLMChatMessage> chat_messages; // Chat message history
public:
	std::string base_prompt_json_escaped;
	std::string tools_json;

	LLMChatMessage current_assistant_response; // Accumulated complete response from the LLM

	WeakReference<ChatBot> chatbot; // ChatBot has a strong reference to this ob, so use a weak reference to avoid cycles.
};
