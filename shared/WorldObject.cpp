/*=====================================================================
WorldObject.cpp
---------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#include "WorldObject.h"


#include <Exception.h>
#include <StringUtils.h>
#include <FileUtils.h>
#include <ConPrint.h>
#include <FileChecksum.h>
#include <Sort.h>
#include <BufferInStream.h>
#if GUI_CLIENT
#include "opengl/OpenGLEngine.h"
#include "../audio/AudioEngine.h"
#include <SceneNodeModel.h>
#include "../gui_client/WinterShaderEvaluator.h"
#endif
#include "../gui_client/PhysicsObject.h"
#include "../shared/ResourceManager.h"
#include <zstd.h>


WorldObject::WorldObject()
{
	creator_id = UserID::invalidUserID();
	flags = COLLIDABLE_FLAG;

	object_type = ObjectType_Generic;
	from_remote_transform_dirty = false;
	from_remote_other_dirty = false;
	from_remote_lightmap_url_dirty = false;
	from_remote_model_url_dirty = false;
	from_remote_flags_dirty = false;
	from_local_transform_dirty = false;
	from_local_other_dirty = false;
	using_placeholder_model = false;
#if GUI_CLIENT
	is_selected = false;
	loaded = false;
	lightmap_baking = false;
	current_lod_level = 0;
	loaded_lod_level = -1;
#endif
	next_snapshot_i = 0;
	//last_snapshot_time = 0;

	instance_index = 0;
	num_instances = 0;
	translation = Vec4f(0.f);

	max_load_dist2 = 1000000000;

	prototype_object = NULL;

	aabb_ws = js::AABBox::emptyAABBox();

	max_model_lod_level = 0;
}


WorldObject::~WorldObject()
{

}


std::string WorldObject::getLODModelURLForLevel(const std::string& base_model_url, int level)
{
	if(level == 0)
		return base_model_url;
	else
	{
		if(hasPrefix(base_model_url, "http:") || hasPrefix(base_model_url, "https:"))
			return base_model_url;

		if(level == 1)
			return removeDotAndExtension(base_model_url) + "_lod1.bmesh"; // LOD models are always saved in BatchedMesh (bmesh) format.
		else
			return removeDotAndExtension(base_model_url) + "_lod2.bmesh";
	}
}


std::string WorldObject::getLODTextureURLForLevel(const std::string& base_texture_url, int level, bool has_alpha)
{
	if(level == 0)
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

		if(level == 1)
			return removeDotAndExtension(base_texture_url) + "_lod1." + (is_gif ? "gif" : (has_alpha ? "png" : "jpg"));
		else
			return removeDotAndExtension(base_texture_url) + "_lod2." + (is_gif ? "gif" : (has_alpha ? "png" : "jpg"));
	}
}


int WorldObject::getLODLevel(const Vec3d& campos) const
{
	const float dist = campos.toVec4fVector().getDist(this->pos.toVec4fVector());
	const float proj_len = aabb_ws.longestLength() / dist;

	if(proj_len > 0.16)
		return 0;
	else if(proj_len > 0.03)
		return 1;
	else
		return 2;
}


int WorldObject::getModelLODLevel(const Vec3d& campos) const // getLODLevel() clamped to max_model_lod_level
{
	if(max_model_lod_level == 0)
		return 0;

	return getLODLevel(campos);
}


std::string WorldObject::getLODModelURL(const Vec3d& campos) const
{
	// Early-out for max_model_lod_level == 0: avoid computing LOD
	if(this->max_model_lod_level == 0)
		return this->model_url;

	const int ob_lod_level = getLODLevel(campos);
	const int ob_model_lod_level = myMin(ob_lod_level, this->max_model_lod_level);
	return getLODModelURLForLevel(this->model_url, ob_model_lod_level);
}


void WorldObject::appendDependencyURLs(int ob_lod_level, std::vector<std::string>& URLs_out)
{
	if(!model_url.empty())
	{
		const int ob_model_lod_level = myMin(ob_lod_level, this->max_model_lod_level);
		URLs_out.push_back(getLODModelURLForLevel(model_url, ob_model_lod_level));
	}

	if(!lightmap_url.empty())
		URLs_out.push_back(lightmap_url); // TEMP NO LIGHTMAP LOD   getLODTextureURLForLevel(lightmap_url, ob_lod_level, /*has alpha=*/false));

	for(size_t i=0; i<materials.size(); ++i)
		materials[i]->appendDependencyURLs(ob_lod_level, URLs_out);
}


void WorldObject::appendDependencyURLsForAllLODLevels(std::vector<std::string>& URLs_out)
{
	if(!model_url.empty())
	{
		URLs_out.push_back(model_url);
		if(max_model_lod_level > 0)
		{
			URLs_out.push_back(getLODModelURLForLevel(model_url, 1));
			URLs_out.push_back(getLODModelURLForLevel(model_url, 2));
		}
	}

	if(!lightmap_url.empty())
		URLs_out.push_back(lightmap_url);

	for(size_t i=0; i<materials.size(); ++i)
		materials[i]->appendDependencyURLsAllLODLevels(URLs_out);
}


void WorldObject::getDependencyURLSet(int ob_lod_level, std::set<std::string>& URLS_out)
{
	std::vector<std::string> URLs;
	this->appendDependencyURLs(ob_lod_level, URLs);

	URLS_out = std::set<std::string>(URLs.begin(), URLs.end());
}


void WorldObject::getDependencyURLSetForAllLODLevels(std::set<std::string>& URLS_out)
{
	std::vector<std::string> URLs;
	this->appendDependencyURLsForAllLODLevels(URLs);

	URLS_out = std::set<std::string>(URLs.begin(), URLs.end());
}


void WorldObject::convertLocalPathsToURLS(ResourceManager& resource_manager)
{
	if(FileUtils::fileExists(this->model_url)) // If the URL is a local path:
		this->model_url = resource_manager.URLForPathAndHash(this->model_url, FileChecksum::fileChecksum(this->model_url));

	for(size_t i=0; i<materials.size(); ++i)
		materials[i]->convertLocalPathsToURLS(resource_manager);

	if(FileUtils::fileExists(this->lightmap_url)) // If the URL is a local path:
		this->lightmap_url = resource_manager.URLForPathAndHash(this->lightmap_url, FileChecksum::fileChecksum(this->lightmap_url));
}


void WorldObject::setTransformAndHistory(const Vec3d& pos_, const Vec3f& axis_, float angle_)
{
	assert(isFinite(angle_));

	pos = pos_;
	axis = axis_;
	angle = angle_;

	for(int i=0; i<HISTORY_BUF_SIZE; ++i)
	{
		pos_snapshots[i] = pos_;
		axis_snapshots[i] = axis_;
		angle_snapshots[i] = angle_;
		snapshot_times[i] = 0;
	}
}


void WorldObject::setPosAndHistory(const Vec3d& pos_)
{
	pos = pos_;
#if GUI_CLIENT
	last_pos = pos_;
#endif

	for(int i=0; i<HISTORY_BUF_SIZE; ++i)
		pos_snapshots[i] = pos_;
}


void WorldObject::getInterpolatedTransform(double cur_time, Vec3d& pos_out, Vec3f& axis_out, float& angle_out) const
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
	// Search through history for first snapshot
	int begin = 0;
	for(int i=(int)next_snapshot_i-HISTORY_BUF_SIZE; i<(int)next_snapshot_i; ++i)
	{
		const int modi = Maths::intMod(i, HISTORY_BUF_SIZE);
		if(snapshot_times[modi] > delayed_time)
		{
			begin = Maths::intMod(modi - 1, HISTORY_BUF_SIZE);
			break;
		}
	}

	const int end = Maths::intMod(begin + 1, HISTORY_BUF_SIZE);

	// Snapshot times may be the same if we haven't received updates for this object yet.
	const float t  = (snapshot_times[end] == snapshot_times[begin]) ? 0.f : (float)((delayed_time - snapshot_times[begin]) / (snapshot_times[end] - snapshot_times[begin])); // Interpolation fraction

	pos_out   = Maths::uncheckedLerp(pos_snapshots  [begin], pos_snapshots  [end], t);
	axis_out  = Maths::uncheckedLerp(axis_snapshots [begin], axis_snapshots [end], t);
	angle_out = Maths::uncheckedLerp(angle_snapshots[begin], angle_snapshots[end], t);

	if(axis_out.length2() < 1.0e-10f)
	{
		axis_out = Vec3f(0,0,1);
		angle_out = 0;
	}

	//const int last_snapshot_i = next_snapshot_i - 1;

	//const double frac = (cur_time - last_snapshot_time) / send_period; // Fraction of send period ahead of last_snapshot cur time is
	//printVar(frac);
	//if(frac > 2.0f)
	//	int a = 9;
	//const double delayed_state_pos = (double)last_snapshot_i + frac - delay; // Delayed state position in normalised period coordinates.
	//const int delayed_state_begin_snapshot_i = myClamp(Maths::floorToInt(delayed_state_pos), last_snapshot_i - HISTORY_BUF_SIZE + 1, last_snapshot_i);
	//const int delayed_state_end_snapshot_i   = myClamp(delayed_state_begin_snapshot_i + 1,   last_snapshot_i - HISTORY_BUF_SIZE + 1, last_snapshot_i);
	//const float t  = delayed_state_pos - delayed_state_begin_snapshot_i; // Interpolation fraction

	//const int begin = Maths::intMod(delayed_state_begin_snapshot_i, HISTORY_BUF_SIZE);
	//const int end   = Maths::intMod(delayed_state_end_snapshot_i,   HISTORY_BUF_SIZE);

	//pos_out   = Maths::uncheckedLerp(pos_snapshots  [begin], pos_snapshots  [end], t);
	//axis_out  = Maths::uncheckedLerp(axis_snapshots [begin], axis_snapshots [end], t);
	//angle_out = Maths::uncheckedLerp(angle_snapshots[begin], angle_snapshots[end], t);

	//if(axis_out.length2() < 1.0e-10f)
	//{
	//	axis_out = Vec3f(0,0,1);
	//	angle_out = 0;
	//}


	//const double send_period = 0.1; // Time between update messages from server
	//const double delay = /*send_period * */2.0; // Objects are rendered using the interpolated state at this past time.

	//const int last_snapshot_i = next_snapshot_i - 1;

	//const double frac = (cur_time - last_snapshot_time) / send_period; // Fraction of send period ahead of last_snapshot cur time is
	//printVar(frac);
	//if(frac > 2.0f)
	//	int a = 9;
	//const double delayed_state_pos = (double)last_snapshot_i + frac - delay; // Delayed state position in normalised period coordinates.
	//const int delayed_state_begin_snapshot_i = myClamp(Maths::floorToInt(delayed_state_pos), last_snapshot_i - HISTORY_BUF_SIZE + 1, last_snapshot_i);
	//const int delayed_state_end_snapshot_i   = myClamp(delayed_state_begin_snapshot_i + 1,   last_snapshot_i - HISTORY_BUF_SIZE + 1, last_snapshot_i);
	//const float t  = delayed_state_pos - delayed_state_begin_snapshot_i; // Interpolation fraction

	//const int begin = Maths::intMod(delayed_state_begin_snapshot_i, HISTORY_BUF_SIZE);
	//const int end   = Maths::intMod(delayed_state_end_snapshot_i,   HISTORY_BUF_SIZE);

	//pos_out   = Maths::uncheckedLerp(pos_snapshots  [begin], pos_snapshots  [end], t);
	//axis_out  = Maths::uncheckedLerp(axis_snapshots [begin], axis_snapshots [end], t);
	//angle_out = Maths::uncheckedLerp(angle_snapshots[begin], angle_snapshots[end], t);

	//if(axis_out.length2() < 1.0e-10f)
	//{
	//	axis_out = Vec3f(0,0,1);
	//	angle_out = 0;
	//}
}


std::string WorldObject::objectTypeString(ObjectType t)
{
	switch(t)
	{
	case ObjectType_Generic: return "generic";
	case ObjectType_Hypercard: return "hypercard";
	case ObjectType_VoxelGroup: return "voxel group";
	case ObjectType_Spotlight: return "spotlight";
	default: return "Unknown";
	}
}


static const uint32 WORLD_OBJECT_SERIALISATION_VERSION = 15;
/*
Version history:
9: introduced voxels
10: changed script_url to script
11: Added flags
12: Added compressed voxel field.
13: Added lightmap URL
14: Added aabb_ws
15: Added max_lod_level
*/


static_assert(sizeof(Voxel) == sizeof(int)*4, "sizeof(Voxel) == sizeof(int)*4");


void WorldObject::writeToStream(OutStream& stream) const
{
	// Write version
	stream.writeUInt32(WORLD_OBJECT_SERIALISATION_VERSION);

	::writeToStream(uid, stream);
	stream.writeUInt32((uint32)object_type);
	stream.writeStringLengthFirst(model_url);

	// Write materials
	stream.writeUInt32((uint32)materials.size());
	for(size_t i=0; i<materials.size(); ++i)
		::writeToStream(*materials[i], stream);

	stream.writeStringLengthFirst(lightmap_url); // new in v13

	stream.writeStringLengthFirst(script);
	stream.writeStringLengthFirst(content);
	stream.writeStringLengthFirst(target_url);

	::writeToStream(pos, stream);
	::writeToStream(axis, stream);
	stream.writeFloat(angle);
	::writeToStream(scale, stream);

	created_time.writeToStream(stream); // new in v5
	::writeToStream(creator_id, stream); // new in v5

	stream.writeUInt32(flags); // new in v11

	stream.writeData(aabb_ws.min_.x, sizeof(float) * 3); // new in v14
	stream.writeData(aabb_ws.max_.x, sizeof(float) * 3);

	stream.writeInt32(max_model_lod_level); // new in v15

	if(object_type == WorldObject::ObjectType_VoxelGroup)
	{
		// Write compressed voxel data
		stream.writeUInt32((uint32)compressed_voxels.size());
		if(compressed_voxels.size() > 0)
			stream.writeData(compressed_voxels.data(), compressed_voxels.dataSizeBytes());
	}
}


void readFromStream(InStream& stream, WorldObject& ob)
{
	// Read version
	const uint32 v = stream.readUInt32();
	if(v > WORLD_OBJECT_SERIALISATION_VERSION)
		throw glare::Exception("Unsupported version " + toString(v) + ", expected " + toString(WORLD_OBJECT_SERIALISATION_VERSION) + ".");

	ob.uid = readUIDFromStream(stream);

	if(v >= 7)
		ob.object_type = (WorldObject::ObjectType)stream.readUInt32(); // TODO: handle invalid values?

	ob.model_url = stream.readStringLengthFirst(10000);
	//if(v >= 2)
	//	ob.material_url = stream.readStringLengthFirst(10000);
	if(v >= 2)
	{
		const size_t num_mats = stream.readUInt32();
		ob.materials.resize(num_mats);
		for(size_t i=0; i<ob.materials.size(); ++i)
		{
			if(ob.materials[i].isNull())
				ob.materials[i] = new WorldMaterial();
			::readFromStream(stream, *ob.materials[i]);
		}
	}

	if(v >= 13)
	{
		ob.lightmap_url = stream.readStringLengthFirst(10000);
		//conPrint("readFromStream: read lightmap_url: " + ob.lightmap_url);
	}

	if(v >= 4 && v < 10)
	{
		stream.readStringLengthFirst(10000); // read and discard script URL
	}
	else if(v >= 10)
	{
		ob.script = stream.readStringLengthFirst(10000);
	}

	if(v >= 6)
		ob.content = stream.readStringLengthFirst(10000);

	if(v >= 8)
		ob.target_url = stream.readStringLengthFirst(10000);

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
		stream.readData(ob.aabb_ws.min_.x, sizeof(float) * 3);
		ob.aabb_ws.min_.x[3] = 1.f;
		stream.readData(ob.aabb_ws.max_.x, sizeof(float) * 3);
		ob.aabb_ws.max_.x[3] = 1.f;
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
			ob.getCompressedVoxels().resize(voxel_data_size);
			if(voxel_data_size > 0)
				stream.readData(ob.getCompressedVoxels().data(), voxel_data_size);
		}
	}


	// Set ephemeral state
	ob.state = WorldObject::State_Alive;
}


void WorldObject::writeToNetworkStream(OutStream& stream) const // Write without version
{
	::writeToStream(uid, stream);
	stream.writeUInt32((uint32)object_type);
	stream.writeStringLengthFirst(model_url);

	// Write materials
	stream.writeUInt32((uint32)materials.size());
	for(size_t i=0; i<materials.size(); ++i)
		::writeToStream(*materials[i], stream);

	stream.writeStringLengthFirst(lightmap_url); // new in v13

	stream.writeStringLengthFirst(script);
	stream.writeStringLengthFirst(content);
	stream.writeStringLengthFirst(target_url);

	::writeToStream(pos, stream);
	::writeToStream(axis, stream);
	stream.writeFloat(angle);
	::writeToStream(scale, stream);

	created_time.writeToStream(stream); // new in v5
	::writeToStream(creator_id, stream); // new in v5

	stream.writeUInt32(flags); // new in v11

	stream.writeStringLengthFirst(creator_name);

	stream.writeData(aabb_ws.min_.x, sizeof(float) * 3); // new in v14
	stream.writeData(aabb_ws.max_.x, sizeof(float) * 3);

	stream.writeInt32(max_model_lod_level); // new in v15

	if(object_type == WorldObject::ObjectType_VoxelGroup)
	{
		// Write compressed voxel data
		stream.writeUInt32((uint32)compressed_voxels.size());
		if(compressed_voxels.size() > 0)
			stream.writeData(compressed_voxels.data(), compressed_voxels.dataSizeBytes());
	}
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

	pos = other.pos;
	axis = other.axis;
	angle = other.angle;

	scale = other.scale;

	created_time = other.created_time;
	creator_id = other.creator_id;

	flags = other.flags;

	creator_name = other.creator_name;

	compressed_voxels = other.compressed_voxels;

	aabb_ws = other.aabb_ws;

	max_model_lod_level = other.max_model_lod_level;
}


void readFromNetworkStreamGivenUID(InStream& stream, WorldObject& ob) // UID will have been read already
{
	// NOTE: The data in here needs to match that in copyNetworkStateFrom()

	ob.object_type = (WorldObject::ObjectType)stream.readUInt32(); // TODO: handle invalid values?
	ob.model_url = stream.readStringLengthFirst(10000);
	//if(v >= 2)
	{
		const size_t num_mats = stream.readUInt32();
		ob.materials.resize(num_mats);
		for(size_t i=0; i<ob.materials.size(); ++i)
		{
			if(ob.materials[i].isNull())
				ob.materials[i] = new WorldMaterial();
			readFromStream(stream, *ob.materials[i]);
		}
	}

	ob.lightmap_url = stream.readStringLengthFirst(10000);

	ob.script = stream.readStringLengthFirst(10000);
	ob.content = stream.readStringLengthFirst(10000);
	ob.target_url = stream.readStringLengthFirst(10000);

	ob.pos = readVec3FromStream<double>(stream);
	ob.axis = readVec3FromStream<float>(stream);
	ob.angle = stream.readFloat();

	//if(v >= 3)
		ob.scale = readVec3FromStream<float>(stream);

	ob.created_time.readFromStream(stream);
	ob.creator_id = readUserIDFromStream(stream);

	ob.flags = stream.readUInt32();

	ob.creator_name = stream.readStringLengthFirst(10000);

	stream.readData(ob.aabb_ws.min_.x, sizeof(float) * 3);
	ob.aabb_ws.min_.x[3] = 1.f;
	stream.readData(ob.aabb_ws.max_.x, sizeof(float) * 3);
	ob.aabb_ws.max_.x[3] = 1.f;

	ob.max_model_lod_level = stream.readInt32();

	if(ob.object_type == WorldObject::ObjectType_VoxelGroup)
	{
		// Read compressed voxel data
		const uint32 voxel_data_size = stream.readUInt32();
		if(voxel_data_size > 1000000)
			throw glare::Exception("Invalid voxel_data_size (too large): " + toString(voxel_data_size));

		// Read voxel data
		ob.getCompressedVoxels().resize(voxel_data_size);
		if(voxel_data_size > 0)
			stream.readData(ob.getCompressedVoxels().data(), voxel_data_size);
	}

	// Set ephemeral state
	//ob.state = WorldObject::State_Alive;
}


const Matrix4f obToWorldMatrix(const WorldObject& ob)
{
	const Vec4f pos((float)ob.pos.x, (float)ob.pos.y, (float)ob.pos.z, 1.f);

	// Don't use a zero scale component, because it makes the matrix uninvertible, which breaks various things, including picking and normals.
	Vec3f use_scale = ob.scale;
	if(use_scale.x == 0) use_scale.x = 1.0e-6f;
	if(use_scale.y == 0) use_scale.y = 1.0e-6f;
	if(use_scale.z == 0) use_scale.z = 1.0e-6f;

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
		compressed_voxels.capacitySizeBytes() + (voxel_group.voxels.capacity() * sizeof(Voxel));
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


void WorldObject::compressVoxelGroup(const VoxelGroup& group, js::Vector<uint8, 16>& compressed_data_out)
{
	size_t max_bucket = 0;
	for(size_t i=0; i<group.voxels.size(); ++i)
		max_bucket = myMax<size_t>(max_bucket, group.voxels[i].mat_index);

	const size_t num_buckets = max_bucket + 1;

	// Step 1: sort by materials
	js::Vector<Voxel, 16> sorted_voxels(group.voxels.size());
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
		const int count = (int)counts[z];
		data[write_i++] = count; // Wriite count of voxels with that material

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

	compressed_data_out.resizeNoCopy(compressed_bound);
	
	const size_t compressed_size = ZSTD_compress(compressed_data_out.data(), compressed_data_out.size(), data.data(), data.dataSizeBytes(),
		ZSTD_CLEVEL_DEFAULT // compression level
	);

	compressed_data_out.resize(compressed_size);

	// conPrint("uncompressed size:      " + toString(group.voxels.size() * sizeof(Voxel)) + " B");
	// conPrint("compressed_size:        " + toString(compressed_size) + " B");
	// const double ratio = (double)group.voxels.size() * sizeof(Voxel) / compressed_size;
	// conPrint("compression ratio: " + toString(ratio));

	//TEMP: decompress and check we get the same value
#ifndef NDEBUG
	VoxelGroup group2;
	decompressVoxelGroup(compressed_data_out.data(), compressed_data_out.size(), group2);
	assert(group2.voxels == sorted_voxels);
#endif
}


void WorldObject::decompressVoxelGroup(const uint8* compressed_data, size_t compressed_data_len, VoxelGroup& group_out)
{
	group_out.voxels.clear();

	const uint64 decompressed_size = ZSTD_getDecompressedSize(compressed_data, compressed_data_len);

	//js::Vector<int, 16> decompressed_data(compressed_data_len);
	BufferInStream instream;
	instream.buf.resizeNoCopy(decompressed_size);

	//ZSTD_decompress(decompressed_data.data(), decompressed_size, compressed_data, compressed_data_len);
	ZSTD_decompress(instream.buf.data(), decompressed_size, compressed_data, compressed_data_len);

	//size_t read_i = 0;

	//int num_mats;
	//std::memcpy(&num_mats, &decompressed_data[read_i++], sizeof(int));
	Vec3<int> current_pos(0, 0, 0);

	const int num_mats = instream.readInt32();
	for(int m=0; m<num_mats; ++m)
	{
		const int count = instream.readInt32();
		for(int i=0; i<count; ++i)
		{
			Vec3<int> relative_pos;
			instream.readData(&relative_pos, sizeof(Vec3<int>));

			const Vec3<int> pos = current_pos + relative_pos;

			group_out.voxels.push_back(Voxel(pos, m));

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
		compressVoxelGroup(this->voxel_group, this->compressed_voxels);
	}
	else
		this->compressed_voxels.clear();
}


void WorldObject::decompressVoxels()
{ 
	if(!this->compressed_voxels.empty()) // If there are compressed voxels:
		decompressVoxelGroup(this->compressed_voxels.data(), this->compressed_voxels.size(), this->voxel_group); // Decompress to voxel_group.
	else
		this->voxel_group.voxels.clear(); // Else there are no compressed voxels, so effectively decompress to zero voxels.

	// conPrint("decompressVoxels: decompressed to " + toString(this->voxel_group.voxels.size()) + " voxels.");
}


void WorldObject::clearDecompressedVoxels()
{
	this->voxel_group.voxels.clearAndFreeMem();
	//this->voxel_group.voxels = std::vector<Voxel>();
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
