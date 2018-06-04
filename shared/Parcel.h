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


	std::string owner_name; // This is 'denormalised' data that is not saved on disk, but set on load from disk or creation.  It is transferred across the network though.


#if GUI_CLIENT
	Reference<GLObject> opengl_engine_ob;
	Reference<PhysicsObject> physics_object;
#endif

};


typedef Reference<Parcel> ParcelRef;


void writeToStream(const Parcel& parcel, OutStream& stream);
void readFromStream(InStream& stream, Parcel& parcel);

void writeToNetworkStream(const Parcel& parcel, OutStream& stream); // write without version
void readFromNetworkStreamGivenID(InStream& stream, Parcel& parcel);
