/*=====================================================================
WorldMaterial.cpp
-----------------
Copyright Glare Technologies Limited 2023 -
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
#include <BufferInStream.h>
#include <BufferOutStream.h>
#include <BufferViewInStream.h>
#include <RuntimeCheck.h>
#include <BumpAllocator.h>


WorldMaterial::WorldMaterial()
{
	colour_rgb = Colour3f(0.85f);
	roughness = ScalarVal(0.5f);
	emission_rgb = Colour3f(0.85f);
	metallic_fraction = ScalarVal(0.0f);
	opacity = ScalarVal(1.0f);
	tex_matrix = Matrix2f::identity();
	emission_lum_flux_or_lum = 0;
	flags = 0;
}


WorldMaterial::~WorldMaterial()
{}


static std::string getLODTextureURLForLevel(const std::string& base_texture_url, int material_min_lod_level, int level, bool has_alpha)
{
	if(level <= material_min_lod_level)
		return base_texture_url;
	else
	{
		// Don't do LOD on mp4 (video) textures (for now).
		// Also don't do LOD with http URLs
		if(::hasExtensionStringView(base_texture_url, "mp4") || hasPrefix(base_texture_url, "http:") || hasPrefix(base_texture_url, "https:"))
			return base_texture_url; 

		// Gifs LOD textures are always gifs.
		// Other image formats get converted to jpg if they don't have alpha, and png if they do.
		const bool is_gif = ::hasExtensionStringView(base_texture_url, "gif");

		if(level == 0)
			return removeDotAndExtension(base_texture_url) + "_lod0." + (is_gif ? "gif" : (has_alpha ? "png" : "jpg"));
		else if(level == 1)
			return removeDotAndExtension(base_texture_url) + "_lod1." + (is_gif ? "gif" : (has_alpha ? "png" : "jpg"));
		else
			return removeDotAndExtension(base_texture_url) + "_lod2." + (is_gif ? "gif" : (has_alpha ? "png" : "jpg"));
	}
}


void ScalarVal::appendDependencyURLs(bool tex_use_sRGB, int material_min_lod_level, int lod_level, std::vector<DependencyURL>& paths_out) const
{
	if(!texture_url.empty())
		paths_out.push_back(DependencyURL(getLODTextureURLForLevel(texture_url, material_min_lod_level, lod_level, /*has alpha=*/false), tex_use_sRGB));
}


void ScalarVal::appendDependencyURLsAllLODLevels(bool tex_use_sRGB, int material_min_lod_level, std::vector<DependencyURL>& paths_out) const
{
	if(!texture_url.empty())
	{
		paths_out.push_back(DependencyURL(texture_url, tex_use_sRGB));
		for(int i=material_min_lod_level+1; i <=2; ++i)
			paths_out.push_back(DependencyURL(getLODTextureURLForLevel(texture_url, material_min_lod_level, i, /*has alpha=*/false), tex_use_sRGB));
	}
}


void ScalarVal::appendDependencyURLsBaseLevel(bool tex_use_sRGB, std::vector<DependencyURL>& paths_out) const
{
	if(!texture_url.empty())
		paths_out.push_back(DependencyURL(texture_url, tex_use_sRGB));
}



void ScalarVal::convertLocalPathsToURLS(ResourceManager& resource_manager)
{
	if(!this->texture_url.empty() && FileUtils::fileExists(this->texture_url)) // If the URL is a local path:
	{
		this->texture_url = resource_manager.URLForPathAndHash(this->texture_url, FileChecksum::fileChecksum(this->texture_url));
	}
}


std::string WorldMaterial::getLODTextureURLForLevel(const std::string& base_texture_url, int level, bool has_alpha) const
{
	return ::getLODTextureURLForLevel(base_texture_url, this->minLODLevel(), level, has_alpha);
}


void WorldMaterial::appendDependencyURLs(int lod_level, std::vector<DependencyURL>& paths_out) const
{
	if(!colour_texture_url.empty())
		paths_out.push_back(DependencyURL(getLODTextureURLForLevel(colour_texture_url, lod_level, this->colourTexHasAlpha())));

	if(!emission_texture_url.empty())
		paths_out.push_back(DependencyURL(getLODTextureURLForLevel(emission_texture_url, lod_level, /*has alpha=*/false)));

	if(!normal_map_url.empty())
		paths_out.push_back(DependencyURL(getLODTextureURLForLevel(normal_map_url, lod_level, /*has alpha=*/false)));

	const int min_lod_level = this->minLODLevel();
	roughness.			appendDependencyURLs(/*use sRGB=*/false, min_lod_level, lod_level, paths_out);
	metallic_fraction.	appendDependencyURLs(/*use sRGB=*/false, min_lod_level, lod_level, paths_out);
	opacity.			appendDependencyURLs(/*use sRGB=*/false, min_lod_level, lod_level, paths_out);
}


void WorldMaterial::appendDependencyURLsAllLODLevels(std::vector<DependencyURL>& paths_out) const
{
	const int min_lod_level = this->minLODLevel();

	if(!colour_texture_url.empty())
	{
		paths_out.push_back(DependencyURL(colour_texture_url));
		for(int i=min_lod_level+1; i <=2; ++i)
			paths_out.push_back(DependencyURL(getLODTextureURLForLevel(colour_texture_url, i, this->colourTexHasAlpha())));
	}
	
	if(!emission_texture_url.empty())
	{
		paths_out.push_back(DependencyURL(emission_texture_url));
		for(int i=min_lod_level+1; i <=2; ++i)
			paths_out.push_back(DependencyURL(getLODTextureURLForLevel(emission_texture_url, i, /*has alpha=*/false)));
	}

	if(!normal_map_url.empty())
	{
		paths_out.push_back(DependencyURL(normal_map_url));
		for(int i=min_lod_level+1; i <=2; ++i)
			paths_out.push_back(DependencyURL(getLODTextureURLForLevel(normal_map_url, i, /*has alpha=*/false)));
	}

	roughness.			appendDependencyURLsAllLODLevels(/*use sRGB=*/false, min_lod_level, paths_out);
	metallic_fraction.	appendDependencyURLsAllLODLevels(/*use sRGB=*/false, min_lod_level, paths_out);
	opacity.			appendDependencyURLsAllLODLevels(/*use sRGB=*/false, min_lod_level, paths_out);
}


void WorldMaterial::appendDependencyURLsBaseLevel(std::vector<DependencyURL>& paths_out) const
{
	if(!colour_texture_url.empty())
		paths_out.push_back(DependencyURL(colour_texture_url));

	if(!emission_texture_url.empty())
		paths_out.push_back(DependencyURL(emission_texture_url));

	if(!normal_map_url.empty())
		paths_out.push_back(DependencyURL(normal_map_url));

	roughness.			appendDependencyURLsBaseLevel(/*use sRGB=*/false, paths_out);
	metallic_fraction.	appendDependencyURLsBaseLevel(/*use sRGB=*/false, paths_out);
	opacity.			appendDependencyURLsBaseLevel(/*use sRGB=*/false, paths_out);
}


void WorldMaterial::convertLocalPathsToURLS(ResourceManager& resource_manager)
{
	if(FileUtils::fileExists(this->colour_texture_url)) // If the URL is a local path:
		this->colour_texture_url = resource_manager.URLForPathAndHash(this->colour_texture_url, FileChecksum::fileChecksum(this->colour_texture_url));
	
	if(FileUtils::fileExists(this->emission_texture_url)) // If the URL is a local path:
		this->emission_texture_url = resource_manager.URLForPathAndHash(this->emission_texture_url, FileChecksum::fileChecksum(this->emission_texture_url));
	
	if(FileUtils::fileExists(this->normal_map_url)) // If the URL is a local path:
		this->normal_map_url = resource_manager.URLForPathAndHash(this->normal_map_url, FileChecksum::fileChecksum(this->normal_map_url));

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


Reference<WorldMaterial> WorldMaterial::loadFromXMLElem(const std::string& mat_file_path, bool convert_rel_paths_to_abs_disk_paths, pugi::xml_node material_elem)
{
	WorldMaterialRef mat = new WorldMaterial();
	mat->name = XMLParseUtils::parseString(material_elem, "name");

	mat->colour_rgb = parseColour3fWithDefault(material_elem, "colour_rgb", Colour3f(0.85f));
	mat->colour_texture_url = XMLParseUtils::parseStringWithDefault(material_elem, "colour_texture_url", "");
	if(convert_rel_paths_to_abs_disk_paths)
		convertRelPathToAbsolute(mat_file_path, mat->colour_texture_url); // Assuming colour_texture_url is a local relative path, make local absolute path from it.

	mat->emission_rgb = parseColour3fWithDefault(material_elem, "emission_rgb", Colour3f(0.f));
	mat->emission_texture_url = XMLParseUtils::parseStringWithDefault(material_elem, "emission_texture_url", "");
	if(convert_rel_paths_to_abs_disk_paths)
		convertRelPathToAbsolute(mat_file_path, mat->emission_texture_url);

	mat->emission_lum_flux_or_lum = (float)XMLParseUtils::parseDoubleWithDefault(material_elem, "emission_lum_flux_or_lum", 0.0);

	mat->roughness = parseScalarVal(material_elem, "roughness", ScalarVal(0.5f));
	if(convert_rel_paths_to_abs_disk_paths)
		convertRelPathToAbsolute(mat_file_path, mat->roughness.texture_url);

	mat->metallic_fraction = parseScalarVal(material_elem, "metallic_fraction", ScalarVal(0.0f));
	if(convert_rel_paths_to_abs_disk_paths)
		convertRelPathToAbsolute(mat_file_path, mat->metallic_fraction.texture_url);

	mat->normal_map_url = XMLParseUtils::parseStringWithDefault(material_elem, "normal_map_url", "");

	mat->opacity = parseScalarVal(material_elem, "opacity", ScalarVal(1.0f));
	if(convert_rel_paths_to_abs_disk_paths)
		convertRelPathToAbsolute(mat_file_path, mat->opacity.texture_url);

	if(material_elem.child("tex_matrix"))
		mat->tex_matrix = parseMatrix2f(material_elem, "tex_matrix");
	return mat;
}


Reference<WorldMaterial> WorldMaterial::loadFromXMLOnDisk(const std::string& mat_file_path, bool convert_rel_paths_to_abs_disk_paths)
{
	IndigoXMLDoc doc(mat_file_path);

	pugi::xml_node root = doc.getRootElement();

	return loadFromXMLElem(mat_file_path, convert_rel_paths_to_abs_disk_paths, root);
}



static void writeColour3fToXML(std::string& xml, const std::string& elem_name, const Colour3f& col, int tab_depth)
{
	xml += std::string(tab_depth, '\t') + "<" + elem_name + ">" + toString(col.r) + " " + toString(col.g) + " " + toString(col.b) + "</" + elem_name + ">\n";
}

static void writeStringElemToXML(std::string& xml, const std::string& elem_name, const std::string& string_val, int tab_depth)
{
	xml += std::string(tab_depth, '\t') + "<" + elem_name + "><![CDATA[" + string_val + "]]></" + elem_name + ">\n";
}

static void writeScalarValToXML(std::string& xml, const std::string& elem_name, const ScalarVal& scalar_val, int tab_depth)
{
	xml += std::string(tab_depth, '\t') + "<" + elem_name + ">\n";
	
	xml += std::string(tab_depth + 1, '\t') + "<val>" + toString(scalar_val.val) + "</val>\n";
	if(!scalar_val.texture_url.empty())
		writeStringElemToXML(xml, "texture_url", scalar_val.texture_url, tab_depth + 1);

	xml += std::string(tab_depth, '\t') + "</" + elem_name + ">\n";
}


std::string WorldMaterial::serialiseToXML(int tab_depth) const
{
	std::string s;
	s += std::string(tab_depth, '\t') + "<material>\n";

	writeStringElemToXML(s, "name", name, tab_depth + 1);

	writeColour3fToXML(s, "colour_rgb", colour_rgb, tab_depth + 1);
	writeStringElemToXML(s, "colour_texture_url", colour_texture_url, tab_depth + 1);

	writeColour3fToXML(s, "emission_rgb", emission_rgb, tab_depth + 1);
	writeStringElemToXML(s, "emission_texture_url", emission_texture_url, tab_depth + 1);
	
	writeStringElemToXML(s, "normal_map_url", normal_map_url, tab_depth + 1);

	writeScalarValToXML(s, "roughness", roughness, tab_depth + 1);
	writeScalarValToXML(s, "metallic_fraction", metallic_fraction, tab_depth + 1);
	writeScalarValToXML(s, "opacity", opacity, tab_depth + 1);

	s += std::string(tab_depth + 1, '\t') + "<tex_matrix>" + toString(tex_matrix.e[0]) + " " + toString(tex_matrix.e[1]) + " " + toString(tex_matrix.e[2]) + " " + toString(tex_matrix.e[3]) + "</tex_matrix>\n";

	s += std::string(tab_depth + 1, '\t') + "<emission_lum_flux_or_lum>" + toString(emission_lum_flux_or_lum) + "</emission_lum_flux_or_lum>\n";
	s += std::string(tab_depth + 1, '\t') + "<flags>" + toString(flags) + "</flags>\n";

	s += std::string(tab_depth, '\t') + "</material>\n";

	return s;
}


void WorldMaterial::writeToXMLOnDisk(const std::string& path) const
{
	const std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n" + serialiseToXML(/*tab_depth=*/0);

	FileUtils::writeEntireFileTextMode(path, xml);
}


static void writeToStream(OutStream& stream, const Colour3f& col)
{
	stream.writeFloat(col.r);
	stream.writeFloat(col.g);
	stream.writeFloat(col.b);
}


static Colour3f readColour3fFromStream(InStream& stream)
{
	Colour3f col;
	col.r = stream.readFloat();
	col.g = stream.readFloat();
	col.b = stream.readFloat();
	return col;
}


static const uint32 WORLD_MATERIAL_SERIALISATION_VERSION = 8;

// v5: added emission_lum_flux
// v6: added flags
// v7: added emission_rgb, emission_texture_url
// v8: added length prefix and normal_map_url


void writeWorldMaterialToStream(const WorldMaterial& mat, OutStream& stream_, glare::Allocator& temp_allocator)
{
	// Write to stream with a length prefix.  Do this by writing to a temporary buffer first, then writing the length of that buffer.
	// Writing a length prefix allows for adding more fields later, while retaining backwards compatibility with older code that can just skip over the new fields.
	BufferOutStream buffer;
	buffer.buf.setAllocator(&temp_allocator);
	buffer.buf.reserve(4096);

	// Write version
	buffer.writeUInt32(WORLD_MATERIAL_SERIALISATION_VERSION);
	buffer.writeUInt32(0); // Size of buffer will be written here later

	writeToStream(buffer, mat.colour_rgb);
	buffer.writeStringLengthFirst(mat.colour_texture_url);

	writeToStream(buffer, mat.emission_rgb);
	buffer.writeStringLengthFirst(mat.emission_texture_url);

	writeScalarValToStream(mat.roughness, buffer);
	writeScalarValToStream(mat.metallic_fraction, buffer);
	writeScalarValToStream(mat.opacity, buffer);

	writeToStream(mat.tex_matrix, buffer);

	buffer.writeFloat(mat.emission_lum_flux_or_lum);

	buffer.writeUInt32(mat.flags);

	buffer.writeStringLengthFirst(mat.normal_map_url);


	// Go back and write size of buffer to buffer size field
	const uint32 buffer_size = (uint32)buffer.buf.size();
	std::memcpy(buffer.buf.data() + sizeof(uint32), &buffer_size, sizeof(uint32));

	// Write buffer to actual output stream
	stream_.writeData(buffer.buf.data(), buffer.buf.size());
}


void readWorldMaterialFromStream(InStream& stream, WorldMaterial& mat, glare::BumpAllocator& bump_allocator)
{
	// Read version
	const uint32 v = stream.readUInt32();

	if(v >= 8) // If length-prefixed:
	{
		const uint32 buffer_size = stream.readUInt32();

		checkProperty(buffer_size >= 8ul, "readWorldMaterialFromStream: buffer_size was too small");
		checkProperty(buffer_size <= 65536ul, "readWorldMaterialFromStream: buffer_size was too large");

		// Read rest of data to buffer
		const uint32 remaining_buffer_size = buffer_size - sizeof(uint32)*2;

		glare::BumpAllocation buffer(remaining_buffer_size, 16, bump_allocator); // Allocate buffer
	
		stream.readData(buffer.ptr, remaining_buffer_size); // Read from stream to buffer

		BufferViewInStream buffer_stream(ArrayRef<uint8>((uint8*)buffer.ptr, buffer.size)); // Create stream view on buffer

		mat.colour_rgb = readColour3fFromStream(buffer_stream);
		mat.colour_texture_url = buffer_stream.readStringLengthFirst(20000);
		mat.emission_rgb = readColour3fFromStream(buffer_stream);
		mat.emission_texture_url = buffer_stream.readStringLengthFirst(20000);
		readScalarValFromStream(buffer_stream, mat.roughness);
		readScalarValFromStream(buffer_stream, mat.metallic_fraction);
		readScalarValFromStream(buffer_stream, mat.opacity);
		mat.tex_matrix = readMatrix2FromStream<float>(buffer_stream);
		mat.emission_lum_flux_or_lum = buffer_stream.readFloat();
		mat.flags = buffer_stream.readUInt32();
		mat.normal_map_url = buffer_stream.readStringLengthFirst(20000);
	}
	else // Else if we are reading older version before length-prefixing:
	{
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
			mat.colour_rgb = readColour3fFromStream(stream);
			try
			{
				mat.colour_texture_url = stream.readStringLengthFirst(20000);
			}
			catch(glare::Exception& e)
			{
				throw glare::Exception("Error while reading colour_texture_url: " + e.what());
			}
		}

		if(v >= 7)
		{
			mat.emission_rgb = readColour3fFromStream(stream);
			mat.emission_texture_url = stream.readStringLengthFirst(20000);
		}
	
		if(v <= 2)
		{
			readScalarValFromStreamOld(stream, mat.roughness);
			readScalarValFromStreamOld(stream, mat.metallic_fraction);
			readScalarValFromStreamOld(stream, mat.opacity);
		}
		else
		{
			readScalarValFromStream(stream, mat.roughness);
			readScalarValFromStream(stream, mat.metallic_fraction);
			readScalarValFromStream(stream, mat.opacity);
		}

		if(v >= 4)
			mat.tex_matrix = readMatrix2FromStream<float>(stream);
		else
			mat.tex_matrix = Matrix2f(1, 0, 0, -1); // Needed for existing object objects etc..

		if(v >= 5)
			mat.emission_lum_flux_or_lum = stream.readFloat();

		if(v >= 6)
			mat.flags = stream.readUInt32();
	}
}


void writeScalarValToStream(const ScalarVal& val, OutStream& stream)
{
	stream.writeFloat(val.val);
	stream.writeStringLengthFirst(val.texture_url);
}


void readScalarValFromStreamOld(InStream& stream, ScalarVal& ob)
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


void readScalarValFromStream(InStream& stream, ScalarVal& ob)
{
	ob.val = stream.readFloat();
	ob.texture_url = stream.readStringLengthFirst(10000);
}


#if BUILD_TESTS


#include <TestUtils.h>
#include <FileUtils.h>
#include <ConPrint.h>
#include <PlatformUtils.h>
#include <BufferOutStream.h>
#include <Timer.h>
#include <ArenaAllocator.h>


static void doAppendDependencyURLsTestForMat(WorldMaterial& mat, bool is_colour_tex, bool expected_use_sRGB)
{
	//----------------------- Test appendDependencyURLs with different LOD levels with alpha -----------------------
	{
		mat.flags = 0;

		std::vector<DependencyURL> urls;
		mat.appendDependencyURLs(/*lod level=*/0, urls);
		testAssert(urls.size() == 1 && urls[0].URL == "sometex.png" && urls[0].use_sRGB == expected_use_sRGB);

		urls.clear();
		mat.appendDependencyURLs(/*lod level=*/-1, urls);
		testAssert(urls.size() == 1 && urls[0].URL == "sometex.png" && urls[0].use_sRGB == expected_use_sRGB);

		urls.clear();
		mat.appendDependencyURLs(/*lod level=*/1, urls);
		testAssert(urls.size() == 1 && urls[0].URL == "sometex_lod1.jpg" && urls[0].use_sRGB == expected_use_sRGB); // Should use jpg extension

		urls.clear();
		mat.appendDependencyURLs(/*lod level=*/2, urls);
		testAssert(urls.size() == 1 && urls[0].URL == "sometex_lod2.jpg" && urls[0].use_sRGB == expected_use_sRGB); // Should use jpg extension

		// Now mark the material as having alpha - now colour texture LODs should still be in PNG format.
		BitUtils::setBit(mat.flags, WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG);

		if(is_colour_tex)
		{
			urls.clear();
			mat.appendDependencyURLs(/*lod level=*/1, urls);
			testAssert(urls.size() == 1 && urls[0].URL == "sometex_lod1.png" && urls[0].use_sRGB == expected_use_sRGB); // Should use png extension (for the alpha channel)

			urls.clear();
			mat.appendDependencyURLs(/*lod level=*/2, urls);
			testAssert(urls.size() == 1 && urls[0].URL == "sometex_lod2.png" && urls[0].use_sRGB == expected_use_sRGB); // Should use png extension
		}
		else
		{
			urls.clear();
			mat.appendDependencyURLs(/*lod level=*/1, urls);
			testAssert(urls.size() == 1 && urls[0].URL == "sometex_lod1.jpg" && urls[0].use_sRGB == expected_use_sRGB); // Should use jpg extension

			urls.clear();
			mat.appendDependencyURLs(/*lod level=*/2, urls);
			testAssert(urls.size() == 1 && urls[0].URL == "sometex_lod2.jpg" && urls[0].use_sRGB == expected_use_sRGB); // Should use jpg extension
		}

	}


	//----------------------- Test appendDependencyURLs with the MIN_LOD_LEVEL_IS_NEGATIVE_1 flag -----------------------
	{
		mat.flags = 0;
		BitUtils::setBit(mat.flags, WorldMaterial::MIN_LOD_LEVEL_IS_NEGATIVE_1);

		std::vector<DependencyURL> urls;
		mat.appendDependencyURLs(/*lod level=*/-1, urls);
		testAssert(urls.size() == 1 && urls[0].URL == "sometex.png" && urls[0].use_sRGB == expected_use_sRGB);

		urls.clear();
		mat.appendDependencyURLs(/*lod level=*/0, urls);
		testAssert(urls.size() == 1 && urls[0].URL == "sometex_lod0.jpg" && urls[0].use_sRGB == expected_use_sRGB); // Should use jpg extension

		urls.clear();
		mat.appendDependencyURLs(/*lod level=*/1, urls);
		testAssert(urls.size() == 1 && urls[0].URL == "sometex_lod1.jpg" && urls[0].use_sRGB == expected_use_sRGB); // Should use jpg extension

		urls.clear();
		mat.appendDependencyURLs(/*lod level=*/2, urls);
		testAssert(urls.size() == 1 && urls[0].URL == "sometex_lod2.jpg" && urls[0].use_sRGB == expected_use_sRGB); // Should use jpg extension

		// Now mark the material as having alpha - now colour texture LODs should still be in PNG format.
		BitUtils::setBit(mat.flags, WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG);

		if(is_colour_tex)
		{
			urls.clear();
			mat.appendDependencyURLs(/*lod level=*/0, urls);
			testAssert(urls.size() == 1 && urls[0].URL == "sometex_lod0.png" && urls[0].use_sRGB == expected_use_sRGB); // Should use png extension (for the alpha channel)

			urls.clear();
			mat.appendDependencyURLs(/*lod level=*/1, urls);
			testAssert(urls.size() == 1 && urls[0].URL == "sometex_lod1.png" && urls[0].use_sRGB == expected_use_sRGB); // Should use png extension

			urls.clear();
			mat.appendDependencyURLs(/*lod level=*/2, urls);
			testAssert(urls.size() == 1 && urls[0].URL == "sometex_lod2.png" && urls[0].use_sRGB == expected_use_sRGB); // Should use png extension
		}
		else
		{
			urls.clear();
			mat.appendDependencyURLs(/*lod level=*/0, urls);
			testAssert(urls.size() == 1 && urls[0].URL == "sometex_lod0.jpg" && urls[0].use_sRGB == expected_use_sRGB); // Should use jpg extension

			urls.clear();
			mat.appendDependencyURLs(/*lod level=*/1, urls);
			testAssert(urls.size() == 1 && urls[0].URL == "sometex_lod1.jpg" && urls[0].use_sRGB == expected_use_sRGB); // Should use jpg extension

			urls.clear();
			mat.appendDependencyURLs(/*lod level=*/2, urls);
			testAssert(urls.size() == 1 && urls[0].URL == "sometex_lod2.jpg" && urls[0].use_sRGB == expected_use_sRGB); // Should use jpg extension
		}
	}


	//----------------------- Test appendDependencyURLsBaseLevel -----------------------
	{
		mat.flags = 0;

		std::vector<DependencyURL> urls;
		mat.appendDependencyURLsBaseLevel(urls);
		testAssert(urls.size() == 1 && urls[0].URL == "sometex.png" && urls[0].use_sRGB == expected_use_sRGB);
	}
	
	//----------------------- Test appendDependencyURLsAllLODLevels -----------------------
	{
		mat.flags = 0;

		std::vector<DependencyURL> urls;
		mat.appendDependencyURLsAllLODLevels(urls);
		testAssert(urls.size() == 3);
		testAssert(urls[0].use_sRGB == expected_use_sRGB && urls[0].URL == "sometex.png");
		testAssert(urls[1].use_sRGB == expected_use_sRGB && urls[1].URL == "sometex_lod1.jpg");
		testAssert(urls[2].use_sRGB == expected_use_sRGB && urls[2].URL == "sometex_lod2.jpg");

		// Now mark the material as having alpha - now colour texture LODs should still be in PNG format.
		BitUtils::setBit(mat.flags, WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG);

		urls.clear();
		mat.appendDependencyURLsAllLODLevels(urls);
		testAssert(urls.size() == 3);
		if(is_colour_tex)
		{
			testAssert(urls[0].use_sRGB == expected_use_sRGB && urls[0].URL == "sometex.png");
			testAssert(urls[1].use_sRGB == expected_use_sRGB && urls[1].URL == "sometex_lod1.png");
			testAssert(urls[2].use_sRGB == expected_use_sRGB && urls[2].URL == "sometex_lod2.png");
		}
		else
		{
			testAssert(urls[0].use_sRGB == expected_use_sRGB && urls[0].URL == "sometex.png");
			testAssert(urls[1].use_sRGB == expected_use_sRGB && urls[1].URL == "sometex_lod1.jpg");
			testAssert(urls[2].use_sRGB == expected_use_sRGB && urls[2].URL == "sometex_lod2.jpg");
		}



		mat.flags = 0;
		BitUtils::setBit(mat.flags, WorldMaterial::MIN_LOD_LEVEL_IS_NEGATIVE_1);

		urls.clear();
		mat.appendDependencyURLsAllLODLevels(urls);
		testAssert(urls.size() == 4);
		testAssert(urls[0].use_sRGB == expected_use_sRGB && urls[0].URL == "sometex.png");
		testAssert(urls[1].use_sRGB == expected_use_sRGB && urls[1].URL == "sometex_lod0.jpg");
		testAssert(urls[1].use_sRGB == expected_use_sRGB && urls[2].URL == "sometex_lod1.jpg");
		testAssert(urls[3].use_sRGB == expected_use_sRGB && urls[3].URL == "sometex_lod2.jpg");

		// Now mark the material as having alpha - now colour texture LODs should still be in PNG format.
		BitUtils::setBit(mat.flags, WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG);

		urls.clear();
		mat.appendDependencyURLsAllLODLevels(urls);
		testAssert(urls.size() == 4);
		if(is_colour_tex)
		{
			testAssert(urls[0].use_sRGB == expected_use_sRGB && urls[0].URL == "sometex.png");
			testAssert(urls[1].use_sRGB == expected_use_sRGB && urls[1].URL == "sometex_lod0.png");
			testAssert(urls[2].use_sRGB == expected_use_sRGB && urls[2].URL == "sometex_lod1.png");
			testAssert(urls[3].use_sRGB == expected_use_sRGB && urls[3].URL == "sometex_lod2.png");
		}
		else
		{
			testAssert(urls[0].use_sRGB == expected_use_sRGB && urls[0].URL == "sometex.png");
			testAssert(urls[1].use_sRGB == expected_use_sRGB && urls[1].URL == "sometex_lod0.jpg");
			testAssert(urls[2].use_sRGB == expected_use_sRGB && urls[2].URL == "sometex_lod1.jpg");
			testAssert(urls[3].use_sRGB == expected_use_sRGB && urls[3].URL == "sometex_lod2.jpg");
		}
	}
}


void WorldMaterial::test()
{
	conPrint("WorldMaterial::test()");


	{
		WorldMaterial mat;
		mat.colour_texture_url = "sometex.png";
		doAppendDependencyURLsTestForMat(mat, /*is_colour_tex=*/true, /*expected_use_sRGB=*/true);
	}
	{
		WorldMaterial mat;
		mat.emission_texture_url = "sometex.png";
		doAppendDependencyURLsTestForMat(mat, /*is_colour_tex=*/false, /*expected_use_sRGB=*/true);
	}
	{
		WorldMaterial mat;
		mat.roughness.texture_url = "sometex.png"; // roughness-metallic texture
		doAppendDependencyURLsTestForMat(mat, /*is_colour_tex=*/false, /*expected_use_sRGB=*/false);
	}
	{
		WorldMaterial mat;
		mat.normal_map_url = "sometex.png";
		doAppendDependencyURLsTestForMat(mat, /*is_colour_tex=*/false, /*expected_use_sRGB=*/false);
	}



	{
		WorldMaterial mat;
		mat.colour_texture_url = "sometex.jpg";


		//----------------------- Test appendDependencyURLs with different LOD levels -----------------------
		std::vector<DependencyURL> urls;
		mat.appendDependencyURLs(/*lod level=*/0, urls);
		testAssert(urls.size() == 1 && urls[0].URL == "sometex.jpg");

		urls.clear();
		mat.appendDependencyURLs(/*lod level=*/-1, urls);
		testAssert(urls.size() == 1 && urls[0].URL == "sometex.jpg");

		urls.clear();
		mat.appendDependencyURLs(/*lod level=*/1, urls);
		testAssert(urls.size() == 1 && urls[0].URL == "sometex_lod1.jpg");

		urls.clear();
		mat.appendDependencyURLs(/*lod level=*/2, urls);
		testAssert(urls.size() == 1 && urls[0].URL == "sometex_lod2.jpg");
	}


	// Perf test
	{
		int iters = 1000;
		WorldMaterial mat;
		mat.colour_texture_url = "sometex.png";
		mat.metallic_fraction.texture_url = "othertex.jpg";

		/*{
			BufferOutStream buf;

			Timer timer;
			for(int i=0; i<iters; ++i)
			{
				buf.buf.resize(0);
				writeWorldMaterialToStream(mat, buf);
			}
			double time_per_iter = timer.elapsed() / iters;
			conPrint("writeToStream time_per_iter: " + doubleToStringNSigFigs(time_per_iter * 1.0e9, 4) + " ns");
		}*/

		{
			Reference<glare::ArenaAllocator> arena_allocator = new glare::ArenaAllocator(1024 * 1024);
			BufferOutStream buf;
			//buf.buf.setAllocator(arena_allocator);

			Timer timer;
			for(int i=0; i<iters; ++i)
			{
				buf.buf.resize(0);
				writeWorldMaterialToStream(mat, buf, *arena_allocator);

				arena_allocator->clear();
			}
			double time_per_iter = timer.elapsed() / iters;
			conPrint("writeWorldMaterialToStream length prefixed time_per_iter: " + doubleToStringNSigFigs(time_per_iter * 1.0e9, 4) + " ns");
		}


		{
			Reference<glare::ArenaAllocator> arena_allocator = new glare::ArenaAllocator(1024 * 1024);
			BufferOutStream buf;
			writeWorldMaterialToStream(mat, buf, *arena_allocator);

			BufferInStream instreambuf;
			instreambuf.buf.resize(buf.buf.size());
			BitUtils::checkedMemcpy(instreambuf.buf.data(), buf.buf.data(), buf.buf.size());

			glare::BumpAllocator bump_allocator(1024 * 1024);

			Timer timer;
			for(int i=0; i<iters; ++i)
			{
				instreambuf.setReadIndex(0);

				WorldMaterial mat2;
				readWorldMaterialFromStream(instreambuf, mat2, bump_allocator);

				testAssert(mat2.colour_texture_url == mat.colour_texture_url);
			}
			double time_per_iter = timer.elapsed() / iters;
			conPrint("readWorldMaterialFromStream time_per_iter: " + doubleToStringNSigFigs(time_per_iter * 1.0e9, 4) + " ns");
		}
		/*{
			Reference<glare::ArenaAllocator> arena_allocator = new glare::ArenaAllocator(1024 * 1024);

			BufferOutStream buf;
			writeWorldMaterialToStreamLengthPrefixed(mat, buf, *arena_allocator);

			BufferInStream instreambuf;
			instreambuf.buf.resize(buf.buf.size());
			BitUtils::checkedMemcpy(instreambuf.buf.data(), buf.buf.data(), buf.buf.size());

			glare::BumpAllocator bump_allocator(1024 * 1024);
			Timer timer;
			for(int i=0; i<iters; ++i)
			{
				instreambuf.setReadIndex(0);

				WorldMaterial mat2;
				readFromStreamLengthPrefixed(instreambuf, mat2, bump_allocator);

				testAssert(mat2.colour_texture_url == mat.colour_texture_url);
			}
			double time_per_iter = timer.elapsed() / iters;
			conPrint("readFromStreamLenPrefix time_per_iter: " + doubleToStringNSigFigs(time_per_iter * 1.0e9, 4) + " ns");
		}*/
	}


	conPrint("WorldMaterial::test() done");
}


#endif

