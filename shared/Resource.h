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
#include <utils/Vector.h>
#include <utils/Mutex.h>
#include <utils/DatabaseKey.h>
#include <set>


struct ResourceDownloadListener : public ThreadSafeRefCounted
{
	virtual ~ResourceDownloadListener() {}
	virtual void dataReceived() = 0;
};


/*=====================================================================
Resource
--------
A resource is a file that is used by an object, for example a mesh or texture,
and that is stored on the server and downloaded to clients as needed.
Resources have a (currently abitrary) URL.
Resources are owned by a single user, so that the resource may count against the user's storage allowance.
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

	Resource(const std::string& URL_, const std::string& local_path_, State s, const UserID& owner_id_) : URL(URL_), local_path(local_path_), state(s), owner_id(owner_id_), num_buffer_readers(0) {}
	Resource() : state(State_NotPresent), num_buffer_readers(0) {}
	
	const std::string getLocalPath() const { return local_path; }
	void setLocalPath(const std::string& p) { local_path = p; }

	State getState() const { return state; }
	void setState(State s) { state = s; }
	
	std::string URL;
	UserID owner_id;

	//void addDownloadListener(const Reference<ResourceDownloadListener>& listener);
	//void removeDownloadListener(const Reference<ResourceDownloadListener>& listener);

	Mutex buffer_mutex; // protects buffer.  Lock should be held by threads other than the DownloadResourcesThread when reading from buffer,
	// and will be held by DownloadResourcesThread when writing to buffer.
	js::Vector<uint8, 16> buffer	GUARDED_BY(buffer_mutex); // Streamed files will be downloaded to this buffer first, then saved to disk.
	int64 num_buffer_readers		GUARDED_BY(buffer_mutex); // If num_buffer_readers > 0, then the buffer won't be cleared when the resource has been fully downloaded.
	//std::set<Reference<ResourceDownloadListener>> listeners;

	DatabaseKey database_key;
private:
	State state; // May be protected by mutex soon.
	std::string local_path; // path on local disk.
};

typedef Reference<Resource> ResourceRef;


void writeToStream(const Resource& resource, OutStream& stream);
void readFromStream(InStream& stream, Resource& resource);

//void writeToNetworkStream(const Resource& resource, OutStream& stream); // write without version
//void readFromNetworkStream/*GivenID*/(InStream& stream, Resource& resource);


struct ResourceRefHash
{
	size_t operator() (const ResourceRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};
