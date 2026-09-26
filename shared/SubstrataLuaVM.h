/*=====================================================================
SubstrataLuaVM.h
----------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include <lua/LuaVM.h>
#include <maths/Vec4f.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/UniqueRef.h>
#include <utils/HashMap.h>
#include <string>
#include <vector>
class PlayerPhysics;
class GUIClient;
class Server;


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
		SubstrataLuaVMArgs() : gui_client(NULL), player_physics(NULL), is_build_vm(false) {}

		GUIClient* gui_client;
		PlayerPhysics* player_physics;

		// If true, this VM runs one-shot build scripts (see LuaBuildScript) rather than per-object scripts.  Globals that need a
		// LuaScriptEvaluator in LuaScript::userdata are not registered on such a VM, since build scripts store their own state there.
		bool is_build_vm;

		// Extra global functions for the underlying LuaVM, set before it is sandboxed.  See LuaVMOptions::c_funcs.
		std::vector<LuaCFunction> vm_c_funcs;
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
	bool is_build_vm;
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
