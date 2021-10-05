/*=====================================================================
LightMapperBot.h
----------------
Copyright Glare Technologies Limited 2020 -
=====================================================================*/


#include "../shared/Protocol.h"
#include "../shared/ResourceManager.h"
#include "../shared/VoxelMeshBuilding.h"
#include "../shared/LODGeneration.h"
#include "../gui_client/ClientThread.h"
#include "../gui_client/DownloadResourcesThread.h"
#include "../gui_client/NetDownloadResourcesThread.h"
#include "../gui_client/UploadResourceThread.h"
#include "../gui_client/ModelLoading.h"
#include <networking/Networking.h>
#include <networking/TLSSocket.h>
#include <PlatformUtils.h>
#include <Clock.h>
#include <Timer.h>
#include <ConPrint.h>
#include <OpenSSL.h>
#include <JSONParser.h>
#include <Exception.h>
#include <TaskManager.h>
#include <StandardPrintOutput.h>
#include <FileChecksum.h>
#include <FileUtils.h>
#include <GlareProcess.h>
#include <networking/URL.h>
#define USE_INDIGO_SDK 1
// Indigo SDK headers:
#if USE_INDIGO_SDK
#include <dll/include/IndigoMesh.h>
#include <dll/include/IndigoException.h>
#include <dll/include/IndigoMaterial.h>
#include <dll/include/SceneNodeModel.h>
#include <dll/include/SceneNodeRenderSettings.h>
#include <dll/include/SceneNodeRoot.h>
#endif
#include <simpleraytracer/raymesh.h>
#include <graphics/BatchedMesh.h>
#include <graphics/KTXDecoder.h>
#include <graphics/EXRDecoder.h>
#include <indigo/UVUnwrapper.h>
#include <tls.h>
#include "../gui_client/IndigoConversion.h"


static const std::string username = "lightmapperbot";
static const std::string password = "3NzpaTM37N";


static const std::string toStdString(const Indigo::String& s)
{
	return std::string(s.dataPtr(), s.length());
}

static const Indigo::String toIndigoString(const std::string& s)
{
	return Indigo::String(s.c_str(), s.length());
}


static bool checkForDisconnect(ThreadSafeQueue<Reference<ThreadMessage> >& msg_queue)
{
	Lock lock(msg_queue.getMutex());
	while(msg_queue.unlockedNonEmpty())
	{
		Reference<ThreadMessage> msg = msg_queue.unlockedDequeue();
		if(dynamic_cast<ClientDisconnectedFromServerMessage*>(msg.ptr()))
			return true;
	}
	return false;
}


class LightMapperBot
{
public:

	LightMapperBot(const std::string& server_hostname_, int server_port_, ResourceManagerRef& resource_manager_, struct tls_config* client_tls_config_)
	:	server_hostname(server_hostname_), server_port(server_port_), resource_manager(resource_manager_), client_tls_config(client_tls_config_)
	{
		resource_download_thread_manager.addThread(new DownloadResourcesThread(&msg_queue, resource_manager, server_hostname, server_port, &this->num_non_net_resources_downloading, client_tls_config));

		for(int i=0; i<4; ++i)
			net_resource_download_thread_manager.addThread(new NetDownloadResourcesThread(&msg_queue, resource_manager, &num_net_resources_downloading));
	}



	void startDownloadingResource(const std::string& url)
	{
		//conPrint("-------------------MainWindow::startDownloadingResource()-------------------\nURL: " + url);

		ResourceRef resource = resource_manager->getOrCreateResourceForURL(url);
		if(resource->getState() != Resource::State_NotPresent) // If it is getting downloaded, or is downloaded:
		{
			conPrint("Already present or being downloaded, skipping...");
			return;
		}

		try
		{
			const URL parsed_url = URL::parseURL(url);

			if(parsed_url.scheme == "http" || parsed_url.scheme == "https")
			{
				this->net_resource_download_thread_manager.enqueueMessage(new DownloadResourceMessage(url));
				num_net_resources_downloading++;
			}
			else
				this->resource_download_thread_manager.enqueueMessage(new DownloadResourceMessage(url));
		}
		catch(glare::Exception& e)
		{
			conPrint("Failed to parse URL '" + url + "': " + e.what());
		}
	}


	// For every resource that the object uses (model, textures etc..), if the resource is not present locally, start downloading it.
	void startDownloadingResourcesForObject(WorldObject* ob)
	{
		std::set<std::string> dependency_URLs;
		ob->getDependencyURLSet(/*lod level=*/0, dependency_URLs);
		for(auto it = dependency_URLs.begin(); it != dependency_URLs.end(); ++it)
		{
			const std::string& url = *it;
			if(!resource_manager->isFileForURLPresent(url))
				startDownloadingResource(url);
		}
	}


	bool allResourcesPresentForOb(WorldObject* ob)
	{
		std::set<std::string> dependency_URLs;
		ob->getDependencyURLSet(/*lod level=*/0, dependency_URLs);
		for(auto it = dependency_URLs.begin(); it != dependency_URLs.end(); ++it)
		{
			const std::string& url = *it;
			if(!resource_manager->isFileForURLPresent(url))
				return false;
		}
		return true;
	}


	// From ModelLoading
	void checkValidAndSanitiseMesh(Indigo::Mesh& mesh)
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

	// Without translation
	static const Matrix4f obToWorldMatrix(const WorldObject* ob)
	{
		return Matrix4f::rotationMatrix(normalise(ob->axis.toVec4fVector()), ob->angle) *
			Matrix4f::scaleMatrix(ob->scale.x, ob->scale.y, ob->scale.z);
	}


	inline static Indigo::Vec3d toIndigoVec3d(const Vec3d& c)
	{
		return Indigo::Vec3d(c.x, c.y, c.z);
	}


	void addObjectToIndigoSG(WorldObject* ob)
	{

	}


	// Load mesh from disk:
	// If batched mesh (bmesh), convert to indigo mesh
	// If indigo mesh, can use directly.
	// If voxel group, mesh it.
	Indigo::SceneNodeMeshRef makeSceneNodeMeshForOb(WorldObject* ob)
	{
		// Construct Indigo Mesh
		Indigo::MeshRef indigo_mesh;
		bool use_shading_normals = true;
		if(ob->object_type == WorldObject::ObjectType_VoxelGroup) // If voxel object, convert to mesh
		{
			ob->decompressVoxels();
			if(ob->getDecompressedVoxelGroup().voxels.size() == 0)
				throw glare::Exception("No voxels in voxel group");
			//BatchedMeshRef batched_mesh = VoxelMeshBuilding::makeBatchedMeshForVoxelGroup(ob->getDecompressedVoxelGroup());
			//indigo_mesh = new Indigo::Mesh();
			//batched_mesh->buildIndigoMesh(*indigo_mesh);

			const VoxelGroup& voxel_group = ob->getDecompressedVoxelGroup();

			// Iterate over voxels and get voxel position bounds
			Vec3<int> minpos, maxpos;
			indigo_mesh = VoxelMeshBuilding::makeIndigoMeshForVoxelGroup(voxel_group, /*subsample factor=*/1, minpos, maxpos);
			use_shading_normals = false;
		}
		else if(ob->object_type == WorldObject::ObjectType_Spotlight)
		{
			// NOTE: copied from MainWindow.cpp:
			const float fixture_w = 0.1;

			// Build Indigo::Mesh
			Indigo::MeshRef spotlight_mesh = new Indigo::Mesh();
			spotlight_mesh->num_uv_mappings = 1;

			spotlight_mesh->vert_positions.resize(4);
			spotlight_mesh->vert_normals.resize(4);
			spotlight_mesh->uv_pairs.resize(4);
			spotlight_mesh->quads.resize(1);

			spotlight_mesh->vert_positions[0] = Indigo::Vec3f(-fixture_w/2, -fixture_w/2, 0.f);
			spotlight_mesh->vert_positions[1] = Indigo::Vec3f(-fixture_w/2,  fixture_w/2, 0.f); // + y
			spotlight_mesh->vert_positions[2] = Indigo::Vec3f( fixture_w/2,  fixture_w/2, 0.f);
			spotlight_mesh->vert_positions[3] = Indigo::Vec3f( fixture_w/2, -fixture_w/2, 0.f); // + x

			spotlight_mesh->vert_normals[0] = Indigo::Vec3f(0, 0, -1);
			spotlight_mesh->vert_normals[1] = Indigo::Vec3f(0, 0, -1);
			spotlight_mesh->vert_normals[2] = Indigo::Vec3f(0, 0, -1);
			spotlight_mesh->vert_normals[3] = Indigo::Vec3f(0, 0, -1);

			spotlight_mesh->uv_pairs[0] = Indigo::Vec2f(0, 0);
			spotlight_mesh->uv_pairs[1] = Indigo::Vec2f(0, 1);
			spotlight_mesh->uv_pairs[2] = Indigo::Vec2f(1, 1);
			spotlight_mesh->uv_pairs[3] = Indigo::Vec2f(1, 0);

			spotlight_mesh->quads[0].mat_index = 0;
			spotlight_mesh->quads[0].vertex_indices[0] = 0;
			spotlight_mesh->quads[0].vertex_indices[1] = 1;
			spotlight_mesh->quads[0].vertex_indices[2] = 2;
			spotlight_mesh->quads[0].vertex_indices[3] = 3;
			spotlight_mesh->quads[0].uv_indices[0] = 0;
			spotlight_mesh->quads[0].uv_indices[1] = 1;
			spotlight_mesh->quads[0].uv_indices[2] = 2;
			spotlight_mesh->quads[0].uv_indices[3] = 3;

			spotlight_mesh->endOfModel();

			indigo_mesh = spotlight_mesh;
			use_shading_normals = false;
		}
		else
		{
			const std::string model_path = resource_manager->pathForURL(ob->model_url);

			if(hasExtension(model_path, "igmesh"))
			{
				try
				{
					indigo_mesh = new Indigo::Mesh();
					Indigo::Mesh::readFromFile(toIndigoString(model_path), *indigo_mesh);
				}
				catch(Indigo::IndigoException& e)
				{
					throw glare::Exception(toStdString(e.what()));
				}
			}
			else if(hasExtension(model_path, "bmesh"))
			{
				BatchedMeshRef batched_mesh = new BatchedMesh();
				BatchedMesh::readFromFile(model_path, *batched_mesh);

				indigo_mesh = new Indigo::Mesh();
				batched_mesh->buildIndigoMesh(*indigo_mesh);
			}
			else
				throw glare::Exception("unhandled model format: " + model_path);
		}

		checkValidAndSanitiseMesh(*indigo_mesh); // Throws Indigo::Exception on invalid mesh.

		Indigo::SceneNodeMeshRef mesh_node = new Indigo::SceneNodeMesh(indigo_mesh);
		mesh_node->normal_smoothing = use_shading_normals;
		return mesh_node;
	}


	Indigo::SceneNodeModelRef makeModelNodeForWorldObject(WorldObject* ob)
	{
		Indigo::SceneNodeMeshRef mesh_node = makeSceneNodeMeshForOb(ob);

		if(ob->object_type == WorldObject::ObjectType_Spotlight)
		{
			Indigo::Vector<Indigo::SceneNodeMaterialRef> indigo_mat_nodes;

			Reference<Indigo::WavelengthDependentParam> albedo_param = new Indigo::ConstantWavelengthDependentParam(new Indigo::RGBSpectrum(Indigo::Vec3d(0.7f), /*gamma=*/2.2));

			float luminous_flux = 10000;
			if(ob->materials.size() >= 1)
				luminous_flux = ob->materials[0]->emission_lum_flux;

			Indigo::DiffuseMaterialRef indigo_mat = new Indigo::DiffuseMaterial(albedo_param);
			indigo_mat->name = toIndigoString("emitting mat");

			if(luminous_flux > 0)
			{
				indigo_mat->base_emission = new Indigo::ConstantWavelengthDependentParam(new Indigo::BlackBodySpectrum(5000, 1.0));//  new Indigo::UniformSpectrum(1.0e10); // TEMP: use D65 or something instead.

				if(ob->materials.size() >= 1)
				{
					// Use colour to multiple emission.
					const WorldMaterialRef world_mat = ob->materials[0];
					indigo_mat->emission = new Indigo::ConstantWavelengthDependentParam(new Indigo::RGBSpectrum(Indigo::Vec3d(world_mat->colour_rgb.r, world_mat->colour_rgb.g, world_mat->colour_rgb.b), /*gamma=*/2.2));
				}
			}
			indigo_mat_nodes.push_back(new Indigo::SceneNodeMaterial(indigo_mat));


			
			Indigo::Vector<Indigo::EmissionScaleRef> emission_scales(1);
			emission_scales[0] = new Indigo::EmissionScale(Indigo::EmissionScale::LUMINOUS_FLUX, luminous_flux, indigo_mat_nodes[0]);

			Indigo::Vector<Indigo::IESProfileRef> ies_profiles(1);
			ies_profiles[0] = new Indigo::IESProfile("zomb_narrow.ies", indigo_mat_nodes[0]);

			Indigo::SceneNodeModelRef model_node = new Indigo::SceneNodeModel();
			model_node->setMaterials(indigo_mat_nodes);
			model_node->setGeometry(mesh_node);
			model_node->keyframes = Indigo::Vector<Indigo::KeyFrame>(1, Indigo::KeyFrame(
				0.0,
				toIndigoVec3d(ob->pos),
				Indigo::AxisAngle::identity()
			));
			model_node->rotation = new Indigo::MatrixRotation(obToWorldMatrix(ob).getUpperLeftMatrix().e);

			if(luminous_flux > 0)
			{
				model_node->setEmissionScales(emission_scales);
				model_node->ies_profiles = ies_profiles;
			}

			return model_node;
		}
		else
		{
			// Make Indigo materials from loaded parcel mats
			Indigo::Vector<Indigo::SceneNodeMaterialRef> indigo_mat_nodes;
			for(size_t i=0; i<ob->materials.size(); ++i)
			{
				const WorldMaterialRef ob_mat = ob->materials[i];

				Indigo::SceneNodeMaterialRef indigo_mat_node = IndigoConversion::convertMaterialToIndigoMat(*ob_mat, *resource_manager);

				//Reference<Indigo::WavelengthDependentParam> albedo_param;
				//if(!parcel_mat->colour_texture_url.empty())
				//{
				//	// TODO: use colour_rgb as tint colour?
				//	const std::string path = resource_manager->pathForURL(parcel_mat->colour_texture_url);

				//	Indigo::Texture tex(toIndigoString(path));
				//	tex.tex_coord_generation = new Indigo::UVTexCoordGenerator(
				//		Indigo::Matrix2(parcel_mat->tex_matrix.e),
				//		Indigo::Vec2d(0.0)
				//	);
				//	albedo_param = new Indigo::TextureWavelengthDependentParam(tex, /*tint_colour=*/new Indigo::RGBSpectrum(Indigo::Vec3d(1.0), /*gamma=*/2.2));
				//}
				//else
				//{
				//	albedo_param = new Indigo::ConstantWavelengthDependentParam(new Indigo::RGBSpectrum(Indigo::Vec3d(parcel_mat->colour_rgb.r, parcel_mat->colour_rgb.g, parcel_mat->colour_rgb.b), /*gamma=*/2.2));
				//}

				//Indigo::DiffuseMaterialRef indigo_mat = new Indigo::DiffuseMaterial(albedo_param);
				//indigo_mat->name = toIndigoString(parcel_mat->name);

				//indigo_mat_nodes.push_back(new Indigo::SceneNodeMaterial(indigo_mat));
				indigo_mat_nodes.push_back(indigo_mat_node);
			}

			if(!ob->pos.isFinite())
				throw glare::Exception("Pos was not finite");
			if(!ob->axis.isFinite())
				throw glare::Exception("axis was not finite");


			Indigo::SceneNodeModelRef model_node = new Indigo::SceneNodeModel();
			model_node->setMaterials(indigo_mat_nodes);
			model_node->setGeometry(mesh_node);
			model_node->keyframes = Indigo::Vector<Indigo::KeyFrame>(1, Indigo::KeyFrame(
				0.0,
				toIndigoVec3d(ob->pos),
				Indigo::AxisAngle::identity()
			));
			model_node->rotation = new Indigo::MatrixRotation(obToWorldMatrix(ob).getUpperLeftMatrix().e);
			return model_node;
		}
	}


	void buildLightMapForOb(WorldState& world_state, WorldObject* ob_to_lightmap)
	{
		try
		{
			conPrint("\n\n\n");
			conPrint("=================== Building lightmap for object ====================");
			conPrint("UID: " + ob_to_lightmap->uid.toString());
			conPrint("model_url: " + ob_to_lightmap->model_url);

			// Hold the world state lock while we process the object and build the indigo scene from it.
			UID ob_uid;
			std::string scene_path;
			bool do_high_qual_bake;
			{
				Lock lock(world_state.mutex);

				ob_uid = ob_to_lightmap->uid;
				
				do_high_qual_bake = BitUtils::isBitSet(ob_to_lightmap->flags, WorldObject::HIGH_QUAL_LIGHTMAP_NEEDS_COMPUTING_FLAG);

				// Clear LIGHTMAP_NEEDS_COMPUTING_FLAG.
				// We do this here, so other clients can re-set the LIGHTMAP_NEEDS_COMPUTING_FLAG while we are baking the lightmap, which means that the
				// lightmap will re-bake when done.
				{
					BitUtils::zeroBit(ob_to_lightmap->flags, WorldObject::LIGHTMAP_NEEDS_COMPUTING_FLAG);
					BitUtils::zeroBit(ob_to_lightmap->flags, WorldObject::HIGH_QUAL_LIGHTMAP_NEEDS_COMPUTING_FLAG);

					// Enqueue ObjectFlagsChanged
					SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
					packet.writeUInt32(Protocol::ObjectFlagsChanged);
					writeToStream(ob_to_lightmap->uid, packet);
					packet.writeUInt32(ob_to_lightmap->flags);

					this->client_thread->enqueueDataToSend(packet);
				}


				// Iterate over all objects and work out which objects should be in the Indigo scene for the lightmap calc.
				std::set<WorldObjectRef> obs_to_render;
				for(auto it = world_state.objects.begin(); it != world_state.objects.end(); ++it)
				{
					const WorldObject* ob = it->second.ptr();
					const double dist = ob_to_lightmap->pos.getDist(ob->pos);

					if(dist < 100)
						obs_to_render.insert(it->second);

					// TEMP: add all objects
					//obs_to_render.insert(it->second);
				}

				// Start downloading any resources we don't have that the object uses.
				for(auto it = obs_to_render.begin(); it != obs_to_render.end(); ++it)
				{
					startDownloadingResourcesForObject(it->ptr());
				}

				// Wait until we have downloaded all resources for the object

				Timer wait_timer;
				while(1)
				{
					bool all_present = true;
					for(auto it = obs_to_render.begin(); it != obs_to_render.end(); ++it)
						if(!allResourcesPresentForOb(it->ptr()))
							all_present = false;

					if(all_present)
						break;

					PlatformUtils::Sleep(50);

					//if(wait_timer.elapsed() > 30)
					//	throw glare::Exception("Failed to download all resources for objects");// with UID " + ob->uid.toString());

					if(wait_timer.elapsed() > 5)//15)
					{
						conPrint("Failed to download all resources for objects, continuing anyway...");
						break;
					}
				}


				Indigo::SceneNodeModelRef model_node_to_lightmap = makeModelNodeForWorldObject(ob_to_lightmap);

				const Matrix4f to_world = obToWorldMatrix(ob_to_lightmap);

				Indigo::SceneNodeMeshRef mesh_node_to_lightmap = model_node_to_lightmap->getGeometry().downcast<Indigo::SceneNodeMesh>();

				Indigo::AABB<float> mesh_aabb_os;
				mesh_node_to_lightmap->mesh->getBoundingBox(mesh_aabb_os);

				const js::AABBox aabb_os(
					Vec4f(mesh_aabb_os.bound[0].x, mesh_aabb_os.bound[0].y, mesh_aabb_os.bound[0].z, 1),
					Vec4f(mesh_aabb_os.bound[1].x, mesh_aabb_os.bound[1].y, mesh_aabb_os.bound[1].z, 1)
				);

				const js::AABBox aabb_ws = aabb_os.transformedAABB(to_world);



				// Work out the resolution at which we should create the lightmap
				//Indigo::AABB<float> aabb;
				//model_node_to_lightmap->getBoundingBox(aabb);

				// compute surface area of AABB
				//const Indigo::Vec3<float> span = aabb_ws.max_ - min_;// aabb.bound[1] - aabb.bound[0];
				//const Vec4f span = aabb_ws.max_ - aabb_ws.min_;// aabb.bound[1] - aabb.bound[0];
				//const float A = 2 * (span.x * span.y) + (span.x * span.z) * (span.y * span.z);c
				/*const float A = aabb_ws.getSurfaceArea();

				// We want an object with maximally sized AABB, similar to that of a parcel, to have the max allowable res (e.g. 512*512)
				// And use a proportionally smaller number of pixels based on a smaller area.

				const float parcel_W = 20;
				const float parcel_H = 10;
				const float parcel_A = parcel_W * parcel_W * 2 + parcel_W * parcel_H * 4;

				const float frac = A / parcel_A;

				const float full_num_px = Maths::square(2048.f);

				const float use_num_px = frac * full_num_px;

				const float use_side_res = std::sqrt(use_num_px);

				const int use_side_res_rounded = Maths::roundUpToMultipleOfPowerOf2((int)use_side_res, (int)4);

				// Clamp to min and max allowable lightmap resolutions
				const int clamped_side_res = myClamp(use_side_res_rounded, 64, 2048);
				*/

				const int clamped_side_res = WorldObject::getLightMapSideResForAABBWS(aabb_ws);
				//printVar(A);
				//printVar(parcel_A);
				//printVar(frac);
				//printVar(use_side_res);
				//printVar(use_side_res_rounded);
				printVar(clamped_side_res);


				Indigo::SceneNodeMeshRef ob_to_lightmap_mesh_node = makeSceneNodeMeshForOb(ob_to_lightmap);
				{
					Indigo::MeshRef ob_to_lightmap_indigo_mesh = ob_to_lightmap_mesh_node->mesh;
				
					// See if this object has a lightmap-suitable UV map already
					const bool has_lightmap_uvs = ob_to_lightmap_indigo_mesh->num_uv_mappings >= 2; // TEMP
					if(!has_lightmap_uvs)
					{
						// Generate lightmap UVs
						StandardPrintOutput print_output;

						const float normed_margin = 2.f / clamped_side_res;

						//const Matrix4f to_world = obToWorldMatrix(ob_to_lightmap);

						UVUnwrapper::build(*ob_to_lightmap_indigo_mesh, to_world, print_output, normed_margin); // Adds UV set to indigo_mesh.

						if(ob_to_lightmap->object_type != WorldObject::ObjectType_VoxelGroup) // If voxel object, don't update to an unwrapped mesh, rather keep voxels.
						{
							// Convert indigo_mesh to a BatchedMesh.
							// This will also merge verts with the same pos and normal.
							BatchedMeshRef batched_mesh = new BatchedMesh();
							batched_mesh->buildFromIndigoMesh(*ob_to_lightmap_indigo_mesh);

							// Save as bmesh in temp location
							const std::string bmesh_disk_path = PlatformUtils::getTempDirPath() + "/lightmapper_bot_temp.bmesh";

							BatchedMesh::WriteOptions write_options;
							write_options.compression_level = 20;
							batched_mesh->writeToFile(bmesh_disk_path);

							// Compute hash over model
							const uint64 model_hash = FileChecksum::fileChecksum(bmesh_disk_path);

							//const std::string original_filename = FileUtils::getFilename(d.result_path); // Use the original filename, not 'temp.igmesh'.
							const std::string mesh_URL = ResourceManager::URLForNameAndExtensionAndHash("unwrapped"/*original_filename*/, "bmesh", model_hash);

							// Copy model to local resources dir.  UploadResourceThread will read from here.
							this->resource_manager->copyLocalFileToResourceDir(bmesh_disk_path, mesh_URL);

							ob_to_lightmap->model_url = mesh_URL;

							// Generate LOD models, to be uploaded to server also.
							ob_to_lightmap->max_model_lod_level = (batched_mesh->numVerts() <= 4 * 6) ? 0 : 2; // If this is a very small model (e.g. a cuboid), don't generate LOD versions of it.

							if(ob_to_lightmap->max_model_lod_level == 2)
							{
								for(int lvl = 1; lvl <= 2; ++lvl)
								{
									const std::string lod_URL  = WorldObject::getLODModelURLForLevel(ob_to_lightmap->model_url, lvl);

									if(!resource_manager->isFileForURLPresent(lod_URL))
									{
										const std::string local_lod_path = resource_manager->pathForURL(lod_URL); // Path where we will write the LOD model.  UploadResourceThread will read from here.

										LODGeneration::generateLODModel(batched_mesh, lvl, local_lod_path);

										resource_manager->setResourceAsLocallyPresentForURL(lod_URL);

										// Spawn an UploadResourceThread to upload the new LOD model
										resource_upload_thread_manager.addThread(new UploadResourceThread(&this->msg_queue, /*local_path=*/local_lod_path, lod_URL, 
											server_hostname, server_port, username, password, client_tls_config, &num_resources_uploading));
									}
								}
							}

							// Send the updated object, with the new model URL, to the server.

							conPrint("Sending unwrapped model '" + mesh_URL + "' to server...");

							// Send ObjectModelURLChanged message to server
							SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
							packet.writeUInt32(Protocol::ObjectModelURLChanged);
							writeToStream(ob_uid, packet);
							packet.writeStringLengthFirst(mesh_URL);

							this->client_thread->enqueueDataToSend(packet);

							// Spawn an UploadResourceThread to upload the new model
							resource_upload_thread_manager.addThread(new UploadResourceThread(&this->msg_queue, /*local_path=*/this->resource_manager->pathForURL(mesh_URL), mesh_URL, 
								server_hostname, server_port, username, password, client_tls_config, &num_resources_uploading));
						}
					}
				}



				//------------------ Make an Indigo scene graph to light the model, then save it to disk ---------------------
				Indigo::SceneNodeModelRef baking_model_node;

				Indigo::SceneNodeRootRef root_node = new Indigo::SceneNodeRoot();

				for(auto it = obs_to_render.begin(); it != obs_to_render.end(); ++it)
				{
					WorldObject* ob = it->ptr();

					if(ob->state == WorldObject::State_Dead)
						continue;

					try
					{
						Indigo::SceneNodeModelRef model_node = makeModelNodeForWorldObject(ob);

						if(ob == ob_to_lightmap)
							model_node->setGeometry(ob_to_lightmap_mesh_node);

						root_node->addChildNode(model_node);

						if(ob == ob_to_lightmap)
							baking_model_node = model_node;
					}
					catch(glare::Exception& e)
					{
						conPrint("Warning: Error while building indigo model for object: " + e.what());
					}
				}


				// Add ground plane as an Indigo scene node model, since it's not an explicit object in Substrata
				{
					//==================== Create ground geometry =========================
					Indigo::MeshRef mesh = new Indigo::Mesh();

					// Make a single quad
					Indigo::Quad q;
					q.mat_index = 0;
					for(int i = 0; i < 4; ++i)
						q.vertex_indices[i] = q.uv_indices[i] = i;
					mesh->quads.push_back(q);

					const float w = 300;
					mesh->vert_positions.push_back(Indigo::Vec3f(-w, -w, 0));
					mesh->vert_positions.push_back(Indigo::Vec3f(-w,  w, 0));
					mesh->vert_positions.push_back(Indigo::Vec3f( w,  w, 0));
					mesh->vert_positions.push_back(Indigo::Vec3f( w, -w, 0));

					mesh->num_uv_mappings = 1;
					mesh->uv_pairs.push_back(Indigo::Vec2f(-w, -w));
					mesh->uv_pairs.push_back(Indigo::Vec2f(-w,  w));
					mesh->uv_pairs.push_back(Indigo::Vec2f( w,  w));
					mesh->uv_pairs.push_back(Indigo::Vec2f( w, -w));

					Indigo::SceneNodeMeshRef mesh_node = new Indigo::SceneNodeMesh(mesh);
					mesh_node->setName("Ground Mesh");

					//==================== Create the ground material. See MainWindow::updateGroundPlane() =========================
					Indigo::Texture tex;
					tex.path = toIndigoString(base_dir_path + "/resources/obstacle.png");
					tex.tex_coord_generation = new Indigo::UVTexCoordGenerator();
					tex.b = std::pow(0.9f, 2.2); // 0.9 RGB colour converted to linear scale.

					Indigo::DiffuseMaterialRef diffuse = new Indigo::DiffuseMaterial(
						new Indigo::TextureWavelengthDependentParam(tex) // albedo param
					);

					Indigo::SceneNodeMaterialRef mat = new Indigo::SceneNodeMaterial(diffuse);
					mat->setName("Ground diffuse material");


					//==================== Create the ground object =========================
					Indigo::SceneNodeModelRef model = new Indigo::SceneNodeModel();
					model->setName("Ground Object");
					model->setGeometry(mesh_node);
					model->keyframes.push_back(Indigo::KeyFrame());
					model->rotation = new Indigo::MatrixRotation();
					model->setMaterials(Indigo::Vector<Indigo::SceneNodeMaterialRef>(1, mat));

					root_node->addChildNode(model); // Add model node to scene graph.  This will add material and geometry nodes used by the model as well.
				}
				


				Indigo::SceneNodeRenderSettingsRef settings_node = Indigo::SceneNodeRenderSettings::getDefaults();
				settings_node->untonemapped_scale.setValue(1.0e-9);
				settings_node->width.setValue(clamped_side_res);
				settings_node->height.setValue(clamped_side_res);
				settings_node->super_sample_factor.setValue(1); // Seems to be required for the denoising to work well
				settings_node->bidirectional.setValue(false);
				settings_node->metropolis.setValue(false);
				settings_node->gpu.setValue(true);
				settings_node->light_map_baking_ob_uid.setValue(baking_model_node->getUniqueID().value()); // Enable light map baking
				settings_node->generate_lightmap_uvs.setValue(false);
				settings_node->capture_direct_sun_illum.setValue(false);
				//settings_node->image_save_period.setValue(2); // Save often for progressive rendering and uploads
				settings_node->save_png.setValue(false);
				settings_node->merging.setValue(false); // Needed for now
				settings_node->vignetting.setValue(false); // Should have no effect on the lightmap, but useful for comparing in non-lightmap baking mode.

				settings_node->optimise_for_denoising.setValue(true);
				settings_node->denoise.setValue(true);

				settings_node->render_env_caustics.setValue(false);

				// See SkyModel2Generator::makeSkyEnvMap() for the details of the whitepoint we chose.
				settings_node->setWhitePoint(Indigo::Vec2d(0.3225750029085, 0.338224992156));

				root_node->addChildNode(settings_node);

				Indigo::SceneNodeTonemappingRef tone_mapping = new Indigo::SceneNodeTonemapping();
				tone_mapping->setType(Indigo::SceneNodeTonemapping::Reinhard);
				tone_mapping->pre_scale = 1;
				tone_mapping->post_scale = 1;
				tone_mapping->burn = 6;
				root_node->addChildNode(tone_mapping);

				Indigo::SceneNodeCameraRef cam = new Indigo::SceneNodeCamera();
				cam->lens_radius = 0.0001;
				cam->autofocus = false;
				cam->exposure_duration = 1.0 / 30.0;
				cam->focus_distance = 2.0;
				cam->forwards = Indigo::Vec3d(0, 1, 0);
				cam->up = Indigo::Vec3d(0, 0, 1);
				cam->setPos(Indigo::Vec3d(0, -2, 0.1));
				root_node->addChildNode(cam);

				Reference<Indigo::SunSkyMaterial> sun_sky_mat = new Indigo::SunSkyMaterial();
				const float sun_phi = 1.f; // See MainWindow.cpp
				const float sun_theta = Maths::pi<float>() / 4;
				sun_sky_mat->sundir = normalise(Indigo::Vec3d(std::cos(sun_phi) * std::sin(sun_theta), std::sin(sun_phi) * std::sin(sun_theta), std::cos(sun_theta)));
				sun_sky_mat->model = "captured-simulation";
				Indigo::SceneNodeBackgroundSettingsRef background_node = new Indigo::SceneNodeBackgroundSettings(sun_sky_mat);
				root_node->addChildNode(background_node);

				root_node->finalise(".");

				scene_path = PlatformUtils::getAppDataDirectory("Cyberspace") + "/lightmap_baking.igs";
		
				// Write Indigo scene to disk.
				root_node->writeToXMLFileOnDisk(
					toIndigoString(scene_path),
					false, // write_absolute_dependency_paths
					NULL // progress_listener
				);

				conPrint("Wrote scene to '" + scene_path + "'.");
			
			} // Release world state lock


			const std::string lightmap_exr_path = PlatformUtils::getAppDataDirectory("Cyberspace") + "/lightmaps/ob_" + ob_uid.toString() + "_lightmap.exr";
			FileUtils::createDirIfDoesNotExist(PlatformUtils::getAppDataDirectory("Cyberspace") + "/lightmaps");
			int lightmap_index = 0;
			if(true)
			{
				//const double halt_time = do_high_qual_bake ? 300.0 : 10;
				//conPrint("Using halt time of " + toString(halt_time) + " s");

				const double halt_spp = do_high_qual_bake ? 2048 : 256;

				conPrint("Using halt samples/px of " + toString(halt_spp));

				Timer indigo_exec_timer;

				const std::string indigo_exe_path = "C:\\programming\\indigo\\output\\vs2019\\indigo_x64\\RelWithDebInfo\\indigo_gui.exe";
				std::vector<std::string> command_line_args;
				command_line_args.push_back(indigo_exe_path);
				command_line_args.push_back(scene_path);
				command_line_args.push_back("--noninteractive");
				command_line_args.push_back("-uexro"); // untonemapped EXR output path:
				command_line_args.push_back(lightmap_exr_path);
				//command_line_args.push_back("-halt"); // Half after N secs
				//command_line_args.push_back(toString(halt_time));
				command_line_args.push_back("-haltspp"); // Half after N samples/pixel
				command_line_args.push_back(toString(halt_spp));
				glare::Process indigo_process(indigo_exe_path, command_line_args);

				Timer timer;
				while(1)
				{
					while(indigo_process.isStdOutReadable())
					{
						const std::string output = indigo_process.readStdOut();
						std::vector<std::string> lines = ::split(output, '\n');
						for(size_t i=0; i<lines.size(); ++i)
							if(!isAllWhitespace(lines[i]))
								conPrint("INDIGO> " + lines[i]);

						// Upload of progressive renders of the lightmap:
						//for(size_t i=0; i<lines.size(); ++i)
						//	if(hasPrefix(lines[i], "Saving untone-mapped EXR to"))
						//	{
						//		compressAndUploadLightmap(lightmap_exr_path, ob_uid, lightmap_index);
						//	}
					}

					// Check to see if the object has been modified, and the lightmap baking needs to be re-started:
					if(lightmap_index >= 1)
					{
						Lock lock(world_state.mutex);
						auto res = world_state.objects.find(ob_uid);
						if(res != world_state.objects.end())
						{
							WorldObjectRef ob2 = res->second;
							if(BitUtils::isBitSet(ob2->flags, WorldObject::LIGHTMAP_NEEDS_COMPUTING_FLAG) || BitUtils::isBitSet(ob2->flags, WorldObject::HIGH_QUAL_LIGHTMAP_NEEDS_COMPUTING_FLAG))
							{
								conPrint("Object has been modified since bake started, aborting bake...");
								indigo_process.terminateProcess();
								return;
							}
						}
					}

					if(!indigo_process.isProcessAlive())
						break;

					PlatformUtils::Sleep(10);
				}

				conPrint("Indigo exection took " + indigo_exec_timer.elapsedStringNSigFigs(3) + " for " + toString(halt_spp) + " spp");

				compressAndUploadLightmap(lightmap_exr_path, ob_uid, lightmap_index);

				std::string output, err_output;
				indigo_process.readAllRemainingStdOutAndStdErr(output, err_output);
				conPrint("INDIGO> " + output);
				conPrint("INDIGO> " + err_output);

				conPrint("Indigo process terminated.");
			}
		}
		catch(PlatformUtils::PlatformUtilsExcep& e)
		{
			throw glare::Exception(e.what());
		}
	}


	void compressAndUploadLightmap(const std::string& lightmap_exr_path, UID ob_uid, int& lightmap_index)
	{
		uint64 lightmap_base_hash = 0;

		//TEMP: test saving EXR in DWAB format to see its size.
		/*{
			Reference<Map2D> map = EXRDecoder::decode(lightmap_exr_path); // Load texture from disk and decode it.
			Reference<ImageMapFloat> image_map_float = map.downcast<ImageMapFloat>();
			EXRDecoder::SaveOptions options;
			options.compression_method = EXRDecoder::CompressionMethod_DWAB;
			EXRDecoder::saveImageToEXR(*image_map_float, ::removeDotAndExtension(lightmap_exr_path) + "_DWAB.exr", "", options);
		}*/

		for(int lvl = 0; lvl <= 2; ++lvl)
		{
			const std::string lod_suffix = (lvl == 0) ? "" : ("_lod" + toString(lvl));

			const std::string                 lightmap_ktx_path = ::removeDotAndExtension(lightmap_exr_path) + "_" + toString(lightmap_index) + lod_suffix + ".ktx2";
			const std::string supercompressed_lightmap_ktx_path = ::removeDotAndExtension(lightmap_exr_path) + "_" + toString(lightmap_index) + lod_suffix + "_su.ktx2";  // supercompressed
			
			// Resize image if we are doing LOD level 1 or 2.
			const std::string resized_exr_path = ::removeDotAndExtension(lightmap_exr_path) + lod_suffix + ".exr";
			if(lvl == 1)
			{
				Reference<Map2D> map = EXRDecoder::decode(lightmap_exr_path); // Load texture from disk and decode it.

				const int new_w = (int)map->getMapWidth()  / 4;
				const int new_h = (int)map->getMapHeight() / 4;
				
				Reference<Map2D> resized_map = map->resizeMidQuality(new_w, new_h, task_manager);

				Reference<ImageMapFloat> image_map_float = resized_map.downcast<ImageMapFloat>();
				EXRDecoder::saveImageToEXR(*image_map_float, resized_exr_path, "", EXRDecoder::SaveOptions());
			}
			else if(lvl == 2)
			{
				Reference<Map2D> map = EXRDecoder::decode(lightmap_exr_path); // Load texture from disk and decode it.

				const int new_w = (int)map->getMapWidth()  / 16;
				const int new_h = (int)map->getMapHeight() / 16;

				Reference<Map2D> resized_map = map->resizeMidQuality(new_w, new_h, task_manager);

				Reference<ImageMapFloat> image_map_float = resized_map.downcast<ImageMapFloat>();
				EXRDecoder::saveImageToEXR(*image_map_float, resized_exr_path, "", EXRDecoder::SaveOptions());
			}

			//================== Run Compressonator to compress the lightmap EXR with BC6 compression into a KTX file. ========================
			{
				const std::string compressonator_path = PlatformUtils::findProgramOnPath("CompressonatorCLI.exe");
				std::vector<std::string> command_line_args;
				command_line_args.push_back(compressonator_path);
				command_line_args.push_back("-fd"); // Specifies the destination texture format to use
				command_line_args.push_back("BC6H"); // BC6H = High-Dynamic Range compression format (https://docs.microsoft.com/en-us/windows/win32/direct3d11/bc6h-format)
				command_line_args.push_back("-mipsize");
				command_line_args.push_back("1");
				command_line_args.push_back(resized_exr_path); // input path
				command_line_args.push_back(lightmap_ktx_path); // output path
				glare::Process compressonator_process(compressonator_path, command_line_args);

				Timer timer;
				while(1)
				{
					while(compressonator_process.isStdOutReadable())
					{
						const std::string output = compressonator_process.readStdOut();
						std::vector<std::string> lines = ::split(output, '\n');
						for(size_t i=0; i<lines.size(); ++i)
						{
							//conPrint("COMPRESS> " + lines[i]);
						}
					}

					if(!compressonator_process.isProcessAlive())
						break;

					PlatformUtils::Sleep(1);
				}

				std::string output, err_output;
				compressonator_process.readAllRemainingStdOutAndStdErr(output, err_output);
				//conPrint("COMPRESS> " + output);
				if(!isAllWhitespace(err_output))
					conPrint("COMPRESS error output> " + err_output);

				if(compressonator_process.getExitCode() != 0)
					throw glare::Exception("compressonator execution returned a non-zero code: " + toString(compressonator_process.getExitCode()));

				//conPrint("Compressonator finished.");
			}

			// Supercompress ktx file - apply ZStd compression to it.
			KTXDecoder::supercompressKTX2File(lightmap_ktx_path, supercompressed_lightmap_ktx_path);

			// Compute hash over lightmap
			const uint64 lightmap_hash = FileChecksum::fileChecksum(supercompressed_lightmap_ktx_path);
			if(lvl == 0)
				lightmap_base_hash = lightmap_hash;

			//const std::string base_lightmap_URL = ResourceManager::URLForNameAndExtensionAndHash(eatExtension(lightmap_exr_path), "ktx2", lightmap_hash);
			const std::string lightmap_URL = ::removeDotAndExtension(FileUtils::getFilename(lightmap_exr_path)) + "_" + toString(lightmap_base_hash) + lod_suffix + ".ktx2";

			// Enqueue ObjectLightmapURLChanged (just for level 0 tho)
			if(lvl == 0)
			{
				SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
				packet.writeUInt32(Protocol::ObjectLightmapURLChanged);
				writeToStream(ob_uid, packet);
				packet.writeStringLengthFirst(lightmap_URL);

				this->client_thread->enqueueDataToSend(packet);
			}

			// Spawn an UploadResourceThread to upload the new lightmap
			conPrint("Uploading lightmap '" + supercompressed_lightmap_ktx_path + "' to the server with URL '" + lightmap_URL + "'...");
			resource_upload_thread_manager.addThread(new UploadResourceThread(&this->msg_queue, supercompressed_lightmap_ktx_path, lightmap_URL, server_hostname, server_port, 
				username, password, client_tls_config, &num_resources_uploading));
		}

		lightmap_index++;
	}


	void doLightMapping(WorldState& world_state, Reference<ClientThread>& client_thread_, ThreadSafeQueue<Reference<ThreadMessage> >& external_msg_queue)
	{
		conPrint("---------------doLightMapping()-----------------");
		this->client_thread = client_thread_;

		try
		{
			//============= Do an initial scan over all objects, to see if any of them need lightmapping ===========
			conPrint("Doing initial scan over all objects...");
			std::set<WorldObjectRef> obs_to_lightmap;
			{
				Lock lock(world_state.mutex);

				for(auto it = world_state.objects.begin(); it != world_state.objects.end(); ++it)
				{
					WorldObject* ob = it->second.ptr();
					// conPrint("Checking object with UID " + ob->uid.toString());
					if(/*!ob->model_url.empty() && */BitUtils::isBitSet(ob->flags, WorldObject::LIGHTMAP_NEEDS_COMPUTING_FLAG) || BitUtils::isBitSet(ob->flags, WorldObject::HIGH_QUAL_LIGHTMAP_NEEDS_COMPUTING_FLAG))
					{
						// Decompress voxel group
						ob->decompressVoxels();

						obs_to_lightmap.insert(ob);
					}
				}
			}

			// Now that we have released the world_state.mutex lock, build lightmaps
			for(auto it = obs_to_lightmap.begin(); it != obs_to_lightmap.end(); ++it)
			{
				try
				{
					buildLightMapForOb(world_state, it->ptr());
				}
				catch(glare::Exception& e)
				{
					conPrint("Error while building lightmap for object: " + e.what());
				}
			}
			obs_to_lightmap.clear();


			conPrint("Done initial scan over all objects.");

			//============= Now loop and wait for any objects to be marked dirty, and check those objects for if they need lightmapping ===========
			while(1)
			{
				//conPrint("Checking dirty set...");
				{
					Lock lock(world_state.mutex);

					for(auto it = world_state.dirty_from_remote_objects.begin(); it != world_state.dirty_from_remote_objects.end(); ++it)
					{
						WorldObject* ob = it->ptr();
						//conPrint("Found object with UID " + ob->uid.toString() + " in dirty set.");
						//conPrint("LIGHTMAP_NEEDS_COMPUTING_FLAG: " + boolToString(BitUtils::isBitSet(ob->flags, WorldObject::LIGHTMAP_NEEDS_COMPUTING_FLAG)));

						if(/*!ob->model_url.empty() && */BitUtils::isBitSet(ob->flags, WorldObject::LIGHTMAP_NEEDS_COMPUTING_FLAG) || BitUtils::isBitSet(ob->flags, WorldObject::HIGH_QUAL_LIGHTMAP_NEEDS_COMPUTING_FLAG))
						{
							// Decompress voxel group
							ob->decompressVoxels();

							obs_to_lightmap.insert(ob);
						}
					}

					world_state.dirty_from_remote_objects.clear();
				}

				// Now that we have released the world_state.mutex lock, build lightmaps
				for(auto it = obs_to_lightmap.begin(); it != obs_to_lightmap.end(); ++it)
				{
					try
					{
						buildLightMapForOb(world_state, it->ptr());
					}
					catch(glare::Exception& e)
					{
						conPrint("Error while building lightmap for object: " + e.what());
					}
				}
				obs_to_lightmap.clear();


				//TEMP HACK: just lightmap an object
			/*	{
					auto res = world_state.objects.find(UID(147435));
					assert(res != world_state.objects.end());
					buildLightMapForOb(world_state, res->second.ptr());
				}*/

				if(checkForDisconnect(external_msg_queue))
					throw glare::Exception("client thread disconnected.");

				PlatformUtils::Sleep(100);
			}
		}
		catch(glare::Exception& e)
		{
			conPrint("Error: " + e.what());
		}
	}


	std::string server_hostname;
	int server_port;

	glare::TaskManager task_manager;

	ResourceManagerRef& resource_manager;

	ThreadManager resource_download_thread_manager;
	ThreadManager net_resource_download_thread_manager;
	ThreadManager resource_upload_thread_manager;

	ThreadSafeQueue<Reference<ThreadMessage> > msg_queue;

	glare::AtomicInt num_non_net_resources_downloading;
	glare::AtomicInt num_net_resources_downloading;
	glare::AtomicInt num_resources_uploading;

	Reference<ClientThread> client_thread;

	struct tls_config* client_tls_config;

	std::string base_dir_path;
};


int main(int argc, char* argv[])
{
	Clock::init();
	Networking::createInstance();
	PlatformUtils::ignoreUnixSignals();
	OpenSSL::init();
	TLSSocket::initTLS();

	ThreadSafeQueue<Reference<ThreadMessage> > msg_queue;

	Reference<WorldState> world_state = new WorldState();

	//const std::string server_hostname = "localhost";
	const std::string server_hostname = "substrata.info";
	const int server_port = 7600;


	// Create and init TLS client config
	struct tls_config* client_tls_config = tls_config_new();
	if(!client_tls_config)
		throw glare::Exception("Failed to initialise TLS (tls_config_new failed)");
	tls_config_insecure_noverifycert(client_tls_config); // TODO: Fix this, check cert etc..
	tls_config_insecure_noverifyname(client_tls_config);


	while(1) // While lightmapper bot should keep running:
	{
		// Connect to substrata server
		try
		{
			// Reset msg queue (get rid of any disconnected msgs)
			msg_queue.clear();

			Reference<ClientThread> client_thread = new ClientThread(
				&msg_queue,
				server_hostname,
				server_port, // port
				"", // avatar URL
				"", // world name - default world
				client_tls_config
			);
			client_thread->world_state = world_state;

			ThreadManager client_thread_manager;
			client_thread_manager.addThread(client_thread);

			const std::string appdata_path = PlatformUtils::getOrCreateAppDataDirectory("Cyberspace");
			const std::string resources_dir = appdata_path + "/resources";
			conPrint("resources_dir: " + resources_dir);
			Reference<ResourceManager> resource_manager = new ResourceManager(resources_dir);


			// Make LogInMessage packet and enqueue to send
			{
				SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
				packet.writeUInt32(Protocol::LogInMessage);
				packet.writeStringLengthFirst(username);
				packet.writeStringLengthFirst(password);

				client_thread->enqueueDataToSend(packet);
			}

			// Send GetAllObjects msg
			{
				SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
				packet.writeUInt32(Protocol::GetAllObjects);
				client_thread->enqueueDataToSend(packet);
			}

			// Wait until we have received all object data.
			conPrint("Waiting for initial data to be received");
			while(!client_thread->all_objects_received)
			{
				if(checkForDisconnect(msg_queue))
					throw glare::Exception("client thread disconnected.");

				PlatformUtils::Sleep(100);
				conPrintStr(".");
			}

			conPrint("Received objects.  world_state->objects.size(): " + toString(world_state->objects.size()));

			conPrint("===================== Running LightMapperBot =====================");

			LightMapperBot bot(server_hostname, server_port, resource_manager, client_tls_config);
			bot.base_dir_path = PlatformUtils::getResourceDirectoryPath();
			bot.doLightMapping(*world_state, client_thread, msg_queue);

			conPrint("===================== Done Running LightMapperBot. =====================");
		}
		catch(glare::Exception& e)
		{
			// Connection failed.
			conPrint("Error: " + e.what());
			PlatformUtils::Sleep(5000);
		}
	}
	return 0;
}
