/*=====================================================================
ModelLoading.cpp
------------------------
File created by ClassTemplate on Wed Oct 07 15:16:48 2009
Code By Nicholas Chapman.
=====================================================================*/
#include "ModelLoading.h"


#include "MeshBuilding.h"
#include "PhysicsWorld.h"
#include "../shared/WorldObject.h"
#include "../shared/ResourceManager.h"
#include "../shared/VoxelMeshBuilding.h"
#include "../dll/include/IndigoMesh.h"
#include "../dll/include/IndigoException.h"
#include "../graphics/formatdecoderobj.h"
#include "../graphics/FormatDecoderSTL.h"
#include "../graphics/FormatDecoderGLTF.h"
#include "../graphics/FormatDecoderVox.h"
#include "../graphics/SRGBUtils.h"
#include "../simpleraytracer/raymesh.h"
#include "../dll/IndigoStringUtils.h"
#include "../utils/ShouldCancelCallback.h"
#include "../utils/FileUtils.h"
#include "../utils/Exception.h"
#include "../utils/PlatformUtils.h"
#include "../utils/StringUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/StandardPrintOutput.h"
#include "../utils/HashMap.h"
#include "../utils/Sort.h"
#include "../utils/IncludeHalf.h"
#include "../utils/BitUtils.h"
#include "../opengl/GLMeshBuilding.h"
#include "../opengl/IncludeOpenGL.h"
#include "../indigo/UVUnwrapper.h"
#include <tracy/Tracy.hpp>
#include <limits>


static inline Colour3f sanitiseAlbedoColour(const Colour3f& col)
{
	const Colour3f clamped = col.clamp(0.f, 1.f);
	if(clamped.isFinite()) // Check for NaN components
		return clamped;
	else
		return Colour3f(0.2f);
}


static inline Colour3f sanitiseEmissionColour(const Colour3f& col)
{
	if(col.isFinite()) // Check for NaN components
		return col;
	else
		return Colour3f(0.0f);
}


static inline Colour3f sanitiseAndConvertToLinearAlbedoColour(const Colour3f& col)
{
	return toLinearSRGB(sanitiseAlbedoColour(col));
}


static inline Colour3f sanitiseAndConvertToLinearEmissionColour(const Colour3f& col)
{
	return toLinearSRGB(sanitiseEmissionColour(col));
}


void ModelLoading::setGLMaterialFromWorldMaterialWithLocalPaths(const WorldMaterial& mat, OpenGLMaterial& opengl_mat)
{
	opengl_mat.albedo_linear_rgb = sanitiseAndConvertToLinearAlbedoColour(mat.colour_rgb);
	opengl_mat.tex_path = mat.colour_texture_url;

	opengl_mat.emission_linear_rgb = sanitiseAndConvertToLinearEmissionColour(mat.emission_rgb);
	opengl_mat.emission_tex_path = mat.emission_texture_url;

	/*
	Luminance 
	L_v = 683.002 lm/W * integral( L_e(lambda) y_bar(lamdba) dlambda		[L_e = spectral radiance]
	L_v = 683.002 lm/W * 106.856 * 10^-9 m L_e						[Assuming L_e is independent of lambda]
	 
	so
	L_e = L_v / (683.002 lm/W * 106.856 * 10^-9 m)

	emission_scale = L_e * 1.0e-9					[ 1.0e-9 factor is to bring floating point numbers into fp16 range]
	emission_scale = 1.0e-9 * L_v / (683.002 lm/W * 106.856 * 10^-9 m)
	               = L_v * (1.0e-9 / (683.002 lm/W * 106.856 * 10^-9 m))
	*/
	opengl_mat.emission_scale = mat.emission_lum_flux_or_lum * (1.0e-9f / (683.002f * 106.856e-9f)); // 1.0e-9f factor to avoid floating point issues.

	opengl_mat.roughness = mat.roughness.val;
	opengl_mat.metallic_roughness_tex_path = mat.roughness.texture_url;
	opengl_mat.normal_map_path = mat.normal_map_url;
	opengl_mat.transparent = (mat.opacity.val < 1.0f) || BitUtils::isBitSet(mat.flags, WorldMaterial::HOLOGRAM_FLAG); // Hologram is done with transparent material shader.

	opengl_mat.hologram             = BitUtils::isBitSet(mat.flags, WorldMaterial::HOLOGRAM_FLAG);
	opengl_mat.use_wind_vert_shader = BitUtils::isBitSet(mat.flags, WorldMaterial::USE_VERT_COLOURS_FOR_WIND);
	opengl_mat.simple_double_sided  = BitUtils::isBitSet(mat.flags, WorldMaterial::DOUBLE_SIDED_FLAG);
	opengl_mat.decal                = BitUtils::isBitSet(mat.flags, WorldMaterial::DECAL_FLAG);

	opengl_mat.metallic_frac = mat.metallic_fraction.val;

	opengl_mat.fresnel_scale = 0.3f;

	// glTexImage2D expects the start of the texture data to be the lower left of the image, whereas it is actually the upper left.  So flip y coord to compensate.
	opengl_mat.tex_matrix = Matrix2f(1, 0, 0, -1) * mat.tex_matrix;
}


static const std::string toLocalPath(const URLString& URL, ResourceManager& resource_manager)
{
	if(URL.empty())
		return "";
	else
	{
		const bool streamable = ::hasExtension(URL, "mp4");
		if(streamable)
			return toString(URL); // Just leave streamable URLs as-is.
		else
			return resource_manager.pathForURL(URL);
	}
}


void ModelLoading::setGLMaterialFromWorldMaterial(const WorldMaterial& mat, int lod_level, const URLString& lightmap_url, bool use_basis, ResourceManager& resource_manager, OpenGLMaterial& opengl_mat)
{
	const WorldMaterial::GetURLOptions get_url_options(use_basis, /*area allocator=*/nullptr);

	opengl_mat.albedo_linear_rgb = sanitiseAndConvertToLinearAlbedoColour(mat.colour_rgb);
	if(!mat.colour_texture_url.empty())
		opengl_mat.tex_path = toLocalPath(mat.getLODTextureURLForLevel(get_url_options, mat.colour_texture_url, lod_level, /*has alpha=*/mat.colourTexHasAlpha()), resource_manager);
	else
		opengl_mat.tex_path.clear();

	opengl_mat.emission_linear_rgb = sanitiseAndConvertToLinearEmissionColour(mat.emission_rgb);
	opengl_mat.emission_tex_path = mat.emission_texture_url;
	opengl_mat.emission_scale = mat.emission_lum_flux_or_lum * (1.0e-9f / (683.002f * 106.856e-9f)); // See comments above


	if(!mat.emission_texture_url.empty())
		opengl_mat.emission_tex_path = toLocalPath(mat.getLODTextureURLForLevel(get_url_options, mat.emission_texture_url, lod_level, /*has alpha=*/false), resource_manager);
	else
		opengl_mat.emission_tex_path.clear();

	if(!mat.roughness.texture_url.empty())
		opengl_mat.metallic_roughness_tex_path = toLocalPath(mat.getLODTextureURLForLevel(get_url_options, mat.roughness.texture_url, lod_level, /*has alpha=*/false), resource_manager);
	else
		opengl_mat.metallic_roughness_tex_path.clear();

	if(!mat.normal_map_url.empty())
		opengl_mat.normal_map_path = toLocalPath(mat.getLODTextureURLForLevel(get_url_options, mat.normal_map_url, lod_level, /*has alpha=*/false), resource_manager);
	else
		opengl_mat.normal_map_path.clear();

	if(!lightmap_url.empty())
		opengl_mat.lightmap_path = toLocalPath(WorldObject::getLODLightmapURLForLevel(lightmap_url, lod_level), resource_manager);
	else
		opengl_mat.lightmap_path.clear();

	opengl_mat.roughness = mat.roughness.val;
	opengl_mat.transparent = (mat.opacity.val < 1.0f) || BitUtils::isBitSet(mat.flags, WorldMaterial::HOLOGRAM_FLAG); // Hologram is done with transparent material shader.

	opengl_mat.metallic_frac = mat.metallic_fraction.val;

	opengl_mat.hologram             = BitUtils::isBitSet(mat.flags, WorldMaterial::HOLOGRAM_FLAG);
	opengl_mat.use_wind_vert_shader = BitUtils::isBitSet(mat.flags, WorldMaterial::USE_VERT_COLOURS_FOR_WIND);
	opengl_mat.simple_double_sided  = BitUtils::isBitSet(mat.flags, WorldMaterial::DOUBLE_SIDED_FLAG);
	opengl_mat.decal                = BitUtils::isBitSet(mat.flags, WorldMaterial::DECAL_FLAG);

	opengl_mat.fresnel_scale = 0.3f;

	// glTexImage2D expects the start of the texture data to be the lower left of the image, whereas it is actually the upper left.  So flip y coord to compensate.
	opengl_mat.tex_matrix = Matrix2f(1, 0, 0, -1) * mat.tex_matrix;

	if(::hasExtension(opengl_mat.tex_path, "mp4"))
		opengl_mat.fresnel_scale = 0; // Remove specular reflections, reduces washed-out look.
}


void ModelLoading::checkValidAndSanitiseMesh(Indigo::Mesh& mesh)
{
	if(mesh.num_uv_mappings > 10)
		throw glare::Exception("Too many UV sets: " + toString(mesh.num_uv_mappings) + ", max is " + toString(10));

/*	if(mesh.vert_normals.size() == 0)
	{
		for(size_t i = 0; i < mesh.vert_positions.size(); ++i)
		{
			this->vertices[i].pos.set(mesh.vert_positions[i].x, mesh.vert_positions[i].y, mesh.vert_positions[i].z);
			this->vertices[i].normal.set(0.f, 0.f, 0.f);
		}

		vertex_shading_normals_provided = false;
	}
	else
	{
		assert(mesh.vert_normals.size() == mesh.vert_positions.size());

		for(size_t i = 0; i < mesh.vert_positions.size(); ++i)
		{
			this->vertices[i].pos.set(mesh.vert_positions[i].x, mesh.vert_positions[i].y, mesh.vert_positions[i].z);
			this->vertices[i].normal.set(mesh.vert_normals[i].x, mesh.vert_normals[i].y, mesh.vert_normals[i].z);

			assert(::isFinite(mesh.vert_normals[i].x) && ::isFinite(mesh.vert_normals[i].y) && ::isFinite(mesh.vert_normals[i].z));
		}

		vertex_shading_normals_provided = true;
	}*/


	// Check any supplied normals are valid.
	for(size_t i=0; i<mesh.vert_normals.size(); ++i)
	{
		const float len2 = mesh.vert_normals[i].length2();
		if(!::isFinite(len2))
			mesh.vert_normals[i] = Indigo::Vec3f(1, 0, 0);
		else
		{
			// NOTE: allow non-unit normals?
		}
	}

	// Copy UVs from Indigo::Mesh
	assert(mesh.num_uv_mappings == 0 || (mesh.uv_pairs.size() % mesh.num_uv_mappings == 0));

	// Check all UVs are not NaNs, as NaN UVs cause NaN filtered texture values, which cause a crash in TextureUnit table look-up.  See https://bugs.glaretechnologies.com/issues/271
	const size_t uv_size = mesh.uv_pairs.size();
	for(size_t i=0; i<uv_size; ++i)
	{
		if(!isFinite(mesh.uv_pairs[i].x))
			mesh.uv_pairs[i].x = 0;
		if(!isFinite(mesh.uv_pairs[i].y))
			mesh.uv_pairs[i].y = 0;
	}

	const uint32 num_uv_groups = (mesh.num_uv_mappings == 0) ? 0 : ((uint32)mesh.uv_pairs.size() / mesh.num_uv_mappings);
	const uint32 num_verts = (uint32)mesh.vert_positions.size();

	// Tris
	for(size_t i = 0; i < mesh.triangles.size(); ++i)
	{
		const Indigo::Triangle& src_tri = mesh.triangles[i];

		// Check vertex indices are in bounds
		for(unsigned int v = 0; v < 3; ++v)
			if(src_tri.vertex_indices[v] >= num_verts)
				throw glare::Exception("Triangle vertex index is out of bounds.  (vertex index=" + toString(mesh.triangles[i].vertex_indices[v]) + ", num verts: " + toString(num_verts) + ")");

		// Check uv indices are in bounds
		if(mesh.num_uv_mappings > 0)
			for(unsigned int v = 0; v < 3; ++v)
				if(src_tri.uv_indices[v] >= num_uv_groups)
					throw glare::Exception("Triangle uv index is out of bounds.  (uv index=" + toString(mesh.triangles[i].uv_indices[v]) + ")");
	}

	// Quads
	for(size_t i = 0; i < mesh.quads.size(); ++i)
	{
		// Check vertex indices are in bounds
		for(unsigned int v = 0; v < 4; ++v)
			if(mesh.quads[i].vertex_indices[v] >= num_verts)
				throw glare::Exception("Quad vertex index is out of bounds.  (vertex index=" + toString(mesh.quads[i].vertex_indices[v]) + ")");

		// Check uv indices are in bounds
		if(mesh.num_uv_mappings > 0)
			for(unsigned int v = 0; v < 4; ++v)
				if(mesh.quads[i].uv_indices[v] >= num_uv_groups)
					throw glare::Exception("Quad uv index is out of bounds.  (uv index=" + toString(mesh.quads[i].uv_indices[v]) + ")");
	}
}


static float getScaleForMesh(const BatchedMesh& mesh)
{
	// Automatically scale object down until it is < x m across
	const float max_span = 5.0f;
	float use_scale = 1.f;
	const js::AABBox aabb = mesh.aabb_os;
	float span = aabb.axisLength(aabb.longestAxis());
	if(::isFinite(span))
	{
		while(span >= max_span)
		{
			use_scale *= 0.1f;
			span *= 0.1f;
		}
	}


	// Scale up if needed
	const float min_span = 0.01f;
	if(::isFinite(span))
	{
		while(span <= min_span)
		{
			use_scale *= 10.f;
			span *= 10.f;
		}
	}

	return use_scale;
}


void ModelLoading::applyScaleToMesh(Indigo::Mesh& mesh, float scale)
{
	for(size_t i=0; i<mesh.vert_positions.size(); ++i)
		mesh.vert_positions[i] *= scale;

	mesh.aabb_os.bound[0] *= scale;
	mesh.aabb_os.bound[1] *= scale;
}


static void scaleMesh(Indigo::Mesh& mesh)
{
	// Automatically scale object down until it is < x m across
	const float max_span = 5.0f;
	float use_scale = 1.f;
	const js::AABBox aabb(
		Vec4f(mesh.aabb_os.bound[0].x, mesh.aabb_os.bound[0].y, mesh.aabb_os.bound[0].z, 1.f),
		Vec4f(mesh.aabb_os.bound[1].x, mesh.aabb_os.bound[1].y, mesh.aabb_os.bound[1].z, 1.f));
	float span = aabb.axisLength(aabb.longestAxis());
	if(::isFinite(span))
	{
		while(span >= max_span)
		{
			use_scale *= 0.1f;
			span *= 0.1f;
		}
	}


	// Scale up if needed
	const float min_span = 0.01f;
	if(::isFinite(span))
	{
		while(span <= min_span)
		{
			use_scale *= 10.f;
			span *= 10.f;
		}
	}


	if(use_scale != 1.f)
	{
		conPrint("Scaling object by " + toString(use_scale));
		ModelLoading::applyScaleToMesh(mesh, use_scale);
	}
}


static float getScaleForVoxModel(const js::AABBox& aabb)
{
	// Automatically scale object down until it is < x m across
	const float max_span = 2.0f;
	float use_scale = 1.f;
	float span = aabb.axisLength(aabb.longestAxis());
	if(::isFinite(span))
	{
		while(span >= max_span)
		{
			use_scale *= 0.5f;
			span *= 0.5f;
		}
	}

	return use_scale;
}


// Rotate vertices around the y axis by half a turn, so that the figure faces in the positive z direction, similarly to Mixamo animation data and readyplayerme avatars.
static void rotateVRMMesh(BatchedMesh& mesh)
{
	conPrint("Rotating VRM mesh");

	const BatchedMesh::VertAttribute& pos = mesh.getAttribute(BatchedMesh::VertAttribute_Position);
	if(pos.component_type != BatchedMesh::ComponentType_Float)
		throw glare::Exception("unhandled pos component type in rotateVRMMesh()");

	const size_t num_verts = mesh.numVerts();
	const size_t vert_stride_B = mesh.vertexSize();

	js::AABBox new_aabb_os = js::AABBox::emptyAABBox();

	for(size_t i=0; i<num_verts; ++i)
	{
		Vec3f v;
		std::memcpy(&v, &mesh.vertex_data[vert_stride_B * i + pos.offset_B], sizeof(Vec3f));
		
		const Vec3f new_v(-v.x, v.y, -v.z);
		
		std::memcpy(&mesh.vertex_data[vert_stride_B * i + pos.offset_B], &new_v, sizeof(Vec3f));
		
		new_aabb_os.enlargeToHoldPoint(new_v.toVec4fPoint());
	}

	const BatchedMesh::VertAttribute* normal_attr = mesh.findAttribute(BatchedMesh::VertAttribute_Normal);
	if(normal_attr)
	{
		if(normal_attr->component_type == BatchedMesh::ComponentType_PackedNormal)
		{
			for(size_t i=0; i<num_verts; ++i)
			{
				uint32 packed_normal;
				std::memcpy(&packed_normal, &mesh.vertex_data[vert_stride_B * i + normal_attr->offset_B], sizeof(uint32));

				Vec4f n = batchedMeshUnpackNormal(packed_normal); // TODO: do this with integer manipulation instead of floats? Will avoid possible rounding error.
				Vec4f new_n(-n[0], n[1], -n[2], 0);

				const uint32 new_packed_normal = batchedMeshPackNormal(new_n);
				std::memcpy(&mesh.vertex_data[vert_stride_B * i + normal_attr->offset_B], &new_packed_normal, sizeof(uint32));
			}
		}
		else
			throw glare::Exception("unhandled normal component type in rotateVRMMesh()");
	}

	// Update animation data
	for(size_t i=0; i<mesh.animation_data.nodes.size(); ++i)
	{
		assert(mesh.animation_data.nodes[i].inverse_bind_matrix.getUpperLeftMatrix() == Matrix3f::identity());
		mesh.animation_data.nodes[i].inverse_bind_matrix.e[12] *= -1.f; // Negate x translation
		mesh.animation_data.nodes[i].inverse_bind_matrix.e[14] *= -1.f; // Negate z translation

		//mesh.animation_data.nodes[i].default_node_hierarchical_to_world.e[12] *= -1.f; // Negate x translation
		//mesh.animation_data.nodes[i].default_node_hierarchical_to_world.e[14] *= -1.f; // Negate z translation

		// Negate x and z components of trans
		mesh.animation_data.nodes[i].trans = Vec4f(
			-mesh.animation_data.nodes[i].trans[0], 
			mesh.animation_data.nodes[i].trans[1], 
			-mesh.animation_data.nodes[i].trans[2],
			0);
	}
	

	mesh.aabb_os = new_aabb_os;
}


/*
let exponent = gamma

gamma = 2 / (alpha^2 - 2)
alpha = r^2
gamma = 2 / (r^4 - 2)
r^4 - 2 = 2 / gamma
r^4 = 2 / gamma + 2
r = (2 / gamma + 2)^(1/4)

*/
static float roughnessForExponent(float exponent)
{
	return std::pow(2.f / (myMax(1.f, exponent) + 2), 1.f / 4.f);
}


void ModelLoading::makeGLObjectForModelFile(
	OpenGLEngine& gl_engine,
	VertexBufferAllocator& vert_buf_allocator,
	glare::Allocator* allocator,
	const std::string& model_path, 
	bool do_opengl_stuff,
	MakeGLObjectResults& results_out
)
{
	results_out.gl_ob = NULL;
	results_out.batched_mesh = NULL;
	results_out.voxels.voxels.clear();
	results_out.materials.clear();
	results_out.scale = Vec3f(1.f);
	results_out.axis = Vec3f(0,0,1);
	results_out.angle = 0;
	results_out.ob_to_world = Matrix4f::identity();


	if(hasExtension(model_path, "vox"))
	{
		VoxFileContents vox_contents;
		FormatDecoderVox::loadModel(model_path, vox_contents);

		// Convert voxels
		if(vox_contents.models.empty())
			throw glare::Exception("No model in vox file.");

		const VoxModel& model = vox_contents.models[0];

		// We will offset the voxel positions so that the origin is in the middle at the bottom of the voxel AABB.
		const int x_offset = (int)-model.aabb.centroid()[0];
		const int y_offset = (int)-model.aabb.centroid()[1];

		results_out.voxels.voxels.resize(model.voxels.size());
		for(size_t i=0; i<vox_contents.models[0].voxels.size(); ++i)
		{
			results_out.voxels.voxels[i].pos = Vec3<int>(model.voxels[i].x + x_offset, model.voxels[i].y + y_offset, model.voxels[i].z);
			results_out.voxels.voxels[i].mat_index = model.voxels[i].mat_index;
		}

		// Convert materials
		results_out.materials.resize(vox_contents.used_materials.size());
		for(size_t i=0; i<results_out.materials.size(); ++i)
		{
			results_out.materials[i] = new WorldMaterial();
			results_out.materials[i]->colour_rgb = Colour3f(
				vox_contents.used_materials[i].col_from_palette[0], 
				vox_contents.used_materials[i].col_from_palette[1], 
				vox_contents.used_materials[i].col_from_palette[2]);
		}

		
		js::Vector<bool, 16> mat_transparent(results_out.materials.size());
		for(size_t i=0; i<results_out.materials.size(); ++i)
			mat_transparent[i] = results_out.materials[i]->opacity.val < 1.f;

		// Scale down voxels so model isn't too large.
		const float use_scale = getScaleForVoxModel(model.aabb);
		const Matrix4f ob_to_world_matrix = Matrix4f::uniformScaleMatrix(use_scale);

		const int subsample_factor = 1;
		PhysicsShape physics_shape;
		Reference<OpenGLMeshRenderData> mesh_data = ModelLoading::makeModelForVoxelGroup(results_out.voxels, subsample_factor, ob_to_world_matrix, /*task_manager,*/ &vert_buf_allocator, /*do opengl stuff=*/do_opengl_stuff, 
			/*need_lightmap_uvs=*/false, mat_transparent, /*build_dynamic_physics_ob=*/false, allocator, physics_shape);

		results_out.ob_to_world = ob_to_world_matrix;

		GLObjectRef ob;
		if(do_opengl_stuff)
		{
			// Make opengl object
			ob = gl_engine.allocateObject();
			ob->ob_to_world_matrix = ob_to_world_matrix;

			ob->mesh_data = mesh_data;

			ob->materials.resize(results_out.materials.size());
			for(size_t i=0; i<results_out.materials.size(); ++i)
			{
				setGLMaterialFromWorldMaterialWithLocalPaths(*results_out.materials[i], ob->materials[i]);
			}
		}

		results_out.scale.set(use_scale, use_scale, use_scale);
		results_out.gl_ob = ob;
	}
	else if(hasExtension(model_path, "obj"))
	{
		MLTLibMaterials mats;
		Indigo::MeshRef mesh = new Indigo::Mesh();
		FormatDecoderObj::streamModel(model_path, *mesh, 1.f, /*parse mtllib=*/true, mats);

		checkValidAndSanitiseMesh(*mesh);

		// Convert model coordinates to z up
		for(size_t i=0; i<mesh->vert_positions.size(); ++i)
			mesh->vert_positions[i] = Indigo::Vec3f(mesh->vert_positions[i].x, -mesh->vert_positions[i].z, mesh->vert_positions[i].y);

		// Also normalise normals to avoid problems encoding into GL_INT_2_10_10_10_REV format.
		for(size_t i=0; i<mesh->vert_normals.size(); ++i)
			mesh->vert_normals[i] = normalise(Indigo::Vec3f(mesh->vert_normals[i].x, -mesh->vert_normals[i].z, mesh->vert_normals[i].y));

		// Automatically scale object down until it is < x m across
		scaleMesh(*mesh);

		// Now that vertices have been modified, recompute AABB
		mesh->endOfModel();

		// Get smallest z coord
		float min_z = std::numeric_limits<float>::max();
		for(size_t i=0; i<mesh->vert_positions.size(); ++i)
			min_z = myMin(min_z, mesh->vert_positions[i].z);

		// Move object so that it lies on the z=0 (ground) plane
		const Matrix4f use_matrix = Matrix4f::identity(); // Matrix4f::translationMatrix(0, 0, -min_z)* ob_to_world_matrix;

		results_out.ob_to_world = use_matrix;

		GLObjectRef ob;
		if(do_opengl_stuff)
		{
			ob = gl_engine.allocateObject();
			ob->ob_to_world_matrix = use_matrix;
			ob->mesh_data = GLMeshBuilding::buildIndigoMesh(&vert_buf_allocator, mesh, /*skip opengl calls=*/false);

			ob->materials.resize(mesh->num_materials_referenced);
		}

		results_out.materials/*loaded_materials_out*/.resize(mesh->num_materials_referenced);
		for(uint32 i=0; i<mesh->num_materials_referenced; ++i)
		{
			results_out.materials[i] = new WorldMaterial();

			// Have we parsed such a material from the .mtl file?
			bool found_mat = false;
			for(size_t z=0; z<mats.materials.size(); ++z)
				if(mats.materials[z].name == toStdString(mesh->used_materials[i]))
				{
					const std::string tex_path = (!mats.materials[z].map_Kd.path.empty()) ? FileUtils::join(FileUtils::getDirectory(mats.mtl_file_path), mats.materials[z].map_Kd.path) : "";

					// Colours in MTL files seem to be linear.
					// We will add the ambient and diffuse colours together to get the final colour, otherwise the result can be too dark.
					const Colour3f use_col = sanitiseAlbedoColour(mats.materials[z].Ka + mats.materials[z].Kd); // Clamps to [0, 1]
					const float roughness = roughnessForExponent(mats.materials[z].Ns_exponent);
					const float alpha = myClamp(mats.materials[z].d_opacity, 0.f, 1.f);

					if(do_opengl_stuff)
					{
						ob->materials[i].albedo_linear_rgb = use_col;
						ob->materials[i].tex_path = tex_path;
						ob->materials[i].roughness = roughness;
						ob->materials[i].alpha = alpha;
					}

					results_out.materials[i]->colour_rgb = toNonLinearSRGB(use_col);
					results_out.materials[i]->colour_texture_url = tex_path;
					results_out.materials[i]->opacity = ScalarVal(alpha);
					results_out.materials[i]->roughness = ScalarVal(roughness);

					found_mat = true;
				}

			if(!found_mat)
			{
				// Assign dummy mat
				if(do_opengl_stuff)
				{
					ob->materials[i].albedo_linear_rgb = toLinearSRGB(Colour3f(0.7f, 0.7f, 0.7f));
					//ob->materials[i].albedo_tex_path = "resources/obstacle.png";
					ob->materials[i].roughness = 0.5f;
				}

				//loaded_materials_out[i]->colour_texture_url = "resources/obstacle.png";
				results_out.materials[i]->opacity = ScalarVal(1.f);
				results_out.materials[i]->roughness = ScalarVal(0.5f);
			}

			if(do_opengl_stuff)
				ob->materials[i].tex_matrix = Matrix2f(1, 0, 0, -1);
		}
		results_out.batched_mesh = BatchedMesh::buildFromIndigoMesh(*mesh);
		results_out.gl_ob = ob;
	}
	else if(hasExtension(model_path, "gltf") || hasExtension(model_path, "glb") || hasExtension(model_path, "vrm"))
	{
		Timer timer;
		
		GLTFLoadedData gltf_data;
		BatchedMeshRef batched_mesh = hasExtension(model_path, "gltf") ? 
			FormatDecoderGLTF::loadGLTFFile(model_path, gltf_data) : 
			FormatDecoderGLTF::loadGLBFile(model_path, gltf_data);
		
		conPrint("Loaded GLTF model in " + timer.elapsedString());

		batched_mesh->checkValidAndSanitiseMesh();

		if(batched_mesh->animation_data.vrm_data.nonNull())
			rotateVRMMesh(*batched_mesh);

		const float scale = getScaleForMesh(*batched_mesh);

		results_out.scale = Vec3f(scale);
		results_out.axis = Vec3f(1,0,0);
		results_out.angle = Maths::pi_2<float>();

		results_out.ob_to_world = Matrix4f::rotationMatrix(normalise(results_out.axis.toVec4fVector()), results_out.angle) * Matrix4f::scaleMatrix(scale, scale, scale);

		GLObjectRef gl_ob;
		if(do_opengl_stuff)
		{
			gl_ob = gl_engine.allocateObject();
			gl_ob->ob_to_world_matrix = results_out.ob_to_world;

			gl_ob->mesh_data = GLMeshBuilding::buildBatchedMesh(&vert_buf_allocator, batched_mesh, /*skip_opengl_calls=*/false);
		}

		const size_t bmesh_num_mats_referenced = batched_mesh->numMaterialsReferenced();
		if(gltf_data.materials.materials.size() < bmesh_num_mats_referenced)
			throw glare::Exception("mats.materials had incorrect size.");

		if(do_opengl_stuff)
			gl_ob->materials.resize(bmesh_num_mats_referenced);
		results_out.materials.resize(bmesh_num_mats_referenced);
		for(uint32 i=0; i<bmesh_num_mats_referenced; ++i)
		{
			results_out.materials[i] = new WorldMaterial();

			const GLTFResultMaterial& gltf_mat = gltf_data.materials.materials[i];

			const std::string tex_path = gltf_mat.diffuse_map.path;
			const std::string metallic_roughness_tex_path = gltf_mat.metallic_roughness_map.path;
			const std::string emission_tex_path = gltf_mat.emissive_map.path;
			const std::string normal_map_path = gltf_mat.normal_map.path;

			// NOTE: gltf has (0,0) at the upper left of the image, as opposed to the Indigo/substrata/opengl convention of (0,0) being at the lower left
			// (See https://github.com/KhronosGroup/glTF/blob/master/specification/2.0/README.md#images)
			// Therefore we need to negate the y coord.
			// For the gl_ob, there would usually be another negation, so the two cancel out.

			const float L_v = 2.0e5f; // luminance.  Chosen to have a bit of a glow in daylight.
			const float L_e = L_v / (683.002f * 106.856e-9f); // spectral radiance.  See previous comments for equation.
			const bool use_emission = gltf_mat.emissive_factor.nonZero();
			if(do_opengl_stuff)
			{
				gl_ob->materials[i].albedo_linear_rgb               = sanitiseAlbedoColour(gltf_mat.colour_factor); // Note that gltf_mat.colour_factor is already linear.
				gl_ob->materials[i].tex_path                        = tex_path;
				gl_ob->materials[i].metallic_roughness_tex_path     = metallic_roughness_tex_path;
				gl_ob->materials[i].emission_tex_path               = emission_tex_path;
				gl_ob->materials[i].normal_map_path                 = normal_map_path;
				gl_ob->materials[i].emission_linear_rgb             = sanitiseEmissionColour(gltf_mat.emissive_factor); // Note that gltf_mat.emissive_factor is already linear.
				gl_ob->materials[i].emission_scale                  = use_emission ? (L_e * 1.0e-9f) : 0.f; // 1.0e-9f factor to avoid floating point issues.
				gl_ob->materials[i].roughness                       = gltf_mat.roughness;
				gl_ob->materials[i].alpha                           = gltf_mat.alpha;
				gl_ob->materials[i].transparent                     = gltf_mat.alpha < 1.0f;
				gl_ob->materials[i].metallic_frac                   = gltf_mat.metallic;
				gl_ob->materials[i].simple_double_sided             = gltf_mat.double_sided;
			}

			results_out.materials[i]->colour_rgb                = toNonLinearSRGB(gltf_mat.colour_factor);
			results_out.materials[i]->colour_texture_url        = tex_path;
			results_out.materials[i]->roughness.texture_url     = metallic_roughness_tex_path; // HACK: just assign to roughness URL
			results_out.materials[i]->emission_texture_url      = emission_tex_path;
			results_out.materials[i]->normal_map_url            = normal_map_path;
			results_out.materials[i]->emission_rgb              = toNonLinearSRGB(gltf_mat.emissive_factor);
			results_out.materials[i]->emission_lum_flux_or_lum  = use_emission ? L_v : 0.f;
			results_out.materials[i]->opacity                   = ScalarVal(gltf_mat.alpha);
			results_out.materials[i]->roughness.val             = gltf_mat.roughness;
			results_out.materials[i]->opacity.val               = gltf_mat.alpha;
			results_out.materials[i]->metallic_fraction.val     = gltf_mat.metallic;
			results_out.materials[i]->tex_matrix                = Matrix2f(1, 0, 0, -1);
			results_out.materials[i]->flags                     = gltf_mat.double_sided ? WorldMaterial::DOUBLE_SIDED_FLAG : 0;
		}
		results_out.batched_mesh = batched_mesh;
		results_out.gl_ob = gl_ob;
	}
	else if(hasExtension(model_path, "stl"))
	{
		try
		{
			Indigo::MeshRef mesh = new Indigo::Mesh();
			FormatDecoderSTL::streamModel(model_path, *mesh, 1.f);
			checkValidAndSanitiseMesh(*mesh);

			// Automatically scale object down until it is < x m across
			scaleMesh(*mesh);

			// Now that vertices have been modified, recompute AABB
			mesh->endOfModel();

			// Get smallest z coord
			float min_z = std::numeric_limits<float>::max();
			for(size_t i=0; i<mesh->vert_positions.size(); ++i)
				min_z = myMin(min_z, mesh->vert_positions[i].z);

			// Move object so that it lies on the z=0 (ground) plane
			const Matrix4f use_matrix = Matrix4f::translationMatrix(0, 0, -min_z);// *ob_to_world_matrix;

			results_out.ob_to_world = use_matrix;

			GLObjectRef ob;
			if(do_opengl_stuff)
			{
				ob = gl_engine.allocateObject();
				ob->ob_to_world_matrix = use_matrix;
				ob->mesh_data = GLMeshBuilding::buildIndigoMesh(&vert_buf_allocator, mesh, false);

				ob->materials.resize(mesh->num_materials_referenced);
			}
			results_out.materials.resize(mesh->num_materials_referenced);
			for(uint32 i=0; i<mesh->num_materials_referenced; ++i)
			{
				// Assign dummy mat
				if(do_opengl_stuff)
				{
					ob->materials[i].albedo_linear_rgb = Colour3f(0.7f, 0.7f, 0.7f);
					ob->materials[i].tex_matrix = Matrix2f(1, 0, 0, -1);
				}

				results_out.materials[i] = new WorldMaterial();
			}

			results_out.batched_mesh = BatchedMesh::buildFromIndigoMesh(*mesh);
			results_out.gl_ob = ob;
		}
		catch(Indigo::IndigoException& e)
		{
			throw glare::Exception(toStdString(e.what()));
		}
	}
	else if(hasExtension(model_path, "igmesh"))
	{
		try
		{
			Indigo::MeshRef mesh = new Indigo::Mesh();
			Indigo::Mesh::readFromFile(toIndigoString(model_path), *mesh);

			checkValidAndSanitiseMesh(*mesh);

			// Automatically scale object down until it is < x m across
			scaleMesh(*mesh);
			
			results_out.ob_to_world = Matrix4f::identity();

			GLObjectRef ob;
			if(do_opengl_stuff)
			{
				ob = gl_engine.allocateObject();
				ob->ob_to_world_matrix = Matrix4f::identity(); // ob_to_world_matrix;
				ob->mesh_data = GLMeshBuilding::buildIndigoMesh(&vert_buf_allocator, mesh, /*skip_opengl_calls=*/false);

				ob->materials.resize(mesh->num_materials_referenced);
			}
			results_out.materials.resize(mesh->num_materials_referenced);
			for(uint32 i=0; i<ob->materials.size(); ++i)
			{
				// Assign dummy mat
				if(do_opengl_stuff)
				{
					ob->materials[i].albedo_linear_rgb = Colour3f(0.7f, 0.7f, 0.7f);
					ob->materials[i].tex_path = "data/resources/obstacle.png";
					ob->materials[i].roughness = 0.5f;
					ob->materials[i].tex_matrix = Matrix2f(1, 0, 0, -1);
				}

				results_out.materials[i] = new WorldMaterial();
				//results_out.materials[i]->colour_texture_url = "resources/obstacle.png";
				results_out.materials[i]->opacity = ScalarVal(1.f);
				results_out.materials[i]->roughness = ScalarVal(0.5f);
			}
			
			results_out.batched_mesh = BatchedMesh::buildFromIndigoMesh(*mesh);
			results_out.gl_ob = ob;
		}
		catch(Indigo::IndigoException& e)
		{
			throw glare::Exception(toStdString(e.what()));
		}
	}
	else if(hasExtension(model_path, "bmesh"))
	{
		BatchedMeshRef bmesh = BatchedMesh::readFromFile(model_path, allocator);

		bmesh->checkValidAndSanitiseMesh();

		// Automatically scale object down until it is < x m across
		//scaleMesh(*bmesh);

		results_out.ob_to_world = Matrix4f::identity();

		GLObjectRef gl_ob;
		if(do_opengl_stuff)
		{
			gl_ob = gl_engine.allocateObject();
			gl_ob->ob_to_world_matrix = Matrix4f::identity(); // ob_to_world_matrix;
			gl_ob->mesh_data = GLMeshBuilding::buildBatchedMesh(&vert_buf_allocator, bmesh, /*skip_opengl_calls=*/false);
		}
		const size_t num_mats = bmesh->numMaterialsReferenced();
		if(do_opengl_stuff)
			gl_ob->materials.resize(num_mats);
		results_out.materials.resize(num_mats);
		for(uint32 i=0; i<num_mats; ++i)
		{
			// Assign dummy mat
			if(do_opengl_stuff)
			{
				gl_ob->materials[i].albedo_linear_rgb = Colour3f(0.7f, 0.7f, 0.7f);
				gl_ob->materials[i].tex_path = "data/resources/obstacle.png";
				gl_ob->materials[i].roughness = 0.5f;
				gl_ob->materials[i].tex_matrix = Matrix2f(1, 0, 0, -1);
			}

			results_out.materials[i] = new WorldMaterial();
			//results_out.materials[i]->colour_texture_url = "resources/obstacle.png";
			results_out.materials[i]->opacity = ScalarVal(1.f);
			results_out.materials[i]->roughness = ScalarVal(0.5f);
		}

		results_out.batched_mesh = bmesh;
		results_out.gl_ob = gl_ob;
	}
	else
		throw glare::Exception("Format not supported: " + getExtension(model_path));
}


GLObjectRef ModelLoading::makeImageCube(OpenGLEngine& gl_engine, VertexBufferAllocator& vert_buf_allocator, 
	const std::string& image_path, int im_w, int im_h,
	BatchedMeshRef& mesh_out,
	std::vector<WorldMaterialRef>& world_materials_out,
	Vec3f& scale_out
	)
{
	float use_w, use_h;
	if(im_w > im_h)
	{
		use_w = 1;
		use_h = (float)im_h / (float)im_w;
	}
	else
	{
		use_h = 1;
		use_w = (float)im_w / (float)im_h;
	}

	MeshBuilding::MeshBuildingResults results = MeshBuilding::makeImageCube(vert_buf_allocator);

	const float depth = 0.02f;
	const Matrix4f use_matrix = Matrix4f::scaleMatrix(use_w, depth, use_h) * Matrix4f::translationMatrix(-0.5f, 0, 0); // transform in gl preview

	GLObjectRef preview_gl_ob = gl_engine.allocateObject();
	preview_gl_ob->ob_to_world_matrix = use_matrix;
	preview_gl_ob->mesh_data = results.opengl_mesh_data;
	preview_gl_ob->materials.resize(2);

	// Front/back face material:
	preview_gl_ob->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(0.9f));
	preview_gl_ob->materials[0].tex_path = image_path;
	preview_gl_ob->materials[0].roughness = 0.5f;
	preview_gl_ob->materials[0].tex_matrix = Matrix2f(1, 0, 0, -1);

	// Edge material:
	preview_gl_ob->materials[1].albedo_linear_rgb = toLinearSRGB(Colour3f(0.7f));
	preview_gl_ob->materials[1].roughness = 0.5f;
	preview_gl_ob->materials[1].tex_matrix = Matrix2f(1, 0, 0, -1);


	scale_out = Vec3f(use_w, depth, use_h);

	world_materials_out.resize(2);

	world_materials_out[0] = new WorldMaterial();
	world_materials_out[0]->colour_rgb = Colour3f(0.9f);
	world_materials_out[0]->opacity = ScalarVal(1.f);
	world_materials_out[0]->roughness = ScalarVal(0.5f);
	world_materials_out[0]->colour_texture_url = image_path;

	world_materials_out[1] = new WorldMaterial();
	world_materials_out[1]->colour_rgb = Colour3f(0.7f);
	world_materials_out[1]->opacity = ScalarVal(1.f);
	world_materials_out[1]->roughness = ScalarVal(0.5f);

	mesh_out = BatchedMesh::buildFromIndigoMesh(*results.indigo_mesh);

	return preview_gl_ob;
}


GLObjectRef ModelLoading::makeGLObjectForMeshDataAndMaterials(OpenGLEngine& gl_engine, const Reference<OpenGLMeshRenderData> gl_meshdata, //size_t num_materials_referenced,
	int ob_lod_level, const std::vector<WorldMaterialRef>& materials, const URLString& lightmap_url, bool use_basis,
	ResourceManager& resource_manager,
	const Matrix4f& ob_to_world_matrix)
{
	// Make the GLObject
	GLObjectRef ob = gl_engine.allocateObject();
	ob->ob_to_world_matrix = ob_to_world_matrix;
	ob->mesh_data = gl_meshdata;

	ob->materials.resize(gl_meshdata->num_materials_referenced);
	for(uint32 i=0; i<ob->materials.size(); ++i)
	{
		if(i < materials.size())
		{
			setGLMaterialFromWorldMaterial(*materials[i], ob_lod_level, lightmap_url, use_basis, resource_manager, ob->materials[i]);
		}
		else
		{
			// Assign dummy mat
			ob->materials[i].albedo_linear_rgb = toLinearSRGB(Colour3f(0.7f, 0.7f, 0.7f));
			ob->materials[i].tex_path = "data/resources/obstacle.png";
			ob->materials[i].roughness = 0.5f;
		}
	}

	// Show LOD level by tinting materials
	if(false)
	{
		const int lod_level = 0;// StringUtils::containsString(lod_model_URL, "_lod1") ? 1 : (StringUtils::containsString(lod_model_URL, "_lod2") ? 2 : 0);
		if(lod_level == 1)
		{
			for(uint32 i=0; i<ob->materials.size(); ++i)
			{
				ob->materials[i].albedo_linear_rgb.r *= 0.6;
				ob->materials[i].albedo_linear_rgb.b *= 0.6;
			}
		}
		else if(lod_level == 2)
		{
			for(uint32 i=0; i<ob->materials.size(); ++i)
			{
				ob->materials[i].albedo_linear_rgb.g *= 0.6;
				ob->materials[i].albedo_linear_rgb.b *= 0.6;
			}
		}
	}
	
	return ob;
}


void ModelLoading::setMaterialTexPathsForLODLevel(GLObject& gl_ob, int ob_lod_level, const std::vector<WorldMaterialRef>& materials,
	const URLString& lightmap_url, bool use_basis, ResourceManager& resource_manager)
{
	for(size_t i=0; i<gl_ob.materials.size(); ++i)
	{
		if(i < materials.size())
			setGLMaterialFromWorldMaterial(*materials[i], ob_lod_level, lightmap_url, use_basis, resource_manager, gl_ob.materials[i]);
	}
}


Reference<OpenGLMeshRenderData> ModelLoading::makeGLMeshDataAndBatchedMeshForModelPath(const std::string& model_path, ArrayRef<uint8> model_data_buf, VertexBufferAllocator* vert_buf_allocator, 
	bool skip_opengl_calls, bool build_physics_ob, bool build_dynamic_physics_ob, const js::Vector<bool>& create_physics_tris_for_mat,
	glare::Allocator* mem_allocator, PhysicsShape& physics_shape_out)
{
	// Load mesh from disk:
	BatchedMeshRef batched_mesh;

	if(hasExtension(model_path, "bmesh"))
	{
		batched_mesh = BatchedMesh::readFromData(model_data_buf.data(), model_data_buf.dataSizeBytes(), mem_allocator);
	}
	else if(hasExtension(model_path, "obj"))
	{
		Indigo::MeshRef mesh = new Indigo::Mesh();

		MLTLibMaterials mats;
		FormatDecoderObj::loadModelFromBuffer(model_data_buf.data(), model_data_buf.dataSizeBytes(), model_path, *mesh, 1.f, /*parse mtllib=*/false, mats); // Throws glare::Exception on failure.

		batched_mesh = BatchedMesh::buildFromIndigoMesh(*mesh);
	}
	else if(hasExtension(model_path, "stl"))
	{
		Indigo::MeshRef mesh = new Indigo::Mesh();

		FormatDecoderSTL::loadModelFromBuffer(model_data_buf.data(), model_data_buf.dataSizeBytes(), *mesh, 1.f);

		batched_mesh = BatchedMesh::buildFromIndigoMesh(*mesh);
	}
	else if(hasExtension(model_path, "gltf"))
	{
		GLTFLoadedData gltf_data;
		batched_mesh = FormatDecoderGLTF::loadGLTFFileFromData(model_data_buf.data(), model_data_buf.dataSizeBytes(), /*gltf base dir=*/FileUtils::getDirectory(model_path), /*write_images_to_disk=*/false, gltf_data);
	}
	else if(hasExtension(model_path, "glb") || hasExtension(model_path, "vrm"))
	{
		GLTFLoadedData gltf_data;
		batched_mesh = FormatDecoderGLTF::loadGLBFileFromData(model_data_buf.data(), model_data_buf.dataSizeBytes(), /*gltf base dir=*/FileUtils::getDirectory(model_path), /*write_images_to_disk=*/false, gltf_data);
	}
	else if(hasExtension(model_path, "igmesh"))
	{
		Indigo::MeshRef mesh = new Indigo::Mesh();

		try
		{
			Indigo::Mesh::readFromBuffer(model_data_buf.data(), model_data_buf.dataSizeBytes(), *mesh);
		}
		catch(Indigo::IndigoException& e)
		{
			throw glare::Exception(toStdString(e.what()));
		}

		batched_mesh = BatchedMesh::buildFromIndigoMesh(*mesh);
	}
	else
		throw glare::Exception("Format not supported: " + getExtension(model_path));


	batched_mesh->checkValidAndSanitiseMesh(); // Throws glare::Exception on invalid mesh.

	batched_mesh->optimise(); // Merge batches sharing the same material.

	if(hasExtension(model_path, "gltf") || hasExtension(model_path, "glb") || hasExtension(model_path, "vrm"))
		if(batched_mesh->animation_data.vrm_data.nonNull())
			rotateVRMMesh(*batched_mesh);

	Reference<OpenGLMeshRenderData> gl_meshdata = GLMeshBuilding::buildBatchedMesh(vert_buf_allocator, batched_mesh, /*skip opengl calls=*/skip_opengl_calls);

	if(build_physics_ob)
		physics_shape_out = PhysicsWorld::createJoltShapeForBatchedMesh(*batched_mesh, /*is dynamic=*/build_dynamic_physics_ob, mem_allocator, &create_physics_tris_for_mat);

	return gl_meshdata;
}


struct ModelLoadingTakeFirstElement
{
	inline uint32 operator() (const std::pair<uint32, uint32>& pair) const { return pair.first; }
};


struct ModelLoadingVertKey
{
	Indigo::Vec3f pos;
	Indigo::Vec2f uv0;
	//Indigo::Vec2f uv1;

	inline bool operator == (const ModelLoadingVertKey& b) const
	{
		return pos == b.pos && uv0 == b.uv0/* && uv1 == b.uv1*/;
	}
	inline bool operator != (const ModelLoadingVertKey& b) const
	{
		return pos != b.pos || uv0 != b.uv0/* || uv1 != b.uv1*/;
	}
};


struct ModelLoadingVertKeyHash
{
	size_t operator() (const ModelLoadingVertKey& v) const
	{
		return hashBytes((const uint8*)&v.pos, sizeof(Vec3f));
	}
};


// Requires dest and src to be 4-byte aligned.
// size is in bytes.
inline static void copyUInt32s(void* const dest, const void* const src, size_t size_B)
{
	/*assert(((uint64)dest % 4 == 0) && ((uint64)src % 4 == 0));

	const size_t num_uints = size_B / 4;

	for(size_t z=0; z<num_uints; ++z)
		*((uint32*)dest + z) = *((const uint32*)src + z);*/
	std::memcpy(dest, src, size_B); // Not necessarily 4 byte aligned now so just use memcpy.
}


static Reference<OpenGLMeshRenderData> buildVoxelOpenGLMeshData(const Indigo::Mesh& mesh_/*, const Vec3<int>& min_vert_coords, const Vec3<int>& max_vert_coords*/, glare::Allocator* mem_allocator)
{
	const Indigo::Mesh* const mesh				= &mesh_;
	const Indigo::Triangle* const tris			= mesh->triangles.data();
	const size_t num_tris						= mesh->triangles.size();
	const Indigo::Vec3f* const vert_positions	= mesh->vert_positions.data();
	const size_t vert_positions_size			= mesh->vert_positions.size();
	const Indigo::Vec2f* const uv_pairs			= mesh->uv_pairs.data();
	const size_t uvs_size						= mesh->uv_pairs.size();

	const bool mesh_has_uvs						= mesh->num_uv_mappings > 0;
	//const bool mesh_has_uv1					= mesh->num_uv_mappings > 1;
	const uint32 num_uv_sets					= mesh->num_uv_mappings;

	// If we have a UV set, it will be the lightmap UVs.

	// Work out the min and max vertex coordinates, to see if we can store in an int8 or int16.
	const int min_coord = myMin((int)mesh_.aabb_os.bound[0].x, (int)mesh_.aabb_os.bound[0].y, (int)mesh_.aabb_os.bound[0].z);
	const int max_coord = myMax((int)mesh_.aabb_os.bound[1].x, (int)mesh_.aabb_os.bound[1].y, (int)mesh_.aabb_os.bound[1].z);

	size_t pos_size;
	GLenum pos_gl_type;
	if(min_coord >= -128 && max_coord < 128)
	{
		pos_size = sizeof(int8) * 3;
		pos_gl_type = GL_BYTE;
	}
	else if(min_coord >= -32768 && max_coord < 32768)
	{
		pos_size = sizeof(int16) * 3;
		pos_gl_type = GL_SHORT;
	}
	else
	{
		// We shouldn't get here because VoxelMeshBuilding doMakeIndigoMeshForVoxelGroupWith3dArray should reject such voxel groups.
		throw glare::Exception("voxel coord magnitude too large: " + toString(min_coord) + ", " + toString(max_coord));
	}

	Reference<OpenGLMeshRenderData> mesh_data = new OpenGLMeshRenderData();
	mesh_data->vert_data.setAllocator(mem_allocator);
	mesh_data->vert_index_buffer.setAllocator(mem_allocator);
	mesh_data->vert_index_buffer_uint16.setAllocator(mem_allocator);
	mesh_data->vert_index_buffer_uint8.setAllocator(mem_allocator);

	mesh_data->has_shading_normals = false;
	mesh_data->has_uvs = mesh_has_uvs;

	mesh_data->num_materials_referenced = mesh->num_materials_referenced;

	size_t num_bytes_per_vert = 0;
	size_t uv0_offset = 0;

	if(!mesh_has_uvs)
	{
		// Special fast case for no UVs (e.g. no lightmap UVs).
		// In this case we don't need to do merging
		num_bytes_per_vert = pos_size;

		glare::AllocatorVector<uint8, 16>& vert_data = mesh_data->vert_data;
		vert_data.resizeNoCopy(vert_positions_size * num_bytes_per_vert);

		// Copy vertex data
		if(pos_gl_type == GL_BYTE)
		{
			for(size_t i=0; i<vert_positions_size; ++i)
			{
				const int8 pos[3] = { (int8)round(vert_positions[i].x), (int8)round(vert_positions[i].y), (int8)round(vert_positions[i].z) };
				std::memcpy(&vert_data[i * pos_size], pos, sizeof(int8) * 3);
			}
		}
		else
		{
			assert(pos_gl_type == GL_SHORT);
			for(size_t i=0; i<vert_positions_size; ++i)
			{
				const int16 pos[3] = { (int16)round(vert_positions[i].x), (int16)round(vert_positions[i].y), (int16)round(vert_positions[i].z) };
				std::memcpy(&vert_data[i * pos_size], pos, sizeof(int16) * 3);
			}
		}

		// Allocate index buffer
		const size_t num_indices = num_tris * 3;
		size_t bytes_per_index_val;
		/*if(vert_positions_size < 128)
		{
			// Don't use 8-bit indices for now, possible bad performance on some devices.
			mesh_data->setIndexType(GL_UNSIGNED_BYTE);
			mesh_data->vert_index_buffer_uint8.resize(num_indices);
			bytes_per_index_val = 1;
		}
		else */if(vert_positions_size < 32768)
		{
			mesh_data->setIndexType(GL_UNSIGNED_SHORT);
			mesh_data->vert_index_buffer_uint16.resize(num_indices);
			bytes_per_index_val = 2;
		}
		else
		{
			mesh_data->setIndexType(GL_UNSIGNED_INT);
			mesh_data->vert_index_buffer.resize(num_indices);
			bytes_per_index_val = 4;
		}

		size_t vert_index_buffer_i = 0; // Current write index into vert_index_buffer
		size_t last_pass_start_index = 0;
		uint32 current_mat_index = std::numeric_limits<uint32>::max();

		if(mesh->triangles.size() > 0)
		{
			// Create list of triangle references sorted by material index
			glare::AllocatorVector<std::pair<uint32, uint32>, 16> unsorted_tri_indices;
			unsorted_tri_indices.setAllocator(mem_allocator);
			unsorted_tri_indices.resizeNoCopy(num_tris);

			glare::AllocatorVector<std::pair<uint32, uint32>, 16> tri_indices; // Sorted by material
			tri_indices.setAllocator(mem_allocator);
			tri_indices.resizeNoCopy(num_tris);

			for(uint32 t = 0; t < num_tris; ++t)
				unsorted_tri_indices[t] = std::make_pair(tris[t].tri_mat_index, t);

			Sort::serialCountingSort(/*in=*/unsorted_tri_indices.data(), /*out=*/tri_indices.data(), num_tris, ModelLoadingTakeFirstElement());

			for(uint32 t = 0; t < num_tris; ++t)
			{
				// If we've switched to a new material then start a new triangle range
				if(tri_indices[t].first != current_mat_index)
				{
					if(t > 0) // Don't add zero-length passes.
					{
						OpenGLBatch batch;
						batch.material_index = current_mat_index;
						batch.prim_start_offset_B = (uint32)(last_pass_start_index * bytes_per_index_val); // adjust to byte offset.
						batch.num_indices = (uint32)(vert_index_buffer_i - last_pass_start_index);
						mesh_data->batches.push_back(batch);
					}
					last_pass_start_index = vert_index_buffer_i;
					current_mat_index = tri_indices[t].first;
				}

				const Indigo::Triangle& tri = tris[tri_indices[t].second];
				if(bytes_per_index_val == 1)
				{
					for(uint32 i = 0; i < 3; ++i) // For each vert in tri:
						mesh_data->vert_index_buffer_uint8 [vert_index_buffer_i++] = (uint8)tri.vertex_indices[i];
				}
				else if(bytes_per_index_val == 2)
				{
					for(uint32 i = 0; i < 3; ++i) // For each vert in tri:
						mesh_data->vert_index_buffer_uint16[vert_index_buffer_i++] = (uint16)tri.vertex_indices[i];
				}
				else
				{
					for(uint32 i = 0; i < 3; ++i) // For each vert in tri:
						mesh_data->vert_index_buffer       [vert_index_buffer_i++] = tri.vertex_indices[i];;
				}
			}

			// Build last pass data that won't have been built yet.
			OpenGLBatch batch;
			batch.material_index = current_mat_index;
			batch.prim_start_offset_B = (uint32)(last_pass_start_index * bytes_per_index_val); // adjust to byte offset.
			batch.num_indices = (uint32)(vert_index_buffer_i - last_pass_start_index);
			mesh_data->batches.push_back(batch);
		}
	}
	else // Else if mesh has UVs:
	{
		// If UVs are somewhat small in magnitude, use GL_HALF_FLOAT instead of GL_FLOAT.
		// If the magnitude is too high we can get artifacts if we just use half precision.
		//const bool use_half_uv1 = true; // TEMP canUseHalfUVs(mesh); // Just for UV1

		//const size_t packed_uv1_size = use_half_uv1 ? sizeof(half)*2 : sizeof(float)*2;

		/*
		Vertex data layout is
		position [always present]
		uv_0     [optional]
		uv_1     [optional]
		*/
		uv0_offset         = Maths::roundUpToMultipleOfPowerOf2(pos_size, sizeof(half)); // WebGL requires alignment of the UV half values to 2-byte boundaries, otherwise gives errors. (see https://github.com/KhronosGroup/WebGL/issues/914)
		// And it may be required in desktop OpenGL as well, or give better peformance.  So just do the alignment unconditionally for now.

		const size_t uv0_size = sizeof(half)  * 2;
		//const GLenum uv0_gl_type = GL_HALF_FLOAT;

		assert(num_uv_sets > 0);
		num_bytes_per_vert = uv0_offset + uv0_size;

		glare::AllocatorVector<uint8, 16>& vert_data = mesh_data->vert_data;
		vert_data.reserve(mesh->vert_positions.size() * num_bytes_per_vert);

		js::Vector<uint32, 16> uint32_indices(mesh->triangles.size() * 3 + mesh->quads.size() * 6);

		size_t vert_index_buffer_i = 0; // Current write index into vert_index_buffer
		size_t next_merged_vert_i = 0;
		size_t last_pass_start_index = 0;
		uint32 current_mat_index = std::numeric_limits<uint32>::max();

		ModelLoadingVertKey empty_key;
		empty_key.pos = Indigo::Vec3f(std::numeric_limits<float>::infinity());
		empty_key.uv0 = Indigo::Vec2f(0.f);
		//empty_key.uv1 = Indigo::Vec2f(0.f);
		HashMap<ModelLoadingVertKey, uint32, ModelLoadingVertKeyHash> vert_map(empty_key, // Map from vert data to merged index
			/*expected_num_items=*/mesh->vert_positions.size(), mem_allocator);


		if(mesh->triangles.size() > 0)
		{
			// Create list of triangle references sorted by material index
			glare::AllocatorVector<std::pair<uint32, uint32>, 16> unsorted_tri_indices;//(num_tris);
			unsorted_tri_indices.setAllocator(mem_allocator);
			unsorted_tri_indices.resizeNoCopy(num_tris);

			glare::AllocatorVector<std::pair<uint32, uint32>, 16> tri_indices;//(num_tris); // Sorted by material
			tri_indices.setAllocator(mem_allocator);
			tri_indices.resizeNoCopy(num_tris);

			for(uint32 t = 0; t < num_tris; ++t)
				unsorted_tri_indices[t] = std::make_pair(tris[t].tri_mat_index, t);

			Sort::serialCountingSort(/*in=*/unsorted_tri_indices.data(), /*out=*/tri_indices.data(), num_tris, ModelLoadingTakeFirstElement());

			for(uint32 t = 0; t < num_tris; ++t)
			{
				// If we've switched to a new material then start a new triangle range
				if(tri_indices[t].first != current_mat_index)
				{
					if(t > 0) // Don't add zero-length passes.
					{
						OpenGLBatch batch;
						batch.material_index = current_mat_index;
						batch.prim_start_offset_B = (uint32)(last_pass_start_index); // Store index for now, will be adjusted to byte offset later.
						batch.num_indices = (uint32)(vert_index_buffer_i - last_pass_start_index);
						mesh_data->batches.push_back(batch);
					}
					last_pass_start_index = vert_index_buffer_i;
					current_mat_index = tri_indices[t].first;
				}

				const Indigo::Triangle& tri = tris[tri_indices[t].second];
				for(uint32 i = 0; i < 3; ++i) // For each vert in tri:
				{
					const uint32 pos_i		= tri.vertex_indices[i];
					const uint32 base_uv_i	= tri.uv_indices[i];
					const uint32 uv_i = base_uv_i * num_uv_sets; // Index of UV for UV set 0.
					if(pos_i >= vert_positions_size)
						throw glare::Exception("vert index out of bounds");
					if(mesh_has_uvs && uv_i >= uvs_size)
						throw glare::Exception("UV index out of bounds");

					// Look up merged vertex
					const Indigo::Vec2f uv0 = mesh_has_uvs ? uv_pairs[uv_i    ] : Indigo::Vec2f(0.f);
					//const Indigo::Vec2f uv1 = mesh_has_uv1 ? uv_pairs[uv_i + 1] : Indigo::Vec2f(0.f);

					ModelLoadingVertKey key;
					key.pos = vert_positions[pos_i];
					key.uv0 = uv0;
					//key.uv1 = uv1;

					const auto res = vert_map.insert(std::make_pair(key, (uint32)next_merged_vert_i)); // Insert new (key, value) pair, or return iterator to existing one.
					const uint32 merged_v_index = res.first->second; // Get existing or new (key, vert_i) pair, then access vert_i.
					if(res.second) // If was inserted:
					{
						next_merged_vert_i++;
						const size_t cur_size = vert_data.size();
						vert_data.resize(cur_size + num_bytes_per_vert);
						if(pos_gl_type == GL_BYTE)
						{
							const int8 pos[3] = { (int8)round(vert_positions[pos_i].x), (int8)round(vert_positions[pos_i].y), (int8)round(vert_positions[pos_i].z) };
							std::memcpy(&vert_data[cur_size], pos, sizeof(int8) * 3);
						}
						else
						{
							assert(pos_gl_type == GL_SHORT);
							const int16 pos[3] = { (int16)round(vert_positions[pos_i].x), (int16)round(vert_positions[pos_i].y), (int16)round(vert_positions[pos_i].z) };
							std::memcpy(&vert_data[cur_size], pos, sizeof(int16) * 3);
						}

						if(mesh_has_uvs)
						{
							// Copy uv_0
							const half half_uv[2] = { half(uv0.x),  half(uv0.y) };
							copyUInt32s(&vert_data[cur_size + uv0_offset], half_uv, 4);

							// Copy uv_1
							/*if(mesh_has_uv1)
							{
								if(use_half_uv1)
								{
									const half half_uv[2] = { half(uv1.x),  half(uv1.y) };
									copyUInt32s(&vert_data[cur_size + uv1_offset], half_uv, 4);
								}
								else
									copyUInt32s(&vert_data[cur_size + uv1_offset], &uv1.x, sizeof(Indigo::Vec2f));
							}*/
						}
					}

					uint32_indices[vert_index_buffer_i++] = (uint32)merged_v_index;
				}
			}
		}

		// Build last pass data that won't have been built yet.
		OpenGLBatch batch;
		batch.material_index = current_mat_index;
		batch.prim_start_offset_B = (uint32)(last_pass_start_index); // Store index for now, will be adjusted to byte offset later.
		batch.num_indices = (uint32)(vert_index_buffer_i - last_pass_start_index);
		mesh_data->batches.push_back(batch);

		const size_t num_merged_verts = next_merged_vert_i;

		// Build index data
		const size_t num_indices = uint32_indices.size();

		/*if(num_merged_verts < 128)
		{
			// Don't use 8-bit indices for now, possible bad performance on some devices.

			mesh_data->setIndexType(GL_UNSIGNED_BYTE);

			mesh_data->vert_index_buffer_uint8.resize(num_indices);

			uint8* const dest_indices = mesh_data->vert_index_buffer_uint8.data();
			for(size_t i=0; i<num_indices; ++i)
				dest_indices[i] = (uint8)uint32_indices[i];
		}
		else */if(num_merged_verts < 32768)
		{
			mesh_data->setIndexType(GL_UNSIGNED_SHORT);

			mesh_data->vert_index_buffer_uint16.resize(num_indices);

			uint16* const dest_indices = mesh_data->vert_index_buffer_uint16.data();
			for(size_t i=0; i<num_indices; ++i)
				dest_indices[i] = (uint16)uint32_indices[i];

			// Adjust batch prim_start_offset, from index to byte offset
			for(size_t i=0; i<mesh_data->batches.size(); ++i)
				mesh_data->batches[i].prim_start_offset_B *= 2;
		}
		else
		{
			mesh_data->setIndexType(GL_UNSIGNED_INT);

			mesh_data->vert_index_buffer.resize(num_indices);

			uint32* const dest_indices = mesh_data->vert_index_buffer.data();
			for(size_t i=0; i<num_indices; ++i)
				dest_indices[i] = uint32_indices[i];

			// Adjust batch prim_start_offset, from index to byte offset
			for(size_t i=0; i<mesh_data->batches.size(); ++i)
				mesh_data->batches[i].prim_start_offset_B *= 4;
		}
	}

	VertexAttrib pos_attrib;
	pos_attrib.enabled = true;
	pos_attrib.num_comps = 3;
	pos_attrib.type = pos_gl_type;
	pos_attrib.normalised = false;
	pos_attrib.stride = (uint32)num_bytes_per_vert;
	pos_attrib.offset = 0;
	mesh_data->vertex_spec.attributes.push_back(pos_attrib);

	// Only bother specifying these attributes if we need lightmap_uv_attrib.  (They are needed to maintain the attribute order in that case). 
	if(num_uv_sets >= 1)
	{
		VertexAttrib normal_attrib;
		normal_attrib.enabled = false;
		normal_attrib.num_comps = 3;
		normal_attrib.type = GL_FLOAT;
		normal_attrib.normalised = false;
		normal_attrib.stride = 4; // (uint32)num_bytes_per_vert; // Stride needs to be a multiple of 4 for WebGL.  Shouldn't matter what it is since the attribute is not enabled.
		normal_attrib.offset = 0;
		mesh_data->vertex_spec.attributes.push_back(normal_attrib);

		VertexAttrib uv_attrib;
		uv_attrib.enabled = false;
		uv_attrib.num_comps = 2;
		uv_attrib.type = GL_FLOAT;
		uv_attrib.normalised = false;
		uv_attrib.stride = 4; // TEMP (uint32)num_bytes_per_vert;
		uv_attrib.offset = 0;
		mesh_data->vertex_spec.attributes.push_back(uv_attrib);

		VertexAttrib colour_attrib;
		colour_attrib.enabled = false;
		colour_attrib.num_comps = 3;
		colour_attrib.type = GL_FLOAT;
		colour_attrib.normalised = false;
		colour_attrib.stride = 4; // TEMP (uint32)num_bytes_per_vert;
		colour_attrib.offset = 0;
		mesh_data->vertex_spec.attributes.push_back(colour_attrib);

		VertexAttrib lightmap_uv_attrib;
		lightmap_uv_attrib.enabled = true;
		lightmap_uv_attrib.num_comps = 2;
		lightmap_uv_attrib.type = GL_HALF_FLOAT;
		lightmap_uv_attrib.normalised = false;
		lightmap_uv_attrib.stride = (uint32)num_bytes_per_vert;
		lightmap_uv_attrib.offset = (uint32)uv0_offset;
		mesh_data->vertex_spec.attributes.push_back(lightmap_uv_attrib);
	}
	/*if(num_uv_sets >= 2)
	{
		VertexAttrib uv_attrib;
		uv_attrib.enabled = true;
		uv_attrib.num_comps = 2;
		uv_attrib.type = use_half_uv1 ? GL_HALF_FLOAT : GL_FLOAT;
		uv_attrib.normalised = false;
		uv_attrib.stride = (uint32)num_bytes_per_vert;
		uv_attrib.offset = (uint32)uv1_offset;
		mesh_data->vertex_spec.attributes.push_back(uv_attrib);
	}*/

	mesh_data->vertex_spec.checkValid();

	mesh_data->aabb_os = js::AABBox(
		Vec4f(mesh_.aabb_os.bound[0].x, mesh_.aabb_os.bound[0].y, mesh_.aabb_os.bound[0].z, 1.f),
		Vec4f(mesh_.aabb_os.bound[1].x, mesh_.aabb_os.bound[1].y, mesh_.aabb_os.bound[1].z, 1.f)
	);

	return mesh_data;
}


Reference<OpenGLMeshRenderData> ModelLoading::makeModelForVoxelGroup(const VoxelGroup& voxel_group, int subsample_factor, const Matrix4f& ob_to_world, 
	VertexBufferAllocator* vert_buf_allocator, bool do_opengl_stuff, bool need_lightmap_uvs, const js::Vector<bool, 16>& mats_transparent, bool build_dynamic_physics_ob, 
	glare::Allocator* mem_allocator, PhysicsShape& physics_shape_out)
{
	ZoneScoped; // Tracy profiler

	// Timer timer;
	StandardPrintOutput print_output;

	Indigo::MeshRef indigo_mesh = VoxelMeshBuilding::makeIndigoMeshForVoxelGroup(voxel_group, subsample_factor, mats_transparent, mem_allocator);
	// We will compute geometric normals in the opengl shader, so don't need to compute them here.

	if(need_lightmap_uvs)
	{
		// UV unwrap it:
		const js::AABBox aabb_os(
			Vec4f(indigo_mesh->aabb_os.bound[0].x, indigo_mesh->aabb_os.bound[0].y, indigo_mesh->aabb_os.bound[0].z, 1.f),
			Vec4f(indigo_mesh->aabb_os.bound[1].x, indigo_mesh->aabb_os.bound[1].y, indigo_mesh->aabb_os.bound[1].z, 1.f)
		);
		const js::AABBox aabb_ws = aabb_os.transformedAABB(ob_to_world);

		const int clamped_side_res = WorldObject::getLightMapSideResForAABBWS(aabb_ws);

		const float normed_margin = 2.f / clamped_side_res;
		UVUnwrapper::build(*indigo_mesh, ob_to_world, print_output, normed_margin); // Adds UV set to indigo_mesh.
	}

	// Convert Indigo mesh to opengl data
	Reference<OpenGLMeshRenderData> mesh_data = buildVoxelOpenGLMeshData(*indigo_mesh, mem_allocator);

	physics_shape_out = PhysicsWorld::createJoltShapeForIndigoMesh(*indigo_mesh, build_dynamic_physics_ob, mem_allocator);

	// Load rendering data into GPU mem if requested.
	if(do_opengl_stuff)
	{
		mesh_data->vbo_handle = vert_buf_allocator->allocateVertexDataSpace(mesh_data->vertex_spec.vertStride(), mesh_data->vert_data.data(), mesh_data->vert_data.dataSizeBytes());

		if(!mesh_data->vert_index_buffer_uint8.empty())
		{
			mesh_data->indices_vbo_handle = vert_buf_allocator->allocateIndexDataSpace(mesh_data->vert_index_buffer_uint8.data(), mesh_data->vert_index_buffer_uint8.dataSizeBytes());
			assert(mesh_data->getIndexType() == GL_UNSIGNED_BYTE);
		}
		else if(!mesh_data->vert_index_buffer_uint16.empty())
		{
			mesh_data->indices_vbo_handle = vert_buf_allocator->allocateIndexDataSpace(mesh_data->vert_index_buffer_uint16.data(), mesh_data->vert_index_buffer_uint16.dataSizeBytes());
			assert(mesh_data->getIndexType() == GL_UNSIGNED_SHORT);
		}
		else
		{
			mesh_data->indices_vbo_handle = vert_buf_allocator->allocateIndexDataSpace(mesh_data->vert_index_buffer.data(), mesh_data->vert_index_buffer.dataSizeBytes());
			assert(mesh_data->getIndexType() == GL_UNSIGNED_INT);
		}

		vert_buf_allocator->getOrCreateAndAssignVAOForMesh(*mesh_data, mesh_data->vertex_spec);

		mesh_data->vert_data.clearAndFreeMem();
		mesh_data->vert_index_buffer.clearAndFreeMem();
		mesh_data->vert_index_buffer_uint16.clearAndFreeMem();
		mesh_data->vert_index_buffer_uint8.clearAndFreeMem();
	}

	// conPrint("ModelLoading::makeModelForVoxelGroup for " + toString(voxel_group.voxels.size()) + " voxels took " + timer.elapsedString());
	return mesh_data;
}


#if BUILD_TESTS


#include <simpleraytracer/raymesh.h>
#include <utils/TaskManager.h>
#include <maths/PCG32.h>
#include <utils/TestUtils.h>
#include <utils/MemAlloc.h>


void ModelLoading::test()
{
	conPrint("ModelLoading::test()");

	// Test two adjacent voxels with different materials.  All faces should be added.
	//{
	//	VoxelGroup group;
	//	group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
	//	group.voxels.push_back(Voxel(Vec3<int>(10, 0, 1), 1));
	//	group.voxels.push_back(Voxel(Vec3<int>(20, 0, 1), 0));
	//	group.voxels.push_back(Voxel(Vec3<int>(30, 0, 1), 1));
	//	group.voxels.push_back(Voxel(Vec3<int>(40, 0, 1), 0));
	//	group.voxels.push_back(Voxel(Vec3<int>(50, 0, 1), 1));

	//	Reference<RayMesh> raymesh;
	//	Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, task_manager, /*do_opengl_stuff=*/false, raymesh);

	//	testAssert(data->batches.size() == 2);
	//	testAssert(raymesh->getTriangles().size() == 6 * 6 * 2);
	//}

	
	// Test a single voxel, without lightmap UVs
	try
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));

		js::Vector<bool, 16> mat_transparent;

		PhysicsShape physics_shape;
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, /*subsample_factor=*/1, Matrix4f::identity(), /*vert_buf_allocator=*/NULL, 
			/*do_opengl_stuff=*/false, /*need_lightmap_uvs=*/false, mat_transparent, /*build_dynamic_physics_ob=*/false, /*mem_allocator=*/NULL, physics_shape);

		testAssert(data->getNumVerts()    == 8); // Verts can be shared due to no lightmap UVs.
		//testAssert(physics_shape->raymesh->getNumVerts() == 8); // Physics mesh verts are always shared, regardless of lightmap UVs on rendering mesh.
		testAssert(data->getNumTris()             == 6 * 2);
		//testAssert(physics_shape->raymesh->getTriangles().size() == 6 * 2);
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}

	// Test a single voxel, with lightmap UVs
	try
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));

		js::Vector<bool, 16> mat_transparent;

		PhysicsShape physics_shape;
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, /*subsample_factor=*/1, Matrix4f::identity(), /*vert_buf_allocator=*/NULL, 
			/*do_opengl_stuff=*/false, /*need_lightmap_uvs=*/true, mat_transparent, /*build_dynamic_physics_ob=*/false, /*mem_allocator=*/NULL, physics_shape);

		testAssert(data->getNumVerts()    == 6 * 4); // UV unwrapping will make verts unique
		//testAssert(physics_shape->raymesh->getNumVerts() == 8); // Physics mesh verts are always shared, regardless of lightmap UVs on rendering mesh.
		testAssert(data->getNumTris()             == 6 * 2);
		//testAssert(physics_shape->raymesh->getTriangles().size() == 6 * 2);
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}

	// Test two adjacent voxels with same material.
	try
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(1, 0, 0), 0));

		js::Vector<bool, 16> mat_transparent;

		PhysicsShape physics_shape;
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, /*subsample_factor=*/1, Matrix4f::identity(), /*vert_buf_allocator=*/NULL, 
			/*do_opengl_stuff=*/false, /*need_lightmap_uvs=*/true, mat_transparent, /*build_dynamic_physics_ob=*/false, /*mem_allocator=*/NULL, physics_shape);

		testAssert(data->getNumVerts()    == 6 * 4); // UV unwrapping will make verts unique
		//testAssert(physics_shape->raymesh->getNumVerts() == 8);
		testAssert(data->getNumTris()             == 6 * 2);
		//testAssert(physics_shape->raymesh->getTriangles().size() == 6 * 2);
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}

	// Test two adjacent voxels (along y axis) with same material.
	try
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(0, 1, 0), 0));

		js::Vector<bool, 16> mat_transparent;

		PhysicsShape physics_shape;
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, /*subsample_factor=*/1, Matrix4f::identity(), /*vert_buf_allocator=*/NULL, 
			/*do_opengl_stuff=*/false, /*need_lightmap_uvs=*/true, mat_transparent, /*build_dynamic_physics_ob=*/false, /*mem_allocator=*/NULL, physics_shape);

		testAssert(data->getNumVerts()    == 6 * 4); // UV unwrapping will make verts unique
		//testAssert(physics_shape->raymesh->getNumVerts() == 8);
		testAssert(data->getNumTris()             == 6 * 2);
		//testAssert(physics_shape->raymesh->getNumVerts() == 8);
		//testAssert(physics_shape->raymesh->getTriangles().size() == 2 * 6);
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}


	// Test two adjacent voxels (along z axis) with same material, without lightmap UVs
	try
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 1), 0));

		js::Vector<bool, 16> mat_transparent;

		PhysicsShape physics_shape;
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, /*subsample_factor=*/1, Matrix4f::identity(), /*vert_buf_allocator=*/NULL, 
			/*do_opengl_stuff=*/false, /*need_lightmap_uvs=*/false, mat_transparent, /*build_dynamic_physics_ob=*/false, /*mem_allocator=*/NULL, physics_shape);

		testAssert(data->getNumVerts()    == 8);
		//testAssert(physics_shape->raymesh->getNumVerts() == 8);
		testAssert(data->getNumTris()             == 6 * 2);
		//testAssert(physics_shape->raymesh->getTriangles().size() == 2 * 6);
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}

	// Test two adjacent voxels (along z axis) with same material, with lightmap UVs
	try
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 1), 0));

		js::Vector<bool, 16> mat_transparent;

		PhysicsShape physics_shape;
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, /*subsample_factor=*/1, Matrix4f::identity(), /*vert_buf_allocator=*/NULL, 
			/*do_opengl_stuff=*/false, /*need_lightmap_uvs=*/true, mat_transparent, /*build_dynamic_physics_ob=*/false, /*mem_allocator=*/NULL, physics_shape);

		testAssert(data->getNumVerts()    == 6 * 4); // UV unwrapping will make verts unique
		//testAssert(physics_shape->raymesh->getNumVerts() == 8);
		testAssert(data->getNumTris()             == 6 * 2);
		//testAssert(physics_shape->raymesh->getTriangles().size() == 2 * 6);
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}


	// Test two adjacent voxels with different opaque materials, without lightmap UVs.  The faces between the voxels should not be added.
	try
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(1, 0, 0), 1));

		js::Vector<bool, 16> mat_transparent(2, false);

		PhysicsShape physics_shape;
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, /*subsample_factor=*/1, Matrix4f::identity(), /*vert_buf_allocator=*/NULL,
			/*do_opengl_stuff=*/false, /*need_lightmap_uvs=*/false , mat_transparent, /*build_dynamic_physics_ob=*/false, /*mem_allocator=*/NULL, physics_shape);

		testEqual(data->getNumVerts(), (size_t)(4 * 3));
		//testAssert(physics_shape->raymesh->getNumVerts() == 4 * 3);
		testEqual(data->getNumTris(), (size_t)(2 * 5 * 2)); // Each voxel should have 5 faces (face between 2 voxels is not added),  * 2 voxels, * 2 triangles/face
		//testAssert(physics_shape->raymesh->getTriangles().size() == 2 * 5 * 2);
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}

	// Test two adjacent voxels with different opaque materials.  The faces between the voxels should not be added.
	try
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(1, 0, 0), 1));

		js::Vector<bool, 16> mat_transparent(2, false);

		PhysicsShape physics_shape;
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, /*subsample_factor=*/1, Matrix4f::identity(), /*vert_buf_allocator=*/NULL, 
			/*do_opengl_stuff=*/false, /*need_lightmap_uvs=*/true, mat_transparent, /*build_dynamic_physics_ob=*/false, /*mem_allocator=*/NULL, physics_shape);

		testEqual(data->getNumVerts(), (size_t)32); // verts half way along the sides of the cuboid can be shared.
		//testAssert(physics_shape->raymesh->getNumVerts() == 4 * 3);
		testEqual(data->getNumTris(), (size_t)(2 * 5 * 2)); // Each voxel should have 5 faces (face between 2 voxels is not added),  * 2 voxels, * 2 triangles/face
		//testAssert(physics_shape->raymesh->getTriangles().size() == 2 * 5 * 2);
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}

	// Test two voxels with large coordinate values.
	try
	{
		VoxelGroup group;
		group.voxels.push_back(Voxel(Vec3<int>(-32768, 0, 0), 0));
		group.voxels.push_back(Voxel(Vec3<int>(32766, 0, 0), 1));

		js::Vector<bool, 16> mat_transparent(2, false);

		PhysicsShape physics_shape;
		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, /*subsample_factor=*/1, Matrix4f::identity(), /*vert_buf_allocator=*/NULL, 
			/*do_opengl_stuff=*/false, /*need_lightmap_uvs=*/false, mat_transparent, /*build_dynamic_physics_ob=*/false, /*mem_allocator=*/NULL, physics_shape);

		testEqual(data->getNumVerts(), (size_t)16);
		testEqual(data->getNumTris(), (size_t)(2 * 6 * 2)); // Each voxel should have 6 faces,  * 2 voxels, * 2 triangles/face
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}


	// Performance test
	//if(false)
	//{
	//	PCG32 rng(1);
	//	VoxelGroup group;
	//	for(int z=0; z<100; z += 2)
	//		for(int y=0; y<100; ++y)
	//			for(int x=0; x<20; ++x)
	//				if(rng.unitRandom() < 0.2f)
	//					group.voxels.push_back(Voxel(Vec3<int>(x, y, z), 0));

	//	for(int i=0; i<500; ++i)
	//	{
	//		Timer timer;

	//		js::Vector<bool, 16> mat_transparent;

	//		Reference<RayMesh> raymesh;
	//		Reference<OpenGLMeshRenderData> data = makeModelForVoxelGroup(group, /*subsample_factor=*/1, Matrix4f::identity(), task_manager, /*vert_buf_allocator=*/NULL, 
	//			/*do_opengl_stuff=*/false, /*need_lightmap_uvs=*/true, mat_transparent, raymesh);

	//		conPrint("Meshing of " + toString(group.voxels.size()) + " voxels took " + timer.elapsedString());
	//		conPrint("Resulting num tris: " + toString(raymesh->getTriangles().size()));
	//	}
	//}

	// Performance test
	if(false)
	{
		try
		{
			
			{
				VoxelGroup group;

				{
					// Object with UID 158939: game console in gallery near central square.  8292300 voxels, but only makes 2485 vertices and 4500 triangles.
					// 158928: another game console
					// 169166: roof of voxel house, 951327 voxels, makes 3440 vertices, 5662 triangles
					// 169202: voxel tree, 108247 voxels, 75756 vertices, 124534 tris

					std::vector<uint8> filecontents;
					FileUtils::readEntireFile("D:\\files\\voxeldata\\ob_169202_voxeldata.voxdata", filecontents);
					//FileUtils::readEntireFile("D:\\files\\voxeldata\\ob_158939_voxeldata.voxdata", filecontents);
					//FileUtils::readEntireFile("N:\\new_cyberspace\\trunk\\testfiles\\voxels\\ob_151064_voxeldata.voxdata", filecontents);
					group.voxels.resize(filecontents.size() / sizeof(Voxel));
					testAssert(filecontents.size() == group.voxels.dataSizeBytes());
					std::memcpy(group.voxels.data(), filecontents.data(), filecontents.size());
				}


				conPrint("AABB: " + group.getAABB().toString());
				conPrint("AABB volume: " + toString(group.getAABB().volume()));

				js::Vector<bool, 16> mat_transparent(256);

				printVar(MemAlloc::getHighWaterMarkB());

				for(int i=0; i<1000; ++i)
				{
					Timer timer;

					PhysicsShape physics_shape;
					makeModelForVoxelGroup(group, /*subsample factor=*/1, /*ob to world=*/Matrix4f::identity(),
						/*vert buf allocator=*/NULL, /*do_opengl_stuff=*/false, /*need_lightmap_uvs=*/false, mat_transparent, /*build_dynamic_physics_ob=*/false, /*mem_allocator=*/NULL, physics_shape);

					conPrint("Meshing of " + toString(group.voxels.size()) + " voxels with subsample_factor=1 took " + timer.elapsedString());
					//conPrint("Resulting num tris: " + toString(data->triangles.size()));

					printVar(MemAlloc::getHighWaterMarkB());
				}

				//{
				//	Timer timer;

				//	Reference<Indigo::Mesh> data = makeIndigoMeshForVoxelGroup(group, /*subsample_factor=*/2, /*generate_shading_normals=*/false, mat_transparent);

				//	conPrint("Meshing of " + toString(group.voxels.size()) + " voxels with subsample_factor=2 took " + timer.elapsedString());
				//	conPrint("Resulting num tris: " + toString(data->triangles.size()));
				//}
			}
		}
		catch(glare::Exception& e)
		{
			failTest(e.what());
		}
	}

	conPrint("ModelLoading::test() done.");
}


#endif // BUILD_TESTS
