/*=====================================================================
ModelLoading.h
----------------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "../shared/WorldMaterial.h"
#include "../opengl/OpenGLEngine.h"
#include "../dll/include/IndigoMesh.h"
struct GLObject;
class Matrix4f;
class ResourceManager;


/*=====================================================================
ModelLoading
-------------

=====================================================================*/
class ModelLoading
{
public:
	static void setGLMaterialFromWorldMaterial(const WorldMaterial& mat, ResourceManager& resource_manager, OpenGLMaterial& opengl_mat);


	// We don't have a material file, just the model file:
	// Throws Indigo::Exception on invalid mesh.
	static GLObjectRef makeGLObjectForModelFile(const std::string& path, 
		const Matrix4f& ob_to_world_matrix, Indigo::MeshRef& mesh_out, float& suggested_scale_out, std::vector<WorldMaterialRef>& loaded_materials_out); // throws Indigo::Exception on failure.


	// For when we have materials:
	// Throws Indigo::Exception on invalid mesh.
	static GLObjectRef makeGLObjectForModelFile(const std::string& model_URL, const std::vector<WorldMaterialRef>& materials,
		ResourceManager& resource_manager,
		const Matrix4f& ob_to_world_matrix, Indigo::MeshRef& mesh_out); // throws Indigo::Exception on failure.
};

