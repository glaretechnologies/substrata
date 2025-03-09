/*=====================================================================
Resource.h
----------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "UserID.h"
#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include <utils/DatabaseKey.h>
#include <string>


//struct ResourceDownloadListener : public ThreadSafeRefCounted
//{
//	virtual ~ResourceDownloadListener() {}
//	virtual void dataReceived() = 0;
//};


// Used in Emscripten.
// We use emscripten_async_wget2_data in EmscriptenResourceDownloader, and need to free the buffer memory ourselves.
struct LoadedBuffer : public ThreadSafeRefCounted
{
	LoadedBuffer() : buffer(nullptr) {}
	~LoadedBuffer() { free(buffer); }

	void* buffer;
	size_t buffer_size;
};


/*=====================================================================
Resource
--------
A resource is a file that is used by an object, for example a mesh or texture,
and that is stored on the server and downloaded to clients as needed.
Resources have a (currently arbitrary) URL.
Resources are owned by a single user, so that the resource may count against the user's storage allowance.
=====================================================================*/
class Resource : public ThreadSafeRefCounted
{
public:
	enum State
	{
		State_NotPresent = 0, // Resource is not present on local disk.
		State_Transferring = 1, // Resource is currently downloading to the local client from the server.
		State_Present = 2 // Resource is completely present on local disk.
		//State_ResourceDownloadFailed
	};

	Resource(const std::string& URL_, const std::string& raw_local_path_, State s, const UserID& owner_id_, bool external_resource);
	Resource() : state(State_NotPresent)/*, num_buffer_readers(0)*/, locally_deleted(false), file_size_B(0), external_resource(false) {}
	
	const std::string getLocalAbsPath(const std::string& base_resource_dir) const { return external_resource ? local_path : (base_resource_dir + "/" + local_path); }
	const std::string getRawLocalPath() const { return local_path; } // Relative path on local disk from base_resources_dir.
	void setRawLocalPath(const std::string& p) { local_path = p; }

	State getState() const { return state; }
	void setState(State s) { state = s; }

	bool isPresent() const { return state == State_Present; }

	void writeToStream(OutStream& stream) const;
	
	
	std::string URL;
	UserID owner_id;

	//void addDownloadListener(const Reference<ResourceDownloadListener>& listener);
	//void removeDownloadListener(const Reference<ResourceDownloadListener>& listener);

	Reference<LoadedBuffer> loaded_buffer; // For emscripten

	DatabaseKey database_key;
private:
	void writeToStreamCommon(OutStream& stream) const;

	//Mutex buffer_mutex; // protects buffer.  Lock should be held by threads other than the DownloadResourcesThread when reading from buffer,
	// and will be held by DownloadResourcesThread when writing to buffer.
	//js::Vector<uint8, 16> buffer	GUARDED_BY(buffer_mutex); // Streamed files will be downloaded to this buffer first, then saved to disk.
	//int64 num_buffer_readers		GUARDED_BY(buffer_mutex); // If num_buffer_readers > 0, then the buffer won't be cleared when the resource has been fully downloaded.
	//std::set<Reference<ResourceDownloadListener>> listeners;

	State state; // May be protected by mutex soon.
	std::string local_path; // Relative path on local disk from base_resources_dir.
public:
	bool external_resource; // External resources are stored locally outside of base_resources_dir.   local_path is absolute.
	bool locally_deleted; // Has resource been deleted with ResourceManager::deleteResourceLocally().  (For Emscripten)

	size_t file_size_B; // Size of resource on disk.  Just used with Emscripten.
};

typedef Reference<Resource> ResourceRef;


uint32 readFromStream(InStream& stream, Resource& resource); // Returns serialisation version

//void writeToNetworkStream(const Resource& resource, OutStream& stream); // write without version
//void readFromNetworkStream/*GivenID*/(InStream& stream, Resource& resource);


struct ResourceRefHash
{
	size_t operator() (const ResourceRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};
