/*=====================================================================
AvatarGraphics.h
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-12 12:24:54 +1300
=====================================================================*/
#pragma once


#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include "../shared/UID.h"
#include "vec3.h"
#include "Matrix4f.h"
#include <string>
#include <vector>
struct GLObject;
class OpenGLEngine;


/*=====================================================================
AvatarGraphics
--------------

=====================================================================*/
class AvatarGraphics : public ThreadSafeRefCounted
{
public:
	AvatarGraphics();
	~AvatarGraphics();

	void setOverallTransform(OpenGLEngine& engine, const Vec3d& pos, const Vec3f& rotation, double cur_time);

	void create(OpenGLEngine& engine);

	void destroy(OpenGLEngine& engine);
	

	Reference<GLObject> upper_arms[2];
	Reference<GLObject> lower_arms[2];

	Reference<GLObject> upper_legs[2];
	Reference<GLObject> lower_legs[2];

	Reference<GLObject> chest;
	Reference<GLObject> pelvis;

	Reference<GLObject> head;

	Reference<GLObject> feet[2];

private:
	void setWalkAnimation(OpenGLEngine& engine, const Vec3d& pos, const Vec3f& rotation, double cur_time);
	void setStandAnimation(OpenGLEngine& engine, const Vec3d& pos, const Vec3f& rotation, double cur_time);

	Vec3d last_pos;
};


typedef Reference<AvatarGraphics> AvatarGraphicsRef;
