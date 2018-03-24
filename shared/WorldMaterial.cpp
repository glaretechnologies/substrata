/*=====================================================================
WorldMaterial.cpp
---------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#include "WorldMaterial.h"


#include "ResourceManager.h"
#include <Exception.h>
#include <StringUtils.h>
#include <FileUtils.h>
#include <FileChecksum.h>


WorldMaterial::WorldMaterial()
{
	colour_rgb = Colour3f(0.85f);
	roughness = ScalarVal(0.5f);
	metallic_fraction = ScalarVal(0.0f);
	opacity = ScalarVal(1.0f);
	tex_matrix = Matrix2f::identity();
}


WorldMaterial::~WorldMaterial()
{}


void ScalarVal::appendDependencyURLs(std::vector<std::string>& paths_out)
{
	if(!texture_url.empty())
		paths_out.push_back(texture_url);
}


void ScalarVal::convertLocalPathsToURLS(ResourceManager& resource_manager)
{
	if(!this->texture_url.empty() && FileUtils::fileExists(this->texture_url)) // If the URL is a local path:
	{
		this->texture_url = resource_manager.URLForPathAndHash(this->texture_url, FileChecksum::fileChecksum(this->texture_url));
	}
}


void WorldMaterial::appendDependencyURLs(std::vector<std::string>& paths_out)
{
	if(!colour_texture_url.empty())
		paths_out.push_back(colour_texture_url);

	roughness.appendDependencyURLs(paths_out);
	metallic_fraction.appendDependencyURLs(paths_out);
	opacity.appendDependencyURLs(paths_out);
}


void WorldMaterial::convertLocalPathsToURLS(ResourceManager& resource_manager)
{
	if(FileUtils::fileExists(this->colour_texture_url)) // If the URL is a local path:
		this->colour_texture_url = resource_manager.URLForPathAndHash(this->colour_texture_url, FileChecksum::fileChecksum(this->colour_texture_url));

	roughness.convertLocalPathsToURLS(resource_manager);
	metallic_fraction.convertLocalPathsToURLS(resource_manager);
	opacity.convertLocalPathsToURLS(resource_manager);
}


static void writeToStream(OutStream& stream, const Colour3f& col)
{
	stream.writeFloat(col.r);
	stream.writeFloat(col.g);
	stream.writeFloat(col.b);
}


static Colour3f readColour3fFromStram(InStream& stream)
{
	Colour3f col;
	col.r = stream.readFloat();
	col.g = stream.readFloat();
	col.b = stream.readFloat();
	return col;
}


static const uint32 WORLD_MATERIAL_SERIALISATION_VERSION = 4;


void writeToStream(const WorldMaterial& mat, OutStream& stream)
{
	// Write version
	stream.writeUInt32(WORLD_MATERIAL_SERIALISATION_VERSION);

	writeToStream(stream, mat.colour_rgb);
	stream.writeStringLengthFirst(mat.colour_texture_url);

	writeToStream(mat.roughness, stream);
	writeToStream(mat.metallic_fraction, stream);
	writeToStream(mat.opacity, stream);

	writeToStream(mat.tex_matrix, stream);
}


void readFromStream(InStream& stream, WorldMaterial& mat)
{
	// Read version
	const uint32 v = stream.readUInt32();
	if(v > WORLD_MATERIAL_SERIALISATION_VERSION)
		throw Indigo::Exception("Unsupported version " + toString(v) + ", expected " + toString(WORLD_MATERIAL_SERIALISATION_VERSION) + ".");

	if(v == 1)
	{
		const uint32 id = stream.readUInt32();
		switch(id)
		{
		case 200:
		{
			mat.colour_rgb.r = stream.readFloat();
			mat.colour_rgb.g = stream.readFloat();
			mat.colour_rgb.b = stream.readFloat();
			break;
		}
		case 201:
		{
			mat.colour_texture_url = stream.readStringLengthFirst(10000);
			break;
		}
		default:
			throw Indigo::Exception("Invalid spectrum material value.");
		};
	}
	else
	{
		mat.colour_rgb = readColour3fFromStram(stream);
		mat.colour_texture_url = stream.readStringLengthFirst(10000);
	}
	
	if(v <= 2)
	{
		readFromStreamOld(stream, mat.roughness);
		readFromStreamOld(stream, mat.metallic_fraction);
		readFromStreamOld(stream, mat.opacity);
	}
	else
	{
		readFromStream(stream, mat.roughness);
		readFromStream(stream, mat.metallic_fraction);
		readFromStream(stream, mat.opacity);
	}

	if(v >= 4)
		mat.tex_matrix = readMatrix2FromStream<float>(stream);
	else
		mat.tex_matrix = Matrix2f(1, 0, 0, -1); // Needed for existing object objects etc..
}


void writeToStream(const ScalarVal& val, OutStream& stream)
{
	stream.writeFloat(val.val);
	stream.writeStringLengthFirst(val.texture_url);
}


void readFromStreamOld(InStream& stream, ScalarVal& ob)
{
	const uint32 id = stream.readUInt32();
	switch(id)
	{
	case 100:
		{
			ob = ScalarVal(stream.readFloat());
			break;
		}
	case 101:
		{
			ob.val = 1.f;
			ob.texture_url = stream.readStringLengthFirst(10000);
			break;
		}
	default:
		throw Indigo::Exception("Invalid scalar material value.");
	};
}


void readFromStream(InStream& stream, ScalarVal& ob)
{
	ob.val = stream.readFloat();
	ob.texture_url = stream.readStringLengthFirst(10000);
}
