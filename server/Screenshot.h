/*=====================================================================
Screenshot.h
------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "../shared/TimeStamp.h"
#include "../shared/UserID.h"
#include "../shared/ParcelID.h"
#include "../maths/vec3.h"
#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include <OutStream.h>
#include <InStream.h>
#include <DatabaseKey.h>


/*=====================================================================
Screenshot
----------

=====================================================================*/
class Screenshot : public ThreadSafeRefCounted
{
public:
	Screenshot();
	~Screenshot();

	uint64 id;

	Vec3d cam_pos;
	Vec3d cam_angles;
	int width_px;
	int highlight_parcel_id;
	
	bool is_map_tile;
	int tile_x, tile_y, tile_z;
	
	TimeStamp created_time;

	std::string local_path; // Path on disk of saved screenshot

	enum ScreenshotState
	{
		ScreenshotState_notdone = 0, // The screenshot has been requested, but has not been taken and stored on disk yet, or it has not been requested
		ScreenshotState_done = 1 // The screenshot has been taken by the screenshot bot and stored on disk.
	};

	ScreenshotState state;

	DatabaseKey database_key;
};


typedef Reference<Screenshot> ScreenshotRef;


void writeToStream(const Screenshot& s, OutStream& stream);
void readFromStream(InStream& stream, Screenshot& s);


struct ScreenshotRefHash
{
	size_t operator() (const ScreenshotRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};
