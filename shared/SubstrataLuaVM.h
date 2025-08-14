/*=====================================================================
SubstrataLuaVM.h
----------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include <maths/Vec4f.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/UniqueRef.h>
#include <utils/HashMap.h>
#include <string>
class PlayerPhysics;
class GUIClient;
class Server;
class LuaVM;


/*=====================================================================
SubstrataLuaVM
--------------


Userdata
--------

__________________
| SubstrataLuaVM |
| -------------- |
|                |<--------------------------------
| lua_vm         |                                 |
 --|--------------                                 |
   |                                               |
   v                                               |
_________                   _______________        |                   
| LuaVM |                   | lua_State   |        |                   
| ----- |                   | ---------   |        |                    
|       |                   | cb.userdata |--------          NOTE: cb is callback data
|       |                   |             |                            
| state |  ----------->     | ud          |                  NOTE: ud is auxiliary data to frealloc / glareLuaAlloc        
 ------                     ---|----------
  ^                            |
  |----------------------------



_____________________
|LuaScriptEvaluator |
|------------------ |
|lua_script         |
 -|-----------------
  |         ^
  |         |
  |          -------
  |                 |
  v                 |
________________    |         _____________
| LuaScript    |    |         | lua_State |
| ---------    |    |         | --------- |
| lua_vm       |    |         |           |
|              |    |         |           |
| userdata  ---|----          |           |
| thread_state | -----------> | userdata  |             NOTE: userdata is accessed via lua_getthreaddata()
 --------------                -----|-----
  ^                                 |
  |----------------------------------

=====================================================================*/
class SubstrataLuaVM : public ThreadSafeRefCounted
{
public:
#if GUI_CLIENT
	struct SubstrataLuaVMArgs
	{
		GUIClient* gui_client;
		PlayerPhysics* player_physics;
	};
#endif
#if SERVER
	struct SubstrataLuaVMArgs
	{
		SubstrataLuaVMArgs(Server* server_) : server(server_) {}
		Server* server;
	};
#endif

	SubstrataLuaVM(const SubstrataLuaVMArgs& args);

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

	HashMap<uint32, int> metatable_uid_to_ref_map;
};
