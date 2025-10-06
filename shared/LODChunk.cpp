/*=====================================================================
LODChunk.cpp
------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "LODChunk.h"


#include "WorldObject.h"
#include <utils/Exception.h>
#include <utils/StringUtils.h>
#include <utils/ConPrint.h>
#include <utils/BufferOutStream.h>
#include <utils/BufferInStream.h>
#include <utils/RuntimeCheck.h>
#if GUI_CLIENT
#include "opengl/OpenGLEngine.h"
#include "../gui_client/MeshManager.h"
#endif
#include <zstd.h>


LODChunk::LODChunk()
{
	needs_rebuild = true;
	db_dirty = false;

#if GUI_CLIENT
	graphics_ob_in_engine = false;
#endif
}


LODChunk::~LODChunk()
{
}


static const uint32 LOD_CHUNK_SERIALISATION_VERSION = 1;


void LODChunk::writeToStream(RandomAccessOutStream& stream) const
{
	// Write to stream with a length prefix.  Do this by writing to the stream, them going back and writing the length of the data we wrote.
	// Writing a length prefix allows for adding more fields later, while retaining backwards compatibility with older code that can just skip over the new fields.

	const size_t initial_write_index = stream.getWriteIndex();

	stream.writeUInt32(LOD_CHUNK_SERIALISATION_VERSION);
	stream.writeUInt32(0); // Size of buffer will be written here later

	::writeToStream<int>(coords, stream);

	stream.writeStringLengthFirst(mesh_url);
	stream.writeStringLengthFirst(combined_array_texture_url);

	stream.writeUInt32((uint32)compressed_mat_info.size());
	stream.writeData(compressed_mat_info.data(), compressed_mat_info.size());

	// Write needs_rebuild
	stream.writeUInt32(needs_rebuild ? 1 : 0);

	// Go back and write size of buffer to buffer size field
	const uint32 buffer_size = (uint32)(stream.getWriteIndex() - initial_write_index);

	std::memcpy(stream.getWritePtrAtIndex(initial_write_index + sizeof(uint32)), &buffer_size, sizeof(uint32));
}


void LODChunk::copyNetworkStateFrom(const LODChunk& other)
{
	coords = other.coords;
	mesh_url = other.mesh_url;
	combined_array_texture_url = other.combined_array_texture_url;
	compressed_mat_info = other.compressed_mat_info;
	needs_rebuild = other.needs_rebuild;
}


const URLString LODChunk::computeMeshURL(bool use_optimised_meshes, int opt_mesh_version) const
{
	if(use_optimised_meshes)
		return toURLString(std::string(removeDotAndExtensionStringView(mesh_url)) + "_opt" + toString(opt_mesh_version) + ".bmesh");
	else
		return mesh_url;
}


void readLODChunkFromStream(RandomAccessInStream& stream, LODChunk& chunk)
{
	const size_t initial_read_index = stream.getReadIndex();

	/*const uint32 version =*/ stream.readUInt32();
	const uint32 buffer_size = stream.readUInt32();

	checkProperty(buffer_size >= 8ul, "readLODChunkFromStream: buffer_size was too small");
	checkProperty(buffer_size <= 1000000ul, "readLODChunkFromStream: buffer_size was too large");

	chunk.coords = readVec3FromStream<int>(stream);

	chunk.mesh_url = stream.readStringLengthFirst(WorldObject::MAX_URL_SIZE);
	chunk.combined_array_texture_url = stream.readStringLengthFirst(WorldObject::MAX_URL_SIZE);

	const uint32 compressed_mat_info_size = stream.readUInt32();
	chunk.compressed_mat_info.resizeNoCopy(compressed_mat_info_size);
	stream.readData(chunk.compressed_mat_info.data(), chunk.compressed_mat_info.size());

	// Read needs_rebuild
	chunk.needs_rebuild = stream.readUInt32() != 0;


	// Discard any remaining unread data
	const size_t read_B = stream.getReadIndex() - initial_read_index; // Number of bytes we have read so far
	if(read_B < (size_t)buffer_size)
		stream.advanceReadIndex((size_t)buffer_size - read_B);
}
