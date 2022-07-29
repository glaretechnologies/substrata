/*=====================================================================
ThreadMessages.h
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include <ThreadMessage.h>
#include <string>


class LogMessage : public ThreadMessage
{
public:
	LogMessage(const std::string& msg_) : msg(msg_) {}
	std::string msg;
};
