/*=====================================================================
URLUtils.cpp
------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "Avatar.h"


#include "STLArenaAllocator.h"


namespace URLUtils
{


// Converts something like
// 
// base_34345436654.bmesh 
// to
// base_34345436654_lod2_opt3.bmesh
// 
// with minimal allocations.
URLString makeOptimisedMeshURL(const URLString& base_model_url, int lod_level, bool get_optimised_mesh, int opt_mesh_version, glare::ArenaAllocator* arena_allocator)
{
	glare::STLArenaAllocator<char> stl_arena_allocator(arena_allocator);
	URLString new_url(stl_arena_allocator);

	new_url.reserve(base_model_url.size() + 16);

	// Assign part of URL before last dot to new_url (or whole thing if no dot)
	const std::string::size_type dot_index = base_model_url.find_last_of('.');
	new_url.assign(base_model_url, /*subpos=*/0, /*count=*/dot_index);

	if(lod_level >= 1)
	{
		new_url += "_lod";
		new_url.push_back('0' + (char)myMin(lod_level, 9));
	}

	if(get_optimised_mesh)
	{
		new_url += "_opt";
		if(opt_mesh_version >= 0 && opt_mesh_version <= 9)
			new_url.push_back('0' + (char)opt_mesh_version);
		else if(opt_mesh_version >= 10 && opt_mesh_version <= 99)
		{
			new_url.push_back('0' + (char)(opt_mesh_version / 10));
			new_url.push_back('0' + (char)(opt_mesh_version % 10));
		}
		else
			new_url += toString(opt_mesh_version);
	}
	new_url += ".bmesh"; // Optimised models are always saved in BatchedMesh (bmesh) format.
	return new_url;
}


} // end namespace URLUtils
