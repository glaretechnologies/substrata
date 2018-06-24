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
#include <physics/jscol_aabbox.h>
#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include <string>
#include <OutStream.h>
#include <InStream.h>
struct GLObject;
class PhysicsObject;


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

	inline bool pointInParcel(const Vec3d& p) const;
	inline bool AABBInParcel(const js::AABBox& aabb) const;
	static inline bool AABBInParcelBounds(const js::AABBox& aabb, const Vec3d& parcel_aabb_min, const Vec3d& parcel_aabb_max);

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
	Vec3d aabb_min;
	Vec3d aabb_max;


	// This is 'denormalised' data that is not saved on disk, but set on load from disk or creation.  It is transferred across the network though.
	std::string owner_name;
	std::vector<std::string> admin_names;
	std::vector<std::string> writer_names;


#if GUI_CLIENT
	Reference<GLObject> opengl_engine_ob;
	Reference<PhysicsObject> physics_object;
#endif

};


typedef Reference<Parcel> ParcelRef;


bool Parcel::pointInParcel(const Vec3d& p) const
{
	return 
		p.x >= aabb_min.x && p.y >= aabb_min.y && p.z >= aabb_min.z &&
		p.x <= aabb_max.x && p.y <= aabb_max.y && p.z <= aabb_max.z;
}


bool Parcel::AABBInParcel(const js::AABBox& aabb) const
{
	return AABBInParcelBounds(aabb, this->aabb_min, this->aabb_max);
}


bool Parcel::AABBInParcelBounds(const js::AABBox& aabb, const Vec3d& parcel_aabb_min, const Vec3d& parcel_aabb_max)
{
	return 
		aabb.min_[0] >= parcel_aabb_min.x && aabb.min_[1] >= parcel_aabb_min.y && aabb.min_[2] >= parcel_aabb_min.z &&
		aabb.max_[0] <= parcel_aabb_max.x && aabb.max_[1] <= parcel_aabb_max.y && aabb.max_[2] <= parcel_aabb_max.z;
}


void writeToStream(const Parcel& parcel, OutStream& stream);
void readFromStream(InStream& stream, Parcel& parcel);

void writeToNetworkStream(const Parcel& parcel, OutStream& stream); // write without version
void readFromNetworkStreamGivenID(InStream& stream, Parcel& parcel);
