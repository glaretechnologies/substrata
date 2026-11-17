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
#include <utils/Reference.h>
#include <string>
#include <vector>
class PlayerPhysics;
class GUIClient;
class Server;


/*=====================================================================
SubstrataLuaVM
--------------

__________________
| SubstrataLuaVM |
| -------------- |
|                |
| lua_vm         |<-----
 --|--------------     |
   |                   |
   v                   |
_________              |    _____________                      ________________ 
| LuaVM |              |    | lua_State |                      | global_State  |
| ----- |              |    | --------- |                      | ------------  |
|       |              |    |           |                      |               |
|       |              |    |           |                      |               |                            
| state | -------------|--> | global ---|--------------------> | ud            |                NOTE: ud is auxiliary data to frealloc / glareLuaAlloc        
 ------                |    | userdata  |                      ---|------------
  ^                    |     -----------                          |       ^
  |                    |                                          |       |
  ---------------------|-------------------------------------------       |
                       |                                                  |
                       |                                                  |
                       |                                                  |
                       -----------------                                  |
                                       |                                  |
                                       |                                  |
__________________________________     |         _____________            |
| SubstrataLuaScript : LuaScript  |    |         | lua_State |            |
| ---------                       |    |         | --------- |            |
| lua_vm                          |    |         |           |            |
| substrata_lua_vm ---------------|----          |           |            |
| userdata                        |              | global ---|-------------
| thread_state -------------------|------------> | userdata--|---            
 ---------------------------------                -----------   |                                                                 NOTE: userdata is accessed via lua_getthreaddata()
                 ^                                              |
                 |                                              |
                 ------------------------------------------------

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
		// SubstrataLuaScript in LuaScript::userdata are not registered on such a VM, since build scripts store their own state there.
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

	int getNumLuaValueReferences() const { return lua_vm->getNumLuaValueReferences(); }


	Reference<LuaVM> lua_vm;

#if GUI_CLIENT
	GUIClient* gui_client;
	PlayerPhysics* player_physics;
	bool is_build_vm; // If true, this VM runs one-shot build scripts (see LuaBuildScript) rather than per-object scripts
#endif

#if SERVER
	Server* server;
#endif
	
	LuaVM::LuaValueRef worldObjectClassMetaTable_ref;
	LuaVM::LuaValueRef worldMaterialClassMetaTable_ref;
	LuaVM::LuaValueRef userClassMetaTable_ref;
	LuaVM::LuaValueRef avatarClassMetaTable_ref;
	LuaVM::LuaValueRef scriptUIWidgetMetaTable_ref; // For handles returned by createUIImage(), createUIButton() etc.

	HashMap<uint32, LuaVM::LuaValueRef> metatable_uid_to_ref_map;

	// Stats tracking:
	uint64 current_period_start_total_num_interrupts;
	double last_period_interrupts_per_sec;
};
