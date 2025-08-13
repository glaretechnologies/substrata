/*=====================================================================
Photo.h
-------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#pragma once


#include "../shared/UserID.h"
#include "../shared/ParcelID.h"
#include "../maths/vec3.h"
#include <utils/TimeStamp.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/Reference.h>
#include <utils/DatabaseKey.h>
class RandomAccessInStream;
class RandomAccessOutStream;


/*=====================================================================
Photo
-----
A photo / screenshot that a user captured in the substrata client photo mode, 
and then uploaded to the server.
=====================================================================*/
class Photo : public ThreadSafeRefCounted
{
public:
	Photo();
	~Photo();

	void writeToStream(RandomAccessOutStream& stream) const;

	static const size_t MAX_CAPTION_SIZE = 10000;
	static const size_t MAX_WORLD_NAME_SIZE = 1000;

	uint64 id;
	UserID creator_id;
	ParcelID parcel_id;
	TimeStamp created_time;
	Vec3d cam_pos;
	Vec3d cam_angles;
	std::string caption;
	uint32 flags;
	std::string world_name;

	std::string local_filename; // file name on disk of full resolution saved photo
	std::string local_thumbnail_filename; // file name on disk of thumbnail (200 px wide)
	std::string local_midsize_filename; // file name on disk of mid-size image (max 1000px wide)

	enum State
	{
		State_published = 0,
		State_deleted = 1
	};
	State state;

	DatabaseKey database_key;
};


typedef Reference<Photo> PhotoRef;


void readPhotoFromStream(RandomAccessInStream& stream, Photo& s);


struct PhotoRefHash
{
	size_t operator() (const PhotoRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};
