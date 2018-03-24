/*=====================================================================
WorldMaterial.h
---------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include <ThreadSafeRefCounted.h>
#include <Reference.h>
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

	Colour3f colour_rgb;
	std::string colour_texture_url;

	ScalarVal roughness;
	ScalarVal metallic_fraction;
	ScalarVal opacity;

	Matrix2f tex_matrix;

	Reference<WorldMaterial> clone() const
	{
		Reference<WorldMaterial> m = new WorldMaterial();
		m->colour_rgb = colour_rgb;
		m->colour_texture_url = colour_texture_url;
		m->roughness = roughness;
		m->metallic_fraction = metallic_fraction;
		m->opacity = opacity;
		return m;
	}

	void appendDependencyURLs(std::vector<std::string>& paths_out);
	
	void convertLocalPathsToURLS(ResourceManager& resource_manager);

private:
};

typedef Reference<WorldMaterial> WorldMaterialRef;


void writeToStream(const WorldMaterial& world_ob, OutStream& stream);
void readFromStream(InStream& stream, WorldMaterial& ob);

void writeToStream(const ScalarVal& val, OutStream& stream);
void readFromStreamOld(InStream& stream, ScalarVal& ob);
void readFromStream(InStream& stream, ScalarVal& ob);
