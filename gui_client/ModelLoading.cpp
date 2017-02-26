/*=====================================================================
ModelLoading.cpp
------------------------
File created by ClassTemplate on Wed Oct 07 15:16:48 2009
Code By Nicholas Chapman.
=====================================================================*/
#include "ModelLoading.h"


#include "../shared/ResourceManager.h"
#include "../dll/include/IndigoMesh.h"
#include "../graphics/formatdecoderobj.h"
#include "../dll/IndigoStringUtils.h"
#include "../utils/FileUtils.h"
#include "../utils/Exception.h"
#include "../utils/PlatformUtils.h"
#include "../utils/StringUtils.h"
#include "../utils/ConPrint.h"


void ModelLoading::setGLMaterialFromWorldMaterial(const WorldMaterial& mat, ResourceManager& resource_manager, OpenGLMaterial& opengl_mat)
{
	if(mat.colour->type == SpectrumVal::SpectrumValType_Constant)
	{
		opengl_mat.albedo_rgb = mat.colour.downcastToPtr<ConstantSpectrumVal>()->rgb;
		opengl_mat.albedo_tex_path = "";
	}
	else if(mat.colour->type == SpectrumVal::SpectrumValType_Texture)
	{
		opengl_mat.albedo_rgb = Colour3f(0.5f);
		opengl_mat.albedo_tex_path = resource_manager.pathForURL(mat.colour.downcastToPtr<TextureSpectrumVal>()->texture_url);
	}

	if(mat.roughness->type == ScalarVal::ScalarValType_Constant)
	{
		opengl_mat.roughness = mat.roughness.downcastToPtr<ConstantScalarVal>()->val;
	}
	else
	{
		opengl_mat.roughness = 0.5f;
	}

	if(mat.opacity->type == ScalarVal::ScalarValType_Constant)
	{
		opengl_mat.transparent = mat.opacity.downcastToPtr<ConstantScalarVal>()->val < 1.0f;
	}
	else
	{
		opengl_mat.transparent = false;
	}
}


// We don't have a material file, just the model file:
GLObjectRef ModelLoading::makeGLObjectForModelFile(const std::string& model_path, 
												   const Matrix4f& ob_to_world_matrix, Indigo::MeshRef& mesh_out)
{
	if(hasExtension(model_path, "obj"))
	{
		MLTLibMaterials mats;
		Indigo::MeshRef mesh = new Indigo::Mesh();
		FormatDecoderObj::streamModel(model_path, *mesh, 1.f, /*parse mtllib=*/true, mats);

		GLObjectRef ob = new GLObject();
		ob->ob_to_world_matrix = ob_to_world_matrix;
		ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh, false);

		ob->materials.resize(mesh->num_materials_referenced);
		for(uint32 i=0; i<ob->materials.size(); ++i)
		{
			// Have we parsed such a material from the .mtl file?
			bool found_mat = false;
			for(size_t z=0; z<mats.materials.size(); ++z)
				if(mats.materials[z].name == toStdString(mesh->used_materials[z]))
				{
					ob->materials[i].albedo_rgb = mats.materials[z].Kd;
					ob->materials[i].albedo_tex_path = mats.materials[z].map_Kd.path;
					ob->materials[i].roughness = 0.5f;//mats.materials[z].Ns_exponent; // TODO: convert
					ob->materials[i].alpha = myClamp(mats.materials[z].d_opacity, 0.f, 1.f);
					found_mat = true;
				}

			if(!found_mat)
			{
				// Assign dummy mat
				ob->materials[i].albedo_rgb = Colour3f(0.7f, 0.7f, 0.7f);
				ob->materials[i].albedo_tex_path = "obstacle.png";
				ob->materials[i].roughness = 0.5f;
			}

			ob->materials[i].tex_matrix = Matrix2f(1, 0, 0, -1);
		}
		mesh_out = mesh;
		return ob;
	}
	else if(hasExtension(model_path, "igmesh"))
	{
		try
		{
			Indigo::MeshRef mesh = new Indigo::Mesh();
			Indigo::Mesh::readFromFile(toIndigoString(model_path), *mesh);
			
			GLObjectRef ob = new GLObject();
			ob->ob_to_world_matrix = ob_to_world_matrix;
			ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh, false);

			ob->materials.resize(mesh->num_materials_referenced);
			for(uint32 i=0; i<ob->materials.size(); ++i)
			{
				// Assign dummy mat
				ob->materials[i].albedo_rgb = Colour3f(0.7f, 0.7f, 0.7f);
				ob->materials[i].albedo_tex_path = "obstacle.png";
				ob->materials[i].roughness = 0.5f;
				ob->materials[i].tex_matrix = Matrix2f(1, 0, 0, -1);
			}
			
			mesh_out = mesh;
			return ob;
		}
		catch(Indigo::IndigoException& e)
		{
			throw Indigo::Exception(toStdString(e.what()));
		}
	}
	else
		throw Indigo::Exception("unhandled format: " + model_path);
}


GLObjectRef ModelLoading::makeGLObjectForModelFile(const std::string& model_path, const std::vector<WorldMaterialRef>& materials, 
												   //const std::map<std::string, std::string>& paths_for_URLs,
												   ResourceManager& resource_manager,
												   const Matrix4f& ob_to_world_matrix, Indigo::MeshRef& mesh_out)
{
	if(hasExtension(model_path, "obj"))
	{
		MLTLibMaterials mats;
		Indigo::MeshRef mesh = new Indigo::Mesh();
		FormatDecoderObj::streamModel(model_path, *mesh, 1.f, false, mats);

		GLObjectRef ob = new GLObject();
		ob->ob_to_world_matrix = ob_to_world_matrix;
		ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh, false);

		ob->materials.resize(mesh->num_materials_referenced);
		for(uint32 i=0; i<ob->materials.size(); ++i)
		{
			if(i < materials.size())
			{
				setGLMaterialFromWorldMaterial(*materials[i], /*paths_for_URLs*/resource_manager, ob->materials[i]);
			}
			else
			{
				// Assign dummy mat
				ob->materials[i].albedo_rgb = Colour3f(0.7f, 0.7f, 0.7f);
				ob->materials[i].albedo_tex_path = "obstacle.png";
				ob->materials[i].roughness = 0.5f;
			}

			ob->materials[i].tex_matrix = Matrix2f(1, 0, 0, -1);
		}
		mesh_out = mesh;
		return ob;

		/*MLTLibMaterials mats;
		Indigo::MeshRef mesh = new Indigo::Mesh();
		FormatDecoderObj::streamModel(model_path, *mesh, 1.f, true, mats);

		GLObjectRef ob = new GLObject();
		ob->ob_to_world_matrix = ob_to_world_matrix;
		ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh, false);

		ob->materials.resize(mesh->num_materials_referenced);
		for(uint32 i=0; i<ob->materials.size(); ++i)
		{
			// Have we parsed such a material from the .mtl file?
			bool found_mat = false;
			for(size_t z=0; z<mats.materials.size(); ++z)
				if(mats.materials[z].name == toStdString(mesh->used_materials[z]))
				{
					ob->materials[i].albedo_rgb = mats.materials[z].Kd;
					ob->materials[i].albedo_tex_path = mats.materials[z].map_Kd.path;
					ob->materials[i].phong_exponent = mats.materials[z].Ns_exponent;
					ob->materials[i].alpha = myClamp(mats.materials[z].d_opacity, 0.f, 1.f);
					found_mat = true;
				}

			if(!found_mat)
			{
				// Assign dummy mat
				ob->materials[i].albedo_rgb = Colour3f(0.7f, 0.7f, 0.7f);
				ob->materials[i].albedo_tex_path = "obstacle.png";
				ob->materials[i].phong_exponent = 10.f;
			}
		}
		mesh_out = mesh;
		return ob;*/
	}
	else if(hasExtension(model_path, "igmesh"))
	{
		try
		{
			Indigo::MeshRef mesh = new Indigo::Mesh();
			Indigo::Mesh::readFromFile(toIndigoString(model_path), *mesh);
			
			GLObjectRef ob = new GLObject();
			ob->ob_to_world_matrix = ob_to_world_matrix;
			ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh, false);

			ob->materials.resize(mesh->num_materials_referenced);
			for(uint32 i=0; i<ob->materials.size(); ++i)
			{
				if(i < materials.size())
				{
					setGLMaterialFromWorldMaterial(*materials[i], /*paths_for_URLs*/resource_manager, ob->materials[i]);
				}
				else
				{
					// Assign dummy mat
					ob->materials[i].albedo_rgb = Colour3f(0.7f, 0.7f, 0.7f);
					ob->materials[i].albedo_tex_path = "obstacle.png";
					ob->materials[i].roughness = 0.5f;
				}

				ob->materials[i].tex_matrix = Matrix2f(1, 0, 0, -1);
			}
			mesh_out = mesh;
			return ob;
		}
		catch(Indigo::IndigoException& e)
		{
			throw Indigo::Exception(toStdString(e.what()));
		}
	}
	else
		throw Indigo::Exception("unhandled model format: " + model_path);
}


