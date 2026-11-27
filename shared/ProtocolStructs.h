/*=====================================================================
ProtocolStructs.h
-----------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "UID.h"
#include <maths/vec3.h>
#include <maths/Quat.h>
#include <utils/Platform.h>


#pragma pack(push, 1)
struct SummonObjectMessageClientToServer
{
	UID object_uid;
	Vec3d pos;
	Vec3f axis;
	float angle;
};
#pragma pack(pop)
static_assert(sizeof(SummonObjectMessageClientToServer) == sizeof(UID) + sizeof(Vec3d) + sizeof(Vec3f) + sizeof(float));


#pragma pack(push, 1)
struct SummonObjectMessageServerToClient
{
	UID object_uid;
	Vec3d pos;
	Vec3f axis;
	float angle;
	uint32 transform_update_avatar_uid;
};
#pragma pack(pop)
static_assert(sizeof(SummonObjectMessageServerToClient) == sizeof(UID) + sizeof(Vec3d) + sizeof(Vec3f) + sizeof(float) + sizeof(uint32));


// The spatial context a Builder AI message was sent in: where the user is and what they are looking at.
// Sent (as a blob) after the message text in a BuilderAIUserMessage, since "here" means somewhere different each message.
#pragma pack(push, 1)
struct BuilderAIContext
{
	Vec3d cam_pos;
	Vec3d cam_forwards;
	uint32 crosshair_pos_valid; // Did the client's crosshair ray hit anything?  (0 or 1)
	Vec3d crosshair_pos;
	uint32 have_selected_ob; // (0 or 1)
	uint64 selected_ob_uid;
};
#pragma pack(pop)
static_assert(sizeof(BuilderAIContext) == sizeof(Vec3d)*3 + sizeof(uint32)*2 + sizeof(uint64));


#pragma pack(push, 1)
struct AvatarTransformUpdateStruct
{
	UID avatar_uid;
	Vec3d pos;
	Vec3f rotation;
	uint32 anim_state;
};
#pragma pack(pop)

static_assert(sizeof(AvatarTransformUpdateStruct) == 48, "sizeof(AvatarTransformUpdateStruct) == 48"); // Check tightly packed with no padding.


#pragma pack(push, 1)
struct ObjectTransformUpdateStruct
{
	UID object_uid;
	Vec3d pos;
	Vec3f axis;
	float angle;
	Vec3f scale;
};
#pragma pack(pop)

static_assert(sizeof(ObjectTransformUpdateStruct) == 60, "sizeof(ObjectTransformUpdateStruct) == 60"); // Check tightly packed with no padding.


// NOTE: can't use #pragma pack(push, 1) because we have an aligned member (Quatf rot).
struct ObjectPhysicsTransformUpdateStruct
{
	UID object_uid;
	Vec3d pos;
	Quatf rot;
	Vec3f linear_vel;
	Vec3f angular_vel;
	double client_cur_time;
};

static_assert(sizeof(ObjectPhysicsTransformUpdateStruct) == 80, "sizeof(ObjectPhysicsTransformUpdateStruct) == 80"); // Check tightly packed with no padding.
