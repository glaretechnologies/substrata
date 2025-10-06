/*=====================================================================
WorldObject.cpp
---------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#include "WorldObject.h"


#if GUI_CLIENT
#include "../audio/AudioEngine.h"
#include "../gui_client/WinterShaderEvaluator.h"
#include "../gui_client/WebViewData.h"
#include "../gui_client/BrowserVidPlayer.h"
#include "../gui_client/AnimatedTextureManager.h"
#include "../gui_client/MeshManager.h"
#include "../gui_client/PhysicsObject.h"
#include "../gui_client/Scripting.h"
#include <graphics/ImageMap.h>
#include <opengl/ui/GLUITextView.h>
#include <opengl/OpenGLEngine.h>
#include <opengl/OpenGLMeshRenderData.h>
#endif // GUI_CLIENT
#include "../shared/ResourceManager.h"
#include "../shared/LuaScriptEvaluator.h"
#include "../shared/ObjectEventHandlers.h"
#include <utils/Exception.h>
#include <utils/StringUtils.h>
#include <utils/FileUtils.h>
#include <utils/ConPrint.h>
#include <utils/FileChecksum.h>
#include <utils/Sort.h>
#include <utils/BufferInStream.h>
#include <utils/FastPoolAllocator.h>
#include <utils/Base64.h>
#include <utils/RandomAccessOutStream.h>
#include <utils/XMLWriteUtils.h>
#include <utils/XMLParseUtils.h>
#include <utils/IncludeXXHash.h>
#include <utils/STLArenaAllocator.h>
#include <zstd.h>
#include <pugixml.hpp>
#if INDIGO_SUPPORT
#include <dll/include/SceneNodeModel.h>
#endif

InstanceInfo::~InstanceInfo()
{
#if GUI_CLIENT
	assert(physics_object.isNull());
#endif
}


WorldObject::WorldObject() noexcept
{
#if !defined(__clang__) // Clang complains with "warning: offset of on non-standard-layout type 'WorldObject' [-Winvalid-offsetof]".
	static_assert(offsetof(WorldObject, centroid_ws) == 0);
	static_assert(offsetof(WorldObject, aabb_ws_longest_len) == 16);
	static_assert(offsetof(WorldObject, aabb_os) == 32);
#endif

	creator_id = UserID::invalidUserID();
	flags = COLLIDABLE_FLAG | AUDIO_AUTOPLAY | AUDIO_LOOP;
	
	object_type = ObjectType_Generic;
	from_remote_transform_dirty = false;
	from_remote_physics_transform_dirty = false;
	from_remote_summoned_dirty = false;
	from_remote_other_dirty = false;
	from_remote_lightmap_url_dirty = false;
	from_remote_model_url_dirty = false;
	from_remote_content_dirty = false;
	from_remote_flags_dirty = false;
	from_remote_physics_ownership_dirty = false;
	from_local_transform_dirty = false;
	from_local_other_dirty = false;
	from_local_physics_dirty = false;
	changed_flags = 0;
	using_placeholder_model = false;

#if GUI_CLIENT
	is_selected = false;
	in_proximity = false;
	in_script_proximity = false;
	in_audio_proximity = false;
	exclude_from_lod_chunk_mesh = false;
	lightmap_baking = false;
	current_lod_level = 0;
	loading_or_loaded_model_lod_level = -10;
	loading_or_loaded_lod_level = -10;
	is_path_controlled = false;
	use_materialise_effect_on_load = false;
	materialise_effect_start_time = -1000.f;

	waypoint_index = 0;
	dist_along_segment = 0;
	last_touch_event_time = -1000.f;
#endif
	next_snapshot_i = 0;
	next_insertable_snapshot_i = 0;
	snapshots_are_physics_snapshots = false;
	//last_snapshot_time = 0;
	aabb_ws_longest_len = -1;
	biased_aabb_len = -1;

	translation = Vec4f(0.f);

	max_load_dist2 = 1000000000;

	aabb_os = js::AABBox::emptyAABBox();

	max_model_lod_level = 0;

	mass = 50.f;
	friction = 0.5f;
	restitution = 0.2f;
	centre_of_mass_offset_os = Vec3f(0.f);

	audio_volume = 1;

	allocator = NULL;
	refcount = 0;

	physics_owner_id = std::numeric_limits<uint32>::max();
	last_physics_ownership_change_global_time = 0;

	transmission_time_offset = 0;

	chunk_batch0_start = chunk_batch0_end = chunk_batch1_start = chunk_batch1_end = 0;

	compressed_voxels_hash = 0;
}


WorldObject::~WorldObject()
{
#if GUI_CLIENT
	assert(physics_object.isNull());
#endif
}


// Converts something like
// 
// base_34345436654.bmesh 
// to
// base_34345436654_lod2_opt3.bmesh
// 
// with minimal allocations.
URLString WorldObject::makeOptimisedMeshURL(const URLString& base_model_url, int lod_level, bool get_optimised_mesh, int opt_mesh_version, glare::ArenaAllocator* arena_allocator)
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


URLString WorldObject::getLODModelURLForLevel(const URLString& base_model_url, int lod_level, const GetLODModelURLOptions& options)
{
	if((lod_level == 0) && !options.get_optimised_mesh)
		return base_model_url;

	if(hasPrefix(base_model_url, "http:") || hasPrefix(base_model_url, "https:"))
		return base_model_url;

	return makeOptimisedMeshURL(base_model_url, lod_level, /*get_optimised_mesh=*/options.get_optimised_mesh, options.opt_mesh_version, options.allocator);
}


static URLString removeTrailingNumerals(const URLString& URL)
{
	URLString res = URL;
	while(!res.empty() && isNumeric(res.back()))
		res.pop_back();
	return res;
}


int WorldObject::getLODLevelForURL(const URLString& URL) // Identifies _lod1 etc. suffix.
{
	URLString base = removeDotAndExtension(URL);

	// Return "_optXX" suffix if present
	const URLString numerals_removed = removeTrailingNumerals(base);
	if(hasSuffix(numerals_removed, "_opt"))
		base = eatSuffix(numerals_removed, "_opt");


	if(hasSuffix(base, "_lod1"))
		return 1;
	else if(hasSuffix(base, "_lod2"))
		return 2;
	else
		return 0;
}


int WorldObject::getModelLODLevel(const Vec3d& campos) const // getLODLevel() clamped to max_model_lod_level
{
	if(max_model_lod_level == 0)
		return 0;

	return myMax(0, getLODLevel(campos));
}


int WorldObject::getModelLODLevelForObLODLevel(int ob_lod_level) const
{
	return myClamp<int>(ob_lod_level, 0, this->max_model_lod_level);
}


URLString WorldObject::getLODModelURL(const Vec3d& campos, const GetLODModelURLOptions& options) const
{
	// Early-out for max_model_lod_level == 0: avoid computing LOD
	if(this->max_model_lod_level == 0)
	{
		if(options.get_optimised_mesh)
			return makeOptimisedMeshURL(this->model_url, /*lod_level=*/0, /*get_optimised_mesh=*/true, options.opt_mesh_version);
		else
			return this->model_url;
	}

	const int ob_lod_level = getLODLevel(campos);
	const int ob_model_lod_level = myClamp(ob_lod_level, 0, this->max_model_lod_level);
	return getLODModelURLForLevel(this->model_url, ob_model_lod_level, options);
}


URLString WorldObject::getLODLightmapURLForLevel(const URLString& base_lightmap_url, int level)
{
	assert(level >= -1 && level <= 2);
	if(level <= 0)
		return base_lightmap_url;
	else if(level == 1)
	{
		URLString res(base_lightmap_url.get_allocator());
		res.reserve(base_lightmap_url.size() + 8);
		res.append(removeDotAndExtension(base_lightmap_url));
		res.append("_lod1.");
		res.append(getExtensionStringView(base_lightmap_url));
		return res;
	}
	else
	{
		URLString res(base_lightmap_url.get_allocator());
		res.reserve(base_lightmap_url.size() + 8);
		res.append(removeDotAndExtension(base_lightmap_url));
		res.append("_lod2.");
		res.append(getExtensionStringView(base_lightmap_url));
		return res;
	}
}


OpenGLTextureKey WorldObject::getLODLightmapPathForLevel(const OpenGLTextureKey& base_lightmap_path, int level)
{
	assert(level >= -1 && level <= 2);
	if(level <= 0)
		return base_lightmap_path;
	else if(level == 1)
	{
		OpenGLTextureKey res(base_lightmap_path.get_allocator());
		res.reserve(base_lightmap_path.size() + 8);
		res.append(removeDotAndExtension(base_lightmap_path));
		res.append("_lod1.");
		res.append(getExtensionStringView(base_lightmap_path));
		return res;
	}
	else
	{
		OpenGLTextureKey res(base_lightmap_path.get_allocator());
		res.reserve(base_lightmap_path.size() + 8);
		res.append(removeDotAndExtension(base_lightmap_path));
		res.append("_lod2.");
		res.append(getExtensionStringView(base_lightmap_path));
		return res;
	}
}


void WorldObject::appendDependencyURLs(int ob_lod_level, const GetDependencyOptions& options, DependencyURLVector& URLs_out) const
{
	if(!model_url.empty())
	{
		const int ob_model_lod_level =  myClamp(ob_lod_level, 0, this->max_model_lod_level);

		GetLODModelURLOptions url_options(/*get_optimised_mesh=*/options.get_optimised_mesh, options.opt_mesh_version);
		url_options.allocator = options.allocator;
		URLs_out.push_back(DependencyURL(getLODModelURLForLevel(model_url, ob_model_lod_level, url_options)));
	}

	if(options.include_lightmaps && !lightmap_url.empty())
	{
		DependencyURL dependency_url(getLODLightmapURLForLevel(lightmap_url, ob_lod_level));
		dependency_url.is_lightmap = true;
		URLs_out.push_back(dependency_url);
	}

	const WorldMaterial::GetURLOptions mat_get_url_options(options.use_basis, options.allocator);

	const size_t materials_size = materials.size();
	for(size_t i=0; i<materials_size; ++i)
		materials[i]->appendDependencyURLs(mat_get_url_options, ob_lod_level, URLs_out);

	if(!audio_source_url.empty())
	{
		glare::STLArenaAllocator<char> stl_allocator(options.allocator);
		URLs_out.push_back(DependencyURL(URLString(audio_source_url, stl_allocator)));
		
		//URLs_out.push_back(DependencyURL(audio_source_url));
	}
}


void WorldObject::appendDependencyURLsForAllLODLevels(const GetDependencyOptions& options, DependencyURLVector& URLs_out) const
{
	if(!model_url.empty())
	{
		GetLODModelURLOptions url_options(/*get_optimised_mesh=*/options.get_optimised_mesh, options.opt_mesh_version);

		URLs_out.push_back(DependencyURL(getLODModelURLForLevel(model_url, 0, url_options)));
		if(max_model_lod_level > 0)
		{
			URLs_out.push_back(DependencyURL(getLODModelURLForLevel(model_url, 1, url_options)));
			URLs_out.push_back(DependencyURL(getLODModelURLForLevel(model_url, 2, url_options)));
		}
	}

	if(!lightmap_url.empty())
		for(int lvl=0; lvl<=2; ++lvl)
		{
			DependencyURL dependency_url(getLODLightmapURLForLevel(lightmap_url, lvl));
			dependency_url.is_lightmap = true;
			URLs_out.push_back(dependency_url);
		}

	const WorldMaterial::GetURLOptions mat_get_url_options(options.use_basis, options.allocator);

	for(size_t i=0; i<materials.size(); ++i)
		materials[i]->appendDependencyURLsAllLODLevels(mat_get_url_options, URLs_out);

	if(!audio_source_url.empty())
		URLs_out.push_back(DependencyURL(audio_source_url));
}


void WorldObject::appendDependencyURLsBaseLevel(const GetDependencyOptions& options, DependencyURLVector& URLs_out) const
{
	if(!model_url.empty())
	{
		GetLODModelURLOptions url_options(/*get_optimised_mesh=*/options.get_optimised_mesh, options.opt_mesh_version);

		URLs_out.push_back(DependencyURL(getLODModelURLForLevel(model_url, 0, url_options)));
	}

	if(options.include_lightmaps && !lightmap_url.empty())
	{
		DependencyURL dependency_url(lightmap_url);
		dependency_url.is_lightmap = true;
		URLs_out.push_back(dependency_url);
	}

	const WorldMaterial::GetURLOptions mat_get_url_options(options.use_basis, options.allocator);

	for(size_t i=0; i<materials.size(); ++i)
		materials[i]->appendDependencyURLsBaseLevel(mat_get_url_options, URLs_out);

	if(!audio_source_url.empty())
		URLs_out.push_back(DependencyURL(audio_source_url));
}


void WorldObject::getDependencyURLSet(int ob_lod_level, const GetDependencyOptions& options, DependencyURLSet& URLS_out) const
{
	glare::STLArenaAllocator<DependencyURL> stl_allocator(options.allocator);
	DependencyURLVector URLs(stl_allocator);

	this->appendDependencyURLs(ob_lod_level, options, URLs);

	URLS_out.clear();
	URLS_out.insert(URLs.begin(), URLs.end());
}


void WorldObject::getDependencyURLSetForAllLODLevels(const GetDependencyOptions& options, DependencyURLSet& URLS_out) const
{
	glare::STLArenaAllocator<DependencyURL> stl_allocator(options.allocator);
	DependencyURLVector URLs(stl_allocator);

	this->appendDependencyURLsForAllLODLevels(options, URLs);

	URLS_out.clear();
	URLS_out.insert(URLs.begin(), URLs.end());
}


void WorldObject::getDependencyURLSetBaseLevel(const GetDependencyOptions& options, DependencyURLSet& URLS_out) const
{
	glare::STLArenaAllocator<DependencyURL> stl_allocator(options.allocator);
	DependencyURLVector URLs(stl_allocator);

	this->appendDependencyURLsBaseLevel(options, URLs);

	URLS_out.clear();
	URLS_out.insert(URLs.begin(), URLs.end());
}


void WorldObject::convertLocalPathsToURLS(ResourceManager& resource_manager)
{
	if(FileUtils::fileExists(this->model_url)) // If the URL is a local path:
		this->model_url = resource_manager.URLForPathAndHash(toStdString(this->model_url), FileChecksum::fileChecksum(this->model_url));

	for(size_t i=0; i<materials.size(); ++i)
		materials[i]->convertLocalPathsToURLS(resource_manager);

	if(FileUtils::fileExists(this->lightmap_url)) // If the URL is a local path:
		this->lightmap_url = resource_manager.URLForPathAndHash(toStdString(this->lightmap_url), FileChecksum::fileChecksum(this->lightmap_url));

	if(FileUtils::fileExists(this->audio_source_url)) // If the URL is a local path:
		this->audio_source_url = resource_manager.URLForPathAndHash(toStdString(this->audio_source_url), FileChecksum::fileChecksum(this->audio_source_url));
}


void WorldObject::setTransformAndHistory(const Vec3d& pos_, const Vec3f& axis_, float angle_)
{
	assert(isFinite(angle_));

	pos = pos_;
	axis = axis_;
	angle = angle_;

	const Quatf rot_quat = Quatf::fromAxisAndAngle(normalise(axis_), angle_);

	for(int i=0; i<HISTORY_BUF_SIZE; ++i)
		snapshots[i] = Snapshot({pos_.toVec4fPoint(), rot_quat, /*linear vel=*/Vec4f(0.f), /*angular_vel=*/Vec4f(0.f), /*client time=*/0.0, /*local time=*/0.0});
}


void WorldObject::setPosAndHistory(const Vec3d& pos_)
{
	pos = pos_;
#if GUI_CLIENT
	//last_pos = pos_;
#endif

	for(int i=0; i<HISTORY_BUF_SIZE; ++i)
		snapshots[i].pos = pos_.toVec4fPoint();
}


void WorldObject::getInterpolatedTransform(double cur_time, Vec3d& pos_out, Quatf& rot_out) const
{
	/*
	Timeline: check marks are snapshots received:

	|---------------|----------------|---------------|----------------|
	                                                                       ^
	                                                                      cur_time
	                                                                  ^
	                                               ^                last snapshot
	                                             cur_time - send_period * delay_factor


	When 'frac' gets > 1:
	|---------------|----------------|---------------|----------------x
	                                                                       ^
	                                                                      cur_time
	                                                 ^
	                                     ^         last snapshot
	                                   cur_time - send_period * delay_factor


	 So should be alright as long as it doesn't exceed 2.
	
	|---------------|----------------|---------------|---------------------------|
	                                                                       ^
	                                                                      cur_time
	                                                 ^
	                                     ^         last snapshot
	                                   cur_time - send_period * delay_factor

	
	*/


	const double send_period = 0.1; // Time between update messages from server
	const double delay = send_period * 2.0; // Objects are rendered using the interpolated state at this past time.

	const double delayed_time = cur_time - delay;
	// Search through history for first snapshot with time > delayed_time
	int begin = 0;
	for(int i=(int)next_snapshot_i-HISTORY_BUF_SIZE; i<(int)next_snapshot_i; ++i)
	{
		const int modi = Maths::intMod(i, HISTORY_BUF_SIZE);
		if(snapshots[modi].local_time > delayed_time)
		{
			begin = Maths::intMod(modi - 1, HISTORY_BUF_SIZE);
			break;
		}
	}

	const int end = Maths::intMod(begin + 1, HISTORY_BUF_SIZE);

	// Snapshot times may be the same if we haven't received updates for this object yet.
	const float t  = myClamp<float>(
		(snapshots[end].local_time == snapshots[begin].local_time) ? 0.f : (float)((delayed_time - snapshots[begin].local_time) / (snapshots[end].local_time - snapshots[begin].local_time)),
		0.f, 1.f); // Interpolation fraction

	pos_out = Vec3d(Maths::uncheckedLerp(snapshots[begin].pos, snapshots[end].pos, t));
	rot_out = Quatf::nlerp(snapshots[begin].rotation, snapshots[end].rotation, t);
}


std::string WorldObject::objectTypeString(ObjectType t)
{
	switch(t)
	{
	case ObjectType_Generic: return "generic";
	case ObjectType_Hypercard: return "hypercard";
	case ObjectType_VoxelGroup: return "voxel group";
	case ObjectType_Spotlight: return "spotlight";
	case ObjectType_WebView: return "web view";
	case ObjectType_Video: return "video";
	case ObjectType_Text: return "text";
	default: return "Unknown";
	}
}

WorldObject::ObjectType WorldObject::objectTypeForString(const std::string& ob_type_string)
{
	if(ob_type_string == "generic") return ObjectType_Generic;
	if(ob_type_string == "hypercard") return ObjectType_Hypercard;
	if(ob_type_string == "voxel group") return ObjectType_VoxelGroup;
	if(ob_type_string == "spotlight") return ObjectType_Spotlight;
	if(ob_type_string == "web view") return ObjectType_WebView;
	if(ob_type_string == "video") return ObjectType_Video;
	if(ob_type_string == "text") return ObjectType_Text;
	throw glare::Exception("Unknown object type '" + ob_type_string + "'");
}


static const uint32 WORLD_OBJECT_SERIALISATION_VERSION = 21;
/*
Version history:
9: introduced voxels
10: changed script_url to script
11: Added flags
12: Added compressed voxel field.
13: Added lightmap URL
14: Added aabb_ws
15: Added max_lod_level
16: Added audio_source_url, audio_volume
17: Added mass, friction, restitution
18: Storing aabb_os instead of aabb_ws.
19: Added last_modified_time
20: Added centre_of_mass_offset_os
21: Added chunk_batch0_start etc.
*/


static_assert(sizeof(Voxel) == sizeof(int)*4, "sizeof(Voxel) == sizeof(int)*4");


void WorldObject::writeToStream(RandomAccessOutStream& stream) const
{
	// Write version
	stream.writeUInt32(WORLD_OBJECT_SERIALISATION_VERSION);

	::writeToStream(uid, stream);
	stream.writeUInt32((uint32)object_type);
	stream.writeStringLengthFirst(model_url);

	// Write materials
	stream.writeUInt32((uint32)materials.size());
	for(size_t i=0; i<materials.size(); ++i)
		::writeWorldMaterialToStream(*materials[i], stream);

	stream.writeStringLengthFirst(lightmap_url); // new in v13

	stream.writeStringLengthFirst(script);
	stream.writeStringLengthFirst(content);
	stream.writeStringLengthFirst(target_url);
	stream.writeStringLengthFirst(audio_source_url);
	stream.writeFloat(audio_volume);

	::writeToStream(pos, stream);
	::writeToStream(axis, stream);
	stream.writeFloat(angle);
	::writeToStream(scale, stream);

	created_time.writeToStream(stream); // new in v5
	last_modified_time.writeToStream(stream); // new in v19
	::writeToStream(creator_id, stream); // new in v5

	stream.writeUInt32(flags); // new in v11

	stream.writeData(aabb_os.min_.x, sizeof(float) * 3); // new in v14.  v18: write aabb_os instead of aabb_ws.
	stream.writeData(aabb_os.max_.x, sizeof(float) * 3);

	stream.writeInt32(max_model_lod_level); // new in v15

	if(object_type == WorldObject::ObjectType_VoxelGroup)
	{
		// Write compressed voxel data
		if(compressed_voxels)
		{
			stream.writeUInt32((uint32)compressed_voxels->size());
			if(compressed_voxels->size() > 0)
				stream.writeData(compressed_voxels->data(), compressed_voxels->dataSizeBytes());
		}
		else
			stream.writeUInt32(0);
	}

	// New in v17:
	stream.writeFloat(mass);
	stream.writeFloat(friction);
	stream.writeFloat(restitution);
	::writeToStream(centre_of_mass_offset_os, stream); // New in v20

	// New in v21:
	stream.writeUInt32(chunk_batch0_start);
	stream.writeUInt32(chunk_batch0_end);
	stream.writeUInt32(chunk_batch1_start);
	stream.writeUInt32(chunk_batch1_end);
}


void readWorldObjectFromStream(RandomAccessInStream& stream, WorldObject& ob)
{
	// Read version
	const uint32 v = stream.readUInt32();
	if(v > WORLD_OBJECT_SERIALISATION_VERSION)
		throw glare::Exception("Unsupported version " + toString(v) + ", expected " + toString(WORLD_OBJECT_SERIALISATION_VERSION) + ".");

	ob.uid = readUIDFromStream(stream);

	if(v >= 7)
		ob.object_type = (WorldObject::ObjectType)stream.readUInt32(); // TODO: handle invalid values?

	ob.model_url = stream.readStringLengthFirst(WorldObject::MAX_URL_SIZE);
	//if(v >= 2)
	//	ob.material_url = stream.readStringLengthFirst(10000);
	if(v >= 2)
	{
		const size_t num_mats = stream.readUInt32();
		if(num_mats > WorldObject::maxNumMaterials())
			throw glare::Exception("Too many materials: " + toString(num_mats));
		ob.materials.resize(num_mats);
		for(size_t i=0; i<ob.materials.size(); ++i)
		{
			if(ob.materials[i].isNull())
				ob.materials[i] = new WorldMaterial();
			::readWorldMaterialFromStream(stream, *ob.materials[i]);
		}
	}

	if(v >= 13)
	{
		ob.lightmap_url = stream.readStringLengthFirst(WorldObject::MAX_URL_SIZE);
		//conPrint("readFromStream: read lightmap_url: " + ob.lightmap_url);
	}

	if(v >= 4 && v < 10)
	{
		static_cast<void>(stream.readStringLengthFirst(WorldObject::MAX_URL_SIZE)); // read and discard script URL.  static_cast<void> to suppress nodiscard warning.
	}
	else if(v >= 10)
	{
		ob.script = stream.readStringLengthFirst(WorldObject::MAX_SCRIPT_SIZE);
	}

	if(v >= 6)
		ob.content = stream.readStringLengthFirst(WorldObject::MAX_CONTENT_SIZE);

	if(v >= 8)
		ob.target_url = stream.readStringLengthFirst(WorldObject::MAX_URL_SIZE);
	
	if(v >= 16)
	{
		ob.audio_source_url = stream.readStringLengthFirst(WorldObject::MAX_URL_SIZE);
		ob.audio_volume = stream.readFloat();
	}

	ob.pos = readVec3FromStream<double>(stream);
	ob.axis = readVec3FromStream<float>(stream);
	ob.angle = stream.readFloat();

	if(v >= 3)
		ob.scale = readVec3FromStream<float>(stream);
	else
		ob.scale = Vec3f(1.f);

	if(v >= 5)
	{
		ob.created_time.readFromStream(stream);
		if(v >= 19)
			ob.last_modified_time.readFromStream(stream);
		else
			ob.last_modified_time = ob.created_time;
		ob.creator_id = readUserIDFromStream(stream);
	}
	else
	{
		ob.created_time = TimeStamp::currentTime();
		ob.creator_id = UserID::invalidUserID();
	}

	if(v >= 11)
		ob.flags = stream.readUInt32();

	if(v >= 14)
	{
		js::AABBox aabb;
		stream.readData(aabb.min_.x, sizeof(float) * 3);
		aabb.min_.x[3] = 1.f;
		stream.readData(aabb.max_.x, sizeof(float) * 3);
		aabb.max_.x[3] = 1.f;

		if(!aabb.min_.isFinite() || !aabb.max_.isFinite())
			aabb = js::AABBox(Vec4f(0,0,0,1), Vec4f(0,0,0,1));


		// Some code for allowing fuzzing to proceed without hitting asserts:
		/*if(ob.axis.length2() < Maths::square(1.0e-10f))
		{
			ob.axis = Vec3f(0,0,1);
			ob.angle = 0;
		}

		if(!(ob.axis.isFinite() && isFinite(ob.angle) && ob.scale.isFinite() && ob.translation.isFinite()))
		{
			ob.axis = Vec3f(0,0,1);
			ob.angle = 0;
			ob.scale = Vec3f(1.f);
			ob.translation = Vec4f(0,0,0,1);
		}*/


		if(v >= 18)
		{
			ob.setAABBOS(aabb);
		}
		else
		{
			if(ob.axis.isFinite() && isFinite(ob.angle) && ob.scale.isFinite() && ob.translation.isFinite())
			{
				// Get approx aabb os from aabb ws.
				const js::AABBox aabb_os = aabb.transformedAABBFast(ob.worldToObMatrix());
				ob.setAABBOS(aabb_os);
			}
			else
			{
				// Can't call setAABBOS() because it will hit an assert in transformChanged() when computing the transformation matrix.
				// conPrint("Invalid transform");
				ob.zeroAABBOS();
			}
		}
	}

	if(v >= 15)
		ob.max_model_lod_level = stream.readInt32();
	
	if(v >= 9 && ob.object_type == WorldObject::ObjectType_VoxelGroup)
	{
		if(v <= 11)
		{
			// Read num voxels
			const uint32 num_voxels = stream.readUInt32();
			if(num_voxels > 1000000)
				throw glare::Exception("Invalid num voxels: " + toString(num_voxels));

			ob.getDecompressedVoxels().resize(num_voxels);

			// Read voxel data
			if(num_voxels > 0)
				stream.readData(ob.getDecompressedVoxels().data(), sizeof(Voxel) * num_voxels);
		}
		else
		{
			// Read compressed voxel data
			const uint32 voxel_data_size = stream.readUInt32();
			if(voxel_data_size > 1000000)
				throw glare::Exception("Invalid voxel_data_size: " + toString(voxel_data_size));

			// Read voxel data
			Reference<glare::SharedImmutableArray<uint8> > compressed_voxels = new glare::SharedImmutableArray<uint8>();
			compressed_voxels->resizeNoCopy(voxel_data_size);
			if(voxel_data_size > 0)
				stream.readData(compressed_voxels->data(), voxel_data_size);

			ob.setCompressedVoxels(compressed_voxels);
		}
	}

	if(v >= 17)
	{
		ob.mass = stream.readFloat();
		ob.friction = stream.readFloat();
		ob.restitution = stream.readFloat();
	}

	if(v >= 20)
		ob.centre_of_mass_offset_os = readVec3FromStream<float>(stream);

	if(v >= 21)
	{
		// New in v21:
		ob.chunk_batch0_start = stream.readUInt32();
		ob.chunk_batch0_end = stream.readUInt32();
		ob.chunk_batch1_start = stream.readUInt32();
		ob.chunk_batch1_end = stream.readUInt32();
	}

	// Set ephemeral state
	ob.state = WorldObject::State_Alive;
}


void WorldObject::writeToNetworkStream(RandomAccessOutStream& stream) const // Write without version
{
	::writeToStream(uid, stream);
	stream.writeUInt32((uint32)object_type);
	stream.writeStringLengthFirst(model_url);

	// Write materials
	stream.writeUInt32((uint32)materials.size());
	for(size_t i=0; i<materials.size(); ++i)
		::writeWorldMaterialToStream(*materials[i], stream);

	stream.writeStringLengthFirst(lightmap_url); // new in v13

	stream.writeStringLengthFirst(script);
	stream.writeStringLengthFirst(content);
	stream.writeStringLengthFirst(target_url);
	stream.writeStringLengthFirst(audio_source_url);
	stream.writeFloat(audio_volume);

	::writeToStream(pos, stream);
	::writeToStream(axis, stream);
	stream.writeFloat(angle);
	::writeToStream(scale, stream);

	created_time.writeToStream(stream); // new in v5
	last_modified_time.writeToStream(stream); // new in v19
	::writeToStream(creator_id, stream); // new in v5

	stream.writeUInt32(flags); // new in v11

	stream.writeStringLengthFirst(creator_name);

	stream.writeData(aabb_os.min_.x, sizeof(float) * 3); // new in v14
	stream.writeData(aabb_os.max_.x, sizeof(float) * 3);

	stream.writeInt32(max_model_lod_level); // new in v15

	if(object_type == WorldObject::ObjectType_VoxelGroup)
	{
		// Write compressed voxel data
		if(compressed_voxels)
		{
			stream.writeUInt32((uint32)compressed_voxels->size());
			if(compressed_voxels->size() > 0)
				stream.writeData(compressed_voxels->data(), compressed_voxels->dataSizeBytes());
		}
		else
			stream.writeUInt32(0);
	}

	// New in v17:
	stream.writeFloat(mass);
	stream.writeFloat(friction);
	stream.writeFloat(restitution);

	// Physics owner id has to be transmitted, or a new client joining will not be aware of who the physics owner of a moving object is, and will incorrectly take ownership of it.
	stream.writeUInt32(physics_owner_id);
	stream.writeDouble(last_physics_ownership_change_global_time);

	// New in v20:
	::writeToStream(centre_of_mass_offset_os, stream);

	// New in v21:
	stream.writeUInt32(chunk_batch0_start);
	stream.writeUInt32(chunk_batch0_end);
	stream.writeUInt32(chunk_batch1_start);
	stream.writeUInt32(chunk_batch1_end);
}


void WorldObject::copyNetworkStateFrom(const WorldObject& other)
{
	// NOTE: The data in here needs to match that in readFromNetworkStreamGivenUID()
	object_type = other.object_type;
	model_url = other.model_url;
	materials = other.materials;

	lightmap_url = other.lightmap_url;

	script = other.script;
	content = other.content;
	target_url = other.target_url;
	audio_source_url = other.audio_source_url;
	audio_volume = other.audio_volume;

	pos = other.pos;
	axis = other.axis;
	angle = other.angle;

	scale = other.scale;

	created_time = other.created_time;
	last_modified_time = other.last_modified_time;
	creator_id = other.creator_id;

	flags = other.flags;

	creator_name = other.creator_name;

	compressed_voxels = other.compressed_voxels;

	aabb_os = other.aabb_os;

	max_model_lod_level = other.max_model_lod_level;

	mass = other.mass;
	friction = other.friction;
	restitution = other.restitution;
	centre_of_mass_offset_os = other.centre_of_mass_offset_os;

	chunk_batch0_start = other.chunk_batch0_start;
	chunk_batch0_end = other.chunk_batch0_end;
	chunk_batch1_start = other.chunk_batch1_start;
	chunk_batch1_end = other.chunk_batch1_end;

	physics_owner_id = other.physics_owner_id;
	last_physics_ownership_change_global_time = other.last_physics_ownership_change_global_time;



	exclude_from_lod_chunk_mesh = BitUtils::isBitSet(flags, WorldObject::EXCLUDE_FROM_LOD_CHUNK_MESH);
}


std::string WorldObject::serialiseToXML(int tab_depth) const
{
	std::string s;
	s.reserve(2048);

	s += std::string(tab_depth, '\t') + "<object>\n";

	XMLWriteUtils::writeUInt64ToXML(s, "uid", uid.value(), tab_depth + 1);
	XMLWriteUtils::writeStringElemToXML(s, "object_type", objectTypeString(object_type), tab_depth + 1);
	XMLWriteUtils::writeStringElemToXML(s, "model_url", model_url, tab_depth + 1);

	// Write materials
	s += std::string(tab_depth + 1, '\t') + "<materials>\n";
	for(size_t i=0; i<materials.size(); ++i)
		s += materials[i]->serialiseToXML(tab_depth + 2);
	s += std::string(tab_depth + 1, '\t') + "</materials>\n";

	XMLWriteUtils::writeStringElemToXML(s, "lightmap_url", lightmap_url, tab_depth + 1);

	XMLWriteUtils::writeStringElemToXML(s, "script", script, tab_depth + 1);
	XMLWriteUtils::writeStringElemToXML(s, "content", content, tab_depth + 1);
	XMLWriteUtils::writeStringElemToXML(s, "target_url", target_url, tab_depth + 1);
	XMLWriteUtils::writeStringElemToXML(s, "audio_source_url", audio_source_url, tab_depth + 1);

	XMLWriteUtils::writeFloatToXML(s, "audio_volume", audio_volume, tab_depth + 1);

	XMLWriteUtils::writeVec3ToXML(s, "pos", pos, tab_depth + 1);
	XMLWriteUtils::writeVec3ToXML(s, "axis", axis, tab_depth + 1);
	XMLWriteUtils::writeFloatToXML(s, "angle", angle, tab_depth + 1);
	XMLWriteUtils::writeVec3ToXML(s, "scale", scale, tab_depth + 1);

	XMLWriteUtils::writeUInt64ToXML(s, "created_time", created_time.time, tab_depth + 1);
	XMLWriteUtils::writeUInt64ToXML(s, "last_modified_time", last_modified_time.time, tab_depth + 1);
	XMLWriteUtils::writeUInt32ToXML(s, "creator_id", creator_id.value(), tab_depth + 1);

	XMLWriteUtils::writeUInt32ToXML(s, "flags", flags, tab_depth + 1);

	XMLWriteUtils::writeVec3ToXML(s, "aabb_os_min", Vec3f(aabb_os.min_), tab_depth + 1);
	XMLWriteUtils::writeVec3ToXML(s, "aabb_os_max", Vec3f(aabb_os.max_), tab_depth + 1);

	XMLWriteUtils::writeInt32ToXML(s, "max_model_lod_level", max_model_lod_level, tab_depth + 1);

	if(object_type == WorldObject::ObjectType_VoxelGroup)
	{
		std::string encoded;
		if(compressed_voxels)
			Base64::encode(compressed_voxels->data(), compressed_voxels->size(), encoded);

		XMLWriteUtils::writeStringElemToXML(s, "compressed_voxels_base64", encoded, tab_depth + 1);
	}

	XMLWriteUtils::writeFloatToXML(s, "mass", mass, tab_depth + 1);
	XMLWriteUtils::writeFloatToXML(s, "friction", friction, tab_depth + 1);
	XMLWriteUtils::writeFloatToXML(s, "restitution", restitution, tab_depth + 1);

	XMLWriteUtils::writeVec3ToXML(s, "centre_of_mass_offset_os", centre_of_mass_offset_os, tab_depth + 1);

	XMLWriteUtils::writeUInt32ToXML(s, "chunk_batch0_start", chunk_batch0_start, tab_depth + 1);
	XMLWriteUtils::writeUInt32ToXML(s, "chunk_batch0_end", chunk_batch0_end, tab_depth + 1);
	XMLWriteUtils::writeUInt32ToXML(s, "chunk_batch1_start", chunk_batch1_start, tab_depth + 1);
	XMLWriteUtils::writeUInt32ToXML(s, "chunk_batch1_end", chunk_batch1_end, tab_depth + 1);

	s += std::string(tab_depth, '\t') + "</object>\n";
	return s;
}


Reference<WorldObject> WorldObject::loadFromXMLElem(const std::string& object_file_path, bool convert_rel_paths_to_abs_disk_paths, pugi::xml_node elem)
{
	Reference<WorldObject> ob = new WorldObject();
	ob->uid = UID(XMLParseUtils::parseUInt64WithDefault(elem, "uid", UID::invalidUID().value()));
	
	const std::string ob_type_str = XMLParseUtils::parseStringWithDefault(elem, "object_type", "generic");
	ob->object_type = objectTypeForString(ob_type_str);
	
	ob->model_url = XMLParseUtils::parseStringWithDefault(elem, "model_url", "");

	if(pugi::xml_node materials_node = elem.child("materials"))
	{
		for(pugi::xml_node mat_node = materials_node.child("material"); mat_node; mat_node = mat_node.next_sibling("material"))
		{
			ob->materials.push_back(WorldMaterial::loadFromXMLElem(object_file_path, convert_rel_paths_to_abs_disk_paths, mat_node));
		}
	}

	ob->lightmap_url = XMLParseUtils::parseStringWithDefault(elem, "lightmap_url", "");

	ob->script           = XMLParseUtils::parseStringWithDefault(elem, "script", "");
	ob->content          = XMLParseUtils::parseStringWithDefault(elem, "content", "");
	ob->target_url       = XMLParseUtils::parseStringWithDefault(elem, "target_url", "");
	ob->audio_source_url = XMLParseUtils::parseStringWithDefault(elem, "audio_source_url", "");

	ob->audio_volume = XMLParseUtils::parseFloatWithDefault(elem, "audio_volume", 1.f);

	ob->pos   = XMLParseUtils::parseVec3d(elem, "pos");
	ob->axis  = XMLParseUtils::parseVec3fWithDefault(elem, "axis", Vec3f(0,0,1));
	ob->angle = XMLParseUtils::parseFloatWithDefault(elem, "angle", 0.0);
	ob->scale = XMLParseUtils::parseVec3fWithDefault(elem, "scale", Vec3f(1,1,1));

	ob->created_time       = TimeStamp(XMLParseUtils::parseUInt64WithDefault(elem, "created_time", 0));
	ob->last_modified_time = TimeStamp(XMLParseUtils::parseUInt64WithDefault(elem, "last_modified_time", 0));

	ob->creator_id = UserID((uint32)XMLParseUtils::parseUInt64WithDefault(elem, "creator_id", UserID::invalidUserID().value()));

	ob->flags = (uint32)XMLParseUtils::parseUInt64WithDefault(elem, "flags", 0);

	ob->aabb_os.min_ = XMLParseUtils::parseVec3fWithDefault(elem, "aabb_os_min", Vec3f(0,0,0)).toVec4fPoint();
	ob->aabb_os.max_ = XMLParseUtils::parseVec3fWithDefault(elem, "aabb_os_max", Vec3f(0,0,0)).toVec4fPoint();

	ob->max_model_lod_level = XMLParseUtils::parseIntWithDefault(elem, "max_model_lod_level", 0);

	if(pugi::xml_node compressed_voxels_base64_node = elem.child("compressed_voxels_base64"))
	{
		const string_view compressed_voxels_base64_str(compressed_voxels_base64_node.child_value(), std::strlen(compressed_voxels_base64_node.child_value()));

		std::vector<unsigned char> decoded_data;
		Base64::decode(compressed_voxels_base64_str, decoded_data);

		if(decoded_data.size() > 0)
		{
			Reference<glare::SharedImmutableArray<uint8> > compressed_voxels = new glare::SharedImmutableArray<uint8>();
			compressed_voxels->resizeNoCopy(decoded_data.size());
			std::memcpy(compressed_voxels->data(), decoded_data.data(), decoded_data.size());

			ob->setCompressedVoxels(compressed_voxels);
		}
	}

	ob->mass =        XMLParseUtils::parseFloatWithDefault(elem, "mass", 50.0);
	ob->friction =    XMLParseUtils::parseFloatWithDefault(elem, "friction", 0.5);
	ob->restitution = XMLParseUtils::parseFloatWithDefault(elem, "restitution", 0.2);

	ob->centre_of_mass_offset_os = XMLParseUtils::parseVec3fWithDefault(elem, "centre_of_mass_offset_os", Vec3f(0,0,0));

	ob->chunk_batch0_start = (uint32)XMLParseUtils::parseUInt64WithDefault(elem, "chunk_batch0_start", 0);
	ob->chunk_batch0_end   = (uint32)XMLParseUtils::parseUInt64WithDefault(elem, "chunk_batch0_end", 0);
	ob->chunk_batch1_start = (uint32)XMLParseUtils::parseUInt64WithDefault(elem, "chunk_batch1_start", 0);
	ob->chunk_batch1_end   = (uint32)XMLParseUtils::parseUInt64WithDefault(elem, "chunk_batch1_end", 0);

	return ob;
}


void readWorldObjectFromNetworkStreamGivenUID(RandomAccessInStream& stream, WorldObject& ob) // UID will have been read already
{
	// NOTE: The data in here needs to match that in copyNetworkStateFrom()

	ob.object_type = (WorldObject::ObjectType)stream.readUInt32(); // TODO: handle invalid values?
	ob.model_url = stream.readStringLengthFirst(WorldObject::MAX_URL_SIZE);
	//if(v >= 2)
	{
		const size_t num_mats = stream.readUInt32();
		if(num_mats > WorldObject::maxNumMaterials())
			throw glare::Exception("Too many materials: " + toString(num_mats));
		ob.materials.resize(num_mats);
		for(size_t i=0; i<ob.materials.size(); ++i)
		{
			if(ob.materials[i].isNull())
				ob.materials[i] = new WorldMaterial();
			readWorldMaterialFromStream(stream, *ob.materials[i]);
		}
	}

	ob.lightmap_url = stream.readStringLengthFirst(WorldObject::MAX_URL_SIZE);

	const std::string new_script = stream.readStringLengthFirst(WorldObject::MAX_SCRIPT_SIZE);
	if(ob.script != new_script)
		ob.changed_flags |= WorldObject::SCRIPT_CHANGED;
	ob.script = new_script;

	if(ob.content.empty())
	{
		ob.content = stream.readStringLengthFirst(WorldObject::MAX_CONTENT_SIZE);
		if(!ob.content.empty())
			ob.changed_flags |= WorldObject::CONTENT_CHANGED;
	}
	else
	{
		const std::string new_content = stream.readStringLengthFirst(WorldObject::MAX_CONTENT_SIZE);
		if(ob.content != new_content)
			ob.changed_flags |= WorldObject::CONTENT_CHANGED;
		ob.content = new_content;
	}

	ob.target_url = stream.readStringLengthFirst(WorldObject::MAX_URL_SIZE);

	const URLString new_audio_source_url = toURLString(stream.readStringLengthFirst(WorldObject::MAX_URL_SIZE));
	if(ob.audio_source_url != new_audio_source_url)
		ob.changed_flags |= WorldObject::AUDIO_SOURCE_URL_CHANGED;
	ob.audio_source_url = new_audio_source_url;
	ob.audio_volume = stream.readFloat();

	ob.pos = readVec3FromStream<double>(stream);
	ob.axis = readVec3FromStream<float>(stream);
	ob.angle = stream.readFloat();

	if(!ob.pos.isFinite())
		ob.pos = Vec3d(0,0,0);
	if(!ob.axis.isFinite())
		ob.axis = Vec3f(1,0,0);
	if(!isFinite(ob.angle))
		ob.angle = 0;

	//if(v >= 3)
	ob.scale = readVec3FromStream<float>(stream);

	ob.created_time.readFromStream(stream);
	ob.last_modified_time.readFromStream(stream);
	ob.creator_id = readUserIDFromStream(stream);

	ob.flags = stream.readUInt32();

	ob.creator_name = stream.readStringLengthFirst(10000);

	js::AABBox aabb;
	stream.readData(aabb.min_.x, sizeof(float) * 3);
	aabb.min_.x[3] = 1.f;
	stream.readData(aabb.max_.x, sizeof(float) * 3);
	aabb.max_.x[3] = 1.f;

	if(!aabb.min_.isFinite() || !aabb.max_.isFinite())
		aabb = js::AABBox(Vec4f(0,0,0,1), Vec4f(0,0,0,1));

	ob.setAABBOS(aabb);

	ob.max_model_lod_level = stream.readInt32();

	if(ob.object_type == WorldObject::ObjectType_VoxelGroup)
	{
		// Read compressed voxel data
		const uint32 voxel_data_size = stream.readUInt32();
		if(voxel_data_size > 1000000)
			throw glare::Exception("Invalid voxel_data_size (too large): " + toString(voxel_data_size));

		// Read voxel data
		Reference<glare::SharedImmutableArray<uint8> > compressed_voxels = new glare::SharedImmutableArray<uint8>();

		compressed_voxels->resizeNoCopy(voxel_data_size);
		if(voxel_data_size > 0)
			stream.readData(compressed_voxels->data(), voxel_data_size);
		ob.setCompressedVoxels(compressed_voxels);
	}

	// New in v17:
	if(!stream.endOfStream())
	{
		ob.mass = stream.readFloat();
		ob.friction = stream.readFloat();
		ob.restitution = stream.readFloat();
	}

	if(!stream.endOfStream())
	{
		const uint32 new_physics_owner_id = stream.readUInt32();
		if(new_physics_owner_id != ob.physics_owner_id)
			ob.changed_flags |= WorldObject::PHYSICS_OWNER_CHANGED;
		ob.physics_owner_id = new_physics_owner_id;
	}
	
	if(!stream.endOfStream())
		ob.last_physics_ownership_change_global_time = stream.readDouble();

	if(!stream.endOfStream())
		ob.centre_of_mass_offset_os = readVec3FromStream<float>(stream);

	if(!stream.endOfStream())
	{
		ob.chunk_batch0_start = stream.readUInt32();
		ob.chunk_batch0_end = stream.readUInt32();
		ob.chunk_batch1_start = stream.readUInt32();
		ob.chunk_batch1_end = stream.readUInt32();
	}

	// Set ephemeral state
	//ob.state = WorldObject::State_Alive;

	ob.exclude_from_lod_chunk_mesh = BitUtils::isBitSet(ob.flags, WorldObject::EXCLUDE_FROM_LOD_CHUNK_MESH);
}


const Vec3f useScaleForWorldOb(const Vec3f& scale)
{
	// Don't use a zero scale component, because it makes the matrix uninvertible, which breaks various things, including picking and normals.
	Vec3f use_scale = scale;
	if(std::fabs(use_scale.x) < 1.0e-6f) use_scale.x = 1.0e-6f;
	if(std::fabs(use_scale.y) < 1.0e-6f) use_scale.y = 1.0e-6f;
	if(std::fabs(use_scale.z) < 1.0e-6f) use_scale.z = 1.0e-6f;
	return use_scale;
}


const Matrix4f obToWorldMatrix(const WorldObject& ob)
{
	const Vec4f pos((float)ob.pos.x, (float)ob.pos.y, (float)ob.pos.z, 1.f);

	// Don't use a zero scale component, because it makes the matrix uninvertible, which breaks various things, including picking and normals.
	Vec3f use_scale = ob.scale;
	if(std::fabs(use_scale.x) < 1.0e-6f) use_scale.x = 1.0e-6f;
	if(std::fabs(use_scale.y) < 1.0e-6f) use_scale.y = 1.0e-6f;
	if(std::fabs(use_scale.z) < 1.0e-6f) use_scale.z = 1.0e-6f;

	// Equivalent to
	//return Matrix4f::translationMatrix(pos + ob.translation) *
	//	Matrix4f::rotationMatrix(normalise(ob.axis.toVec4fVector()), ob.angle) *
	//	Matrix4f::scaleMatrix(use_scale.x, use_scale.y, use_scale.z));

	Matrix4f rot = Matrix4f::rotationMatrix(normalise(ob.axis.toVec4fVector()), ob.angle);
	rot.setColumn(0, rot.getColumn(0) * use_scale.x);
	rot.setColumn(1, rot.getColumn(1) * use_scale.y);
	rot.setColumn(2, rot.getColumn(2) * use_scale.z);
	rot.setColumn(3, pos + ob.translation);
	return rot;
}


const Matrix4f worldToObMatrix(const WorldObject& ob)
{
	const Vec4f pos((float)ob.pos.x, (float)ob.pos.y, (float)ob.pos.z, 1.f);

	return Matrix4f::scaleMatrix(1/ob.scale.x, 1/ob.scale.y, 1/ob.scale.z) *
		Matrix4f::rotationMatrix(normalise(ob.axis.toVec4fVector()), -ob.angle) *
		Matrix4f::translationMatrix(-pos - ob.translation);
}


size_t WorldObject::getTotalMemUsage() const
{
	return sizeof(WorldObject) + 
		(compressed_voxels ? compressed_voxels->dataSizeBytes() : 0) + 
		(voxel_group.voxels.capacitySizeBytes());
}


int WorldObject::getLightMapSideResForAABBWS(const js::AABBox& aabb_ws)
{
	const float A = aabb_ws.getSurfaceArea();

	// We want an object with maximally sized AABB, similar to that of a parcel, to have the max allowable res (e.g. 512*512)
	// And use a proportionally smaller number of pixels based on a smaller area.

	const float parcel_W = 20;
	const float parcel_H = 10;
	const float parcel_A = parcel_W * parcel_W * 2 + parcel_W * parcel_H * 4;

	const float frac = A / parcel_A;

	const float full_num_px = Maths::square(2048.f);

	const float use_num_px = frac * full_num_px;

	const float use_side_res = std::sqrt(use_num_px);

	const int use_side_res_rounded = Maths::roundUpToMultipleOfPowerOf2((int)use_side_res, (int)4);

	// Clamp to min and max allowable lightmap resolutions
	const int clamped_side_res = myClamp(use_side_res_rounded, 64, 2048);

	return clamped_side_res;
}


struct GetMatIndex
{
	size_t operator() (const Voxel& v)
	{
		return (size_t)v.mat_index;
	}
};


Reference<glare::SharedImmutableArray<uint8> > WorldObject::compressVoxelGroup(const VoxelGroup& group)
{
	size_t max_bucket = 0;
	for(size_t i=0; i<group.voxels.size(); ++i)
		max_bucket = myMax<size_t>(max_bucket, group.voxels[i].mat_index);

	const size_t num_buckets = max_bucket + 1;

	// Step 1: sort by materials
	glare::AllocatorVector<Voxel, 16> sorted_voxels(group.voxels.size());
	Sort::serialCountingSortWithNumBuckets(group.voxels.data(), sorted_voxels.data(), group.voxels.size(), num_buckets, GetMatIndex());

	//std::vector<Voxel> sorted_voxels = group.voxels;
	//std::sort(sorted_voxels.begin(), sorted_voxels.end(), VoxelComparator());


	// Count num items in each bucket
	std::vector<size_t> counts(num_buckets, 0);
	for(size_t i=0; i<group.voxels.size(); ++i)
		counts[group.voxels[i].mat_index]++;

	Vec3<int> current_pos(0, 0, 0);
	int v_i = 0;

	js::Vector<int, 16> data(1 + (int)counts.size() + group.voxels.size() * 3);
	size_t write_i = 0;

	data[write_i++] = (int)counts.size(); // Write num materials

	for(size_t z=0; z<counts.size(); ++z)
	{
		const size_t count = counts[z];
		data[write_i++] = (int)count; // Write count of voxels with that material

		for(size_t i=0; i<count; ++i)
		{
			Vec3<int> relative_pos = sorted_voxels[v_i].pos - current_pos;
			//conPrint("relative_pos: " + relative_pos.toString());

			data[write_i++] = relative_pos.x;
			data[write_i++] = relative_pos.y;
			data[write_i++] = relative_pos.z;

			current_pos = sorted_voxels[v_i].pos;

			v_i++;
		}
	}

	assert(write_i == data.size());

	const size_t compressed_bound = ZSTD_compressBound(data.size() * sizeof(int));

	js::Vector<uint8> compressed_data(compressed_bound);
	
	const size_t compressed_size = ZSTD_compress(compressed_data.data(), compressed_data.size(), data.data(), data.dataSizeBytes(),
		ZSTD_CLEVEL_DEFAULT // compression level
	);

	compressed_data.resize(compressed_size);

	Reference<glare::SharedImmutableArray<uint8> > compressed_immutable_data = new glare::SharedImmutableArray<uint8>(compressed_data.begin(), compressed_data.end());

	// conPrint("uncompressed size:      " + toString(group.voxels.size() * sizeof(Voxel)) + " B");
	// conPrint("compressed_size:        " + toString(compressed_size) + " B");
	// const double ratio = (double)group.voxels.size() * sizeof(Voxel) / compressed_size;
	// conPrint("compression ratio: " + toString(ratio));

	//TEMP: decompress and check we get the same value
#ifndef NDEBUG
	VoxelGroup group2;
	decompressVoxelGroup(compressed_immutable_data->data(), compressed_immutable_data->size(), NULL, group2);
	assert(group2.voxels == sorted_voxels);
#endif

	return compressed_immutable_data;
}


void WorldObject::decompressVoxelGroup(const uint8* compressed_data, size_t compressed_data_len, glare::Allocator* mem_allocator, VoxelGroup& group_out)
{
	group_out.voxels.clear();

	const uint64 decompressed_size = ZSTD_getFrameContentSize(compressed_data, compressed_data_len);
	if(decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN || decompressed_size == ZSTD_CONTENTSIZE_ERROR)
		throw glare::Exception("Failed to get decompressed_size");

	BufferInStream instream;
	if(mem_allocator)
		instream.buf.setAllocator(mem_allocator);
	instream.buf.resizeNoCopy(decompressed_size);

	const size_t res = ZSTD_decompress(instream.buf.data(), decompressed_size, compressed_data, compressed_data_len);
	if(ZSTD_isError(res))
		throw glare::Exception("Decompression of buffer failed: " + toString(res));
	if(res < decompressed_size)
		throw glare::Exception("Decompression of buffer failed: not enough bytes in result");

	// Do a pass over the data to get the total number of voxels, so that we can resize group_out.voxels up-front.
	int total_num_voxels = 0;
	const int num_mats = instream.readInt32();
	for(int m=0; m<num_mats; ++m)
	{
		const int count = instream.readInt32(); // Number of voxels with this material.
		if(count < 0 || count > 64000000)
			throw glare::Exception("Voxel count is too large: " + toString(count));

		instream.advanceReadIndex(sizeof(Vec3<int>) * count); // Skip over voxel data.
		total_num_voxels += count;
	}

	// Reset stream read index to beginning.
	instream.setReadIndex(0);

	if(total_num_voxels > 64000000)
		throw glare::Exception("Voxel count is too large: " + toString(total_num_voxels));

	group_out.voxels.resizeNoCopy(total_num_voxels);

	Vec3<int> current_pos(0, 0, 0);

	instream.readInt32(); // Read num_mats again
	size_t write_i = 0;
	for(int m=0; m<num_mats; ++m)
	{
		const int count = instream.readInt32(); // Number of voxels with this material.
		if(count < 0 || count > 64000000)
			throw glare::Exception("Voxel count is too large: " + toString(count));

		const Vec3<int>* relative_positions = (const Vec3<int>*)instream.currentReadPtr(); // Pointer should be sufficiently aligned.

		instream.advanceReadIndex(sizeof(Vec3<int>) * count); // Advance past relative positions.  (Checks relative_positions pointer points to a valid range)

		for(int i=0; i<count; ++i)
		{
			const Vec3<int> pos = current_pos + relative_positions[i];

			group_out.voxels[write_i++] = Voxel(pos, m);

			current_pos = pos;
		}
	}

	if(!instream.endOfStream())
		throw glare::Exception("Didn't reach EOF while reading voxels.");
}


void WorldObject::compressVoxels()
{
	if(!this->voxel_group.voxels.empty())
	{
		this->compressed_voxels = compressVoxelGroup(this->voxel_group);
	}
	else
		this->compressed_voxels = NULL;
}


void WorldObject::decompressVoxels()
{ 
	if(this->compressed_voxels && !this->compressed_voxels->empty()) // If there are compressed voxels:
		decompressVoxelGroup(this->compressed_voxels->data(), this->compressed_voxels->size(), /*mem allocator=*/NULL, this->voxel_group); // Decompress to voxel_group.
	else
		this->voxel_group.voxels.clear(); // Else there are no compressed voxels, so effectively decompress to zero voxels.

	// conPrint("decompressVoxels: decompressed to " + toString(this->voxel_group.voxels.size()) + " voxels.");
}


void WorldObject::clearDecompressedVoxels()
{
	this->voxel_group.voxels.clearAndFreeMem();
	//this->voxel_group.voxels = std::vector<Voxel>();
}


void WorldObject::setCompressedVoxels(Reference<glare::SharedImmutableArray<uint8>> v)
{
	compressed_voxels = v;

	// Compute compressed_voxels_hash
	if(compressed_voxels)
		compressed_voxels_hash = XXH64(compressed_voxels->data(), compressed_voxels->size(), /*seed=*/1);
	else
		compressed_voxels_hash = 0;
}


void WorldObject::setAABBOS(const js::AABBox& aabb_os_)
{
	this->aabb_os = aabb_os_;
	
	transformChanged();
}


void WorldObject::zeroAABBOS()
{
	this->aabb_os = js::AABBox(Vec4f(0,0,0,1), Vec4f(0,0,0,1));
	this->centroid_ws = Vec4f(0,0,0,1);
	this->aabb_ws_longest_len = 0;
	this->biased_aabb_len = 0;
}


void WorldObject::transformChanged() // Rebuild centroid_ws, biased_aabb_len
{
	const Matrix4f ob_to_world = this->obToWorldMatrix();

	doTransformChanged(ob_to_world, this->scale.toVec4fVector());
}


js::AABBox VoxelGroup::getAABB() const
{
	// Iterate over voxels and get voxel position bounds
	Vec3<int> minpos( 1000000000);
	Vec3<int> maxpos(-1000000000);
	for(size_t i=0; i<voxels.size(); ++i)
	{
		minpos = minpos.min(voxels[i].pos);
		maxpos = maxpos.max(voxels[i].pos);
	}
	return js::AABBox(Vec4f((float)minpos.x, (float)minpos.y, (float)minpos.z, 1), Vec4f((float)(maxpos.x + 1), (float)(maxpos.y + 1), (float)(maxpos.z + 1), 1));
}


// Throws glare::Exception if transform not OK, for example if any components are infinite or NaN. 
void checkTransformOK(const WorldObject* ob)
{
	// Sanity check position, axis, angle
	if(!ob->pos.isFinite())
		throw glare::Exception("Position had non-finite component.");
	if(!ob->axis.isFinite())
		throw glare::Exception("axis had non-finite component.");
	if(!::isFinite(ob->angle))
		throw glare::Exception("angle was non-finite.");

	const Matrix4f ob_to_world_matrix = obToWorldMatrix(*ob);

	// Sanity check ob_to_world_matrix matrix
	for(int i=0; i<16; ++i)
		if(!::isFinite(ob_to_world_matrix.e[i]))
			throw glare::Exception("ob_to_world_matrix had non-finite component.");

	Matrix4f world_to_ob;
	const bool ob_to_world_invertible = ob_to_world_matrix.getInverseForAffine3Matrix(world_to_ob);
	if(!ob_to_world_invertible)
		throw glare::Exception("ob_to_world_matrix was not invertible."); // TEMP: do we actually need this restriction?

	// Check world_to_ob matrix
	for(int i=0; i<16; ++i)
		if(!::isFinite(world_to_ob.e[i]))
			throw glare::Exception("world_to_ob had non-finite component.");
}


Reference<ObjectEventHandlers> WorldObject::getOrCreateEventHandlers()
{
	if(!event_handlers)
		event_handlers = new ObjectEventHandlers();
	return event_handlers;
}


void doDestroyOb(WorldObject* ob)
{
	if(ob->allocator)
	{
		glare::FastPoolAllocator* allocator = ob->allocator;
		const int allocation_index = ob->allocation_index;
		ob->~WorldObject(); // Call destructor on object
		allocator->free(allocation_index);
	}
	else
		delete ob;
}


#if BUILD_TESTS


#include <utils/IndigoXMLDoc.h>
#include <utils/BufferOutStream.h>
#include <utils/BufferViewInStream.h>
#include <utils/TestUtils.h>
#include <utils/FileOutStream.h>


#if 0
// Command line:
// C:\fuzz_corpus\worldobject N:\substrata\testfiles\fuzz_seeds\worldobject

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	return 0;
}


extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	//TEMP: Write out an object with voxels for a fuzz seed
	/*{
		WorldObject ob;
		ob.object_type = WorldObject::ObjectType_VoxelGroup;
		ob.getDecompressedVoxels().push_back(Voxel(Vec3<int>(0,1,2), 0));
		ob.getDecompressedVoxels().push_back(Voxel(Vec3<int>(4,5,6), 1));
		ob.compressVoxels();

		{
			BufferOutStream outstream;
			ob.writeToStream(outstream);

			FileOutStream file("ob_with_voxels.worldobject");
			file.writeData(outstream.buf.data(), outstream.buf.size());
		}
	}*/


	try
	{
		BufferViewInStream stream(ArrayRef<uint8>(data, size));

		WorldObject ob;
		readWorldObjectFromStream(stream, ob);
	}
	catch(glare::Exception&)
	{
	}
	
	return 0;  // Non-zero return values are reserved for future use.
}
#endif


static void testObjectsEqual(WorldObject& ob1, WorldObject& ob2)
{
	testAssert(ob1.getCompressedVoxels().nonNull() == ob2.getCompressedVoxels().nonNull());
	if(ob1.getCompressedVoxels().nonNull())
		testAssert(*ob1.getCompressedVoxels() == *ob2.getCompressedVoxels());

	testAssert(ob1.materials.size() == ob2.materials.size());
	for(size_t i=0; i<ob1.materials.size(); ++i)
	{
		WorldMaterial& mat1 = *ob1.materials[i];
		WorldMaterial& mat2 = *ob2.materials[i];
		testAssert(mat1.normal_map_url == mat2.normal_map_url);
	}
}


void WorldObject::test()
{
	conPrint("WorldObject::test()");


	//----------------------------- Test makeOptimisedMeshURL ----------------------------
	testAssert(makeOptimisedMeshURL("something_5345345435", /*lod level=*/0, /*get optimised mesh=*/false, /*opt mesh version=*/1) == "something_5345345435.bmesh");
	testAssert(makeOptimisedMeshURL("something_5345345435.bmesh", /*lod level=*/0, /*get optimised mesh=*/false, /*opt mesh version=*/1) == "something_5345345435.bmesh");
	testAssert(makeOptimisedMeshURL("something_5345345435.bmesh", /*lod level=*/1, /*get optimised mesh=*/false, /*opt mesh version=*/1) == "something_5345345435_lod1.bmesh");
	testAssert(makeOptimisedMeshURL("something_5345345435.bmesh", /*lod level=*/2, /*get optimised mesh=*/false, /*opt mesh version=*/1) == "something_5345345435_lod2.bmesh");
	testAssert(makeOptimisedMeshURL("something_5345345435.bmesh", /*lod level=*/2, /*get optimised mesh=*/true, /*opt mesh version=*/1) == "something_5345345435_lod2_opt1.bmesh");
	testAssert(makeOptimisedMeshURL("something_5345345435.bmesh", /*lod level=*/2, /*get optimised mesh=*/true, /*opt mesh version=*/123) == "something_5345345435_lod2_opt123.bmesh");
	testAssert(makeOptimisedMeshURL("something_5345345435.bmesh", /*lod level=*/0, /*get optimised mesh=*/true, /*opt mesh version=*/123) == "something_5345345435_opt123.bmesh");

	testAssert(makeOptimisedMeshURL("something_5345345435.bmesh", /*lod level=*/0, /*get optimised mesh=*/true, /*opt mesh version=*/0) == "something_5345345435_opt0.bmesh");
	testAssert(makeOptimisedMeshURL("something_5345345435.bmesh", /*lod level=*/0, /*get optimised mesh=*/true, /*opt mesh version=*/1) == "something_5345345435_opt1.bmesh");
	testAssert(makeOptimisedMeshURL("something_5345345435.bmesh", /*lod level=*/0, /*get optimised mesh=*/true, /*opt mesh version=*/9) == "something_5345345435_opt9.bmesh");
	testAssert(makeOptimisedMeshURL("something_5345345435.bmesh", /*lod level=*/0, /*get optimised mesh=*/true, /*opt mesh version=*/10) == "something_5345345435_opt10.bmesh");
	testAssert(makeOptimisedMeshURL("something_5345345435.bmesh", /*lod level=*/0, /*get optimised mesh=*/true, /*opt mesh version=*/11) == "something_5345345435_opt11.bmesh");
	testAssert(makeOptimisedMeshURL("something_5345345435.bmesh", /*lod level=*/0, /*get optimised mesh=*/true, /*opt mesh version=*/99) == "something_5345345435_opt99.bmesh");
	testAssert(makeOptimisedMeshURL("something_5345345435.bmesh", /*lod level=*/0, /*get optimised mesh=*/true, /*opt mesh version=*/100) == "something_5345345435_opt100.bmesh");


	//---------------------------- Test getLODModelURLForLevel ----------------------------
	testAssert(getLODModelURLForLevel("base", /*lod level=*/0, GetLODModelURLOptions(/*get optimised mesh=*/false, /*opt mesh version=*/1)) == "base");
	testAssert(getLODModelURLForLevel("base", /*lod level=*/1, GetLODModelURLOptions(/*get optimised mesh=*/false, /*opt mesh version=*/1)) == "base_lod1.bmesh");
	testAssert(getLODModelURLForLevel("base", /*lod level=*/2, GetLODModelURLOptions(/*get optimised mesh=*/false, /*opt mesh version=*/1)) == "base_lod2.bmesh");

	testAssert(getLODModelURLForLevel("base", /*lod level=*/0, GetLODModelURLOptions(/*get optimised mesh=*/true, /*opt mesh version=*/7)) == "base_opt7.bmesh");
	testAssert(getLODModelURLForLevel("base", /*lod level=*/0, GetLODModelURLOptions(/*get optimised mesh=*/true, /*opt mesh version=*/89)) == "base_opt89.bmesh");
	testAssert(getLODModelURLForLevel("base", /*lod level=*/1, GetLODModelURLOptions(/*get optimised mesh=*/true, /*opt mesh version=*/89)) == "base_lod1_opt89.bmesh");
	testAssert(getLODModelURLForLevel("base", /*lod level=*/2, GetLODModelURLOptions(/*get optimised mesh=*/true, /*opt mesh version=*/89)) == "base_lod2_opt89.bmesh");


	//----------------------------- Test getLODLevelForURL ----------------------------
	testAssert(getLODLevelForURL("something_5345345435.bmesh") == 0);
	testAssert(getLODLevelForURL("something_5345345435_lod1.bmesh") == 1);
	testAssert(getLODLevelForURL("something_5345345435_lod2.bmesh") == 2);
	testAssert(getLODLevelForURL("something_5345345435_lod2_opt1.bmesh") == 2);
	testAssert(getLODLevelForURL("something_5345345435_lod2_opt2.bmesh") == 2);
	testAssert(getLODLevelForURL("something_5345345435_lod2_opt3.bmesh") == 2);
	testAssert(getLODLevelForURL("something_5345345435_lod2_opt123.bmesh") == 2);

	testAssert(getLODLevelForURL("something_5345345435") == 0);
	testAssert(getLODLevelForURL("something") == 0);
	testAssert(getLODLevelForURL("") == 0);


	try
	{

		{
			WorldObject ob;
			ob.pos = Vec3d(0.0);
			ob.axis = Vec3f(0,0,1);
			ob.angle = 0;
			ob.materials.push_back(new WorldMaterial());
			ob.materials.push_back(new WorldMaterial());
			ob.materials.push_back(new WorldMaterial());

			ob.script = "abc";

			BufferOutStream buf;
			ob.writeToStream(buf);

			// Test reading back object works
			BufferInStream instream(ArrayRef<uint8>(buf.buf.data(), buf.buf.size()));
			WorldObject ob2;
			readWorldObjectFromStream(instream, ob2);
			testObjectsEqual(ob, ob2);

			// Test writing to and reading from XML.
			const std::string xml = ob.serialiseToXML(/*tab depth=*/0);
			IndigoXMLDoc doc(xml.c_str(), xml.size());
			WorldObjectRef ob3 = WorldObject::loadFromXMLElem(/*object_file_path=*/".", /*convert_rel_paths_to_abs_disk_paths=*/false, doc.getRootElement());
			testObjectsEqual(ob, *ob3);
		}

		// Test with some large materials
		{
			WorldObject ob;
			ob.pos = Vec3d(0.0);
			ob.axis = Vec3f(0,0,1);
			ob.angle = 0;
			ob.materials.push_back(new WorldMaterial());
			ob.materials.back()->normal_map_url = std::string(WorldObject::MAX_URL_SIZE, 'A');
			ob.materials.push_back(new WorldMaterial());
			ob.materials.back()->normal_map_url = std::string(WorldObject::MAX_URL_SIZE, 'B');
			ob.materials.push_back(new WorldMaterial());

			ob.script = "abc";

			BufferOutStream buf;
			ob.writeToStream(buf);

			// Test reading back object works
			BufferInStream instream(ArrayRef<uint8>(buf.buf.data(), buf.buf.size()));
			WorldObject ob2;
			readWorldObjectFromStream(instream, ob2);
			testObjectsEqual(ob, ob2);

			// Test writing to and reading from XML.
			const std::string xml = ob.serialiseToXML(/*tab depth=*/0);
			IndigoXMLDoc doc(xml.c_str(), xml.size());
			WorldObjectRef ob3 = WorldObject::loadFromXMLElem(/*object_file_path=*/".", /*convert_rel_paths_to_abs_disk_paths=*/false, doc.getRootElement());
			testObjectsEqual(ob, *ob3);
		}

		// Test that an object with lots of materials serialises without using too much mem
		{
			WorldObject ob;
			ob.pos = Vec3d(0.0);
			ob.axis = Vec3f(0,0,1);
			ob.angle = 0;

			for(int i=0; i<1024; ++i)
				ob.materials.push_back(new WorldMaterial());

			ob.script = "abc";

			BufferOutStream buf;
			ob.writeToStream(buf);

			// Test reading back object works
			BufferInStream instream(ArrayRef<uint8>(buf.buf.data(), buf.buf.size()));
			WorldObject ob2;
			readWorldObjectFromStream(instream, ob2);
			testObjectsEqual(ob, ob2);

			// Test writing to and reading from XML.
			const std::string xml = ob.serialiseToXML(/*tab depth=*/0);
			IndigoXMLDoc doc(xml.c_str(), xml.size());
			WorldObjectRef ob3 = WorldObject::loadFromXMLElem(/*object_file_path=*/".", /*convert_rel_paths_to_abs_disk_paths=*/false, doc.getRootElement());
			testObjectsEqual(ob, *ob3);
		}


		// Test with a voxel object with some compressed voxel data.
		{
			WorldObject ob;
			ob.pos = Vec3d(0.0);
			ob.axis = Vec3f(0,0,1);
			ob.angle = 0;
			ob.materials.push_back(new WorldMaterial());
			ob.object_type = WorldObject::ObjectType_VoxelGroup;
			ob.compressed_voxels = new glare::SharedImmutableArray<uint8>();
			ob.compressed_voxels->resizeNoCopy(100);
			for(size_t i=0; i<100; ++i)
				(*ob.compressed_voxels)[i] = (uint8)i;

			BufferOutStream buf;
			ob.writeToStream(buf);
			BufferInStream instream(buf.buf);
			WorldObject ob2;
			readWorldObjectFromStream(instream, ob2);
			testObjectsEqual(ob, ob2);

			// Test writing to and reading from XML.
			const std::string xml = ob.serialiseToXML(/*tab depth=*/0);
			IndigoXMLDoc doc(xml.c_str(), xml.size());
			WorldObjectRef ob3 = WorldObject::loadFromXMLElem(/*object_file_path=*/".", /*convert_rel_paths_to_abs_disk_paths=*/false, doc.getRootElement());
			testObjectsEqual(ob, *ob3);
		}
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}


	//----------------------------------------------------
	try
	{
		glare::ArenaAllocator arena_allocator(1024 * 1024);


		const std::string xml_path = "C:\\code\\substrata\\testfiles\\world_objects\\sandbox_parcel_objects.xml";
		IndigoXMLDoc doc(xml_path);

		std::vector<WorldObjectRef> obs;
		if(std::string(doc.getRootElement().name()) == "objects")
		{
			for(pugi::xml_node ob_node = doc.getRootElement().child("object"); ob_node; ob_node = ob_node.next_sibling("object"))
			{
				WorldObjectRef ob = WorldObject::loadFromXMLElem(/*object file path=*/xml_path, /*convert rel paths to abs disk paths=*/false, ob_node);
				obs.push_back(ob);
			}
		}

		if(1)
		{
			double smallest_time = 1.0e20;
			for(int z=0; z<10000; ++z)
			{
				Timer timer;
				glare::ArenaAllocator use_arena = arena_allocator.getFreeAreaArenaAllocator();
				glare::STLArenaAllocator<DependencyURL> stl_arena_allocator(&use_arena);
				DependencyURLSet URLs(std::less<DependencyURL>(), stl_arena_allocator);

				for(size_t i=0; i<obs.size(); ++i)
				{
					WorldObject::GetDependencyOptions options;
					options.use_basis = true;
					options.allocator = &use_arena;
					obs[i]->getDependencyURLSet(/*ob lod level=*/1, options, URLs);
				}
				smallest_time = myMin(smallest_time, timer.elapsed());
			}
			conPrint("objects getDependencyURLSet (with allocator) took " + doubleToStringNSigFigs(smallest_time * 1.0e9) + " ns");
		}
		if(1)
		{
			double smallest_time = 1.0e20;
			for(int z=0; z<10000; ++z)
			{
				Timer timer;
				DependencyURLSet URLs;
				for(size_t i=0; i<obs.size(); ++i)
				{
					WorldObject::GetDependencyOptions options;
					options.use_basis = true;
					obs[i]->getDependencyURLSet(/*ob lod level=*/1, options, URLs);
				}
				smallest_time = myMin(smallest_time, timer.elapsed());
			}
			conPrint("objects getDependencyURLSet (no allocator)   took " + doubleToStringNSigFigs(smallest_time * 1.0e9) + " ns");
		}


		{
			double smallest_time = 1.0e20;
			for(int z=0; z<1000; ++z)
			{
				
				Timer timer;
				const int num_inner_iters = 10;
				for(int q=0; q<num_inner_iters; ++q)
				{
					{
						glare::ArenaAllocator use_arena = arena_allocator.getFreeAreaArenaAllocator();
						glare::STLArenaAllocator<DependencyURL> stl_arena_allocator(&use_arena);
						DependencyURLVector URL_vector(stl_arena_allocator);
						for(size_t i=0; i<obs.size(); ++i)
						{
							WorldObject::GetDependencyOptions options;
							options.use_basis = true;
							options.allocator = &use_arena;
							obs[i]->appendDependencyURLs(/*ob lod level=*/1, options, URL_vector);
						}
					}
				}
				smallest_time = myMin(smallest_time, timer.elapsed() / num_inner_iters);
			}
			conPrint("objects appendDependencyURLs (with allocator) took " + doubleToStringNSigFigs(smallest_time * 1.0e9) + " ns");
		}
		{
			double smallest_time = 1.0e20;
			for(int z=0; z<1000; ++z)
			{
				
				Timer timer;
				const int num_inner_iters = 10;
				for(int q=0; q<num_inner_iters; ++q)
				{
					{
						DependencyURLVector URL_vector;
						for(size_t i=0; i<obs.size(); ++i)
						{
							WorldObject::GetDependencyOptions options;
							options.use_basis = true;
							obs[i]->appendDependencyURLs(/*ob lod level=*/1, options, URL_vector);
						}
					}
				}
				smallest_time = myMin(smallest_time, timer.elapsed() / num_inner_iters);
			}
			conPrint("objects appendDependencyURLs (no allocator)   took " + doubleToStringNSigFigs(smallest_time * 1.0e9) + " ns");
		}
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}









	conPrint("WorldObject::test() done");
}

#endif // BUILD_TESTS
