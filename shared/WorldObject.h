/*=====================================================================
WorldObject.h
-------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "TimeStamp.h"
#include "DependencyURL.h"
#include "WorldMaterial.h"
#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include <Vector.h>
#include <DatabaseKey.h>
#include "../shared/UID.h"
#include "../shared/UserID.h"
#include "vec3.h"
#include <physics/jscol_aabbox.h>
#include <indigo/DiscreteDistribution.h>
#if GUI_CLIENT
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
namespace glare { class AudioSource; }
namespace glare { class PoolAllocator; }
class ResourceManager;
class WinterShaderEvaluator;
class Matrix4f;
class RayMesh;
namespace Indigo { class SceneNodeModel; }
namespace js { class AABBox; }
class WebViewData;
struct AnimatedTexObData;
struct MeshData;
class UInt8ComponentValueTraits;
template <class V, class ComponentValueTraits> class ImageMap;


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

	js::Vector<Voxel, 16> voxels;
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

	static std::string getLODModelURLForLevel(const std::string& base_model_url, int level);
	static int getLODLevelForURL(const std::string& URL); // Identifies _lod1 etc. suffix.
	static std::string getLODLightmapURL(const std::string& base_lightmap_url, int level);

	inline int getLODLevel(const Vec3d& campos) const;
	inline int getLODLevel(const Vec4f& campos) const;
	inline float getMaxDistForLODLevel(int level);
	inline int getLODLevel(float cam_to_ob_d2) const;
	int getModelLODLevel(const Vec3d& campos) const; // getLODLevel() clamped to max_model_lod_level, also clamped to >= 0.
	int getModelLODLevelForObLODLevel(int ob_lod_level) const; // getLODLevel() clamped to max_model_lod_level, also clamped to >= 0.
	std::string getLODModelURL(const Vec3d& campos) const; // Using lod level clamped to max_model_lod_level

	// Sometimes we are not interested in all dependencies, such as lightmaps.  So make returning those optional.
	struct GetDependencyOptions
	{
		GetDependencyOptions() : include_lightmaps(true) {}
		bool include_lightmaps;
	};
	void appendDependencyURLs(int ob_lod_level, const GetDependencyOptions& options, std::vector<DependencyURL>& URLs_out) const;
	void appendDependencyURLsForAllLODLevels(std::vector<DependencyURL>& URLs_out) const;
	void appendDependencyURLsBaseLevel( const GetDependencyOptions& options, std::vector<DependencyURL>& URLs_out) const;

	void getDependencyURLSet(int ob_lod_level, const GetDependencyOptions& options, std::set<DependencyURL>& URLS_out) const;
	void getDependencyURLSetForAllLODLevels(std::set<DependencyURL>& URLS_out) const;
	void getDependencyURLSetBaseLevel(const GetDependencyOptions& options, std::set<DependencyURL>& URLS_out) const;

	void convertLocalPathsToURLS(ResourceManager& resource_manager);

	void getInterpolatedTransform(double cur_time, Vec3d& pos_out, Vec3f& axis_out, float& angle_out) const;
	void setTransformAndHistory(const Vec3d& pos, const Vec3f& axis, float angle);
	void setPosAndHistory(const Vec3d& pos);
	inline bool isCollidable() const;
	inline void setCollidable(bool c);

	size_t getTotalMemUsage() const;

	static int getLightMapSideResForAABBWS(const js::AABBox& aabb_ws);

	static void compressVoxelGroup(const VoxelGroup& group, js::Vector<uint8, 16>& compressed_data_out);
	static void decompressVoxelGroup(const uint8* compressed_data, size_t compressed_data_len, VoxelGroup& group_out);
	void compressVoxels();
	void decompressVoxels();
	void clearDecompressedVoxels();

	//VoxelGroup& getDecompressedVoxelGroup() { return voxel_group; }
	const VoxelGroup& getDecompressedVoxelGroup() const { return voxel_group; }
	js::Vector<Voxel, 16>& getDecompressedVoxels() { return voxel_group.voxels; }
	const js::Vector<Voxel, 16>& getDecompressedVoxels() const { return voxel_group.voxels; }
	js::Vector<uint8, 16>& getCompressedVoxels() { return compressed_voxels; }
	const js::Vector<uint8, 16>& getCompressedVoxels() const { return compressed_voxels; }
	//void getCompressedVoxels() const { return compressed_voxels; }


	void writeToStream(OutStream& stream) const;
	void writeToNetworkStream(OutStream& stream) const; // Write without version

	void copyNetworkStateFrom(const WorldObject& other);


	enum ObjectType
	{
		ObjectType_Generic = 0,
		ObjectType_Hypercard = 1,
		ObjectType_VoxelGroup = 2,
		ObjectType_Spotlight = 3,
		ObjectType_WebView = 4
	};

	static std::string objectTypeString(ObjectType t);

	// Group aabb_ws, current_lod_level together in first cache line to make MainWindow::checkForLODChanges() fast.

	js::AABBox aabb_ws; // World space axis-aligned bounding box.  Not authoritative.  Used to show a box while loading, also for computing LOD levels.
	UID uid;
	uint32 object_type;
	int current_lod_level; // LOD level as a function of distance from camera etc.. Kept up to date.

	//std::string name;
	std::string model_url;
	//std::string material_url;
	std::vector<WorldMaterialRef> materials;
	std::string lightmap_url;
	std::string script;
	std::string content; // For ObjectType_Hypercard
	std::string target_url; // For ObjectType_Hypercard
	Vec3d pos;
	Vec3f axis;
	float angle;
	Vec3f scale;

	static const uint32 COLLIDABLE_FLAG                         = 1; // Is this object solid from the point of view of the physics engine?
	static const uint32 LIGHTMAP_NEEDS_COMPUTING_FLAG           = 2; // Does the lightmap for this object need to be built or rebuilt?
	static const uint32 HIGH_QUAL_LIGHTMAP_NEEDS_COMPUTING_FLAG = 4; // Does a hiqh-quality lightmap for this object need to be built or rebuilt?
	uint32 flags;

	TimeStamp created_time;
	UserID creator_id;

	std::string creator_name; // This is 'denormalised' data that is not saved on disk, but set on load from disk or creation.  It is transferred across the network though.

	int max_model_lod_level; // maximum LOD level for model.  0 for models that don't have lower LOD versions.

#if GUI_CLIENT
	Reference<glare::AudioSource> audio_source;
#endif
	std::string audio_source_url;
	float audio_volume;

	enum State
	{
		State_JustCreated = 0,
		State_Alive,
		State_Dead
	};

	State state;
	bool from_remote_transform_dirty; // Transformation has been changed remotely
	bool from_remote_other_dirty;     // Something else has been changed remotely
	bool from_remote_lightmap_url_dirty; // Lightmap URL has been changed remotely
	bool from_remote_model_url_dirty; // Model URL has been changed remotely
	bool from_remote_flags_dirty;     // Flags have been changed remotely

	bool from_local_transform_dirty;  // Transformation has been changed locally
	bool from_local_other_dirty;      // Something else has been changed locally

	static const uint32 AUDIO_SOURCE_URL_CHANGED	= 1; // Set when audio_source_url is changed
	static const uint32 SCRIPT_CHANGED				= 2; // Set when script is changed
	static const uint32 MODEL_URL_CHANGED			= 4;
	uint32 changed_flags;

	bool using_placeholder_model;

	std::string loaded_content;

	//std::string loaded_audio_source_url;

	//std::string loaded_script;
	int instance_index;
	int num_instances; // number of instances of the prototype object that this is object is an instance of.
	Vec4f translation; // As computed by a script.  Translation from current position in pos.
	WorldObject* prototype_object; // for instances - this is the object this object is a copy of.
	std::vector<Reference<WorldObject>> instances;

	DatabaseKey database_key;

#if GUI_CLIENT
	Reference<GLObject> opengl_engine_ob;
	Reference<GLLight> opengl_light;
	Reference<PhysicsObject> physics_object;

	Reference<MeshData> mesh_manager_data; // Hang on to a reference to the mesh data, so when object-uses of it are removed, it can be removed from the MeshManager with meshDataBecameUnused().

	enum AudioState
	{
		AudioState_NotLoaded,
		AudioState_Loading, // The audio resource is downloading, or a LoadAudioTask has been constructed and is queued and/or executing.
		AudioState_Loaded,
		AudioState_ErrorWhileLoading
	};
	AudioState audio_state;

	Reference<ImageMap<uint8, UInt8ComponentValueTraits> > hypercard_map;

	//Reference<Indigo::SceneNodeModel> indigo_model_node;

	bool is_selected;

	bool in_proximity; // Used by proximity loader

	//Vec3d last_pos; // Used by proximity loader

	bool lightmap_baking; // Is lightmap baking in progress for this object?

	Reference<WinterShaderEvaluator> script_evaluator;

	js::Vector<Matrix4f, 16> instance_matrices;

	int loaded_model_lod_level; // If we have loaded a model for this object, this is the LOD level of the model.
	// This may differ from current_lod_level, for example if the new LOD level model needs to be downloaded from the server, then loaded_lod_level will be the previous level.
	int loaded_lod_level; // Level for textures etc..  Actually this is more like what lod level we have requested textures at.  TODO: clarify and improve.


	Reference<WebViewData> web_view_data;
	Reference<AnimatedTexObData> animated_tex_data;


	Reference<ObScatteringInfo> scattering_info;


	// For objects that are path controlled:
	int waypoint_index;
	float dist_along_segment;
#endif // GUI_CLIENT

	float max_load_dist2;
	
	/*
		Snapshots for client-side interpolation purposes.
		next_i = index to write next snapshot in.
		pos_snapshots[next_i - 1] is the last received update, received at time last_snapshot_time.
		pos_snapshots[next_i - 2] is the update received before that, will be considerd to be received at last_snapshot_time - update_send_period.
	*/
	static const int HISTORY_BUF_SIZE = 4;
	Vec3d pos_snapshots[HISTORY_BUF_SIZE];
	Vec3f axis_snapshots[HISTORY_BUF_SIZE];
	float angle_snapshots[HISTORY_BUF_SIZE];
	double snapshot_times[HISTORY_BUF_SIZE];
	//double last_snapshot_time;
	uint32 next_snapshot_i;
private:
	VoxelGroup voxel_group;
	js::Vector<uint8, 16> compressed_voxels;

public:
	glare::PoolAllocator* allocator; // Non-null if this object was allocated from the allocator
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


void readFromStream(InStream& stream, WorldObject& ob);
void readFromNetworkStreamGivenUID(InStream& stream, WorldObject& ob); // UID will have been read already


const Matrix4f obToWorldMatrix(const WorldObject& ob);
const Matrix4f worldToObMatrix(const WorldObject& ob);

// Throws glare::Exception if transform not OK, for example if any components are infinite or NaN. 
void checkTransformOK(const WorldObject* ob);


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

	const Vec4f use_pos = this->aabb_ws.centroid(); // this->pos.toVec4fVector()
	const float recip_dist = (campos - use_pos).fastApproxRecipLength();
	float proj_len = aabb_ws.longestLength() * recip_dist;

	// For voxel objects, push out the transition distances a bit.
	if(object_type == ObjectType_VoxelGroup)
		proj_len *= 2;

	if(proj_len > 0.6)
		return -1;
	else if(proj_len > 0.16)
		return 0;
	else if(proj_len > 0.03)
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
	float aabb_len = aabb_ws.longestLength();
	if(object_type == ObjectType_VoxelGroup)
		aabb_len *= 2;

	const float eps_factor = 1.001f; // Make distance slightly larger to account for fastApproxRecipLength() usage in getLODLevel().
	if(level == -1)
		return aabb_len * (eps_factor / 0.6f);
	else if(level == 0)
		return aabb_len * (eps_factor  / 0.16f);
	else if(level == 1)
		return aabb_len * (eps_factor  / 0.03f);
	else
		return std::numeric_limits<float>::max();
}


int WorldObject::getLODLevel(float cam_to_ob_d2) const
{
	const float recip_dist = _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(cam_to_ob_d2)));
	//assert(epsEqual(recip_dist, 1 / sqrt(cam_to_ob_d2)));

	float proj_len = aabb_ws.longestLength() * recip_dist;

	// For voxel objects, push out the transition distances a bit.
	if(object_type == ObjectType_VoxelGroup)
		proj_len *= 2;

	if(proj_len > 0.6)
		return -1;
	else if(proj_len > 0.16)
		return 0;
	else if(proj_len > 0.03)
		return 1;
	else
		return 2;
}