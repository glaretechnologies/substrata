/*=====================================================================
WorldObject.h
-------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "DependencyURL.h"
#include "WorldMaterial.h"
#include "../shared/UID.h"
#include "../shared/UserID.h"
#include <utils/TimeStamp.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/Reference.h>
#include <utils/Vector.h>
#include <utils/SharedImmutableArray.h>
#include <utils/AllocatorVector.h>
#include <utils/DatabaseKey.h>
#include <utils/STLArenaAllocator.h>
#include <maths/vec3.h>
#include <maths/Quat.h>
#include <physics/jscol_aabbox.h>
#include <indigo/DiscreteDistribution.h>
#if GUI_CLIENT
#include <opengl/OpenGLTextureKey.h>
//#include "../gui_client/MeshManager.h"
//#include <graphics/ImageMap.h>
#endif
#include <string>
#include <vector>
#include <set>
#include <new>
struct GLObject;
struct GLLight;
class PhysicsObject;
class RandomAccessInStream;
class RandomAccessOutStream;
namespace glare { class AudioSource; }
namespace glare { class FastPoolAllocator; }
namespace glare { class ArenaAllocator; }
namespace Scripting { class VehicleScript; }
class ResourceManager;
class WinterShaderEvaluator;
class LuaScriptEvaluator;
class ObjectEventHandlers;
class Matrix4f;
class RayMesh;
namespace Indigo { class SceneNodeModel; }
namespace js { class AABBox; }
class WebViewData;
class BrowserVidPlayer;
struct AnimatedTexObData;
struct MeshData;
struct PhysicsShapeData;
class GLUITextView;
class UInt8ComponentValueTraits;
template <class V, class ComponentValueTraits> class ImageMap;
namespace pugi { class xml_node; }


class Voxel
{
public:
	Voxel(const Vec3<int>& pos_, int mat_index_) : pos(pos_), mat_index(mat_index_) {}
	Voxel() {}

	bool operator == (const Voxel& other) const { return pos == other.pos && mat_index == other.mat_index; }
	bool operator != (const Voxel& other) const { return pos != other.pos || mat_index != other.mat_index; }

	Vec3<int> pos;
	int mat_index; // Index into materials
};


class VoxelGroup
{
public:
	// Iterate over voxels and get voxel position bounds
	js::AABBox getAABB() const;

	glare::AllocatorVector<Voxel, 16> voxels;
};


class WorldObject;
void doDestroyOb(WorldObject* ob);

// Template specialisation of destroyAndFreeOb for WorldObject.  This is called when being freed by a Reference.
// We will use this to free from the WorldState PoolMap if the object was allocated from there.
template <>
inline void destroyAndFreeOb<WorldObject>(WorldObject* ob)
{
	doDestroyOb(ob);
}


//namespace glare {
//	template <class A, class B, class C> class PoolMap;
//}
struct UIDHasher;


struct ObScatteringInfo : public ThreadSafeRefCounted
{
	js::AABBox aabb_ws;
	Reference<RayMesh> raymesh; // for list of triangles
	DiscreteDistribution uniform_dist; // Used for sampling a point on the object surface uniformly wrt. surface area.
	float total_surface_area; // Total surface area of mesh
};


struct InstanceInfo
{
	~InstanceInfo();

	GLARE_ALIGNED_16_NEW_DELETE

#if GUI_CLIENT
	// Instances are drawn with OpenGL instancing, so don't need their own opengl object.
	Reference<PhysicsObject> physics_object;

	int instance_index;
	int num_instances; // number of instances of the prototype object that this is object is an instance of.

	Reference<WinterShaderEvaluator> script_evaluator;

	WorldObject* prototype_object; // This is the object this instance is an instance of.

	Vec3d pos;
	Vec3f axis;
	float angle;
	Vec3f scale;
	Vec4f translation; // As computed by a script.  Translation from current position in pos.
#endif
};


/*=====================================================================
WorldObject
-----------

=====================================================================*/
class WorldObject // : public ThreadSafeRefCounted
{
public:
	WorldObject() noexcept;
	~WorldObject();

	GLARE_ALIGNED_16_NEW_DELETE

	// For placement new in PoolMap:
#if __cplusplus >= 201703L
	void* operator new  (size_t size, std::align_val_t alignment, void* ptr) { return ptr; }
	void* operator new  (size_t size, void* ptr) { return ptr; }
#else
	void* operator new  (size_t size, void* ptr) { return ptr; }
#endif

	struct GetLODModelURLOptions
	{
		GetLODModelURLOptions(bool get_optimised_mesh_, int opt_mesh_version_) : get_optimised_mesh(get_optimised_mesh_), opt_mesh_version(opt_mesh_version_), allocator(nullptr) {}
		bool get_optimised_mesh;
		int opt_mesh_version;
		glare::ArenaAllocator* allocator;
	};

	static URLString getLODModelURLForLevel(const URLString& base_model_url, int level, const GetLODModelURLOptions& options);
	static int getLODLevelForURL(const URLString& URL); // Identifies _lod1 etc. suffix.
	static URLString getLODLightmapURLForLevel(const URLString& base_lightmap_url, int level);
#if GUI_CLIENT
	static OpenGLTextureKey getLODLightmapPathForLevel(const OpenGLTextureKey& base_lightmap_path, int level);
#endif
	static URLString makeOptimisedMeshURL(const URLString& base_model_url, int lod_level, bool get_optimised_mesh, int opt_mesh_version, glare::ArenaAllocator* allocator = nullptr);

	inline int getLODLevel(const Vec3d& campos) const;
	inline int getLODLevel(const Vec4f& campos) const;
	inline float getMaxDistForLODLevel(int level);
	inline int getLODLevel(float cam_to_ob_d2) const;
	int getModelLODLevel(const Vec3d& campos) const; // getLODLevel() clamped to max_model_lod_level, also clamped to >= 0.
	int getModelLODLevelForObLODLevel(int ob_lod_level) const; // getLODLevel() clamped to max_model_lod_level, also clamped to >= 0.
	URLString getLODModelURL(const Vec3d& campos, const GetLODModelURLOptions& options) const; // Using lod level clamped to max_model_lod_level

	// Sometimes we are not interested in all dependencies, such as lightmaps.  So make returning those optional.
	struct GetDependencyOptions
	{
		GetDependencyOptions() : include_lightmaps(true), use_basis(true), get_optimised_mesh(false), opt_mesh_version(-1), allocator(nullptr) {}
		bool include_lightmaps;
		bool use_basis;
		bool get_optimised_mesh;
		int opt_mesh_version;
		glare::ArenaAllocator* allocator;
	};
	void appendDependencyURLs(int ob_lod_level, const GetDependencyOptions& options, DependencyURLVector& URLs_out) const;
	void appendDependencyURLsForAllLODLevels(const GetDependencyOptions& options, DependencyURLVector& URLs_out) const;
	void appendDependencyURLsBaseLevel(const GetDependencyOptions& options, DependencyURLVector& URLs_out) const;

	

	void getDependencyURLSet(int ob_lod_level, const GetDependencyOptions& options, DependencyURLSet& URLS_out) const;
	void getDependencyURLSetForAllLODLevels(const GetDependencyOptions& options, DependencyURLSet& URLS_out) const;
	void getDependencyURLSetBaseLevel(const GetDependencyOptions& options, DependencyURLSet& URLS_out) const;

	void convertLocalPathsToURLS(ResourceManager& resource_manager);

	void getInterpolatedTransform(double cur_time, Vec3d& pos_out, Quatf& rot_out) const;
	void setTransformAndHistory(const Vec3d& pos, const Vec3f& axis, float angle);
	void setPosAndHistory(const Vec3d& pos);
	inline bool isCollidable() const;
	inline void setCollidable(bool c);
	inline bool isDynamic() const;
	inline void setDynamic(bool c);
	inline bool isSensor() const;
	inline void setIsSensor(bool c);

	size_t getTotalMemUsage() const;

	static int getLightMapSideResForAABBWS(const js::AABBox& aabb_ws);

	static Reference<glare::SharedImmutableArray<uint8> > compressVoxelGroup(const VoxelGroup& group);
	static void decompressVoxelGroup(const uint8* compressed_data, size_t compressed_data_len, glare::Allocator* mem_allocator, VoxelGroup& group_out);
	void compressVoxels();
	void decompressVoxels();
	void clearDecompressedVoxels();

	//VoxelGroup& getDecompressedVoxelGroup() { return voxel_group; }
	const VoxelGroup& getDecompressedVoxelGroup() const { return voxel_group; }
	      glare::AllocatorVector<Voxel, 16>& getDecompressedVoxels()       { return voxel_group.voxels; }
	const glare::AllocatorVector<Voxel, 16>& getDecompressedVoxels() const { return voxel_group.voxels; }

	      Reference<glare::SharedImmutableArray<uint8> > getCompressedVoxels()       { return compressed_voxels; }
	const Reference<glare::SharedImmutableArray<uint8> > getCompressedVoxels() const { return compressed_voxels; }

	void setCompressedVoxels(Reference<glare::SharedImmutableArray<uint8> > v);


	void writeToStream(RandomAccessOutStream& stream) const;
	void writeToNetworkStream(RandomAccessOutStream& stream) const; // Write without version

	void copyNetworkStateFrom(const WorldObject& other);

	std::string serialiseToXML(int tab_depth) const;
	static Reference<WorldObject> loadFromXMLElem(const std::string& object_file_path, bool convert_rel_paths_to_abs_disk_paths, pugi::xml_node elem); // object_file_path is used for converting relative paths to absolute.

	void setAABBOS(const js::AABBox& aabb_os); // Sets object-space AABB, also calls transformChanged().
	void zeroAABBOS();
	inline const js::AABBox& getAABBOS() const { return aabb_os; }
	inline       js::AABBox  getAABBWS() const;

	inline const Vec4f getCentroidWS() const { return centroid_ws; }

	inline const Matrix4f obToWorldMatrix() const;
	inline const Matrix4f worldToObMatrix() const;

	inline float getAABBWSLongestLength() const { assert(aabb_ws_longest_len >= 0); return aabb_ws_longest_len; }	// == getAABBWS().longestLength()
	inline float getBiasedAABBLength() const    { assert(biased_aabb_len >= 0);     return biased_aabb_len; }		// == getAABBWS().longestLength() * lod_bias_factor.  lod_bias_factor can be something like 2 for voxel objects to push out the transition distances a bit.

	void transformChanged(); // Rebuild centroid_ws, aabb_ws_longest_len, biased_aabb_len
	inline void doTransformChanged(const Matrix4f& ob_to_world, const Vec4f& use_scale); // Rebuild centroid_ws, aabb_ws_longest_len, biased_aabb_len
	inline void doTransformChangedIgnoreRotation(const Vec4f& position, const Vec4f& use_scale); // Rebuild centroid_ws, aabb_ws_longest_len, biased_aabb_len

	inline static size_t maxNumMaterials() { return 2048; } // There's an object in the cryptovoxels world with 1398 materials.

	// Gets event_handlers.  If event_handlers is null, sets to a new ObjectEventHandlers object first. 
	Reference<ObjectEventHandlers> getOrCreateEventHandlers();

	enum ObjectType
	{
		ObjectType_Generic = 0,
		ObjectType_Hypercard = 1,
		ObjectType_VoxelGroup = 2,
		ObjectType_Spotlight = 3,
		ObjectType_WebView = 4,
		ObjectType_Video = 5, // A Youtube or Twitch video, or mp4 video, with video-specific UI.
		ObjectType_Text = 6 // Text displayed on a quad
	};
	static const uint64 NUM_OBJECT_TYPES = 7;

	static std::string objectTypeString(ObjectType t);
	static ObjectType objectTypeForString(const std::string& ob_type_string);

	static void test();

public:
	// Group centroid_ws, current_lod_level, biased_aabb_len and in_proximity together in first cache line (64 B) to make MainWindow::checkForLODChanges() fast.
	Vec4f centroid_ws; // Object-space AABB centroid transformed to world space.
private:
	float aabb_ws_longest_len;	// == getAABBWS().longestLength()
	float biased_aabb_len;		// == getAABBWS().longestLength() * lod_bias_factor.  lod_bias_factor can be something like 2 for voxel objects to push out the transition distances a bit.
public:
	int current_lod_level; // LOD level as a function of distance from camera etc.. Kept up to date.
	bool in_proximity; // Is the object currently in load proximity to camera?
	bool in_script_proximity; // Is the object currently in script proximity to camera?  For onUserMovedNearToObject and onUserMovedAwayFromObject events.
	bool in_audio_proximity;
	bool exclude_from_lod_chunk_mesh; // Equal to BitUtils::isBitSet(ob->flags, WorldObject::EXCLUDE_FROM_LOD_CHUNK_MESH), but placed in first object cache line.
private:
	js::AABBox aabb_os; // Object-space AABB
public:
	UID uid;
	ObjectType object_type;

	static const size_t MAX_URL_SIZE                      = 1000;
	static const size_t MAX_SCRIPT_SIZE                   = 10000;
	static const size_t MAX_CONTENT_SIZE                  = 10000;
	

	URLString model_url;
	std::vector<WorldMaterialRef> materials;
	URLString lightmap_url;
	std::string script;
	std::string content; // For ObjectType_Hypercard, ObjectType_Text
	std::string target_url;
	Vec3d pos;
	Vec3f axis;
	float angle;
	Vec3f scale;

	static const uint32 COLLIDABLE_FLAG                         = 1; // Is this object solid from the point of view of the physics engine?
	static const uint32 LIGHTMAP_NEEDS_COMPUTING_FLAG           = 2; // Does the lightmap for this object need to be built or rebuilt?
	static const uint32 HIGH_QUAL_LIGHTMAP_NEEDS_COMPUTING_FLAG = 4; // Does a high-quality lightmap for this object need to be built or rebuilt?
	static const uint32 DYNAMIC_FLAG                            = 8; // Is this object a dynamic object (moving object) from the point of view of the physics engine?
	static const uint32 SUMMONED_FLAG                           = 16; // Is this object a vehicle object that was summoned by a user?
	static const uint32 VIDEO_AUTOPLAY                          = 32; // For video objects, should the video auto-play?
	static const uint32 VIDEO_LOOP                              = 64; // For video objects, should the video loop?
	static const uint32 VIDEO_MUTED                             = 128; // For video objects, should the video be initially muted?
	static const uint32 IS_SENSOR_FLAG                          = 256; // Is this a physics sensor?
	static const uint32 EXCLUDE_FROM_LOD_CHUNK_MESH             = 512; // Should this object be excluded from LOD Chunk meshes? (for e.g. moving objects)
	static const uint32 AUDIO_AUTOPLAY                          = 1024; // For objects that play audio, should the audio auto-play?
	static const uint32 AUDIO_LOOP                              = 2048; // For objects that play audio, should the audio loop?
	uint32 flags;

	TimeStamp created_time;
	TimeStamp last_modified_time;
	UserID creator_id;

	std::string creator_name; // This is 'denormalised' data that is not saved on disk, but set on load from disk or creation.  It is transferred across the network though.

	int max_model_lod_level; // maximum LOD level for model.  0 for models that don't have lower LOD versions.

	float mass; // For physics engine: Mass of object in kg.
	float friction; // "Friction of the body (dimensionless number, usually between 0 and 1, 0 = no friction, 1 = friction force equals force that presses the two bodies together)"
	float restitution; // "Restitution of body (dimensionless number, usually between 0 and 1, 0 = completely inelastic collision response, 1 = completely elastic collision response)"
	Vec3f centre_of_mass_offset_os;


	// sub-range of the indices from the LOD chunk geometry that correspond to this object.
	// batch 0 is opaque triangles, batch 1 is transparent triangles.
	uint32 chunk_batch0_start; // start index
	uint32 chunk_batch0_end; // end index
	uint32 chunk_batch1_start;
	uint32 chunk_batch1_end;


	uint32 physics_owner_id;
	double last_physics_ownership_change_global_time; // Last change or renewal time.

#if GUI_CLIENT
	Reference<glare::AudioSource> audio_source;
#endif
	URLString audio_source_url;
	float audio_volume;

	enum State
	{
		State_JustCreated = 0,
		State_InitialSend,
		State_Alive,
		State_Dead
	};

	State state;
	bool from_remote_transform_dirty; // Transformation has been changed remotely
	bool from_remote_physics_transform_dirty; // Transformation has been changed remotely
	bool from_remote_summoned_dirty;  // Object has been summoned remotely, changing transformation.
	bool from_remote_other_dirty;     // Something else has been changed remotely
	bool from_remote_lightmap_url_dirty; // Lightmap URL has been changed remotely
	bool from_remote_model_url_dirty; // Model URL has been changed remotely
	bool from_remote_content_dirty; // Content has changed remotely.
	bool from_remote_flags_dirty;     // Flags have been changed remotely
	bool from_remote_physics_ownership_dirty; // Physics ownership has been changed remotely.

	bool from_local_transform_dirty;  // Transformation has been changed locally
	bool from_local_other_dirty;      // Something else has been changed locally
	bool from_local_physics_dirty; // The physics engine has changed the state of an object locally.

	bool was_just_created; // True if object was created from a State_JustCreated message, false if from a State_InitialSend message.

	uint32 last_transform_update_avatar_uid; // Avatar UID of last client that sent the last ObjectTransformUpdate or ObjectPhysicsTransformUpdate for this message.
	double last_transform_client_time;

	static const uint32 AUDIO_SOURCE_URL_CHANGED	= 1; // Set when audio_source_url is changed
	static const uint32 SCRIPT_CHANGED				= 2; // Set when script is changed
	static const uint32 CONTENT_CHANGED				= 4; // Set when script is changed
	static const uint32 MODEL_URL_CHANGED			= 8;
	static const uint32 DYNAMIC_CHANGED				= 16;
	static const uint32 PHYSICS_VALUE_CHANGED		= 32;
	static const uint32 PHYSICS_OWNER_CHANGED		= 64;
	uint32 changed_flags;

	bool using_placeholder_model;

	Vec4f translation; // As computed by a script.  Translation from current position in pos.

	DatabaseKey database_key;

#if GUI_CLIENT
	js::Vector<InstanceInfo> instances;

	Reference<GLObject> opengl_engine_ob;
	Reference<GLLight> opengl_light;
	Reference<PhysicsObject> physics_object;

	Reference<GLObject> edit_aabb; // Visualisation of the object bounding box, for editing, for decal objects etc.

	Reference<GLObject> diagnostics_gl_ob; // For diagnostics visualisation
	Reference<GLObject> diagnostics_unsmoothed_gl_ob; // For diagnostics visualisation

	Reference<GLUITextView> diagnostic_text_view; // For diagnostics visualisation


	Reference<MeshData> mesh_manager_data; // Hang on to a reference to the mesh data, so when object-uses of it are removed, it can be removed from the MeshManager with meshDataBecameUnused().
	Reference<PhysicsShapeData> mesh_manager_shape_data; // Likewise for the physics mesh data.

	enum AudioState
	{
		AudioState_NotLoaded,
		AudioState_Loading, // The audio resource is downloading, or a LoadAudioTask has been constructed and is queued and/or executing.
		AudioState_Loaded,
		AudioState_ErrorWhileLoading
	};
	AudioState audio_state;

	Reference<ImageMap<uint8, UInt8ComponentValueTraits> > hypercard_map;

#if INDIGO_SUPPORT
	Reference<Indigo::SceneNodeModel> indigo_model_node;
#endif

	bool is_selected;

	//Vec3d last_pos; // Used by proximity loader

	bool lightmap_baking; // Is lightmap baking in progress for this object?

	bool use_materialise_effect_on_load; // When the opengl object is loaded, enable materialise effect on the materials.
	float materialise_effect_start_time;

	Reference<WinterShaderEvaluator> script_evaluator; // Winter script evaluator

	Reference<Scripting::VehicleScript> vehicle_script;

	js::Vector<Matrix4f, 16> instance_matrices;

	int loading_or_loaded_model_lod_level; // The LOD level we have started (or finished) loading the model for this object at.  Can be different than loading_or_loaded_lod_level due to max_model_lod_level clamping.
	int loading_or_loaded_lod_level; // Lod level for textures etc.. 
	// Both these lod levels will be reset to -10 if the model is unloaded due to being out of camera proximity, so that the model can be reloaded.

	bool is_path_controlled; // Is this object controlled by a path controller script?  If so, we want to set the OpenGL transform from the physics engine.

	Reference<WebViewData> web_view_data;
	Reference<BrowserVidPlayer> browser_vid_player;
	Reference<AnimatedTexObData> animated_tex_data;


	//Reference<ObScatteringInfo> scattering_info;


	// For objects that are path controlled:
	int waypoint_index;
	float dist_along_segment;

	double last_touch_event_time;
#endif // GUI_CLIENT

	float max_load_dist2;
	
	/*
		Snapshots for client-side interpolation purposes.
		next_snapshot_i: index (before modulo) to write next snapshot in.  (e.g. written index is next_snapshot_i % HISTORY_BUF_SIZE)
		pos_snapshots[(next_snapshot_i - 1) % HISTORY_BUF_SIZE] is the last received update, received at time last_snapshot_time.
		pos_snapshots[(next_snapshot_i - 2) % HISTORY_BUF_SIZE] is the update received before that, will be considerd to be received at last_snapshot_time - update_send_period.
	*/
	static const int HISTORY_BUF_SIZE = 4;
	uint32 next_snapshot_i;
	uint32 next_insertable_snapshot_i; // Next insertable physics snapshot index.

	struct Snapshot
	{
		Vec4f pos;
		Quatf rotation;
		Vec4f linear_vel;
		Vec4f angular_vel;
		double client_time; // global time on client when it took this snapshot.
		double local_time; // Clock::getTimeSinceInit() when this snapshot was received locally.
	};

	Snapshot snapshots[HISTORY_BUF_SIZE];

	bool snapshots_are_physics_snapshots; // Physics snapshots have different semantics than basic transform snapshots.
	double transmission_time_offset;

	Vec4f linear_vel; // Just for storing before sending out in a ObjectPhysicsTransformUpdate message.
	Vec4f angular_vel;

	Reference<LuaScriptEvaluator> lua_script_evaluator;

	Reference<ObjectEventHandlers> event_handlers;

private:
	VoxelGroup voxel_group;
	Reference<glare::SharedImmutableArray<uint8> > compressed_voxels;
public:
	uint64 compressed_voxels_hash;

	glare::FastPoolAllocator* allocator; // Non-null if this object was allocated from the allocator
	int allocation_index;

	// From ThreadSafeRefCounted.  Manually define this stuff here, so refcount can be defined not at the start of the structure, which wastes space due to alignment issues.
	inline glare::atomic_int decRefCount() const { return refcount.decrement(); }
	inline void incRefCount() const { refcount.increment(); }
	inline glare::atomic_int getRefCount() const { return refcount; }
	mutable glare::AtomicInt refcount;
};


typedef Reference<WorldObject> WorldObjectRef;


bool WorldObject::isCollidable() const
{
	return (flags & COLLIDABLE_FLAG) != 0;
}


void WorldObject::setCollidable(bool c)
{
	if(c)
		flags |= COLLIDABLE_FLAG;
	else
		flags &= ~COLLIDABLE_FLAG;
}


bool WorldObject::isDynamic() const
{
	return (flags & DYNAMIC_FLAG) != 0;
}


void WorldObject::setDynamic(bool c)
{
	if(c)
		flags |= DYNAMIC_FLAG;
	else
		flags &= ~DYNAMIC_FLAG;
}


bool WorldObject::isSensor() const
{
	return (flags & IS_SENSOR_FLAG) != 0;
}


void WorldObject::setIsSensor(bool c)
{
	if(c)
		flags |= IS_SENSOR_FLAG;
	else
		flags &= ~IS_SENSOR_FLAG;
}


void readWorldObjectFromStream(RandomAccessInStream& stream, WorldObject& ob);
void readWorldObjectFromNetworkStreamGivenUID(RandomAccessInStream& stream, WorldObject& ob); // UID will have been read already


const Matrix4f obToWorldMatrix(const WorldObject& ob);
const Matrix4f worldToObMatrix(const WorldObject& ob);
const Vec3f useScaleForWorldOb(const Vec3f& scale); // Don't use a zero scale component, because it makes the ob-to-world matrix uninvertible, which breaks various things, including picking and normals.

// Throws glare::Exception if transform not OK, for example if any components are infinite or NaN. 
void checkTransformOK(const WorldObject* ob);


const Matrix4f WorldObject::obToWorldMatrix() const { return ::obToWorldMatrix(*this); }
const Matrix4f WorldObject::worldToObMatrix() const { return ::worldToObMatrix(*this); }


struct WorldObjectRefHash
{
	size_t operator() (const WorldObjectRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};


int WorldObject::getLODLevel(const Vec3d& campos) const
{
	return getLODLevel(campos.toVec4fPoint());
}


int WorldObject::getLODLevel(const Vec4f& campos) const
{
	// proj_len = aabb_ws.longestLength() / ||campos - pos||
	assert(biased_aabb_len >= 0); // Check biased_aabb_len has been computed (should be >= 0 if so, it's set to -1 in constructor).
	assert(this->centroid_ws[3] == 1.f);

	const float recip_dist = (campos - this->centroid_ws).fastApproxRecipLength();
	const float proj_len = biased_aabb_len * recip_dist;

	if(proj_len > 0.6f)
		return -1;
	else if(proj_len > 0.16f)
		return 0;
	else if(proj_len > 0.03f)
		return 1;
	else
		return 2;
}


/*
proj_len = aabb_ws.longestLength() / dist_to_cam

lod level 2 if proj_len <= 0.03 

= aabb_ws.longestLength() / dist_to_cam <= 0.03

= aabb_ws.longestLength() <= 0.03 * dist_to_cam

= aabb_ws.longestLength() / 0.03 <= dist_to_cam
*/
float WorldObject::getMaxDistForLODLevel(int level)
{
	const float eps_factor = 1.001f; // Make distance slightly larger to account for fastApproxRecipLength() usage in getLODLevel().
	if(level == -1)
		return biased_aabb_len * (eps_factor / 0.6f);
	else if(level == 0)
		return biased_aabb_len * (eps_factor  / 0.16f);
	else if(level == 1)
		return biased_aabb_len * (eps_factor  / 0.03f);
	else
		return std::numeric_limits<float>::max();
}


int WorldObject::getLODLevel(float cam_to_ob_d2) const
{
	assert(biased_aabb_len >= 0); // Check biased_aabb_len has been computed (should be >= 0 if so, it's set to -1 in constructor).

	const float recip_dist = _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(cam_to_ob_d2)));
	//assert(epsEqual(recip_dist, 1 / sqrt(cam_to_ob_d2)));

	const float proj_len = biased_aabb_len * recip_dist;

	if(proj_len > 0.6f)
		return -1;
	else if(proj_len > 0.16f)
		return 0;
	else if(proj_len > 0.03f)
		return 1;
	else
		return 2;
}


js::AABBox WorldObject::getAABBWS() const
{
	return this->aabb_os.transformedAABBFast(this->obToWorldMatrix());
}


void WorldObject::doTransformChanged(const Matrix4f& ob_to_world, const Vec4f& use_scale) // Rebuild centroid_ws, aabb_ws_longest_len, biased_aabb_len
{
	this->centroid_ws = ob_to_world * aabb_os.centroid();

	// Translations and rotations don't change the longest AABB side length, just scaling.
	this->aabb_ws_longest_len = horizontalMax(((aabb_os.max_ - aabb_os.min_) * use_scale).v);

	this->biased_aabb_len = this->aabb_ws_longest_len;

	// For voxel objects, push out the transition distances a bit.
	if(object_type == ObjectType_VoxelGroup)
		this->biased_aabb_len *= 2;

	if(BitUtils::isBitSet(flags, SUMMONED_FLAG))
		this->biased_aabb_len *= 4;
}


// Rebuild centroid_ws, aabb_ws_longest_len, biased_aabb_len, ignoring rotation part of object-to-world transformation.
// See evalObjectScript() in Scripting.cpp
void WorldObject::doTransformChangedIgnoreRotation(const Vec4f& use_position, const Vec4f& use_scale) 
{
	assert(use_scale[3] == 0);
	assert(use_position[3] == 1);
	this->centroid_ws = aabb_os.centroid() * use_scale + use_position;

	this->aabb_ws_longest_len = horizontalMax(((aabb_os.max_ - aabb_os.min_) * use_scale).v);

	this->biased_aabb_len = this->aabb_ws_longest_len;

	// For voxel objects, push out the transition distances a bit.
	if(object_type == ObjectType_VoxelGroup)
		this->biased_aabb_len *= 2;

	if(BitUtils::isBitSet(flags, SUMMONED_FLAG))
		this->biased_aabb_len *= 4;
}
