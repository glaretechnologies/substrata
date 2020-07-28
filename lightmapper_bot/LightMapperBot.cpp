/*=====================================================================
LightMapperBot.h
----------------
Copyright Glare Technologies Limited 2020 -
=====================================================================*/


#include "../shared/Protocol.h"
#include "../shared/ResourceManager.h"
#include "../gui_client/ClientThread.h"
#include "../gui_client/DownloadResourcesThread.h"
#include "../gui_client/NetDownloadResourcesThread.h"
#include "../gui_client/UploadResourceThread.h"
#include <networking/networking.h>
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
#include <networking/url.h>
#define USE_INDIGO_SDK 1
// Indigo SDK headers:
#if USE_INDIGO_SDK
#include <dll/include/IndigoMesh.h>
#include <dll/include/IndigoMaterial.h>
#include <dll/include/SceneNodeModel.h>
#include <dll/include/SceneNodeRenderSettings.h>
#include <dll/include/SceneNodeRoot.h>
#endif
#include <simpleraytracer/raymesh.h>
#include <graphics/BatchedMesh.h>
#include <indigo/UVUnwrapper.h>


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


class LightMapperBot
{
public:

	LightMapperBot(const std::string& server_hostname_, int server_port_, ResourceManagerRef& resource_manager_)
	:	server_hostname(server_hostname_), server_port(server_port_), resource_manager(resource_manager_)
	{
		resource_download_thread_manager.addThread(new DownloadResourcesThread(&msg_queue, resource_manager, server_hostname, server_port, &this->num_non_net_resources_downloading));

		for(int i=0; i<4; ++i)
			net_resource_download_thread_manager.addThread(new NetDownloadResourcesThread(&msg_queue, resource_manager, &num_net_resources_downloading));
	}



	void startDownloadingResource(const std::string& url)
	{
		//conPrint("-------------------MainWindow::startDownloadingResource()-------------------\nURL: " + url);

		ResourceRef resource = resource_manager->getResourceForURL(url);
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
		catch(Indigo::Exception& e)
		{
			conPrint("Failed to parse URL '" + url + "': " + e.what());
		}
	}


	// For every resource that the object uses (model, textures etc..), if the resource is not present locally, start downloading it.
	void startDownloadingResourcesForObject(WorldObject* ob)
	{
		std::set<std::string> dependency_URLs;
		ob->getDependencyURLSet(dependency_URLs);
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
		ob->getDependencyURLSet(dependency_URLs);
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
			throw Indigo::Exception("Too many UV sets: " + toString(mesh.num_uv_mappings) + ", max is " + toString(10));

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
					throw Indigo::Exception("Triangle vertex index is out of bounds.  (vertex index=" + toString(mesh.triangles[i].vertex_indices[v]) + ", num verts: " + toString(num_verts) + ")");

			// Check uv indices are in bounds
			if(mesh.num_uv_mappings > 0)
				for(unsigned int v = 0; v < 3; ++v)
					if(src_tri.uv_indices[v] >= num_uv_groups)
						throw Indigo::Exception("Triangle uv index is out of bounds.  (uv index=" + toString(mesh.triangles[i].uv_indices[v]) + ")");
		}

		// Quads
		for(size_t i = 0; i < mesh.quads.size(); ++i)
		{
			// Check vertex indices are in bounds
			for(unsigned int v = 0; v < 4; ++v)
				if(mesh.quads[i].vertex_indices[v] >= num_verts)
					throw Indigo::Exception("Quad vertex index is out of bounds.  (vertex index=" + toString(mesh.quads[i].vertex_indices[v]) + ")");

			// Check uv indices are in bounds
			if(mesh.num_uv_mappings > 0)
				for(unsigned int v = 0; v < 4; ++v)
					if(mesh.quads[i].uv_indices[v] >= num_uv_groups)
						throw Indigo::Exception("Quad uv index is out of bounds.  (uv index=" + toString(mesh.quads[i].uv_indices[v]) + ")");
		}
	}


	void buildLightMapForOb(WorldObject* ob)
	{
		//if(BitUtils::isBitSet(ob->flags, WorldObject::LIGHTMAP_NEEDS_COMPUTING_FLAG))
		{
			// Start downloading any resources we don't have that the object uses.
			startDownloadingResourcesForObject(ob);
		}

		// Wait until we have downloaded all resources for the object

		Timer wait_timer;
		while(1)
		{
			if(allResourcesPresentForOb(ob))
				break;

			PlatformUtils::Sleep(50);

			if(wait_timer.elapsed() > 30)
				throw Indigo::Exception("Failed to download all resources for object with UID " + ob->uid.toString());
		}


		// Load mesh from disk:
		// If batched mesh (bmesh), convert to indigo mesh
		// If indigo mesh, can use directly
		const std::string model_path = resource_manager->pathForURL(ob->model_url);

		Indigo::MeshRef indigo_mesh;
		if(hasExtension(model_path, "igmesh"))
		{
			try
			{
				Indigo::Mesh::readFromFile(toIndigoString(model_path), *indigo_mesh);
			}
			catch(Indigo::IndigoException& e)
			{
				throw Indigo::Exception(toStdString(e.what()));
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
			throw Indigo::Exception("unhandled model format: " + model_path);

		checkValidAndSanitiseMesh(*indigo_mesh); // Throws Indigo::Exception on invalid mesh.

		// If voxel object, convert to mesh

		// See if this object has a lightmap-suitable UV map already
		const bool has_lightmap_uvs = indigo_mesh->num_uv_mappings >= 2; // TEMP
		if(!has_lightmap_uvs)
		{
			// Generate lightmap UVs
			StandardPrintOutput print_output;
			UVUnwrapper::build(*indigo_mesh, print_output); // Adds UV set to indigo_mesh.

			// Convert indigo_mesh to a BatchedMesh.
			// This will also merge verts with the same pos and normal.
			BatchedMeshRef batched_mesh = new BatchedMesh();
			batched_mesh->buildFromIndigoMesh(*indigo_mesh);


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

			ob->model_url = mesh_URL;

			// Send the updated object, with the new model URL, to the server.

			// Enqueue ObjectFullUpdate
			SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
			packet.writeUInt32(Protocol::ObjectFullUpdate);
			writeToNetworkStream(*ob, packet);

			this->client_thread->enqueueDataToSend(packet);

			// Spawn an UploadResourceThread to upload the new model
			resource_upload_thread_manager.addThread(new UploadResourceThread(&this->msg_queue, this->resource_manager->pathForURL(mesh_URL), mesh_URL, server_hostname, server_port, username, password));
		}



		//------------------ Make an Indigo scene graph to light the model, then save it to disk ---------------------

		Indigo::SceneNodeRootRef root_node = new Indigo::SceneNodeRoot();

		Indigo::SceneNodeMeshRef mesh_node = new Indigo::SceneNodeMesh(indigo_mesh);

		// Make Indigo materials from loaded parcel mats
		Indigo::Vector<Indigo::SceneNodeMaterialRef> indigo_mat_nodes;
		for(size_t i=0; i<ob->materials.size(); ++i)
		{
			const WorldMaterialRef parcel_mat = ob->materials[i];

			Reference<Indigo::WavelengthDependentParam> albedo_param;
			if(!parcel_mat->colour_texture_url.empty())
			{
				const std::string path = resource_manager->pathForURL(parcel_mat->colour_texture_url);
				albedo_param = new Indigo::TextureWavelengthDependentParam(Indigo::Texture(toIndigoString(path)), new Indigo::RGBSpectrum(Indigo::Vec3d(1.0), /*gamma=*/2.2));
			}
			else
			{
				albedo_param = new Indigo::ConstantWavelengthDependentParam(new Indigo::RGBSpectrum(Indigo::Vec3d(parcel_mat->colour_rgb.r, parcel_mat->colour_rgb.g, parcel_mat->colour_rgb.b), /*gamma=*/2.2));
			}

			Indigo::DiffuseMaterialRef indigo_mat = new Indigo::DiffuseMaterial(
				albedo_param
			);
			indigo_mat->name = toIndigoString(parcel_mat->name);

			indigo_mat_nodes.push_back(new Indigo::SceneNodeMaterial(indigo_mat));
		}

		Indigo::SceneNodeModelRef model_node = new Indigo::SceneNodeModel();
		model_node->setMaterials(indigo_mat_nodes);
		model_node->setGeometry(mesh_node);
		model_node->keyframes.push_back(Indigo::KeyFrame(0.0, Indigo::Vec3d(0, 0, 0), Indigo::AxisAngle::identity()));
		model_node->rotation = new Indigo::MatrixRotation();
		root_node->addChildNode(model_node);

		Indigo::SceneNodeRenderSettingsRef settings_node = Indigo::SceneNodeRenderSettings::getDefaults();
		settings_node->width.setValue(1024);
		settings_node->height.setValue(1024);
		settings_node->bidirectional.setValue(false);
		settings_node->metropolis.setValue(false);
		settings_node->light_map_baking_ob_uid.setValue(model_node->getUniqueID().value()); // Enable light map baking
		settings_node->generate_lightmap_uvs.setValue(false);
		settings_node->capture_direct_sun_illum.setValue(false);
		//settings_node->image_save_period.setValue(5);
		settings_node->merging.setValue(false); // Needed for now
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
		sun_sky_mat->sundir = normalise(Indigo::Vec3d(std::cos(sun_phi) * std::sin(sun_theta), std::sin(sun_phi) * sun_theta, std::cos(sun_theta)));
		sun_sky_mat->model = "captured-simulation";
		Indigo::SceneNodeBackgroundSettingsRef background_node = new Indigo::SceneNodeBackgroundSettings(sun_sky_mat);
		root_node->addChildNode(background_node);

		root_node->finalise(".");

		const std::string scene_path = PlatformUtils::getAppDataDirectory("Cyberspace") + "/cv_baking.igs";
		
		// Write Indigo scene to disk.
		root_node->writeToXMLFileOnDisk(
			toIndigoString(scene_path),
			false, // write_absolute_dependency_paths
			NULL // progress_listener
		);

		conPrint("Wrote scene to cv_baking.igs");

		const std::string lightmap_path = PlatformUtils::getAppDataDirectory("Cyberspace") + "/lightmap.exr";

		const std::string indigo_exe_path = "C:\\programming\\indigo\\output\\vs2019\\indigo_x64\\RelWithDebInfo\\indigo_gui.exe";
		std::vector<std::string> command_line_args;
		command_line_args.push_back(indigo_exe_path);
		command_line_args.push_back(scene_path);
		command_line_args.push_back("--noninteractive");
		//command_line_args.push_back("-o");
		command_line_args.push_back("-uexro");
		command_line_args.push_back(lightmap_path);
		command_line_args.push_back("-halt");
		command_line_args.push_back("200");
		Process indigo_process(indigo_exe_path, command_line_args);

		Timer timer;
		while(1)
		{
			while(indigo_process.isStdOutReadable())
			{
				const std::string output = indigo_process.readStdOut();
				std::vector<std::string> lines = ::split(output, '\n');
				for(size_t i=0; i<lines.size(); ++i)
					conPrint("INDIGO> " + lines[i]);
			}

			if(!indigo_process.isProcessAlive())
				break;

			PlatformUtils::Sleep(1);
		}

		std::string output, err_output;
		indigo_process.readAllRemainingStdOutAndStdErr(output, err_output);
		conPrint("INDIGO> " + output);
		conPrint("INDIGO> " + err_output);

		conPrint("Indigo process terminated.");

		
		// Compute hash over lightmap
		const uint64 lightmap_hash = FileChecksum::fileChecksum(lightmap_path);

		const std::string lightmap_URL = ResourceManager::URLForNameAndExtensionAndHash("lightmap", ::getExtension(lightmap_path), lightmap_hash);

		// Copy model to local resources dir.  UploadResourceThread will read from here.
		this->resource_manager->copyLocalFileToResourceDir(lightmap_path, lightmap_URL);

		ob->lightmap_url = lightmap_URL;

		// Enqueue ObjectFullUpdate
		SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
		packet.writeUInt32(Protocol::ObjectFullUpdate);
		writeToNetworkStream(*ob, packet);

		this->client_thread->enqueueDataToSend(packet);

		// Spawn an UploadResourceThread to upload the new lightmap
		resource_upload_thread_manager.addThread(new UploadResourceThread(&this->msg_queue, this->resource_manager->pathForURL(lightmap_URL), lightmap_URL, server_hostname, server_port, username, password));
	}


	void doLightMapping(WorldState& world_state, Reference<ClientThread>& client_thread_)
	{
		conPrint("---------------doLightMapping()-----------------");
		this->client_thread = client_thread_;

		try
		{
			{
				Lock lock(world_state.mutex);

				for(auto it = world_state.objects.begin(); it != world_state.objects.end(); ++it)
				{
					WorldObject* ob = it->second.ptr();
					if(!ob->model_url.empty())
						buildLightMapForOb(ob);
				}
			}

			// Wait a while until ObjectFullUpdate msgs have been sent.  TODO: do properly.
			PlatformUtils::Sleep(5000);
		}
		catch(Indigo::Exception& e)
		{
			conPrint("Error: " + e.what());
		}
	}


	std::string server_hostname;
	int server_port;

	Indigo::TaskManager task_manager;

	ResourceManagerRef& resource_manager;

	ThreadManager resource_download_thread_manager;
	ThreadManager net_resource_download_thread_manager;
	ThreadManager resource_upload_thread_manager;

	ThreadSafeQueue<Reference<ThreadMessage> > msg_queue;

	IndigoAtomic num_non_net_resources_downloading;
	IndigoAtomic num_net_resources_downloading;

	Reference<ClientThread> client_thread;
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

	const std::string server_hostname = "localhost"; // "substrata.info"
	const int server_port = 7600;

	Reference<ClientThread> client_thread = new ClientThread(
		&msg_queue,
		server_hostname,
		server_port, // port
		"sdfsdf", // avatar URL
		"" // world name - default world
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

	// Wait until we have received parcel data.  This means we have received all objects
	conPrint("Waiting for initial data to be received");
	while(!client_thread->initial_state_received)
	{
		PlatformUtils::Sleep(10);
		conPrintStr(".");
	}

	conPrint("Received objects.  world_state->objects.size(): " + toString(world_state->objects.size()));

	conPrint("===================== Running LightMapperBot =====================");

	LightMapperBot bot(server_hostname, server_port, resource_manager);
	bot.doLightMapping(*world_state, client_thread);

	conPrint("===================== Done Running LightMapperBot. =====================");

	return 0;
}
