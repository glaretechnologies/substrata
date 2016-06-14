/*=====================================================================
WorldObject.cpp
-------------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#include "WorldObject.h"


#include "opengl/OpenGLEngine.h"
#include "../gui_client/PhysicsObject.h"


WorldObject::WorldObject()
{
	dirty = false;
	opengl_engine_ob = NULL;
	physics_object = NULL;
}


WorldObject::~WorldObject()
{

}



