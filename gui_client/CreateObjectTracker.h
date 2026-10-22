/*=====================================================================
CreateObjectTracker.h
---------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "../shared/UID.h"
#include <utils/ThreadSafeRefCounted.h>
#include <utils/Reference.h>
#include <utils/Mutex.h>
#include <string>
#include <vector>
#include <map>
#include <set>


/*=====================================================================
CreateObjectTracker
-------------------
What the server said about objects created in bulk from other threads,
such as by build scripts (LuaBuildScript) or the Voxels parcel importer
(VoxelsParcelImporter).  Owned by GUIClient, because the responses arrive
in its message queue, and can do so after the code that made the objects
has finished.

Code creating objects puts a token from makeToken() in the UID field of each
CreateObject message, which the server echoes back in its
CreateObjectResponse (see Protocol.h).  Tokens are handed out in order, so
one run's objects occupy a contiguous range and can be looked up with one
pass.

State only: it does no waiting, and the creating code polls it however suits.
=====================================================================*/
class CreateObjectTracker : public ThreadSafeRefCounted
{
public:
	CreateObjectTracker() : next_token(1) {}

	struct Outcome
	{
		Outcome() : result(0) {}
		uint32 result; // One of Protocol::CreateObjectResult_*.
		UID created_ob_uid; // The UID the server assigned, valid when result is CreateObjectResult_Success.
		std::string error_msg; // Empty unless the object was refused.
	};

	uint64 makeToken(); // Token 0 is never handed out, so it can't be confused with a create that isn't tracked.

	// Returns whether this was a token we handed out, i.e. whether the code that made the object is responsible for reporting it.
	bool handleCreateObjectResponse(uint32 result, uint64 client_token, UID created_ob_uid, const std::string& error_msg);

	// Returns whether a response has arrived for this token yet, and if so what it said.
	bool getOutcomeForToken(uint64 client_token, Outcome& outcome_out) const;

	// Counts the responses that have arrived for tokens in [begin_token, end_token), and gives the first refusal message.
	void getOutcomesForRange(uint64 begin_token, uint64 end_token, size_t& num_responses_out, size_t& num_refused_out, std::string& first_error_out) const;

	// Gets the UIDs the server gave the objects it created for tokens in [begin_token, end_token), from the responses that have arrived.
	void getCreatedUIDsForRange(uint64 begin_token, uint64 end_token, std::vector<UID>& uids_out) const;

	void forgetRange(uint64 begin_token, uint64 end_token);

	// The objects created this session.  render_view waits for these to show their models before rendering.
	void addBuiltObjectUIDs(const std::vector<UID>& uids);
	void getBuiltObjectUIDs(std::vector<UID>& uids_out) const;

private:
	mutable Mutex mutex;
	uint64 next_token   GUARDED_BY(mutex);
	std::map<uint64, Outcome> outcomes  GUARDED_BY(mutex);
	std::set<UID> built_object_uids  GUARDED_BY(mutex);
};
typedef Reference<CreateObjectTracker> CreateObjectTrackerRef;
