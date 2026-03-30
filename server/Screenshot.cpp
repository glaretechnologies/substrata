/*=====================================================================
Screenshot.cpp
--------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "Screenshot.h"


#include <Exception.h>
#include <StringUtils.h>


Screenshot::Screenshot()
{
	width_px = 800;
	highlight_parcel_id = -1;
	screenshot_type = ScreenshotType_Default;
	tile_x = tile_y = tile_z = 0;
}


Screenshot::~Screenshot()
{}


static const uint32 SCREENSHOT_SERIALISATION_VERSION = 6;
// v2: added width_px
// v3: added highlight_parcel_id
// v4: Added is_map_tile (now screenshot_type), tile_x etc.
// v5: Added URL
// v6: Added gear_item_id

void writeScreenshotToStream(const Screenshot& shot, OutStream& stream)
{
	// Write version
	stream.writeUInt32(SCREENSHOT_SERIALISATION_VERSION);

	stream.writeData(&shot.id, sizeof(shot.id));

	stream.writeData(&shot.cam_pos, sizeof(shot.cam_pos));
	stream.writeData(&shot.cam_angles, sizeof(shot.cam_angles));

	stream.writeInt32(shot.width_px);
	stream.writeInt32(shot.highlight_parcel_id);

	stream.writeUInt32((uint32)shot.screenshot_type);
	stream.writeInt32(shot.tile_x);
	stream.writeInt32(shot.tile_y);
	stream.writeInt32(shot.tile_z);

	shot.created_time.writeToStream(stream);

	stream.writeStringLengthFirst(shot.local_path);

	stream.writeUInt32((uint32)shot.state);

	stream.writeStringLengthFirst(shot.URL);

	writeToStream(shot.gear_item_id, stream);
}


void readScreenshotFromStream(InStream& stream, Screenshot& shot)
{
	// Read version
	const uint32 v = stream.readUInt32();
	if(v > SCREENSHOT_SERIALISATION_VERSION)
		throw glare::Exception("Unsupported version " + toString(v) + ", expected " + toString(SCREENSHOT_SERIALISATION_VERSION) + ".");

	shot.id = stream.readUInt64();

	stream.readData(&shot.cam_pos, sizeof(shot.cam_pos));
	stream.readData(&shot.cam_angles, sizeof(shot.cam_angles));

	if(v >= 2)
		shot.width_px = stream.readInt32();
	if(v >= 3)
		shot.highlight_parcel_id = stream.readInt32();

	if(v >= 4)
	{
		const uint32 screenshot_type = stream.readUInt32();
		if(screenshot_type > (uint32)Screenshot::ScreenshotType_Gear)
			throw glare::Exception("Invalid screenshot_type " + toString(screenshot_type));
		shot.screenshot_type = (Screenshot::ScreenshotType)screenshot_type;
		shot.tile_x = stream.readInt32();
		shot.tile_y = stream.readInt32();
		shot.tile_z = stream.readInt32();
	}

	shot.created_time.readFromStream(stream);

	shot.local_path = stream.readStringLengthFirst(10000);

	const uint32 s = stream.readUInt32();
	if(s > (uint32)Screenshot::ScreenshotState_done)
		throw glare::Exception("Invalid state");

	shot.state = (Screenshot::ScreenshotState)s;

	if(v >= 5)
		shot.URL = stream.readStringLengthFirst(10000);

	if(v >= 6)
		shot.gear_item_id = readUIDFromStream(stream);
}
