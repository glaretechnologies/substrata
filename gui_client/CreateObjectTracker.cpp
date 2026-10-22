/*=====================================================================
CreateObjectTracker.cpp
-----------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "CreateObjectTracker.h"


#include "../shared/Protocol.h"
#include <utils/Lock.h>


uint64 CreateObjectTracker::makeToken()
{
	Lock lock(mutex);
	return next_token++;
}


bool CreateObjectTracker::handleCreateObjectResponse(uint32 result, uint64 client_token, UID created_ob_uid, const std::string& error_msg)
{
	Lock lock(mutex);

	if(client_token == 0 || client_token >= next_token)
		return false; // Not a token we handed out, so this create came from somewhere else in the client.

	Outcome& outcome = outcomes[client_token];
	outcome.result = result;
	outcome.created_ob_uid = created_ob_uid;
	outcome.error_msg = error_msg;
	return true;
}


bool CreateObjectTracker::getOutcomeForToken(uint64 client_token, Outcome& outcome_out) const
{
	Lock lock(mutex);

	auto res = outcomes.find(client_token);
	if(res == outcomes.end())
		return false;

	outcome_out = res->second;
	return true;
}


void CreateObjectTracker::getOutcomesForRange(uint64 begin_token, uint64 end_token, size_t& num_responses_out, size_t& num_refused_out, std::string& first_error_out) const
{
	Lock lock(mutex);

	num_responses_out = 0;
	num_refused_out = 0;
	first_error_out.clear();

	for(auto it = outcomes.lower_bound(begin_token); it != outcomes.end() && it->first < end_token; ++it)
	{
		num_responses_out++;
		if(it->second.result != Protocol::CreateObjectResult_Success)
		{
			num_refused_out++;
			if(first_error_out.empty())
				first_error_out = it->second.error_msg;
		}
	}
}


void CreateObjectTracker::getCreatedUIDsForRange(uint64 begin_token, uint64 end_token, std::vector<UID>& uids_out) const
{
	Lock lock(mutex);

	uids_out.clear();

	for(auto it = outcomes.lower_bound(begin_token); it != outcomes.end() && it->first < end_token; ++it)
		if((it->second.result == Protocol::CreateObjectResult_Success) && it->second.created_ob_uid.valid())
			uids_out.push_back(it->second.created_ob_uid);
}


void CreateObjectTracker::addBuiltObjectUIDs(const std::vector<UID>& uids)
{
	Lock lock(mutex);

	built_object_uids.insert(uids.begin(), uids.end());
}


void CreateObjectTracker::getBuiltObjectUIDs(std::vector<UID>& uids_out) const
{
	Lock lock(mutex);

	uids_out.assign(built_object_uids.begin(), built_object_uids.end());
}


void CreateObjectTracker::forgetRange(uint64 begin_token, uint64 end_token)
{
	Lock lock(mutex);

	outcomes.erase(outcomes.lower_bound(begin_token), outcomes.lower_bound(end_token));
}
