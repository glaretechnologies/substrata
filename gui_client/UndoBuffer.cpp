/*=====================================================================
UndoBuffer.cpp
--------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "UndoBuffer.h"


#include <ConPrint.h>
#include <StringUtils.h>
#include <Clock.h>
#include <Lock.h>
#include <BufferOutStream.h>
#include <BufferInStream.h>
#include <ContainerUtils.h>


UndoBuffer::UndoBuffer()
:	index(0)
{
	arena_allocator = new glare::ArenaAllocator(1024 * 1024);
}


UndoBuffer::~UndoBuffer()
{
}


void UndoBuffer::startWorldObjectEdit(const WorldObject& ob)
{
	//conPrint("UndoBuffer::startWorldObjectEdit()");

	BufferOutStream temp_buf;
	ob.writeToStream(temp_buf, *arena_allocator);
	arena_allocator->clear();
	current_edit.start = temp_buf.buf;
}


void UndoBuffer::finishWorldObjectEdit(const WorldObject& ob)
{
	//conPrint("UndoBuffer::finishWorldObjectEdit()");

	BufferOutStream temp_buf;
	ob.writeToStream(temp_buf, *arena_allocator);
	arena_allocator->clear();

	current_edit.end = temp_buf.buf;

	// Trim any chunks >= index.
	// This is effectively trimming of a dead branch of the undo tree.
	chunks.resize(index);

	//conPrint("Pushed edit " + toString(index));
	chunks.push_back(current_edit);
	index++;
}


void UndoBuffer::replaceFinishWorldObjectEdit(const WorldObject& ob)
{
	//conPrint("UndoBuffer::replaceFinishWorldObjectEdit()");

	const int index_to_replace = index - 1;

	if(index_to_replace < 0 || index_to_replace >= (int)chunks.size())
		return;

	BufferOutStream temp_buf;
	ob.writeToStream(temp_buf, *arena_allocator);
	arena_allocator->clear();

	//conPrint("replacing edit " + toString(index_to_replace) + " end");
	chunks[index_to_replace].end = temp_buf.buf;
}


WorldObjectRef UndoBuffer::getUndoWorldObject(glare::BumpAllocator& bump_allocator)
{
	//conPrint("UndoBuffer::getUndoWorldObject()");

	if(chunks.empty() || index == 0)
	{
		//conPrint("nothing to undo to");
		return NULL;
	}

	const int chunk_to_pop = index - 1;
	const size_t chunk_size = chunks[chunk_to_pop].start.size();

	BufferInStream stream;
	stream.buf.resize(chunk_size);
	for(size_t z=0; z<chunk_size; ++z)
		stream.buf[z] = chunks[chunk_to_pop].start[z];

	//conPrint("Undoing edit " + toString(chunk_to_pop));

	index--;

	WorldObjectRef ob = new WorldObject();
	readWorldObjectFromStream(stream, *ob, bump_allocator);
	return ob;
}


WorldObjectRef UndoBuffer::getRedoWorldObject(glare::BumpAllocator& bump_allocator)
{
	if(index >= (int)chunks.size())
		return NULL;

	const int chunk_to_pop = index;
	const size_t chunk_size = chunks[chunk_to_pop].end.size();

	BufferInStream stream;
	stream.buf.resize(chunk_size);
	for(size_t z=0; z<chunk_size; ++z)
		stream.buf[z] = chunks[chunk_to_pop].end[z];

	index++;

	WorldObjectRef ob = new WorldObject();
	readWorldObjectFromStream(stream, *ob, bump_allocator);
	return ob;
}
