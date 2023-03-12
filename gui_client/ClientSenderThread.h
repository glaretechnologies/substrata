/*=====================================================================
ClientSenderThread.h
--------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include <MessageableThread.h>
#include <Platform.h>
#include <Vector.h>
#include <ArrayRef.h>
#include <SocketInterface.h>


/*=====================================================================
ClientSenderThread
------------------
Sending is done on a separate thread to avoid deadlocks where both the client and server 
get stuck sending large amounts of data to each other, without doing any reads.
=====================================================================*/
class ClientSenderThread : public MessageableThread
{
public:
	ClientSenderThread(Reference<SocketInterface> tls_socket);
	virtual ~ClientSenderThread();

	virtual void doRun();

	void enqueueDataToSend(const ArrayRef<uint8> data); // threadsafe

	virtual void kill();

private:
	SocketInterfaceRef socket;
	glare::AtomicInt should_die;

	Condition stuff_to_do_condition;
	
	Mutex mutex;
	js::Vector<uint8, 16> data_to_send			GUARDED_BY(mutex);
	js::Vector<uint8, 16> temp_data_to_send;
};
