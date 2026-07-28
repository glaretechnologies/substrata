/*=====================================================================
BuilderAISession.cpp
--------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "BuilderAISession.h"


#include "Server.h"
#include "ServerWorldState.h"
#include "../shared/Protocol.h"
#include "../shared/MessageUtils.h"
#include "../webserver/MCPHandlers.h"
#include <ai/LLMThread.h>
#include <ConPrint.h>
#include <Exception.h>
#include <StringUtils.h>
#include <SocketBufferOutStream.h>
#include <Vector.h>
#include <networking/SocketInterface.h>


// A runaway agent could otherwise fill a world with objects.  Counted over the lifetime of the session.
static const int MAX_TOOL_CALLS_PER_SESSION = 400;

// Number of messages of conversation history to keep.  A single build turn can be many messages, as each tool call
// and its result is a message.
static const size_t MAX_NUM_CHAT_MESSAGES = 400;

// If a request to the LLM server fails, LLMClient doesn't tell us, so without this the panel would wait forever.
static const double TURN_TIMEOUT_S = 180.0;


static const std::string makeSystemPrompt(const std::string& world_name, const std::string& user_name)
{
	return
		"You are the Builder AI for Substrata, a metaverse platform.  You build things in the 3d world on behalf of the user, "
		"by calling the tools available to you.\n"
		"\n"
		"You are building in the world named '" + world_name + "' (pass this as world_name to the tools).  "
		"You are acting on behalf of the user '" + user_name + "', and can only modify objects that user is allowed to modify.\n"
		"\n"
		"Coordinate system: z is up, and 1 unit is 1 metre.  Rotations are given as an axis (axis_x, axis_y, axis_z) plus an "
		"angle in radians.  A person is about 1.8m tall, a doorway about 2.1m, a storey of a building about 3m.\n"
		"\n"
		"Build things out of the primitive tools (create_cube, create_cylinder, create_sphere, create_cone, create_wedge), which "
		"take a size in metres and an optional material.  Prefer base_pos over pos when placing something that should sit on the "
		"ground, as it saves you having to halve the height yourself.  create_wedge is useful for pitched roofs and ramps.\n"
		"\n"
		"Guidance:\n"
		"- Work out the whole layout before you start creating objects, then create them.\n"
		"- Build near the position given in the user's message unless they say otherwise.  Do not build at the origin.\n"
		"- Use list_objects_near first if the user refers to something that already exists, or if you need to avoid building on top of things.\n"
		"- Keep builds to a reasonable number of objects.  A simple house is tens of objects, not hundreds.\n"
		"- Give things sensible materials and colours rather than leaving everything the default white.\n"
		"- Briefly tell the user what you are building as you go.  Keep it short; they can see the result.\n"
		"- If a tool call fails, read the error, fix the arguments and try again rather than repeating the same call.";
}


BuilderAISession::BuilderAISession(Server* server_, UserID user_id_, const std::string& user_name_, const std::string& world_name_,
	SocketInterface* socket_, EventFD* wake_event_fd_)
:	server(server_),
	user_id(user_id_),
	user_name(user_name_),
	world_name(world_name_),
	socket(socket_),
	wake_event_fd(wake_event_fd_),
	turn_in_progress(false),
	cancel_requested(false),
	dispatched_tool_calls(false),
	num_tool_calls_made(0)
{
	LLMThread::Settings settings;
	settings.base_prompt = makeSystemPrompt(world_name, user_name);
	settings.max_num_messages = MAX_NUM_CHAT_MESSAGES;
	settings.reasoning_effort = LLMClient::ReasoningEffort_high;

	// Use the same tools as the /mcp endpoint, so in-world building and external agents always have the same capabilities.
	std::vector<MCPHandlers::ToolSpec> tool_specs;
	MCPHandlers::getToolSpecs(tool_specs);
	for(size_t i=0; i<tool_specs.size(); ++i)
	{
		ToolFunctionSpec spec;
		spec.name              = tool_specs[i].name;
		spec.description       = tool_specs[i].description;
		spec.input_schema_json = tool_specs[i].input_schema_json;
		settings.tool_functions.funcs.push_back(spec);
	}

	conPrint("BuilderAISession: Using the model '" + server->config.AI_model_id + "', reasoning effort: " + LLMClient::reasoningEffortString(settings.reasoning_effort));

	// The LLMThread posts responses to llm_msg_queue, and notifies our WorkerThread's event fd so it wakes to process them.
	llm_thread = new LLMThread(server->config.AI_model_id, settings, &server->world_state->server_credentials, &llm_msg_queue);
	llm_thread->out_msg_queue_event_fd = wake_event_fd;
	// NOTE: llm_thread->user is left null: only this session's LLMThread posts to llm_msg_queue, so responses don't need routing.
	llm_thread_manager.addThread(llm_thread);
}


BuilderAISession::~BuilderAISession()
{
	// Shut the LLM thread down.  NOTE: blocks until any in-flight HTTP request to the LLM server completes.
	llm_thread_manager.killThreadsBlocking();
}


void BuilderAISession::sendStringMessageToClient(uint32 message_id, const std::string& s)
{
	SocketBufferOutStream packet;
	MessageUtils::initPacket(packet, message_id);
	packet.writeStringLengthFirst(s);
	MessageUtils::updatePacketLengthField(packet);

	socket->writeData(packet.buf.data(), packet.buf.size());
	socket->flush();
}


void BuilderAISession::sendEmptyMessageToClient(uint32 message_id)
{
	SocketBufferOutStream packet;
	MessageUtils::initPacket(packet, message_id);
	MessageUtils::updatePacketLengthField(packet);

	socket->writeData(packet.buf.data(), packet.buf.size());
	socket->flush();
}


// Rendered into each user message, so the model knows where the user is when they say "here".
static const std::string makeContextPreamble(const BuilderAIContext& context)
{
	std::string s = "[Context: the user is at (" + doubleToStringMaxNDecimalPlaces(context.cam_pos.x, 2) + ", " + doubleToStringMaxNDecimalPlaces(context.cam_pos.y, 2) + ", " +
		doubleToStringMaxNDecimalPlaces(context.cam_pos.z, 2) + "), looking in direction (" +
		doubleToStringMaxNDecimalPlaces(context.cam_forwards.x, 2) + ", " + doubleToStringMaxNDecimalPlaces(context.cam_forwards.y, 2) + ", " +
		doubleToStringMaxNDecimalPlaces(context.cam_forwards.z, 2) + ").";

	if(context.crosshair_pos_valid != 0)
		s += "  They are pointing at (" + doubleToStringMaxNDecimalPlaces(context.crosshair_pos.x, 2) + ", " +
			doubleToStringMaxNDecimalPlaces(context.crosshair_pos.y, 2) + ", " + doubleToStringMaxNDecimalPlaces(context.crosshair_pos.z, 2) +
			"), so 'here' and 'there' mean that position.";
	else
		s += "  They are not pointing at anything in particular, so 'here' means just in front of them.";

	if(context.have_selected_ob != 0)
		s += "  They have object with UID " + toString(context.selected_ob_uid) + " selected, so 'this' or 'it' probably means that object.";

	s += "]\n\n";
	return s;
}


void BuilderAISession::handleUserMessage(const std::string& user_text, const BuilderAIContext& context)
{
	if(turn_in_progress)
	{
		// The conversation can't have two turns in flight at once, and the UI should be preventing this.
		sendStringMessageToClient(Protocol::BuilderAIError, "Still working on the previous message.");
		return;
	}

	cancel_requested = false;

	Reference<SendAIChatPostContent> msg = new SendAIChatPostContent();
	msg->message = makeContextPreamble(context) + user_text;
	msg->should_send_to_server_immediately = true;
	llm_thread->getMessageQueue().enqueue(msg);

	conPrint("BuilderAISession: sending msg to LLM:\n" + msg->message);

	turn_in_progress = true;
	time_since_turn_activity.reset();
}


void BuilderAISession::handleCancel()
{
	// Best-effort: we can't interrupt an HTTP request that is already in flight, so we let the response arrive and then
	// decline to act on it.
	conPrint("BuilderAISession: cancel requested.");
	cancel_requested = true;
}


void BuilderAISession::processLLMMessages()
{
	js::Vector<Reference<ThreadMessage>, 16> temp_messages;
	llm_msg_queue.dequeueAnyQueuedItems(temp_messages);

	if(!temp_messages.empty())
		time_since_turn_activity.reset();

	for(size_t i=0; i<temp_messages.size(); ++i)
	{
		ThreadMessage* msg = temp_messages[i].ptr();

		if(dynamic_cast<AIChatResponseDataMessage*>(msg))
		{
			// Some streamed assistant text: pass it straight on to the panel.
			if(!cancel_requested)
				sendStringMessageToClient(Protocol::BuilderAITextDelta, static_cast<AIChatResponseDataMessage*>(msg)->message);
		}
		else if(dynamic_cast<AIToolFunctionCallMessage*>(msg))
		{
			const std::vector<Reference<ToolFunctionCall>>& calls = static_cast<AIToolFunctionCallMessage*>(msg)->calls->calls;

			dispatched_tool_calls = true;

			for(size_t z=0; z<calls.size(); ++z)
			{
				const ToolFunctionCall* call = calls[z].ptr();
				if(call == NULL)
					continue;

				MCPHandlers::ToolResult result;
				if(cancel_requested)
				{
					result.text = "Cancelled by the user.";
					result.is_error = true;
				}
				else if(num_tool_calls_made >= MAX_TOOL_CALLS_PER_SESSION)
				{
					result.text = "Tool call limit for this session reached.  Tell the user, and stop.";
					result.is_error = true;
				}
				else
				{
					sendStringMessageToClient(Protocol::BuilderAIToolActivity, call->function_name);

					// NOTE: callTool takes the world state lock itself, for the duration of the individual tool call.
					result = MCPHandlers::callTool(*server->world_state, call->function_name, call->args_json, user_id, user_name);
					num_tool_calls_made++;
				}

				Reference<SendAIChatToolCallResult> result_msg = new SendAIChatToolCallResult();
				result_msg->tool_call_result.tool_call_id   = call->call_id;
				result_msg->tool_call_result.tool_call_name = call->function_name;
				result_msg->tool_call_result.content        = result.text;

				// Only kick off the next request once every result for this response has been appended, and not at all if
				// we are cancelling: leaving the results in the history without sending keeps the conversation consistent
				// (every tool call has a result) for whenever the user sends their next message.
				result_msg->should_send_to_server_immediately = !cancel_requested && ((z + 1) == calls.size());

				llm_thread->getMessageQueue().enqueue(result_msg);
			}
		}
		else if(dynamic_cast<AIChatResponseDoneMessage*>(msg))
		{
			// If the response contained tool calls then we have just sent the results back, so the turn continues and there
			// is more to come.  Otherwise the assistant has finished.
			if(!dispatched_tool_calls || cancel_requested)
			{
				turn_in_progress = false;
				sendEmptyMessageToClient(Protocol::BuilderAITurnComplete);
			}

			dispatched_tool_calls = false;
		}
	}
}


void BuilderAISession::checkForTurnTimeout()
{
	// LLMClient doesn't report a failed request back to us, so a turn that gets no response at all would otherwise leave
	// the panel waiting forever.
	if(turn_in_progress && (time_since_turn_activity.elapsed() > TURN_TIMEOUT_S))
	{
		conPrint("BuilderAISession: turn timed out.");
		turn_in_progress = false;
		dispatched_tool_calls = false;
		sendStringMessageToClient(Protocol::BuilderAIError, "The AI server did not respond.  Please try again.");
		sendEmptyMessageToClient(Protocol::BuilderAITurnComplete);
	}
}
