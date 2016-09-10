/*=====================================================================
WorldObject.h
-------------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include "../shared/UID.h"
#include "vec3.h"
#include <string>
struct GLObject;
class PhysicsObject;


const uint32 ConnectionTypeUpdates	= 500;
const uint32 ConnectionTypeUploadResource	= 501;
const uint32 ConnectionTypeDownloadResources	= 502;



//TEMP HACK move elsewhere
const uint32 ObjectCreated			= 3000;
const uint32 ObjectDestroyed		= 3001;
const uint32 ObjectTransformUpdate	= 3002;


//TEMP HACK move elsewhere
const uint32 GetFile				= 4000;



//TEMP HACK move elsewhere
const uint32 UploadResource			= 5000;


/*=====================================================================
Object
-------------------

=====================================================================*/
class WorldObject : public ThreadSafeRefCounted
{
public:
	WorldObject();
	~WorldObject();

	UID uid;
	//std::string name;
	std::string model_url;
	Vec3d pos;
	Vec3f axis;
	float angle;


	enum State
	{
		State_JustCreated = 0,
		State_Alive,
		State_Dead
	};

	State state;
	bool from_remote_dirty;
	bool from_local_dirty;

	bool using_placeholder_model;


	Reference<GLObject> opengl_engine_ob;
	Reference<PhysicsObject> physics_object;
private:

};


typedef Reference<WorldObject> WorldObjectRef;


void writeToStream(const WorldObject& world_ob, OutStream& stream);
void readFromStream(InStream& stream, WorldObject& ob);

