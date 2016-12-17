/*=====================================================================
Avatar.h
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-12 12:24:54 +1300
=====================================================================*/
#pragma once


#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include "../shared/UID.h"
#include "vec3.h"
#include <string>
struct GLObject;


const uint32 AvatarCreated			= 1000;
const uint32 AvatarDestroyed		= 1001;
const uint32 AvatarTransformUpdate	= 1002;

const uint32 ChatMessageID = 2000;//TEMP HACK move elsewhere


/*=====================================================================
Avatar
-------------------

=====================================================================*/
class Avatar : public ThreadSafeRefCounted
{
public:
	Avatar();
	~Avatar();

	UID uid;
	std::string name;
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

	bool using_placeholder_model;

	Reference<GLObject> opengl_engine_ob;
private:

};


typedef Reference<Avatar> AvatarRef;
