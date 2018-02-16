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
	colour_rgb = Colour3f(0.7f);
	roughness = new ConstantScalarVal(0.5f);
	metallic_fraction = new ConstantScalarVal(0.0f);
	opacity = new ConstantScalarVal(1.0f);
}


WorldMaterial::~WorldMaterial()
{

}


void WorldMaterial::appendDependencyURLs(std::vector<std::string>& paths_out)
{
	if(!colour_texture_url.empty())
		paths_out.push_back(colour_texture_url);

	roughness->appendDependencyURLs(paths_out);
	metallic_fraction->appendDependencyURLs(paths_out);
	opacity->appendDependencyURLs(paths_out);
}


void WorldMaterial::convertLocalPathsToURLS(ResourceManager& resource_manager)
{
	if(FileUtils::fileExists(this->colour_texture_url)) // If the URL is a local path:
		this->colour_texture_url = resource_manager.URLForPathAndHash(this->colour_texture_url, FileChecksum::fileChecksum(this->colour_texture_url));

	roughness->convertLocalPathsToURLS(resource_manager);
	metallic_fraction->convertLocalPathsToURLS(resource_manager);
	opacity->convertLocalPathsToURLS(resource_manager);
}


void ConstantScalarVal::writeToStream(OutStream& stream)
{
	stream.writeUInt32(100);
	stream.writeFloat(this->val);
}


void TextureScalarVal::writeToStream(OutStream& stream)
{
	stream.writeUInt32(101);
	stream.writeStringLengthFirst(this->texture_url);
}


void TextureScalarVal::appendDependencyURLs(std::vector<std::string>& paths_out)
{
	paths_out.push_back(this->texture_url);
}


void TextureScalarVal::convertLocalPathsToURLS(ResourceManager& resource_manager)
{
	if(FileUtils::fileExists(this->texture_url)) // If the URL is a local path:
	{
		this->texture_url = resource_manager.URLForPathAndHash(this->texture_url, FileChecksum::fileChecksum(this->texture_url));
	}
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


static const uint32 WORLD_MATERIAL_SERIALISATION_VERSION = 2;


void writeToStream(const WorldMaterial& mat, OutStream& stream)
{
	// Write version
	stream.writeUInt32(WORLD_MATERIAL_SERIALISATION_VERSION);

	writeToStream(stream, mat.colour_rgb);
	stream.writeStringLengthFirst(mat.colour_texture_url);

	writeToStream(mat.roughness, stream);
	writeToStream(mat.metallic_fraction, stream);
	writeToStream(mat.opacity, stream);
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
	readFromStream(stream, mat.roughness);
	readFromStream(stream, mat.metallic_fraction);
	readFromStream(stream, mat.opacity);
}


void writeToStream(const ScalarValRef& val, OutStream& stream)
{
	val->writeToStream(stream);
}


void readFromStream(InStream& stream, ScalarValRef& ob)
{
	const uint32 id = stream.readUInt32();
	switch(id)
	{
	case 100:
		{
			ob = new ConstantScalarVal(stream.readFloat());
			break;
		}
	case 101:
		{
			ob = new TextureScalarVal(stream.readStringLengthFirst(10000));
			break;
		}
	default:
		throw Indigo::Exception("Invalid scalar material value.");
	};
}
