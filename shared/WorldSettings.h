/*=====================================================================
WorldSettings.h
---------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "TimeStamp.h"
#include <vec3.h>
#include <vec2.h>
#include <OutStream.h>
#include <InStream.h>
#include <DatabaseKey.h>
#include <string>
#include <vector>


struct TerrainSpecSection
{
	int x, y; // section coordinates.  (0,0) is section centered on world origin.

	std::string heightmap_URL;
	std::string mask_map_URL;
};

struct TerrainSpec
{
	std::vector<TerrainSpecSection> section_specs;

	std::string detail_col_map_URLs[4];
	std::string detail_height_map_URLs[4];

	float terrain_section_width_m;
	float water_z;

	static const uint32 WATER_ENABLED_FLAG = 1;
	uint32 flags;
};


/*=====================================================================
WorldSettings
-------------

=====================================================================*/
class WorldSettings
{
public:
	WorldSettings();
	~WorldSettings();

	GLARE_ALIGNED_16_NEW_DELETE

	void writeToStream(OutStream& stream) const;


	TerrainSpec terrain_spec;

	DatabaseKey database_key;
	bool db_dirty; // If true, there is a change that has not been saved to the DB.
};


void readWorldSettingsFromStream(InStream& stream, WorldSettings& settings);
