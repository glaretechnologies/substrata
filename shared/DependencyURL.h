/*=====================================================================
DependencyURL.h
---------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once

#include "../shared/URLString.h"
#include <utils/STLArenaAllocator.h>
#include <string>
#include <set>
#include <vector>


struct DependencyURL
{
	explicit DependencyURL() : use_sRGB(true), is_lightmap(false) {}
	explicit DependencyURL(const URLString& URL_) : URL(URL_), use_sRGB(true), is_lightmap(false) {}
	explicit DependencyURL(const URLString& URL_, bool use_sRGB_) : URL(URL_), use_sRGB(use_sRGB_), is_lightmap(false) {}

	URLString URL;
	bool use_sRGB; // for textures.  We keep track of this so we can load e.g. metallic-roughness textures into the OpenGL engine without sRGB.
	bool is_lightmap; // If this is true, we want to load the texture (when downloaded) as not using mipmaps.

	inline bool operator < (const DependencyURL& other) const { return URL < other.URL; }
};


typedef std::set<DependencyURL, std::less<DependencyURL>, glare::STLArenaAllocator<DependencyURL>> DependencyURLSet;

typedef std::vector<DependencyURL, glare::STLArenaAllocator<DependencyURL>> DependencyURLVector;
