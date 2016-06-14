/*=====================================================================
ModelLoading.cpp
------------------------
File created by ClassTemplate on Wed Oct 07 15:16:48 2009
Code By Nicholas Chapman.
=====================================================================*/
#include "ModelLoading.h"


#include "../dll/include/IndigoMesh.h"
#include "../graphics/formatdecoderobj.h"
#include "../dll/IndigoStringUtils.h"
#include "../utils/FileUtils.h"
#include "../utils/Exception.h"
#include "../utils/PlatformUtils.h"
#include "../utils/StringUtils.h"
#include "../utils/ConPrint.h"


GLObjectRef ModelLoading::makeGLObjectForModelFile(const std::string& path, const Matrix4f& ob_to_world_matrix, Indigo::MeshRef& mesh_out)
{
	if(hasExtension(path, "obj"))
	{
		MLTLibMaterials mats;
		Indigo::MeshRef mesh = new Indigo::Mesh();
		FormatDecoderObj::streamModel(path, *mesh, 1.f, true, mats);

		GLObjectRef ob = new GLObject();
		ob->ob_to_world_matrix = ob_to_world_matrix;
		ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh);

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
		return ob;
	}
	else
		throw Indigo::Exception("unhandled format: " + path);
}



//else if(hasExtension(path, "igmesh"))
//{
//			Indigo::Mesh::readFromFile(toIndigoString(path), *mesh);