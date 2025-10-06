/*=====================================================================
WorldSettings.h
---------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "DependencyURL.h"
#include "TimeStamp.h"
#include <vec3.h>
#include <vec2.h>
#include <OutStream.h>
#include <InStream.h>
#include <DatabaseKey.h>
#include <string>
#include <vector>
#include <set>


struct TerrainSpecSection
{
	int x, y; // section coordinates.  (0,0) is section centered on world origin.

	URLString heightmap_URL;
	URLString mask_map_URL;
	URLString tree_mask_map_URL;
};

struct TerrainSpec
{
	std::vector<TerrainSpecSection> section_specs;

	URLString detail_col_map_URLs[4];
	URLString detail_height_map_URLs[4];

	float terrain_section_width_m;
	float water_z;
	float default_terrain_z;

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

	// NOTE: not setting use_sRGB etc. in DependencyURLs, as this getDependencyURLSet method is just used on the server currently, which doesn't need those.
	void getDependencyURLSet(std::set<DependencyURL>& URLs_out);

	void writeToStream(OutStream& stream) const;

	void copyNetworkStateFrom(const WorldSettings& other);

	TerrainSpec terrain_spec;

	DatabaseKey database_key;
	bool db_dirty; // If true, there is a change that has not been saved to the DB.

private:
	GLARE_DISABLE_COPY(WorldSettings)
};


void readWorldSettingsFromStream(InStream& stream, WorldSettings& settings);
