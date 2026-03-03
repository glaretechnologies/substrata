/*=====================================================================
WorldSettings.cpp
-----------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "WorldSettings.h"


#include <Exception.h>
#include <StringUtils.h>
#include <ConPrint.h>
#include <BufferOutStream.h>
#include <BufferInStream.h>
#include <RuntimeCheck.h>


WorldSettings::WorldSettings()
{
	terrain_spec.terrain_section_width_m = 8192;
	terrain_spec.terrain_height_scale = 1.f;
	terrain_spec.water_z = -4;
	terrain_spec.default_terrain_z = 0;
	terrain_spec.flags = 0;

	sun_phi = 1.f;
	sun_theta = Maths::pi<float>() / 4;

	db_dirty = false;
}


WorldSettings::~WorldSettings()
{
}


void WorldSettings::clear()
{
	terrain_spec = TerrainSpec();
}


void WorldSettings::getDependencyURLSet(std::set<DependencyURL>& URLs_out)
{
	for(size_t i=0; i<terrain_spec.section_specs.size(); ++i)
	{
		const TerrainSpecSection& section_spec = terrain_spec.section_specs[i];

		if(!section_spec.heightmap_URL.empty())
			URLs_out.insert(DependencyURL(section_spec.heightmap_URL));
		if(!section_spec.mask_map_URL.empty())
			URLs_out.insert(DependencyURL(section_spec.mask_map_URL));
		if(!section_spec.tree_mask_map_URL.empty())
			URLs_out.insert(DependencyURL(section_spec.tree_mask_map_URL));
	}

	for(int i=0; i<4; ++i)
		if(!terrain_spec.detail_col_map_URLs[i].empty())
			URLs_out.insert(DependencyURL(terrain_spec.detail_col_map_URLs[i]));

	for(int i=0; i<4; ++i)
		if(!terrain_spec.detail_height_map_URLs[i].empty())
			URLs_out.insert(DependencyURL(terrain_spec.detail_height_map_URLs[i]));
}


static const uint32 WORLDSETTINGS_SERIALISATION_VERSION = 5;


void WorldSettings::writeToStream(OutStream& stream) const
{
	BufferOutStream buffer;
	buffer.buf.reserve(4096);

	buffer.writeUInt32(WORLDSETTINGS_SERIALISATION_VERSION);
	buffer.writeUInt32(0); // Size of buffer will be written here later
	
	buffer.writeUInt32((uint32)terrain_spec.section_specs.size());
	for(size_t i=0; i<terrain_spec.section_specs.size(); ++i)
	{
		const TerrainSpecSection& section_spec = terrain_spec.section_specs[i];

		buffer.writeInt32(section_spec.x);
		buffer.writeInt32(section_spec.y);
		buffer.writeStringLengthFirst(section_spec.heightmap_URL);
		buffer.writeStringLengthFirst(section_spec.mask_map_URL);
	}

	for(int i=0; i<4; ++i)
		buffer.writeStringLengthFirst(terrain_spec.detail_col_map_URLs[i]);
	for(int i=0; i<4; ++i)
		buffer.writeStringLengthFirst(terrain_spec.detail_height_map_URLs[i]);

	buffer.writeFloat(terrain_spec.terrain_section_width_m);
	buffer.writeFloat(terrain_spec.water_z);
	buffer.writeUInt32(terrain_spec.flags);

	buffer.writeFloat(terrain_spec.default_terrain_z); // New in v2

	// New in v3: Write tree_mask_map_URLs
	for(size_t i=0; i<terrain_spec.section_specs.size(); ++i)
	{
		const TerrainSpecSection& section_spec = terrain_spec.section_specs[i];
		buffer.writeStringLengthFirst(section_spec.tree_mask_map_URL);
	}

	buffer.writeFloat(sun_theta);
	buffer.writeFloat(sun_phi);

	buffer.writeFloat(terrain_spec.terrain_height_scale); // New in v5

	// Go back and write size of buffer to buffer size field
	const uint32 buffer_size = (uint32)buffer.buf.size();
	std::memcpy(buffer.buf.data() + sizeof(uint32), &buffer_size, sizeof(uint32));

	// Write buffer to actual output stream
	stream.writeData(buffer.buf.data(), buffer.buf.size());
}


void WorldSettings::copyNetworkStateFrom(const WorldSettings& other)
{
	terrain_spec = other.terrain_spec;

	sun_theta = other.sun_theta;
	sun_phi   = other.sun_phi;
}


void readWorldSettingsFromStream(InStream& stream_, WorldSettings& settings)
{
	const uint32 version = stream_.readUInt32();
	const uint32 buffer_size = stream_.readUInt32();

	checkProperty(buffer_size >= 8ul, "WorldSettings readFromStream: buffer_size was too small");
	checkProperty(buffer_size <= 131072ul, "WorldSettings readFromStream: buffer_size was too large");

	// Read rest of data to buffer
	const uint32 remaining_buffer_size = buffer_size - sizeof(uint32)*2;

	BufferInStream buffer_stream;
	buffer_stream.buf.resize(remaining_buffer_size);

	stream_.readData(buffer_stream.buf.data(), remaining_buffer_size);

	// Read terrain spec sections
	const uint32 num_section_specs = buffer_stream.readUInt32();
	checkProperty(num_section_specs <= 4096, "num_section_specs was too large");

	settings.terrain_spec.section_specs.resize(num_section_specs);
	for(uint32 i=0; i<num_section_specs; ++i)
	{
		TerrainSpecSection& section_spec = settings.terrain_spec.section_specs[i];

		section_spec.x = buffer_stream.readInt32();
		section_spec.y = buffer_stream.readInt32();
		section_spec.heightmap_URL = buffer_stream.readStringLengthFirst(/*max_string_length=*/1024);
		section_spec.mask_map_URL  = buffer_stream.readStringLengthFirst(/*max_string_length=*/1024);
	}

	for(int i=0; i<4; ++i)
		settings.terrain_spec.detail_col_map_URLs[i]    = buffer_stream.readStringLengthFirst(/*max_string_length=*/1024);
	for(int i=0; i<4; ++i)
		settings.terrain_spec.detail_height_map_URLs[i] = buffer_stream.readStringLengthFirst(/*max_string_length=*/1024);

	settings.terrain_spec.terrain_section_width_m = buffer_stream.readFloat();
	settings.terrain_spec.water_z = buffer_stream.readFloat();
	settings.terrain_spec.flags = buffer_stream.readUInt32();

	if(version >= 2)
		settings.terrain_spec.default_terrain_z = buffer_stream.readFloat();

	if(version >= 3)
	{
		// Read tree_mask_map_URLs
		for(uint32 i=0; i<num_section_specs; ++i)
		{
			TerrainSpecSection& section_spec = settings.terrain_spec.section_specs[i];
			section_spec.tree_mask_map_URL = buffer_stream.readStringLengthFirst(/*max_string_length=*/1024);
		}
	}

	if(version >= 4)
	{
		settings.sun_theta = buffer_stream.readFloat();
		settings.sun_phi   = buffer_stream.readFloat();
	}

	if(version >= 5)
		settings.terrain_spec.terrain_height_scale = buffer_stream.readFloat();

	// We effectively skip any remaining data we have not processed by discarding buffer_stream.
}
