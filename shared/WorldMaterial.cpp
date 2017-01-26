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
	colour = new ConstantSpectrumVal(Colour3f(0.5f));
	roughness = new ConstantScalarVal(0.5f);
	metallic_fraction = new ConstantScalarVal(0.0f);
	opacity = new ConstantScalarVal(1.0f);
}


WorldMaterial::~WorldMaterial()
{

}


void WorldMaterial::appendDependencyURLs(std::vector<std::string>& paths_out)
{
	colour->appendDependencyURLs(paths_out);
	roughness->appendDependencyURLs(paths_out);
	metallic_fraction->appendDependencyURLs(paths_out);
	opacity->appendDependencyURLs(paths_out);
}


void WorldMaterial::convertLocalPathsToURLS(ResourceManager& resource_manager)
{
	colour->convertLocalPathsToURLS(resource_manager);
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


void ConstantSpectrumVal::writeToStream(OutStream& stream)
{
	stream.writeUInt32(200);
	stream.writeFloat(this->rgb.r);
	stream.writeFloat(this->rgb.g);
	stream.writeFloat(this->rgb.b);
}


void TextureSpectrumVal::writeToStream(OutStream& stream)
{
	stream.writeUInt32(201);
	stream.writeStringLengthFirst(this->texture_url);
}


void TextureSpectrumVal::convertLocalPathsToURLS(ResourceManager& resource_manager)
{
	if(FileUtils::fileExists(this->texture_url)) // If the URL is a local path:
	{
		this->texture_url = resource_manager.URLForPathAndHash(this->texture_url, FileChecksum::fileChecksum(this->texture_url));
	}
}


void TextureSpectrumVal::appendDependencyURLs(std::vector<std::string>& paths_out)
{
	paths_out.push_back(this->texture_url);
}


static const uint32 WORLD_MATERIAL_SERIALISATION_VERSION = 1;


void writeToStream(const WorldMaterial& mat, OutStream& stream)
{
	// Write version
	stream.writeUInt32(WORLD_MATERIAL_SERIALISATION_VERSION);

	writeToStream(mat.colour, stream);
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

	readFromStream(stream, mat.colour);
	readFromStream(stream, mat.roughness);
	readFromStream(stream, mat.metallic_fraction);
	readFromStream(stream, mat.opacity);
}


void writeToStream(const ScalarValRef& val, OutStream& stream)
{
	val->writeToStream(stream);
}


void writeToStream(const SpectrumValRef& val, OutStream& stream)
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


void readFromStream(InStream& stream, SpectrumValRef& ob)
{
	const uint32 id = stream.readUInt32();
	switch(id)
	{
	case 200:
		{
			Colour3f col;
			col.r = stream.readFloat();
			col.g = stream.readFloat();
			col.b = stream.readFloat();
			ob = new ConstantSpectrumVal(col);
			break;
		}
	case 201:
		{
			ob = new TextureSpectrumVal(stream.readStringLengthFirst(10000));
			break;
		}
	default:
		throw Indigo::Exception("Invalid spectrum material value.");
	};
}
