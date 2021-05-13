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


struct AnimEvents
{
	AnimEvents() : footstrike(false) {}
	bool footstrike;
	Vec3d footstrike_pos;
};

/*=====================================================================
AvatarGraphics
--------------

=====================================================================*/
class AvatarGraphics : public ThreadSafeRefCounted
{
public:
	AvatarGraphics();
	~AvatarGraphics();

	void setOverallTransform(OpenGLEngine& engine, const Vec3d& pos, const Vec3f& rotation, double cur_time, AnimEvents& anim_events_out);

	void create(OpenGLEngine& engine);

	void destroy(OpenGLEngine& engine);
	
	void setSelectedObBeam(OpenGLEngine& engine, const Vec3d& target_pos); // create or update beam
	void hideSelectedObBeam(OpenGLEngine& engine);


	struct BodyPart
	{
		Matrix4f base_transform; // Transform that scales and rotates cylinder to avatar space.
		Reference<GLObject> gl_ob;
	};

	BodyPart upper_arms[2];
	BodyPart lower_arms[2];

	BodyPart upper_legs[2];
	BodyPart lower_legs[2];

	BodyPart chest;
	BodyPart pelvis;

	BodyPart head;

	BodyPart feet[2];


	Reference<GLObject> selected_ob_beam;

	

private:
	void setWalkAnimation(OpenGLEngine& engine, const Vec3d& pos, const Vec3f& rotation, double cur_time, AnimEvents& anim_events_out);
	void setStandAnimation(OpenGLEngine& engine, const Vec3d& pos, const Vec3f& rotation, double cur_time);

	Vec3d last_pos;
	Vec3d last_hand_pos;
	Vec3d last_selected_ob_target_pos;
};


typedef Reference<AvatarGraphics> AvatarGraphicsRef;
