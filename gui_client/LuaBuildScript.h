/*=====================================================================
LuaBuildScript.h
----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "ThreadMessages.h"
#include "../shared/UID.h"
#include <maths/vec2.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/Reference.h>
#include <utils/Mutex.h>
#include <utils/Condition.h>
#include <string>
#include <vector>
#include <map>
class GUIClient;


/*=====================================================================
TerrainHeightQuery
------------------
Terrain heights a build script wants.  The terrain belongs to the main thread,
so the build script's thread sends the query to GUIClient in a
TerrainQueriesToProcessMessage and waits on 'condition' until 'done'.
=====================================================================*/
class TerrainHeightQuery : public ThreadSafeRefCounted
{
public:
	TerrainHeightQuery() : done(false) {}

	std::vector<Vec2f> points; // Input, set before the query is sent.

	Mutex mutex;
	Condition condition;
	bool done                     GUARDED_BY(mutex);
	std::vector<float> heights    GUARDED_BY(mutex); // One per point.  Valid when done and error_msg is empty.
	std::string error_msg         GUARDED_BY(mutex);
};
typedef Reference<TerrainHeightQuery> TerrainHeightQueryRef;


class TerrainQueriesToProcessMessage : public ThreadMessage
{
public:
	TerrainQueriesToProcessMessage(const TerrainHeightQueryRef& query_) : ThreadMessage(Msg_TerrainQueriesToProcessMessage), query(query_) {}
	TerrainHeightQueryRef query;
};


/*=====================================================================
LuaBuilderState
---------------
What the server said about the objects build scripts asked it to create.
Owned by MainWindow, because the responses arrive after the script that made
the objects has finished running.

A script puts a token in the UID field of each CreateObject message, which the
server echoes back in its CreateObjectResponse (see Protocol.h).  Tokens are
handed out in order, so a run's objects occupy a contiguous range and can be
looked up with one pass.

State only: it does no waiting, and the run polls it however suits.
=====================================================================*/
class LuaBuilderState : public ThreadSafeRefCounted
{
public:
	LuaBuilderState() : next_token(1) {}

	struct Outcome
	{
		Outcome() : result(0) {}
		uint32 result; // One of Protocol::CreateObjectResult_*.
		UID created_ob_uid; // The UID the server assigned, valid when result is CreateObjectResult_Success.
		std::string error_msg; // Empty unless the object was refused.
	};

	uint64 makeToken(); // Token 0 is never handed out, so it can't be confused with a create that isn't from a build script.

	// Returns whether this was a token we handed out, i.e. whether a build script is responsible for reporting it.
	bool handleCreateObjectResponse(uint32 result, uint64 client_token, UID created_ob_uid, const std::string& error_msg);

	// Returns whether a response has arrived for this token yet, and if so what it said.  A run polls this when it needs the UID the
	// server assigned to an object it just asked for, as getOrCreateGroup() does.
	bool getOutcomeForToken(uint64 client_token, Outcome& outcome_out) const;

	// Counts the responses that have arrived for tokens in [begin_token, end_token), and gives the first refusal message.
	void getOutcomesForRange(uint64 begin_token, uint64 end_token, size_t& num_responses_out, size_t& num_refused_out, std::string& first_error_out) const;

	// Gets the UIDs the server gave the objects it created for tokens in [begin_token, end_token), from the responses that have arrived.
	void getCreatedUIDsForRange(uint64 begin_token, uint64 end_token, std::vector<UID>& uids_out) const;

	void forgetRange(uint64 begin_token, uint64 end_token);

private:
	mutable Mutex mutex;
	uint64 next_token   GUARDED_BY(mutex);
	std::map<uint64, Outcome> outcomes  GUARDED_BY(mutex);
};
typedef Reference<LuaBuilderState> LuaBuilderStateRef;


/*=====================================================================
LuaBuildScript
--------------
Runs a one-shot Luau 'build script', for the run_lua_build_script MCP tool
(see MCPClientHandler).

Build scripts are a different thing from the per-object scripts run by
LuaScriptEvaluator: they run once to completion, are not attached to a world
object, and have their own set of globals, registered per-script so they are
not visible to object scripts.

run() is called from an MCP handler thread, so it makes its own SubstrataLuaVM
rather than sharing GUIClient::lua_vm: constructing a LuaScript creates a thread
on the VM's lua_State, which the main thread is using for object scripts.
=====================================================================*/
struct LuaBuildScriptOptions
{
	LuaBuildScriptOptions() : max_num_interrupts(64 * 1024 * 1024), max_output_size(64 * 1024), max_objects(10000) {}

	size_t max_num_interrupts; // Execution budget: the script is aborted once the Lua interrupt callback has fired this many times.
	size_t max_output_size; // Bytes of print() output kept; output past this is dropped and output_truncated is set.
	size_t max_objects; // The script is aborted if it tries to create more objects than this.

	// What the script is called in error messages, normally the path of the file it came from.  "script" if left empty.
	std::string script_name;
};


struct LuaBuildScriptResults
{
	LuaBuildScriptResults() : success(false), output_truncated(false), num_objects_created(0), num_group_objects_created(0), num_objects_deleted(0), num_tris_created(0), num_verts_created(0), elapsed_s(0),
		num_creates_answered(0), num_creates_refused(0) {}

	bool success;
	std::string output; // Text the script printed with print().
	bool output_truncated;
	std::string error_msg; // Valid when !success.  Has source locations for compile errors.
	size_t num_objects_created; // Objects sent to the server, including group objects.  Also counted for a run that then failed.
	size_t num_group_objects_created; // How many of num_objects_created were group objects made for new groups.
	size_t num_objects_deleted; // Objects deleteObjectsInGroup() asked the server to destroy.
	size_t num_tris_created; // Triangles in the models of the objects sent, counting a model once per object that uses it.
	size_t num_verts_created; // Vertices, counted the same way.
	double elapsed_s;

	// What the server made of the objects, from the responses that arrived before we stopped waiting.
	size_t num_creates_answered;
	size_t num_creates_refused;
	std::string first_refusal_msg;
};


class LuaBuildScript
{
public:
	// Runs script_src to completion.  Does not throw: failures are reported in results_out.
	static void run(GUIClient* gui_client, LuaBuilderState* builder_state, const std::string& script_src, const LuaBuildScriptOptions& options, LuaBuildScriptResults& results_out);

	static void test();
};
