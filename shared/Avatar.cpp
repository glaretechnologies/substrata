/*=====================================================================
Avatar.cpp
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-12 12:24:54 +1300
=====================================================================*/
#include "Avatar.h"


#include "ResourceManager.h"
#include "WorldObject.h"
#if GUI_CLIENT
#include "opengl/OpenGLEngine.h"
#include "opengl/OpenGLMeshRenderData.h"
#include "opengl/ui/GLUIImage.h"
#include "../gui_client/MeshManager.h"
#include "../gui_client/AvatarGraphics.h"
#include "../gui_client/HoverCarPhysics.h"
#include "../audio/AudioEngine.h"
#endif
#include <utils/ConPrint.h>
#include <utils/StringUtils.h>
#include <utils/IncludeXXHash.h>
#include <utils/FileUtils.h>
#include <utils/FileChecksum.h>
#include <utils/RandomAccessInStream.h>
#include <utils/RandomAccessOutStream.h>
#include <physics/jscol_aabbox.h>


const Matrix4f obToWorldMatrix(const Avatar& ob)
{
	// From rotateThenTranslateMatrix in AvatarGraphics
	Matrix4f m;
	const float rot_len2 = ob.rotation.length2();
	if(rot_len2 < 1.0e-20f)
		m.setToIdentity();
	else
	{
		const float rot_len = std::sqrt(rot_len2);
		m.setToRotationMatrix(ob.rotation.toVec4fVector() / rot_len, rot_len);
	}
	m.setColumn(3, Vec4f((float)ob.pos.x, (float)ob.pos.y, (float)ob.pos.z, 1.f));
	return m * ob.avatar_settings.pre_ob_to_world_matrix;
}


Avatar::Avatar()
{
	transform_dirty = false;
	other_dirty = false;
	//opengl_engine_ob = NULL;
	//using_placeholder_model = false;

	next_snapshot_i = 0;
//	last_snapshot_time = 0;

	selected_object_uid = UID::invalidUID();

#if GUI_CLIENT
	our_avatar = false;
	audio_stream_sampling_rate = 0;
	audio_stream_id = 0;

	name_colour = Colour3f(0.8f);

	avatar_settings.pre_ob_to_world_matrix = Matrix4f::identity();

	nametag_z_offset = 0;

	use_materialise_effect_on_load = false;
	materialise_effect_start_time = -1000.f;

	vehicle_seat_index = 0;
	pending_vehicle_transition = VehicleNoChange;
	underwater = false;
	last_foam_decal_creation_time = 0;
#endif
	anim_state = 0;
	last_physics_input_bitflags = 0;
}


Avatar::~Avatar()
{}


void Avatar::appendDependencyURLs(int ob_lod_level, const GetDependencyOptions& options, DependencyURLVector& URLs_out)
{
	if(!avatar_settings.model_url.empty())
	{
		GetLODModelURLOptions url_options(options.get_optimised_mesh, options.opt_mesh_version);

		URLs_out.push_back(DependencyURL(getLODModelURLForLevel(avatar_settings.model_url, ob_lod_level, url_options)));
	}

	const WorldMaterial::GetURLOptions mat_options(options.use_basis, /*area allocator=*/nullptr);

	for(size_t i=0; i<avatar_settings.materials.size(); ++i)
		avatar_settings.materials[i]->appendDependencyURLs(mat_options, ob_lod_level, URLs_out);
}


void Avatar::appendDependencyURLsForAllLODLevels(const GetDependencyOptions& options, DependencyURLVector& URLs_out)
{
	if(!avatar_settings.model_url.empty())
	{
		GetLODModelURLOptions url_options(options.get_optimised_mesh, options.opt_mesh_version);

		URLs_out.push_back(DependencyURL(getLODModelURLForLevel(avatar_settings.model_url, 0, url_options)));
		URLs_out.push_back(DependencyURL(getLODModelURLForLevel(avatar_settings.model_url, 1, url_options)));
		URLs_out.push_back(DependencyURL(getLODModelURLForLevel(avatar_settings.model_url, 2, url_options)));
	}

	const WorldMaterial::GetURLOptions mat_options(options.use_basis, /*area allocator=*/nullptr);

	for(size_t i=0; i<avatar_settings.materials.size(); ++i)
		avatar_settings.materials[i]->appendDependencyURLsAllLODLevels(mat_options, URLs_out);
}


void Avatar::appendDependencyURLsBaseLevel(const GetDependencyOptions& options, DependencyURLVector& URLs_out) const
{
	if(!avatar_settings.model_url.empty())
	{
		GetLODModelURLOptions url_options(options.get_optimised_mesh, options.opt_mesh_version);

		URLs_out.push_back(DependencyURL(getLODModelURLForLevel(avatar_settings.model_url, 0, url_options)));
	}

	const WorldMaterial::GetURLOptions mat_options(options.use_basis, /*area allocator=*/nullptr);

	for(size_t i=0; i<avatar_settings.materials.size(); ++i)
		avatar_settings.materials[i]->appendDependencyURLsBaseLevel(mat_options, URLs_out);
}


void Avatar::getDependencyURLSet(int ob_lod_level, const GetDependencyOptions& options, DependencyURLSet& URLS_out)
{
	DependencyURLVector URLs;
	this->appendDependencyURLs(ob_lod_level, options, URLs);

	URLS_out = DependencyURLSet(URLs.begin(), URLs.end());
}


void Avatar::getDependencyURLSetForAllLODLevels(const GetDependencyOptions& options, DependencyURLSet& URLS_out)
{
	DependencyURLVector URLs;
	this->appendDependencyURLsForAllLODLevels(options, URLs);

	URLS_out = DependencyURLSet(URLs.begin(), URLs.end());
}


void Avatar::getDependencyURLSetBaseLevel(const GetDependencyOptions& options, DependencyURLSet& URLS_out) const
{
	DependencyURLVector URLs;
	this->appendDependencyURLsBaseLevel(options, URLs);

	URLS_out = DependencyURLSet(URLs.begin(), URLs.end());
}


void Avatar::convertLocalPathsToURLS(ResourceManager& resource_manager)
{
	if(FileUtils::fileExists(this->avatar_settings.model_url)) // If the URL is a local path:
		this->avatar_settings.model_url = resource_manager.URLForPathAndHash(toStdString(this->avatar_settings.model_url), FileChecksum::fileChecksum(this->avatar_settings.model_url));

	for(size_t i=0; i<avatar_settings.materials.size(); ++i)
		avatar_settings.materials[i]->convertLocalPathsToURLS(resource_manager);
}


int Avatar::getLODLevel(const Vec3d& campos) const
{
	// TEMP: just use 0 for now
	return 0;
	/*Vec4f pos_((float)pos.x, (float)pos.y, (float)pos.z, 1.f);
	const js::AABBox aabb_os( // Use approx OS AABB for now.
		pos_ - Vec4f(1.f, 1.f, 1.f, 0),
		pos_ + Vec4f(1.f, 1.f, 1.f, 0)
	);

	const js::AABBox aabb_ws = aabb_os.transformedAABBFast(obToWorldMatrix(*this));

	const float dist = campos.toVec4fVector().getDist(this->pos.toVec4fVector());
	const float proj_len = aabb_ws.longestLength() / dist;

	if(proj_len > 0.16)
		return 0;
	else if(proj_len > 0.03)
		return 1;
	else
		return 2;*/
}


float Avatar::getMaxDistForLODLevel(int level) const
{
	return std::numeric_limits<float>::max(); // TEMP
}


URLString Avatar::getLODModelURLForLevel(const URLString& base_model_url, int lod_level, const GetLODModelURLOptions& options) const
{
	if((lod_level == 0) && !options.get_optimised_mesh)
		return base_model_url;

	if(hasPrefix(base_model_url, "http:") || hasPrefix(base_model_url, "https:"))
		return base_model_url;

	return WorldObject::makeOptimisedMeshURL(base_model_url, lod_level, /*get_optimised_mesh=*/options.get_optimised_mesh, options.opt_mesh_version);
}


int Avatar::getModelLODLevelForObLODLevel(int ob_lod_level) const
{
	return 0; // TEMP just use LOD 0 for now        
	//myClamp<int>(ob_lod_level, 0, this->max_model_lod_level);
}


void Avatar::generatePseudoRandomNameColour()
{
#if GUI_CLIENT
	// Assign a pseudo-random name colour to the avatar
	const uint64 hash_r = XXH64(name.c_str(), name.size(), /*seed=*/1);
	const uint64 hash_g = XXH64(name.c_str(), name.size(), /*seed=*/2);
	const uint64 hash_b = XXH64(name.c_str(), name.size(), /*seed=*/3);

	name_colour.r = (float)((double)hash_r / (double)std::numeric_limits<uint64>::max()) * 0.7f;
	name_colour.g = (float)((double)hash_g / (double)std::numeric_limits<uint64>::max()) * 0.7f;
	name_colour.b = (float)((double)hash_b / (double)std::numeric_limits<uint64>::max()) * 0.7f;

	// conPrint("Generated name_colour=" + name_colour.toVec3().toString() + " for avatar " + name);
#endif
}


void Avatar::setTransformAndHistory(const Vec3d& pos_, const Vec3f& rotation_)
{
	pos = pos_;
	rotation = rotation_;

	for(int i=0; i<HISTORY_BUF_SIZE; ++i)
	{
		pos_snapshots[i] = pos_;
		rotation_snapshots[i] = rotation_;
		snapshot_times[i] = 0;
	}
}


void Avatar::getInterpolatedTransform(double cur_time, Vec3d& pos_out, Vec3f& rotation_out) const
{
	/*
	Timeline: check marks are snapshots received:

	|---------------|----------------|---------------|----------------|
	                                                                       ^
	                                                                      cur_time
	                                                                  ^
	                                               ^                last snapshot
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

	float t;
	if(snapshot_times[end] == snapshot_times[begin])
		t = 0;
	else
		t  = (float)((delayed_time - snapshot_times[begin]) / (snapshot_times[end] - snapshot_times[begin])); // Interpolation fraction

	pos_out      = Maths::uncheckedLerp(pos_snapshots[begin], pos_snapshots[end], t);
	rotation_out = Maths::uncheckedLerp(rotation_snapshots[begin], rotation_snapshots[end], t);

	//const double send_period = 0.1; // Time between update messages from server
	//const double delay = /*send_period * */2.0; // Objects are rendered using the interpolated state at this past time.  In normalised period coordinates.

	//const int last_snapshot_i = next_snapshot_i - 1;

	//const double frac = (cur_time - last_snapshot_time) / send_period; // Fraction of send period ahead of last_snapshot cur time is
	////printVar(frac);
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


void Avatar::copyNetworkStateFrom(const Avatar& other)
{
	// NOTE: The data in here needs to match that in readFromNetworkStreamGivenUID()
	name = other.name;
	pos = other.pos;
	rotation = other.rotation;

	avatar_settings.copyNetworkStateFrom(other.avatar_settings);
}


void AvatarSettings::copyNetworkStateFrom(const AvatarSettings& other)
{
	model_url = other.model_url;
	materials = other.materials;
	pre_ob_to_world_matrix = other.pre_ob_to_world_matrix;
}


static bool materialsEqual(const std::vector<WorldMaterialRef>& materials_a, const std::vector<WorldMaterialRef>& materials_b)
{
	if(materials_a.size() != materials_b.size())
		return false;

	for(size_t i=0; i<materials_a.size(); ++i)
	{
		if(materials_a[i].isNull() || materials_b[i].isNull())
			return false;

		if(!(*materials_a[i] == *materials_b[i]))
			return false;
	}

	return true;
}


bool AvatarSettings::operator == (const AvatarSettings& other) const
{
	return 
		model_url == other.model_url &&
		materialsEqual(materials, other.materials) &&
		pre_ob_to_world_matrix == other.pre_ob_to_world_matrix;
}


void writeAvatarSettingsToStream(const AvatarSettings& settings, RandomAccessOutStream& stream)
{
	stream.writeStringLengthFirst(settings.model_url);

	// Write materials
	stream.writeUInt32((uint32)settings.materials.size());
	for(size_t i=0; i<settings.materials.size(); ++i)
		::writeWorldMaterialToStream(*settings.materials[i], stream);

	stream.writeData(settings.pre_ob_to_world_matrix.e, sizeof(float)*16);
}


void readAvatarSettingsFromStream(RandomAccessInStream& stream, AvatarSettings& settings)
{
	settings.model_url	= stream.readStringLengthFirst(10000);

	// Read materials
	{
		const uint32 num_mats = stream.readUInt32();
		const uint32 MAX_NUM_MATS = 1024;
		if(num_mats > MAX_NUM_MATS)
			throw glare::Exception("Too many materials: " + toString(num_mats));
		settings.materials.resize(num_mats);
		for(size_t i=0; i<settings.materials.size(); ++i)
		{
			if(settings.materials[i].isNull())
				settings.materials[i] = new WorldMaterial();
			readWorldMaterialFromStream(stream, *settings.materials[i]);
		}
	}

	stream.readData(settings.pre_ob_to_world_matrix.e, sizeof(float)*16);
}



void writeAvatarToNetworkStream(const Avatar& avatar, RandomAccessOutStream& stream) // Write without version
{
	writeToStream(avatar.uid, stream);
	stream.writeStringLengthFirst(avatar.name);
	writeToStream(avatar.pos, stream);
	writeToStream(avatar.rotation, stream);
	writeAvatarSettingsToStream(avatar.avatar_settings, stream);
}


void readAvatarFromNetworkStreamGivenUID(RandomAccessInStream& stream, Avatar& avatar) // UID will have been read already
{
	avatar.name			= stream.readStringLengthFirst(10000);
	avatar.pos			= readVec3FromStream<double>(stream);
	avatar.rotation		= readVec3FromStream<float>(stream);
	readAvatarSettingsFromStream(stream, avatar.avatar_settings);
}
