/*=====================================================================
URLUtils.h
----------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "URLString.h"
namespace glare { class ArenaAllocator; }


namespace URLUtils
{

URLString makeOptimisedMeshURL(const URLString& base_model_url, int model_min_lod_level, int lod_level, bool get_optimised_mesh, int opt_mesh_version, glare::ArenaAllocator* arena_allocator);

} // end namespace URLUtils
