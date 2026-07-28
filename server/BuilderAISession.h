/*=====================================================================
BuilderAISession.h
------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "../shared/UserID.h"
#include "../shared/ProtocolStructs.h"
#include <Reference.h>
#include <ThreadSafeRefCounted.h>
#include <ThreadSafeQueue.h>
#include <ThreadManager.h>
#include <ThreadMessage.h>
#include <Timer.h>
#include <string>
#include <vector>
class Server;
class LLMThread;
class SocketInterface;
class EventFD;


/*=====================================================================
BuilderAISession
----------------
The server side of the in-world "Builder AI".  Drives a conversation with an
LLM on behalf of a single connected user, and dispatches the tool calls the LLM
makes to the same tool implementations as the /mcp endpoint.

Owned by a WorkerThread, and driven entirely from that thread (see
WorkerThread::doRun).  The LLM itself is talked to on a separate LLMThread, so
the multi-second HTTP requests don't block the WorkerThread; the LLMThread posts
its responses back to llm_msg_queue and notifies the WorkerThread's event fd to
wake it.  The WorkerThread then calls processLLMMessages() to handle them.

Tool calls act as the user that owns the connection, so are subject to that
user's object and parcel permissions.
=====================================================================*/
class BuilderAISession : public ThreadSafeRefCounted
{
public:
	// wake_event_fd is the WorkerThread's event fd, notified by the LLMThread when it posts a response, so the
	// WorkerThread wakes to process it.  socket is the connection to the user, written to directly from this thread.
	BuilderAISession(Server* server, UserID user_id, const std::string& user_name, const std::string& world_name,
		SocketInterface* socket, EventFD* wake_event_fd);
	~BuilderAISession();

	// Start a new turn with the user's message and the spatial context it was sent in.
	// If a turn is already in progress, sends an error to the client and does nothing.
	void handleUserMessage(const std::string& user_text, const BuilderAIContext& context);

	// The user wants to stop the current build.  Best-effort: an in-flight LLM request can't be interrupted.
	void handleCancel();

	// Handle any responses the LLMThread has posted.  Called by the WorkerThread after it wakes.
	// Does the tool dispatch (taking the world state lock per tool call) and writes results back to the client.
	void processLLMMessages();

	// Give up on a turn that has received no response for too long (e.g. the LLM request failed).
	void checkForTurnTimeout();

private:
	void sendStringMessageToClient(uint32 message_id, const std::string& s);
	void sendEmptyMessageToClient(uint32 message_id);

	Server* server;
	UserID user_id;
	std::string user_name;
	std::string world_name;
	SocketInterface* socket;
	EventFD* wake_event_fd;

	ThreadSafeQueue<Reference<ThreadMessage>> llm_msg_queue; // Responses from the LLMThread.
	ThreadManager llm_thread_manager;
	Reference<LLMThread> llm_thread;

	bool turn_in_progress;
	bool cancel_requested;
	bool dispatched_tool_calls; // Did the response currently being received contain tool calls?
	int num_tool_calls_made;
	Timer time_since_turn_activity;
};


typedef Reference<BuilderAISession> BuilderAISessionRef;
