/*=====================================================================
Avatar.cpp
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-12 12:24:54 +1300
=====================================================================*/
#include "Avatar.h"


#include "opengl/OpenGLEngine.h"
#include <utils/ConPrint.h>
#include <utils/StringUtils.h>


Avatar::Avatar()
{
	transform_dirty = false;
	other_dirty = false;
	opengl_engine_ob = NULL;
	using_placeholder_model = false;

	next_snapshot_i = 0;
	last_snapshot_time = 0;
}


Avatar::~Avatar()
{

}


void Avatar::setTransformAndHistory(const Vec3d& pos_, const Vec3f& axis_, float angle_)
{
	pos = pos_;
	axis = axis_;
	angle = angle_;

	for(int i=0; i<HISTORY_BUF_SIZE; ++i)
	{
		pos_snapshots[i] = pos_;
		axis_snapshots[i] = axis_;
		angle_snapshots[i] = angle_;
	}
}


void Avatar::getInterpolatedTransform(double cur_time, Vec3d& pos_out, Vec3f& axis_out, float& angle_out) const
{
	/*
	Timeline: check marks are snapshots received:

	|---------------|----------------|---------------|----------------|
	                                                                       ^
	                                                                      cur_time
	                                                                  ^
	                                               ^                last snapshot
	                                             cur_time - send_period * delay_factor

	*/

	const double send_period = 0.1; // Time between update messages from server
	const double delay = /*send_period * */2.0; // Objects are rendered using the interpolated state at this past time.

	const int last_snapshot_i = next_snapshot_i - 1;

	const double frac = (cur_time - last_snapshot_time) / send_period; // Fraction of send period ahead of last_snapshot cur time is
	//printVar(frac);
	const double delayed_state_pos = (double)last_snapshot_i + frac - delay; // Delayed state position in normalised period coordinates.
	const int delayed_state_begin_snapshot_i = myClamp(Maths::floorToInt(delayed_state_pos), last_snapshot_i - HISTORY_BUF_SIZE + 1, last_snapshot_i);
	const int delayed_state_end_snapshot_i   = myClamp(delayed_state_begin_snapshot_i + 1,   last_snapshot_i - HISTORY_BUF_SIZE + 1, last_snapshot_i);
	const float t  = delayed_state_pos - delayed_state_begin_snapshot_i; // Interpolation fraction

	const int begin = Maths::intMod(delayed_state_begin_snapshot_i, HISTORY_BUF_SIZE);
	const int end   = Maths::intMod(delayed_state_end_snapshot_i,   HISTORY_BUF_SIZE);

	pos_out   = Maths::uncheckedLerp(pos_snapshots  [begin], pos_snapshots  [end], t);
	axis_out  = Maths::uncheckedLerp(axis_snapshots [begin], axis_snapshots [end], t);
	angle_out = Maths::uncheckedLerp(angle_snapshots[begin], angle_snapshots[end], t);

	if(axis_out.length2() < 1.0e-10f)
	{
		axis_out = Vec3f(0,0,1);
		angle_out = 0;
	}
}



void writeToNetworkStream(const Avatar& avatar, OutStream& stream) // Write without version
{
	writeToStream(avatar.uid, stream);
	stream.writeStringLengthFirst(avatar.name);
	stream.writeStringLengthFirst(avatar.model_url);
	writeToStream(avatar.pos, stream);
	writeToStream(avatar.axis, stream);
	stream.writeFloat(avatar.angle);
}


void readFromNetworkStreamGivenUID(InStream& stream, Avatar& avatar) // UID will have been read already
{
	avatar.name			= stream.readStringLengthFirst(10000);
	avatar.model_url	= stream.readStringLengthFirst(10000);
	avatar.pos			= readVec3FromStream<double>(stream);
	avatar.axis			= readVec3FromStream<float>(stream);
	avatar.angle		= stream.readFloat();
}
