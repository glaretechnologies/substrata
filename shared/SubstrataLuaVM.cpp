/*=====================================================================
SubstrataLuaVM.cpp
------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "SubstrataLuaVM.h"


#include "../shared/SubstrataLuaScript.h"
#include "../shared/ObjectEventHandlers.h"
#include "../shared/ResourceManager.h"
#include "../shared/RateLimiter.h"
#include "LuaScriptUIFunctions.h"
#include "WorldObject.h"
#include "MessageUtils.h"
#include "Protocol.h"
#if GUI_CLIENT
#include "../gui_client/PlayerPhysics.h"
#include "../gui_client/GUIClient.h"
#include "../gui_client/VehiclePhysics.h"
#include "../gui_client/ScriptMessageUI.h"
#elif SERVER
#include "../server/Server.h"
#include "../server/LuaHTTPRequestManager.h"
#include "../server/MeshLODGenThread.h"
#endif
#include <lua/LuaVM.h>
#include <lua/LuaScript.h>
#include <lua/LuaUtils.h>
#include <lua/LuaSerialisation.h>
#include <utils/StringUtils.h>
#include <utils/RuntimeCheck.h>
#include <utils/JSONParser.h>
#include <maths/mathstypes.h>
#include <limits>
#include <lualib.h>
#include <Luau/Common.h>
#include <BufferViewInStream.h>


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
	Atom_target_url,
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
	Atom_audio_loop,
	Atom_materials,
	Atom_playAudio, // resets cur read index to start of audio, starts playing the audio (unpauses if paused)
	Atom_isPlayingAudio,
	Atom_playSound, // Play a sound identified by a resource URL
	Atom_startPlayingAnimation,
	Atom_getAnimationIndex,
	Atom_moveTo,
	Atom_rotateTo,

	// Object, Avatar
	Atom_linear_velocity,

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
	Atom_onChatMessage,
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
	StringAtom({"target_url",				Atom_target_url,				}),
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
	StringAtom({"audio_loop",				Atom_audio_loop,				}),
	StringAtom({"playAudio",				Atom_playAudio,					}),
	StringAtom({"isPlayingAudio",			Atom_isPlayingAudio,			}),
	StringAtom({"playSound",				Atom_playSound,					}),
	StringAtom({"startPlayingAnimation",	Atom_startPlayingAnimation,		}),
	StringAtom({"getAnimationIndex",		Atom_getAnimationIndex,			}),
	StringAtom({"moveTo",					Atom_moveTo,					}),
	StringAtom({"rotateTo",					Atom_rotateTo,					}),

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
	StringAtom({"onChatMessage",			Atom_onChatMessage,				}),
};


#if GUI_CLIENT
// Define this function to satisfy the Thread Safety Analysis.  Checks that we hold the world state lock.
// ASSERT_CAPABILITY means that after this function has finished executing, the calling thread is guaranteed to hold world_state->mutex.
static inline void checkHoldWorldStateMutex(SubstrataLuaScript* sub_lua_script, WorldState* world_state) ASSERT_CAPABILITY(world_state->mutex)
{
	// Don't bother checking the actual mutex addresses, just rely on the type system (WorldStateLock) and assume we don't mix up multiple WorldStateLocks, which should be unlikely.
	if(!(sub_lua_script->cur_world_state_lock)) // && (&sub_lua_script->cur_world_state_lock->getMutex() == &world_state->mutex)))
	{
		assert(0);
		throw glare::Exception("Internal error: didn't hold correct world state lock");
	}
}
#else // else if SERVER:
static inline void checkHoldWorldStateMutex(SubstrataLuaScript* sub_lua_script, ServerAllWorldsState* world_state) ASSERT_CAPABILITY(world_state->mutex)
{
	if(!(sub_lua_script->cur_world_state_lock))
	{
		assert(0);
		throw glare::Exception("Internal error: didn't hold world state lock");
	}
}
#endif


static std::string getTableStringFieldWithSizeCheck(lua_State* state, int index, const char* field_name, size_t max_size); // Defined below.


// Construct a WorldMaterial from a table on the lua stack.  The fields have the same names as the Material attributes.  Missing fields keep the WorldMaterial default values.
static WorldMaterialRef getWorldMaterialFromLuaTable(lua_State* state, int table_index)
{
	WorldMaterialRef mat = new WorldMaterial();
	mat->colour_rgb					= Colour3f(LuaUtils::getTableVec3fFieldWithDefault(state, table_index, "colour", Vec3f(mat->colour_rgb.r, mat->colour_rgb.g, mat->colour_rgb.b)));
	mat->colour_texture_url			= getTableStringFieldWithSizeCheck(state, table_index, "colour_texture_url", WorldObject::MAX_URL_SIZE);
	mat->emission_rgb				= Colour3f(LuaUtils::getTableVec3fFieldWithDefault(state, table_index, "emission_rgb", Vec3f(mat->emission_rgb.r, mat->emission_rgb.g, mat->emission_rgb.b)));
	mat->emission_texture_url		= getTableStringFieldWithSizeCheck(state, table_index, "emission_texture_url", WorldObject::MAX_URL_SIZE);
	mat->normal_map_url				= getTableStringFieldWithSizeCheck(state, table_index, "normal_map_url", WorldObject::MAX_URL_SIZE);
	mat->roughness.val				= (float)LuaUtils::getTableNumberFieldWithDefault(state, table_index, "roughness_val", mat->roughness.val);
	mat->roughness.texture_url		= getTableStringFieldWithSizeCheck(state, table_index, "roughness_texture_url", WorldObject::MAX_URL_SIZE);
	mat->metallic_fraction.val		= (float)LuaUtils::getTableNumberFieldWithDefault(state, table_index, "metallic_fraction_val", mat->metallic_fraction.val);
	mat->opacity.val				= (float)LuaUtils::getTableNumberFieldWithDefault(state, table_index, "opacity_val", mat->opacity.val);
	mat->tex_matrix					= LuaUtils::getTableMatrix2fFieldWithDefault(state, table_index, "tex_matrix", mat->tex_matrix);
	mat->emission_lum_flux_or_lum	= (float)LuaUtils::getTableNumberFieldWithDefault(state, table_index, "emission_lum_flux_or_lum", mat->emission_lum_flux_or_lum);
	BitUtils::setOrZeroBit(mat->flags, WorldMaterial::HOLOGRAM_FLAG,     LuaUtils::getTableBoolFieldWithDefault(state, table_index, "hologram",     /*default val=*/false));
	BitUtils::setOrZeroBit(mat->flags, WorldMaterial::DOUBLE_SIDED_FLAG, LuaUtils::getTableBoolFieldWithDefault(state, table_index, "double_sided", /*default val=*/false));

	return mat;
}


static WorldObject* getWorldObjectForUID(SubstrataLuaScript* sub_lua_script, const UID uid); // Defined below.
static std::string errorContextString(lua_State* state); // Defined below.


// Reads the Lua array of materials at array_index on the stack, and appends a WorldMaterial for each element to materials_out.
// An element can be a Material object (a table with the Material metatable, as returned by ob.materials and ob:getMaterial()), in which case the material
// it refers to is copied, with all its fields.  Otherwise the element should be a plain table of material fields, which is read with getTableWorldMaterial().
// Reads elements up to the first nil.  Throws glare::Exception if an element is not a table, or if there are more than WorldObject::maxNumMaterials() elements.
static void getWorldMaterialsFromLuaArray(lua_State* state, SubstrataLuaScript* sub_lua_script, int array_index, std::vector<WorldMaterialRef>& materials_out)
{
	array_index = lua_absindex(state, array_index); // Make the index absolute, as values are pushed below.
	LuaUtils::checkValueIsTable(state, array_index);

	const int max_num_mats = (int)WorldObject::maxNumMaterials();
	for(int i=1; i<=max_num_mats + 1; ++i) // Read one past the max, so that we can detect too many materials.
	{
		const int elem_type = lua_rawgeti(state, array_index, i); // Pushes array[i]
		if(elem_type == LUA_TNIL)
		{
			lua_pop(state, 1); // Pops nil
			break;
		}
		if(i > max_num_mats)
			throw glare::Exception("Too many materials in materials array (max " + toString(max_num_mats) + ")." + errorContextString(state));
		if(elem_type != LUA_TTABLE)
			throw glare::Exception("Material " + toString(i) + " in materials array was not a table." + errorContextString(state));

		// See if the element is a Material object, by checking if its metatable is the Material class metatable.  (lua_rawequal doesn't call any metamethods)
		bool is_material_ob = false;
		if(lua_getmetatable(state, -1)) // If the element has a metatable, pushes it and returns non-zero.  Otherwise pushes nothing.
		{
			lua_getref(state, sub_lua_script->substrata_lua_vm->worldMaterialClassMetaTable_ref.ref); // Pushes worldMaterialClassMetaTable
			is_material_ob = lua_rawequal(state, -1, -2) != 0; // Stack unchanged
			lua_pop(state, 2); // Pops worldMaterialClassMetaTable and the element's metatable
		}

		if(is_material_ob)
		{
			// Copy the material the Material object refers to.
			const UID src_ob_uid((uint64)LuaUtils::getTableLightUserDataField(state, /*table index=*/-1, "uid")); // Stack unchanged
			const size_t src_mat_index = (size_t)LuaUtils::getTableLightUserDataField(state, /*table index=*/-1, "idx"); // Stack unchanged

			const WorldObject* src_ob = getWorldObjectForUID(sub_lua_script, src_ob_uid); // Throws if there is no object with the UID.
			if(src_mat_index >= src_ob->materials.size())
				throw glare::Exception("Material " + toString(i) + " in materials array refers to a material that no longer exists." + errorContextString(state));

			materials_out.push_back(src_ob->materials[src_mat_index]->clone());
		}
		else
			materials_out.push_back(getWorldMaterialFromLuaTable(state, /*table index=*/-1)); // Stack unchanged

		lua_pop(state, 1); // Pops array[i]
	}
}


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


static SubstrataLuaScript* getSubstrataLuaScript(lua_State* state)
{
	return static_cast<SubstrataLuaScript*>(static_cast<LuaScript*>(lua_getthreaddata(state)));
}


#if 0 // GUI_CLIENT
static void enqueueMessageToSend(ClientThread& client_thread, SocketBufferOutStream& packet)
{
	MessageUtils::updatePacketLengthField(packet);

	client_thread.enqueueDataToSend(packet.buf);
}
#endif


// Returns true if the string value changed, false otherwise.
static std::string getTableStringFieldWithSizeCheck(lua_State* state, int index, const char* field_name, size_t max_size)
{
	const std::string new_string = LuaUtils::getTableStringFieldWithEmptyDefault(state, index, field_name);
	if(new_string.size() > max_size)
		throw glare::Exception("field " + std::string(field_name) + " was too long. (string had length " + toString(new_string.size()) + ", max len is " + toString(max_size) + ")" + errorContextString(state));
	
	return new_string;
}


static int createObject(lua_State* state)
{
	// Expected args:
	// Arg 1: ob_params : Table

	checkNumArgs(state, /*num_args_required*/1);
	LuaUtils::checkValueIsTable(state, /*index=*/1);

	const int ob_params_table_index = 1;

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	WorldObjectRef ob = new WorldObject();

	const double lifetime = LuaUtils::getTableNumberFieldWithDefault(state, ob_params_table_index, "lifetime", 30.0);
	if(!isFinite(lifetime) || (lifetime <= 0) || (lifetime > 60.0))
		throw glare::Exception("Invalid lifetime, must be finite, and > 0 and <= 60 (seconds)");

	if(!LuaUtils::tableHasField(state, ob_params_table_index, "model_url"))
		throw glare::Exception("createObject: model_url table field required.");
	ob->model_url = getTableStringFieldWithSizeCheck(state, ob_params_table_index, "model_url", WorldObject::MAX_URL_SIZE);
	
	ob->setCollidable(LuaUtils::getTableBoolFieldWithDefault(state, ob_params_table_index, "collidable", /*default val=*/true));
	ob->setDynamic(LuaUtils::getTableBoolFieldWithDefault(state, ob_params_table_index, "dynamic", /*default val=*/true));
	
	ob->content    = getTableStringFieldWithSizeCheck(state, ob_params_table_index, "content",    WorldObject::MAX_CONTENT_SIZE);
	ob->script     = getTableStringFieldWithSizeCheck(state, ob_params_table_index, "script",     WorldObject::MAX_SCRIPT_SIZE);
	ob->target_url = getTableStringFieldWithSizeCheck(state, ob_params_table_index, "target_url", WorldObject::MAX_URL_SIZE);


	ob->pos = Vec3d(sub_lua_script->world_object->getAABBWS().centroid() + Vec4f(sub_lua_script->world_object->getAABBWS().span()[0]/2 + 0.5f, 0,0,0)); // By default, position just to side of the script object.
	if(LuaUtils::tableHasField(state, ob_params_table_index, "pos"))
	{
		ob->pos = LuaUtils::getTableVec3dField(state, ob_params_table_index, "pos");
		if(!ob->pos.isFinite())
			throw glare::Exception("Invalid pos: not finite");
	}

	ob->axis = LuaUtils::getTableVec3fFieldWithDefault(state, ob_params_table_index, "axis", Vec3f(1,0,0));
	if(!ob->axis.isFinite() || (ob->axis.length2() < Maths::square(1.0e-6)))
		throw glare::Exception("Invalid axis: infinite, NaN or approximately zero");
	ob->angle = (float)LuaUtils::getTableNumberFieldWithDefault(state, ob_params_table_index, "angle", 0.0);
	if(!::isFinite(ob->angle))
		throw glare::Exception("Invalid angle: not finite");
	ob->scale = LuaUtils::getTableVec3fFieldWithDefault(state, ob_params_table_index, "scale", Vec3f(1,1,1));
	if(!ob->scale.isFinite() || (ob->scale.length2() < Maths::square(1.0e-6)))
		throw glare::Exception("Invalid scale: infinite, NaN or approximately zero");

	checkTransformOK(ob.ptr()); // Throws glare::Exception if not

#if SERVER
	// When an object can't be created for a reason other than invalid arguments (e.g. a limit is reached), return nil and print a warning,
	// rather than raising a Lua error, since an error would stop the script from running any more handlers.
	User* creator_user = nullptr;
	{
		ServerAllWorldsState* worlds_state = sub_lua_script->server->world_state.ptr();
		checkHoldWorldStateMutex(sub_lua_script, worlds_state);

		// The new object's creator is set to the user who created the script's object (this_object), so that user owns the new object.
		// The limits below are per user, and count objects created by all of that user's scripts.
		auto user_res = worlds_state->user_id_to_users.find(sub_lua_script->world_object->creator_id);
		if(user_res == worlds_state->user_id_to_users.end())
			throw glare::Exception("createObject(): Could not find the creator of the script object, object not created.");
		
		creator_user = user_res->second.ptr();

		if(creator_user->num_live_ephemeral_obs >= ServerAllWorldsState::MAX_NUM_EPHEMERAL_OBS_PER_USER)
		{
			sub_lua_script->printRateLimitedWarning("createObject(): Too many objects created by scripts of this user exist (max " + toString(ServerAllWorldsState::MAX_NUM_EPHEMERAL_OBS_PER_USER) + "), object not created.");
			lua_pushnil(state);
			return 1;
		}

		if(!creator_user->create_ephemeral_ob_rate_limiter.checkAddEvent(sub_lua_script->server->getCurrentGlobalTime()))
		{
			sub_lua_script->printRateLimitedWarning("createObject(): Objects are being created too often by scripts of this user (max " + toString(ServerAllWorldsState::MAX_NUM_EPHEMERAL_OBS_CREATED_PER_USER_PER_SEC) + " per second), object not created.");
			lua_pushnil(state);
			return 1;
		}

		// Set aabb_os and max_model_lod_level
		auto res = worlds_state->mesh_URL_to_mesh_info.find(ob->model_url);
		if(res != worlds_state->mesh_URL_to_mesh_info.end())
		{
			WorldStateMeshInfo& mesh_info = res->second;
			if(!mesh_info.load_succeeded)
				throw glare::Exception("Failed to load model with URL '" + toStdString(ob->model_url) + "'.  Model could not be loaded.");

			ob->setAABBOS(mesh_info.aabb_os);

			ob->max_model_lod_level = mesh_info.num_verts <= 24 ? 0 : 2;
		}
		else
		{
			// Mesh info was not cached.  Get MeshLODGenThread to load the mesh and cache its info, so that a later call can succeed.
			sub_lua_script->server->enqueueMsgForLodGenThread(new CheckGenMeshInfoForURL(ob->model_url));

			sub_lua_script->printRateLimitedWarning("createObject(): Model information for model URL '" + toStdString(ob->model_url) + "' not loaded yet, object not created.  Try again soon.");
			lua_pushnil(state);
			return 1;
		}
	}
#endif

	ob->flags |= (WorldObject::EPHEMERAL_SCRIPT_CREATED | WorldObject::EXCLUDE_FROM_LOD_CHUNK_MESH);

	ob->created_time = TimeStamp::currentTime();
	ob->last_modified_time = ob->created_time;
	ob->creator_id = sub_lua_script->world_object->creator_id;

#if SERVER
	ob->creator_name = creator_user->name;
#endif

	
	// Materials
	const int value_type = lua_rawgetfield(state, ob_params_table_index, "materials"); // Pushes materials field value (use lua_rawgetfield to avoid metamethod call).  Returns the type of the pushed value.
	if(value_type == LUA_TTABLE)
		getWorldMaterialsFromLuaArray(state, sub_lua_script, /*array index=*/-1, ob->materials); // Stack unchanged
	lua_pop(state, 1); // Pops materials field value


#if GUI_CLIENT

	// Send CreateObject message to server
	//{
	//	MessageUtils::initPacket(sub_lua_script->gui_client->scratch_packet, Protocol::CreateObject);
	//	ob->writeToNetworkStream(sub_lua_script->gui_client->scratch_packet);
	//
	//	enqueueMessageToSend(*sub_lua_script->gui_client->client_thread, sub_lua_script->gui_client->scratch_packet);
	//}
#elif SERVER

	// Insert object into world state
	{
		ServerAllWorldsState* worlds_state = sub_lua_script->server->world_state.ptr();
		checkHoldWorldStateMutex(sub_lua_script, worlds_state);

		ob->uid = worlds_state->getNextObjectUID();
		ob->state = WorldObject::State_JustCreated;
		ob->from_remote_dirty |= WorldObject::OTHER_DIRTY;

		ob->die_time = sub_lua_script->server->getCurrentGlobalTime() + lifetime;
	
		worlds_state->checkAddWorldObjectToDirtyFromRemoteSet(ob.ptr(), *sub_lua_script->world_state, *sub_lua_script->cur_world_state_lock);

		// Add to world_state->ephemeral_objects
		ServerAllWorldsState::EphemeralObInfo ob_info;
		ob_info.ob = ob;
		ob_info.world = sub_lua_script->world_state;
		ob_info.creator = creator_user;
		worlds_state->ephemeral_objects.push_back(ob_info);

		creator_user->num_live_ephemeral_obs++;

		// The object is inserted into the world's object map, and its script created and run, by the server main loop on a later tick, not here.
		// See ServerAllWorldsState::ephemeral_obs_pending_insert.
		worlds_state->ephemeral_obs_pending_insert.push_back(ob_info);
	}

#endif

#if SERVER
	sub_lua_script->pushWorldObjectTableOntoStack(state, ob->uid);
	return 1; // Count of returned values
#else
	lua_pushnil(state);
	return 1; // Count of returned values
#endif
}


// Find a WorldObject for the given UID.  Throws exception if no such object with UID found.
static WorldObject* getWorldObjectForUID(SubstrataLuaScript* sub_lua_script, const UID uid)
{
	// See if the UID is that of the sub_lua_script object, in which case we can use the sub_lua_script pointer and avoid the map lookup.
	if(uid == sub_lua_script->world_object->uid)
		return sub_lua_script->world_object;
	else
	{
#if GUI_CLIENT
		{
			WorldState* world_state = sub_lua_script->gui_client->world_state.ptr();

			checkHoldWorldStateMutex(sub_lua_script, world_state);

			auto res = world_state->objects.find(uid);
			if(res == world_state->objects.end())
				throw glare::Exception("getObjectForUID(): No object with UID " + uid.toString());

			return res.getValue().ptr();
		}
#elif SERVER
		{
			if(sub_lua_script->cur_world_state_lock == nullptr)
			{
				assert(0);
				throw glare::Exception("Internal error: cur_world_state_lock was null");
			}


			ServerWorldState::ObjectMapType& objects = sub_lua_script->world_state->getObjects(*sub_lua_script->cur_world_state_lock);

			auto res = objects.find(uid);
			if(res != objects.end())
				return res.getValue().ptr();

			// The object may have been created by createObject() and not inserted into the object map yet.
			ServerAllWorldsState* worlds_state = sub_lua_script->server->world_state.ptr();
			checkHoldWorldStateMutex(sub_lua_script, worlds_state);
			for(size_t i=0; i<worlds_state->ephemeral_obs_pending_insert.size(); ++i)
			{
				const ServerAllWorldsState::EphemeralObInfo& info = worlds_state->ephemeral_obs_pending_insert[i];
				if((info.ob->uid == uid) && (info.world == sub_lua_script->world_state))
					return info.ob.ptr();
			}

			throw glare::Exception("getObjectForUID(): No object with UID " + uid.toString());
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

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	/*WorldObject* ob =*/ getWorldObjectForUID(sub_lua_script, uid); // Just call this to throw an excep if no such object exists

	sub_lua_script->pushWorldObjectTableOntoStack(state, uid);
	return 1;
}


static int getCurrentTime(lua_State* state)
{
	// Expected args:
	// none

	checkNumArgs(state, /*num_args_required*/0);

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

#if GUI_CLIENT
	lua_pushnumber(state, sub_lua_script->gui_client->world_state->getCurrentGlobalTime()); // NOTE: do we want to use global time for this?
#elif SERVER
	lua_pushnumber(state, sub_lua_script->server->getCurrentGlobalTime()); // NOTE: do we want to use global time for this?
#endif
	return 1;
}


static const int MAX_NUM_OB_EVENT_LISTENS = 100;


static int luaAddEventListener(lua_State* state)
{
	// Expected args:
	// Arg 1: event_name : String
	// Arg 2: ob_uid : UID
	// Arg 3: handler : Function

	checkNumArgs(state, /*num_args_required*/3);
	if(lua_type(state, /*index=*/3) != LUA_TFUNCTION)
		throw glare::Exception("createTimer(): arg 3 must be a function" + errorContextString(state));

	int event_name_atom = -1;
	const char* event_name = LuaUtils::getStringAndAtom(state, /*index=*/1, event_name_atom);

	// Get object UID
	const UID ob_uid = UID((uint64)LuaUtils::getDoubleArg(state, /*index=*/2));

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);
	
	// For the client, we may be trying to add an event listener for an object that has not been sent from the server and loaded yet.
	// In this case getWorldObjectForUID will throw an exception.
	// To handle it we will add the event handler to world_state->pending_event_handlers, so that the event handler can be assigned to the object later
	// when it is actually loaded.  See pending_event_handlers usage in GUIClient::timerEvent.
	WorldObject* ob = nullptr;
	Reference<ObjectEventHandlers> ob_event_handlers;

#if GUI_CLIENT
	try
	{
		ob = getWorldObjectForUID(sub_lua_script, ob_uid); // Try and get object, but handle the exception if not found.
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
		WorldState* world_state = sub_lua_script->gui_client->world_state.ptr();
		if(world_state->pending_event_handlers[ob_uid].isNull())
			world_state->pending_event_handlers[ob_uid] = new ObjectEventHandlers();
		ob_event_handlers = world_state->pending_event_handlers[ob_uid];
	}
#else
	ob = getWorldObjectForUID(sub_lua_script, ob_uid);
	ob_event_handlers = ob->getOrCreateEventHandlers();
#endif

	// ob may be null at this point
	runtimeCheck(ob_event_handlers.nonNull()); // ob_event_handlers should be non-null


	if(sub_lua_script->num_obs_event_listening >= MAX_NUM_OB_EVENT_LISTENS)
		throw glare::Exception("Script added too many event listeners, max is " + toString(MAX_NUM_OB_EVENT_LISTENS) + errorContextString(state));

	// Work out the handler list to add the listener to, and if it is a spatial event:
	HandlerList* handler_list = nullptr;
	[[maybe_unused]] bool is_spatial_event = false;

	switch(event_name_atom)
	{
	case Atom_onUserUsedObject:
		assert(stringEqual(event_name, "onUserUsedObject"));
		handler_list = &ob_event_handlers->onUserUsedObject_handlers;
		break;
	case Atom_onUserTouchedObject:
		assert(stringEqual(event_name, "onUserTouchedObject"));
		handler_list = &ob_event_handlers->onUserTouchedObject_handlers;
		break;
	case Atom_onUserMovedNearToObject:
		assert(stringEqual(event_name, "onUserMovedNearToObject"));
		handler_list = &ob_event_handlers->onUserMovedNearToObject_handlers;
		is_spatial_event = true;
		break;
	case Atom_onUserMovedAwayFromObject:
		assert(stringEqual(event_name, "onUserMovedAwayFromObject"));
		handler_list = &ob_event_handlers->onUserMovedAwayFromObject_handlers;
		is_spatial_event = true;
		break;
	case Atom_onUserEnteredParcel:
		assert(stringEqual(event_name, "onUserEnteredParcel"));
		handler_list = &ob_event_handlers->onUserEnteredParcel_handlers;
		is_spatial_event = true;
		break;
	case Atom_onUserExitedParcel:
		assert(stringEqual(event_name, "onUserExitedParcel"));
		handler_list = &ob_event_handlers->onUserExitedParcel_handlers;
		is_spatial_event = true;
		break;
	case Atom_onUserEnteredVehicle:
		assert(stringEqual(event_name, "onUserEnteredVehicle"));
		handler_list = &ob_event_handlers->onUserEnteredVehicle_handlers;
		break;
	case Atom_onUserExitedVehicle:
		assert(stringEqual(event_name, "onUserExitedVehicle"));
		handler_list = &ob_event_handlers->onUserExitedVehicle_handlers;
		break;
	case Atom_onChatMessage:
		assert(stringEqual(event_name, "onChatMessage"));
		handler_list = &ob_event_handlers->onChatMessage_handlers;
		break;
	default:
		throw glare::Exception("Unknown event '" + std::string(event_name) + "'" + errorContextString(state));
	}

	const LuaVM::LuaValueRef handler_func_ref = sub_lua_script->getRefToFunctionOnStack(state, /*index=*/3); // Get a reference to the Lua handler function

	const void* handler_func_ptr = lua_topointer(state, /*index=*/3);

	HandlerFunc handler_func;
	handler_func.script = WeakReference<SubstrataLuaScript>(sub_lua_script);
	handler_func.handler_func_ref = handler_func_ref;
	handler_func.function_ptr = handler_func_ptr;

	// Try and add the handler to the list of handlers:
	runtimeCheck(handler_list);
	const bool added = handler_list->addHandler(handler_func);
	if(added)
		sub_lua_script->num_obs_event_listening++;
	else
		sub_lua_script->freeRefToFunction(handler_func_ref); // Release the reference to the handler func as we are not holding on to it

#if GUI_CLIENT
	if(is_spatial_event && ob)
		sub_lua_script->gui_client->scripted_ob_proximity_checker.addObject(ob);
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

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	if(av_uid == sub_lua_script->gui_client->client_avatar_uid)
	{
		// We want to send to ourselves
		if(sub_lua_script->gui_client->script_message_ui)
			sub_lua_script->gui_client->script_message_ui->showMessage(msg);
	}
#endif

	return 0;
}


// showMessage(msg): shows the message to the local user.  For client-side code that has no avatar to pass to showMessageToUser(),
// such as UI button onClick callbacks.  Does nothing on the server.
static int showMessage(lua_State* state)
{
	// Expected args:
	// Arg 1: msg : String

	checkNumArgs(state, /*num_args_required*/1);

	[[maybe_unused]] const std::string msg = LuaUtils::getString(state, /*index=*/1);

#if GUI_CLIENT
	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	if(sub_lua_script->gui_client->script_message_ui)
		sub_lua_script->gui_client->script_message_ui->showMessage(msg);
#endif

	return 0;
}


#if SERVER
static constexpr size_t MAX_OBJECT_STORAGE_ITEM_SIZE_B = 1 << 16;
#endif

static int objectStorageGetItem(lua_State* state)
{
	// Expected args:
	// Arg 1: key : String

	checkNumArgs(state, /*num_args_required*/1);

	const std::string key_string = LuaUtils::getStringArg(state, /*index=*/1);

#if GUI_CLIENT
	lua_pushnil(state); // Return nil 
#endif
#if SERVER
	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	ServerAllWorldsState* world_state = sub_lua_script->server->world_state.ptr();
	checkHoldWorldStateMutex(sub_lua_script, world_state);

	ObjectStorageKey key;
	key.ob_uid = sub_lua_script->world_object->uid;
	key.key_string = key_string;
	auto res = world_state->object_storage_items.find(key);
	if(res == world_state->object_storage_items.end())
	{
		lua_pushnil(state); // Return nil if no item found for key.
	}
	else
	{
		try
		{
			BufferViewInStream stream(ArrayRef<uint8>(res->second->data.data(), res->second->data.size()));
			LuaSerialisation::DeserialisationOptions options;
			options.max_table_entries = MAX_OBJECT_STORAGE_ITEM_SIZE_B / 2;
			LuaSerialisation::deserialise(state, sub_lua_script->substrata_lua_vm->metatable_uid_to_ref_map, stream, options); // Pushes deserialised Lua value onto Lua stack.
		}
		catch(glare::Exception& /*e*/)
		{
			throw glare::Exception("Error while deserialising Lua object.");
		}
	}
#endif
	return 1;
}


static int objectStorageSetItem(lua_State* state)
{
	// Expected args:
	// Arg 1: key : string
	// Arg 2: value : Any serialisable Lua object

	checkNumArgs(state, /*num_args_required*/2);

	const std::string key_string = LuaUtils::getStringArg(state, /*index=*/1);
	if(key_string.size() > 256)
		throw glare::Exception("Key is too long");

#if SERVER
	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	ServerAllWorldsState* world_state = sub_lua_script->server->world_state.ptr();
	checkHoldWorldStateMutex(sub_lua_script, world_state);

	try
	{
		// Serialise the Lua value to a buffer.
		BufferOutStream buf_stream;
		LuaSerialisation::SerialisationOptions options;
		options.max_depth = 16;
		options.max_serialised_size_B = MAX_OBJECT_STORAGE_ITEM_SIZE_B;
		LuaSerialisation::serialise(state, /*stack index=*/2, options, buf_stream);

		// Look up ObjectStorage item, create if not present already.
		ObjectStorageKey key;
		key.ob_uid = sub_lua_script->world_object->uid;
		key.key_string = key_string;

		Reference<ObjectStorageItem> item = world_state->getOrCreateObjectStorageItem(key); // May throw
		assert(item->key == key);

		// Set the item data
		item->data = buf_stream.buf;

		world_state->db_dirty_object_storage_items.insert(item);
		world_state->markAsChanged();
	}
	catch(glare::Exception& e)
	{
		throw glare::Exception("Error serialising Lua object: " + e.what());
	}
#endif
	
	return 0;
}


static int doHTTPGetRequestAsync(lua_State* state)
{
	// Expected args:
	// Arg 1: URL : string
	// Arg 2: additional header lines : table
	// Arg 3: onDone callback
	// Arg 4: onError callback

	// onDone gets passed:
	// 
	// {
	//   response_code: number
	//   response_message : string
	//   mime_type : string
	//   body_data : buffer
	// }

	// onError gets passed
	// {
	//	  error_code : number
	//	  error_description : string
	// }

	checkNumArgs(state, /*num_args_required*/4);

	const std::string URL_string = LuaUtils::getStringArg(state, /*index=*/1);
	LuaUtils::checkArgIsFunction(state, /*index=*/3);
	LuaUtils::checkArgIsFunction(state, /*index=*/4);

#if SERVER
	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	ServerAllWorldsState* world_state = sub_lua_script->server->world_state.ptr();
	checkHoldWorldStateMutex(sub_lua_script, world_state);

	if(!BitUtils::isBitSet(world_state->feature_flag_info.feature_flags, ServerAllWorldsState::LUA_HTTP_REQUESTS_FEATURE_FLAG))
		throw glare::Exception("Lua HTTP requests feature-flag disabled, not doing request");

	Reference<LuaHTTPRequest> request = new LuaHTTPRequest();
	request->script_user_id = sub_lua_script->world_object->creator_id;
	request->sub_lua_script = sub_lua_script;
	request->request_type = "GET";
	request->URL = URL_string;

	LuaUtils::checkValueIsTable(state, /*index=*/2);
	lua_pushnil(state); // Push first key onto stack
	while(1)
	{
		int notdone = lua_next(state, /*table_index=*/2); // pops a key from the stack, and pushes a key-value pair from the table at the given index
		if(notdone == 0)
			break;

		// Check the key type, as getString() would convert a number key to a string in place, which would break lua_next.
		if(lua_type(state, -2) != LUA_TSTRING)
			throw glare::Exception("doHTTPGetRequestAsync(): additional header table keys must be strings (header names)" + errorContextString(state));

		request->additional_headers.push_back(LuaUtils::getString(state, -2));
		request->additional_headers.back() += ": ";
		request->additional_headers.back() += LuaUtils::getString(state, -1);

		lua_pop(state, 1); // Remove value, keep key on stack for next lua_next call
	}

	request->onDone_ref  = sub_lua_script->getRefToFunctionOnStack(state, /*index=*/3);
	request->onError_ref = sub_lua_script->getRefToFunctionOnStack(state, /*index=*/4);

	sub_lua_script->server->enqueueLuaHTTPRequest(request);
#endif
	return 0;
}


static int doHTTPPostRequestAsync(lua_State* state)
{
	// Expected args:
	// Arg 1: URL : string
	// Arg 2: post_content : string
	// Arg 3: content_type: string
	// Arg 4: additional header lines : table
	// Arg 5: onDone callback
	// Arg 6: onError callback

	// onDone gets passed:
	// 
	// {
	//   response_code: number
	//   response_message : string
	//   mime_type : string
	//   body_data : buffer
	// }

	// onError gets passed
	// {
	//	  error_code : number
	//	  error_description : string
	// }

	checkNumArgs(state, /*num_args_required*/6);

	const std::string URL_string = LuaUtils::getStringArg(state, /*index=*/1);
	const std::string post_content = LuaUtils::getStringArg(state, /*index=*/2);
	const std::string content_type = LuaUtils::getStringArg(state, /*index=*/3);
	// Arg 4 is additional header lines, parsed below.
	LuaUtils::checkArgIsFunction(state, /*index=*/5);
	LuaUtils::checkArgIsFunction(state, /*index=*/6);

#if SERVER
	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	ServerAllWorldsState* world_state = sub_lua_script->server->world_state.ptr();
	checkHoldWorldStateMutex(sub_lua_script, world_state);

	if(!BitUtils::isBitSet(world_state->feature_flag_info.feature_flags, ServerAllWorldsState::LUA_HTTP_REQUESTS_FEATURE_FLAG))
		throw glare::Exception("Lua HTTP requests feature-flag disabled, not doing request");

	Reference<LuaHTTPRequest> request = new LuaHTTPRequest();
	request->script_user_id = sub_lua_script->world_object->creator_id;
	request->sub_lua_script = sub_lua_script;
	request->request_type = "POST";
	request->URL = URL_string;
	request->post_content = post_content;
	request->content_type = content_type;

	LuaUtils::checkValueIsTable(state, /*index=*/4);
	lua_pushnil(state); // Push first key onto stack
	while(1)
	{
		int notdone = lua_next(state, /*table_index=*/4); // pops a key from the stack, and pushes a key-value pair from the table at the given index
		if(notdone == 0)
			break;

		// Check the key type, as getString() would convert a number key to a string in place, which would break lua_next.
		if(lua_type(state, -2) != LUA_TSTRING)
			throw glare::Exception("doHTTPPostRequestAsync(): additional header table keys must be strings (header names)" + errorContextString(state));

		request->additional_headers.push_back(LuaUtils::getString(state, -2));
		request->additional_headers.back() += ": ";
		request->additional_headers.back() += LuaUtils::getString(state, -1);

		lua_pop(state, 1); // Remove value, keep key on stack for next lua_next call
	}

	request->onDone_ref  = sub_lua_script->getRefToFunctionOnStack(state, /*index=*/5);
	request->onError_ref = sub_lua_script->getRefToFunctionOnStack(state, /*index=*/6);

	sub_lua_script->server->enqueueLuaHTTPRequest(request);
#endif
	return 0;
}


static int getSecret(lua_State* state)
{
	// Expected args:
	// Arg 1: secret_name : string

	checkNumArgs(state, /*num_args_required*/1);

	const std::string secret_name = LuaUtils::getStringArg(state, /*index=*/1);

#if GUI_CLIENT
	lua_pushnil(state);
#endif
#if SERVER
	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	ServerAllWorldsState* world_state = sub_lua_script->server->world_state.ptr();
	checkHoldWorldStateMutex(sub_lua_script, world_state);

	const UserSecretKey key(/*user_id = */sub_lua_script->world_object->creator_id, secret_name);
	auto res = world_state->user_secrets.find(key);
	if(res == world_state->user_secrets.end())
	{
		lua_pushnil(state); // Return nil if no item found for key.
	}
	else
	{
		LuaUtils::pushString(state, res->second->value);
	}
#endif
	return 1;
}


// Pushes the converted JSON node onto the top of the Lua stack
static void pushJSONLuaNode(lua_State* state, const JSONNode& node)
{
	if(!lua_checkstack(state, /*size=*/1)) // Make sure there is space for the value on the Lua stack
		throw glare::Exception("Failed to alloc lua stack space");

	switch (node.getType())
	{
	case JSONNodeType::Type_Number:
	{
		lua_pushnumber(state, node.getDoubleValue());
		break;	
	}
	case JSONNodeType::Type_String:
	{
		const string_view string_val = node.getStringValue();
		lua_pushlstring(state, string_val.data(), string_val.size());
		break;	
	}
	case JSONNodeType::Type_Boolean:
	{
		lua_pushboolean(state, node.getBoolValue());
		break;
	}
	case JSONNodeType::Type_Array:
	{
		lua_createtable(state, /*num array elems hint=*/(int)node.numChildren(), /*num other elems hint=*/0);
		for(size_t i=0; i<node.numChildren(); ++i)
		{
			pushJSONLuaNode(state, node.getArrayChildNode(i));

			lua_rawseti(state, /*table index=*/-2, 1 + (int)i);
		}

		break;
	}
	case JSONNodeType::Type_Object:
	{
		lua_createtable(state, /*num array elems hint=*/0, /*num other elems hint=*/(int)node.numNameValPairs());
		for(auto it = node.objectChildrenBegin(); it != node.objectChildrenEnd(); ++it)
		{
			pushJSONLuaNode(state, it.valueNode());

			lua_setfield(state, /*table index=*/-2, toString(it.name()).c_str());
		}

		break;
	}
	case JSONNodeType::Type_Null:
	{
		lua_pushnil(state);
		break;
	}
	default:
		runtimeCheckFailed("Invalid JSON type");
	}
}


static int parseJSON(lua_State* state)
{
	// arg 1: json : string

	// Returns lua object

	checkNumArgs(state, /*num_args_required*/1);

	const std::string json_string = LuaUtils::getStringArg(state, /*index=*/1);

	try
	{
		JSONParser parser;
		parser.parseBuffer(json_string.c_str(), json_string.size());

		pushJSONLuaNode(state, parser.rootNode());
	}
	catch(glare::Exception& e)
	{
		throw glare::Exception("Error while parsing JSON: " + e.what());
	}

	return 1;
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


	const double raw_interval_time = LuaUtils::getDoubleArg(state, /*index=*/2);
	const bool repeating = LuaUtils::getBool(state, /*index=*/3);

	// Don't allow the interval time to be too low.
	const double interval_time = myMax(0.1, raw_interval_time);

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);
#if GUI_CLIENT
	const double cur_time = sub_lua_script->gui_client->total_timer.elapsed();
	ScriptTimerQueue& timer_queue = sub_lua_script->gui_client->script_timer_queue;
#elif SERVER
	const double cur_time = sub_lua_script->server->total_timer.elapsed();
	ScriptTimerQueue& timer_queue = sub_lua_script->server->timer_queue;
#endif


	// Get a reference to the onTimerEvent function
	const LuaVM::LuaValueRef onTimerEvent_ref = sub_lua_script->getRefToFunctionOnStack(state, /*index=*/1);


	// Find free timer slot
	for(int i=0; i<SubstrataLuaScript::MAX_NUM_TIMERS; ++i)
	{
		if(sub_lua_script->timers[i].id == -1) // If timer slot is free:
		{
			const int timer_id = sub_lua_script->next_timer_id++; // Make new unique timer id (to avoid ABA problem)

			// Record in slot
			sub_lua_script->timers[i].id = timer_id;
			sub_lua_script->timers[i].onTimerEvent_ref = onTimerEvent_ref;

			ScriptTimerQueueTimer timer;
			timer.onTimerEvent_ref = onTimerEvent_ref;
			timer.tigger_time = timer_queue.getFirstTriggerTime(cur_time, interval_time, repeating);
			timer.repeating = repeating;
			timer.period = interval_time;
			timer.timer_index = i;
			timer.timer_id = timer_id;
			//timer.sub_lua_script_handle = sub_lua_script->generational_handle;
			timer.sub_lua_script = sub_lua_script;
			timer_queue.addTimer(cur_time, timer);

			lua_pushnumber(state, (double)timer_id); // Push timer id
			return 1; // Count of returned values
		}
	}

	// If got here, there are no free timer slots
	sub_lua_script->freeRefToFunction(onTimerEvent_ref); // Free the callback reference so we don't leak it
	throw glare::Exception("createTimer(): Could not create timer, 4 timers already created." + errorContextString(state));
//#else
//	throw glare::Exception("createTimer(): todo on server.");
//#endif
}


static int destroyTimer(lua_State* state)
{
	// arg 1: timer_id : Number

	checkNumArgs(state, /*num_args_required*/1);

	// Check the value is finite and in int range before converting it.  Also reject negative values, to avoid timer_id of -1 which is what destroyed timers have as IDs.
	const double timer_id_double = LuaUtils::getDoubleArg(state, /*index=*/1);
	if(!isFinite(timer_id_double) || !(timer_id_double >= 0.0 && timer_id_double <= (double)std::numeric_limits<int>::max()))
		throw glare::Exception("destroyTimer(): Invalid timer ID." + errorContextString(state));
	const int timer_id = (int)timer_id_double;

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	for(int i=0; i<SubstrataLuaScript::MAX_NUM_TIMERS; ++i)
		if(sub_lua_script->timers[i].id == timer_id)
		{
			sub_lua_script->destroyTimer(/*timer index=*/i, timer_id);
		}
	
	return 0; // Count of returned values
}


static int getNumMaterials(lua_State* state)
{
	// arg 1: ob : WorldObject

	checkNumArgs(state, /*num_args_required*/1);

	// Get object UID
	const UID ob_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	
	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	WorldObject* ob = getWorldObjectForUID(sub_lua_script, ob_uid);


	lua_pushnumber(state, (double)ob->materials.size());
	return 1; // Count of returned values
}


// Pushes a Material table for the material with the given (zero-based) index of the object with UID ob_uid onto the stack.
static void pushWorldMaterialTable(lua_State* state, SubstrataLuaScript* sub_lua_script, UID ob_uid, size_t index)
{
	// Make a material table with object UID and material index
	lua_createtable(state, /*num array elems=*/0, /*num non-array elems=*/2); // Pushes new material table

	//LuaUtils::setNumberAsTableField(state, "uid", (double)uid.value());
	LuaUtils::setLightUserDataAsTableField(state, "uid", (void*)ob_uid.value()); // Sets material_table.uid, stack unchanged
	//LuaUtils::setNumberAsTableField(state, "idx", (double)index);
	LuaUtils::setLightUserDataAsTableField(state, "idx", (void*)index); // Sets material_table.idx, stack unchanged

	// Set metatable to worldMaterialClassMetaTable_ref
	lua_getref(state, sub_lua_script->substrata_lua_vm->worldMaterialClassMetaTable_ref.ref); // Pushes worldMaterialClassMetaTable onto the stack.
	lua_setmetatable(state, -2); // Pops worldMaterialClassMetaTable, sets it as the metatable of the material table.  Material table is left on top of the stack.
}


static int getMaterial(lua_State* state)
{
	// arg 1: ob : WorldObject
	// arg 2: index : Number

	checkNumArgs(state, /*num_args_required*/2);

	// Get object UID
	const UID ob_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	const double value = LuaUtils::getDoubleArg(state, /*index=*/2);

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	WorldObject* ob = getWorldObjectForUID(sub_lua_script, ob_uid);

	if(!isFinite(value) || !(value >= 0.0 && value < (double)ob->materials.size()))
		throw glare::Exception("Invalid material index");
	const size_t index = (size_t)value;

	if(index >= ob->materials.size())
		throw glare::Exception("Invalid material index" + errorContextString(state));

	pushWorldMaterialTable(state, sub_lua_script, ob_uid, index); // Pushes material table

	return 1; // Count of returned values
}


static int playAudio(lua_State* state)
{
	// arg 1: ob : WorldObject

	checkNumArgs(state, /*num_args_required*/1);

#if GUI_CLIENT
	
	// Get object UID
	const UID ob_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	WorldObject* ob = getWorldObjectForUID(sub_lua_script, ob_uid);

	if(ob->audio_source)
		sub_lua_script->gui_client->audio_engine.seekToStartAndUnpauseAudio(*ob->audio_source);
#endif

	return 0; // Count of returned values
}


static int isPlayingAudio(lua_State* state)
{
	// arg 1: ob : WorldObject

	checkNumArgs(state, /*num_args_required*/1);

	bool is_playing = false;
#if GUI_CLIENT
	
	// Get object UID
	const UID ob_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	WorldObject* ob = getWorldObjectForUID(sub_lua_script, ob_uid);

	if(ob->audio_source)
		is_playing = ob->audio_source->isPlaying();
#endif

	lua_pushboolean(state, is_playing ? 1 : 0);
	return 1; // Count of returned values
}


static int playSound(lua_State* state)
{
	// arg 1: ob : WorldObject
	// arg 2: resource_URL : String

	checkNumArgs(state, /*num_args_required*/2);

	// The script runs on the server and on clients.  To avoid playing the sound twice:
	// * A client plays the sound itself only if its own avatar triggered the event being handled (e.g. it touched the object), so the user hears it without network delay.
	// * The server broadcasts a ScriptPlaySound message holding the avatar that triggered the event (invalid for e.g. timers).  Clients play it unless their own avatar triggered the event.

	// Get object UID
	const UID ob_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));

	const string_view resource_URL = LuaUtils::getStringArgAsStringView(state, /*index=*/2);

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	if(!sub_lua_script->isResourceURLDeclared(resource_URL))
	{
		sub_lua_script->warnAboutUndeclaredResourceURL("playSound", resource_URL);
		return 0;
	}

	WorldObject* ob = getWorldObjectForUID(sub_lua_script, ob_uid);

#if GUI_CLIENT
	if(sub_lua_script->calling_avatar_uid.valid() && (sub_lua_script->calling_avatar_uid == sub_lua_script->gui_client->client_avatar_uid))
		sub_lua_script->gui_client->playScriptSound(*ob, URLString(resource_URL));

	// We don't need to send a ScriptPlaySound message to the server, instead we rely on e.g. UserUsedObjectMessages being sent to the server, and the same script calling playSound on the server.

#elif SERVER
	if(!sub_lua_script->play_sound_rate_limiter.checkAddEvent(sub_lua_script->server->getCurrentGlobalTime()))
		return 0; // Over the rate limit, don't broadcast.

	ServerAllWorldsState* worlds_state = sub_lua_script->server->world_state.ptr();
	checkHoldWorldStateMutex(sub_lua_script, worlds_state);

	SocketBufferOutStream& packet = worlds_state->getLuaScratchPacket();
	MessageUtils::initPacket(packet, Protocol::ScriptPlaySound);
	writeToStream(ob->uid, packet);
	packet.writeStringLengthFirst(resource_URL);
	writeToStream(sub_lua_script->calling_avatar_uid, packet);
	MessageUtils::updatePacketLengthField(packet);

	sub_lua_script->server->enqueuePacketToBroadcastForWorld(packet, sub_lua_script->world_state);
#endif

	return 0; // Count of returned values
}


static int startPlayingAnimation(lua_State* state)
{
	// arg 1: ob : WorldObject
	// arg 2: animation_index : Number

	checkNumArgs(state, /*num_args_required*/2);

#if GUI_CLIENT
	
	// Get object UID from arg 1.
	const UID ob_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));

	const int animation_index = (int)LuaUtils::getDoubleArg(state, /*index=*/2);

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	WorldObject* ob = getWorldObjectForUID(sub_lua_script, ob_uid);
	
	if(ob->opengl_engine_ob)
	{
		if(ob->opengl_engine_ob->next_anim_i != -1)
		{
			ob->opengl_engine_ob->current_anim_i = ob->opengl_engine_ob->next_anim_i;
		}

		ob->opengl_engine_ob->next_anim_i = animation_index;

		const double cur_time = Clock::getTimeSinceInit();
		const double TRANSITION_PERIOD = 0.3; // TODO: set this with a default arg?

		ob->opengl_engine_ob->transition_start_time = cur_time;
		ob->opengl_engine_ob->transition_end_time   = cur_time + TRANSITION_PERIOD;
	}
#endif

	return 0; // Count of returned values
}


// Try and find an animation for an object by name.  Return -1 if no such animation found.
static int getAnimationIndex(lua_State* state)
{
	// arg 1: ob : WorldObject
	// arg 2: animation_name: String

	checkNumArgs(state, /*num_args_required*/2);

	int result_anim_index = -1;
#if GUI_CLIENT
	
	// Get object UID
	const UID ob_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));

	const std::string animation_name = LuaUtils::getStringArg(state, /*index=*/2);

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	WorldObject* ob = getWorldObjectForUID(sub_lua_script, ob_uid);
	
	if(ob->opengl_engine_ob && ob->opengl_engine_ob->mesh_data)
		result_anim_index = ob->opengl_engine_ob->mesh_data->animation_data.getAnimationIndex(animation_name);

#endif
	lua_pushnumber(state, result_anim_index);

	return 1; // Count of returned values
}


#if SERVER
// Look up the object referred to by the WorldObject table at the given stack index, and check that the calling script has
// permission to modify it.  Throws glare::Exception on failure.  Must be called with the world state lock held.
static WorldObject* getObjectForScriptedMove(lua_State* state, int table_index)
{
	const UID uid((uint64)LuaUtils::getTableNumberField(state, table_index, "uid"));

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	if(sub_lua_script->cur_world_state_lock == nullptr)
	{
		assert(0);
		throw glare::Exception("Internal error: cur_world_state_lock was null");
	}

	WorldObject* ob = getWorldObjectForUID(sub_lua_script, uid);

	// A script has permission to modify an object if and only if the creator of the script is also the creator of the object.
	if(ob->creator_id != sub_lua_script->world_object->creator_id)
		throw glare::Exception("Script does not have permissions to modify object (ob UID: " + uid.toString() + ")" + errorContextString(state));

	return ob;
}


// Parse the trailing optional arguments common to moveTo()/rotateTo(): an easing value at easing_index and an onCompleted
// callback at callback_index (fixed positions; pass nil to skip an earlier optional arg).  easing_out is left unchanged if
// no easing arg is given; callback_ref_out is set to a Lua reference to the callback function, or left as LUA_NOREF if none.
static void parseMoveOptionalArgs(LuaScript* script, lua_State* state, int easing_index, int callback_index, uint32& easing_out, LuaVM::LuaValueRef& callback_ref_out)
{
	const int num_args = lua_gettop(state);

	// Optional easing value (number).
	if((num_args >= easing_index) && (lua_type(state, easing_index) != LUA_TNIL))
		easing_out = (uint32)LuaUtils::getDoubleArg(state, easing_index);

	// Optional onCompleted callback (function).
	if((num_args >= callback_index) && (lua_type(state, callback_index) != LUA_TNIL))
	{
		if(lua_type(state, callback_index) != LUA_TFUNCTION)
			throw glare::Exception("onCompleted argument must be a function." + errorContextString(state));
		callback_ref_out = script->getRefToFunctionOnStack(state, callback_index); // NOTE: Luau lua_ref references the value at the given index without popping it.
	}
}


// Schedule a one-shot Lua callback to fire 'delay' seconds from now, reusing the script timer machinery.  callback_ref must
// already have been obtained via lua_ref.  When the timer fires the callback is invoked (with the script's object as its
// argument, as for timer callbacks) and its slot + reference are freed automatically (see the timer loop in Server.cpp).
static void scheduleOneShotServerCallback(SubstrataLuaScript* sub_lua_script, LuaVM::LuaValueRef callback_ref, double delay)
{
	const double cur_time = sub_lua_script->server->total_timer.elapsed();
	ScriptTimerQueue& timer_queue = sub_lua_script->server->timer_queue;

	for(int i=0; i<SubstrataLuaScript::MAX_NUM_TIMERS; ++i)
	{
		if(sub_lua_script->timers[i].id == -1) // If timer slot is free:
		{
			const int timer_id = sub_lua_script->next_timer_id++; // Make new unique timer id (to avoid ABA problem)

			sub_lua_script->timers[i].id = timer_id;
			sub_lua_script->timers[i].onTimerEvent_ref = callback_ref;

			ScriptTimerQueueTimer timer;
			timer.onTimerEvent_ref = callback_ref;
			timer.tigger_time = cur_time + delay;
			timer.repeating = false; // One-shot.
			timer.period = delay;
			timer.timer_index = i;
			timer.timer_id = timer_id;
			timer.sub_lua_script = sub_lua_script;
			timer_queue.addTimer(cur_time, timer);
			return;
		}
	}

	// No free timer slot.  Free the callback reference so we don't leak it, then report the error.
	sub_lua_script->freeRefToFunction(callback_ref);
	throw glare::Exception("Could not register onCompleted callback: too many active timers/callbacks (max " + toString(SubstrataLuaScript::MAX_NUM_TIMERS) + ").");
}
#endif // SERVER


// object:moveTo(target_pos, duration, [easing], [onCompleted]): smoothly move the object from its current position to target_pos over
// 'duration' seconds.  Executes on the server, which broadcasts an ObjectMoveTo message; clients interpolate based on
// the shared global clock.
static int luaMoveTo(lua_State* state)
{
	// arg 1: ob : WorldObject
	// arg 2: target_pos : Vec3d
	// arg 3: duration : Number (seconds)
	// arg 4: easing : Number (optional; pass nil for the default if supplying arg 5)
	// arg 5: onCompleted : function (optional; called on the server when the move finishes)

#if SERVER
	if(lua_gettop(state) < 3)
		throw glare::Exception("moveTo() requires at least 3 arguments (object, target position, duration)" + errorContextString(state));

	const Vec3d target_pos = LuaUtils::getVec3d(state, /*index=*/2);
	const float duration   = (float)LuaUtils::getDoubleArg(state, /*index=*/3);

	if(!(duration > 0.f) || !isFinite(duration))
		throw glare::Exception("moveTo() duration must be > 0" + errorContextString(state));

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	WorldObject* ob = getObjectForScriptedMove(state, /*table index=*/1); // Looks up the object and checks modify permission.

	// Optional args: easing (index 4), onCompleted callback (index 5).
	uint32 easing = Protocol::MoveTo_EasingSmoothstep;
	LuaVM::LuaValueRef callback_ref;
	parseMoveOptionalArgs(sub_lua_script, state, /*easing_index=*/4, /*callback_index=*/5, easing, callback_ref);

	const Vec3d start_pos = ob->pos;
	const double start_time = sub_lua_script->server->getCurrentGlobalTime();

	ServerAllWorldsState* worlds_state = sub_lua_script->server->world_state.ptr();
	checkHoldWorldStateMutex(sub_lua_script, worlds_state);

	// Build and broadcast the ObjectMoveTo message.
	SocketBufferOutStream& packet = worlds_state->getLuaScratchPacket();
	MessageUtils::initPacket(packet, Protocol::ObjectMoveTo);
	writeToStream(ob->uid, packet);
	packet.writeDouble(start_time);
	packet.writeFloat(duration);
	packet.writeUInt32(easing);
	writeToStream(start_pos,  packet);
	writeToStream(target_pos, packet);
	MessageUtils::updatePacketLengthField(packet);

	sub_lua_script->server->enqueuePacketToBroadcastForWorld(packet, sub_lua_script->world_state);

	// Update the object's canonical position to the target, so it persists and late-joining clients see the final position.
	// Note: we deliberately do not set TRANSFORM_DIRTY in from_remote_dirty, as that would broadcast an ObjectTransformUpdate that snaps clients to the target.
	ob->pos = target_pos;
	worlds_state->checkAddWorldObjectAsDBDirty(ob, *sub_lua_script->world_state, *sub_lua_script->cur_world_state_lock);

	// If an onCompleted callback was provided, schedule it to fire when the move finishes.
	if(callback_ref.ref != LUA_NOREF)
		scheduleOneShotServerCallback(sub_lua_script, callback_ref, duration);
#endif // SERVER

	return 0; // Count of returned values
}


// object:rotateTo(target_axis, target_angle, duration, [easing], [onCompleted]): smoothly rotate the object from its current orientation
// to the given axis-angle orientation over 'duration' seconds.  Executes on the server (see luaMoveTo).
static int luaRotateTo(lua_State* state)
{
	// arg 1: ob : WorldObject
	// arg 2: target_axis : Vec3f
	// arg 3: target_angle : Number (radians)
	// arg 4: duration : Number (seconds)
	// arg 5: easing : Number (optional; pass nil for the default if supplying arg 6)
	// arg 6: onCompleted : function (optional; called on the server when the rotation finishes)

#if SERVER
	if(lua_gettop(state) < 4)
		throw glare::Exception("rotateTo() requires at least 4 arguments (object, target axis, target angle, duration)" + errorContextString(state));

	const Vec3f target_axis  = LuaUtils::getVec3f(state, /*index=*/2);
	const float target_angle = (float)LuaUtils::getDoubleArg(state, /*index=*/3);
	const float duration     = (float)LuaUtils::getDoubleArg(state, /*index=*/4);

	if(!(duration > 0.f) || !isFinite(duration))
		throw glare::Exception("rotateTo() duration must be > 0" + errorContextString(state));

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	WorldObject* ob = getObjectForScriptedMove(state, /*table index=*/1); // Looks up the object and checks modify permission.

	// Optional args: easing (index 5), onCompleted callback (index 6).
	uint32 easing = Protocol::MoveTo_EasingSmoothstep;
	LuaVM::LuaValueRef callback_ref;
	parseMoveOptionalArgs(sub_lua_script, state, /*easing_index=*/5, /*callback_index=*/6, easing, callback_ref);

	const Vec3f start_axis = ob->axis;
	const float start_angle = ob->angle;
	const double start_time = sub_lua_script->server->getCurrentGlobalTime();

	ServerAllWorldsState* worlds_state = sub_lua_script->server->world_state.ptr();
	checkHoldWorldStateMutex(sub_lua_script, worlds_state);

	// Build and broadcast the ObjectRotateTo message.
	SocketBufferOutStream& packet = worlds_state->getLuaScratchPacket();
	MessageUtils::initPacket(packet, Protocol::ObjectRotateTo);
	writeToStream(ob->uid, packet);
	packet.writeDouble(start_time);
	packet.writeFloat(duration);
	packet.writeUInt32(easing);
	writeToStream(start_axis, packet);
	packet.writeFloat(start_angle);
	writeToStream(target_axis, packet);
	packet.writeFloat(target_angle);
	MessageUtils::updatePacketLengthField(packet);

	sub_lua_script->server->enqueuePacketToBroadcastForWorld(packet, sub_lua_script->world_state);

	// Update the object's canonical orientation to the target (see note in luaMoveTo).
	ob->axis = target_axis;
	ob->angle = target_angle;
	worlds_state->checkAddWorldObjectAsDBDirty(ob, *sub_lua_script->world_state, *sub_lua_script->cur_world_state_lock);

	// If an onCompleted callback was provided, schedule it to fire when the rotation finishes.
	if(callback_ref.ref != LUA_NOREF)
		scheduleOneShotServerCallback(sub_lua_script, callback_ref, duration);
#endif // SERVER

	return 0; // Count of returned values
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

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	WorldObject* ob = getWorldObjectForUID(sub_lua_script, uid);

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
	case Atom_target_url:
		assert(stringEqual(key_str, "target_url"));
		LuaUtils::pushString(state, ob->target_url);
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
	case Atom_script:
		assert(stringEqual(key_str, "script"));
		LuaUtils::pushString(state, ob->script);
		break;
	case Atom_audio_loop:
		assert(stringEqual(key_str, "audio_loop"));
		lua_pushboolean(state, BitUtils::isBitSet(ob->flags, WorldObject::AUDIO_LOOP));
		break;
	case Atom_materials:
	{
		assert(stringEqual(key_str, "materials"));

		// Push a Lua array (indexed from 1, like the materials table passed to createObject()) of the object's materials.  So materials[1] is the same material as getMaterial(0).
		lua_createtable(state, /*num array elems=*/(int)ob->materials.size(), /*num non-array elems=*/0); // Pushes array table
		for(size_t i=0; i<ob->materials.size(); ++i)
		{
			pushWorldMaterialTable(state, sub_lua_script, uid, /*index=*/i); // Pushes material table
			lua_rawseti(state, /*table index=*/-2, (int)i + 1); // Pops material table, sets array[i + 1] = material table.  Array table is left on top of the stack.
		}
		break;
	}
	case Atom_playAudio:
		assert(stringEqual(key_str, "playAudio"));
		lua_pushcfunction(state, playAudio, "playAudio");
		break;
	case Atom_isPlayingAudio:
		assert(stringEqual(key_str, "isPlayingAudio"));
		lua_pushcfunction(state, isPlayingAudio, "isPlayingAudio");
		break;
	case Atom_playSound:
		assert(stringEqual(key_str, "playSound"));
		lua_pushcfunction(state, playSound, "playSound");
		break;
	case Atom_startPlayingAnimation:
		assert(stringEqual(key_str, "startPlayingAnimation"));
		lua_pushcfunction(state, startPlayingAnimation, "startPlayingAnimation");
		break;
	case Atom_getAnimationIndex:
		assert(stringEqual(key_str, "getAnimationIndex"));
		lua_pushcfunction(state, getAnimationIndex, "getAnimationIndex");
		break;
	case Atom_moveTo:
		assert(stringEqual(key_str, "moveTo"));
		lua_pushcfunction(state, luaMoveTo, "moveTo");
		break;
	case Atom_rotateTo:
		assert(stringEqual(key_str, "rotateTo"));
		lua_pushcfunction(state, luaRotateTo, "rotateTo");
		break;
	case Atom_linear_velocity:
		assert(stringEqual(key_str, "linear_velocity"));
		LuaUtils::pushVec3f(state, Vec3f(ob->linear_vel));
		break;

	default:
		throw glare::Exception("Unknown field '" + std::string(key_str) + "'" + errorContextString(state));
	}

	return 1; // Count of returned values
}


#if SERVER
// Returns true if the string value changed, false otherwise.
template <class StringType>
static bool assignStringWithSizeCheck(lua_State* state, int index, StringType& field, const char* field_name, size_t max_size)
{
	const string_view new_string = LuaUtils::getStringAsStringView(state, index);
	if(new_string.size() > max_size)
		throw glare::Exception("New " + std::string(field_name) + " length too long. (string had length " + toString(new_string.size()) + ", max len is " + toString(max_size) + ")" + errorContextString(state));
	
	if(field != new_string)
	{
		field = new_string;
		return true;
	}
	else
		return false;
}


// Returns true if the value changed, false otherwise.
template <class T>
static bool assignIfChanged(T& field, const T& new_val)
{
	if(field == new_val)
		return false;

	field = new_val;
	return true;
}


// Sets or zeroes the bit.  Returns true if the bit changed, false otherwise.
template <class T>
static bool setOrZeroBitIfChanged(T& flags, T bitflag, bool should_set)
{
	if(BitUtils::isBitSet(flags, bitflag) == should_set)
		return false;

	BitUtils::setOrZeroBit(flags, bitflag, should_set);
	return true;
}
#endif


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

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	if(sub_lua_script->cur_world_state_lock == nullptr)
	{
		assert(0);
		throw glare::Exception("Internal error: cur_world_state_lock was null");
	}

	WorldObject* ob = getWorldObjectForUID(sub_lua_script, uid);

	// Check permissions before we update object.
	// A script has permissions to modify an object if and only if the creator of the script is also the creator of the object.
	if(ob->creator_id != sub_lua_script->world_object->creator_id)
		throw glare::Exception("Script does not have permissions to modify object (ob UID: " + uid.toString() + ")");


	bool transform_changed = false;
	bool other_changed = false;
	bool specific_dirty_flag_set = false;

	int atom = -1;
	const char* key_str = LuaUtils::getStringAndAtom(state, /*index=*/2, atom);
	switch(atom) // NOTE: The switch cases should be in the same order as the Atom enum values to ensure nice code-gen.
	{
	case Atom_model_url:
		assert(stringEqual(key_str, "model_url"));

		if(assignStringWithSizeCheck(state, /*index=*/3, /*field=*/ob->model_url, /*field name=*/"model_url", /*max size=*/WorldObject::MAX_URL_SIZE))
		{
			ob->from_remote_dirty |= WorldObject::MODEL_URL_DIRTY;
			specific_dirty_flag_set = true;
		}

		break;
	case Atom_pos:
		{
		assert(stringEqual(key_str, "pos"));
		transform_changed = assignIfChanged(ob->pos, LuaUtils::getVec3d(state, /*index=*/3));
		break;
		}
	case Atom_axis:
		assert(stringEqual(key_str, "axis"));
		transform_changed = assignIfChanged(ob->axis, LuaUtils::getVec3f(state, /*index=*/3));
		break;
	case Atom_angle:
		assert(stringEqual(key_str, "angle"));
		transform_changed = assignIfChanged(ob->angle, LuaUtils::getFloat(state, /*index=*/3));
		break;
	case Atom_scale:
		assert(stringEqual(key_str, "scale"));
		transform_changed = assignIfChanged(ob->scale, LuaUtils::getVec3f(state, /*index=*/3));
		break;
	case Atom_collidable:
		{
		assert(stringEqual(key_str, "collidable"));
		const bool collidable = LuaUtils::getBool(state, /*index=*/3);
		if(collidable != ob->isCollidable())
		{
			ob->setCollidable(collidable);
			other_changed = true;
		}
		break;
		}
	case Atom_dynamic:
		{
		const bool dynamic = LuaUtils::getBool(state, /*index=*/3);
		if(dynamic != ob->isDynamic())
		{
			ob->setDynamic(dynamic);
			other_changed = true;
		}
		break;
		}
	case Atom_sensor:
		{
		const bool sensor = LuaUtils::getBool(state, /*index=*/3);
		if(sensor != ob->isSensor())
		{
			ob->setIsSensor(sensor);
			other_changed = true;
		}
		break;
		}
	case Atom_content:
		{
		assert(stringEqual(key_str, "content"));

		if(assignStringWithSizeCheck(state, /*index=*/3, /*field=*/ob->content, /*field name=*/"content", /*max size=*/WorldObject::MAX_CONTENT_SIZE))
		{
			ob->from_remote_dirty |= WorldObject::CONTENT_DIRTY;
			specific_dirty_flag_set = true;
		}
		break;
		}
	case Atom_target_url:
		{
		assert(stringEqual(key_str, "target_url"));

		if(assignStringWithSizeCheck(state, /*index=*/3, /*field=*/ob->target_url, /*field name=*/"target_url", /*max size=*/WorldObject::MAX_URL_SIZE))
			other_changed = true;
		break;
		}
	case Atom_video_autoplay:
		assert(stringEqual(key_str, "video_autoplay"));
		other_changed = setOrZeroBitIfChanged(ob->flags, WorldObject::VIDEO_AUTOPLAY, LuaUtils::getBool(state, /*index=*/3));
		break;
	case Atom_video_loop:
		assert(stringEqual(key_str, "video_loop"));
		other_changed = setOrZeroBitIfChanged(ob->flags, WorldObject::VIDEO_LOOP, LuaUtils::getBool(state, /*index=*/3));
		break;
	case Atom_video_muted:
		assert(stringEqual(key_str, "video_muted"));
		other_changed = setOrZeroBitIfChanged(ob->flags, WorldObject::VIDEO_MUTED, LuaUtils::getBool(state, /*index=*/3));
		break;
	case Atom_mass:
		assert(stringEqual(key_str, "mass"));
		other_changed = assignIfChanged(ob->mass, LuaUtils::getFloat(state, /*index=*/3));
		break;
	case Atom_friction:
		assert(stringEqual(key_str, "friction"));
		other_changed = assignIfChanged(ob->friction, LuaUtils::getFloat(state, /*index=*/3));
		break;
	case Atom_restitution:
		assert(stringEqual(key_str, "restitution"));
		other_changed = assignIfChanged(ob->restitution, LuaUtils::getFloat(state, /*index=*/3));
		break;
	case Atom_centre_of_mass_offset_os:
		assert(stringEqual(key_str, "centre_of_mass_offset_os"));
		other_changed = assignIfChanged(ob->centre_of_mass_offset_os, LuaUtils::getVec3f(state, /*index=*/3));
		break;
	case Atom_audio_source_url:
		assert(stringEqual(key_str, "audio_source_url"));

		if(assignStringWithSizeCheck(state, /*index=*/3, /*field=*/ob->audio_source_url, /*field name=*/"audio_source_url", /*max size=*/WorldObject::MAX_URL_SIZE)) // If changed:
			other_changed = true;
		break;
	case Atom_audio_volume:
		assert(stringEqual(key_str, "audio_volume"));
		other_changed = assignIfChanged(ob->audio_volume, LuaUtils::getFloat(state, /*index=*/3));
		break;
	case Atom_audio_loop:
		assert(stringEqual(key_str, "audio_loop"));
		other_changed = setOrZeroBitIfChanged(ob->flags, WorldObject::AUDIO_LOOP, LuaUtils::getBool(state, /*index=*/3));
		break;
	case Atom_materials:
		{
		assert(stringEqual(key_str, "materials"));

		// Build the complete new materials list before replacing ob->materials, as the array may contain Material objects that refer to this object's current materials.
		std::vector<WorldMaterialRef> new_materials;
		getWorldMaterialsFromLuaArray(state, sub_lua_script, /*array index=*/3, new_materials); // Stack unchanged.  Throws if the value is not a table.
		ob->materials = new_materials;
		other_changed = true;

		// The new materials may use textures that don't have basis/KTX versions generated yet.
		sub_lua_script->server->enqueueMsgForLodGenThread(new CheckGenResourcesForObject(ob->uid));
		break;
		}
	case Atom_linear_velocity:
		{
		assert(stringEqual(key_str, "linear_velocity"));
		const Vec3f linear_vel = LuaUtils::getVec3f(state, /*index=*/3);
		if(!linear_vel.isFinite())
			throw glare::Exception("linear_velocity must be finite" + errorContextString(state));
		ob->linear_vel = linear_vel.toVec4fVector();
		ob->from_remote_dirty |= WorldObject::LINEAR_VEL_DIRTY; // ObjectPhysicsTransformUpdate messages won't overwrite linear_vel while this is set.
		specific_dirty_flag_set = true;
		break;
		}
	default:
		throw glare::Exception("Unknown field '" + std::string(key_str) + "'");
	}

	if(transform_changed)
	{
		ob->last_transform_update_avatar_uid = std::numeric_limits<uint32>::max();
		ob->from_remote_dirty |= WorldObject::TRANSFORM_DIRTY; // TODO: rename
	}
	else if(other_changed)
	{
		ob->from_remote_dirty |= WorldObject::OTHER_DIRTY; // TODO: rename
	}

	if(transform_changed || other_changed || specific_dirty_flag_set)
	{
		ServerAllWorldsState* worlds_state = sub_lua_script->server->world_state.ptr();
		checkHoldWorldStateMutex(sub_lua_script, worlds_state);

		// Update in the database, Add to dirty set so changes will be sent to clients
		worlds_state->checkAddWorldObToDBDirtyAndDirtyFromRemoteSets(ob, *sub_lua_script->world_state, *sub_lua_script->cur_world_state_lock);
	}

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

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	WorldObject* ob = getWorldObjectForUID(sub_lua_script, uid);

	if(mat_index >= ob->materials.size())
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

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	WorldObject* ob = getWorldObjectForUID(sub_lua_script, uid);

	// Check permissions before we update object
	if(ob->creator_id != sub_lua_script->world_object->creator_id)
		throw glare::Exception("Script does not have permissions to modifiy object (ob UID: " + uid.toString() + ")" + errorContextString(state));


	if(mat_index >= ob->materials.size())
		throw glare::Exception("Invalid material index" + errorContextString(state));

	WorldMaterial* mat = ob->materials[mat_index].ptr();

	bool changed = false;

	// Read key
	int atom = -1;
	const char* key_str = LuaUtils::getStringAndAtom(state, /*index=*/2, atom);
	switch(atom) // NOTE: The switch cases should be in the same order as the Atom enum values to ensure nice code-gen.
	{
	case Atom_colour:
		assert(stringEqual(key_str, "colour"));
		changed = assignIfChanged(mat->colour_rgb, Colour3f(LuaUtils::getVec3f(state, /*index=*/3)));
		break;
	case Atom_colour_texture_url:
		assert(stringEqual(key_str, "colour_texture_url"));
		changed = assignStringWithSizeCheck(state, /*index=*/3, mat->colour_texture_url, "colour_texture_url", WorldObject::MAX_URL_SIZE);
		break;
	case Atom_emission_rgb:
		assert(stringEqual(key_str, "emission_rgb"));
		changed = assignIfChanged(mat->emission_rgb, Colour3f(LuaUtils::getVec3f(state, /*index=*/3)));
		break;
	case Atom_emission_texture_url:
		assert(stringEqual(key_str, "emission_texture_url"));
		changed = assignStringWithSizeCheck(state, /*index=*/3, mat->emission_texture_url, "emission_texture_url", WorldObject::MAX_URL_SIZE);
		break;
	case Atom_normal_map_url:
		assert(stringEqual(key_str, "normal_map_url"));
		changed = assignStringWithSizeCheck(state, /*index=*/3, mat->normal_map_url, "normal_map_url", WorldObject::MAX_URL_SIZE);
		break;
	case Atom_roughness_val:
		assert(stringEqual(key_str, "roughness_val"));
		changed = assignIfChanged(mat->roughness.val, LuaUtils::getFloat(state, /*index=*/3));
		break;
	case Atom_roughness_texture_url:
		assert(stringEqual(key_str, "roughness_texture_url"));
		changed = assignStringWithSizeCheck(state, /*index=*/3, mat->roughness.texture_url, "roughness_texture_url", WorldObject::MAX_URL_SIZE);
		break;
	case Atom_metallic_fraction_val:
		assert(stringEqual(key_str, "metallic_fraction_val"));
		changed = assignIfChanged(mat->metallic_fraction.val, LuaUtils::getFloat(state, /*index=*/3));
		break;
	case Atom_opacity_val:
		assert(stringEqual(key_str, "opacity_val"));
		changed = assignIfChanged(mat->opacity.val, LuaUtils::getFloat(state, /*index=*/3));
		break;
	case Atom_tex_matrix:
		assert(stringEqual(key_str, "tex_matrix"));
		changed = assignIfChanged(mat->tex_matrix, LuaUtils::getMatrix2f(state, /*index=*/3));
		break;
	case Atom_emission_lum_flux_or_lum:
		assert(stringEqual(key_str, "emission_lum_flux_or_lum"));
		changed = assignIfChanged(mat->emission_lum_flux_or_lum, LuaUtils::getFloat(state, /*index=*/3));
		break;
	case Atom_hologram:
		assert(stringEqual(key_str, "hologram"));
		changed = setOrZeroBitIfChanged(mat->flags, WorldMaterial::HOLOGRAM_FLAG, LuaUtils::getBool(state, /*index=*/3));
		break;
	case Atom_double_sided:
		assert(stringEqual(key_str, "double_sided"));
		changed = setOrZeroBitIfChanged(mat->flags, WorldMaterial::DOUBLE_SIDED_FLAG, LuaUtils::getBool(state, /*index=*/3));
		break;
	default:
		throw glare::Exception("Unknown field '" + std::string(key_str) + "'" + errorContextString(state));
	}

	if(changed)
	{
		// Mark the object as dirty, sending the updated object will send the updated material as well.
		ob->from_remote_dirty |= WorldObject::OTHER_DIRTY; // TODO: rename

		ServerAllWorldsState* worlds_state = sub_lua_script->server->world_state.ptr();
		checkHoldWorldStateMutex(sub_lua_script, worlds_state);

		worlds_state->checkAddWorldObToDBDirtyAndDirtyFromRemoteSets(ob, *sub_lua_script->world_state, *sub_lua_script->cur_world_state_lock);
	}

	return 0; // Count of returned values
#endif
}


// C++ implementation of __index for User class. Used when a User table field is read from.
static int userClassIndexMetaMethod(lua_State* state)
{
	throw glare::Exception("Disabled");
#if 0
	// arg 1 is table
	// arg 2 is the key (field name)

	assert(lua_gettop(state) == 2); // Should be 2 args.
	checkNumArgs(state, 2);

	const UID uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

#if GUI_CLIENT
	auto res = sub_lua_script->gui_client->world_state->avatars.find(uid);
	if(res == sub_lua_script->gui_client->world_state->avatars.end())
		throw glare::Exception("No such avatar with given UID" + errorContextString(state));

	Avatar* avatar = res->second.ptr();

#elif SERVER
	// Get user UID from the user table
	//const UserID uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "id"));

	//auto res = sub_lua_vm->server->world_state->user_id_to_users.find(uid);
	//if(res == sub_lua_vm->server->world_state->user_id_to_users.end())
	//	throw glare::Exception("No such user with given UID");

	//User* user = res->second.ptr();


	if(sub_lua_script->cur_world_state_lock == nullptr)
		throw glare::Exception("Internal error: cur_world_state_lock was null");
	const ServerWorldState::AvatarMapType& avatars = sub_lua_script->world_state->getAvatars(*sub_lua_script->cur_world_state_lock);
	auto res = avatars.find(uid);
	if(res == avatars.end())
		throw glare::Exception("No such avatar with given UID" + errorContextString(state));

	const Avatar* avatar = res.getValue().ptr();
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
#endif
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

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

#if GUI_CLIENT
	WorldState* world_state = sub_lua_script->gui_client->world_state.ptr();
	checkHoldWorldStateMutex(sub_lua_script, world_state);

	auto res = world_state->avatars.find(avatar_uid);
	if(res == world_state->avatars.end())
		throw glare::Exception("No such avatar with given UID" + errorContextString(state));

	Avatar* avatar = res->second.ptr();
#elif SERVER

	if(sub_lua_script->cur_world_state_lock == nullptr)
		throw glare::Exception("Internal error: cur_world_state_lock was null");
	const ServerWorldState::AvatarMapType& avatars = sub_lua_script->world_state->getAvatars(*sub_lua_script->cur_world_state_lock);

	auto res = avatars.find(avatar_uid);
	if(res == avatars.end())
		throw glare::Exception("No such avatar with given UID" + errorContextString(state));

	Avatar* avatar = res.getValue().ptr();
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
	case Atom_linear_velocity:
		assert(stringEqual(key_str, "linear_velocity"));
#if GUI_CLIENT
		LuaUtils::pushVec3f(state, Vec3f(sub_lua_script->gui_client->player_physics.getLinearVel()));
		return 1;
#else // else server:
		LuaUtils::pushVec3f(state, Vec3f(0.f));
		return 1;
#endif
	case Atom_name:
		assert(stringEqual(key_str, "name"));
		LuaUtils::pushString(state, avatar->name);
		return 1;
	case Atom_vehicle_inside: // Returns an Object table of the vehicle object the avatar is inside-of/riding, or nil if none.
	{
		assert(stringEqual(key_str, "vehicle_inside"));
#if GUI_CLIENT
		const WorldObject* vehicle_inside;
		if(avatar_uid == sub_lua_script->gui_client->client_avatar_uid) // If this is our avatar:
			vehicle_inside = sub_lua_script->gui_client->vehicle_controller_inside ? sub_lua_script->gui_client->vehicle_controller_inside->getControlledObject() : nullptr;
		else
			vehicle_inside = avatar->entered_vehicle.ptr();

		if(vehicle_inside)
			sub_lua_script->pushWorldObjectTableOntoStack(state, vehicle_inside->uid);
		else
			lua_pushnil(state);
#else // else server:
		if(avatar->vehicle_inside_uid.valid())
			sub_lua_script->pushWorldObjectTableOntoStack(state, avatar->vehicle_inside_uid);
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
	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);
	WorldState* world_state = sub_lua_script->gui_client->world_state.ptr();
	checkHoldWorldStateMutex(sub_lua_script, world_state);
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
			if(avatar_uid == sub_lua_script->gui_client->client_avatar_uid)
			{
				const Vec3d pos = LuaUtils::getVec3d(state, /*index=*/3);
				sub_lua_script->gui_client->player_physics.setEyePosition(pos);
			}
#endif
			break;
		}
		case Atom_linear_velocity:
		{
			assert(stringEqual(key_str, "linear_velocity"));
#if GUI_CLIENT
			if(avatar_uid == sub_lua_script->gui_client->client_avatar_uid)
			{
				const Vec3f new_linear_vel = LuaUtils::getVec3f(state, /*index=*/3);
				sub_lua_script->gui_client->player_physics.setLinearVel(new_linear_vel.toVec4fVector());
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
	default:
		throw glare::Exception("Avatar class: Unknown field '" + std::string(key_str) + "'" + errorContextString(state));
	}

	return 0; // Count of returned values
}


// "gets called when a string is created to assign an atom id"
static int16_t glareLuaUserAtom(lua_State* L, const char* str, size_t stringlen)
{
	// conPrint("glareLuaUserAtom(): str: " + std::string(str, stringlen));

	for(size_t i=0; i<staticArrayNumElems(string_atoms); ++i)
		if(stringEqual(str, string_atoms[i].str))
			return (int16)string_atoms[i].atom;

	return -1; // or use ATOM_UNDEF here?
}


// NOTE: These values can't change without breaking deserialisation of these serialised objects.
static const uint32 Vec3d_metatable_UID			= 1; // See LuaVM::LuaVM()
static const uint32 Vec2_metatable_UID			= 2; // See LuaVM::LuaVM()
static const uint32 WorldObject_metatable_UID	= 100;
static const uint32 WorldMaterial_metatable_UID	= 101;
static const uint32 User_metatable_UID			= 102;
static const uint32 Avatar_metatable_UID		= 103;


SubstrataLuaVM::SubstrataLuaVM(const SubstrataLuaVMArgs& args)
:	metatable_uid_to_ref_map(std::numeric_limits<uint32>::max()),
#if GUI_CLIENT
	gui_client(args.gui_client),
	player_physics(args.player_physics),
	is_build_vm(args.is_build_vm),
#endif
#if SERVER
	server(args.server),
#endif
	current_period_start_total_num_interrupts(0),
	last_period_interrupts_per_sec(0)
{
	LuaVMOptions vm_options;
#if GUI_CLIENT
	vm_options.c_funcs = args.vm_c_funcs;
#endif

	vm_options.max_total_mem_allowed = 16 * 1024 * 1024;
#if GUI_CLIENT
	if(is_build_vm)
		vm_options.max_total_mem_allowed = 1024 * 1024 * 1024; // one-shot build scripts (see LuaBuildScript) can use more RAM.
#endif

	lua_vm = new LuaVM(vm_options);


	lua_callbacks(lua_vm->state)->useratom = glareLuaUserAtom;


	metatable_uid_to_ref_map.insert(std::make_pair(Vec3d_metatable_UID, lua_vm->Vec3dMetaTable_ref));
	metatable_uid_to_ref_map.insert(std::make_pair(Vec2_metatable_UID,  lua_vm->Vec2MetaTable_ref));

	// Functions that call getSubstrataLuaScript() are only valid for per-object scripts.  A build-script thread's data is a plain
	// LuaScript (with a BuildScriptContext in LuaScript::userdata), so those functions are left unregistered on a build VM.  See LuaBuildScript.
	// Functions registered on both kinds of VM must not call getSubstrataLuaScript().
#if GUI_CLIENT
	const bool register_object_script_funcs = !args.is_build_vm;
#else
	const bool register_object_script_funcs = true;
#endif


	// Set some global functions

	if(register_object_script_funcs)
	{
		lua_pushcfunction(lua_vm->state, createObject, /*debugname=*/"createObject");
		lua_setglobal(lua_vm->state, "createObject"); // Pops a value from the stack and sets it as the new value of global name.

		lua_pushcfunction(lua_vm->state, luaGetWorldObjectForUID, /*debugname=*/"getObjectForUID");
		lua_setglobal(lua_vm->state, "getObjectForUID");

		LuaScriptUIFunctions::registerGlobalFunctions(lua_vm->state);
	
		lua_pushcfunction(lua_vm->state, getCurrentTime, /*debugname=*/"getCurrentTime");
		lua_setglobal(lua_vm->state, "getCurrentTime");

		lua_pushcfunction(lua_vm->state, showMessageToUser, /*debugname=*/"showMessageToUser");
		lua_setglobal(lua_vm->state, "showMessageToUser");

		lua_pushcfunction(lua_vm->state, showMessage, /*debugname=*/"showMessage");
		lua_setglobal(lua_vm->state, "showMessage");

		lua_pushcfunction(lua_vm->state, createTimer, /*debugname=*/"createTimer");
		lua_setglobal(lua_vm->state, "createTimer");
	
		lua_pushcfunction(lua_vm->state, destroyTimer, /*debugname=*/"destroyTimer");
		lua_setglobal(lua_vm->state, "destroyTimer");
	
		lua_pushcfunction(lua_vm->state, luaAddEventListener, /*debugname=*/"addEventListener");
		lua_setglobal(lua_vm->state, "addEventListener");
	
		lua_pushcfunction(lua_vm->state, doHTTPGetRequestAsync, /*debugname=*/"doHTTPGetRequestAsync");
		lua_setglobal(lua_vm->state, "doHTTPGetRequestAsync");
	
		lua_pushcfunction(lua_vm->state, doHTTPPostRequestAsync, /*debugname=*/"doHTTPPostRequestAsync");
		lua_setglobal(lua_vm->state, "doHTTPPostRequestAsync");


		lua_pushcfunction(lua_vm->state, getSecret, /*debugname=*/"getSecret");
		lua_setglobal(lua_vm->state, "getSecret");
	}

	lua_pushcfunction(lua_vm->state, parseJSON, /*debugname=*/"parseJSON");
	lua_setglobal(lua_vm->state, "parseJSON");

	if(register_object_script_funcs)
	{
		lua_createtable(lua_vm->state, /*narr=*/0, /*nrec=*/2);
		lua_vm->setCFunctionAsTableField(objectStorageGetItem, /*debugname=*/"objectStorageGetItem", /*key=*/"getItem");
		lua_vm->setCFunctionAsTableField(objectStorageSetItem, /*debugname=*/"objectStorageSetItem", /*key=*/"setItem");
		lua_setglobal(lua_vm->state, "objectstorage"); // Set table as global name table 'objectstorage'
	}


	//--------------------------- Create metatables for our classes ---------------------------

	//--------------------------- Create WorldObject Metatable ---------------------------
	lua_createtable(lua_vm->state, /*num array elems=*/0, /*num non-array elems=*/2); // Create WorldObject metatable
	
	LuaUtils::setNumberAsTableField(lua_vm->state, "uid", WorldObject_metatable_UID); // Set metatable UID (for serialisation)

	lua_vm->setCFunctionAsTableField(worldObjectClassIndexMetaMethod,    /*debugname=*/"worldObjectClassIndexMetaMethod",    /*key=*/"__index");
	lua_vm->setCFunctionAsTableField(worldObjectClassNewIndexMetaMethod, /*debugname=*/"worldObjectClassNewIndexMetaMethod", /*key=*/"__newindex");

	lua_setreadonly(lua_vm->state, /*index=*/-1, /*enabled=*/1); // Set metatable as read-only.

	worldObjectClassMetaTable_ref = lua_vm->createReferenceToLuaValue(lua_vm->state, /*index=*/-1); // Get reference to WorldObjectMetaTable.  Does not pop.
	lua_pop(lua_vm->state, 1); // Pop WorldObjectMetaTable from stack
	//--------------------------- End create User Metatable ---------------------------


	//--------------------------- Create WorldMaterial Metatable ---------------------------
	lua_createtable(lua_vm->state, /*num array elems=*/0, /*num non-array elems=*/2); // Create WorldMaterial metatable

	LuaUtils::setNumberAsTableField(lua_vm->state, "uid", WorldMaterial_metatable_UID); // Set metatable UID (for serialisation)

	lua_vm->setCFunctionAsTableField(worldMaterialClassIndexMetaMethod,    /*debugname=*/"worldMaterialClassIndexMetaMethod",    /*key=*/"__index");
	lua_vm->setCFunctionAsTableField(worldMaterialClassNewIndexMetaMethod, /*debugname=*/"worldMaterialClassNewIndexMetaMethod", /*key=*/"__newindex");

	lua_setreadonly(lua_vm->state, /*index=*/-1, /*enabled=*/1); // Set metatable as read-only.

	worldMaterialClassMetaTable_ref = lua_vm->createReferenceToLuaValue(lua_vm->state, /*index=*/-1); // Get reference to WorldMaterialMetaTable.  Does not pop.
	lua_pop(lua_vm->state, 1); // Pop WorldMaterialMetaTable from stack
	//--------------------------- End create User Metatable ---------------------------
	

	//--------------------------- Create User Metatable ---------------------------
	lua_createtable(lua_vm->state, /*num array elems=*/0, /*num non-array elems=*/2); // Create User metatable

	LuaUtils::setNumberAsTableField(lua_vm->state, "uid", User_metatable_UID); // Set metatable UID (for serialisation)

	lua_vm->setCFunctionAsTableField(userClassIndexMetaMethod,    /*debugname=*/"userIndexMetaMethod",    /*key=*/"__index");
	lua_vm->setCFunctionAsTableField(userClassNewIndexMetaMethod, /*debugname=*/"userNewIndexMetaMethod", /*key=*/"__newindex");

	lua_setreadonly(lua_vm->state, /*index=*/-1, /*enabled=*/1); // Set metatable as read-only.

	userClassMetaTable_ref = lua_vm->createReferenceToLuaValue(lua_vm->state, /*index=*/-1); // Get reference to UserClassMetaTable.  Does not pop.
	lua_pop(lua_vm->state, 1); // Pop UserClassMetaTable from stack
	//--------------------------- End create User Metatable ---------------------------


	//--------------------------- Create Avatar Metatable ---------------------------
	lua_createtable(lua_vm->state, /*num array elems=*/0, /*num non-array elems=*/2); // Create Avatar metatable

	LuaUtils::setNumberAsTableField(lua_vm->state, "uid", Avatar_metatable_UID); // Set metatable UID (for serialisation)

	lua_vm->setCFunctionAsTableField(avatarClassIndexMetaMethod,    /*debugname=*/"avatarIndexMetaMethod",    /*key=*/"__index");
	lua_vm->setCFunctionAsTableField(avatarClassNewIndexMetaMethod, /*debugname=*/"avatarNewIndexMetaMethod", /*key=*/"__newindex");

	lua_setreadonly(lua_vm->state, /*index=*/-1, /*enabled=*/1); // Set metatable as read-only.

	avatarClassMetaTable_ref = lua_vm->createReferenceToLuaValue(lua_vm->state, /*index=*/-1); // Get reference to AvatarClassMetaTable.  Does not pop.
	lua_pop(lua_vm->state, 1); // Pop AvatarClassMetaTable from stack
	//--------------------------- End create Avatar Metatable ---------------------------


	scriptUIWidgetMetaTable_ref = LuaScriptUIFunctions::createWidgetMetaTable(*lua_vm);

	lua_vm->finishInitAndSandbox();

	// Add items to metatable_uid_to_ref_map for deserialisation
	metatable_uid_to_ref_map.insert(std::make_pair(WorldObject_metatable_UID,   worldObjectClassMetaTable_ref));
	metatable_uid_to_ref_map.insert(std::make_pair(WorldMaterial_metatable_UID, worldMaterialClassMetaTable_ref)); 
	metatable_uid_to_ref_map.insert(std::make_pair(User_metatable_UID,          userClassMetaTable_ref)); 
	metatable_uid_to_ref_map.insert(std::make_pair(Avatar_metatable_UID,        avatarClassMetaTable_ref)); 
}


SubstrataLuaVM::~SubstrataLuaVM()
{
	if(lua_vm)
	{
		lua_vm->destroyReferenceToLuaValue(lua_vm->state, worldObjectClassMetaTable_ref);
		lua_vm->destroyReferenceToLuaValue(lua_vm->state, worldMaterialClassMetaTable_ref);
		lua_vm->destroyReferenceToLuaValue(lua_vm->state, userClassMetaTable_ref);
		lua_vm->destroyReferenceToLuaValue(lua_vm->state, avatarClassMetaTable_ref);
		lua_vm->destroyReferenceToLuaValue(lua_vm->state, scriptUIWidgetMetaTable_ref);
	}
}
