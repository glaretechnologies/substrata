/*=====================================================================
ThreadMessages.h
----------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#pragma once


#include <ThreadMessage.h>
#include <string>


enum GuiClientThreadMessages
{
	Msg_ModelLoadedThreadMessage,
	Msg_TextureLoadedThreadMessage,
	Msg_AudioLoadedThreadMessage,
	Msg_ScriptLoadedThreadMessage,
	Msg_ClientConnectingToServerMessage,
	Msg_ClientConnectedToServerMessage,
	Msg_AudioStreamToServerStartedMessage,
	Msg_AudioStreamToServerEndedMessage,
	Msg_RemoteClientAudioStreamToServerStarted,
	Msg_RemoteClientAudioStreamToServerEnded,
	Msg_ClientProtocolTooOldMessage,
	Msg_ClientDisconnectedFromServerMessage,
	Msg_AvatarIsHereMessage,
	Msg_AvatarCreatedMessage,
	Msg_AvatarPerformGestureMessage,
	Msg_AvatarStopGestureMessage,
	Msg_ChatMessage,
	Msg_InfoMessage,
	Msg_ErrorMessage,
	Msg_LogMessage,
	Msg_LoggedInMessage,
	Msg_LoggedOutMessage,
	Msg_SignedUpMessage,
	Msg_ServerAdminMessage,
	Msg_WorldSettingsReceivedMessage,
	Msg_WorldDetailsReceivedMessage,
	Msg_MapTilesResultReceivedMessage,
	Msg_UserSelectedObjectMessage,
	Msg_UserDeselectedObjectMessage,
	Msg_GetFileMessage,
	Msg_NewResourceOnServerMessage,
	Msg_ResourceDownloadedMessage,
	Msg_TerrainChunkGeneratedMsg,
	Msg_WindNoiseLoaded,
	Msg_TextureUploadedMessage = 1000, // Should match the values from <opengl/OpenGLUploadThread.h>
	Msg_AnimatedTextureUpdated = 1001,
	Msg_GeometryUploadedMessage = 1002,
	Msg_OpenGLUploadErrorMessage = 1003,
};


class LogMessage : public ThreadMessage
{
public:
	LogMessage(const std::string& msg_) : ThreadMessage(Msg_LogMessage), msg(msg_) {}
	std::string msg;
};


class InfoMessage : public ThreadMessage
{
public:
	InfoMessage(const std::string& msg_) : ThreadMessage(Msg_InfoMessage), msg(msg_) {}
	std::string msg;
};


class ErrorMessage : public ThreadMessage
{
public:
	ErrorMessage(const std::string& msg_) : ThreadMessage(Msg_ErrorMessage), msg(msg_) {}
	std::string msg;
};
