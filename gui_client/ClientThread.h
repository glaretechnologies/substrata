/*=====================================================================
ClientThread.h
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-16 22:59:23 +1300
=====================================================================*/
#pragma once


#include "../shared/WorldState.h"
#include <MessageableThread.h>
#include <Platform.h>
#include <MyThread.h>
#include <SocketBufferOutStream.h>
#include <EventFD.h>
#include <ThreadManager.h>
#include <mysocket.h>
#include <set>
#include <string>
class WorkUnit;
class PrintOutput;
class ThreadMessageSink;
class Server;
class MainWindow;


class ChatMessage : public ThreadMessage
{
public:
	ChatMessage(const std::string& name_, const std::string& msg_) : name(name_), msg(msg_) {}
	std::string name, msg;
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


/*=====================================================================
ClientThread
-------------------
Maintains network connection to server.
=====================================================================*/
class ClientThread : public MessageableThread
{
public:
	ClientThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue, const std::string& hostname, int port, MainWindow* main_window,
		const std::string& avatar_URL);
	virtual ~ClientThread();

	virtual void doRun();

	void enqueueDataToSend(const std::string& data); // threadsafe
	void enqueueDataToSend(const SocketBufferOutStream& packet); // threadsafe

	void killConnection();

	Reference<WorldState> world_state;
	UID client_avatar_uid;
private:
	ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue;
	ThreadSafeQueue<std::string> data_to_send;
	EventFD event_fd;
	std::string hostname;
	int port;
	MySocketRef socket;
	MainWindow* main_window;
	std::string avatar_URL;
};
