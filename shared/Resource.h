/*=====================================================================
Resource.h
----------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#pragma once


#include "UserID.h"
#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include <string>


/*=====================================================================
Resource
--------
A resource is a file that is used by an object, for example a mesh or texture,
and that is stored on the server and downloaded to clients as needed.
Resources have a (currently abitrary) URL.
Resources are owned by a single user, so that the resource may count against the user's storage allowments.
=====================================================================*/
class Resource : public ThreadSafeRefCounted
{
public:
	enum State
	{
		State_NotPresent, // Resource is not present on local disk.
		State_Transferring, // Resource is currently downloading to the local client from the server.
		State_Present // Resource is completely present on local disk.
		//State_ResourceDownloadFailed
	};

	Resource(const std::string& URL_, const std::string& local_path_, State s, const UserID& owner_id_) : URL(URL_), local_path(local_path_), state(s), owner_id(owner_id_) {}
	Resource() : state(State_NotPresent) {}
	
	const std::string getLocalPath() const { return local_path; }
	void setLocalPath(const std::string& p) { local_path = p; }

	State getState() const { return state; }
	void setState(State s) { state = s; }
	
	std::string URL;
	UserID owner_id;
private:
	State state; // May be protected by mutex soon.
	std::string local_path; // path on local disk.
};

typedef Reference<Resource> ResourceRef;


void writeToStream(const Resource& resource, OutStream& stream);
void readFromStream(InStream& stream, Resource& resource);

//void writeToNetworkStream(const Resource& resource, OutStream& stream); // write without version
//void readFromNetworkStream/*GivenID*/(InStream& stream, Resource& resource);
