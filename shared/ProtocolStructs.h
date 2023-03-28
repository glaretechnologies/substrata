/*=====================================================================
ProtocolStructs.h
-----------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "UID.h"
#include <maths/vec3.h>
#include <utils/Platform.h>

#pragma pack(push, 1)
struct SummonObjectMessageClientToServer
{
	UID object_uid;
	Vec3d pos;
	Vec3f axis;
	float angle;
	float aabb_data[6];
};
#pragma pack(pop)
static_assert(sizeof(SummonObjectMessageClientToServer) == sizeof(UID) + sizeof(Vec3d) + sizeof(Vec3f) + sizeof(float) + sizeof(float)*6);


#pragma pack(push, 1)
struct SummonObjectMessageServerToClient
{
	UID object_uid;
	Vec3d pos;
	Vec3f axis;
	float angle;
	float aabb_data[6];
	uint32 transform_update_avatar_uid;
};
#pragma pack(pop)
static_assert(sizeof(SummonObjectMessageServerToClient) == sizeof(UID) + sizeof(Vec3d) + sizeof(Vec3f) + sizeof(float) + sizeof(float)*6 + sizeof(uint32));
