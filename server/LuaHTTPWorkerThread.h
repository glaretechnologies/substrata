/*=====================================================================
LuaHTTPWorkerThread.h
---------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "../shared/UID.h"
#include <networking/HTTPClient.h>
#include <utils/MessageableThread.h>
class LuaHTTPRequestManager;
class HTTPClient;


/*=====================================================================
LuaHTTPWorkerThread
-------------------

=====================================================================*/
class LuaHTTPWorkerThread : public MessageableThread
{
public:
	LuaHTTPWorkerThread(LuaHTTPRequestManager* manager);

	virtual ~LuaHTTPWorkerThread();

	virtual void doRun() override;

	virtual void kill() override;

private:
	LuaHTTPRequestManager* manager;

	Reference<HTTPClient> http_client;
};
