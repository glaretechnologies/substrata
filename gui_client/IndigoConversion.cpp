/*=====================================================================
IndigoConversion.cpp
--------------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#include "IndigoConversion.h"


#include "PhysicsObject.h"
#include "../shared/ResourceManager.h"
#include <maths/matrix3.h>
#include <maths/Matrix4f.h>


#if INDIGO_SUPPORT


inline static Indigo::Vec3d toIndigoVec3d(const Colour3f& c)
{
	return Indigo::Vec3d(c.r, c.g, c.b);
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


Indigo::String convertURLToPath(const std::string& URL, ResourceManager& resource_manager)
{
	return toIndigoString(resource_manager.pathForURL(URL));
}


static Indigo::WavelengthDependentParamRef getAlbedoParam(const WorldMaterial& mat, ResourceManager& resource_manager)
{
	if(mat.colour_texture_url.empty())
	{
		return new Indigo::ConstantWavelengthDependentParam(new Indigo::RGBSpectrum(toIndigoVec3d(mat.colour_rgb), 2.2));
	}
	else
	{
		Indigo::Texture tex(convertURLToPath(mat.colour_texture_url, resource_manager));
		tex.tex_coord_generation = new Indigo::UVTexCoordGenerator(
			Indigo::Matrix2(mat.tex_matrix.e),
			Indigo::Vec2d(0.0)
		);
		return new Indigo::TextureWavelengthDependentParam(tex);
	}
}


Indigo::SceneNodeMaterialRef IndigoConversion::convertMaterialToIndigoMat(const WorldMaterial& mat, ResourceManager& resource_manager)
{
	if(mat.opacity.val == 1.0)
	{
		if(mat.roughness.val == 1.0)
		{
			// Export as diffuse
			return new Indigo::SceneNodeMaterial("diffuse mat", new Indigo::DiffuseMaterial(
				getAlbedoParam(mat, resource_manager)
			));
		}
		else
		{
			// Export as phong
			if(mat.metallic_fraction.val == 0.0)
			{
				return new Indigo::SceneNodeMaterial("phong mat", new Indigo::PhongMaterial(
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
				return new Indigo::SceneNodeMaterial("metallic mat", new Indigo::PhongMaterial(
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

				return new Indigo::SceneNodeMaterial("partially metallic mat", new Indigo::BlendMaterial(
					non_metallic,
					metallic,
					new Indigo::ConstantDisplacementParam(mat.metallic_fraction.val),
					false // step blend
				));
			}
		}
	}
	else
	{
		Indigo::SceneNodeMediumRef medium = new Indigo::SceneNodeMedium(new Indigo::BasicMedium(
			1.5, // IOR
			0.0, // cauchy b
			new Indigo::ConstantVolumeParam(new Indigo::RGBSpectrum(toIndigoVec3d(mat.colour_rgb), 2.2)) // TODO: use inverse thing
		));

		// Convert to specular
		Indigo::SpecularMaterialRef specular = new Indigo::SpecularMaterial(medium);

		return new Indigo::SceneNodeMaterial("specular mat", specular);
	}
}


Indigo::SceneNodeMeshRef IndigoConversion::convertMesh(const WorldObject& object)
{
	Indigo::SceneNodeMeshRef mesh = new Indigo::SceneNodeMesh();
	mesh->setName("mesh geom");

	mesh->mesh = object.physics_object->geometry->toIndigoMesh();

	mesh->normal_smoothing = true;

	return mesh;
}


// without pos
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
	model->setGeometry(convertMesh(object));

	return model;
}


#endif
