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
class LuaVM;


/*=====================================================================
SubstrataLuaVM
------------------

=====================================================================*/
class SubstrataLuaVM : public RefCounted
{
public:
	SubstrataLuaVM(GUIClient* gui_client, PlayerPhysics* player_physics);
	virtual ~SubstrataLuaVM();


	UniqueRef<LuaVM> lua_vm;

	GUIClient* gui_client;
	PlayerPhysics* player_physics;
	
	int userClassMetaTable_ref;
};
