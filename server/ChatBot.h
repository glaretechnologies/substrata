/*=====================================================================
ChatBot.h
---------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "../shared/UserID.h"
#include "../shared/WorldMaterial.h"
#include "../shared/Avatar.h"
#include "../shared/WorldStateLock.h"
#include <TimeStamp.h>
#include <ThreadSafeRefCounted.h>
#include <WeakRefCounted.h>
#include <Reference.h>
#include <Timer.h>
#include <OutStream.h>
#include <SocketBufferOutStream.h>
#include <InStream.h>
#include <DatabaseKey.h>
#include <map>


class RandomAccessInStream;
class LLMThread;
class Server;
class ServerWorldState;
class ToolFunctionCall;


class ChatBotToolFunction : public ThreadSafeRefCounted
{
public:
	void writeToStream(RandomAccessOutStream& stream);

	static const int MAX_FUNCTION_NAME_SIZE = 1'000;
	static const int MAX_DESCRIPTION_NAME_SIZE = 10'000;
	static const int MAX_RESULT_CONTENT_SIZE = 100'000;

	std::string function_name;
	std::string description;
	std::string result_content;
};

void readChatBotToolFunctionFromStream(RandomAccessInStream& stream, ChatBotToolFunction& func);



/*=====================================================================
ChatBot
-------
A chatbot that uses LLMs for thinking.
=====================================================================*/
class ChatBot : public WeakRefCounted
{
public:
	ChatBot();
	~ChatBot();

	// Threadsafe, will be called from WorkerThreads but only when the world lock is held.
	Reference<LLMThread> createLLMThread(Server* server, WorldStateLock& lock);

	// A user/avatar sent a chat message, which this bot is in range of.
	// Threadsafe, will be called from WorkerThreads but only when the world lock is held.
	void processHeardChatMessage(WorldStateLock& lock, const std::string& msg, AvatarRef sender_avatar, const std::string& avatar_name, Server* server);

	// Handle some (partial, streaming) chat data coming back from an LLM cloud server.
	// Called from the main thread only, in server.cpp.
	void handleLLMChatResponse(const std::string& msg, Server* server, WorldStateLock& world_lock);

	// Called from the main thread only, in server.cpp.
	void handleLLMChatResponseDone(Server* server, WorldStateLock& world_lock);

	// Called from the main thread only, in server.cpp.
	void handleLLMToolFunctionCall(const std::vector<Reference<ToolFunctionCall>>& calls, Server* server, WorldStateLock& world_lock);

	// Called from the main thread only, in server.cpp.
	struct ThinkResults
	{
		Reference<LLMThread> llm_thread_being_killed;
	};
	ThinkResults think(Server* server, WorldStateLock& world_lock);

	void writeToStream(RandomAccessOutStream& stream);

	uint64 id;

	UserID owner_id;
	TimeStamp created_time;

	static const int MAX_NAME_SIZE = 200;
	std::string name;

	AvatarSettings avatar_settings;

	uint32 flags;

	Vec3d pos;
	float heading;

	static const int MAX_BASE_PROMPT_SIZE = 10000;
	std::string base_prompt; // Actually custom_prompt

	std::map<std::string, Reference<ChatBotToolFunction>> info_tool_functions; // Map from function name to ChatBotToolFunction ref.  Tool functions that the LLM can call.

	DatabaseKey database_key;

	Reference<LLMThread> llm_thread; // Thread that does the communication with the LLM cloud server.

	ServerWorldState* world; // world the chatbot is in.

	UID avatar_uid; // UID of the avatar of the chatbot.
	Reference<Avatar> avatar; // avatar of the chatbot.

	Reference<Avatar> chatting_with_av; // Avatar we are chatting with, may be null if not chatting with anyone.

private:
	void sendChatMessage(const string_view message, Server* server);

	// The response from the LLM is streamed back from the cloud server, however we only want to chat in complete sentences, not in fragments of sentences.  So we will scan the accumulated response for sentence ends.
	size_t next_sentence_start_index; // Index into total_llm_response.  Index at which the next sentence starts, e.g. 1 place past end of last sentence.
	size_t next_sentence_search_pos; // Current search position in total_llm_response for a sentence end.
	std::string total_llm_response;

	Timer sentences_received_timer; // Once we have received a complete sentence from the LLVM server, start this timer.  When it has completed, send any queued sentences.

	Timer repeating_gesture_timer; // This will be unpaused and reset when a repeating gesture such as waving starts.  When it hits some elapsed time, we will stop the gesture.

	Timer time_since_last_interaction;

	SocketBufferOutStream scratch_packet;
};


typedef Reference<ChatBot> ChatBotRef;


void readChatBotFromStream(RandomAccessInStream& stream, ChatBot& chatbot);


struct ChatBotRefHash
{
	size_t operator() (const ChatBotRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};
