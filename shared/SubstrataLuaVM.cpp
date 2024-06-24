/*=====================================================================
SubstrataLuaVM.cpp
------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "SubstrataLuaVM.h"


#include "../shared/LuaScriptEvaluator.h"
#include "WorldObject.h"
#include "MessageUtils.h"
#include "Protocol.h"
#if GUI_CLIENT
#include "../gui_client/PlayerPhysics.h"
#include "../gui_client/GUIClient.h"
#elif SERVER
#include "../server/Server.h"
#endif
#include <lua/LuaVM.h>
#include <lua/LuaScript.h>
#include <lua/LuaUtils.h>
#include <utils/StringUtils.h>
#include <utils/RuntimeCheck.h>
#include <lualib.h>
#include <Luau/Common.h>


// Construct a WorldMaterial from a table on the lua stack
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

#if GUI_CLIENT
	const double x = LuaUtils::getTableNumberField(state, /*table_index=*/2, "x");
	const double y = LuaUtils::getTableNumberField(state, /*table_index=*/2, "y");
	const double z = LuaUtils::getTableNumberField(state, /*table_index=*/2, "z");

	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	sub_lua_vm->player_physics->setLinearVel(Vec4f((float)x, (float)y, (float)z, 0));
#endif

	return 0; // Count of returned values
}


#if GUI_CLIENT
static void enqueueMessageToSend(ClientThread& client_thread, SocketBufferOutStream& packet)
{
	MessageUtils::updatePacketLengthField(packet);

	client_thread.enqueueDataToSend(packet.buf);
}
#endif


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
	
	ob->model_url = LuaUtils::getTableStringFieldWithEmptyDefault(state, ob_params_table_index, "model_url");
	ob->pos = LuaUtils::getTableVec3dField(state, ob_params_table_index, "pos");
	ob->axis = LuaUtils::getTableVec3fFieldWithDefault(state, ob_params_table_index, "axis", Vec3f(1,0,0));
	ob->angle = (float)LuaUtils::getTableNumberFieldWithDefault(state, ob_params_table_index, "angle", 0.0);
	ob->scale = LuaUtils::getTableVec3fFieldWithDefault(state, ob_params_table_index, "scale", Vec3f(1,1,1));

	ob->setCollidable(LuaUtils::getTableBoolFieldWithDefault(state, ob_params_table_index, "collidable", /*default val=*/true));
	ob->setDynamic(LuaUtils::getTableBoolFieldWithDefault(state, ob_params_table_index, "dynamic", /*default val=*/true));
	
	ob->content = LuaUtils::getTableStringFieldWithEmptyDefault(state, ob_params_table_index, "content");
	ob->script = LuaUtils::getTableStringFieldWithEmptyDefault(state, ob_params_table_index, "script");

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

#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	// Send CreateObject message to server
	{
		MessageUtils::initPacket(sub_lua_vm->gui_client->scratch_packet, Protocol::CreateObject);
		ob->writeToNetworkStream(sub_lua_vm->gui_client->scratch_packet);

		enqueueMessageToSend(*sub_lua_vm->gui_client->client_thread, sub_lua_vm->gui_client->scratch_packet);
	}
#elif SERVER

	// Insert object into world state
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	ob->uid = sub_lua_vm->server->world_state->getNextObjectUID();
	ob->state = WorldObject::State_JustCreated;
	ob->from_remote_other_dirty = true;
	
	//cur_world_state->addWorldObjectAsDBDirty(new_ob); // TEMP: don't add to DB

	script_evaluator->world_state->dirty_from_remote_objects.insert(ob);
	script_evaluator->world_state->objects.insert(std::make_pair(ob->uid, ob));

#endif

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

	const double raw_interval_time = lua_tonumber(state, /*index=*/2);
	const bool repeating = lua_toboolean(state, /*index=*/3);

	// Don't allow the interval time to be too low.
	const double interval_time = myMax(0.1, raw_interval_time);

	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;
#if GUI_CLIENT
	const double cur_time = sub_lua_vm->gui_client->total_timer.elapsed();
	TimerQueue& timer_queue = sub_lua_vm->gui_client->timer_queue;
#elif SERVER
	const double cur_time = sub_lua_vm->server->total_timer.elapsed();
	TimerQueue& timer_queue = sub_lua_vm->server->timer_queue;
#endif

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
			timer_queue.addTimer(cur_time, timer);

			lua_pushnumber(state, (double)i); // Push timer id
			return 1; // Count of returned values
		}
	}

	// If got here, there are no free timer slots
	throw glare::Exception("createTimer(): Could not create timer, 4 timers already created.");
//#else
//	throw glare::Exception("createTimer(): todo on server.");
//#endif
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


static int getNumMaterials(lua_State* state)
{
	// arg 1: ob : WorldObject
	// 
	// Get object UID
	//const UID uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	const UID uid((uint64)LuaUtils::getTableLightUserDataField(state, /*table index=*/1, "uid"));
	
#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	auto res = sub_lua_vm->gui_client->world_state->objects.find(uid);
	if(res == sub_lua_vm->gui_client->world_state->objects.end())
		throw glare::Exception("No such object with given UID");

	WorldObject* ob = res.getValue().ptr();
#else
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	auto res = script_evaluator->world_state->objects.find(uid);
	if(res == script_evaluator->world_state->objects.end())
		throw glare::Exception("No such object with given UID");

	WorldObject* ob = res->second.ptr();
#endif

	lua_pushnumber(state, (double)ob->materials.size());
	return 1; // Count of returned values
}


static int getMaterial(lua_State* state)
{
	// arg 1: ob : WorldObject
	// arg 2: index : Number
	
	// Get object UID
	//const UID uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	const UID uid((uint64)LuaUtils::getTableLightUserDataField(state, /*table index=*/1, "uid"));

#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	auto res = sub_lua_vm->gui_client->world_state->objects.find(uid);
	if(res == sub_lua_vm->gui_client->world_state->objects.end())
		throw glare::Exception("No such object with given UID");

	WorldObject* ob = res.getValue().ptr();
#else
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	auto res = script_evaluator->world_state->objects.find(uid);
	if(res == script_evaluator->world_state->objects.end())
		throw glare::Exception("No such object with given UID");

	WorldObject* ob = res->second.ptr();
#endif

	const size_t index = (size_t)lua_tonumber(state, /*index=*/2);

	if(index > ob->materials.size())
		throw glare::Exception("Invalid material index");

	// Make a material table with object UID and material index
	lua_createtable(state, /*num array elems=*/0, /*num non-array elems=*/2);

	//LuaUtils::setNumberAsTableField(state, "uid", (double)uid.value());
	LuaUtils::setLightUserDataAsTableField(state, "uid", (void*)uid.value());
	//LuaUtils::setNumberAsTableField(state, "idx", (double)index);
	LuaUtils::setLightUserDataAsTableField(state, "idx", (void*)index);

	return 1; // Count of returned values
}





// C++ implementation of __index for WorldObject class. Used when a WorldObject table field is read from.
static int worldObjectClassIndexMetaMethod(lua_State* state)
{
	// arg 1 is table (WorldObject)
	// arg 2 is the key (method name)

	// Get object UID
	//const UID uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	const UID uid((uint64)LuaUtils::getTableLightUserDataField(state, /*table index=*/1, "uid"));

#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	auto res = sub_lua_vm->gui_client->world_state->objects.find(uid);
	if(res == sub_lua_vm->gui_client->world_state->objects.end())
		throw glare::Exception("No such object with given UID");

	WorldObject* ob = res.getValue().ptr();
#else
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	auto res = script_evaluator->world_state->objects.find(uid);
	if(res == script_evaluator->world_state->objects.end())
		throw glare::Exception("No such object with given UID");

	WorldObject* ob = res->second.ptr();
#endif
	
	//size_t stringlen = 0;
	const char* key_str = lua_tolstring(state, /*index=*/2, NULL/*&stringlen*/); // May return NULL if not a string
	if(key_str)
	{
		if(stringEqual(key_str, "pos"))
		{
			LuaUtils::pushVec3d(state, ob->pos);
		}
		else if(stringEqual(key_str, "axis"))
		{
			LuaUtils::pushVec3f(state, ob->axis);
		}
		else if(stringEqual(key_str, "angle"))
		{
			lua_pushnumber(state, ob->angle);
		}
		else if(stringEqual(key_str, "scale"))
		{
			LuaUtils::pushVec3f(state, ob->scale);
		}
		else if(stringEqual(key_str, "model_url"))
		{
			LuaUtils::pushString(state, ob->model_url);
		}
		else if(stringEqual(key_str, "collidable"))
		{
			lua_pushboolean(state, ob->isCollidable());
		}
		else if(stringEqual(key_str, "dynamic"))
		{
			lua_pushboolean(state, ob->isDynamic());
		}
		else if(stringEqual(key_str, "content"))
		{
			LuaUtils::pushString(state, ob->content);
		}
		else if(stringEqual(key_str, "video_autoplay"))
		{
			lua_pushboolean(state, BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_AUTOPLAY));
		}
		else if(stringEqual(key_str, "video_loop"))
		{
			lua_pushboolean(state, BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_LOOP));
		}
		else if(stringEqual(key_str, "video_muted"))
		{
			lua_pushboolean(state, BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_MUTED));
		}
		else if(stringEqual(key_str, "mass"))
		{
			lua_pushnumber(state, ob->mass);
		}
		else if(stringEqual(key_str, "friction"))
		{
			lua_pushnumber(state, ob->friction);
		}
		else if(stringEqual(key_str, "restitution"))
		{
			lua_pushnumber(state, ob->restitution);
		}
		else if(stringEqual(key_str, "centre_of_mass_offset_os"))
		{
			LuaUtils::pushVec3f(state, ob->centre_of_mass_offset_os);
		}
		else if(stringEqual(key_str, "audio_source_url"))
		{
			LuaUtils::pushString(state, ob->audio_source_url);
		}
		else if(stringEqual(key_str, "audio_volume"))
		{
			lua_pushnumber(state, ob->audio_volume);
		}
		else if(stringEqual(key_str, "getNumMaterials"))
		{
			lua_pushcfunction(state, getNumMaterials, "getNumMaterials");
		}
		else if(stringEqual(key_str, "getMaterial"))
		{
			lua_pushcfunction(state, getMaterial, "getMaterial");
		}
		else
			throw glare::Exception("Unknown field");
	}

	return 1; // Count of returned values
}


// C++ implementation of __newindex for WorldObject class.  Used when a value is assigned to a WorldObject field
static int worldObjectClassNewIndexMetaMethod(lua_State* state)
{
	// Arg 1: table 
	// Arg 2: key
	// Arg 3: value
	
	// Get object UID
	//const UID uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	const UID uid((uint64)LuaUtils::getTableLightUserDataField(state, /*table index=*/1, "uid"));

#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	auto res = sub_lua_vm->gui_client->world_state->objects.find(uid);
	if(res == sub_lua_vm->gui_client->world_state->objects.end())
		throw glare::Exception("No such object with given UID");

	WorldObject* ob = res.getValue().ptr();
#else
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	auto res = script_evaluator->world_state->objects.find(uid);
	if(res == script_evaluator->world_state->objects.end())
		throw glare::Exception("No such object with given UID");

	WorldObject* ob = res->second.ptr();
#endif

	// Read key
	const char* key_str = lua_tolstring(state, /*index=*/2, NULL); // May return NULL if not a string
	if(key_str)
	{
		bool transform_changed = false;
		bool other_changed = false;
		if(stringEqual(key_str, "pos"))
		{
			ob->pos = LuaUtils::getVec3d(state, /*index=*/3);
			transform_changed = true;
		}
		else if(stringEqual(key_str, "axis"))
		{
			ob->axis = LuaUtils::getVec3f(state, /*index=*/3);
			transform_changed = true;
		}
		else if(stringEqual(key_str, "angle"))
		{
			ob->angle = LuaUtils::getFloat(state, /*index=*/3);
			transform_changed = true;
		}
		else if(stringEqual(key_str, "scale"))
		{
			ob->scale = LuaUtils::getVec3f(state, /*index=*/3);
			transform_changed = true;
		}
		else if(stringEqual(key_str, "model_url"))
		{
			ob->model_url = LuaUtils::getString(state, /*index=*/3);

			ob->content = LuaUtils::getString(state, /*index=*/3);
			ob->from_remote_model_url_dirty = true; // TODO: rename

#if SERVER
			script_evaluator->world_state->dirty_from_remote_objects.insert(ob);
#endif
		}
		else if(stringEqual(key_str, "collidable"))
		{
			ob->setCollidable(lua_toboolean(state, /*index=*/3));
			other_changed = true;
		}
		else if(stringEqual(key_str, "dynamic"))
		{
			ob->setDynamic(lua_toboolean(state, /*index=*/3));
			other_changed = true;
		}
		else if(stringEqual(key_str, "content"))
		{
			ob->content = LuaUtils::getString(state, /*index=*/3);
			ob->from_remote_content_dirty = true; // TODO: rename

#if SERVER
			//script_evaluator->world_state->addWorldObjectAsDBDirty(ob);
			script_evaluator->world_state->dirty_from_remote_objects.insert(ob);
#endif
		}
		else if(stringEqual(key_str, "video_autoplay"))
		{
			BitUtils::setOrZeroBit(ob->flags, WorldObject::VIDEO_AUTOPLAY, lua_toboolean(state, /*index=*/3));
			other_changed = true;
		}
		else if(stringEqual(key_str, "video_loop"))
		{
			BitUtils::setOrZeroBit(ob->flags, WorldObject::VIDEO_LOOP, lua_toboolean(state, /*index=*/3));
			other_changed = true;
		}
		else if(stringEqual(key_str, "video_muted"))
		{
			BitUtils::setOrZeroBit(ob->flags, WorldObject::VIDEO_MUTED, lua_toboolean(state, /*index=*/3));
			other_changed = true;
		}
		else if(stringEqual(key_str, "mass"))
		{
			ob->mass = LuaUtils::getFloat(state, /*index=*/3);
			other_changed = true;
		}
		else if(stringEqual(key_str, "friction"))
		{
			ob->friction = LuaUtils::getFloat(state, /*index=*/3);
			other_changed = true;
		}
		else if(stringEqual(key_str, "restitution"))
		{
			ob->restitution = LuaUtils::getFloat(state, /*index=*/3);
			other_changed = true;
		}
		else if(stringEqual(key_str, "centre_of_mass_offset_os"))
		{
			ob->centre_of_mass_offset_os = LuaUtils::getVec3f(state, /*index=*/3);
			other_changed = true;
		}
		else if(stringEqual(key_str, "audio_source_url"))
		{
			ob->audio_source_url = LuaUtils::getString(state, /*index=*/3);
			other_changed = true;
		}
		else if(stringEqual(key_str, "audio_volume"))
		{
			ob->audio_volume = LuaUtils::getFloat(state, /*index=*/3);
			other_changed = true;
		}
		else
			throw glare::Exception("Unknown field");

		if(transform_changed)
		{
			ob->last_transform_update_avatar_uid = std::numeric_limits<uint32>::max();
			ob->from_remote_transform_dirty = true; // TODO: rename
#if SERVER
			script_evaluator->world_state->dirty_from_remote_objects.insert(ob);
#endif
		}
		else if(other_changed)
		{
			ob->from_remote_other_dirty = true; // TODO: rename
#if SERVER
			script_evaluator->world_state->dirty_from_remote_objects.insert(ob);
#endif
		}
	}

	return 0; // Count of returned values
}



// C++ implementation of __index for WorldMaterial class. Used when a WorldMaterial table field is read from.
static int worldMaterialClassIndexMetaMethod(lua_State* state)
{
	// arg 1 is table (WorldMaterial)
	// arg 2 is the key (method name)

	// Get object UID from the world material table
	//const UID uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	const UID uid((uint64)LuaUtils::getTableLightUserDataField(state, /*table index=*/1, "uid"));

	// Get material index from the world material table
	const size_t mat_index = (size_t)LuaUtils::getTableNumberField(state, /*table index=*/1, "idx");

#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	auto res = sub_lua_vm->gui_client->world_state->objects.find(uid);
	if(res == sub_lua_vm->gui_client->world_state->objects.end())
		throw glare::Exception("No such object with given UID");

	WorldObject* ob = res.getValue().ptr();
#else
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	auto res = script_evaluator->world_state->objects.find(uid);
	if(res == script_evaluator->world_state->objects.end())
		throw glare::Exception("No such object with given UID");

	WorldObject* ob = res->second.ptr();
#endif

	if(mat_index > ob->materials.size())
		throw glare::Exception("Invalid material index");

	WorldMaterial* mat = ob->materials[mat_index].ptr();
	
	const char* key_str = lua_tolstring(state, /*index=*/2, /*len=*/NULL); // May return NULL if not a string
	if(key_str)
	{
		if(stringEqual(key_str, "colour"))
		{
			LuaUtils::pushVec3f(state, mat->colour_rgb.toVec3());
		}
		else if(stringEqual(key_str, "colour_texture_url"))
		{
			LuaUtils::pushString(state, mat->colour_texture_url);
		}
		else if(stringEqual(key_str, "emission_rgb"))
		{
			LuaUtils::pushVec3f(state, mat->emission_rgb.toVec3());
		}
		else if(stringEqual(key_str, "emission_texture_url"))
		{
			LuaUtils::pushString(state, mat->emission_texture_url);
		}
		else if(stringEqual(key_str, "normal_map_url"))
		{
			LuaUtils::pushString(state, mat->normal_map_url);
		}
		else if(stringEqual(key_str, "roughness_val"))
		{
			lua_pushnumber(state, mat->roughness.val);
		}
		else if(stringEqual(key_str, "roughness_texture_url"))
		{
			LuaUtils::pushString(state, mat->roughness.texture_url);
		}
		else if(stringEqual(key_str, "metallic_fraction_val"))
		{
			lua_pushnumber(state, mat->metallic_fraction.val);
		}
		else if(stringEqual(key_str, "opacity_val"))
		{
			lua_pushnumber(state, mat->opacity.val);
		}
		else if(stringEqual(key_str, "tex_matrix"))
		{
			LuaUtils::pushMatrix2f(state, mat->tex_matrix);
		}
		else if(stringEqual(key_str, "emission_lum_flux_or_lum"))
		{
			lua_pushnumber(state, mat->emission_lum_flux_or_lum);
		}
		else if(stringEqual(key_str, "hologram"))
		{
			lua_pushboolean(state, BitUtils::isBitSet(mat->flags, WorldMaterial::HOLOGRAM_FLAG));
		}
		else if(stringEqual(key_str, "double_sided"))
		{
			lua_pushboolean(state, BitUtils::isBitSet(mat->flags, WorldMaterial::DOUBLE_SIDED_FLAG));
		}
		else
			throw glare::Exception("Unknown field");
	}

	return 1; // Count of returned values
}


// C++ implementation of __index for User class. Used when a User table field is read from.
static int userClassIndexMetaMethod(lua_State* state)
{
	// arg 1 is table
	// arg 2 is the key (field name)

#if SERVER
	// Get user UID from the user table
	//const UserID uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));

	//SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	//auto res = sub_lua_vm->server->world_state->user_id_to_users.find(uid);
	//if(res == sub_lua_vm->server->world_state->user_id_to_users.end())
	//	throw glare::Exception("No such user with given UID");

	//User* user = res->second.ptr();
#endif


	const char* key_str = lua_tolstring(state, /*index=*/2, /*stringlen=*/NULL); // May return NULL if not a string
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
//#if SERVER
//		else if(stringEqual(key_str, "name"))
//		{
//			LuaUtils::pushString(user->name);
//			return 1;
//		}
//#endif
		else
			throw glare::Exception("Unknown field");
	}

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


SubstrataLuaVM::SubstrataLuaVM()
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

	//--------------------------- Create WorldObject Metatable ---------------------------
	lua_createtable(lua_vm->state, /*num array elems=*/0, /*num non-array elems=*/2); // Create WorldObject metatable
			
	// Set worldObjectClassIndexMetaMethod as __index metamethod
	lua_vm->setCFunctionAsTableField(worldObjectClassIndexMetaMethod, /*debugname=*/"worldObjectClassIndexMetaMethod", /*table index=*/-2, /*key=*/"__index");

	// Set worldObjectClassNewIndexMetaMethod as __newindex metamethod
	lua_vm->setCFunctionAsTableField(worldObjectClassNewIndexMetaMethod, /*debugname=*/"worldObjectClassNewIndexMetaMethod", /*table index=*/-2, /*key=*/"__newindex");

	worldObjectClassMetaTable_ref = lua_ref(lua_vm->state, /*index=*/-1); // Get reference to WorldObjectMetaTable.  Does not pop.
	lua_pop(lua_vm->state, 1); // Pop WorldObjectMetaTable from stack
	//--------------------------- End create User Metatable ---------------------------
	// 
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
