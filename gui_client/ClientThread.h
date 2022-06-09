/*=====================================================================
ClientThread.h
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-16 22:59:23 +1300
=====================================================================*/
#pragma once


#include "WorldState.h"
#include <MessageableThread.h>
#include <Platform.h>
#include <MyThread.h>
#include <SocketBufferOutStream.h>
#include <EventFD.h>
#include <ThreadManager.h>
#include <MySocket.h>
#include <Vector.h>
#include <BufferInStream.h>
#include <set>
#include <string>
class WorkUnit;
class PrintOutput;
class ThreadMessageSink;
class Server;
class MainWindow;
struct tls_config;
namespace glare { class PoolAllocator; }


class ChatMessage : public ThreadMessage
{
public:
	ChatMessage(const std::string& name_, const std::string& msg_) : name(name_), msg(msg_) {}
	std::string name, msg;
};


class AvatarPerformGestureMessage : public ThreadMessage
{
public:
	AvatarPerformGestureMessage(const UID avatar_uid_, const std::string& gesture_name_) : avatar_uid(avatar_uid_), gesture_name(gesture_name_) {}
	UID avatar_uid;
	std::string gesture_name;
};


class AvatarStopGestureMessage : public ThreadMessage
{
public:
	AvatarStopGestureMessage(const UID avatar_uid_) : avatar_uid(avatar_uid_) {}
	UID avatar_uid;
};


// When the server wants a file from the client, it will send the client a GetFile protocol message.  The ClientThread will send this 'GetFileMessage' back to MainWindow.
class GetFileMessage : public ThreadMessage
{
public:
	GetFileMessage(const std::string& URL_) : URL(URL_) {}
	std::string URL;
};


// When the server has file uploaded to it, it will send a message to clients, so they can download it.
class NewResourceOnServerMessage : public ThreadMessage
{
public:
	NewResourceOnServerMessage(const std::string& URL_) : URL(URL_) {}
	std::string URL;
};


class AvatarCreatedMessage : public ThreadMessage
{
public:
	AvatarCreatedMessage(const UID& avatar_uid_) : avatar_uid(avatar_uid_) {}
	UID avatar_uid;
};


class AvatarIsHereMessage : public ThreadMessage
{
public:
	AvatarIsHereMessage(const UID& avatar_uid_) : avatar_uid(avatar_uid_) {}
	UID avatar_uid;
};


class UserSelectedObjectMessage : public ThreadMessage
{
public:
	UserSelectedObjectMessage(const UID& avatar_uid_, const UID& object_uid_) : avatar_uid(avatar_uid_), object_uid(object_uid_) {}
	UID avatar_uid, object_uid;
};


class UserDeselectedObjectMessage : public ThreadMessage
{
public:
	UserDeselectedObjectMessage(const UID& avatar_uid_, const UID& object_uid_) : avatar_uid(avatar_uid_), object_uid(object_uid_) {}
	UID avatar_uid, object_uid;
};


class ClientConnectedToServerMessage : public ThreadMessage
{
};


class ClientConnectingToServerMessage : public ThreadMessage
{
};


class ClientDisconnectedFromServerMessage : public ThreadMessage
{
public:
	ClientDisconnectedFromServerMessage() {}
	ClientDisconnectedFromServerMessage(const std::string& error_message_) : error_message(error_message_) {}
	std::string error_message;
};


class InfoMessage : public ThreadMessage
{
public:
	InfoMessage(const std::string& msg_) : msg(msg_) {}
	std::string msg;
};


class ErrorMessage : public ThreadMessage
{
public:
	ErrorMessage(const std::string& msg_) : msg(msg_) {}
	std::string msg;
};


class LoggedInMessage : public ThreadMessage
{
public:
	LoggedInMessage(UserID user_id_, const std::string& username_) : user_id(user_id_), username(username_) {}
	UserID user_id;
	std::string username;
	AvatarSettings avatar_settings;
};


class LoggedOutMessage : public ThreadMessage
{
public:
	LoggedOutMessage() {}
};


class SignedUpMessage : public ThreadMessage
{
public:
	SignedUpMessage(UserID user_id_, const std::string& username_) : user_id(user_id_), username(username_) {}
	UserID user_id;
	std::string username;
};


class ServerAdminMessage : public ThreadMessage
{
public:
	ServerAdminMessage(const std::string& msg_) : msg(msg_) {}
	std::string msg;
};


/*=====================================================================
ClientThread
-------------------
Maintains network connection to server.
=====================================================================*/
class ClientThread : public MessageableThread
{
public:
	ClientThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue, const std::string& hostname, int port,
		const std::string& avatar_URL, const std::string& world_name, struct tls_config* config, const Reference<glare::PoolAllocator>& world_ob_pool_allocator);
	virtual ~ClientThread();

	virtual void doRun();

	void enqueueDataToSend(const std::string& data); // threadsafe
	void enqueueDataToSend(const SocketBufferOutStream& packet); // threadsafe

	virtual void kill();

	void killConnection();

	bool all_objects_received;
	Reference<WorldState> world_state;
	UID client_avatar_uid;
private:
	WorldObjectRef allocWorldObject();

	glare::AtomicInt should_die;
	ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue;
	EventFD event_fd;
	std::string hostname;
	int port;
	SocketInterfaceRef socket;
	std::string avatar_URL;
	std::string world_name;
	struct tls_config* config;

	Mutex data_to_send_mutex;
	js::Vector<uint8, 16> data_to_send;
	js::Vector<uint8, 16> temp_data_to_send;

	BufferInStream msg_buffer;

	Reference<glare::PoolAllocator> world_ob_pool_allocator;
};
