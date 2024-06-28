/*=====================================================================
SubstrataLuaVM.h
----------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include <maths/Vec4f.h>
#include <utils/RefCounted.h>
#include <utils/UniqueRef.h>
#include <string>
class PlayerPhysics;
class GUIClient;
class Server;
class LuaVM;


/*=====================================================================
SubstrataLuaVM
--------------

=====================================================================*/
class SubstrataLuaVM : public RefCounted
{
public:
	SubstrataLuaVM();
	~SubstrataLuaVM();


	UniqueRef<LuaVM> lua_vm;

#if GUI_CLIENT
	GUIClient* gui_client;
	PlayerPhysics* player_physics;
#endif

#if SERVER
	Server* server;
#endif
	
	int worldObjectClassMetaTable_ref;
	int worldMaterialClassMetaTable_ref;
	int userClassMetaTable_ref;
	int avatarClassMetaTable_ref;
};
