/*=====================================================================
WorldObject.h
-------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "TimeStamp.h"
#include "WorldMaterial.h"
#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include <Vector.h>
#include "../shared/UID.h"
#include "../shared/UserID.h"
#include "vec3.h"
#if GUI_CLIENT
#include <graphics/ImageMap.h>
#endif
#include <string>
#include <vector>
#include <set>
struct GLObject;
class PhysicsObject;
class ResourceManager;
class WinterShaderEvaluator;
class Matrix4f;
namespace Indigo { class SceneNodeModel; }


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
	std::vector<Voxel> voxels;
};


/*=====================================================================
WorldObject
-----------

=====================================================================*/
class WorldObject : public ThreadSafeRefCounted
{
public:
	WorldObject();
	~WorldObject();

	GLARE_ALIGNED_16_NEW_DELETE

	void appendDependencyURLs(std::vector<std::string>& URLs_out);
	void getDependencyURLSet(std::set<std::string>& URLS_out);
	void convertLocalPathsToURLS(ResourceManager& resource_manager);

	void getInterpolatedTransform(double cur_time, Vec3d& pos_out, Vec3f& axis_out, float& angle_out) const;
	void setTransformAndHistory(const Vec3d& pos, const Vec3f& axis, float angle);
	void setPosAndHistory(const Vec3d& pos);
	inline bool isCollidable() const;
	inline void setCollidable(bool c);

	static void compressVoxelGroup(const VoxelGroup& group, js::Vector<uint8, 16>& compressed_data_out);
	static void decompressVoxelGroup(const uint8* compressed_data, size_t compressed_data_len, VoxelGroup& group_out);
	void compressVoxels();
	void decompressVoxels();

	enum ObjectType
	{
		ObjectType_Generic,
		ObjectType_Hypercard,
		ObjectType_VoxelGroup
	};

	static std::string objectTypeString(ObjectType t);

	UID uid;
	uint32 object_type;
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

	static const uint32 COLLIDABLE_FLAG               = 1; // Is this object solid from the point of view of the physics engine?
	static const uint32 LIGHTMAP_NEEDS_COMPUTING_FLAG = 2; // Does the lightmap for this object need to be built or rebuilt?
	uint32 flags;

	VoxelGroup voxel_group;
	js::Vector<uint8, 16> compressed_voxels;

	TimeStamp created_time;
	UserID creator_id;

	std::string creator_name; // This is 'denormalised' data that is not saved on disk, but set on load from disk or creation.  It is transferred across the network though.

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
	bool from_remote_flags_dirty;     // Flags have been changed remotely

	bool from_local_transform_dirty;  // Transformation has been changed locally
	bool from_local_other_dirty;      // Something else has been changed locally

	bool using_placeholder_model;
	std::string loaded_model_url;

	std::string loaded_content;

	std::string loaded_script;
	int instance_index;
	int num_instances; // number of instances of the prototype object that this is object is an instance of.
	Vec4f translation; // As computed by a script.  Translation from current position in pos.
	Reference<WorldObject> prototype_object; // for instances - this is the object this object is a copy of.

	//Reference<WorldMaterial> material;

#if GUI_CLIENT
	Reference<GLObject> opengl_engine_ob;
	Reference<PhysicsObject> physics_object;

	ImageMapUInt8Ref hypercard_map;

	Reference<Indigo::SceneNodeModel> indigo_model_node;

	bool is_selected;
#endif
	Reference<WinterShaderEvaluator> script_evaluator;

	
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


void writeToStream(const WorldObject& world_ob, OutStream& stream);
void readFromStream(InStream& stream, WorldObject& ob);

void writeToNetworkStream(const WorldObject& world_ob, OutStream& stream); // Write without version
void readFromNetworkStreamGivenUID(InStream& stream, WorldObject& ob); // UID will have been read already


const Matrix4f obToWorldMatrix(const WorldObject& ob);


struct WorldObjectRefHash
{
	size_t operator() (const WorldObjectRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};
