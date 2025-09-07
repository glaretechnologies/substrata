/*=====================================================================
GarbageDeleterThread.h
----------------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#pragma once


#include "WinterShaderEvaluator.h"
#include <utils/MessageableThread.h>
#include <utils/AllocatorVector.h>


struct GarbageToDelete
{
	glare::AllocatorVector<uint8, 16> uint8_data;
	glare::AllocatorVector<uint8, 16> uint8_data2;
	glare::AllocatorVector<uint32, 16> uint32_data;
	glare::AllocatorVector<uint16, 16> uint16_data;

	Reference<WinterShaderEvaluator> winter_shader_evaluator;
};


class DeleteGarbageMessage : public ThreadMessage
{
public:
	DeleteGarbageMessage() {}
	
	GarbageToDelete garbage;
};


/*=====================================================================
GarbageDeleterThread
--------------------
Certain memory-freeing operations take quite a while, due to, I think, the C runtime or OS zeroing out the freed mem.

Seems to zero at about 60 GB/s:

OpenGLMeshRenderData::clearAndFreeGeometryMem(): took 0.4398 ms for 23,360,484 B (53.12 GB/s)
OpenGLMeshRenderData::clearAndFreeGeometryMem(): took 0.3445 ms for 20,062,944 B (58.24 GB/s)

So the solution is to pass the data to the GarbageDeleterThread, which does the actual free() call.   In this way we avoid the free call blocking the main thread.

The other thing this is used for is deleting Winter scripts, which use LLVM, and whose deletion triggers a large number of object destructors, which takes quite a while to execute (2 or 3 ms).

There's a *lot* of LLVM objects used and I don't think it can all be given a custom allocator.  
Due to a design flaw with LLVM (or the version I'm using at least), all the intermediate data structures needed to produce the JIT'd code are kept around - the AST nodes etc...

Ideally all that stuff could be deleted immediately in the worker thread where the script was built, and just the small piece of JIT'd code could be returned.
=====================================================================*/
class GarbageDeleterThread : public MessageableThread
{
public:
	GarbageDeleterThread();
	virtual ~GarbageDeleterThread();

	virtual void doRun() override;
};
