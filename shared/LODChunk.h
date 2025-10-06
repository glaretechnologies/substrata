/*=====================================================================
LODChunk.h
----------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "DependencyURL.h"
#include "TimeStamp.h"
#if GUI_CLIENT
#include <opengl/OpenGLTexture.h>
#endif
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
struct MeshData;


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

	const URLString computeMeshURL(bool use_optimised_meshes, int opt_mesh_version) const;

	const URLString& getMeshURL() const { return mesh_url; }

	Vec3i coords;
	URLString mesh_url;
	URLString combined_array_texture_url;
	js::Vector<uint8> compressed_mat_info;
	bool needs_rebuild; // Does the chunk mesh or texture need rebuilding due to an object change in the chunk?


#if GUI_CLIENT
	Reference<GLObject> graphics_ob;
	bool graphics_ob_in_engine;
	OpenGLTextureKey combined_array_texture_path;

	Reference<GLObject> diagnostics_gl_ob; // For diagnostics visualisation

	Reference<MeshData> mesh_manager_data; // Hang on to a reference to the mesh data, so when chunk-uses of it are removed, it can be removed from the MeshManager with meshDataBecameUnused().
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

