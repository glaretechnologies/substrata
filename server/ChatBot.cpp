/*=====================================================================
ChatBot.cpp
-----------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "ChatBot.h"


#include "Server.h"
#include "ServerWorldState.h"
#include "LLMThread.h"
#include "../shared/MessageUtils.h"
#include "../shared/Protocol.h"
#include <webserver/Escaping.h>
#include <Exception.h>
#include <StringUtils.h>
#include <ConPrint.h>
#include <SocketBufferOutStream.h>
#include <KillThreadMessage.h>
#include <RuntimeCheck.h>
#include <RandomAccessOutStream.h>


ChatBot::ChatBot()
:	flags(0),
	world(nullptr),
	pos(Vec3d(0.0)),
	heading(0),
	scratch_packet(SocketBufferOutStream::DontUseNetworkByteOrder)
{
	next_sentence_search_pos = 0;
	next_sentence_start_index = 0;
	sentences_received_timer.pause();

	repeating_gesture_timer.pause();
}


ChatBot::~ChatBot()
{

}


void ChatBot::processHeardChatMessage(WorldStateLock& lock, const std::string& msg, AvatarRef sender_avatar, const std::string& avatar_name, Server* server)
{
	assert(llm_thread);
	if(llm_thread)
	{
		// Send the chat message to the LLM cloud server.
		SendAIChatPostContent* send_chat_msg = new SendAIChatPostContent();
		send_chat_msg->message = avatar_name + ": " + msg;
		llm_thread->getMessageQueue().enqueue(send_chat_msg);

		this->chatting_with_av = sender_avatar;

		time_since_last_interaction.reset();
	}
}


// Handle some (partial, streaming) chat data coming back from an LLM cloud server.
void ChatBot::handleLLMChatResponse(const std::string& msg, Server* server, WorldStateLock& world_lock)
{
	// conPrint("-----ChatBot::handleLLMChatResponse----\n" + msg);

	total_llm_response += msg;

	const size_t MAX_TOTAL_RESPONSE_SIZE = 1'000'000;
	if(total_llm_response.size() > MAX_TOTAL_RESPONSE_SIZE)
	{
		conPrint("Warning: Truncating oversized LLM response");
		total_llm_response.resize(MAX_TOTAL_RESPONSE_SIZE);
	}

	// Scan through total response, if we have accumulated a sentence, queue it to send to server main thread.
	while(next_sentence_search_pos < total_llm_response.size())
	{
		if(	total_llm_response[next_sentence_search_pos] == '.' || 
			total_llm_response[next_sentence_search_pos] == '\n' ||
			total_llm_response[next_sentence_search_pos] == '\r' ||
			total_llm_response[next_sentence_search_pos] == '?' ||
			total_llm_response[next_sentence_search_pos] == '!')
		{
			next_sentence_search_pos++; // Advance past full stop or newline.

			next_sentence_start_index = next_sentence_search_pos; // Set next_total_llm_response_start to start of next sentence

			// Start sentences-received timer if it isn't already running.
			if(sentences_received_timer.isPaused())
			{
				// conPrint("!!!!!!!!!!!!!! resetAndUnpause sentences_received_timer...");
				sentences_received_timer.resetAndUnpause();
			}
		}
		else
		{
			next_sentence_search_pos++;
		}
	}

	assert(next_sentence_start_index <= total_llm_response.size());

	time_since_last_interaction.reset();
}


void ChatBot::handleLLMChatResponseDone(Server* server, WorldStateLock& world_lock)
{
	if(!total_llm_response.empty()) // total_llm_response may be empty when doing tool calls.
	{
		sendChatMessage(total_llm_response, server);
	}

	total_llm_response.clear();
	next_sentence_search_pos = 0;
	next_sentence_start_index = 0;

	sentences_received_timer.pause();

	time_since_last_interaction.reset();
}


void ChatBot::handleLLMToolFunctionCall(const std::vector<Reference<ToolFunctionCall>>& calls, Server* server, WorldStateLock& world_lock)
{
	if(llm_thread)
	{
		for(size_t z=0; z<calls.size(); ++z)
		{
			Reference<ToolFunctionCall> call = calls[z];

			// conPrint("Received tool call for function '" + call->function_name + "'...");

			if(call->function_name == "perform_wave_gesture")
			{
				// Enqueue AvatarPerformGesture messages to worker threads to send
				MessageUtils::initPacket(scratch_packet, Protocol::AvatarPerformGesture);
				::writeToStream(avatar_uid, scratch_packet);
				scratch_packet.writeStringLengthFirst("Waving 1"); // See GestureUI.cpp
				MessageUtils::updatePacketLengthField(scratch_packet);

				server->enqueuePacketToBroadcastForWorld(scratch_packet, world);


				// Send response to LLM thread to send to LLM cloud server.
				Reference<SendAIChatToolCallResult> result_msg = new SendAIChatToolCallResult();
				result_msg->tool_call_id = call->call_id;
				result_msg->tool_call_name = call->function_name;
				result_msg->content = "Gesture performed successfully.";
				result_msg->should_send_to_server_immediately = false;
				llm_thread->getMessageQueue().enqueue(result_msg);

				repeating_gesture_timer.resetAndUnpause();
			}
			else if(call->function_name == "perform_bow_gesture")
			{
				// Enqueue AvatarPerformGesture messages to worker threads to send
				MessageUtils::initPacket(scratch_packet, Protocol::AvatarPerformGesture);
				::writeToStream(avatar_uid, scratch_packet);
				scratch_packet.writeStringLengthFirst("Quick Informal Bow"); // See GestureUI.cpp
				MessageUtils::updatePacketLengthField(scratch_packet);

				server->enqueuePacketToBroadcastForWorld(scratch_packet, world);


				// Send response to LLM thread to send to LLM cloud server.
				Reference<SendAIChatToolCallResult> result_msg = new SendAIChatToolCallResult();
				result_msg->tool_call_id = call->call_id;
				result_msg->tool_call_name = call->function_name;
				result_msg->content = "Gesture performed successfully.";
				result_msg->should_send_to_server_immediately = false;
				llm_thread->getMessageQueue().enqueue(result_msg);
			}
			else
			{
				auto res = info_tool_functions.find(call->function_name);
				if(res != info_tool_functions.end())
				{
					const ChatBotToolFunction* func = res->second.ptr();

					// Send response to LLM thread to send to LLM cloud server.
					Reference<SendAIChatToolCallResult> result_msg = new SendAIChatToolCallResult();
					result_msg->tool_call_id = call->call_id;
					result_msg->tool_call_name = call->function_name;
					result_msg->content = func->result_content;

					llm_thread->getMessageQueue().enqueue(result_msg);
				}
				else
				{
					// Send response to LLM thread to send to LLM cloud server.
					Reference<SendAIChatToolCallResult> result_msg = new SendAIChatToolCallResult();
					result_msg->tool_call_id = call->call_id;
					result_msg->tool_call_name = call->function_name;
					result_msg->content = "Unknown tool function '" + call->function_name + "'.";

					llm_thread->getMessageQueue().enqueue(result_msg);
				}
			}
		}
	}

	time_since_last_interaction.reset();
}


void ChatBot::sendChatMessage(const string_view message, Server* server)
{
	// Send total_llm_response as a chat message
	MessageUtils::initPacket(scratch_packet, Protocol::ChatMessageID);
	scratch_packet.writeStringLengthFirst(name); // Write sender name
	scratch_packet.writeStringLengthFirst(message); // Write message 
	::writeToStream(avatar_uid, scratch_packet); // Write sender avatar UID (= the avatar of this chatbot)
	
	MessageUtils::updatePacketLengthField(scratch_packet);

	server->enqueuePacketToBroadcastForWorld(scratch_packet, world);
}


ChatBot::ThinkResults ChatBot::think(Server* server, WorldStateLock& /*world_lock*/)
{
	ThinkResults think_results;

	if(sentences_received_timer.isRunning() && (sentences_received_timer.elapsed() > 0.3))
	{
		// Send all complete sentences as a chat message
		runtimeCheck(next_sentence_start_index <= total_llm_response.size());
		sendChatMessage(string_view(total_llm_response.data(), next_sentence_start_index), server);

		// Remove the prefix of total_llm_response that we sent.
		total_llm_response.erase(/*offset=*/0, /*count=*/next_sentence_start_index);

		// Adjust next_sentence_search_pos to take account of the prefix we just removed.
		runtimeCheck(next_sentence_search_pos >= next_sentence_start_index);
		next_sentence_search_pos -= next_sentence_start_index;
		next_sentence_start_index = 0;

		sentences_received_timer.pause();
	}

	// Stop repeating gestures such as waving.
	if(repeating_gesture_timer.isRunning() && (repeating_gesture_timer.elapsed() > 5.0))
	{
		// Enqueue AvatarStopGesture messages to worker threads to send
		MessageUtils::initPacket(scratch_packet, Protocol::AvatarStopGesture);
		::writeToStream(avatar_uid, scratch_packet);
		MessageUtils::updatePacketLengthField(scratch_packet);

		server->enqueuePacketToBroadcastForWorld(scratch_packet, world);

		repeating_gesture_timer.pause();
	}

	const double FACE_TOWARDS_CHATTING_AV_DIST = 6.0;
	if(chatting_with_av && (chatting_with_av->pos.getDist2(this->pos) < Maths::square(FACE_TOWARDS_CHATTING_AV_DIST)) && avatar)
	{
		// Turn to look at the avatar that we are chatting with.
		const Vec3d to_chat_av = chatting_with_av->pos - this->pos;
		const float target_heading = (float)::atan2(to_chat_av.y, to_chat_av.x);

		avatar->rotation.z = target_heading;
		const float target_pitch = Maths::pi_2<float>() - (float)std::asin(to_chat_av.z / sqrt(Maths::square(to_chat_av.x) + Maths::square(to_chat_av.y)));
		avatar->rotation.y = target_pitch;

		avatar->transform_dirty = true;
	}


	// Terminate the LLM thread after some period of time without user chat messages to the chatbot or replies from the LLM.
	const double KILL_TIME_AFTER_LAST_INTERACTION = 120.0;
	if(llm_thread && (time_since_last_interaction.elapsed() > KILL_TIME_AFTER_LAST_INTERACTION))
	{
		conPrint("ChatBot::think: killing the LLMThread as no interaction for " + toString(KILL_TIME_AFTER_LAST_INTERACTION) + " s.");
		llm_thread->getMessageQueue().enqueue(new KillThreadMessage());
		llm_thread->kill();
		think_results.llm_thread_being_killed = llm_thread;
		llm_thread = nullptr;

		this->chatting_with_av = nullptr;
	}

	return think_results;
}


Reference<LLMThread> ChatBot::createLLMThread(Server* server, WorldStateLock& lock)
{
	conPrint("ChatBot::createLLMThread()");

	time_since_last_interaction.reset();

	// Make tools_json
	std::string tools_json = 
		"\"tools\": [         \n";


	std::vector<Reference<ChatBotToolFunction>> built_in_tool_functions;
	{
		Reference<ChatBotToolFunction> func = new ChatBotToolFunction();
		func->function_name = "perform_wave_gesture";
		func->description = "Make the chatbot's avatar perform a waving gesture.";
		built_in_tool_functions.push_back(func);
	}
	{
		Reference<ChatBotToolFunction> func = new ChatBotToolFunction();
		func->function_name = "perform_bow_gesture";
		func->description = "Make the chatbot's avatar perform a quick, informal bowing gesture.";
		built_in_tool_functions.push_back(func);
	}


	std::map<std::string, Reference<ChatBotToolFunction>> all_tool_functions = info_tool_functions;
	for(int i=0; i<built_in_tool_functions.size(); ++i)
		all_tool_functions[built_in_tool_functions[i]->function_name] = built_in_tool_functions[i];

	int index = 0;
	for(auto it = all_tool_functions.begin(); it != all_tool_functions.end(); ++it)
	{
		Reference<ChatBotToolFunction> func = it->second;
		tools_json +=
		"	{																			\n"
		"	  \"type\": \"function\",													\n"
		"	  \"function\": {															\n"
		"		\"name\": \"" + web::Escaping::JSONEscape(func->function_name) + "\",		\n"
		"		\"description\": \"" + web::Escaping::JSONEscape(func->description) + "\",	\n"
		"		\"parameters\": {														\n"
		"		  \"type\": \"object\",													\n" // TEMP: just assuming all functions take zero parameters for now.
		"		  \"properties\": {},													\n"
		"		  \"required\": []														\n"
		"		}																		\n"
		"	  }																			\n"
		"	}" + (((index + 1) < all_tool_functions.size()) ? "," : "") + "\n";

		index++;
	}

	tools_json += 
		"],																				\n";
	tools_json += 
		"\"tool_choice\": \"auto\",														\n";

	
	Reference<LLMThread> new_llm_thread = new LLMThread(server->config.AI_model_id);
	new_llm_thread->base_prompt_json_escaped = web::Escaping::JSONEscape(server->config.shared_LLM_prompt_part + this->custom_prompt_part);
	new_llm_thread->tools_json = tools_json;
	new_llm_thread->chatbot = this;
	return new_llm_thread;
}


static const uint32 CHATBOT_SERIALISATION_VERSION = 1;


void ChatBot::writeToStream(RandomAccessOutStream& stream)
{
	// Write to stream with a length prefix.  Do this by writing to the stream, them going back and writing the length of the data we wrote.
	// Writing a length prefix allows for adding more fields later, while retaining backwards compatibility with older code that can just skip over the new fields.

	const size_t initial_write_index = stream.getWriteIndex();

	stream.writeUInt32(CHATBOT_SERIALISATION_VERSION);
	stream.writeUInt32(0); // Size of buffer will be written here later


	stream.writeUInt64(id);

	::writeToStream(owner_id, stream);
	created_time.writeToStream(stream);

	stream.writeStringLengthFirst(name);

	writeAvatarSettingsToStream(avatar_settings, stream);

	stream.writeUInt32(flags);

	::writeToStream(pos, stream);
	stream.writeFloat(heading);

	stream.writeStringLengthFirst(custom_prompt_part);

	// Write info_tool_functions
	stream.writeUInt32((uint32)info_tool_functions.size());
	for(auto it = info_tool_functions.begin(); it != info_tool_functions.end(); ++it)
	{
		it->second->writeToStream(stream);
	}


	// Go back and write size of buffer to buffer size field
	const uint32 buffer_size = (uint32)(stream.getWriteIndex() - initial_write_index);

	std::memcpy(stream.getWritePtrAtIndex(initial_write_index + sizeof(uint32)), &buffer_size, sizeof(uint32));
}


void readChatBotFromStream(RandomAccessInStream& stream, ChatBot& chatbot)
{
	const size_t initial_read_index = stream.getReadIndex();

	/*const uint32 version =*/ stream.readUInt32();
	const size_t buffer_size = stream.readUInt32();

	checkProperty(buffer_size >= 8ul, "readChatBotFromStream: buffer_size was too small");
	checkProperty(buffer_size <= 10000000ul, "readChatBotFromStream: buffer_size was too large");

	chatbot.id = stream.readUInt64();

	chatbot.owner_id = readUserIDFromStream(stream);
	chatbot.created_time.readFromStream(stream);

	chatbot.name = stream.readStringLengthFirst(ChatBot::MAX_NAME_SIZE);

	readAvatarSettingsFromStream(stream, chatbot.avatar_settings);

	chatbot.flags = stream.readUInt32();

	chatbot.pos = ::readVec3FromStream<double>(stream);
	chatbot.heading = stream.readFloat();

	chatbot.custom_prompt_part = stream.readStringLengthFirst(ChatBot::MAX_CUSTOM_PROMPT_PART_SIZE);

	// Read info_tool_functions
	const uint32 info_tool_functions_size = stream.readUInt32();
	if(info_tool_functions_size > 10000)
		throw glare::Exception("info_tool_functions_size too large: " + toString(info_tool_functions_size));
	for(size_t i=0; i<info_tool_functions_size; ++i)
	{
		Reference<ChatBotToolFunction> func = new ChatBotToolFunction();
		readChatBotToolFunctionFromStream(stream, *func);
		chatbot.info_tool_functions[func->function_name] = func;
	}


	// Discard any remaining unread data
	const size_t read_B = stream.getReadIndex() - initial_read_index; // Number of bytes we have read so far
	if(read_B < buffer_size)
		stream.advanceReadIndex(buffer_size - read_B);
}


static const uint32 CHATBOT_TOOL_FUNCTION_SERIALISATION_VERSION = 1;


void ChatBotToolFunction::writeToStream(RandomAccessOutStream& stream)
{
	// Write to stream with a length prefix.  Do this by writing to the stream, them going back and writing the length of the data we wrote.
	// Writing a length prefix allows for adding more fields later, while retaining backwards compatibility with older code that can just skip over the new fields.

	const size_t initial_write_index = stream.getWriteIndex();

	stream.writeUInt32(CHATBOT_TOOL_FUNCTION_SERIALISATION_VERSION);
	stream.writeUInt32(0); // Size of buffer will be written here later


	stream.writeStringLengthFirst(function_name);
	stream.writeStringLengthFirst(description);
	stream.writeStringLengthFirst(result_content);


	// Go back and write size of buffer to buffer size field
	const uint32 buffer_size = (uint32)(stream.getWriteIndex() - initial_write_index);
	std::memcpy(stream.getWritePtrAtIndex(initial_write_index + sizeof(uint32)), &buffer_size, sizeof(uint32));
}


void readChatBotToolFunctionFromStream(RandomAccessInStream& stream, ChatBotToolFunction& func)
{
	const size_t initial_read_index = stream.getReadIndex();

	/*const uint32 version =*/ stream.readUInt32();
	const size_t buffer_size = stream.readUInt32();

	checkProperty(buffer_size >= 8ul, "readChatBotToolFunctionFromStream: buffer_size was too small");
	checkProperty(buffer_size <= 1000000ul, "readChatBotToolFunctionFromStream: buffer_size was too large");

	
	func.function_name  = stream.readStringLengthFirst(ChatBotToolFunction::MAX_FUNCTION_NAME_SIZE);
	func.description    = stream.readStringLengthFirst(ChatBotToolFunction::MAX_DESCRIPTION_NAME_SIZE);
	func.result_content = stream.readStringLengthFirst(ChatBotToolFunction::MAX_RESULT_CONTENT_SIZE);


	// Discard any remaining unread data
	const size_t read_B = stream.getReadIndex() - initial_read_index; // Number of bytes we have read so far
	if(read_B < buffer_size)
		stream.advanceReadIndex(buffer_size - read_B);
}
