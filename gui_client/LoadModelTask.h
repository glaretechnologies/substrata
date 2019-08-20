/*=====================================================================
LoadModelTask.h
---------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#pragma once


#include "../shared/WorldObject.h"
#include "PhysicsObject.h"
#include <opengl/OpenGLEngine.h>
#include <Task.h>
#include <ThreadMessage.h>
#include <string>
class MainWindow;
class OpenGLEngine;
class MeshManager;
class ResourceManager;


class ModelLoadedThreadMessage : public ThreadMessage
{
public:
	// Results of the task:
	GLObjectRef opengl_ob;
	PhysicsObjectRef physics_ob;

	WorldObjectRef ob;
};


/*=====================================================================
LoadModelTask
-------------

=====================================================================*/
class LoadModelTask : public Indigo::Task
{
public:
	LoadModelTask();
	virtual ~LoadModelTask();

	virtual void run(size_t thread_index);

	WorldObjectRef ob;
	Reference<OpenGLEngine> opengl_engine;
	MainWindow* main_window;
	MeshManager* mesh_manager;
	Reference<ResourceManager> resource_manager;
	Indigo::TaskManager* model_building_task_manager;
};
