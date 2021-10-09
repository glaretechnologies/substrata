/*=====================================================================
WorldMaterial.h
---------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include <BitUtils.h>
#include <graphics/colour3.h>
#include "../shared/UID.h"
#include "vec3.h"
#include <maths/Matrix2.h>
#include <string>
#include <vector>
struct GLObject;
class PhysicsObject;
class ResourceManager;


struct ScalarVal
{
	ScalarVal() : val(0.0f) {}
	ScalarVal(const float v) : val(v) {}

	void appendDependencyURLs(std::vector<std::string>& paths_out);
	void convertLocalPathsToURLS(ResourceManager& resource_manager);

	float val;
	std::string texture_url;
};


/*=====================================================================
WorldMaterial
-------------

=====================================================================*/
class WorldMaterial : public ThreadSafeRefCounted
{
public:
	WorldMaterial();
	~WorldMaterial();

	std::string name; // Not serialised currently.

	Colour3f colour_rgb;
	std::string colour_texture_url;

	ScalarVal roughness;
	ScalarVal metallic_fraction;
	ScalarVal opacity;

	Matrix2f tex_matrix;

	float emission_lum_flux;

	static const uint32 COLOUR_TEX_HAS_ALPHA_FLAG   = 1; // Does the texture referenced by colour_texture_url have an alpha channel?
	// Used to determine the file format of LOD level textures, e.g. will be a PNG if this flag is set.
	static const uint32 MIN_LOD_LEVEL_IS_NEGATIVE_1 = 2;

	uint32 flags;

	// NOTE: If adding new member variables, make sure to add to clone() below.
	

	inline bool colourTexHasAlpha() const { return BitUtils::isBitSet(flags, COLOUR_TEX_HAS_ALPHA_FLAG); }

	inline int minLODLevel() const { return BitUtils::isBitSet(flags, MIN_LOD_LEVEL_IS_NEGATIVE_1) ? -1 : 0; }


	Reference<WorldMaterial> clone() const
	{
		Reference<WorldMaterial> m = new WorldMaterial();
		m->name = name;
		m->colour_rgb = colour_rgb;
		m->colour_texture_url = colour_texture_url;
		m->roughness = roughness;
		m->metallic_fraction = metallic_fraction;
		m->opacity = opacity;
		m->tex_matrix = tex_matrix;
		m->emission_lum_flux = emission_lum_flux;
		m->flags = flags;
		return m;
	}

	std::string getLODTextureURLForLevel(const std::string& base_texture_url, int level, bool has_alpha) const;

	void appendDependencyURLs(int lod_level, std::vector<std::string>& paths_out);

	void appendDependencyURLsAllLODLevels(std::vector<std::string>& paths_out);
	
	void convertLocalPathsToURLS(ResourceManager& resource_manager);

	static Reference<WorldMaterial> loadFromXMLOnDisk(const std::string& path);

private:
};

typedef Reference<WorldMaterial> WorldMaterialRef;


void writeToStream(const WorldMaterial& world_ob, OutStream& stream);
void readFromStream(InStream& stream, WorldMaterial& ob);

void writeToStream(const ScalarVal& val, OutStream& stream);
void readFromStreamOld(InStream& stream, ScalarVal& ob);
void readFromStream(InStream& stream, ScalarVal& ob);
