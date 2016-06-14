/*=====================================================================
ModelLoading.h
----------------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "../opengl/OpenGLEngine.h"
#include "../dll/include/IndigoMesh.h"
struct GLObject;
class Matrix4f;


/*=====================================================================
ModelLoading
-------------

=====================================================================*/
class ModelLoading
{
public:
	static GLObjectRef makeGLObjectForModelFile(const std::string& path, const Matrix4f& ob_to_world_matrix, Indigo::MeshRef& mesh_out); // throws Indigo::Exception on failure.
};

