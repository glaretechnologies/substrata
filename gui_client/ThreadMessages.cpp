/*=====================================================================
ThreadMessages.cpp
------------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#include "ThreadMessages.h"


// Avoid including OpenGLUploadThread.h from ThreadMessages.h to reduce compile times, but check the enums are a match here.
#include <opengl/OpenGLUploadThread.h>
#include <opengl/OpenGLMeshRenderData.h>
#include <graphics/BatchedMesh.h>


static_assert(Msg_TextureUploadedMessage   == OpenGLUploadThreadMessages_TextureUploadedMessage);
static_assert(Msg_AnimatedTextureUpdated   == OpenGLUploadThreadMessages_AnimatedTextureUpdated);
static_assert(Msg_GeometryUploadedMessage  == OpenGLUploadThreadMessages_GeometryUploadedMessage);
static_assert(Msg_OpenGLUploadErrorMessage == OpenGLUploadThreadMessages_OpenGLUploadErrorMessage);

