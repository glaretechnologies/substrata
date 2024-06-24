/*=====================================================================
ScriptedObjectProximityChecker.cpp
----------------------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "ScriptedObjectProximityChecker.h"


#include "GUIClient.h"
#include "../shared/LuaScriptEvaluator.h"
#include "../shared/MessageUtils.h"
#include "../shared/Protocol.h"


static void enqueueMessageToSend(ClientThread& client_thread, SocketBufferOutStream& packet)
{
	MessageUtils::updatePacketLengthField(packet);

	client_thread.enqueueDataToSend(packet.buf);
}


ScriptedObjectProximityChecker::ScriptedObjectProximityChecker()
:	objects(/*empty_val=*/WorldObjectRef())
{}


ScriptedObjectProximityChecker::~ScriptedObjectProximityChecker()
{}


void ScriptedObjectProximityChecker::clear()
{
	objects.clear();
}


void ScriptedObjectProximityChecker::think(const Vec4f& campos)
{
	//for(auto it = objects.begin(); it != objects.end(); ++it)

	WorldObjectRef* const objects_data = objects.vector.data();
	const size_t objects_size          = objects.vector.size();

	for(size_t i=0; i<objects_size; ++i)
	{
		if(i + 16 < objects_size)
			_mm_prefetch((const char*)(&objects_data[i + 16]->centroid_ws), _MM_HINT_T0);

		WorldObject* ob = objects_data[i].ptr(); // it->ptr();

		const bool cur_in_proximity = ob->in_script_proximity;

		//const bool new_in_proximity = ob->getAABBWS().distanceToPoint(campos) < 20.0f;
		const Vec4f closest_point_in_aabb = ob->getAABBWS().getClosestPointInAABB(campos);
		const float dist2 = campos.getDist2(closest_point_in_aabb);
		const bool new_in_proximity = dist2 < Maths::square(20.f);

		if(new_in_proximity != cur_in_proximity)
		{
			ob->in_script_proximity = new_in_proximity;

			if(new_in_proximity)
			{
				// Execute onUserMovedNearToObject locally
				//assert(ob->lua_script_evaluator.nonNull() && ob->lua_script_evaluator->isOnUserMovedNearToObjectDefined());
				if(ob->lua_script_evaluator.nonNull() && ob->lua_script_evaluator->isOnUserMovedNearToObjectDefined())
				{
					ob->lua_script_evaluator->doOnUserMovedNearToObject(gui_client->logged_in_user_id);
				}

				// Send message to server to execute on server as well
				MessageUtils::initPacket(gui_client->scratch_packet, Protocol::UserMovedNearToObjectMessage);
				writeToStream(ob->uid, gui_client->scratch_packet);
				enqueueMessageToSend(*gui_client->client_thread, gui_client->scratch_packet);
			}
			else
			{
				// Execute onUserMovedAwayFromObject locally
				//assert(ob->lua_script_evaluator.nonNull() && ob->lua_script_evaluator->isOnUserMovedAwayFromObjectDefined());
				if(ob->lua_script_evaluator.nonNull() && ob->lua_script_evaluator->isOnUserMovedAwayFromObjectDefined())
				{
					ob->lua_script_evaluator->doOnUserMovedAwayFromObject(gui_client->logged_in_user_id);
				}

				// Send message to server to execute on server as well
				MessageUtils::initPacket(gui_client->scratch_packet, Protocol::UserMovedAwayFromObjectMessage);
				writeToStream(ob->uid, gui_client->scratch_packet);
				enqueueMessageToSend(*gui_client->client_thread, gui_client->scratch_packet);

			}
		}
	}
}
