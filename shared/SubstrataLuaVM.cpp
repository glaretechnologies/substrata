/*=====================================================================
SubstrataLuaVM.cpp
------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "SubstrataLuaVM.h"


#include "WorldObject.h"
#include "MessageUtils.h"
#include "Protocol.h"
#if GUI_CLIENT
#include "../gui_client/PlayerPhysics.h"
#include "../gui_client/GUIClient.h"
#endif

#include <lua/LuaVM.h>
#include <lua/LuaUtils.h>
#include <utils/StringUtils.h>
#include <utils/RuntimeCheck.h>
#include <lualib.h>
#include <Luau/Common.h>



static WorldMaterialRef getTableWorldMaterial(lua_State* state, int table_index)
{
	WorldMaterialRef mat = new WorldMaterial();
	mat->colour_texture_url = LuaUtils::getTableStringField(state, table_index, "colour_texture_url");
	mat->roughness.val = (float)LuaUtils::getTableNumberFieldWithDefault(state, table_index, "roughness_val", 0.5);
	mat->tex_matrix = LuaUtils::getTableMatrix2fFieldWithDefault(state, table_index, "tex_matrix", Matrix2f::identity());

	return mat;
}


static int user_setLinearVelocity(lua_State* state)
{
	LuaUtils::printStack(state);

	// Expected args:
	// Arg 1: user : User
	// Arg 2: velocity_change : vec3

	const double x = LuaUtils::getTableNumberField(state, /*table_index=*/2, "x");
	const double y = LuaUtils::getTableNumberField(state, /*table_index=*/2, "y");
	const double z = LuaUtils::getTableNumberField(state, /*table_index=*/2, "z");

#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	sub_lua_vm->player_physics->setLinearVel(Vec4f((float)x, (float)y, (float)z, 0));
#endif

	return 0; // Count of returned values
}


static void enqueueMessageToSend(ClientThread& client_thread, SocketBufferOutStream& packet)
{
	MessageUtils::updatePacketLengthField(packet);

	client_thread.enqueueDataToSend(packet.buf);
}


static int createObject(lua_State* state)
{
	// Expected args:
	// Arg 1: ob_params : Table
	const int initial_stack_size = lua_gettop(state);
	if(initial_stack_size < 1)
		throw glare::Exception("createObject(): Expected 1 arg (ob params)");

	if(!lua_istable(state, 1))
		throw glare::Exception("createObject(): arg 1 (ob_params) was not a table");

	const int ob_params_table_index = 1;

	WorldObjectRef ob = new WorldObject();
	
	ob->model_url = LuaUtils::getTableStringField(state, ob_params_table_index, "model_url");
	ob->pos = LuaUtils::getTableVec3dField(state, ob_params_table_index, "pos");
	ob->axis = LuaUtils::getTableVec3fFieldWithDefault(state, ob_params_table_index, "axis", Vec3f(1,0,0));
	ob->angle = (float)LuaUtils::getTableNumberField(state, ob_params_table_index, "angle");
	ob->scale = LuaUtils::getTableVec3fFieldWithDefault(state, ob_params_table_index, "scale", Vec3f(1,1,1));

	ob->setCollidable(LuaUtils::getTableBoolFieldWithDefault(state, ob_params_table_index, "collidable", /*default val=*/true));
	ob->setDynamic(LuaUtils::getTableBoolFieldWithDefault(state, ob_params_table_index, "dynamic", /*default val=*/true));
	
	ob->content = LuaUtils::getTableStringField(state, ob_params_table_index, "content");
	ob->script = LuaUtils::getTableStringField(state, ob_params_table_index, "script");

	// Materials

	const int value_type = lua_rawgetfield(state, ob_params_table_index, "materials"); // Push field value onto stack (use lua_rawgetfield to avoid metamethod call).  Returns the type of the pushed value.
	if(value_type == LUA_TTABLE)
	{
		const int max_num_mats = 100;
		for(int i=1; i<=max_num_mats; ++i)
		{
			runtimeCheck(lua_istable(state, -1)); // Materials table should still be on top of stack

			const int mat_type = lua_rawgeti(state, /*table index=*/-1, i);
			if(mat_type == LUA_TTABLE)
			{
				// Parse table
				WorldMaterialRef mat = getTableWorldMaterial(state, -1);
				ob->materials.push_back(mat);

				lua_pop(state, 1); // Pop material value
			}
			else
			{
				lua_pop(state, 1); // Pop null value
				break;
			}
		}

	}
	lua_pop(state, 1); // Pop materials value

	assert(lua_gettop(state) == initial_stack_size); // Check stack is same size as at the start of the function

	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	// Send CreateObject message to server
	{
		MessageUtils::initPacket(sub_lua_vm->gui_client->scratch_packet, Protocol::CreateObject);
		ob->writeToNetworkStream(sub_lua_vm->gui_client->scratch_packet);

		enqueueMessageToSend(*sub_lua_vm->gui_client->client_thread, sub_lua_vm->gui_client->scratch_packet);
	}

	return 0; // Count of returned values
}


static int createTimer(lua_State* state)
{
	// arg 1: onTimerEvent : function
	// arg 2: interval_time_s : Number   The interval time, in seconds
	// arg 3: repeating : bool

	// Returns a timer handle : Number

	const int initial_stack_size = lua_gettop(state);
	if(initial_stack_size < 3)
		throw glare::Exception("createTimer(): Expected 3 args");

	
	if(lua_type(state, /*index=*/1) != LUA_TFUNCTION)
		throw glare::Exception("createTimer(): arg 1 must be a function");

	// Get a reference to the onTimerEvent function
	const int onTimerEvent_ref = lua_ref(state, /*index=*/1);

	const double interval_time = lua_tonumber(state, /*index=*/2);
	const bool repeating = lua_toboolean(state, /*index=*/3);

	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;
	const double cur_time = sub_lua_vm->gui_client->total_timer.elapsed();

	// Find free timer slot
	for(int i=0; i<LuaScriptEvaluator::MAX_NUM_TIMERS; ++i)
	{
		if(script_evaluator->timers[i].id == -1) // If timer slot is free:
		{
			const int timer_id = script_evaluator->next_timer_id++; // Make new unique timer id (to avoid ABA problem)

			// Record in slot
			script_evaluator->timers[i].id = timer_id;
			script_evaluator->timers[i].onTimerEvent_ref = onTimerEvent_ref;

			TimerQueueTimer timer;
			timer.onTimerEvent_ref = onTimerEvent_ref;
			timer.tigger_time = cur_time + interval_time;
			timer.repeating = repeating;
			timer.period = interval_time;
			timer.timer_index = i;
			timer.timer_id = timer_id;
			//timer.lua_script_evaluator_handle = script_evaluator->generational_handle;
			timer.lua_script_evaluator = script_evaluator;
			sub_lua_vm->gui_client->timer_queue.addTimer(cur_time, timer);

			lua_pushnumber(state, (double)i); // Push timer id
			return 1; // Count of returned values
		}
	}

	// If got here, there are no free timer slots
	throw glare::Exception("createTimer(): Could not create timer, 4 timers already created.");
}


static int destroyTimer(lua_State* state)
{
	// arg 1: timer_id : Number

	const int timer_id = (int)lua_tonumber(state, /*index=*/1);

	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	for(int i=0; i<LuaScriptEvaluator::MAX_NUM_TIMERS; ++i)
		if(script_evaluator->timers[i].id == timer_id)
		{
			script_evaluator->destroyTimer(/*timer index=*/i);
		}
	
	return 0; // Count of returned values
}


// C++ implementation of __index for User class. Used when a User table field is read from.
static int userClassIndexMetaMethod(lua_State* state)
{
	// arg 1 is table
	// arg 2 is the key (method name)

	//size_t stringlen = 0;
	const char* key_str = lua_tolstring(state, /*index=*/2, NULL/*&stringlen*/); // May return NULL if not a string
	if(key_str)
	{
		if(stringEqual(key_str, "setLinearVelocity"))
		{
			lua_pushcfunction(state, user_setLinearVelocity, "user_setLinearVelocity");
			return 1;
		}
		else if(stringEqual(key_str, "createObject"))
		{
			lua_pushcfunction(state, createObject, "createObject");
			return 1;
		}
	}

	lua_pushnil(state);
	return 1; // Count of returned values
}


// C++ implementation of __newindex.  Used when a value is assigned to a table field.
static int userClassNewIndexMetaMethod(lua_State* state)
{
	// Arg 1: table 
	// Arg 2: key
	// Arg 3: value
	
	// Read key
	size_t stringlen = 0;
	const char* key_str = lua_tolstring(state, /*index=*/2, &stringlen); // May return NULL if not a string
	if(key_str)
	{
		
	}

	// By default, just do the assignment to the original table here.
	//lua_rawsetfield(state, /*table index=*/1, key_str);
	lua_rawset(state, /*table index=*/1); // Sets table[key] = value, pops key and value from stack.

	return 0; // Count of returned values
}


SubstrataLuaVM::SubstrataLuaVM(GUIClient* gui_client_, PlayerPhysics* player_physics_)
:	gui_client(gui_client_),
	player_physics(player_physics_)
{
	lua_vm.set(new LuaVM());

	lua_callbacks(lua_vm->state)->userdata = this;

	// Set some global functions
	lua_pushcfunction(lua_vm->state, createObject, /*debugname=*/"createObject");
	lua_setglobal(lua_vm->state, "createObject"); // Pops a value from the stack and sets it as the new value of global name.
	
	lua_pushcfunction(lua_vm->state, createTimer, /*debugname=*/"createTimer");
	lua_setglobal(lua_vm->state, "createTimer");
	
	lua_pushcfunction(lua_vm->state, destroyTimer, /*debugname=*/"destroyTimer");
	lua_setglobal(lua_vm->state, "destroyTimer");


	//--------------------------- Create metatables for our classes ---------------------------
		
	//--------------------------- Create User Metatable ---------------------------
	lua_createtable(lua_vm->state, /*num array elems=*/0, /*num non-array elems=*/2); // Create User metatable
			
	// Set userIndexMetaMethod as __index metamethod
	lua_vm->setCFunctionAsTableField(userClassIndexMetaMethod, /*debugname=*/"userIndexMetaMethod", /*table index=*/-2, /*key=*/"__index");

	// Set glareLuaNewIndexMetaMethod as __newindex metamethod
	lua_vm->setCFunctionAsTableField(userClassNewIndexMetaMethod, /*debugname=*/"userNewIndexMetaMethod", /*table index=*/-2, /*key=*/"__newindex");

	//lua_setglobal(lua_vm->state, "UserClassMetaTable"); // Pops a value from the stack and sets it as the new value of global name
	userClassMetaTable_ref = lua_ref(lua_vm->state, /*index=*/-1); // Get reference to UserClassMetaTable.  Does not pop.
	lua_pop(lua_vm->state, 1); // Pop UserClassMetaTable from stack
	//--------------------------- End create User Metatable ---------------------------

	lua_vm->finishInitAndSandbox();
}


SubstrataLuaVM::~SubstrataLuaVM()
{
}
