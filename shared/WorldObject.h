/*=====================================================================
WorldObject.h
-------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "WorldMaterial.h"
#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include "../shared/UID.h"
#include "vec3.h"
#include <string>
#include <vector>
#include <set>
struct GLObject;
class PhysicsObject;
class ResourceManager;
class WinterShaderEvaluator;


const uint32 CyberspaceHello = 1357924680;
const uint32 CyberspaceProtocolVersion = 4;
const uint32 ClientProtocolOK		= 10000;
const uint32 ClientProtocolTooOld	= 10001;
const uint32 ClientProtocolTooNew	= 10002;


const uint32 ConnectionTypeUpdates	= 500;
const uint32 ConnectionTypeUploadResource	= 501;
const uint32 ConnectionTypeDownloadResources	= 502;



//TEMP HACK move elsewhere
const uint32 ObjectCreated			= 3000;
const uint32 ObjectDestroyed		= 3001;
const uint32 ObjectTransformUpdate	= 3002;
const uint32 ObjectFullUpdate		= 3003;


//TEMP HACK move elsewhere
const uint32 GetFile				= 4000;



//TEMP HACK move elsewhere
const uint32 UploadResource			= 5000;


//TEMP HACK move elsewhere
const uint32 UserSelectedObject		= 6000;
const uint32 UserDeselectedObject	= 6001;


/*=====================================================================
WorldObject
-----------

=====================================================================*/
class WorldObject : public ThreadSafeRefCounted
{
public:
	WorldObject();
	~WorldObject();

	void appendDependencyURLs(std::vector<std::string>& URLs_out);
	void getDependencyURLSet(std::set<std::string>& URLS_out);
	void convertLocalPathsToURLS(ResourceManager& resource_manager);

	void getInterpolatedTransform(double cur_time, Vec3d& pos_out, Vec3f& axis_out, float& angle_out) const;
	void setTransformAndHistory(const Vec3d& pos, const Vec3f& axis, float angle);
	void setPosAndHistory(const Vec3d& pos);

	UID uid;
	//std::string name;
	std::string model_url;
	//std::string material_url;
	std::vector<WorldMaterialRef> materials;
	std::string script_url;
	Vec3d pos;
	Vec3f axis;
	float angle;
	Vec3f scale;

	enum State
	{
		State_JustCreated = 0,
		State_Alive,
		State_Dead
	};

	State state;
	bool from_remote_transform_dirty; // Transformation has been changed remotely
	bool from_remote_other_dirty;     // Something else has been changed remotely

	bool from_local_transform_dirty;  // Transformation has been changed locally
	bool from_local_other_dirty;      // Something else has been changed locally

	bool using_placeholder_model;
	std::string loaded_model_url;

	//Reference<WorldMaterial> material;

	Reference<GLObject> opengl_engine_ob;
	Reference<PhysicsObject> physics_object;
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


void writeToStream(const WorldObject& world_ob, OutStream& stream);
void readFromStream(InStream& stream, WorldObject& ob);

void writeToNetworkStream(const WorldObject& world_ob, OutStream& stream); // Write without version
void readFromNetworkStreamGivenUID(InStream& stream, WorldObject& ob); // UID will have been read already
