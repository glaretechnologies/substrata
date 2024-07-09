/*=====================================================================
SubstrataLuaVM.cpp
------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "SubstrataLuaVM.h"


#include "../shared/LuaScriptEvaluator.h"
#include "../shared/ObjectEventHandlers.h"
#include "WorldObject.h"
#include "MessageUtils.h"
#include "Protocol.h"
#if GUI_CLIENT
#include "../gui_client/PlayerPhysics.h"
#include "../gui_client/GUIClient.h"
#include "../gui_client/VehiclePhysics.h"
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


// String atom tables.  Used for fast selection of table fields without having to do string comparisons.
struct StringAtom
{
	const char* str;
	int atom;
};

enum StringAtomEnum
{
	// Common
	Atom_uid,
	Atom_idx,

	// WorldObject
	Atom_model_url,
	Atom_pos,
	Atom_axis,
	Atom_angle,
	Atom_scale,
	Atom_collidable,
	Atom_dynamic,
	Atom_sensor,
	Atom_content,
	Atom_video_autoplay,
	Atom_video_loop,
	Atom_video_muted,
	Atom_mass,
	Atom_friction,
	Atom_restitution,
	Atom_centre_of_mass_offset_os,
	Atom_audio_source_url,
	Atom_audio_volume,
	Atom_getNumMaterials,
	Atom_getMaterial,
	Atom_script,
	Atom_materials,

	// WorldMaterial
	Atom_colour,
	Atom_colour_texture_url,
	Atom_emission_rgb,
	Atom_emission_texture_url,
	Atom_normal_map_url,
	Atom_roughness_val,
	Atom_roughness_texture_url,
	Atom_metallic_fraction_val,
	Atom_opacity_val,
	Atom_tex_matrix,
	Atom_emission_lum_flux_or_lum,
	Atom_hologram,
	Atom_double_sided,

	// Avatar
	Atom_name,
	Atom_linear_velocity,
	Atom_vehicle_inside,

	// Events
	Atom_onUserUsedObject,
	Atom_onUserTouchedObject,
	Atom_onUserMovedNearToObject,
	Atom_onUserMovedAwayFromObject,
	Atom_onUserEnteredParcel,
	Atom_onUserExitedParcel,
	Atom_onUserEnteredVehicle,
	Atom_onUserExitedVehicle,
};

static StringAtom string_atoms[] = 
{
	// Common
	StringAtom({"uid",						Atom_uid						}),
	StringAtom({"idx",						Atom_idx						}),

	// WorldObject
	StringAtom({"model_url",				Atom_model_url					}),
	StringAtom({"pos",						Atom_pos						}),
	StringAtom({"axis",						Atom_axis						}),
	StringAtom({"angle",					Atom_angle						}),
	StringAtom({"scale",					Atom_scale						}),
	StringAtom({"collidable",				Atom_collidable					}),
	StringAtom({"dynamic",					Atom_dynamic,					}),
	StringAtom({"sensor",					Atom_sensor,					}),
	StringAtom({"content",					Atom_content,					}),
	StringAtom({"video_autoplay",			Atom_video_autoplay,			}),
	StringAtom({"video_loop",				Atom_video_loop,				}),
	StringAtom({"video_muted",				Atom_video_muted,				}),
	StringAtom({"mass",						Atom_mass,						}),
	StringAtom({"friction",					Atom_friction,					}),
	StringAtom({"restitution",				Atom_restitution,				}),
	StringAtom({"centre_of_mass_offset_os",	Atom_centre_of_mass_offset_os,	}),
	StringAtom({"audio_source_url",			Atom_audio_source_url,			}),
	StringAtom({"audio_volume",				Atom_audio_volume,				}),
	StringAtom({"getNumMaterials",			Atom_getNumMaterials,			}),
	StringAtom({"getMaterial",				Atom_getMaterial,				}),
	StringAtom({"script",					Atom_script,					}),
	StringAtom({"materials",				Atom_materials,					}),

	// WorldMaterial
	StringAtom({"colour",					Atom_colour,					}),
	StringAtom({"colour_texture_url",		Atom_colour_texture_url,		}),
	StringAtom({"emission_rgb",				Atom_emission_rgb,				}),
	StringAtom({"emission_texture_url",		Atom_emission_texture_url,		}),
	StringAtom({"normal_map_url",			Atom_normal_map_url,			}),
	StringAtom({"roughness_val",			Atom_roughness_val,				}),
	StringAtom({"roughness_texture_url",	Atom_roughness_texture_url,		}),
	StringAtom({"metallic_fraction_val",	Atom_metallic_fraction_val,		}),
	StringAtom({"opacity_val",				Atom_opacity_val,				}),
	StringAtom({"tex_matrix",				Atom_tex_matrix,				}),
	StringAtom({"emission_lum_flux_or_lum",	Atom_emission_lum_flux_or_lum,	}),
	StringAtom({"hologram",					Atom_hologram,					}),
	StringAtom({"double_sided",				Atom_double_sided,				}),

	// Avatar
	StringAtom({"name",						Atom_name,						}),
	StringAtom({"linear_velocity",			Atom_linear_velocity,			}),
	StringAtom({"vehicle_inside",			Atom_vehicle_inside,			}),

	// Events:
	StringAtom({"onUserUsedObject",			Atom_onUserUsedObject,			}),
	StringAtom({"onUserTouchedObject",		Atom_onUserTouchedObject,		}),
	StringAtom({"onUserMovedNearToObject",	Atom_onUserMovedNearToObject,	}),
	StringAtom({"onUserMovedAwayFromObject",Atom_onUserMovedAwayFromObject,	}),
	StringAtom({"onUserEnteredParcel",		Atom_onUserEnteredParcel,		}),
	StringAtom({"onUserExitedParcel",		Atom_onUserExitedParcel,		}),
	StringAtom({"onUserEnteredVehicle",		Atom_onUserEnteredVehicle,		}),
	StringAtom({"onUserExitedVehicle",		Atom_onUserExitedVehicle,		}),
};


#if GUI_CLIENT
// Define this function to satisfy the Thread Safety Analysis.  Checks that we hold the world state lock.
// ASSERT_CAPABILITY means that after this function has finished executing, the calling thread is guaranteed to hold world_state->mutex.
static inline void checkHoldWorldStateMutex(LuaScriptEvaluator* script_evaluator, WorldState* world_state) ASSERT_CAPABILITY(world_state->mutex)
{
	// Don't bother checking the actual mutex addresses, just rely on the type system (WorldStateLock) and assume we don't mix up multiple WorldStateLocks, which should be unlikely.
	if(!(script_evaluator->cur_world_state_lock)) // && (&script_evaluator->cur_world_state_lock->getMutex() == &world_state->mutex)))
	{
		assert(0);
		throw glare::Exception("Internal error: didn't hold correct world state lock");
	}
}
#endif


// Construct a WorldMaterial from a table on the lua stack
#if 0
static WorldMaterialRef getTableWorldMaterial(lua_State* state, int table_index)
{
	WorldMaterialRef mat = new WorldMaterial();
	mat->colour_texture_url = LuaUtils::getTableStringField(state, table_index, "colour_texture_url");
	mat->roughness.val = (float)LuaUtils::getTableNumberFieldWithDefault(state, table_index, "roughness_val", 0.5);
	mat->tex_matrix = LuaUtils::getTableMatrix2fFieldWithDefault(state, table_index, "tex_matrix", Matrix2f::identity());

	return mat;
}
#endif


static std::string errorContextString(lua_State* state)
{
	return "\n" + LuaUtils::getCallStackAsString(state);
}


static void checkNumArgs(lua_State* state, int num_args_required)
{
	const int num_args = lua_gettop(state);
	if(num_args < num_args_required)
		throw glare::Exception("Expected " + toString(num_args_required) + " arg(s) to function, got " + toString(num_args) + "." + errorContextString(state));
}


#if 0 // GUI_CLIENT
static void enqueueMessageToSend(ClientThread& client_thread, SocketBufferOutStream& packet)
{
	MessageUtils::updatePacketLengthField(packet);

	client_thread.enqueueDataToSend(packet.buf);
}
#endif


#if 0 // TEMP DISABLED
static int createObject(lua_State* state)
{
	// Expected args:
	// Arg 1: ob_params : Table

	checkNumArgs(state, /*num_args_required*/1);
	//const int initial_stack_size = lua_gettop(state);

	if(!lua_istable(state, 1))
		throw glare::Exception("createObject(): arg 1 (ob_params) was not a table" + errorContextString(state));

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

	//assert(lua_gettop(state) == initial_stack_size); // Check stack is same size as at the start of the function

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

	{
		ob->uid = sub_lua_vm->server->world_state->getNextObjectUID();
		ob->state = WorldObject::State_JustCreated;
		ob->from_remote_other_dirty = true;
	
		//cur_world_state->addWorldObjectAsDBDirty(new_ob); // TEMP: don't add to DB

		script_evaluator->world_state->getDirtyFromRemoteObjects(*script_evaluator->cur_world_state_lock).insert(ob);
		script_evaluator->world_state->getObjects(*script_evaluator->cur_world_state_lock).insert(std::make_pair(ob->uid, ob));
	}

#endif

	return 0; // Count of returned values
}
#endif


// Find a WorldObject for the given UID.  Throws exception if no such object with UID found.
static WorldObject* getWorldObjectForUID(LuaScriptEvaluator* script_evaluator, const UID uid)
#if GUI_CLIENT
	
#endif
{
	// See if the UID is that of the script_evaluator object, in which case we can use the script_evaluator pointer and avoid the map lookup.
	if(uid == script_evaluator->world_object->uid)
		return script_evaluator->world_object;
	else
	{
#if GUI_CLIENT
		{
			SubstrataLuaVM* sub_lua_vm = script_evaluator->substrata_lua_vm; //(SubstrataLuaVM*)lua_callbacks(state)->userdata;
			WorldState* world_state = sub_lua_vm->gui_client->world_state.ptr();

			checkHoldWorldStateMutex(script_evaluator, world_state);

			auto res = world_state->objects.find(uid);
			if(res == world_state->objects.end())
				throw glare::Exception("getObjectForUID(): No object with UID " + uid.toString());

			return res.getValue().ptr();
		}
#elif SERVER
		{
			if(script_evaluator->cur_world_state_lock == nullptr)
			{
				assert(0);
				throw glare::Exception("Internal error: cur_world_state_lock was null");
			}


			ServerWorldState::ObjectMapType& objects = script_evaluator->world_state->getObjects(*script_evaluator->cur_world_state_lock);

			auto res = objects.find(uid);
			if(res == objects.end())
				throw glare::Exception("getObjectForUID(): No object with UID " + uid.toString());

			return res->second.ptr();
		}
#endif
	}
}


static int luaGetWorldObjectForUID(lua_State* state)
{
	// Expected args:
	// Arg 1: uid : Number

	checkNumArgs(state, /*num_args_required*/1);

	const UID uid((uint64)LuaUtils::getDoubleArg(state, /*index=*/1));

	LuaScript* script = (LuaScript*)lua_getthreaddata(state); // NOTE: this double pointer-chasing sucks
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	/*WorldObject* ob =*/ getWorldObjectForUID(script_evaluator, uid); // Just call this to throw an excep if no such object exists

	script_evaluator->pushWorldObjectTableOntoStack(uid);
	return 1;
}


static int getCurrentTime(lua_State* state)
{
	// Expected args:
	// none

	checkNumArgs(state, /*num_args_required*/0);

	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
#if GUI_CLIENT
	lua_pushnumber(state, sub_lua_vm->gui_client->world_state->getCurrentGlobalTime()); // NOTE: do we want to use global time for this?
#elif SERVER
	lua_pushnumber(state, sub_lua_vm->server->getCurrentGlobalTime()); // NOTE: do we want to use global time for this?
#endif
	return 1;
}


static const int MAX_NUM_OB_EVENT_LISTENS = 20;


static int luaAddEventListener(lua_State* state)
{
	// Expected args:
	// Arg 1: event_name : String
	// Arg 2: ob_uid : UID
	// Arg 3: handler : Function

	checkNumArgs(state, /*num_args_required*/3);
	if(lua_type(state, /*index=*/3) != LUA_TFUNCTION)
		throw glare::Exception("createTimer(): arg 1 must be a function" + errorContextString(state));

	int event_name_atom = -1;
	const char* event_name = LuaUtils::getStringAndAtom(state, /*index=*/1, event_name_atom);

	// Get object UID
	const UID ob_uid = UID((uint64)LuaUtils::getDoubleArg(state, /*index=*/2));


	const void* handler_func_ptr = lua_topointer(state, /*index=*/3);
	const int handler_func_ref = lua_ref(state, /*index=*/3); // Get a reference to the handler function

	LuaScript* script = (LuaScript*)lua_getthreaddata(state); // NOTE: this double pointer-chasing sucks
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;
	
#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = script_evaluator->substrata_lua_vm;
	WorldState* world_state = sub_lua_vm->gui_client->world_state.ptr();
#endif

	script_evaluator->num_obs_event_listening++;
	if(script_evaluator->num_obs_event_listening > MAX_NUM_OB_EVENT_LISTENS)
		throw glare::Exception("Script added too many event listeners, max is " + toString(MAX_NUM_OB_EVENT_LISTENS) + errorContextString(state));

	
	// For the client, we may be trying to add an event listener for an object that has not been sent from the server and loaded yet.
	// In this case getWorldObjectForUID will throw an exception.
	// To handle it we will add the event handler to world_state->pending_event_handlers, so that the event handler can be assigned to the object later
	// when it is actually loaded.  See pending_event_handlers usage in GUIClient::timerEvent.
	WorldObject* ob = nullptr;
	Reference<ObjectEventHandlers> ob_event_handlers;

#if GUI_CLIENT
	try
	{
		ob = getWorldObjectForUID(script_evaluator, ob_uid); // Try and get object, but handle the exception if not found.
	}
	catch(glare::Exception&)
	{}

	if(ob)
	{
		// Object exists, so add event handler to it directly.
		ob_event_handlers = ob->getOrCreateEventHandlers();
	}
	else
	{
		// conPrint("================ luaAddEventListener(): Object " + ob_uid.toString() + " was null (not loaded yet), adding to pending_event_handlers instead.");
		if(world_state->pending_event_handlers[ob_uid].isNull())
			world_state->pending_event_handlers[ob_uid] = new ObjectEventHandlers();
		ob_event_handlers = world_state->pending_event_handlers[ob_uid];
	}
#else
	ob = getWorldObjectForUID(script_evaluator, ob_uid);
	ob_event_handlers = ob->getOrCreateEventHandlers();
#endif

	// ob may be null at this point
	runtimeCheck(ob_event_handlers.nonNull()); // ob_event_handlers should be non-null


	HandlerFunc handler_func;
	handler_func.script = WeakReference<LuaScriptEvaluator>(script_evaluator);
	handler_func.handler_func_ref = handler_func_ref;
	handler_func.function_ptr = handler_func_ptr;

	[[maybe_unused]] bool added_spatial_event = false;
	switch(event_name_atom)
	{
	case Atom_onUserUsedObject:
		assert(stringEqual(event_name, "onUserUsedObject"));
		ob_event_handlers->onUserUsedObject_handlers.addHandler(handler_func);
		break;
	case Atom_onUserTouchedObject:
		assert(stringEqual(event_name, "onUserTouchedObject"));
		ob_event_handlers->onUserTouchedObject_handlers.addHandler(handler_func);
		break;
	case Atom_onUserMovedNearToObject:
		assert(stringEqual(event_name, "onUserMovedNearToObject"));
		ob_event_handlers->onUserMovedNearToObject_handlers.addHandler(handler_func);
		added_spatial_event = true;
		break;
	case Atom_onUserMovedAwayFromObject:
		assert(stringEqual(event_name, "onUserMovedAwayFromObject"));
		ob_event_handlers->onUserMovedAwayFromObject_handlers.addHandler(handler_func);
		added_spatial_event = true;
		break;
	case Atom_onUserEnteredParcel:
		assert(stringEqual(event_name, "onUserEnteredParcel"));
		ob_event_handlers->onUserEnteredParcel_handlers.addHandler(handler_func);
		added_spatial_event = true;
		break;
	case Atom_onUserExitedParcel:
		assert(stringEqual(event_name, "onUserExitedParcel"));
		ob_event_handlers->onUserExitedParcel_handlers.addHandler(handler_func);
		added_spatial_event = true;
		break;
	case Atom_onUserEnteredVehicle:
		assert(stringEqual(event_name, "onUserEnteredVehicle"));
		ob_event_handlers->onUserEnteredVehicle_handlers.addHandler(handler_func);
		break;
	case Atom_onUserExitedVehicle:
		assert(stringEqual(event_name, "onUserExitedVehicle"));
		ob_event_handlers->onUserExitedVehicle_handlers.addHandler(handler_func);
		break;
	default:
		throw glare::Exception("Unknown event '" + std::string(event_name) + "'" + errorContextString(state));
	}

#if GUI_CLIENT
	if(added_spatial_event && ob)
		sub_lua_vm->gui_client->scripted_ob_proximity_checker.addObject(ob);
#endif

	return 0;
}


static int showMessageToUser(lua_State* state)
{
	// Expected args:
	// Arg 1: msg : String
	// Arg 2: av : Avatar

	checkNumArgs(state, /*num_args_required*/2);

	const std::string msg = LuaUtils::getString(state, /*index=*/1);

#if GUI_CLIENT
	const UID av_uid = UID((uint64)LuaUtils::getTableNumberField(state, /*table index=*/2, "uid"));
#else
	LuaUtils::getTableNumberField(state, /*table index=*/2, "uid"); // Check for UID anyway
#endif

#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	if(av_uid == sub_lua_vm->gui_client->client_avatar_uid)
	{
		// We want to send to ourselves
		sub_lua_vm->gui_client->showScriptMessage(msg);
	}
#endif

	return 0;
}


static int createTimer(lua_State* state)
{
	// arg 1: onTimerEvent : function
	// arg 2: interval_time_s : Number   The interval time, in seconds
	// arg 3: repeating : bool

	// Returns a timer handle : Number

	checkNumArgs(state, /*num_args_required*/3);

	if(lua_type(state, /*index=*/1) != LUA_TFUNCTION)
		throw glare::Exception("createTimer(): arg 1 must be a function" + errorContextString(state));

	// Get a reference to the onTimerEvent function
	const int onTimerEvent_ref = lua_ref(state, /*index=*/1);

	const double raw_interval_time = LuaUtils::getDoubleArg(state, /*index=*/2);
	const bool repeating = LuaUtils::getBool(state, /*index=*/3);

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
	throw glare::Exception("createTimer(): Could not create timer, 4 timers already created." + errorContextString(state));
//#else
//	throw glare::Exception("createTimer(): todo on server.");
//#endif
}


static int destroyTimer(lua_State* state)
{
	// arg 1: timer_id : Number

	checkNumArgs(state, /*num_args_required*/1);

	const int timer_id = (int)LuaUtils::getDoubleArg(state, /*index=*/1);

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

	checkNumArgs(state, /*num_args_required*/1);

	// Get object UID
	//const UID uid((uint64)LuaUtils::getTableLightUserDataField(state, /*table index=*/1, "uid"));
	const UID ob_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/2, "uid"));
	
	LuaScript* script = (LuaScript*)lua_getthreaddata(state); // NOTE: this double pointer-chasing sucks
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	WorldObject* ob = getWorldObjectForUID(script_evaluator, ob_uid);


	lua_pushnumber(state, (double)ob->materials.size());
	return 1; // Count of returned values
}


static int getMaterial(lua_State* state)
{
	// arg 1: ob : WorldObject
	// arg 2: index : Number

	checkNumArgs(state, /*num_args_required*/2);
	
	// Get object UID
	const UID ob_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	const size_t index = (size_t)LuaUtils::getDoubleArg(state, /*index=*/2);

	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	LuaScript* script = (LuaScript*)lua_getthreaddata(state); // NOTE: this double pointer-chasing sucks
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	WorldObject* ob = getWorldObjectForUID(script_evaluator, ob_uid);

	if(index > ob->materials.size())
		throw glare::Exception("Invalid material index" + errorContextString(state));

	// Make a material table with object UID and material index
	lua_createtable(state, /*num array elems=*/0, /*num non-array elems=*/2);

	//LuaUtils::setNumberAsTableField(state, "uid", (double)uid.value());
	LuaUtils::setLightUserDataAsTableField(state, "uid", (void*)ob_uid.value());
	//LuaUtils::setNumberAsTableField(state, "idx", (double)index);
	LuaUtils::setLightUserDataAsTableField(state, "idx", (void*)index);

	// Set metatable to worldMaterialClassMetaTable_ref
	lua_getref(state, sub_lua_vm->worldMaterialClassMetaTable_ref); // Pushes worldObjectClassMetaTable_ref onto the stack.
	lua_setmetatable(state, -2); // "Pops a table from the stack and sets it as the new metatable for the value at the given acceptable index."

	return 1; // Count of returned values
}


// C++ implementation of __index for WorldObject class. Used when a WorldObject table field is read from.
static int worldObjectClassIndexMetaMethod(lua_State* state)
{
	// arg 1 is table (WorldObject)
	// arg 2 is the key (method name)

	assert(lua_gettop(state) == 2); // Should be 2 args.
	checkNumArgs(state, 2);

	// Get object UID
	const UID uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));

	LuaScript* script = (LuaScript*)lua_getthreaddata(state); // NOTE: this double pointer-chasing sucks
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	WorldObject* ob = getWorldObjectForUID(script_evaluator, uid);

	int atom = -1;
	const char* key_str = LuaUtils::getStringAndAtom(state, /*index=*/2, atom);
	switch(atom) // NOTE: The switch cases should be in the same order as the Atom enum values to ensure nice code-gen.
	{
	case Atom_model_url:
		assert(stringEqual(key_str, "model_url"));
		LuaUtils::pushString(state, ob->model_url);
		break;
	case Atom_pos:
		assert(stringEqual(key_str, "pos"));
		LuaUtils::pushVec3d(state, ob->pos);
		break;
	case Atom_axis:
		assert(stringEqual(key_str, "axis"));
		LuaUtils::pushVec3f(state, ob->axis);
		break;
	case Atom_angle:
		assert(stringEqual(key_str, "angle"));
		lua_pushnumber(state, ob->angle);
		break;
	case Atom_scale:
		assert(stringEqual(key_str, "scale"));
		LuaUtils::pushVec3f(state, ob->scale);
		break;
	case Atom_collidable:
		assert(stringEqual(key_str, "collidable"));
		lua_pushboolean(state, ob->isCollidable());
		break;
	case Atom_dynamic:
		assert(stringEqual(key_str, "dynamic"));
		lua_pushboolean(state, ob->isDynamic());
		break;
	case Atom_sensor:
		assert(stringEqual(key_str, "sensor"));
		lua_pushboolean(state, ob->isSensor());
		break;
	case Atom_content:
		assert(stringEqual(key_str, "content"));
		LuaUtils::pushString(state, ob->content);
		break;
	case Atom_video_autoplay:
		assert(stringEqual(key_str, "video_autoplay"));
		lua_pushboolean(state, BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_AUTOPLAY));
		break;
	case Atom_video_loop:
		assert(stringEqual(key_str, "video_loop"));
		lua_pushboolean(state, BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_LOOP));
		break;
	case Atom_video_muted:
		assert(stringEqual(key_str, "video_muted"));
		lua_pushboolean(state, BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_MUTED));
		break;
	case Atom_mass:
		assert(stringEqual(key_str, "mass"));
		lua_pushnumber(state, ob->mass);
		break;
	case Atom_friction:
		assert(stringEqual(key_str, "friction"));
		lua_pushnumber(state, ob->friction);
		break;	
	case Atom_restitution:
		assert(stringEqual(key_str, "restitution"));
		lua_pushnumber(state, ob->restitution);
		break;
	case Atom_centre_of_mass_offset_os:
		assert(stringEqual(key_str, "centre_of_mass_offset_os"));
		LuaUtils::pushVec3f(state, ob->centre_of_mass_offset_os);
		break;
	case Atom_audio_source_url:
		assert(stringEqual(key_str, "audio_source_url"));
		LuaUtils::pushString(state, ob->audio_source_url);
		break;
	case Atom_audio_volume:
		assert(stringEqual(key_str, "audio_volume"));
		lua_pushnumber(state, ob->audio_volume);
		break;
	case Atom_getNumMaterials:
		assert(stringEqual(key_str, "getNumMaterials"));
		lua_pushcfunction(state, getNumMaterials, "getNumMaterials");
		break;
	case Atom_getMaterial:
		assert(stringEqual(key_str, "getMaterial"));
		lua_pushcfunction(state, getMaterial, "getMaterial");
		break;
	default:
		throw glare::Exception("Unknown field '" + std::string(key_str) + "'" + errorContextString(state));
	}

	return 1; // Count of returned values
}


// C++ implementation of __newindex for WorldObject class.  Used when a value is assigned to a WorldObject field
static int worldObjectClassNewIndexMetaMethod(lua_State* state)
{
#if GUI_CLIENT
	return 0; // Count of returned values
#else

	// Arg 1: table 
	// Arg 2: key
	// Arg 3: value

	assert(lua_gettop(state) == 3); // Should be 3 args.
	checkNumArgs(state, 3);
	
	// Get object UID
	const UID uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));

	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state); // NOTE: this double pointer-chasing sucks
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	if(script_evaluator->cur_world_state_lock == nullptr)
	{
		assert(0);
		throw glare::Exception("Internal error: cur_world_state_lock was null");
	}

	WorldObject* ob = getWorldObjectForUID(script_evaluator, uid);

	// Check permissions before we update object.
	// A script has permissions to modify an object if and only if the creator of the script is also the creator of the object.
	if(ob->creator_id != script_evaluator->world_object->creator_id)
		throw glare::Exception("Script does not have permissions to modifiy object (ob UID: " + uid.toString() + ")");


	bool transform_changed = false;
	bool other_changed = false;

	int atom = -1;
	const char* key_str = LuaUtils::getStringAndAtom(state, /*index=*/2, atom);
	switch(atom) // NOTE: The switch cases should be in the same order as the Atom enum values to ensure nice code-gen.
	{
	case Atom_model_url:
		assert(stringEqual(key_str, "model_url"));

		ob->model_url = LuaUtils::getString(state, /*index=*/3);

		ob->content = LuaUtils::getString(state, /*index=*/3);
		ob->from_remote_model_url_dirty = true; // TODO: rename

#if SERVER
		script_evaluator->world_state->getDirtyFromRemoteObjects(*script_evaluator->cur_world_state_lock).insert(ob);
#endif
		break;
	case Atom_pos:
		assert(stringEqual(key_str, "pos"));
		ob->pos = LuaUtils::getVec3d(state, /*index=*/3);
		transform_changed = true;
		break;
	case Atom_axis:
		assert(stringEqual(key_str, "axis"));
		ob->axis = LuaUtils::getVec3f(state, /*index=*/3);
		transform_changed = true;
		break;
	case Atom_angle:
		assert(stringEqual(key_str, "angle"));
		ob->angle = LuaUtils::getFloat(state, /*index=*/3);
		transform_changed = true;
		break;
	case Atom_scale:
		assert(stringEqual(key_str, "scale"));
		ob->scale = LuaUtils::getVec3f(state, /*index=*/3);
		transform_changed = true;
		break;
	case Atom_collidable:
		assert(stringEqual(key_str, "collidable"));
		ob->setCollidable(LuaUtils::getBool(state, /*index=*/3));
		other_changed = true;
		break;
	case Atom_dynamic:
		ob->setDynamic(LuaUtils::getBool(state, /*index=*/3));
		other_changed = true;
		break;
	case Atom_sensor:
		ob->setIsSensor(LuaUtils::getBool(state, /*index=*/3));
		other_changed = true;
		break;
	case Atom_content:
		assert(stringEqual(key_str, "content"));
		ob->content = LuaUtils::getString(state, /*index=*/3);
		ob->from_remote_content_dirty = true; // TODO: rename
		script_evaluator->world_state->getDirtyFromRemoteObjects(*script_evaluator->cur_world_state_lock).insert(ob);
		break;
	case Atom_video_autoplay:
		assert(stringEqual(key_str, "video_autoplay"));
		BitUtils::setOrZeroBit(ob->flags, WorldObject::VIDEO_AUTOPLAY, LuaUtils::getBool(state, /*index=*/3));
		other_changed = true;
		break;
	case Atom_video_loop:
		assert(stringEqual(key_str, "video_loop"));
		BitUtils::setOrZeroBit(ob->flags, WorldObject::VIDEO_LOOP, LuaUtils::getBool(state, /*index=*/3));
		other_changed = true;
		break;
	case Atom_video_muted:
		assert(stringEqual(key_str, "video_muted"));
		BitUtils::setOrZeroBit(ob->flags, WorldObject::VIDEO_MUTED, LuaUtils::getBool(state, /*index=*/3));
		other_changed = true;
		break;
	case Atom_mass:
		assert(stringEqual(key_str, "mass"));
		ob->mass = LuaUtils::getFloat(state, /*index=*/3);
		other_changed = true;
		break;
	case Atom_friction:
		assert(stringEqual(key_str, "friction"));
		ob->friction = LuaUtils::getFloat(state, /*index=*/3);
		other_changed = true;
		break;	
	case Atom_restitution:
		assert(stringEqual(key_str, "restitution"));
		ob->restitution = LuaUtils::getFloat(state, /*index=*/3);
		other_changed = true;
		break;
	case Atom_centre_of_mass_offset_os:
		assert(stringEqual(key_str, "centre_of_mass_offset_os"));
		ob->centre_of_mass_offset_os = LuaUtils::getVec3f(state, /*index=*/3);
		other_changed = true;
		break;
	case Atom_audio_source_url:
		assert(stringEqual(key_str, "audio_source_url"));
		ob->audio_source_url = LuaUtils::getString(state, /*index=*/3);
		other_changed = true;
		break;
	case Atom_audio_volume:
		assert(stringEqual(key_str, "audio_volume"));
		ob->audio_volume = LuaUtils::getFloat(state, /*index=*/3);
		other_changed = true;
		break;
	default:
		throw glare::Exception("Unknown field '" + std::string(key_str) + "'");
	}

	if(transform_changed)
	{
		ob->last_transform_update_avatar_uid = std::numeric_limits<uint32>::max();
		ob->from_remote_transform_dirty = true; // TODO: rename
		script_evaluator->world_state->getDirtyFromRemoteObjects(*script_evaluator->cur_world_state_lock).insert(ob);
	}
	else if(other_changed)
	{
		ob->from_remote_other_dirty = true; // TODO: rename
		script_evaluator->world_state->getDirtyFromRemoteObjects(*script_evaluator->cur_world_state_lock).insert(ob);
	}

	script_evaluator->world_state->addWorldObjectAsDBDirty(ob, *script_evaluator->cur_world_state_lock);
	sub_lua_vm->server->world_state->markAsChanged();

	return 0; // Count of returned values
#endif
}



// C++ implementation of __index for WorldMaterial class. Used when a WorldMaterial table field is read from.
static int worldMaterialClassIndexMetaMethod(lua_State* state)
{
	// arg 1 is table (WorldMaterial)
	// arg 2 is the key (method name)

	assert(lua_gettop(state) == 2); // Should be 2 args.
	checkNumArgs(state, 2);

	// Get object UID from the world material table
	//const UID uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	const UID uid((uint64)LuaUtils::getTableLightUserDataField(state, /*table index=*/1, "uid"));

	// Get material index from the world material table
	//const size_t mat_index = (size_t)LuaUtils::getTableNumberField(state, /*table index=*/1, "idx");
	const size_t mat_index = (size_t)LuaUtils::getTableLightUserDataField(state, /*table index=*/1, "idx");

	LuaScript* script = (LuaScript*)lua_getthreaddata(state); // NOTE: this double pointer-chasing sucks
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	WorldObject* ob = getWorldObjectForUID(script_evaluator, uid);

	if(mat_index > ob->materials.size())
		throw glare::Exception("Invalid material index" + errorContextString(state));

	WorldMaterial* mat = ob->materials[mat_index].ptr();
	
	int atom = -1;
	const char* key_str = LuaUtils::getStringAndAtom(state, /*index=*/2, atom);
	switch(atom) // NOTE: The switch cases should be in the same order as the Atom enum values to ensure nice code-gen.
	{
	case Atom_colour:
		assert(stringEqual(key_str, "colour"));
		LuaUtils::pushVec3f(state, mat->colour_rgb.toVec3());
		break;
	case Atom_colour_texture_url:
		assert(stringEqual(key_str, "colour_texture_url"));
		LuaUtils::pushString(state, mat->colour_texture_url);
		break;
	case Atom_emission_rgb:
		assert(stringEqual(key_str, "emission_rgb"));
		LuaUtils::pushVec3f(state, mat->emission_rgb.toVec3());
		break;
	case Atom_emission_texture_url:
		assert(stringEqual(key_str, "emission_texture_url"));
		LuaUtils::pushString(state, mat->emission_texture_url);
		break;
	case Atom_normal_map_url:
		assert(stringEqual(key_str, "normal_map_url"));
		LuaUtils::pushString(state, mat->normal_map_url);
		break;
	case Atom_roughness_val:
		assert(stringEqual(key_str, "roughness_val"));
		lua_pushnumber(state, mat->roughness.val);
		break;
	case Atom_roughness_texture_url:
		assert(stringEqual(key_str, "roughness_texture_url"));
		LuaUtils::pushString(state, mat->roughness.texture_url);
		break;
	case Atom_metallic_fraction_val:
		assert(stringEqual(key_str, "metallic_fraction_val"));
		lua_pushnumber(state, mat->metallic_fraction.val);
		break;
	case Atom_opacity_val:
		assert(stringEqual(key_str, "opacity_val"));
		lua_pushnumber(state, mat->opacity.val);
		break;
	case Atom_tex_matrix:
		assert(stringEqual(key_str, "tex_matrix"));
		LuaUtils::pushMatrix2f(state, mat->tex_matrix);
		break;
	case Atom_emission_lum_flux_or_lum:
		assert(stringEqual(key_str, "emission_lum_flux_or_lum"));
		lua_pushnumber(state, mat->emission_lum_flux_or_lum);
		break;
	case Atom_hologram:
		assert(stringEqual(key_str, "hologram"));
		lua_pushboolean(state, BitUtils::isBitSet(mat->flags, WorldMaterial::HOLOGRAM_FLAG));
		break;
	case Atom_double_sided:
		assert(stringEqual(key_str, "double_sided"));
		lua_pushboolean(state, BitUtils::isBitSet(mat->flags, WorldMaterial::DOUBLE_SIDED_FLAG));
		break;
	default:
		throw glare::Exception("Unknown field '" + std::string(key_str) + "'" + errorContextString(state));
	}

	return 1; // Count of returned values
}


// C++ implementation of __newindex for WorldMaterial class.  Used when a value is assigned to a WorldMaterial field
static int worldMaterialClassNewIndexMetaMethod(lua_State* state)
{
#if GUI_CLIENT
	return 0; // Count of returned values
#else

	// Arg 1: WorldMaterial
	// Arg 2: key : string
	// Arg 3: value

	assert(lua_gettop(state) == 3); // Should be 3 args.
	checkNumArgs(state, 3);
	
	// Get object UID from the world material table
	//const UID uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	const UID uid((uint64)LuaUtils::getTableLightUserDataField(state, /*table index=*/1, "uid"));

	// Get material index from the world material table
	const size_t mat_index = (size_t)LuaUtils::getTableLightUserDataField(state, /*table index=*/1, "idx");

	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state); // NOTE: this double pointer-chasing sucks
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	WorldObject* ob = getWorldObjectForUID(script_evaluator, uid);

	// Check permissions before we update object
	if(ob->creator_id != script_evaluator->world_object->creator_id)
		throw glare::Exception("Script does not have permissions to modifiy object (ob UID: " + uid.toString() + ")" + errorContextString(state));


	if(mat_index > ob->materials.size())
		throw glare::Exception("Invalid material index" + errorContextString(state));

	WorldMaterial* mat = ob->materials[mat_index].ptr();

	// Read key
	int atom = -1;
	const char* key_str = LuaUtils::getStringAndAtom(state, /*index=*/2, atom);
	switch(atom) // NOTE: The switch cases should be in the same order as the Atom enum values to ensure nice code-gen.
	{
	case Atom_colour:
		assert(stringEqual(key_str, "colour"));
		mat->colour_rgb = Colour3f(LuaUtils::getVec3f(state, /*index=*/3));
		break;
	case Atom_colour_texture_url:
		assert(stringEqual(key_str, "colour_texture_url"));
		mat->colour_texture_url = LuaUtils::getString(state, /*index=*/3);
		break;
	case Atom_emission_rgb:
		assert(stringEqual(key_str, "emission_rgb"));
		mat->emission_rgb = Colour3f(LuaUtils::getVec3f(state, /*index=*/3));
		break;
	case Atom_emission_texture_url:
		assert(stringEqual(key_str, "emission_texture_url"));
		mat->emission_texture_url = LuaUtils::getString(state, /*index=*/3);
		break;
	case Atom_normal_map_url:
		assert(stringEqual(key_str, "normal_map_url"));
		mat->normal_map_url = LuaUtils::getString(state, /*index=*/3);
		break;
	case Atom_roughness_val:
		assert(stringEqual(key_str, "roughness_val"));
		mat->roughness.val = LuaUtils::getFloat(state, /*index=*/3);
		break;
	case Atom_roughness_texture_url:
		assert(stringEqual(key_str, "roughness_texture_url"));
		mat->roughness.texture_url = LuaUtils::getString(state, /*index=*/3);
		break;
	case Atom_metallic_fraction_val:
		assert(stringEqual(key_str, "metallic_fraction_val"));
		mat->metallic_fraction.val = LuaUtils::getFloat(state, /*index=*/3);
		break;
	case Atom_opacity_val:
		assert(stringEqual(key_str, "opacity_val"));
		mat->opacity.val = LuaUtils::getFloat(state, /*index=*/3);
		break;
	case Atom_tex_matrix:
		assert(stringEqual(key_str, "tex_matrix"));
		mat->tex_matrix = LuaUtils::getMatrix2f(state, /*index=*/3);
		break;
	case Atom_emission_lum_flux_or_lum:
		assert(stringEqual(key_str, "emission_lum_flux_or_lum"));
		mat->emission_lum_flux_or_lum = LuaUtils::getFloat(state, /*index=*/3);
		break;
	case Atom_hologram:
		assert(stringEqual(key_str, "hologram"));
		BitUtils::setOrZeroBit(mat->flags, WorldMaterial::HOLOGRAM_FLAG, LuaUtils::getBool(state, /*index=*/3));
		break;
	case Atom_double_sided:
		assert(stringEqual(key_str, "double_sided"));
		BitUtils::setOrZeroBit(mat->flags, WorldMaterial::DOUBLE_SIDED_FLAG, LuaUtils::getBool(state, /*index=*/3));
		break;
	default:
		throw glare::Exception("Unknown field '" + std::string(key_str) + "'" + errorContextString(state));
	}

	// Mark the object as dirty, sending the updated object will send the updated material as well.
	ob->from_remote_other_dirty = true; // TODO: rename
	script_evaluator->world_state->getDirtyFromRemoteObjects(*script_evaluator->cur_world_state_lock).insert(ob);

	script_evaluator->world_state->addWorldObjectAsDBDirty(ob, *script_evaluator->cur_world_state_lock);
	sub_lua_vm->server->world_state->markAsChanged();

	return 0; // Count of returned values
#endif
}


// C++ implementation of __index for User class. Used when a User table field is read from.
static int userClassIndexMetaMethod(lua_State* state)
{
	throw glare::Exception("Disabled");

	// arg 1 is table
	// arg 2 is the key (field name)

	assert(lua_gettop(state) == 2); // Should be 2 args.
	checkNumArgs(state, 2);

	const UID uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));

#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	//LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	//LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	auto res = sub_lua_vm->gui_client->world_state->avatars.find(uid);
	if(res == sub_lua_vm->gui_client->world_state->avatars.end())
		throw glare::Exception("No such avatar with given UID" + errorContextString(state));

	Avatar* avatar = res->second.ptr();

#elif SERVER
	// Get user UID from the user table
	//const UserID uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "id"));

	//SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	//auto res = sub_lua_vm->server->world_state->user_id_to_users.find(uid);
	//if(res == sub_lua_vm->server->world_state->user_id_to_users.end())
	//	throw glare::Exception("No such user with given UID");

	//User* user = res->second.ptr();



//	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	if(script_evaluator->cur_world_state_lock == nullptr)
		throw glare::Exception("Internal error: cur_world_state_lock was null");
	const ServerWorldState::AvatarMapType& avatars = script_evaluator->world_state->getAvatars(*script_evaluator->cur_world_state_lock);
	auto res = avatars.find(uid);
	if(res == avatars.end())
		throw glare::Exception("No such avatar with given UID" + errorContextString(state));

	const Avatar* avatar = res->second.ptr();
	//if(res == sub_lua_vm->server->world_state->user_id_to_users.end())
	//	throw glare::Exception("No such user with given UID");

	//User* user = res->second.ptr();
#endif


	const char* key_str = lua_tolstring(state, /*index=*/2, /*stringlen=*/NULL); // May return NULL if not a string
	if(key_str)
	{
		if(stringEqual(key_str, "pos"))
		{
			LuaUtils::pushVec3d(state, avatar->pos);
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
			throw glare::Exception("User class: Unknown field '" + std::string(key_str) + "'" + errorContextString(state));
	}

	return 1; // Count of returned values
}


// C++ implementation of __newindex.  Used when a value is assigned to a table field.
static int userClassNewIndexMetaMethod(lua_State* state)
{
	// Arg 1: table 
	// Arg 2: key
	// Arg 3: value

	assert(lua_gettop(state) == 3); // Should be 3 args.
	checkNumArgs(state, 3);
	
	// Read key
//	size_t stringlen = 0;
//	const char* key_str = lua_tolstring(state, /*index=*/2, &stringlen); // May return NULL if not a string
//	if(key_str)
//	{
//		
//	}
//
//	// By default, just do the assignment to the original table here.
//	//lua_rawsetfield(state, /*table index=*/1, key_str);
//	lua_rawset(state, /*table index=*/1); // Sets table[key] = value, pops key and value from stack.

	return 0; // Count of returned values
}


// C++ implementation of __index for Avatar class. Used when a Avatar table field is read from.
static int avatarClassIndexMetaMethod(lua_State* state)
{
	// arg 1 is Avatar table
	// arg 2 is the key (field name)

	assert(lua_gettop(state) == 2); // Should be 2 args.
	checkNumArgs(state, 2);

	const UID avatar_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));

	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	WorldState* world_state = sub_lua_vm->gui_client->world_state.ptr();
	checkHoldWorldStateMutex(script_evaluator, world_state);

	auto res = world_state->avatars.find(avatar_uid);
	if(res == world_state->avatars.end())
		throw glare::Exception("No such avatar with given UID" + errorContextString(state));

	Avatar* avatar = res->second.ptr();
#elif SERVER

	if(script_evaluator->cur_world_state_lock == nullptr)
		throw glare::Exception("Internal error: cur_world_state_lock was null");
	const ServerWorldState::AvatarMapType& avatars = script_evaluator->world_state->getAvatars(*script_evaluator->cur_world_state_lock);

	auto res = avatars.find(avatar_uid);
	if(res == avatars.end())
		throw glare::Exception("No such avatar with given UID" + errorContextString(state));

	Avatar* avatar = res->second.ptr();
#endif

	// Read key
	int atom = -1;
	const char* key_str = LuaUtils::getStringAndAtom(state, /*index=*/2, atom);
	switch(atom)
	{
	case Atom_pos:
		assert(stringEqual(key_str, "pos"));
		LuaUtils::pushVec3d(state, avatar->pos);
		return 1;
	case Atom_name:
		assert(stringEqual(key_str, "name"));
		LuaUtils::pushString(state, avatar->name);
		return 1;
	case Atom_linear_velocity:
		assert(stringEqual(key_str, "linear_velocity"));
#if GUI_CLIENT
		LuaUtils::pushVec3f(state, Vec3f(sub_lua_vm->gui_client->player_physics.getLinearVel()));
		return 1;
#else // else server:
		LuaUtils::pushVec3f(state, Vec3f(0.f));
		return 1;
#endif
	case Atom_vehicle_inside: // Returns an Object table of the vehicle object the avatar is inside-of/riding, or nil if none.
	{
		assert(stringEqual(key_str, "vehicle_inside"));
#if GUI_CLIENT
		const WorldObject* vehicle_inside;
		if(avatar_uid == sub_lua_vm->gui_client->client_avatar_uid) // If this is our avatar:
			vehicle_inside = sub_lua_vm->gui_client->vehicle_controller_inside ? sub_lua_vm->gui_client->vehicle_controller_inside->getControlledObject() : nullptr;
		else
			vehicle_inside = avatar->entered_vehicle.ptr();

		if(vehicle_inside)
			script_evaluator->pushWorldObjectTableOntoStack(vehicle_inside->uid);
		else
			lua_pushnil(state);
#else // else server:
		if(avatar->vehicle_inside_uid.valid())
			script_evaluator->pushWorldObjectTableOntoStack(avatar->vehicle_inside_uid);
		else
			lua_pushnil(state);
#endif
		return 1;
	}
	default:
		throw glare::Exception("Avatar class: Unknown field '" + std::string(key_str) + "'" + errorContextString(state));
	}
}


// C++ implementation of __newindex.  Used when a value is assigned to a table field.
static int avatarClassNewIndexMetaMethod(lua_State* state)
{
	// Arg 1: table 
	// Arg 2: key
	// Arg 3: value

#if GUI_CLIENT
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	WorldState* world_state = sub_lua_vm->gui_client->world_state.ptr();
	checkHoldWorldStateMutex(script_evaluator, world_state);
#endif

	[[maybe_unused]] const UID avatar_uid = UID((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));

	// Read key
	int atom = -1;
	const char* key_str = LuaUtils::getStringAndAtom(state, /*index=*/2, atom);
	switch(atom) // NOTE: The switch cases should be in the same order as the Atom enum values to ensure nice code-gen.
	{
		case Atom_pos:
		{
			assert(stringEqual(key_str, "pos"));
#if GUI_CLIENT
			if(avatar_uid == sub_lua_vm->gui_client->client_avatar_uid)
			{
				const Vec3d pos = LuaUtils::getVec3d(state, /*index=*/3);
				sub_lua_vm->gui_client->player_physics.setEyePosition(pos);
			}
#endif
			break;
		}
		case Atom_name:
		{
			assert(stringEqual(key_str, "name"));
			throw glare::Exception("Can't set avatar name");
			break;
		}
		case Atom_linear_velocity:
		{
			assert(stringEqual(key_str, "linear_velocity"));
#if GUI_CLIENT
			if(avatar_uid == sub_lua_vm->gui_client->client_avatar_uid)
			{
				const Vec3f new_linear_vel = LuaUtils::getVec3f(state, /*index=*/3);
				sub_lua_vm->gui_client->player_physics.setLinearVel(new_linear_vel.toVec4fVector());
			}
#endif
			break;
		}
	default:
		throw glare::Exception("Avatar class: Unknown field '" + std::string(key_str) + "'" + errorContextString(state));
	}

	return 0; // Count of returned values
}


// "gets called when a string is created; returned atom can be retrieved via tostringatom"
static int16_t glareLuaUserAtom(const char* str, size_t stringlen)
{
	// conPrint("glareLuaUserAtom(): str: " + std::string(str, stringlen));

	for(size_t i=0; i<staticArrayNumElems(string_atoms); ++i)
		if(stringEqual(str, string_atoms[i].str))
			return (int16)string_atoms[i].atom;

	return -1; // or use ATOM_UNDEF here?
}


SubstrataLuaVM::SubstrataLuaVM()
{
	lua_vm.set(new LuaVM());
	lua_vm->max_total_mem_allowed = 16 * 1024 * 1024;

	lua_callbacks(lua_vm->state)->userdata = this;
	lua_callbacks(lua_vm->state)->useratom = glareLuaUserAtom;


	// Set some global functions
//TEMP DISABLED	lua_pushcfunction(lua_vm->state, createObject, /*debugname=*/"createObject");
//	lua_setglobal(lua_vm->state, "createObject"); // Pops a value from the stack and sets it as the new value of global name.
	
	lua_pushcfunction(lua_vm->state, luaGetWorldObjectForUID, /*debugname=*/"getObjectForUID");
	lua_setglobal(lua_vm->state, "getObjectForUID");
	
	lua_pushcfunction(lua_vm->state, getCurrentTime, /*debugname=*/"getCurrentTime");
	lua_setglobal(lua_vm->state, "getCurrentTime");
	
	lua_pushcfunction(lua_vm->state, showMessageToUser, /*debugname=*/"showMessageToUser");
	lua_setglobal(lua_vm->state, "showMessageToUser");

	lua_pushcfunction(lua_vm->state, createTimer, /*debugname=*/"createTimer");
	lua_setglobal(lua_vm->state, "createTimer");
	
	lua_pushcfunction(lua_vm->state, destroyTimer, /*debugname=*/"destroyTimer");
	lua_setglobal(lua_vm->state, "destroyTimer");
	
	lua_pushcfunction(lua_vm->state, luaAddEventListener, /*debugname=*/"addEventListener");
	lua_setglobal(lua_vm->state, "addEventListener");

	//--------------------------- Create metatables for our classes ---------------------------

	//--------------------------- Create WorldObject Metatable ---------------------------
	lua_createtable(lua_vm->state, /*num array elems=*/0, /*num non-array elems=*/2); // Create WorldObject metatable
			
	lua_vm->setCFunctionAsTableField(worldObjectClassIndexMetaMethod,    /*debugname=*/"worldObjectClassIndexMetaMethod",    /*table index=*/-2, /*key=*/"__index");
	lua_vm->setCFunctionAsTableField(worldObjectClassNewIndexMetaMethod, /*debugname=*/"worldObjectClassNewIndexMetaMethod", /*table index=*/-2, /*key=*/"__newindex");

	lua_setreadonly(lua_vm->state, /*index=*/-1, /*enabled=*/1); // Set metatable as read-only.

	worldObjectClassMetaTable_ref = lua_ref(lua_vm->state, /*index=*/-1); // Get reference to WorldObjectMetaTable.  Does not pop.
	lua_pop(lua_vm->state, 1); // Pop WorldObjectMetaTable from stack
	//--------------------------- End create User Metatable ---------------------------


	//--------------------------- Create WorldMaterial Metatable ---------------------------
	lua_createtable(lua_vm->state, /*num array elems=*/0, /*num non-array elems=*/2); // Create WorldMaterial metatable
			
	lua_vm->setCFunctionAsTableField(worldMaterialClassIndexMetaMethod,    /*debugname=*/"worldMaterialClassIndexMetaMethod",    /*table index=*/-2, /*key=*/"__index");
	lua_vm->setCFunctionAsTableField(worldMaterialClassNewIndexMetaMethod, /*debugname=*/"worldMaterialClassNewIndexMetaMethod", /*table index=*/-2, /*key=*/"__newindex");

	lua_setreadonly(lua_vm->state, /*index=*/-1, /*enabled=*/1); // Set metatable as read-only.

	worldMaterialClassMetaTable_ref = lua_ref(lua_vm->state, /*index=*/-1); // Get reference to WorldMaterialMetaTable.  Does not pop.
	lua_pop(lua_vm->state, 1); // Pop WorldMaterialMetaTable from stack
	//--------------------------- End create User Metatable ---------------------------
	

	//--------------------------- Create User Metatable ---------------------------
	lua_createtable(lua_vm->state, /*num array elems=*/0, /*num non-array elems=*/2); // Create User metatable
			
	lua_vm->setCFunctionAsTableField(userClassIndexMetaMethod,    /*debugname=*/"userIndexMetaMethod",    /*table index=*/-2, /*key=*/"__index");
	lua_vm->setCFunctionAsTableField(userClassNewIndexMetaMethod, /*debugname=*/"userNewIndexMetaMethod", /*table index=*/-2, /*key=*/"__newindex");

	lua_setreadonly(lua_vm->state, /*index=*/-1, /*enabled=*/1); // Set metatable as read-only.

	userClassMetaTable_ref = lua_ref(lua_vm->state, /*index=*/-1); // Get reference to UserClassMetaTable.  Does not pop.
	lua_pop(lua_vm->state, 1); // Pop UserClassMetaTable from stack
	//--------------------------- End create User Metatable ---------------------------


	//--------------------------- Create Avatar Metatable ---------------------------
	lua_createtable(lua_vm->state, /*num array elems=*/0, /*num non-array elems=*/2); // Create Avatar metatable
			
	lua_vm->setCFunctionAsTableField(avatarClassIndexMetaMethod,    /*debugname=*/"avatarIndexMetaMethod",    /*table index=*/-2, /*key=*/"__index");
	lua_vm->setCFunctionAsTableField(avatarClassNewIndexMetaMethod, /*debugname=*/"avatarNewIndexMetaMethod", /*table index=*/-2, /*key=*/"__newindex");

	lua_setreadonly(lua_vm->state, /*index=*/-1, /*enabled=*/1); // Set metatable as read-only.

	avatarClassMetaTable_ref = lua_ref(lua_vm->state, /*index=*/-1); // Get reference to AvatarClassMetaTable.  Does not pop.
	lua_pop(lua_vm->state, 1); // Pop AvatarClassMetaTable from stack
	//--------------------------- End create Avatar Metatable ---------------------------

	lua_vm->finishInitAndSandbox();
}


SubstrataLuaVM::~SubstrataLuaVM()
{
}
