/*=====================================================================
IndigoConversion.cpp
--------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "IndigoConversion.h"


#include "PhysicsObject.h"
#include "../shared/ResourceManager.h"
#include <opengl/OpenGLMeshRenderData.h>
#include <graphics/BatchedMesh.h>
#include <graphics/ImageMap.h>
#include <maths/matrix3.h>
#include <maths/Matrix4f.h>
#include <ConPrint.h>


#if INDIGO_SUPPORT


#include <dll/include/IndigoMaterial.h>
#include <dll/include/SceneNodeModel.h>


inline static Indigo::Vec3d toIndigoVec3d(const Colour3f& c)
{
	if(c.isFinite())
		return Indigo::Vec3d(c.r, c.g, c.b);
	else
		return Indigo::Vec3d(0.0);
}


inline static Indigo::Vec3d toIndigoVec3d(const Vec3d& c)
{
	return Indigo::Vec3d(c.x, c.y, c.z);
}


inline static Indigo::Vec2d toIndigoVec2d(const Vec2d& c)
{
	return Indigo::Vec2d(c.x, c.y);
}


inline static const Indigo::String toIndigoString(const std::string& s)
{
	return Indigo::String(s.c_str(), s.length());
}


//static Indigo::String convertURLToPath(const std::string& URL, ResourceManager& resource_manager)
//{
//	return toIndigoString(resource_manager.pathForURL(URL));
//}


static Indigo::WavelengthDependentParamRef getAlbedoParam(const WorldMaterial& mat, ResourceManager& resource_manager)
{
	Indigo::RGBSpectrumRef rgb_spect = new Indigo::RGBSpectrum(toIndigoVec3d(mat.colour_rgb), 2.2);

	if(mat.colour_texture_url.empty())
	{
		return new Indigo::ConstantWavelengthDependentParam(rgb_spect);
	}
	else
	{
		const std::string path = resource_manager.pathForURL(mat.colour_texture_url);

		// Image formats that are supported by Indigo.  NOTE: no ktx!
		const bool is_allowed_file_type =
			hasExtension(path, "jpg") || hasExtension(path, "jpeg") ||
			hasExtension(path, "png") ||
			hasExtension(path, "tif") || hasExtension(path, "tiff") ||
			hasExtension(path, "exr") ||
			hasExtension(path, "gif");

		if(is_allowed_file_type)
		{
			Indigo::Texture tex(toIndigoString(path));
			tex.tex_coord_generation = new Indigo::UVTexCoordGenerator(
				Indigo::Matrix2(mat.tex_matrix.e),
				Indigo::Vec2d(0.0)
			);
			return new Indigo::TextureWavelengthDependentParam(tex, rgb_spect);
		}
		else
		{
			return new Indigo::ConstantWavelengthDependentParam(rgb_spect);
		}
	}
}


static Indigo::WavelengthDependentParamRef getEmissionParam(const WorldMaterial& mat, ResourceManager& resource_manager)
{
	if(mat.emission_texture_url.empty())
	{
		return Indigo::WavelengthDependentParamRef();
	}
	else
	{
		const std::string path = resource_manager.pathForURL(mat.emission_texture_url);

		// Image formats that are supported by Indigo.  NOTE: no ktx!
		const bool is_allowed_file_type =
			hasExtension(path, "jpg") || hasExtension(path, "jpeg") ||
			hasExtension(path, "png") ||
			hasExtension(path, "tif") || hasExtension(path, "tiff") ||
			hasExtension(path, "exr") ||
			hasExtension(path, "gif");

		if(is_allowed_file_type)
		{
			Indigo::Texture tex(toIndigoString(path));
			tex.tex_coord_generation = new Indigo::UVTexCoordGenerator(
				Indigo::Matrix2(mat.tex_matrix.e),
				Indigo::Vec2d(0.0)
			);
			return new Indigo::TextureWavelengthDependentParam(tex);
		}
		else
		{
			return Indigo::WavelengthDependentParamRef();
		}
	}
}


static void setEmissionParams(const WorldMaterial& mat, const Indigo::SceneNodeMaterialRef mat_node, ResourceManager& resource_manager)
{
	if(mat.emission_lum_flux_or_lum > 0)
	{
		mat_node->material->base_emission = new Indigo::ConstantWavelengthDependentParam(new Indigo::RGBSpectrum(toIndigoVec3d(mat.emission_rgb), 2.2));

		mat_node->material->emission = getEmissionParam(mat, resource_manager);
	}
}


Indigo::SceneNodeMaterialRef IndigoConversion::convertMaterialToIndigoMat(const WorldMaterial& mat, ResourceManager& resource_manager)
{
	// Handle hologram materials as null materials with emission.  We don't want them to be specular materials.
	if((mat.flags & mat.HOLOGRAM_FLAG) != 0)
	{
		Indigo::SceneNodeMaterialRef mat_node = new Indigo::SceneNodeMaterial("hologram mat", new Indigo::NullMaterial());
		mat_node->material->backface_emit = true;
		setEmissionParams(mat, mat_node, resource_manager);
		return mat_node;
	}

	if(mat.opacity.val == 1.0)
	{
		Indigo::SceneNodeMaterialRef mat_node;
		if(mat.roughness.val == 1.0)
		{
			// Export as diffuse
			mat_node = new Indigo::SceneNodeMaterial("diffuse mat", new Indigo::DiffuseMaterial(
				getAlbedoParam(mat, resource_manager)
			));
		}
		else
		{
			// Export as phong
			if(mat.metallic_fraction.val == 0.0)
			{
				mat_node = new Indigo::SceneNodeMaterial("phong mat", new Indigo::PhongMaterial(
					getAlbedoParam(mat, resource_manager), // albedo
					NULL, // spec refl
					new Indigo::ConstantWavelengthIndependentParam(mat.roughness.val), // roughness
					new Indigo::ConstantWavelengthIndependentParam(0.5), // fresnel scale
					1.5, // IOR
					"" // nk data
				));
			}
			else if(mat.metallic_fraction.val == 1.0)
			{
				mat_node = new Indigo::SceneNodeMaterial("metallic mat", new Indigo::PhongMaterial(
					NULL, // albedo
					getAlbedoParam(mat, resource_manager), // spec refl
					new Indigo::ConstantWavelengthIndependentParam(mat.roughness.val), // roughness
					new Indigo::ConstantWavelengthIndependentParam(1.0), // fresnel scale
					1.5, // IOR
					"" // nk data
				));
			}
			else
			{
				// Export as a blend between metallic phong and non-metallic phong.
				Indigo::SceneNodeMaterialRef non_metallic = new Indigo::SceneNodeMaterial(new Indigo::PhongMaterial(
					getAlbedoParam(mat, resource_manager), // albedo
					NULL, // spec refl
					new Indigo::ConstantWavelengthIndependentParam(mat.roughness.val), // roughness
					new Indigo::ConstantWavelengthIndependentParam(0.5), // fresnel scale
					1.5, // IOR
					"" // nk data
				));

				Indigo::SceneNodeMaterialRef metallic = new Indigo::SceneNodeMaterial(new Indigo::PhongMaterial(
					NULL, // albedo
					getAlbedoParam(mat, resource_manager), // spec refl
					new Indigo::ConstantWavelengthIndependentParam(mat.roughness.val), // roughness
					new Indigo::ConstantWavelengthIndependentParam(1.0), // fresnel scale
					1.5, // IOR
					"" // nk data
				));

				mat_node = new Indigo::SceneNodeMaterial("partially metallic mat", new Indigo::BlendMaterial(
					non_metallic,
					metallic,
					new Indigo::ConstantDisplacementParam(mat.metallic_fraction.val),
					false // step blend
				));
			}
		}

		setEmissionParams(mat, mat_node, resource_manager);

		return mat_node;
	}
	else
	{
		/*
		lets say we have col_target_sRGB.r. (sRGB col)

		col_T = transmittance
		also
		col_T.r = exp(-d * absorb_rgb.r)

		col_target_sRGB.r = linearToSRGB(col_target_linear.r) = linearToSRGB(col_T) = linearToSRGB(exp(-d * absorb_rgb.r))

		srgbToLinear(col_target_sRGB.r) = exp(-d * absorb_rgb.r)

		srgbToLinear(col_target_sRGB.r) = exp(-d * absorb_rgb.r)

		srgbToLinear(col_target_sRGB.r) = exp(-d * absorb_rgb.r)

		ln(srgbToLinear(col_target_sRGB.r)) = -d * absorb_rgb.r

		absorb_rgb.r = ln(srgbToLinear(col_target_sRGB.r)) / -d

		[approximate srgbToLinear(x) as x ^ 2.2:}

		absorb_rgb.r = ln(col_target_sRGB.r ^ 2.2) / -d
		*/


		// Note that we actually use the sRGB colour directly as a linear colour in transparent_frag_shader.glsl, so we don't need to do the
		// sRGB -> linear conversion.
		Colour3f linear_target_col = mat.colour_rgb;// (std::pow(mat.colour_rgb.r, 2.2f), std::pow(mat.colour_rgb.g, 2.2f), std::pow(mat.colour_rgb.b, 2.2f));

		linear_target_col.lowerClampInPlace(0.001f); // Avoid log(0) giving -inf.

		const float d = 0.2;
		const Colour3f absorb_rgb(std::log(linear_target_col.r) / -d, std::log(linear_target_col.g) / -d, std::log(linear_target_col.b) / -d);

		Indigo::SceneNodeMediumRef medium = new Indigo::SceneNodeMedium(new Indigo::BasicMedium(
			1.5, // IOR
			0.0, // cauchy b
			new Indigo::ConstantVolumeParam(new Indigo::RGBSpectrum(toIndigoVec3d(absorb_rgb), 1.0))
		));

		// Convert to specular
		Indigo::SpecularMaterialRef specular = new Indigo::SpecularMaterial(medium);
		specular->is_arch_glass = true;

		return new Indigo::SceneNodeMaterial("specular mat", specular);
	}
}


#if GUI_CLIENT
Indigo::SceneNodeMeshRef IndigoConversion::convertMesh(const WorldObject& object, ResourceManager& resource_manager)
{
	Indigo::SceneNodeMeshRef mesh = new Indigo::SceneNodeMesh();
	mesh->setName("mesh geom");


	// TEMP HACK:
	if(!object.model_url.empty())
	{
		const std::string local_path = resource_manager.getLocalAbsPathForResource(*resource_manager.getExistingResourceForURL(object.model_url));

		BatchedMeshRef bmesh = BatchedMesh::readFromFile(local_path, NULL);

		mesh->mesh = bmesh->buildIndigoMesh();
	}

	//mesh->mesh = object.physics_object->geometry->toIndigoMesh();

	mesh->normal_smoothing = true;

	return mesh;
}


// Without translation
static const Matrix4f obToWorldMatrix(const WorldObject* ob)
{
	return Matrix4f::rotationMatrix(normalise(ob->axis.toVec4fVector()), ob->angle) *
		Matrix4f::scaleMatrix(ob->scale.x, ob->scale.y, ob->scale.z);
}


Indigo::SceneNodeModelRef IndigoConversion::convertObject(const WorldObject& object, ResourceManager& resource_manager)
{
	Indigo::SceneNodeModelRef model = new Indigo::SceneNodeModel();
	model->setName(toIndigoString("Object with UID " + object.uid.toString()));
	
	// Convert transform
	model->keyframes.push_back(Indigo::KeyFrame(
		0.0,
		toIndigoVec3d(object.pos),
		Indigo::AxisAngle::identity()
	));

	model->rotation = new Indigo::MatrixRotation(obToWorldMatrix(&object).getUpperLeftMatrix().e);

	// Convert materials
	if(object.object_type == WorldObject::ObjectType_Hypercard && object.hypercard_map.nonNull())
	{
		// Export as diffuse using an in-mem tex map.
		const ImageMapUInt8Ref src_map = object.hypercard_map;

		Indigo::Texture tex;
		Indigo::InMemUInt8TexMapRef in_mem_tex = new Indigo::InMemUInt8TexMap(src_map->getWidth(), src_map->getHeight(), src_map->getN());
		tex.in_mem_tex = in_mem_tex;
		std::memcpy(in_mem_tex->getPixel(0, 0), src_map->getData(), src_map->getWidth() * src_map->getHeight() * src_map->getN());

		tex.b = 0.85f;

		Indigo::WavelengthDependentParamRef albedo = new Indigo::TextureWavelengthDependentParam(tex);

		Indigo::SceneNodeMaterialRef mat = new Indigo::SceneNodeMaterial(new Indigo::DiffuseMaterial(albedo));

		model->setMaterials(Indigo::Vector<Indigo::SceneNodeMaterialRef>(1, mat));
	}
	else
	{
		Indigo::Vector<Indigo::SceneNodeMaterialRef> indigo_mats(object.materials.size());
		for(size_t i=0; i<object.materials.size(); ++i)
			indigo_mats[i] = convertMaterialToIndigoMat(*object.materials[i], resource_manager);

		model->setMaterials(indigo_mats);
	}

	// Convert geometry
	model->setGeometry(convertMesh(object, resource_manager));

	return model;
}
#endif


#endif
