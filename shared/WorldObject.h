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


//TEMP HACK move elsewhere
const uint32 ObjectCreated			= 3000;
const uint32 ObjectDestroyed		= 3001;
const uint32 ObjectTransformUpdate	= 3002;


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
		State_JustCreated,
		State_Alive,
		State_Dead
	};

	State state;
	bool dirty;


	Reference<GLObject> opengl_engine_ob;
	Reference<PhysicsObject> physics_object;
private:

};


typedef Reference<WorldObject> WorldObjectRef;
