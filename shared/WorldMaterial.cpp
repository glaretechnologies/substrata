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
#include <IndigoXMLDoc.h>
#include <XMLParseUtils.h>
#include <Parser.h>


WorldMaterial::WorldMaterial()
{
	colour_rgb = Colour3f(0.85f);
	roughness = ScalarVal(0.5f);
	metallic_fraction = ScalarVal(0.0f);
	opacity = ScalarVal(1.0f);
	tex_matrix = Matrix2f::identity();
	emission_lum_flux = 0;
	flags = 0;
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


void WorldMaterial::appendDependencyURLs(int lod_level, std::vector<std::string>& paths_out)
{
	if(!colour_texture_url.empty())
		paths_out.push_back(WorldObject::getLODTextureURLForLevel(colour_texture_url, lod_level, this->colourTexHasAlpha()));

	roughness.appendDependencyURLs(paths_out);
	metallic_fraction.appendDependencyURLs(paths_out);
	opacity.appendDependencyURLs(paths_out);
}


void WorldMaterial::appendDependencyURLsAllLODLevels(std::vector<std::string>& paths_out)
{
	if(!colour_texture_url.empty())
	{
		paths_out.push_back(colour_texture_url);

		paths_out.push_back(WorldObject::getLODTextureURLForLevel(colour_texture_url, 1, this->colourTexHasAlpha()));
		paths_out.push_back(WorldObject::getLODTextureURLForLevel(colour_texture_url, 2, this->colourTexHasAlpha()));
	}

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


static Colour3f parseColour3fWithDefault(pugi::xml_node elem, const char* elemname, const Colour3f& default_val)
{
	pugi::xml_node childnode = elem.child(elemname);
	if(!childnode)
		return default_val;

	const char* const child_text = childnode.child_value();

	Parser parser(child_text, std::strlen(child_text));

	parser.parseWhiteSpace();

	Colour3f v;
	if(!parser.parseFloat(v.r))
		throw glare::Exception("Failed to parse Vec3 from '" + std::string(std::string(child_text)) + "'." + XMLParseUtils::elemContext(childnode));
	parser.parseWhiteSpace();
	if(!parser.parseFloat(v.g))
		throw glare::Exception("Failed to parse Vec3 from '" + std::string(std::string(child_text)) + "'." + XMLParseUtils::elemContext(childnode));
	parser.parseWhiteSpace();
	if(!parser.parseFloat(v.b))
		throw glare::Exception("Failed to parse Vec3 from '" + std::string(std::string(child_text)) + "'." + XMLParseUtils::elemContext(childnode));
	parser.parseWhiteSpace();

	// We should be at the end of the string now
	if(parser.notEOF())
		throw glare::Exception("Parse error while parsing Vec3 from '" + std::string(std::string(child_text)) + "'." + XMLParseUtils::elemContext(childnode));

	return v;
}


static Matrix2f parseMatrix2f(pugi::xml_node elem, const char* elemname)
{
	pugi::xml_node childnode = elem.child(elemname);
	if(!childnode)
		throw glare::Exception(std::string("could not find element '") + elemname + "'." + XMLParseUtils::elemContext(elem));

	const char* const child_text = childnode.child_value();

	Parser parser(child_text, std::strlen(child_text));

	parser.parseWhiteSpace();

	Matrix2f m;
	if(!parser.parseFloat(m.e[0]))
		throw glare::Exception("Failed to parse Matrix2f from '" + std::string(std::string(child_text)) + "'." + XMLParseUtils::elemContext(childnode));
	parser.parseWhiteSpace();
	if(!parser.parseFloat(m.e[1]))
		throw glare::Exception("Failed to parse Matrix2f from '" + std::string(std::string(child_text)) + "'." + XMLParseUtils::elemContext(childnode));
	parser.parseWhiteSpace();
	if(!parser.parseFloat(m.e[2]))
		throw glare::Exception("Failed to parse Matrix2f from '" + std::string(std::string(child_text)) + "'." + XMLParseUtils::elemContext(childnode));
	parser.parseWhiteSpace();
	if(!parser.parseFloat(m.e[3]))
		throw glare::Exception("Failed to parse Matrix2f from '" + std::string(std::string(child_text)) + "'." + XMLParseUtils::elemContext(childnode));
	parser.parseWhiteSpace();

	// We should be at the end of the string now
	if(parser.notEOF())
		throw glare::Exception("Parse error while parsing Matrix2f from '" + std::string(std::string(child_text)) + "'." + XMLParseUtils::elemContext(childnode));

	return m;
}


static ScalarVal parseScalarVal(pugi::xml_node elem, const char* elemname, const ScalarVal& default_scalar_val)
{
	pugi::xml_node n = elem.child(elemname);
	if(n)
	{
		ScalarVal scalar_val;
		scalar_val.val = (float)XMLParseUtils::parseDoubleWithDefault(n, "val", 0.0);
		scalar_val.texture_url = XMLParseUtils::parseStringWithDefault(n, "texture_url", "");
		return scalar_val;
	}
	else
		return default_scalar_val;
}


static void convertRelPathToAbsolute(const std::string& mat_file_path, std::string& relative_path_in_out)
{
	if(!relative_path_in_out.empty())
		relative_path_in_out = FileUtils::join(FileUtils::getDirectory(mat_file_path), relative_path_in_out);
}


Reference<WorldMaterial> WorldMaterial::loadFromXMLOnDisk(const std::string& mat_file_path)
{
	IndigoXMLDoc doc(mat_file_path);

	pugi::xml_node root = doc.getRootElement();

	WorldMaterialRef mat = new WorldMaterial();
	mat->name = XMLParseUtils::parseString(root, "name");

	mat->colour_rgb = parseColour3fWithDefault(root, "colour_rgb", Colour3f(0.85f));
	mat->colour_texture_url = XMLParseUtils::parseStringWithDefault(root, "colour_texture_url", "");
	convertRelPathToAbsolute(mat_file_path, mat->colour_texture_url); // Assuming colour_texture_url is a local relative path, make local absolute path from it.

	mat->roughness = parseScalarVal(root, "roughness", ScalarVal(0.5f));
	convertRelPathToAbsolute(mat_file_path, mat->roughness.texture_url);

	mat->metallic_fraction = parseScalarVal(root, "metallic_fraction", ScalarVal(0.0f));
	convertRelPathToAbsolute(mat_file_path, mat->metallic_fraction.texture_url);

	mat->opacity = parseScalarVal(root, "opacity", ScalarVal(1.0f));
	convertRelPathToAbsolute(mat_file_path, mat->opacity.texture_url);

	if(root.child("tex_matrix"))
		mat->tex_matrix = parseMatrix2f(root, "tex_matrix");
	return mat;
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


static const uint32 WORLD_MATERIAL_SERIALISATION_VERSION = 6;

// v5: added emission_lum_flux
// v6: added flags


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

	stream.writeFloat(mat.emission_lum_flux);

	stream.writeUInt32(mat.flags);
}


void readFromStream(InStream& stream, WorldMaterial& mat)
{
	// Read version
	const uint32 v = stream.readUInt32();
	if(v > WORLD_MATERIAL_SERIALISATION_VERSION)
		throw glare::Exception("Unsupported version " + toString(v) + ", expected " + toString(WORLD_MATERIAL_SERIALISATION_VERSION) + ".");

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
			throw glare::Exception("Invalid spectrum material value.");
		};
	}
	else
	{
		mat.colour_rgb = readColour3fFromStram(stream);
		try
		{
			mat.colour_texture_url = stream.readStringLengthFirst(20000);
		}
		catch(glare::Exception& e)
		{
			throw glare::Exception("Error while reading colour_texture_url: " + e.what());
		}
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

	if(v >= 5)
		mat.emission_lum_flux = stream.readFloat();

	if(v >= 6)
		mat.flags = stream.readUInt32();
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
		throw glare::Exception("Invalid scalar material value.");
	};
}


void readFromStream(InStream& stream, ScalarVal& ob)
{
	ob.val = stream.readFloat();
	ob.texture_url = stream.readStringLengthFirst(10000);
}
