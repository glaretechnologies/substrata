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
#include "Matrix4f.h"
#include <string>
#include <vector>
struct GLObject;
class AvatarGraphics;


/*=====================================================================
Avatar
-------------------

=====================================================================*/
class Avatar : public ThreadSafeRefCounted
{
public:
	Avatar();
	~Avatar();

	//GLARE_ALIGNED_16_NEW_DELETE

	void appendDependencyURLs(std::vector<std::string>& URLs_out);

	void getInterpolatedTransform(double cur_time, Vec3d& pos_out, Vec3f& rotation_out) const;
	void setTransformAndHistory(const Vec3d& pos, const Vec3f& rotation);

	UID uid;
	std::string name;
	std::string model_url;
	Vec3d pos;
	Vec3f rotation;

	UID selected_object_uid; // Will be set to invalidUID if no object selected.

	enum State
	{
		State_JustCreated,
		State_Alive,
		State_Dead
	};

	State state;
	bool transform_dirty;
	bool other_dirty;

	//bool using_placeholder_model;

	//Reference<GLObject> opengl_engine_ob;
#if GUI_CLIENT
	Reference<GLObject> opengl_engine_nametag_ob;

	Reference<AvatarGraphics> graphics;
#endif

	/*
		Snapshots for client-side interpolation purposes.
		next_i = index to write next snapshot in.
		pos_snapshots[next_i - 1] is the last received update, received at time last_snapshot_time.
		pos_snapshots[next_i - 2] is the update received before that, will be considerd to be received at last_snapshot_time - update_send_period.
	*/
	static const int HISTORY_BUF_SIZE = 4;
	Vec3d pos_snapshots[HISTORY_BUF_SIZE];
	Vec3f rotation_snapshots[HISTORY_BUF_SIZE];
	double snapshot_times[HISTORY_BUF_SIZE]; // Time as measured by Clock::getTimeSinceInit().
	//double last_snapshot_time;
	uint32 next_snapshot_i;
private:

};


typedef Reference<Avatar> AvatarRef;


void writeToNetworkStream(const Avatar& world_ob, OutStream& stream); // Write without version.  Writes UID.
void readFromNetworkStreamGivenUID(InStream& stream, Avatar& ob); // UID will have been read already
