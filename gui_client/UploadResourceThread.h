/*=====================================================================
UploadResourceThread.h
----------------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#pragma once


#include "../shared/URLString.h"
#include <MessageableThread.h>
#include <SocketInterface.h>
#include <string>
struct tls_config;


struct ResourceToUpload : public ThreadSafeRefCounted
{
	ResourceToUpload(const std::string& local_path_, const URLString& resource_URL_) : local_path(local_path_), resource_URL(resource_URL_) {}
	std::string local_path;
	URLString resource_URL;
};


/*=====================================================================
UploadResourceThread
--------------------
Uploads resources to the server
=====================================================================*/
class UploadResourceThread : public MessageableThread
{
public:
	UploadResourceThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue, ThreadSafeQueue<Reference<ResourceToUpload>>* upload_queue, const std::string& hostname, int port,
		const std::string& username, const std::string& password, struct tls_config* config, glare::AtomicInt* num_resources_uploading);
	virtual ~UploadResourceThread();

	virtual void doRun();

	virtual void kill();

	void killConnection();

private:
	ThreadSafeQueue<Reference<ResourceToUpload>>* upload_queue;
	std::string hostname;
	std::string username, password;
	int port;
	struct tls_config* config;
	glare::AtomicInt* num_resources_uploading;

	glare::AtomicInt should_die;
public:
	SocketInterfaceRef socket;
};
