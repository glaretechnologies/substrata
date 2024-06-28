/*=====================================================================
ScriptedObjectProximityChecker.cpp
----------------------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "ScriptedObjectProximityChecker.h"


#include "GUIClient.h"
#include "../shared/LuaScriptEvaluator.h"
#include "../shared/ObjectEventHandlers.h"
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


// Iterate over objects, see if the player has moved near to or away from any objects, and execute the relevant event handlers if so.
void ScriptedObjectProximityChecker::think(const Vec4f& campos)
{
	WorldObjectRef* const objects_data = objects.vector.data();
	const size_t objects_size          = objects.vector.size();

	for(size_t i=0; i<objects_size; ++i)
	{
		if(i + 16 < objects_size)
			_mm_prefetch((const char*)(&objects_data[i + 16]->centroid_ws), _MM_HINT_T0);

		WorldObject* const ob = objects_data[i].ptr();

		const bool cur_in_proximity = ob->in_script_proximity;

		const Vec4f closest_point_in_aabb = ob->getAABBWS().getClosestPointInAABB(campos);
		const float dist2 = campos.getDist2(closest_point_in_aabb);
		const bool new_in_proximity = dist2 < Maths::square(20.f);

		if(new_in_proximity != cur_in_proximity)
		{
			ob->in_script_proximity = new_in_proximity;

			if(new_in_proximity)
			{
				// Execute onUserMovedNearToObject locally
				if((ob->lua_script_evaluator && ob->lua_script_evaluator->isOnUserMovedNearToObjectDefined()) || (ob->event_handlers && ob->event_handlers->onUserMovedNearToObject_handlers.nonEmpty()))
				{
					if(ob->lua_script_evaluator)
						ob->lua_script_evaluator->doOnUserMovedNearToObject(ob->lua_script_evaluator->onUserMovedNearToObject_ref, gui_client->client_avatar_uid, ob->uid);

					// Execute any event handlers also
					if(ob->event_handlers)
						ob->event_handlers->executeOnUserMovedNearToObjectHandlers(/*avatar_uid=*/gui_client->client_avatar_uid, ob->uid);

					// Send message to server to execute on server as well
					MessageUtils::initPacket(gui_client->scratch_packet, Protocol::UserMovedNearToObjectMessage);
					writeToStream(ob->uid, gui_client->scratch_packet);
					enqueueMessageToSend(*gui_client->client_thread, gui_client->scratch_packet);
				}
			}
			else
			{
				// Execute onUserMovedAwayFromObject locally
				if((ob->lua_script_evaluator && ob->lua_script_evaluator->isOnUserMovedAwayFromObjectDefined()) || (ob->event_handlers && ob->event_handlers->onUserMovedAwayFromObject_handlers.nonEmpty()))
				{
					if(ob->lua_script_evaluator)
						ob->lua_script_evaluator->doOnUserMovedAwayFromObject(ob->lua_script_evaluator->onUserMovedAwayFromObject_ref, gui_client->client_avatar_uid, ob->uid);

					// Execute any event handlers also
					if(ob->event_handlers)
						ob->event_handlers->executeOnUserMovedAwayFromObjectHandlers(/*avatar_uid=*/gui_client->client_avatar_uid, ob->uid);

					// Send message to server to execute on server as well
					MessageUtils::initPacket(gui_client->scratch_packet, Protocol::UserMovedAwayFromObjectMessage);
					writeToStream(ob->uid, gui_client->scratch_packet);
					enqueueMessageToSend(*gui_client->client_thread, gui_client->scratch_packet);
				}
			}
		}
	}
}
