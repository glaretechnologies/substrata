/*=====================================================================
DynamicTextureUpdaterThread.cpp
-------------------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "DynamicTextureUpdaterThread.h"


#include "Server.h"
#include "ServerWorldState.h"
#include "ServerSideScripting.h"
#include "MeshLODGenThread.h"
#include "../shared/ImageDecoding.h"
#include <ConPrint.h>
#include <Exception.h>
#include <Lock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <Timer.h>
#include <TaskManager.h>
#include <FileUtils.h>
#include <IncludeXXHash.h>
#include <HTTPClient.h>
#include <KillThreadMessage.h>
#include <graphics/ImageMap.h>


DynamicTextureUpdaterThread::DynamicTextureUpdaterThread(Server* server_, ServerAllWorldsState* world_state_)
:	server(server_), world_state(world_state_)
{
}


DynamicTextureUpdaterThread::~DynamicTextureUpdaterThread()
{
}


struct ObWithDynamicTexture
{
	std::string world_name;
	UID ob_uid;
	Reference<ServerSideScripting::ServerSideScript> script;
};


// See if object has a server-side script, if so, add info about it to obs_with_dyn_textures_out.
static void checkForDynamicTextureToCheck(const std::string& world_name, WorldObject* ob, ServerAllWorldsState* world_state, std::vector<ObWithDynamicTexture>& obs_with_dyn_textures_out) REQUIRES(world_state->mutex)
{
	if(!ob->script.empty())
	{
		try
		{
			Reference<ServerSideScripting::ServerSideScript> script = ServerSideScripting::parseXMLScript(ob->script);
			if(script.nonNull())
			{
				// Look up user who created the object, to check ALLOW_DYN_TEX_UPDATE_CHECKING flag on the user
				auto user_res = world_state->user_id_to_users.find(ob->creator_id);
				if(user_res != world_state->user_id_to_users.end())
				{
					const User* user = user_res->second.ptr();
					if(BitUtils::isBitSet(user->flags, User::ALLOW_DYN_TEX_UPDATE_CHECKING))
					{
						obs_with_dyn_textures_out.push_back({world_name, ob->uid, script});
					}
					else
					{
						conPrint("\tDynamicTextureUpdaterThread: User '" + user->name + "' must have ALLOW_DYN_TEX_UPDATE_CHECKING flag set to allow checking for dynamic textures.");
					}
				}
			}
		}
		catch(glare::Exception& e)
		{
			conPrint("\tDynamicTextureUpdaterThread: Excep while parsing XML script: " + e.what());
		}
	}
}


static std::string sanitiseString(const std::string& s)
{
	std::string res = s;
	for(size_t i=0; i<s.size(); ++i)
		if(!::isAlphaNumeric(s[i]))
			res[i] = '_';
	return res;
}


struct DynTextureFetchResults
{
	URLString substrata_URL; // Set to empty string if exception occurred or download failed.
};


// Returns substrata URL of resource for the downloaded file.
static URLString fetchFileForURLAndAddAsResource(const std::string& base_URL, ServerAllWorldsState* world_state)
{
	HTTPClient http_client;
	http_client.setAsNotIndependentlyHeapAllocated();
	http_client.max_data_size			= 32 * 1024 * 1024; // 32 MB
	http_client.max_socket_buffer_size	= 32 * 1024 * 1024; // 32 MB

	std::vector<uint8> data;
	HTTPClient::ResponseInfo response = http_client.downloadFile(base_URL, data);

	if(response.response_code >= 200 && response.response_code < 300)
	{
		conPrint("\tDynamicTextureUpdaterThread: Got HTTP " + toString(response.response_code) + " response, file size: " + ::getNiceByteSize(data.size()));

		// If original URL didn't have a file extension in it, pick one based on MIME type
		std::string use_extension = sanitiseString(::getExtension(base_URL));
		if(use_extension.empty())
		{
			// Work out extension to use - see https://developer.mozilla.org/en-US/docs/Web/HTTP/Basics_of_HTTP/MIME_types
			if(response.mime_type == "image/gif")
				use_extension = "gif";
			else if(response.mime_type == "image/jpeg")
				use_extension = "jpg";
			else if(response.mime_type == "image/png")
				use_extension = "png";
			else
				throw glare::Exception("Unknown MIME type for image or unsupported MIME type: '" + response.mime_type + "'");
		}

		if(!ImageDecoding::isSupportedImageExtension(use_extension))
			throw glare::Exception("Image type extension not supported: '" + use_extension + "'.");

		if(!ImageDecoding::areMagicBytesValid(data.data(), data.size(), use_extension))
			throw glare::Exception("Image magic bytes are not valid for extension '" + use_extension + "'.");

		const uint64 hash = XXH64(data.data(), data.size(), /*seed=*/1);

		const URLString URL = ResourceManager::URLForNameAndExtensionAndHash(::removeDotAndExtension(base_URL), use_extension, hash);

		conPrint("\tDynamicTextureUpdaterThread: current/new URL: " + toStdString(URL) + "");

		if(URL.size() > WorldObject::MAX_URL_SIZE)
			throw glare::Exception("URL too long.");

		{
			Lock lock(world_state->mutex);

			if(!world_state->resource_manager->isFileForURLPresent(URL))
			{
				conPrint("\tDynamicTextureUpdaterThread: Resource not already present, adding to resource_manager...");

				const std::string local_abs_path = world_state->resource_manager->pathForURL(URL);

				FileUtils::writeEntireFile(local_abs_path, data);

				world_state->resource_manager->setResourceAsLocallyPresentForURL(URL);

				ResourceRef resource = world_state->resource_manager->getExistingResourceForURL(URL);
				world_state->addResourceAsDBDirty(resource);
			}
			else
			{
				conPrint("\tDynamicTextureUpdaterThread: texture is already present as a resource.");
			}
		} // End lock scope

		return URL;
	}
	else
		throw glare::Exception("Non 200 HTTP return code: " + toString(response.response_code) + ", msg: '" + response.response_message + "'"); 

}


static void checkDynamicTexture(const ObWithDynamicTexture& ob_with_dyn_tex, ServerAllWorldsState* world_state, Server* server, std::map<std::string, DynTextureFetchResults>& fetch_results_map)
{
	const std::string base_URL = ob_with_dyn_tex.script->base_image_URL;

	const auto fetch_it = fetch_results_map.find(base_URL);
	if(fetch_it == fetch_results_map.end())
	{
		conPrint("\tDynamicTextureUpdaterThread: Requesting file at URL '" + ob_with_dyn_tex.script->base_image_URL + "'...");

		try
		{
			const URLString substrata_URL = fetchFileForURLAndAddAsResource(base_URL, world_state);

			fetch_results_map[base_URL] = DynTextureFetchResults({substrata_URL});
		}
		catch(glare::Exception& e)
		{
			conPrint("\tDynamicTextureUpdaterThread: Excep fetching URL '" + base_URL + "': " + e.what());
			fetch_results_map[base_URL] = DynTextureFetchResults({""});
		}
	}

	assert(fetch_results_map.count(base_URL) > 0);
	DynTextureFetchResults fetch_results = fetch_results_map[base_URL];

	if(fetch_results.substrata_URL.empty())
	{
		// We already tried to fetch from this URL, and it failed.
		conPrint("\tDynamicTextureUpdaterThread: Fetch for URL '" + base_URL + "' failed, skipping");
	}
	else
	{
		{
			WorldStateLock lock(world_state->mutex);

			const URLString substrata_URL = fetch_results.substrata_URL;

			// Update object to use new texture
			const auto ob_res = world_state->world_states[ob_with_dyn_tex.world_name]->getObjects(lock).find(ob_with_dyn_tex.ob_uid);
			if(ob_res != world_state->world_states[ob_with_dyn_tex.world_name]->getObjects(lock).end())
			{
				WorldObject* ob = ob_res->second.ptr();

				if(ob_with_dyn_tex.script->material_index < ob->materials.size())
				{
					WorldMaterial* material = ob->materials[ob_with_dyn_tex.script->material_index].ptr();

					bool tex_URL_changed = false;
					if(ob_with_dyn_tex.script->material_texture == "colour")
					{
						if(substrata_URL != material->colour_texture_url) // If new URL is different from existing texture URL:
						{
							material->colour_texture_url = substrata_URL;
							tex_URL_changed = true;
						}
					}
					else if(ob_with_dyn_tex.script->material_texture == "emission")
					{
						if(substrata_URL != material->emission_texture_url) // If new URL is different from existing texture URL:
						{
							material->emission_texture_url = substrata_URL;
							tex_URL_changed = true;
						}
					}
					else
						throw glare::Exception("Invalid material_texture type");

					if(tex_URL_changed) // If new URL is different from existing texture URL:
					{
						conPrint("\tDynamicTextureUpdaterThread: Texture is different from existing texture, updating object...");

						world_state->world_states[ob_with_dyn_tex.world_name]->addWorldObjectAsDBDirty(ob, lock);
						world_state->markAsChanged();

						ob->from_remote_other_dirty = true; // Set this so a ObjectFullUpdate message is sent to clients.
						world_state->world_states[ob_with_dyn_tex.world_name]->getDirtyFromRemoteObjects(lock).insert(ob);

						// Send a message to MeshLODGenThread to generate LOD textures for this new texture (if not already generated)
						CheckGenResourcesForObject* msg = new CheckGenResourcesForObject();
						msg->ob_uid = ob_with_dyn_tex.ob_uid;
						server->enqueueMsgForLodGenThread(msg);
					}
					else
						conPrint("\tDynamicTextureUpdaterThread: Texture is the same as existing texture on object.");
				}
			}
		} // End lock scope
	}
}


void DynamicTextureUpdaterThread::doRun()
{
	PlatformUtils::setCurrentThreadName("DynamicTextureUpdaterThread");

	try
	{
		while(1)
		{
			Timer time_since_last_scan;

			//-------------------------------------------  Wait until we have a kill message, or N seconds have elapsed, or the force-update flag is set ------------------------------------------- 
			while(time_since_last_scan.elapsed() < 3600.0)
			{
				// Block for a while, or until we have a message
				ThreadMessageRef msg;
				const bool got_msg = getMessageQueue().dequeueWithTimeout(/*wait_time_seconds=*/4.0, msg);
				if(got_msg)
				{
					if(dynamic_cast<KillThreadMessage*>(msg.ptr()))
						return;
				}

				// Check if the force-update flag is set (can be set in admin web interface).  If so, abort wait.
				{
					Lock lock(world_state->mutex);
					if(world_state->force_dyn_tex_update)
					{
						world_state->force_dyn_tex_update = false;
						break;
					}
				}
			}

			//-------------------------------------------  Iterate over objects, get list of objects using dynamic textures -------------------------------------------
			conPrint("DynamicTextureUpdaterThread: Iterating over world object(s)...");
			Timer timer;
			std::vector<ObWithDynamicTexture> obs_with_dyn_textures;

			{
				WorldStateLock lock(world_state->mutex);

				for(auto world_it = world_state->world_states.begin(); world_it != world_state->world_states.end(); ++world_it)
				{
					ServerWorldState* world = world_it->second.ptr();
					ServerWorldState::ObjectMapType& objects = world->getObjects(lock);
					for(auto it = objects.begin(); it != objects.end(); ++it)
					{
						WorldObject* ob = it->second.ptr();
						try
						{
							checkForDynamicTextureToCheck(/*world name=*/world_it->first, ob, world_state, obs_with_dyn_textures);
						}
						catch(glare::Exception& e)
						{
							conPrint("\tDynamicTextureUpdaterThread: exception while processing object: " + e.what());
						}
					}
				}
			} // End lock scope

			conPrint("DynamicTextureUpdaterThread: Iterating over objects took " + timer.elapsedStringNSigFigs(4) + ", obs_with_dyn_textures: " + toString(obs_with_dyn_textures.size()));
			//----------------------------------------------------------------------------------------------------------------------------------------------------

			//-------------------------------------------  Check each dynamic texture, without holding the world lock -------------------------------------------
			conPrint("DynamicTextureUpdaterThread: Checking for image updates...");
			timer.reset();

			std::map<std::string, DynTextureFetchResults> fetch_results_map;

			for(size_t i=0; i<obs_with_dyn_textures.size(); ++i)
			{
				const ObWithDynamicTexture& ob_with_dyn_tex = obs_with_dyn_textures[i];
				try
				{
					conPrint("DynamicTextureUpdaterThread: Checking dynamic texture for URL '" + ob_with_dyn_tex.script->base_image_URL + "'");

					checkDynamicTexture(ob_with_dyn_tex, world_state, server, fetch_results_map);
				}
				catch(glare::Exception& e)
				{
					conPrint("\tDynamicTextureUpdaterThread: glare::Exception while checking dynamic texture changes: " + e.what());
				}
			}
			conPrint("DynamicTextureUpdaterThread: Done checking for image updates textures. (Elapsed: " + timer.elapsedStringNSigFigs(4) + ")");
			//----------------------------------------------------------------------------------------------------------------------------------------------------
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("DynamicTextureUpdaterThread: glare::Exception: " + e.what());
	}
	catch(std::exception& e) // catch std::bad_alloc etc..
	{
		conPrint(std::string("DynamicTextureUpdaterThread: Caught std::exception: ") + e.what());
	}
}
