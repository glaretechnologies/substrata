/*=====================================================================
Server.h
----------------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "ServerWorldState.h"
#include "ThreadManager.h"
#include "../shared/ResourceManager.h"
#include "../shared/LuaScriptEvaluator.h"
#include "../shared/ScriptTimerQueue.h"
#include <IPAddress.h>
#include <utils/UniqueRef.h>
#include <utils/Timer.h>
#include <unordered_set>
class WorkerThread;
class SubstrataLuaVM;
class LuaHTTPRequestManager;
class LuaHTTPRequest;
class SocketBufferOutStream;


struct ServerConnectedClientInfo
{
	IPAddress ip_addr;
	UID client_avatar_id;
	int client_UDP_port; // UDP port on client end
};


class UserUsedObjectThreadMessage : public ThreadMessage
{
public:
	UID avatar_uid; // May be invalid if user is not logged in

	Reference<ServerWorldState> world; // World the client is connected to and that the object is in.
	UID object_uid;
};

class UserTouchedObjectThreadMessage : public ThreadMessage
{
public:
	UID avatar_uid; // May be invalid if user is not logged in

	Reference<ServerWorldState> world; // World the client is connected to and that the object is in.
	UID object_uid;
};

class UserMovedNearToObjectThreadMessage : public ThreadMessage
{
public:
	UID avatar_uid; // May be invalid if user is not logged in

	Reference<ServerWorldState> world; // World the client is connected to and that the object is in.
	UID object_uid;
};

class UserMovedAwayFromObjectThreadMessage : public ThreadMessage
{
public:
	UID avatar_uid; // May be invalid if user is not logged in

	Reference<ServerWorldState> world; // World the client is connected to and that the object is in.
	UID object_uid;
};

class UserEnteredParcelThreadMessage : public ThreadMessage
{
public:
	UID avatar_uid; // May be invalid if user is not logged in
	UserID client_user_id;  // May be invalid if user is not logged in

	Reference<ServerWorldState> world; // World the client is connected to and that the object is in.
	UID object_uid;
	ParcelID parcel_id;
};

class UserExitedParcelThreadMessage : public ThreadMessage
{
public:
	UID avatar_uid; // May be invalid if user is not logged in

	Reference<ServerWorldState> world; // World the client is connected to and that the object is in.
	UID object_uid;
	ParcelID parcel_id;
};


/*=====================================================================
Server
--------------------
=====================================================================*/
class Server : public LuaScriptOutputHandler
{
public:
	Server();
	~Server();

	// LuaScriptOutputHandler interface:
	virtual void printFromLuaScript(LuaScript* script, const char* s, size_t len) override;
	virtual void errorOccurredFromLuaScript(LuaScript* script, const std::string& msg) override;

	void logLuaMessage(const std::string& msg, UserScriptLogMessage::MessageType message_type, UID world_ob_uid, UserID script_creator_user_id); // Thread-safe
	void logLuaError(const std::string& msg, UID world_ob_uid, UserID script_creator_user_id); // Thread-safe

	double getCurrentGlobalTime() const;

	void enqueueMsg(ThreadMessageRef msg);
	void enqueueMsgForLodGenThread(ThreadMessageRef msg) { mesh_lod_gen_thread_manager.enqueueMessage(msg); }

	void enqueueLuaHTTPRequest(Reference<LuaHTTPRequest> request);


	// Called from off main thread
	void clientUDPPortOpen(WorkerThread* worker_thread, const IPAddress& ip_addr, UID client_avatar_id/*, int client_UDP_port*/);
	void clientDisconnected(WorkerThread* worker_thread);

	// Called when we receive a UDP packet from a client, which allows the client remote UDP port to be known.
	void clientUDPPortBecameKnown(UID client_avatar_uid, const IPAddress& ip_addr, int client_UDP_port);

	// Enqueues packet to all WorkerThreads connected to the given world.
	// Thread-safe, can be called from any thread.
	void enqueuePacketToBroadcastForWorld(const SocketBufferOutStream& packet_buffer, ServerWorldState* world);

	// Enqueues packet to all WorkerThreads.
	// Thread-safe, can be called from any thread.
	void enqueuePacketToBroadcastForAllWorlds(const SocketBufferOutStream& packet_buffer);

	// Tracks URLs of textures that MeshLODGenThread has queued to generate, and hasn't finished with yet.  A client that asks for one of these is told it is being processed.
	// Thread-safe.
	void textureGenerationQueued(const URLString& URL); // Records the URL as being generated, and tells connected clients.
	void removeTextureURLBeingGenerated(const URLString& URL);
	bool isTextureURLBeingGenerated(const URLString& URL);


	Reference<ServerAllWorldsState> world_state;

	// Connected client worker threads
	ThreadManager worker_thread_manager;

	ThreadManager mesh_lod_gen_thread_manager;

	ThreadManager udp_handler_thread_manager;

	ThreadManager dyn_tex_updater_thread_manager;

	ThreadManager llm_thread_manager;

	ThreadSafeQueue<Reference<ThreadMessage> > message_queue; // Contains messages from worker threads to the main server thread.

	std::string screenshot_dir; // Dir generated screenshots (taken by the screenshot bot, for parcels) will be saved to.
	std::string photo_dir; // Dir uploaded photos (taken by users with the photo mode) will be saved to.

	ServerConfig config;

	Mutex texture_URLs_being_generated_mutex;
	std::unordered_set<URLString, URLStringHasher> texture_URLs_being_generated GUARDED_BY(texture_URLs_being_generated_mutex);

	Mutex connected_clients_mutex;
	std::map<WorkerThread*, ServerConnectedClientInfo> connected_clients;
	glare::AtomicInt connected_clients_changed;

	Timer total_timer;
	ScriptTimerQueue timer_queue;
	std::vector<ScriptTimerQueueTimer> temp_triggered_timers;

	Reference<LuaHTTPRequestManager> lua_http_manager;
};
