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
const uint32 AvatarFullUpdate		= 1003;

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

	void getInterpolatedTransform(double cur_time, Vec3d& pos_out, Vec3f& axis_out, float& angle_out) const;
	void setTransformAndHistory(const Vec3d& pos, const Vec3f& axis, float angle);

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
	bool transform_dirty;
	bool other_dirty;

	bool using_placeholder_model;

	Reference<GLObject> opengl_engine_ob;

	/*
		Snapshots for client-side interpolation purposes.
		next_i = index to write next snapshot in.
		pos_snapshots[next_i - 1] is the last received update, received at time last_snapshot_time.
		pos_snapshots[next_i - 2] is the update received before that, will be considerd to be received at last_snapshot_time - update_send_period.
	*/
	static const int HISTORY_BUF_SIZE = 4;
	Vec3d pos_snapshots[HISTORY_BUF_SIZE];
	Vec3f axis_snapshots[HISTORY_BUF_SIZE];
	float angle_snapshots[HISTORY_BUF_SIZE];
	double last_snapshot_time;
	uint32 next_snapshot_i;
private:

};


typedef Reference<Avatar> AvatarRef;


void writeToNetworkStream(const Avatar& world_ob, OutStream& stream); // Write without version.  Writes UID.
void readFromNetworkStreamGivenUID(InStream& stream, Avatar& ob); // UID will have been read already
