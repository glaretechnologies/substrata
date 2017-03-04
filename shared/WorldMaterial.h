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
#include <string>
#include <vector>
struct GLObject;
class PhysicsObject;
class ResourceManager;


struct ScalarVal : public ThreadSafeRefCounted
{
	enum ScalarValType
	{
		ScalarValType_Constant,
		ScalarValType_Texture
	};

	ScalarVal(ScalarValType t) : type(t) {}
	virtual void writeToStream(OutStream& stream) = 0;
	virtual void appendDependencyURLs(std::vector<std::string>& paths_out) = 0;
	virtual void convertLocalPathsToURLS(ResourceManager& resource_manager) = 0;

	ScalarValType type;
};
typedef Reference<ScalarVal> ScalarValRef;

struct ConstantScalarVal : public ScalarVal
{
	ConstantScalarVal() : ScalarVal(ScalarValType_Constant) {}
	ConstantScalarVal(float v) : ScalarVal(ScalarValType_Constant), val(v) {}
	virtual void writeToStream(OutStream& stream);
	virtual void appendDependencyURLs(std::vector<std::string>& paths_out) {}
	virtual void convertLocalPathsToURLS(ResourceManager& resource_manager) {}

	float val;
};

struct TextureScalarVal : public ScalarVal
{
	TextureScalarVal() : ScalarVal(ScalarValType_Texture) {}
	TextureScalarVal(const std::string& texture_url_) : ScalarVal(ScalarValType_Texture), texture_url(texture_url_) {}
	virtual void writeToStream(OutStream& stream);
	virtual void appendDependencyURLs(std::vector<std::string>& paths_out);
	virtual void convertLocalPathsToURLS(ResourceManager& resource_manager);
	std::string texture_url;
};

struct SpectrumVal : public ThreadSafeRefCounted
{
	enum SpectrumValType
	{
		SpectrumValType_Constant,
		SpectrumValType_Texture
	};

	SpectrumVal(SpectrumValType t) : type(t) {}
	virtual void writeToStream(OutStream& stream) = 0;
	virtual void appendDependencyURLs(std::vector<std::string>& paths_out) = 0;
	virtual void convertLocalPathsToURLS(ResourceManager& resource_manager) = 0;

	SpectrumValType type;
};
typedef Reference<SpectrumVal> SpectrumValRef;

struct ConstantSpectrumVal : public SpectrumVal
{
	ConstantSpectrumVal(const Colour3f& rgb_) : SpectrumVal(SpectrumValType_Constant), rgb(rgb_) {}
	ConstantSpectrumVal() : SpectrumVal(SpectrumValType_Constant) {}
	virtual void writeToStream(OutStream& stream);
	virtual void appendDependencyURLs(std::vector<std::string>& paths_out) {}
	virtual void convertLocalPathsToURLS(ResourceManager& resource_manager) {}
	Colour3f rgb;
};

struct TextureSpectrumVal : public SpectrumVal
{
	TextureSpectrumVal() : SpectrumVal(SpectrumValType_Texture) {}
	TextureSpectrumVal(const std::string& texture_url_) : SpectrumVal(SpectrumValType_Texture), texture_url(texture_url_) {}
	virtual void writeToStream(OutStream& stream);
	virtual void appendDependencyURLs(std::vector<std::string>& paths_out);
	virtual void convertLocalPathsToURLS(ResourceManager& resource_manager);
	std::string texture_url;
};


/*=====================================================================
WorldMaterial
-----------

=====================================================================*/
class WorldMaterial : public ThreadSafeRefCounted
{
public:
	WorldMaterial();
	~WorldMaterial();

	Reference<SpectrumVal> colour;
	Reference<ScalarVal> roughness;
	Reference<ScalarVal> metallic_fraction;
	Reference<ScalarVal> opacity;

	Reference<WorldMaterial> clone() const
	{
		Reference<WorldMaterial> m = new WorldMaterial();
		m->colour = colour;
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

void writeToStream(const ScalarValRef& val, OutStream& stream);
void readFromStream(InStream& stream, ScalarValRef& ob);

void writeToStream(const SpectrumValRef& val, OutStream& stream);
void readFromStream(InStream& stream, SpectrumValRef& ob);
	