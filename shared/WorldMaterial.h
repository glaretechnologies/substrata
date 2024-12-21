/*=====================================================================
WorldMaterial.h
---------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "DependencyURL.h"
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
class RandomAccessInStream;
class RandomAccessOutStream;
namespace pugi { class xml_node; }
namespace glare { class Allocator; }


struct ScalarVal
{
	ScalarVal() : val(0.0f) {}
	explicit ScalarVal(const float v) : val(v) {}

	void appendDependencyURLs(bool tex_use_sRGB, int material_min_lod_level, int lod_level, std::vector<DependencyURL>& paths_out) const;
	void appendDependencyURLsAllLODLevels(bool tex_use_sRGB, int material_min_lod_level, std::vector<DependencyURL>& paths_out) const;
	void appendDependencyURLsBaseLevel(bool tex_use_sRGB, std::vector<DependencyURL>& paths_out) const;

	void convertLocalPathsToURLS(ResourceManager& resource_manager);

	bool operator == (const ScalarVal& b) const
	{
		return 
			val == b.val &&
			texture_url == b.texture_url;
	}

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

	// NOTE: If adding new member variables, make sure to add to clone() and operator ==() below.

	Colour3f colour_rgb; // Non-linear sRGB
	std::string colour_texture_url;

	Colour3f emission_rgb; // Non-linear sRGB
	std::string emission_texture_url;

	std::string normal_map_url;

	ScalarVal roughness; // Metallic-roughness texture URL will be stored in roughness.texture_url.
	ScalarVal metallic_fraction;
	ScalarVal opacity;

	Matrix2f tex_matrix;

	float emission_lum_flux_or_lum; // For spotlights, luminous flux.  For generic model materials, luminance.

	static const uint32 COLOUR_TEX_HAS_ALPHA_FLAG   = 1; // Does the texture referenced by colour_texture_url have an alpha channel?
	// Used to determine the file format of LOD level textures, e.g. will be a PNG if this flag is set.
	static const uint32 MIN_LOD_LEVEL_IS_NEGATIVE_1 = 2;
	static const uint32 HOLOGRAM_FLAG               = 4; // E.g. just emission, no light scattering.
	static const uint32 USE_VERT_COLOURS_FOR_WIND   = 8;
	static const uint32 DOUBLE_SIDED_FLAG           = 16;
	static const uint32 DECAL_FLAG                  = 32;

	uint32 flags;

	
	inline bool colourTexHasAlpha() const { return BitUtils::isBitSet(flags, COLOUR_TEX_HAS_ALPHA_FLAG); }

	inline int minLODLevel() const { return BitUtils::isBitSet(flags, MIN_LOD_LEVEL_IS_NEGATIVE_1) ? -1 : 0; }

	inline bool isDecal() const { return BitUtils::isBitSet(flags, DECAL_FLAG); }


	Reference<WorldMaterial> clone() const
	{
		Reference<WorldMaterial> m = new WorldMaterial();
		m->name = name;
		m->colour_rgb = colour_rgb;
		m->colour_texture_url = colour_texture_url;
		m->emission_rgb = emission_rgb;
		m->emission_texture_url = emission_texture_url;
		m->normal_map_url = normal_map_url;
		m->roughness = roughness;
		m->metallic_fraction = metallic_fraction;
		m->opacity = opacity;
		m->tex_matrix = tex_matrix;
		m->emission_lum_flux_or_lum = emission_lum_flux_or_lum;
		m->flags = flags;
		return m;
	}

	bool operator == (const WorldMaterial& b) const
	{
		return
			name == b.name &&
			colour_rgb == b.colour_rgb &&
			colour_texture_url == b.colour_texture_url &&
			emission_rgb == b.emission_rgb &&
			emission_texture_url == b.emission_texture_url &&
			normal_map_url == b.normal_map_url &&
			roughness == b.roughness &&
			metallic_fraction == b.metallic_fraction &&
			opacity == b.opacity &&
			tex_matrix == b.tex_matrix &&
			emission_lum_flux_or_lum == b.emission_lum_flux_or_lum &&
			flags == b.flags;
	}

	std::string getLODTextureURLForLevel(const std::string& base_texture_url, int level, bool has_alpha) const;

	void appendDependencyURLs(int lod_level, std::vector<DependencyURL>& paths_out) const;
	void appendDependencyURLsAllLODLevels(std::vector<DependencyURL>& paths_out) const;
	void appendDependencyURLsBaseLevel(std::vector<DependencyURL>& paths_out) const;

	void convertLocalPathsToURLS(ResourceManager& resource_manager);

	static Reference<WorldMaterial> loadFromXMLElem(const std::string& mat_file_path, bool convert_rel_paths_to_abs_disk_paths, pugi::xml_node elem); // mat_file_path is used for converting relative paths to absolute.
	static Reference<WorldMaterial> loadFromXMLOnDisk(const std::string& path, bool convert_rel_paths_to_abs_disk_paths);
	

	std::string serialiseToXML(int tab_depth) const;
	void writeToXMLOnDisk(const std::string& path) const;
	

	static void test();
private:
};

typedef Reference<WorldMaterial> WorldMaterialRef;


// WorldMaterial serialisation
void writeWorldMaterialToStream(const WorldMaterial& world_ob, RandomAccessOutStream& stream);
void readWorldMaterialFromStream(RandomAccessInStream& stream, WorldMaterial& ob);

// ScalarVal serialisation
void writeScalarValToStream(const ScalarVal& val, OutStream& stream);
void readScalarValFromStreamOld(InStream& stream, ScalarVal& ob);
void readScalarValFromStream(InStream& stream, ScalarVal& ob);
