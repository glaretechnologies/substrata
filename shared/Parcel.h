/*=====================================================================
Parcel.h
--------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#pragma once


#include "TimeStamp.h"
#include "ParcelID.h"
#include "UserID.h"
#include <vec3.h>
#include <vec2.h>
#include <physics/jscol_aabbox.h>
#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include <string>
#include <OutStream.h>
#include <InStream.h>
struct GLObject;
class PhysicsObject;
class OpenGLEngine;
class RayMesh;
namespace Indigo { class TaskManager; }


/*=====================================================================
Parcel
------
Land parcel
=====================================================================*/
class Parcel : public ThreadSafeRefCounted
{
public:
	Parcel();
	~Parcel();

	void build(); // Build cached data like aabb_min

	bool pointInParcel(const Vec3d& p) const;
	bool AABBInParcel(const js::AABBox& aabb) const;
	static bool AABBInParcelBounds(const js::AABBox& aabb, const Vec3d& parcel_aabb_min, const Vec3d& parcel_aabb_max);
	bool isAxisAlignedBox() const;

	// For client:
	enum State
	{
		State_JustCreated,
		State_Alive,
		State_Dead
	};

	State state;
	bool from_remote_dirty; // parcel has been changed remotely
	bool from_local_dirty;  // parcel has been changed locally


	ParcelID id;
	UserID owner_id;
	TimeStamp created_time;
	std::string description;
	std::vector<UserID> admin_ids;
	std::vector<UserID> writer_ids;
	std::vector<ParcelID> child_parcel_ids;
	

	Vec2d verts[4];
	Vec2d zbounds;

	// A bound around the verts and zbounds above.  Derived data, not transferred across network
	Vec3d aabb_min;
	Vec3d aabb_max;


	// This is 'denormalised' data that is not saved on disk, but set on load from disk or creation.  It is transferred across the network though.
	std::string owner_name;
	std::vector<std::string> admin_names;
	std::vector<std::string> writer_names;


#if GUI_CLIENT
	Reference<GLObject> opengl_engine_ob;
	Reference<PhysicsObject> physics_object;

	Reference<GLObject> makeOpenGLObject(Reference<OpenGLEngine>& opengl_engine); // Shader program will be set by calling code later.

	Reference<PhysicsObject> makePhysicsObject(Reference<RayMesh>& unit_cube_raymesh, Indigo::TaskManager& task_manager);
#endif

};


typedef Reference<Parcel> ParcelRef;



void writeToStream(const Parcel& parcel, OutStream& stream);
void readFromStream(InStream& stream, Parcel& parcel);

void writeToNetworkStream(const Parcel& parcel, OutStream& stream); // write without version
void readFromNetworkStreamGivenID(InStream& stream, Parcel& parcel);
