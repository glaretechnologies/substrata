/*=====================================================================
LODChunk.h
----------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "DependencyURL.h"
#include "TimeStamp.h"
#include <maths/vec3.h>
#include <maths/vec2.h>
#include <utils/RandomAccessInStream.h>
#include <utils/RandomAccessOutStream.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/DatabaseKey.h>
#include <utils/Reference.h>
#include <utils/Vector.h>
#include <string>
#include <vector>
#include <set>
struct GLObject;


/*=====================================================================
LODChunk
--------

=====================================================================*/
class LODChunk : public ThreadSafeRefCounted
{
public:
	LODChunk();
	~LODChunk();

	GLARE_ALIGNED_16_NEW_DELETE

	void writeToStream(RandomAccessOutStream& stream) const;

	void copyNetworkStateFrom(const LODChunk& other);


	Vec3i coords;

	std::string mesh_url;
	std::string combined_array_texture_url;
	js::Vector<uint8> compressed_mat_info;
	bool needs_rebuild; // Does the chunk mesh or texture need rebuilding due to an object change in the chunk?


#if GUI_CLIENT
	Reference<GLObject> graphics_ob;
	bool graphics_ob_in_engine;
	std::string combined_array_texture_path;
#endif


	DatabaseKey database_key;
	bool db_dirty; // If true, there is a change that has not been saved to the DB.

private:
	GLARE_DISABLE_COPY(LODChunk)
};

typedef Reference<LODChunk> LODChunkRef;


void readLODChunkFromStream(RandomAccessInStream& stream, LODChunk& chunk);


struct LODChunkRefHash
{
	size_t operator() (const LODChunkRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};

