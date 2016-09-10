/*=====================================================================
WorldObject.cpp
-------------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#include "WorldObject.h"


#include <Exception.h>
#include <StringUtils.h>
#include "opengl/OpenGLEngine.h"
#include "../gui_client/PhysicsObject.h"


WorldObject::WorldObject()
{
	from_remote_dirty = false;
	from_local_dirty = false;
	opengl_engine_ob = NULL;
	physics_object = NULL;
	using_placeholder_model = false;
}


WorldObject::~WorldObject()
{

}


static const uint32 WORLD_OBJECT_SERIALISATION_VERSION = 1;


void writeToStream(const WorldObject& world_ob, OutStream& stream)
{
	// Write version
	stream.writeUInt32(WORLD_OBJECT_SERIALISATION_VERSION);

	writeToStream(world_ob.uid, stream);
	stream.writeStringLengthFirst(world_ob.model_url);
	writeToStream(world_ob.pos, stream);
	writeToStream(world_ob.axis, stream);
	stream.writeFloat(world_ob.angle);
}


void readFromStream(InStream& stream, WorldObject& ob)
{
	// Read version
	const uint32 v = stream.readUInt32();
	if(v != WORLD_OBJECT_SERIALISATION_VERSION)
		throw Indigo::Exception("Unknown version " + toString(v) + ", expected " + toString(WORLD_OBJECT_SERIALISATION_VERSION) + ".");

	ob.uid = readUIDFromStream(stream);
	ob.model_url = stream.readStringLengthFirst();
	ob.pos = readVec3FromStream<double>(stream);
	ob.axis = readVec3FromStream<float>(stream);
	ob.angle = stream.readFloat();


	// Set ephemeral state
	ob.state = WorldObject::State_Alive;
}
