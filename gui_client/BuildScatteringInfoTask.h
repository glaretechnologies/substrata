/*=====================================================================
BuildScatteringInfoTask.h
-------------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "../shared/WorldObject.h"
#include <Task.h>
#include <TaskManager.h>
#include <ThreadMessage.h>
#include <ThreadSafeQueue.h>
#include <string>
class OpenGLEngine;
class MeshManager;
class ResourceManager;


class BuildScatteringInfoDoneThreadMessage : public ThreadMessage
{
public:
	// Results of the task:
	UID ob_uid;
	Reference<ObScatteringInfo> ob_scattering_info;
};


/*=====================================================================
BuildScatteringInfoTask
-----------------------
Compute ObScatteringInfo for an object - used for generating points on the surface of an object.
=====================================================================*/
class BuildScatteringInfoTask : public glare::Task
{
public:
	BuildScatteringInfoTask();
	virtual ~BuildScatteringInfoTask();

	virtual void run(size_t thread_index);


	Matrix4f ob_to_world;

	UID ob_uid;
	std::string lod_model_url; // The URL of a model with a specific LOD level to load.  Empty when loading voxel object.
	
	WorldObjectRef voxel_ob; // If non-null, the task is to load/mesh the voxels for this object.

	Reference<ResourceManager> resource_manager;
	ThreadSafeQueue<Reference<ThreadMessage> >* result_msg_queue;
};
