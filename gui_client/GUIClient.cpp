/*=====================================================================
GUIClient.cpp
-------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/


#include "GUIClient.h"
#include <settings/SettingsStore.h>
#include "ClientThread.h"
#include "ModelLoading.h"
#include "MeshBuilding.h"
#include "ThreadMessages.h"
#include "TerrainSystem.h"
#include "TerrainDecalManager.h"
#include "LoadScriptTask.h"
#include "UploadResourceThread.h"
#include "DownloadResourcesThread.h"
#include "NetDownloadResourcesThread.h"
#include "ObjectPathController.h"
#include "AvatarGraphics.h"
#include "WinterShaderEvaluator.h"
#include "ClientUDPHandlerThread.h"
#include "URLWhitelist.h"
#include "URLParser.h"
#include "LoadModelTask.h"
#include "BuildScatteringInfoTask.h"
#include "LoadTextureTask.h"
#include "LoadAudioTask.h"
#include "../audio/MicReadThread.h"
#include "MakeHypercardTextureTask.h"
#include "SaveResourcesDBThread.h"
#include "GarbageDeleterThread.h"
#include "BiomeManager.h"
#include "WebViewData.h"
#include "BrowserVidPlayer.h"
#include "AnimatedTextureManager.h"
#include "ParticleManager.h"
#include "Scripting.h"
#include "HoverCarPhysics.h"
#include "BikePhysics.h"
#include "CarPhysics.h"
#include "BoatPhysics.h"
#include "JoltUtils.h"
#include "MiniMap.h"
#if !defined(EMSCRIPTEN)
#include "../networking/TLSSocket.h"
#endif
#include "../shared/Protocol.h"
#include "../shared/LODGeneration.h"
#include "../shared/ImageDecoding.h"
#include "../shared/MessageUtils.h"
#include "../shared/FileTypes.h"
#include "../shared/LuaScriptEvaluator.h"
#include "../shared/SubstrataLuaVM.h"
#include "../shared/ObjectEventHandlers.h"
#include "../shared/WorldStateLock.h"
#include "../server/User.h"
#include "../shared/WorldSettings.h"
#include "../maths/Quat.h"
#include "../maths/GeometrySampling.h"
#include "../utils/Clock.h"
#include "../utils/Timer.h"
#include "../utils/PlatformUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/Exception.h"
#include "../utils/TaskManager.h"
#include "../utils/SocketBufferOutStream.h"
#include "../utils/StringUtils.h"
#include "../utils/FileUtils.h"
#include "../utils/FileChecksum.h"
#include "../utils/Parser.h"
#include "../utils/OpenSSL.h"
#include "../utils/CryptoRNG.h"
#include "../utils/FileInStream.h"
#include "../utils/IncludeXXHash.h"
#include "../utils/IndigoXMLDoc.h"
#include "../utils/FastPoolAllocator.h"
#include "../utils/RuntimeCheck.h"
#include <utils/IncludeHalf.h>
#include "../utils/MemAlloc.h"
#include "../utils/UTF8Utils.h"
#include "../networking/Networking.h"
#include "../networking/URL.h"
#include "../graphics/ImageMap.h"
#include "../graphics/SRGBUtils.h"
#include "../graphics/BasisDecoder.h"
#include "../dll/include/IndigoMesh.h"
#include "../indigo/TextureServer.h"
#include <opengl/OpenGLShader.h>
#include <opengl/MeshPrimitiveBuilding.h>
#include <opengl/OpenGLUploadThread.h>
#include <opengl/PBOPool.h>
#include <opengl/VBOPool.h>
#include <opengl/OpenGLMeshRenderData.h>
#include <opengl/SSAODebugging.h>
#include "../audio/AudioFileReader.h"
#include <Escaping.h>
#if !defined(EMSCRIPTEN)
#include <VirtualMachine.h>
#include <tls.h>
#endif
#include <Jolt/Physics/PhysicsSystem.h>
#include <tracy/Tracy.hpp>
#include "superluminal/PerformanceAPI.h"
#if BUGSPLAT_SUPPORT
#include <BugSplat.h>
#endif
#include <clocale>
#if defined(EMSCRIPTEN)
#include <emscripten/emscripten.h>
#include <unistd.h>
#include <malloc.h>
#endif
#include <zstd.h>


static const double ground_quad_w = 2000.f; // TEMP was 1000, 2000 is for CV rendering
static const float ob_load_distance = 2000.f;
// See also  // TEMP HACK: set a smaller max loading distance for CV features in ClientThread.cpp

std::vector<AvatarRef> test_avatars;
std::vector<double> test_avatar_phases;


static const Colour4f DEFAULT_OUTLINE_COLOUR   = Colour4f::fromHTMLHexString("0ff7fb"); // light blue
static const Colour4f PICKED_UP_OUTLINE_COLOUR = Colour4f::fromHTMLHexString("69fa2d"); // light green
static const Colour4f PARCEL_OUTLINE_COLOUR    = Colour4f::fromHTMLHexString("f09a13"); // orange

static const Colour3f axis_arrows_default_cols[]   = { Colour3f(0.6f,0.2f,0.2f), Colour3f(0.2f,0.6f,0.2f), Colour3f(0.2f,0.2f,0.6f) };
static const Colour3f axis_arrows_mouseover_cols[] = { Colour3f(1,0.45f,0.3f),   Colour3f(0.3f,1,0.3f),    Colour3f(0.3f,0.45f,1) };

static const float DECAL_EDGE_AABB_WIDTH = 0.02f;

static const bool LOD_CHUNK_SUPPORT = true;
static const float chunk_w = 128.f;
static const float recip_chunk_w = 1.f / chunk_w;

static const URLString DEFAULT_AVATAR_MODEL_URL = "xbot.bmesh"; // This file should be in the resources directory in the distribution.


GUIClient::GUIClient(const std::string& base_dir_path_, const std::string& appdata_path_, const ArgumentParser& args)
:	base_dir_path(base_dir_path_),
	appdata_path(appdata_path_),
	parsed_args(args),
	connection_state(ServerConnectionState_NotConnected),
	logged_in_user_id(UserID::invalidUserID()),
	logged_in_user_flags(0),
	shown_object_modification_error_msg(false),
	num_frames_since_fps_timer_reset(0),
	last_fps(0),
	voxel_edit_marker_in_engine(false),
	voxel_edit_face_marker_in_engine(false),
	selected_ob_picked_up(false),
	process_model_loaded_next(true),
	proximity_loader(/*load distance=*/ob_load_distance),
	load_distance(ob_load_distance),
	load_distance2(ob_load_distance*ob_load_distance),
	client_tls_config(NULL),
	last_foostep_side(0),
	last_animated_tex_time(0),
	last_model_and_tex_loading_time(0),
	grabbed_axis(-1),
	grabbed_angle(0),
	force_new_undo_edit(false),
#if EMSCRIPTEN
	model_and_texture_loader_task_manager("model and texture loader task manager", /*num threads=*/myClamp<uint32>(PlatformUtils::getNumLogicalProcessors() / 2, 1, 8)),
#else
	model_and_texture_loader_task_manager("model and texture loader task manager", /*num threads=*/myMax<size_t>(PlatformUtils::getNumLogicalProcessors() / 2, 1)),
#endif
	//task_manager(NULL), // Currently just used for LODGeneration::generateLODTexturesForMaterialsIfNotPresent().
	url_parcel_uid(-1),
	running_destructor(false),
	biome_manager(NULL),
	scratch_packet(SocketBufferOutStream::DontUseNetworkByteOrder),
	frame_num(0),
	next_lod_changes_begin_i(0),
	axis_and_rot_obs_enabled(false),
	last_vehicle_renewal_msg_time(-1),
	stack_allocator(/*size (B)=*/22 * 1024 * 1024), // Used for the Jolt physics temp allocator also.
	arena_allocator(/*size (B)=*/4 * 1024 * 1024), // Used for WorldObject::appendDependencyURLs() etc.
	server_protocol_version(0),
	server_capabilities(0),
	settings(NULL),
	ui_interface(NULL),
	extracted_anim_data_loaded(false),
	server_using_lod_chunks(false),
	server_has_basis_textures(false),
	server_has_basisu_terrain_detail_maps(false),
	server_has_optimised_meshes(false),
	server_opt_mesh_version(-1),
	last_cursor_movement_was_from_mouse(true),
	sent_perform_gesture_without_stop_gesture(false),
	use_lightmaps(true),
	cur_loading_model_lod_level(-1)
{
	resources_dir_path = base_dir_path + "/data/resources";

	scripted_ob_proximity_checker.gui_client = this;

	SubstrataLuaVM::SubstrataLuaVMArgs lua_vm_args;
	lua_vm_args.gui_client = this;
	lua_vm_args.player_physics = &this->player_physics;
	lua_vm = new SubstrataLuaVM(lua_vm_args);

	SHIFT_down = false;
	CTRL_down = false;
	W_down = false;
	A_down = false;
	S_down = false;
	D_down = false;
	space_down = false;
	C_down = false;
	left_down = false;
	right_down = false;
	up_down = false;
	down_down = false;
	B_down = false;

	texture_server = new TextureServer(/*use_canonical_path_keys=*/false); // Just used for caching textures for GUIClient::setMaterialFlagsForObject()

	model_and_texture_loader_task_manager.setThreadPriorities(MyThread::Priority_Lowest);
	
	this->world_ob_pool_allocator = new glare::FastPoolAllocator(/*ob alloc size=*/sizeof(WorldObject), /*alignment=*/64, /*block capacity=*/1024);
	this->world_ob_pool_allocator->name = "world_ob_pool_allocator";

	this->texture_loaded_msg_allocator = new glare::FastPoolAllocator(/*ob alloc size=*/sizeof(TextureLoadedThreadMessage), /*alignment=*/64, /*block capacity=*/64);
	this->texture_loaded_msg_allocator->name = "texture_loaded_msg_allocator";

	proximity_loader.callbacks = this;

	cam_controller.setMouseSensitivity(-1.0);

	try
	{
#if EMSCRIPTEN
		const uint64 rnd_buf = (uint64)(emscripten_random() * (float)std::numeric_limits<uint64>::max());
		this->rng = PCG32(1, rnd_buf); 
#else
		uint64 rnd_buf;
		CryptoRNG::getRandomBytes((uint8*)&rnd_buf, sizeof(uint64));
		this->rng = PCG32(1, rnd_buf);
#endif
	}
	catch(glare::Exception& e)
	{
		conPrint(e.what());
	}

	biome_manager = new BiomeManager();

	for(int i=0; i<NUM_AXIS_ARROWS; ++i)
		axis_arrow_segments[i] = LineSegment4f(Vec4f(0, 0, 0, 1), Vec4f(1, 0, 0, 1));

	this->animated_texture_manager = new AnimatedTextureManager();
}


void GUIClient::staticInit()
{
	// Set the C standard lib locale back to c, so e.g. printf works as normal, and uses '.' as the decimal separator.
	std::setlocale(LC_ALL, "C");

	Clock::init();
	Networking::init();
#if !defined(EMSCRIPTEN)
	Winter::VirtualMachine::init();
	TLSSocket::initTLS();
#endif
	PlatformUtils::ignoreUnixSignals();
	BasisDecoder::init();
}


void GUIClient::staticShutdown()
{
#if !defined(EMSCRIPTEN)
	OpenSSL::shutdown();
	Winter::VirtualMachine::shutdown();
#endif
	Networking::shutdown();
}


#if EMSCRIPTEN

static void onAnimDataLoad(unsigned int firstarg, void* userdata_arg, const char* filename)
{
	// conPrint("onAnimDataLoad: " + std::string(filename) + ", firstarg: " + toString(firstarg));

	GUIClient* gui_client = (GUIClient*)userdata_arg;
	gui_client->extracted_anim_data_loaded = true;
}

static void onAnimDataError(unsigned int, void* userdata_arg, int http_status_code)
{
	conPrint("onAnimDataError: " + toString(http_status_code));
}

static void onAnimDataProgress(unsigned int, void* userdata_arg, int percent_complete)
{
	// conPrint("onAnimDataProgress: " + toString(percent_complete));
}

#endif // EMSCRIPTEN


void GUIClient::initialise(const std::string& cache_dir, const Reference<SettingsStore>& settings_store_, UIInterface* ui_interface_, glare::TaskManager* high_priority_task_manager_, Reference<glare::Allocator> worker_allocator_)
{
	ZoneScoped; // Tracy profiler

	settings = settings_store_;
	ui_interface = ui_interface_;
	high_priority_task_manager = high_priority_task_manager_;
	worker_allocator = worker_allocator_;

	PhysicsWorld::init(); // init Jolt stuff

	const float dist = (float)settings->getDoubleValue(/*MainOptionsDialog::objectLoadDistanceKey()*/"ob_load_distance", /*default val=*/2000.0);
	proximity_loader.setLoadDistance(dist);
	this->load_distance = dist;
	this->load_distance2 = dist*dist;

	const std::string resources_dir = cache_dir + "/resources";
	FileUtils::createDirIfDoesNotExist(resources_dir);

	print("resources_dir: " + resources_dir);
	resource_manager = new ResourceManager(resources_dir);


	// The user may have changed the resources dir (by changing the custom cache directory) since last time we ran.
	// In this case, we want to check if each resource is actually present on disk in the current resources dir.
	const std::string last_resources_dir = settings->getStringValue("last_resources_dir", "");
	const bool resources_dir_changed = last_resources_dir != resources_dir;
	if(resources_dir_changed)
		settings->setStringValue("last_resources_dir", resources_dir);

	const std::string resources_db_path = appdata_path + "/resources_db";
	try
	{
		if(FileUtils::fileExists(resources_db_path))
			resource_manager->loadFromDisk(resources_db_path, /*check_if_resources_exist_on_disk=*/resources_dir_changed);
	}
	catch(glare::Exception& e)
	{
		conPrint("WARNING: failed to load resources database from '" + resources_db_path + "': " + e.what());
	}

#if !defined(EMSCRIPTEN)
	// With Emscripten we use an ephemeral virtual file system, so no point in saving resource manager state to it.
	save_resources_db_thread_manager.addThread(new SaveResourcesDBThread(resource_manager, resources_db_path));
#endif

	garbage_deleter_thread_manager.addThread(new GarbageDeleterThread());


	// Add default avatar mesh as an external resource.
	if(resource_manager->getExistingResourceForURL(DEFAULT_AVATAR_MODEL_URL).isNull())
	{
		ResourceRef resource = new Resource(/*URL=*/DEFAULT_AVATAR_MODEL_URL, /*local (abs) path=*/resources_dir_path + "/" + toStdString(DEFAULT_AVATAR_MODEL_URL), Resource::State_Present, UserID(), /*external_resource=*/true);
		resource_manager->addResource(resource);
	}


	// Add capsule mesh resource (used for audio objects)
	const URLString capsule_model_URL = "Capsule_obj_7611321750126528672.bmesh";
	if(!resource_manager->isFileForURLPresent(capsule_model_URL))
	{
		const std::string capsule_local_model_path = resources_dir_path + "/" + toStdString(capsule_model_URL);
		resource_manager->addResource(new Resource(capsule_model_URL, capsule_local_model_path, Resource::State_Present, UserID(), /*external resource=*/true));
	}


#if !defined(EMSCRIPTEN)
	// Create and init TLS client config
	client_tls_config = tls_config_new();
	if(!client_tls_config)
		throw glare::Exception("Failed to initialise TLS (tls_config_new failed)");
	tls_config_insecure_noverifycert(client_tls_config); // TODO: Fix this, check cert etc..
	tls_config_insecure_noverifyname(client_tls_config);


	// Init audio engine immediately if we are not on the web.  Web browsers need to wait for an input gesture is completed before trying to play sounds.
	initAudioEngine();
#endif
}


class WindNoiseLoaded : public ThreadMessage
{
public:
	WindNoiseLoaded(glare::SoundFileRef sound_) : sound(sound_) {}
	glare::SoundFileRef sound;
};


class LoadWindNoiseTask : public glare::Task
{
public:
	virtual void run(size_t /*thread_index*/)
	{
		glare::SoundFileRef sound = glare::AudioFileReader::readAudioFile(resources_dir_path + "/sounds/wind_noise_48000_hz_mono.mp3");
		result_msg_queue->enqueue(new WindNoiseLoaded(sound));
	}
	std::string resources_dir_path;
	ThreadSafeQueue<Reference<ThreadMessage> >* result_msg_queue;
};


void GUIClient::initAudioEngine()
{
	try
	{
		audio_engine.init();


		// Load a wind sound and create a non-spatial audio source, to use for a rushing effect when the player moves fast.
		// Do off main thread since it takes about 8ms to load on my 5900x.
		Reference<LoadWindNoiseTask> t = new LoadWindNoiseTask();
		t->resources_dir_path = resources_dir_path;
		t->result_msg_queue = &this->msg_queue;
		model_and_texture_loader_task_manager.addTask(t);
	}
	catch(glare::Exception& e) 
	{
		logMessage("Audio engine could not be initialised: " + e.what());
	}
}


static void assignLoadedOpenGLTexturesToAvatarMats(Avatar* av, bool use_basis, OpenGLEngine& opengl_engine, ResourceManager& resource_manager, AnimatedTextureManager& animated_texture_manager, glare::ArenaAllocator* allocator);


static const float arc_handle_half_angle = 1.5f;


void GUIClient::afterGLInitInitialise(double device_pixel_ratio, Reference<OpenGLEngine> opengl_engine_, 
	const TextRendererFontFaceSizeSetRef& fonts, const TextRendererFontFaceSizeSetRef& emoji_fonts)
{
	opengl_engine = opengl_engine_;

	// Add a default array texture.  Will be used for the chunk array texture before the proper one is loaded.
	std::vector<uint8> tex_data(4*4*3, 200);
	default_array_tex = new OpenGLTexture(4, 4, opengl_engine.ptr(),
		tex_data, OpenGLTextureFormat::Format_SRGB_Uint8, OpenGLTexture::Filtering_Nearest,
		OpenGLTexture::Wrapping_Repeat, false, -1, /* num array images=*/1);


	gl_ui = new GLUI();
	gl_ui->create(opengl_engine, (float)device_pixel_ratio, fonts, emoji_fonts, &this->stack_allocator);

	gesture_ui.create(opengl_engine, /*gui_client_=*/this, gl_ui);

	ob_info_ui.create(opengl_engine, /*gui_client_=*/this, gl_ui);

	misc_info_ui.create(opengl_engine, /*gui_client_=*/this, gl_ui);
	
	hud_ui.create(opengl_engine, /*gui_client_=*/this, gl_ui);

	chat_ui.create(opengl_engine, /*gui_client_=*/this, gl_ui);

	photo_mode_ui.create(opengl_engine, /*gui_client_=*/this, gl_ui, this->settings);
	photo_mode_ui.setVisible(false);


	// For non-Emscripten, init this stuff now.  For Emscripten, since this data is loaded from the webserver, wait until we are connecting and hence know the server hostname.
#if !EMSCRIPTEN
	this->async_texture_loader = new AsyncTextureLoader(/*local_path_prefix=*/base_dir_path + "/data", opengl_engine.ptr());

	opengl_engine->startAsyncLoadingData(this->async_texture_loader.ptr());

	// For emscripten, wait until we connect to server.
	terrain_decal_manager = new TerrainDecalManager(this->base_dir_path, /*async_tex_loader=*/async_texture_loader.ptr(), opengl_engine.ptr());

	particle_manager = new ParticleManager(this->base_dir_path, /*async_tex_loader=*/async_texture_loader.ptr(), opengl_engine.ptr(), physics_world.ptr(), terrain_decal_manager.ptr());
#endif


	const float sun_phi = 1.f;
	const float sun_theta = Maths::pi<float>() / 4;
	opengl_engine->setEnvMapTransform(Matrix3f::rotationAroundZAxis(sun_phi));

	/*
	Set env material
	*/
	{
		OpenGLMaterial env_mat;
		env_mat.tex_matrix = Matrix2f(-1 / Maths::get2Pi<float>(), 0, 0, 1 / Maths::pi<float>());

		opengl_engine->setEnvMat(env_mat);
	}

	opengl_engine->setSunDir(normalise(Vec4f(std::cos(sun_phi) * sin(sun_theta), std::sin(sun_phi) * sin(sun_theta), cos(sun_theta), 0)));


	// Make an arrow marking the axes at the origin
#if BUILD_TESTS
	const Vec4f arrow_origin(0, 0, 0.05f, 1);
	{
		GLObjectRef arrow = opengl_engine->makeArrowObject(arrow_origin, arrow_origin + Vec4f(1, 0, 0, 0), Colour4f(0.6, 0.2, 0.2, 1.f), 1.f);
		opengl_engine->addObject(arrow);
	}
	{
		GLObjectRef arrow = opengl_engine->makeArrowObject(arrow_origin, arrow_origin + Vec4f(0, 1, 0, 0), Colour4f(0.2, 0.6, 0.2, 1.f), 1.f);
		opengl_engine->addObject(arrow);
	}
	{
		GLObjectRef arrow = opengl_engine->makeArrowObject(arrow_origin, arrow_origin + Vec4f(0, 0, 1, 0), Colour4f(0.2, 0.2, 0.6, 1.f), 1.f);
		opengl_engine->addObject(arrow);
	}
#endif

	// For ob placement:
	axis_arrow_objects[0] = opengl_engine->makeArrowObject(Vec4f(0,0,0,1), Vec4f(1, 0, 0, 1), Colour4f(0.6, 0.2, 0.2, 1.f), 1.f);
	axis_arrow_objects[1] = opengl_engine->makeArrowObject(Vec4f(0,0,0,1), Vec4f(0, 1, 0, 1), Colour4f(0.2, 0.6, 0.2, 1.f), 1.f);
	axis_arrow_objects[2] = opengl_engine->makeArrowObject(Vec4f(0,0,0,1), Vec4f(0, 0, 1, 1), Colour4f(0.2, 0.2, 0.6, 1.f), 1.f);

	//axis_arrow_objects[3] = opengl_engine->makeArrowObject(arrow_origin, arrow_origin - Vec4f(1, 0, 0, 0), Colour4f(0.6, 0.2, 0.2, 1.f), 1.f);
	//axis_arrow_objects[4] = opengl_engine->makeArrowObject(arrow_origin, arrow_origin - Vec4f(0, 1, 0, 0), Colour4f(0.2, 0.6, 0.2, 1.f), 1.f);
	//axis_arrow_objects[5] = opengl_engine->makeArrowObject(arrow_origin, arrow_origin - Vec4f(0, 0, 1, 0), Colour4f(0.2, 0.2, 0.6, 1.f), 1.f);

	for(int i=0; i<3; ++i)
	{
		axis_arrow_objects[i]->materials[0].albedo_linear_rgb = toLinearSRGB(axis_arrows_default_cols[i]);
		axis_arrow_objects[i]->always_visible = true;
	}


	for(int i=0; i<3; ++i)
	{
		GLObjectRef ob = opengl_engine->allocateObject();
		ob->ob_to_world_matrix = Matrix4f::translationMatrix((float)i * 3, 0, 2);
		ob->mesh_data = MeshBuilding::makeRotationArcHandleMeshData(*opengl_engine->vert_buf_allocator, arc_handle_half_angle * 2);
		ob->materials.resize(1);
		ob->materials[0].albedo_linear_rgb = toLinearSRGB(axis_arrows_default_cols[i]);
		ob->always_visible = true;
		rot_handle_arc_objects[i] = ob;
	}


	// Build ground plane graphics and physics data
	ground_quad_mesh_opengl_data = MeshPrimitiveBuilding::makeQuadMesh(*opengl_engine->vert_buf_allocator, Vec4f(1,0,0,0), Vec4f(0,1,0,0), /*res=*/16);
	ground_quad_shape = PhysicsWorld::createGroundQuadShape(ground_quad_w);


	// Make hypercard physics mesh
	{
		Indigo::MeshRef mesh = new Indigo::Mesh();
		{
			mesh->addVertex(Indigo::Vec3f(0,0,0));
			mesh->addVertex(Indigo::Vec3f(0,0,1));
			mesh->addVertex(Indigo::Vec3f(1,0,1));
			mesh->addVertex(Indigo::Vec3f(1,0,0));
			const unsigned int vertex_indices[]   = {0, 1, 2};
			mesh->addTriangle(vertex_indices, vertex_indices, 0);
			const unsigned int vertex_indices_2[] = {0, 2, 3};
			mesh->addTriangle(vertex_indices_2, vertex_indices_2, 0);
		}
		mesh->endOfModel();

		hypercard_quad_shape = PhysicsWorld::createJoltShapeForIndigoMesh(*mesh, /*build_dynamic_physics_ob=*/false);
	}

	hypercard_quad_opengl_mesh = MeshPrimitiveBuilding::makeQuadMesh(*opengl_engine->vert_buf_allocator, Vec4f(1, 0, 0, 0), Vec4f(0, 0, 1, 0), /*vert_res=*/2);

	{
		Indigo::MeshRef mesh = new Indigo::Mesh();
		{
			mesh->addVertex(Indigo::Vec3f(0,0,0));
			mesh->addVertex(Indigo::Vec3f(1,0,0));
			mesh->addVertex(Indigo::Vec3f(1,1,0));
			mesh->addVertex(Indigo::Vec3f(0,1,0));
			const unsigned int vertex_indices[]   = {0, 1, 2};
			mesh->addTriangle(vertex_indices, vertex_indices, 0);
			const unsigned int vertex_indices_2[] = {0, 2, 3};
			mesh->addTriangle(vertex_indices_2, vertex_indices_2, 0);
		}
		mesh->endOfModel();

		text_quad_shape = PhysicsWorld::createJoltShapeForIndigoMesh(*mesh, /*build_dynamic_physics_ob=*/false);
	}

	// Make spotlight meshes
	{
		MeshBuilding::MeshBuildingResults results = MeshBuilding::makeSpotlightMeshes(base_dir_path, *opengl_engine->vert_buf_allocator);
		spotlight_opengl_mesh = results.opengl_mesh_data;
		spotlight_shape = results.physics_shape;
	}

	// Make image cube meshes
	{
		MeshBuilding::MeshBuildingResults results = MeshBuilding::makeImageCube(*opengl_engine->vert_buf_allocator);
		image_cube_opengl_mesh = results.opengl_mesh_data;
		image_cube_shape = results.physics_shape;
	}

	// Make unit-cube raymesh (used for placeholder model)
	unit_cube_shape = image_cube_shape;


	// Create single-voxel mesh for fast special-case adding of single voxel models, which are used a lot.
	{
		const js::Vector<bool, 16> mat_transparent(1, false);
		const bool need_lightmap_uvs = true;
		VoxelGroup voxel_group;
		voxel_group.voxels.push_back(Voxel(Vec3i(0,0,0), /*mat index=*/0));

		PhysicsShape single_voxel_shape;
		Reference<OpenGLMeshRenderData> single_voxel_mesh_opengl_data = ModelLoading::makeModelForVoxelGroup(voxel_group, /*subsample_factor=*/1, /*ob_to_world_matrix=*/Matrix4f::identity(), /*vert_buf_allocator=*/opengl_engine->vert_buf_allocator.ptr(), 
			/*do_opengl_stuff=*/true, need_lightmap_uvs, mat_transparent, /*build_dynamic_physics_ob=*/false, worker_allocator.ptr(), /*physics shape out=*/single_voxel_shape);

		single_voxel_meshdata = new MeshData(/*url=*/"single voxel meshdata", single_voxel_mesh_opengl_data, &mesh_manager);

		single_voxel_shapedata = new PhysicsShapeData(/*url=*/"single voxel shapedata", /*dynamic=*/false, single_voxel_shape, &mesh_manager);
	}


	// Make object-placement beam model
	{
		ob_placement_beam = opengl_engine->allocateObject();
		ob_placement_beam->ob_to_world_matrix = Matrix4f::identity();
		ob_placement_beam->mesh_data = opengl_engine->getCylinderMesh();

		OpenGLMaterial material;
		material.albedo_linear_rgb = toLinearSRGB(Colour3f(0.3f, 0.8f, 0.3f));
		material.transparent = true;
		material.alpha = 0.9f;

		ob_placement_beam->setSingleMaterial(material);

		// Make object-placement beam hit marker out of a sphere.
		ob_placement_marker = opengl_engine->allocateObject();
		ob_placement_marker->ob_to_world_matrix = Matrix4f::identity();
		ob_placement_marker->mesh_data = opengl_engine->getSphereMeshData();

		ob_placement_marker->setSingleMaterial(material);
	}

	{
		// Make ob_denied_move_marker
		ob_denied_move_marker = opengl_engine->allocateObject();
		ob_denied_move_marker->ob_to_world_matrix = Matrix4f::identity();
		ob_denied_move_marker->mesh_data = opengl_engine->getSphereMeshData();

		OpenGLMaterial material;
		material.albedo_linear_rgb = toLinearSRGB(Colour3f(0.8f, 0.2f, 0.2f));
		material.transparent = true;
		material.alpha = 0.9f;

		ob_denied_move_marker->setSingleMaterial(material);
	}

	// Make voxel_edit_marker model
	{
		voxel_edit_marker = opengl_engine->allocateObject();
		voxel_edit_marker->ob_to_world_matrix = Matrix4f::identity();
		voxel_edit_marker->mesh_data = opengl_engine->getCubeMeshData();

		OpenGLMaterial material;
		material.albedo_linear_rgb = toLinearSRGB(Colour3f(0.3f, 0.8f, 0.3f));
		material.transparent = true;
		material.alpha = 0.3f;

		voxel_edit_marker->setSingleMaterial(material);
	}

	// Make voxel_edit_face_marker model
	{
		voxel_edit_face_marker = opengl_engine->allocateObject();
		voxel_edit_face_marker->ob_to_world_matrix = Matrix4f::identity();
		voxel_edit_face_marker->mesh_data = opengl_engine->getUnitQuadMeshData();

		OpenGLMaterial material;
		material.albedo_linear_rgb = toLinearSRGB(Colour3f(0.3f, 0.8f, 0.3f));
		voxel_edit_face_marker->setSingleMaterial(material);
	}

	// Make shader for parcels
	{
		const std::string use_shader_dir = base_dir_path + "/data/shaders";
		const std::string version_directive    = opengl_engine->getVersionDirective();
		const std::string preprocessor_defines = opengl_engine->getPreprocessorDefines();
				
		parcel_shader_prog = new OpenGLProgram(
			"parcel hologram prog",
			new OpenGLShader(use_shader_dir + "/parcel_vert_shader.glsl", version_directive, preprocessor_defines, GL_VERTEX_SHADER),
			new OpenGLShader(use_shader_dir + "/parcel_frag_shader.glsl", version_directive, preprocessor_defines, GL_FRAGMENT_SHADER),
			opengl_engine->getAndIncrNextProgramIndex(),
			/*wait for build to complete=*/true
		);
		opengl_engine->addProgram(parcel_shader_prog);
		// Let any glare::Exception thrown fall through to below.
	}

	
	// TEMP: make an avatar for testing of animation retargeting etc.
	if(false) // test_avatars.empty() && extracted_anim_data_loaded)
	{
		//const std::string path = "C:\\Users\\nick\\Downloads\\jokerwithchainPOV.vrm";
		//const std::string path = "D:\\models\\readyplayerme_nick_tpose.glb";
		//const std::string path = "D:\\models\\generic dude avatar.glb";
		const std::string path = resources_dir_path + "/" + toStdString(DEFAULT_AVATAR_MODEL_URL);
		MemMappedFile model_file(path);
		ArrayRef<uint8> model_buffer((const uint8*)model_file.fileData(), model_file.fileSize());

		js::Vector<bool> create_tris_for_mat;

		PhysicsShape physics_shape;
		Reference<OpenGLMeshRenderData> mesh_data = ModelLoading::makeGLMeshDataAndBatchedMeshForModelPath(path, model_buffer,
			opengl_engine->vert_buf_allocator.ptr(), false, /*build_physics_ob=*/true, /*build_dynamic_physics_ob=*/false, create_tris_for_mat, /*allocator=*/nullptr, physics_shape);

		const int N = 1500;
		test_avatars.resize(N);
		for(size_t q=0; q<test_avatars.size(); ++q)
		{
			AvatarRef test_avatar = new Avatar();
			test_avatars[q] = test_avatar;
			test_avatar->pos = Vec3d(3,0,2);
			test_avatar->rotation = Vec3f(1,0,0);

			const float EYE_HEIGHT = 1.67f;
			const Matrix4f to_z_up(Vec4f(1,0,0,0), Vec4f(0, 0, 1, 0), Vec4f(0, -1, 0, 0), Vec4f(0,0,0,1));
			test_avatar->avatar_settings.pre_ob_to_world_matrix = Matrix4f::translationMatrix(0, 0, -EYE_HEIGHT) * to_z_up;

			const Matrix4f ob_to_world_matrix = obToWorldMatrix(*test_avatar);


			test_avatar->graphics.skinned_gl_ob = ModelLoading::makeGLObjectForMeshDataAndMaterials(*opengl_engine, mesh_data, /*ob_lod_level=*/0, 
				test_avatar->avatar_settings.materials, /*lightmap_url=*/URLString(), /*use_basis=*/true, *resource_manager, ob_to_world_matrix);

				
			// Load animation data
			{
#if EMSCRIPTEN
				FileInStream file("/extracted_avatar_anim.bin");
#else
				FileInStream file(resources_dir_path + "/extracted_avatar_anim.bin");
#endif
				
				test_avatar->graphics.skinned_gl_ob->mesh_data->animation_data.loadAndRetargetAnim(file);
			}

			test_avatar->graphics.build();

			for(size_t z=0; z<test_avatar->graphics.skinned_gl_ob->materials.size(); ++z)
				test_avatar->graphics.skinned_gl_ob->materials[z].alpha = 0.5f;
			opengl_engine->objectMaterialsUpdated(*test_avatar->graphics.skinned_gl_ob);

			// for(size_t i=0; i<test_avatar->graphics.skinned_gl_ob->mesh_data->animation_data.nodes.size(); ++i)
			// {
			// 	conPrint("node " + toString(i) + ": " + test_avatar->graphics.skinned_gl_ob->mesh_data->animation_data.nodes[i].name);
			// }

			assignLoadedOpenGLTexturesToAvatarMats(test_avatar.ptr(), /*use_basis=*/this->server_has_basis_textures, *opengl_engine, *resource_manager, *animated_texture_manager, &arena_allocator);

			opengl_engine->addObject(test_avatar->graphics.skinned_gl_ob);
		}
	}

	// TEMP: Load a GLB object directly into the graphics engine for testing.
	if(false)
	{
		ModelLoading::MakeGLObjectResults results;
		ModelLoading::makeGLObjectForModelFile(*opengl_engine, *opengl_engine->vert_buf_allocator, /*allocator=*/nullptr, 
			//"D:\\models\\readyplayerme_avatar_animation_18.glb", 
			"D:\\models\\BMWCONCEPTBIKE\\bike no armature.glb",
			//"N:\\glare-core\\trunk\\testfiles\\gltf\\BoxAnimated.glb", 
			/*do_opengl_stuff=*/true,
			results);

		results.gl_ob->ob_to_world_matrix = Matrix4f::rotationMatrix(Vec4f(1,0,0,0), Maths::pi_2<float>()) * Matrix4f::uniformScaleMatrix(1);
		opengl_engine->addObject(results.gl_ob);
	}

	if(ui_interface->supportsSharedGLContexts())
	{
		opengl_upload_thread = new OpenGLUploadThread();
		opengl_upload_thread->gl_context = ui_interface->makeNewSharedGLContext();
		opengl_upload_thread->make_gl_context_current_func = [&](void* gl_context) { ui_interface->makeGLContextCurrent(gl_context); };
		opengl_upload_thread->opengl_engine = opengl_engine.ptr();
		opengl_upload_thread->out_msg_queue = &this->msg_queue;

		opengl_worker_thread_manager.addThread(opengl_upload_thread);
	}
	else
	{
		pbo_pool = new PBOPool();
		vbo_pool       = new VBOPool(GL_ARRAY_BUFFER);
		index_vbo_pool = new VBOPool(GL_ELEMENT_ARRAY_BUFFER); // WebGL requires index data and vertex data to be kept separate
	}
}


GUIClient::~GUIClient()
{
	// Kill various threads before we start destroying members of GUIClient they may depend on.
	// Need to kill DownloadResourcesThread before we destroy the download_queue, which it references, for example.
	/*if(task_manager)
	{
		delete task_manager;
		task_manager = NULL;
	}*/

	if(wind_audio_source)
		audio_engine.removeSource(wind_audio_source);
	wind_audio_source = NULL;

	player_physics.shutdown();

	// Save resources DB to disk if it has un-saved changes.
	const std::string resources_db_path = appdata_path + "/resources_db";
	try
	{
		if(resource_manager->hasChanged())
			resource_manager->saveToDisk(resources_db_path);
	}
	catch(glare::Exception& e)
	{
		conPrint("WARNING: failed to save resources database to '" + resources_db_path + "': " + e.what());
	}

#if !defined(EMSCRIPTEN)
	if(this->client_tls_config)
		tls_config_free(this->client_tls_config);
#endif
}


void GUIClient::shutdown()
{
	// Destroy/close all OpenGL stuff, because once glWidget is destroyed, the OpenGL context is destroyed, so we can't free stuff properly.

	this->msg_queue.clear();

	load_item_queue.clear();

	model_and_texture_loader_task_manager.removeQueuedTasks();
	model_and_texture_loader_task_manager.waitForTasksToComplete();

	opengl_worker_thread_manager.killThreadsBlocking();

	model_loaded_messages_to_process.clear();
	texture_loaded_messages_to_process.clear();
	async_model_loaded_messages_to_process.clear();
	async_texture_loaded_messages_to_process.clear();


	// Clear web_view_obs - will close QWebEngineViews
	for(auto entry : web_view_obs)
	{
		entry->web_view_data = NULL;
	}
	web_view_obs.clear();

	// Clear browser_vid_player_obs
	for(auto entry : browser_vid_player_obs)
	{
		entry->browser_vid_player = NULL;
	}
	browser_vid_player_obs.clear();

	// Clear obs_with_animated_tex - will close video readers
	for(auto entry : obs_with_animated_tex)
	{
		entry->animated_tex_data = NULL;
	}
	obs_with_animated_tex.clear();

	for(size_t i=0; i<test_avatars.size(); ++i)
		test_avatars[i]->graphics.destroy(*opengl_engine); // Remove any OpenGL object for it


	disconnectFromServerAndClearAllObjects();

	
	if(biome_manager) delete biome_manager;

	// Remove the notifications from the UI
	for(auto it = notifications.begin(); it != notifications.end(); ++it)
		gl_ui->removeWidget(it->text_view); 
	notifications.clear();

	// Remove the script_messages from the UI
	for(auto it = script_messages.begin(); it != script_messages.end(); ++it)
		gl_ui->removeWidget(it->text_view); 
	script_messages.clear();

	default_array_tex = nullptr;

	misc_info_ui.destroy();

	ob_info_ui.destroy();

	gesture_ui.destroy();
	
	hud_ui.destroy();

	chat_ui.destroy();

	photo_mode_ui.destroy();

	minimap = nullptr;

	if(gl_ui.nonNull())
	{
		gl_ui->destroy();
		gl_ui = NULL;
	}


	ground_quad_mesh_opengl_data = NULL;
	hypercard_quad_opengl_mesh = NULL;
	image_cube_opengl_mesh = NULL;
	spotlight_opengl_mesh = NULL;
	cur_loading_mesh_data = NULL;
	single_voxel_meshdata = NULL;
	single_voxel_shapedata = NULL;

	ob_placement_beam = NULL;
	ob_placement_marker = NULL;
	voxel_edit_marker = NULL;
	voxel_edit_face_marker = NULL;
	ob_denied_move_marker = NULL;
	ob_denied_move_markers.clear();
	aabb_os_vis_gl_ob = NULL;
	aabb_ws_vis_gl_ob = NULL;
	selected_ob_vis_gl_obs.clear();
	for(int i=0; i<NUM_AXIS_ARROWS; ++i)
		axis_arrow_objects[i] = NULL;
	for(int i=0; i<3; ++i)
		rot_handle_arc_objects[i] = NULL;
	player_phys_debug_spheres.clear();
	wheel_gl_objects.clear();
	car_body_gl_object = NULL;
	mouseover_selected_gl_ob = NULL;

	
	client_thread_manager.killThreadsBlocking();
	client_udp_handler_thread_manager.killThreadsBlocking();
	mic_read_thread_manager.killThreadsBlocking();
	resource_upload_thread_manager.killThreadsBlocking();
	resource_download_thread_manager.killThreadsBlocking();
	net_resource_download_thread_manager.killThreadsBlocking();
	save_resources_db_thread_manager.killThreadsBlocking();
	garbage_deleter_thread_manager.killThreadsBlocking();
	

	model_and_texture_loader_task_manager.cancelAndWaitForTasksToComplete();

	this->msg_queue.clear();

	opengl_worker_thread_manager.killThreadsBlocking();
	if(opengl_upload_thread)
	{
		// Make sure the allocators are destroyed after opengl_upload_thread as opengl_upload_thread has a message queue that might have messages from these allocators.
		Reference<glare::FastPoolAllocator> upload_texture_msg_allocator       = opengl_upload_thread->upload_texture_msg_allocator;
		Reference<glare::FastPoolAllocator> animated_texture_updated_allocator = opengl_upload_thread->animated_texture_updated_allocator;
		opengl_upload_thread = NULL;
	}

	pbo_pool = NULL;
	vbo_pool = NULL;
	index_vbo_pool = NULL;
	pbo_async_tex_loader.clear();
	async_geom_loader.clear();
	async_index_geom_loader.clear();


	mesh_manager.clear(); // Mesh manager has references to cached/unused meshes, so need to zero out the references before we shut down the OpenGL engine.

	animated_texture_manager = NULL;

	this->opengl_engine = NULL;

	texture_server = NULL;
}


void GUIClient::startDownloadingResource(const URLString& url, const Vec4f& centroid_ws, float aabb_ws_longest_len, const DownloadingResourceInfo& resource_info)
{
	assert(url.get_allocator().arena_allocator == nullptr);
	assert(resource_info.used_by_other || resource_info.used_by_terrain || !resource_info.using_objects.using_object_uids.empty());

	//conPrint("-------------------GUIClient::startDownloadingResource()-------------------\nURL: " + url);
	//if(shouldStreamResourceViaHTTP(url))
	//	return;

	ResourceRef resource = resource_manager->getOrCreateResourceForURL(url);
	if(resource->getState() != Resource::State_NotPresent) // If it is getting downloaded, or is downloaded:
	{
		//conPrint("Already present or being downloaded, skipping...");
		return;
	}

	if(resource_manager->isInDownloadFailedURLs(url))
	{
		//conPrint("startDownloadingResource(): Skipping download due to having failed: " + url);
		return;
	}

	try
	{
		if(this->URL_to_downloading_info.count(url) == 0)
		{
			this->URL_to_downloading_info[url] = resource_info;
		}
		else
		{
			// Merge
			DownloadingResourceInfo& existing_info = this->URL_to_downloading_info[url];
			existing_info.used_by_terrain   = existing_info.used_by_terrain   || resource_info.used_by_terrain;
			existing_info.used_by_other     = existing_info.used_by_other     || resource_info.used_by_other;

			if(!resource_info.using_objects.using_object_uids.empty())
				existing_info.using_objects.using_object_uids.push_back(resource_info.using_objects.using_object_uids[0]);
		}

		if(hasPrefix(url, "http://") || hasPrefix(url, "https://"))
		{
			this->net_resource_download_thread_manager.enqueueMessage(new DownloadResourceMessage(url));
			num_net_resources_downloading++;
		}
		else
		{
			// conPrint("Queued resource to download...");
			this->download_queue.enqueueOrUpdateItem(url, centroid_ws, /*size factor=*/DownloadQueueItem::sizeFactorForAABBWS(aabb_ws_longest_len));
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("Failed to parse URL '" + toStdString(url) + "': " + e.what());
	}
}


bool GUIClient::checkAddTextureToProcessingSet(const OpenGLTextureKey& path)
{
	auto res = textures_processing.insert(path);
	return res.second; // Was texture inserted? (will be false if already present in set)
}


bool GUIClient::checkAddModelToProcessingSet(const URLString& url, bool dynamic_physics_shape)
{
	ModelProcessingKey key(url, dynamic_physics_shape);
	auto res = models_processing.insert(key);
	return res.second; // Was model inserted? (will be false if already present in set)
}


static void enqueueMessageToSend(ClientThread& client_thread, SocketBufferOutStream& packet)
{
	MessageUtils::updatePacketLengthField(packet);

	client_thread.enqueueDataToSend(packet.buf);
}


void GUIClient::sendChatMessage(const std::string& message)
{
	//conPrint("GUIClient::sendChatMessage()");

	if(this->connection_state == ServerConnectionState_NotConnected)
	{
		showErrorNotification("Can't send a chat message when not connected to server.");
	}
	else
	{
		// Make message packet and enqueue to send
		MessageUtils::initPacket(scratch_packet, Protocol::ChatMessageID);
		scratch_packet.writeStringLengthFirst(message);

		enqueueMessageToSend(*client_thread, scratch_packet);
	}
}


bool GUIClient::checkAddAudioToProcessingSet(const URLString& url)
{
	auto res = audio_processing.insert(url);
	return res.second; // Was audio inserted? (will be false if already present in set)
}


bool GUIClient::checkAddScriptToProcessingSet(const std::string& script_content) // returns true if was not in processed set (and hence this call added it), false if it was.
{
	auto res = script_content_processing.insert(script_content);
	return res.second; // Was inserted? (will be false if already present in set)
}


// Is non-empty and has a supported image extension.
// Mp4 files will be handled with other code, not loaded in a LoadTextureTask, so we want to return false for mp4 extensions.
static inline bool isValidImageTextureURL(const URLString& URL)
{
	return !URL.empty() && ImageDecoding::hasSupportedImageExtension(URL);
}


static inline bool isValidLightMapURL(OpenGLEngine& opengl_engine, const string_view URL)
{
	if(URL.empty())
		return false;
	else
	{
		if(ImageDecoding::hasSupportedImageExtension(URL))
		{
			// KTX and KTX2 files used for lightmaps use bc6h compression, which mac and some web platforms don't support.  So don't try and load them if we don't support BC6H.
			if(opengl_engine.texture_compression_BC6H_support)
				return true;
			else
			{
				const string_view extension = getExtensionStringView(URL);
				return (extension != "ktx") && (extension != "ktx2"); 
			}
		}
		else
			return false;
	}
}


// Start loading texture, if present
void GUIClient::startLoadingTextureIfPresent(const URLString& tex_url, const Vec4f& centroid_ws, float aabb_ws_longest_len, float max_task_dist, float importance_factor, 
	const TextureParams& tex_params)
{
	if(isValidImageTextureURL(tex_url))
	{
		ResourceRef resource = resource_manager->getExistingResourceForURL(tex_url);
		if(resource.nonNull() && (resource->getState() == Resource::State_Present)) // If the texture is present on disk:
		{
			const OpenGLTextureKey local_abs_tex_path = OpenGLTextureKey(resource_manager->getLocalAbsPathForResource(*resource));

			startLoadingTextureForLocalPath(local_abs_tex_path, resource, centroid_ws, aabb_ws_longest_len, max_task_dist, importance_factor, tex_params);
		}
	}
}


void GUIClient::startLoadingTextureForLocalPath(const OpenGLTextureKey& local_abs_tex_path, const ResourceRef& resource, const Vec4f& centroid_ws, float aabb_ws_longest_len, float max_task_dist, float importance_factor, 
		const TextureParams& tex_params)
{
	//assert(resource_manager->getExistingResourceForURL(tex_url).nonNull() && resource_manager->getExistingResourceForURL(tex_url)->getState() == Resource::State_Present);
	if(!opengl_engine->isOpenGLTextureInsertedForKey(local_abs_tex_path)) // If texture is not uploaded to GPU already:
	{
		const bool just_added = checkAddTextureToProcessingSet(local_abs_tex_path); // If not being loaded already:
		if(just_added)
		{
			// conPrint("Adding LoadTextureTask for texture '" + local_abs_tex_path + "'...");
			const bool used_by_terrain = this->terrain_system.nonNull() && this->terrain_system->isTextureUsedByTerrain(local_abs_tex_path);

			Reference<LoadTextureTask> task = new LoadTextureTask(opengl_engine, resource_manager, &this->msg_queue, local_abs_tex_path, resource, tex_params, used_by_terrain, worker_allocator, texture_loaded_msg_allocator,
				opengl_upload_thread);

			load_item_queue.enqueueItem(
				resource->URL, // key
				centroid_ws, 
				aabb_ws_longest_len, 
				task,
				max_task_dist, 
				importance_factor
			);
		}
		else
		{
			load_item_queue.checkUpdateItemPosition(/*key=*/resource->URL, centroid_ws, aabb_ws_longest_len, importance_factor);
		}
	}
}


// max_dist_for_ob_lod_level: maximum distance from camera at which the object will still be at ob_lod_level.
// max_dist_for_ob_lod_level_clamped_0: maximum distance from camera at which the object will still be at max(0, ob_lod_level).   [e.g. treats level -1 as 0]
void GUIClient::startLoadingTextureForObjectOrAvatar(const UID& ob_uid, const UID& avatar_uid, const Vec4f& centroid_ws, float aabb_ws_longest_len, float max_dist_for_ob_lod_level, float max_dist_for_ob_lod_level_clamped_0, float importance_factor, const WorldMaterial& world_mat, int ob_lod_level, 
	const URLString& texture_url, bool tex_has_alpha, bool use_sRGB, bool allow_compression)
{
	const WorldMaterial::GetURLOptions get_url_options(/*use basis=*/server_has_basis_textures, /*area allocator=*/nullptr);

	const URLString lod_tex_url = world_mat.getLODTextureURLForLevel(get_url_options, texture_url, ob_lod_level, tex_has_alpha);

	// If the material has minimum LOD level = 0, and the current object LOD level is -1, then we want to use the max distance for LOD level 0, not for -1.
	float use_max_dist_for_ob_lod_level;
	if((ob_lod_level == -1) && (world_mat.minLODLevel() == 0))
		use_max_dist_for_ob_lod_level = max_dist_for_ob_lod_level_clamped_0;
	else
		use_max_dist_for_ob_lod_level = max_dist_for_ob_lod_level;


	if(ob_uid.valid())
		this->loading_texture_URL_to_world_ob_UID_map[lod_tex_url].insert(ob_uid);
	else if(avatar_uid.valid())
		this->loading_texture_URL_to_avatar_UID_map[lod_tex_url].insert(avatar_uid);


	TextureParams tex_params;
	tex_params.use_sRGB = use_sRGB;
	tex_params.allow_compression = allow_compression;
	startLoadingTextureIfPresent(lod_tex_url, centroid_ws, aabb_ws_longest_len, use_max_dist_for_ob_lod_level, importance_factor, tex_params);
}


void GUIClient::startLoadingTexturesForObject(const WorldObject& ob, int ob_lod_level, float max_dist_for_ob_lod_level, float max_dist_for_ob_lod_level_clamped_0)
{
	// Process model materials - start loading any textures that are present on disk, and not already loaded and processed:
	for(size_t i=0; i<ob.materials.size(); ++i)
	{
		const WorldMaterial* mat = ob.materials[i].ptr();
		if(!mat->colour_texture_url.empty())
			startLoadingTextureForObjectOrAvatar(ob.uid, /*avatar uid=*/UID::invalidUID(), ob.getCentroidWS(), ob.getAABBWSLongestLength(), max_dist_for_ob_lod_level, max_dist_for_ob_lod_level_clamped_0, /*importance factor=*/1.f, *mat, ob_lod_level, mat->colour_texture_url, mat->colourTexHasAlpha(), /*use_sRGB=*/true, /*allow_compression=*/true);
		if(!mat->emission_texture_url.empty())
			startLoadingTextureForObjectOrAvatar(ob.uid, /*avatar uid=*/UID::invalidUID(), ob.getCentroidWS(), ob.getAABBWSLongestLength(), max_dist_for_ob_lod_level, max_dist_for_ob_lod_level_clamped_0, /*importance factor=*/1.f, *mat, ob_lod_level, mat->emission_texture_url, /*has_alpha=*/false, /*use_sRGB=*/true, /*allow_compression=*/true);
		if(!mat->roughness.texture_url.empty())
			startLoadingTextureForObjectOrAvatar(ob.uid, /*avatar uid=*/UID::invalidUID(), ob.getCentroidWS(), ob.getAABBWSLongestLength(), max_dist_for_ob_lod_level, max_dist_for_ob_lod_level_clamped_0, /*importance factor=*/1.f, *mat, ob_lod_level, mat->roughness.texture_url, /*has_alpha=*/false, /*use_sRGB=*/false, /*allow_compression=*/true);
		if(!mat->normal_map_url.empty())
			startLoadingTextureForObjectOrAvatar(ob.uid, /*avatar uid=*/UID::invalidUID(), ob.getCentroidWS(), ob.getAABBWSLongestLength(), max_dist_for_ob_lod_level, max_dist_for_ob_lod_level_clamped_0, /*importance factor=*/1.f, *mat, ob_lod_level, mat->normal_map_url, /*has_alpha=*/false, /*use_sRGB=*/false, /*allow_compression=*/false);
	}

	// Start loading lightmap
	if(this->use_lightmaps && isValidLightMapURL(*opengl_engine, ob.lightmap_url))
	{
		const URLString lod_tex_url = WorldObject::getLODLightmapURLForLevel(ob.lightmap_url, ob_lod_level);

		ResourceRef resource = resource_manager->getExistingResourceForURL(lod_tex_url);
		if(resource.nonNull() && (resource->getState() == Resource::State_Present)) // If the texture is present on disk:
		{
			const OpenGLTextureKey tex_path = OpenGLTextureKey(resource_manager->getLocalAbsPathForResource(*resource));

			if(!opengl_engine->isOpenGLTextureInsertedForKey(tex_path)) // If texture is not uploaded to GPU already:
			{
				const bool just_added = checkAddTextureToProcessingSet(tex_path); // If not being loaded already:
				if(just_added)
				{
					TextureParams tex_params;
					// For lightmaps, don't use mipmaps, for a couple of reasons:
					// If we use trilinear filtering, we get leaking of colours past geometry edges at lower mip levels.
					// Also we don't have code to generate mipmaps for floating point data currently, and hence have to rely on the OpenGL driver which can be slow.
					// This code is also in startDownloadingResourcesForObject().
					tex_params.filtering = OpenGLTexture::Filtering_Bilinear;
					tex_params.use_mipmaps = false;
					load_item_queue.enqueueItem(/*key=*/lod_tex_url, ob, 
						new LoadTextureTask(opengl_engine, resource_manager, &this->msg_queue, tex_path, resource, tex_params, /*is_terrain_map=*/false, worker_allocator, texture_loaded_msg_allocator, opengl_upload_thread), 
						max_dist_for_ob_lod_level_clamped_0); // Lightmaps don't have LOD level -1 so used max dist for LOD level >= 0.
				}
				// Lightmaps are only used by a single object, so there should be no other uses of the lightmap, so don't need to call load_item_queue.checkUpdateItemPosition()

				this->loading_texture_URL_to_world_ob_UID_map[lod_tex_url].insert(ob.uid);
			}
		}
	}
}


void GUIClient::startLoadingTexturesForAvatar(const Avatar& av, int ob_lod_level, float max_dist_for_ob_lod_level, bool our_avatar)
{
	// Prioritise loading our avatar first.
	const float our_avatar_importance_factor = our_avatar ? 1.0e4f : 1.f;

	// Process model materials - start loading any textures that are present on disk, and not already loaded and processed:
	for(size_t i=0; i<av.avatar_settings.materials.size(); ++i)
	{
		const WorldMaterial* mat = av.avatar_settings.materials[i].ptr();
		if(!mat->colour_texture_url.empty())
			startLoadingTextureForObjectOrAvatar(/*ob uid=*/UID::invalidUID(), av.uid, av.pos.toVec4fPoint(), /*aabb_ws_longest_len=*/1.8f, max_dist_for_ob_lod_level, max_dist_for_ob_lod_level, our_avatar_importance_factor, *mat, ob_lod_level, mat->colour_texture_url, mat->colourTexHasAlpha(), /*use_sRGB=*/true, /*allow compression=*/true);
		if(!mat->emission_texture_url.empty())
			startLoadingTextureForObjectOrAvatar(/*ob uid=*/UID::invalidUID(), av.uid, av.pos.toVec4fPoint(), /*aabb_ws_longest_len=*/1.8f, max_dist_for_ob_lod_level, max_dist_for_ob_lod_level, our_avatar_importance_factor, *mat, ob_lod_level, mat->emission_texture_url, /*has_alpha=*/false, /*use_sRGB=*/true, /*allow compression=*/true);
		if(!mat->roughness.texture_url.empty())
			startLoadingTextureForObjectOrAvatar(/*ob uid=*/UID::invalidUID(), av.uid, av.pos.toVec4fPoint(), /*aabb_ws_longest_len=*/1.8f, max_dist_for_ob_lod_level, max_dist_for_ob_lod_level, our_avatar_importance_factor, *mat, ob_lod_level, mat->roughness.texture_url, /*has_alpha=*/false, /*use_sRGB=*/false, /*allow compression=*/true);
		if(!mat->normal_map_url.empty())
			startLoadingTextureForObjectOrAvatar(/*ob uid=*/UID::invalidUID(), av.uid, av.pos.toVec4fPoint(), /*aabb_ws_longest_len=*/1.8f, max_dist_for_ob_lod_level, max_dist_for_ob_lod_level, our_avatar_importance_factor, *mat, ob_lod_level, mat->normal_map_url, /*has_alpha=*/false, /*use_sRGB=*/false, /*allow compression=*/false);
	}
}


static void removeAnimatedTextureUse(GLObject& ob, AnimatedTextureManager& animated_texture_manager)
{
	const size_t materials_size = ob.materials.size();
	for(size_t z=0; z<materials_size; ++z)
	{
		OpenGLMaterial& mat = ob.materials[z];

		if(mat.albedo_texture && mat.albedo_texture->hasMultiFrameTextureData())
			animated_texture_manager.removeTextureUse(mat.albedo_texture, &ob, z);

		if(mat.emission_texture && mat.emission_texture->hasMultiFrameTextureData())
			animated_texture_manager.removeTextureUse(mat.emission_texture, &ob, z);

		if(mat.normal_map && mat.normal_map->hasMultiFrameTextureData())
			animated_texture_manager.removeTextureUse(mat.normal_map, &ob, z);

		if(mat.metallic_roughness_texture && mat.metallic_roughness_texture->hasMultiFrameTextureData())
			animated_texture_manager.removeTextureUse(mat.metallic_roughness_texture, &ob, z);
	}
}


void GUIClient::removeAndDeleteGLObjectsForOb(WorldObject& ob)
{
	if(ob.opengl_engine_ob)
	{
		removeAnimatedTextureUse(*ob.opengl_engine_ob, *animated_texture_manager);
		opengl_engine->removeObject(ob.opengl_engine_ob);
	}

	if(ob.opengl_light)
		opengl_engine->removeLight(ob.opengl_light);

	ob.opengl_engine_ob = NULL;

	ob.mesh_manager_data = NULL;

	ob.loading_or_loaded_model_lod_level = -10;
	ob.loading_or_loaded_lod_level = -10;
	ob.using_placeholder_model = false;
}


void GUIClient::removeAndDeleteGLAndPhysicsObjectsForOb(WorldObject& ob)
{
	removeAndDeleteGLObjectsForOb(ob);

	destroyVehiclePhysicsControllingObject(&ob); // Destroy any vehicle controller controlling this object, as vehicle controllers have pointers to physics bodies, which we can't leave dangling.

	if(ob.physics_object.nonNull())
	{
		physics_world->removeObject(ob.physics_object);
		ob.physics_object = NULL;
	}

	ob.mesh_manager_shape_data = NULL;

	// TOOD: removeObScriptingInfo(&ob);
}


void GUIClient::removeAndDeleteGLObjectForAvatar(Avatar& av)
{
	av.graphics.destroy(*opengl_engine);

	av.mesh_data = NULL;
}


/*
60 m is roughly the distance at which a source with volume factor 1 becomes inaudible, with a reasonable system volume level.

intensity I = A^2 / r^2

suppose we have some volume amplitude scale s.

I_s = (s A / r_s)^2
I_s = s^2 (A / r_s)^2

Solving for same intensity for unscaled and scaled source: (I = I_s)
A^2 / r^2 = s^2 A^2 / r_s^2

1 /r^2 = s^2 / r_s^2
r_s^2 / r^2 = s^2

r_s / r = s

*/
static inline float maxAudioDistForSourceVolFactor(float volume_factor)
{
	return 60.f * volume_factor;
}


// Returns false if the resource is already being loaded, or is already loaded into e.g. the opengl engine.
// Also returns false if it's audio that is past the max audio distance etc.
bool GUIClient::isResourceCurrentlyNeededForObjectGivenIsDependency(const URLString& url, const WorldObject* ob) const
{
	// conPrint("isResourceCurrentlyNeededForObjectGivenIsDependency: url: " + url);

	const string_view extension = getExtensionStringView(url);

	if(ImageDecoding::isSupportedImageExtension(extension))
	{
		// If it's already loaded into the opengl engine, don't download it.
		ResourceRef resource = resource_manager->getOrCreateResourceForURL(url); // NOTE: don't want to add resource here ideally.
		const std::string local_path = resource_manager->getLocalAbsPathForResource(*resource);

		const OpenGLTextureKey key(local_path);

		// If we are already processing this file, don't download it.
		if(textures_processing.count(key) > 0)
			return false;

		if(opengl_engine->isOpenGLTextureInsertedForKey(key))
			return false;
	}
	else
	{
		if(ModelLoading::isSupportedModelExtension(extension))
		{
			if(models_processing.count(ModelProcessingKey(url, ob->isDynamic())) > 0)
				return false;
			if(mesh_manager.getMeshData(url))
				return false;
		}
		else
		{
			if(FileTypes::isSupportedAudioFileExtension(extension))
			{
				if(audio_processing.count(url) > 0)
				{
					// conPrint("'" + url + "' is in audio_processing.");
					return false;
				}

				const double ob_dist = ob->pos.getDist(cam_controller.getPosition());
				const bool in_range = ob_dist < maxAudioDistForSourceVolFactor(ob->audio_volume);
				if(!in_range)
				{
					// conPrint("'" + url + "' is not in range.");
					return false;
				}
			}
			else
			{
				if(FileTypes::isSupportedVideoFileExtension(extension))
				{
					const double ob_dist = ob->pos.getDist(cam_controller.getPosition());
					const double max_play_dist = AnimatedTexData::maxVidPlayDist();
					const bool in_range = ob_dist < max_play_dist;
					if(!in_range)
						return false;
				}
				else
				{
					// Else not any kind of valid extension.
					return false;
				}
			}
		}
	}

	// conPrint("isResourceCurrentlyNeededForObjectGivenIsDependency: returning true for url: " + url);
	return true;
}


// Does the resource with the given URL need to be loaded by this object?
// True iff the resource is used by the object, and it's not already loaded or being loaded.
// Called handling ResourceDownloadedMessage
bool GUIClient::isResourceCurrentlyNeededForObject(const URLString& url, const WorldObject* ob) const
{
	// conPrint("isResourceCurrentlyNeededForObject(): " + url + ", ob: " + ob->uid.toString());
	if(!ob->in_proximity)
	{
		// conPrint("!ob->in_proximity");
		return false;
	}

	const int ob_lod_level = ob->current_lod_level;
	if(ob_lod_level < -1)
	{
		// conPrint("ob_lod_level < -1: " + toString(ob->current_lod_level));
		return false;
	}

	glare::ArenaAllocator use_arena = arena_allocator.getFreeAreaArenaAllocator();
	glare::STLArenaAllocator<DependencyURL> stl_arena_allocator(&use_arena);

	WorldObject::GetDependencyOptions options;
	options.use_basis = this->server_has_basis_textures;
	options.include_lightmaps = this->use_lightmaps;
	options.get_optimised_mesh = this->server_has_optimised_meshes;
	options.opt_mesh_version = this->server_opt_mesh_version;
	options.allocator = &use_arena;

	DependencyURLSet dependency_URLs(std::less<DependencyURL>(), stl_arena_allocator);
	ob->getDependencyURLSet(ob_lod_level, options, dependency_URLs);

	if(dependency_URLs.count(DependencyURL(url)) == 0) // TODO: handle sRGB stuff. Current DependencyURL operator < doesn't take into account sRGB etc.
		return false;
	/*bool found = false;
	for(auto it=dependency_URLs.begin(); it != dependency_URLs.end(); ++it)
		if(it->URL == url)
		{
			found = true;
			break;
		}
	if(!found)
	{
		// conPrint("not a dependency.");
		return false;
	}*/

	return isResourceCurrentlyNeededForObjectGivenIsDependency(url, ob);
}


// Does the resource with the given URL need to be loaded?
// Called handling ResourceDownloadedMessage and by EmscriptenResourceDownloader
bool GUIClient::isDownloadingResourceCurrentlyNeeded(const URLString& URL) const
{
	auto res = URL_to_downloading_info.find(URL);
	if(res != URL_to_downloading_info.end())
	{
		const DownloadingResourceInfo& info = res->second;
		if(info.used_by_other || info.used_by_terrain)
			return true;

		// See if the resource is needed by a WorldObject:
		Lock lock(this->world_state->mutex);
		for(size_t i=0; i<info.using_objects.using_object_uids.size(); ++i)
		{
			// Lookup WorldObject for UID
			auto ob_res = world_state->objects.find(info.using_objects.using_object_uids[i]);
			if(ob_res != world_state->objects.end())
			{
				WorldObject* ob = ob_res.getValue().ptr();
				if(isResourceCurrentlyNeededForObject(URL, ob))
					return true;
			}
		}
	}

	return false;
}


// For every resource that the object uses (model, textures etc..), if the resource is not present locally, start downloading it, if we are not already downloading it.
void GUIClient::startDownloadingResourcesForObject(WorldObject* ob, int ob_lod_level)
{
	ZoneScoped; // Tracy profiler
	ZoneText(ob->uid.toString().c_str(), ob->uid.toString().size());

	// conPrint("startDownloadingResourcesForObject: ob_lod_level: " + toString(ob_lod_level));

	glare::ArenaAllocator use_arena = arena_allocator.getFreeAreaArenaAllocator();
	glare::STLArenaAllocator<DependencyURL> stl_arena_allocator(&use_arena);

	WorldObject::GetDependencyOptions options;
	options.use_basis = this->server_has_basis_textures;
	options.include_lightmaps = this->use_lightmaps;
	options.get_optimised_mesh = this->server_has_optimised_meshes;
	options.opt_mesh_version = this->server_opt_mesh_version;
	options.allocator = &use_arena;

	DependencyURLSet dependency_URLs(std::less<DependencyURL>(), stl_arena_allocator);

	ob->getDependencyURLSet(ob_lod_level, options, dependency_URLs);

	for(auto it = dependency_URLs.begin(); it != dependency_URLs.end(); ++it)
	{
		const DependencyURL& url_info = *it;
		const URLString& url = url_info.URL;
		
		if(!resource_manager->isFileForURLPresent(url))
		{
			if(isResourceCurrentlyNeededForObjectGivenIsDependency(url, ob))
			{
				DownloadingResourceInfo info;
				info.texture_params.use_sRGB = url_info.use_sRGB;
				if(url_info.is_lightmap)
				{
					// conPrint("Resource '" + url + "' is a lightmap, setting use_mipmaps false etc.");
					// This code is also in startLoadingTexturesForObject()
					info.texture_params.filtering = OpenGLTexture::Filtering_Bilinear;
					info.texture_params.use_mipmaps = false;
				}

				info.build_dynamic_physics_ob = ob->isDynamic();
				info.pos = ob->pos;
				info.size_factor = LoadItemQueueItem::sizeFactorForAABBWS(ob->getAABBWSLongestLength(), /*importance_factor=*/1.f);
				info.using_objects.using_object_uids.push_back(ob->uid);

				// Copy URL to one not allocated from arena.
				startDownloadingResource(URLString(url.begin(), url.end()), ob->getCentroidWS(), ob->getAABBWSLongestLength(), info);
			}
		}
	}
}


void GUIClient::startDownloadingResourcesForAvatar(Avatar* ob, int ob_lod_level, bool our_avatar)
{
	glare::ArenaAllocator use_arena = arena_allocator.getFreeAreaArenaAllocator();
	glare::STLArenaAllocator<DependencyURL> stl_arena_allocator(&use_arena);

	Avatar::GetDependencyOptions options;
	options.get_optimised_mesh = this->server_has_optimised_meshes;
	options.use_basis = this->server_has_basis_textures;
	options.opt_mesh_version = this->server_opt_mesh_version;

	DependencyURLSet dependency_URLs(std::less<DependencyURL>(), stl_arena_allocator);

	ob->getDependencyURLSet(ob_lod_level, options, dependency_URLs);

	for(auto it = dependency_URLs.begin(); it != dependency_URLs.end(); ++it)
	{
		const DependencyURL& url_info = *it;
		const URLString& url = url_info.URL;

		const bool has_video_extension = FileTypes::hasSupportedVideoFileExtension(url);

		if(has_video_extension || ImageDecoding::hasSupportedImageExtension(url) || ModelLoading::hasSupportedModelExtension(url))
		{
			// Only download mp4s if the camera is near them in the world.
			bool in_range = true;
			if(has_video_extension)
			{
				const double ob_dist = ob->pos.getDist(cam_controller.getPosition());
				const double max_play_dist = AnimatedTexData::maxVidPlayDist();
				in_range = ob_dist < max_play_dist;
			}

			if(in_range && !resource_manager->isFileForURLPresent(url))// && !stream)
			{
				const float our_avatar_importance_factor = our_avatar ? 1.0e4f : 1.f;

				DownloadingResourceInfo info;
				info.texture_params.use_sRGB = url_info.use_sRGB;
				info.build_dynamic_physics_ob = false;
				info.pos = ob->pos;
				info.size_factor = LoadItemQueueItem::sizeFactorForAABBWS(/*aabb_ws_longest_len=*/1.8f, our_avatar_importance_factor);
				info.used_by_other = true;

				// Copy URL to one not allocated from arena.
				startDownloadingResource(URLString(url.begin(), url.end()), ob->pos.toVec4fPoint(), /*aabb_ws_longest_len=*/1.8f, info);
			}
		}
	}
}


// For when the desired texture LOD is not loaded, pick another texture LOD that is loaded (if it exists).
// Prefer lower LOD levels (more detail).
static Reference<OpenGLTexture> getBestTextureLOD(const WorldMaterial& world_mat, const OpenGLTextureKey& base_tex_path, bool tex_has_alpha, bool use_sRGB, bool use_basis, OpenGLEngine& opengl_engine, glare::ArenaAllocator* allocator)
{
	for(int lvl=world_mat.minLODLevel(); lvl<=2; ++lvl)
	{
		const WorldMaterial::GetURLOptions get_url_options(use_basis, allocator);
		const OpenGLTextureKey tex_lod_path = world_mat.getLODTexturePathForLevel(get_url_options, base_tex_path, lvl, tex_has_alpha);
		Reference<OpenGLTexture> tex = opengl_engine.getTextureIfLoaded(tex_lod_path);
		if(tex.nonNull())
			return tex;
	}

	return Reference<OpenGLTexture>();
}


static Reference<OpenGLTexture> getBestLightmapLOD(const OpenGLTextureKey& base_lightmap_path, OpenGLEngine& opengl_engine)
{
	for(int lvl=0; lvl<=2; ++lvl)
	{
		const OpenGLTextureKey tex_lod_path = WorldObject::getLODLightmapPathForLevel(base_lightmap_path, lvl);
		Reference<OpenGLTexture> tex = opengl_engine.getTextureIfLoaded(tex_lod_path);
		if(tex.nonNull())
			return tex;
	}

	return Reference<OpenGLTexture>();
}


// If not a mp4 texture - we won't have LOD levels for mp4 textures, just keep the texture vid playback writes to.
static inline bool isNonEmptyAndNotMp4(const string_view path)
{
	return !path.empty() && !::hasExtension(path, "mp4");
}


static void checkAssignBestOpenGLTexture(OpenGLTextureRef& opengl_texture, const WorldMaterial* world_mat, const URLString& texture_URL, const OpenGLTextureKey& desired_tex_path, GLObject* ob, size_t mat_index, OpenGLEngine& opengl_engine, 
	ResourceManager& resource_manager, AnimatedTextureManager& animated_texture_manager, glare::ArenaAllocator* allocator, bool use_basis, bool tex_has_alpha, bool use_sRGB, bool& mat_changed_out)
{
	if(isNonEmptyAndNotMp4(desired_tex_path))
	{
		try
		{
			if(!opengl_texture || (opengl_texture->key != desired_tex_path)) // If the desired texture is not currently assigned:
			{
				OpenGLTextureRef new_tex = opengl_engine.getTextureIfLoaded(desired_tex_path); // Try and get desired texture

				// If the desired texture isn't loaded, try and use a different LOD level of the texture, that is actually loaded.
				if(!new_tex && world_mat)
				{
					glare::STLArenaAllocator<char> stl_allocator(allocator);
					OpenGLTextureKey base_tex_path(stl_allocator);
					resource_manager.getTexPathForURL(texture_URL, /*path out=*/base_tex_path);

					new_tex = getBestTextureLOD(*world_mat, base_tex_path, tex_has_alpha, /*use_sRGB=*/use_sRGB, use_basis, opengl_engine, allocator); 
				}

				if(new_tex != opengl_texture) // If we have a new texture to assign to opengl_texture:
				{
					// If old texture was animated, remove use of the animated texture from the animated texture manager
					if(opengl_texture && opengl_texture->hasMultiFrameTextureData())
						animated_texture_manager.removeTextureUse(opengl_texture, ob, mat_index);

					opengl_texture = new_tex;

					mat_changed_out = true;

					// If new texture is animated, add use to animated texture manager
					if(opengl_texture && opengl_texture->hasMultiFrameTextureData())
						animated_texture_manager.checkAddTextureUse(opengl_texture, ob, mat_index);
				}
			}
		}
		catch(glare::Exception& e)
		{
			conPrint("error loading texture: " + e.what());
		}
	}
}


// Update textures to correct LOD-level textures.
// Try and use the texture with the target LOD level first (given by e.g. opengl_mat.tex_path).
// If that texture is not currently loaded into the OpenGL Engine, then use another texture LOD that is loaded, as chosen in getBestTextureLOD().
static void doAssignLoadedOpenGLTexturesToMats(WorldObject* ob, bool use_basis, bool use_lightmaps, OpenGLEngine& opengl_engine, ResourceManager& resource_manager, 
	AnimatedTextureManager& animated_texture_manager, glare::ArenaAllocator* allocator)
{
	ZoneScoped; // Tracy profiler

	if(!ob->opengl_engine_ob)
		return;

	for(size_t z=0; z<ob->opengl_engine_ob->materials.size(); ++z)
	{
		OpenGLMaterial& opengl_mat = ob->opengl_engine_ob->materials[z];
		const WorldMaterial* world_mat = (z < ob->materials.size()) ? ob->materials[z].ptr() : NULL;

		bool mat_changed = false;

		checkAssignBestOpenGLTexture(opengl_mat.albedo_texture, world_mat, world_mat ? world_mat->colour_texture_url : URLString(), opengl_mat.tex_path, ob->opengl_engine_ob.ptr(), z, opengl_engine, resource_manager, animated_texture_manager, allocator, use_basis, 
			/*tex has alpha=*/world_mat ? world_mat->colourTexHasAlpha() : false, /*use sRBB=*/true, mat_changed);

		checkAssignBestOpenGLTexture(opengl_mat.emission_texture, world_mat, world_mat ? world_mat->emission_texture_url : URLString(), opengl_mat.emission_tex_path, ob->opengl_engine_ob.ptr(), z, opengl_engine, resource_manager, animated_texture_manager, allocator,use_basis, 
			/*tex has alpha=*/false, /*use sRBB=*/true, mat_changed);

		checkAssignBestOpenGLTexture(opengl_mat.metallic_roughness_texture, world_mat, world_mat ? world_mat->roughness.texture_url : URLString(), opengl_mat.metallic_roughness_tex_path, ob->opengl_engine_ob.ptr(), z, opengl_engine, resource_manager, animated_texture_manager, allocator,use_basis, 
			/*tex has alpha=*/false, /*use sRBB=*/false, mat_changed);

		checkAssignBestOpenGLTexture(opengl_mat.normal_map, world_mat, world_mat ? world_mat->normal_map_url : URLString(), opengl_mat.normal_map_path, ob->opengl_engine_ob.ptr(), z, opengl_engine, resource_manager, animated_texture_manager, allocator, use_basis, 
			/*tex has alpha=*/false, /*use_sRGB=*/false, mat_changed);

		if(use_lightmaps && isValidLightMapURL(opengl_engine, opengl_mat.lightmap_path))
		{
			try
			{
				if(!opengl_mat.lightmap_texture || (opengl_mat.lightmap_texture->key != opengl_mat.lightmap_path)) // If the desired texture it not loaded:
				{
					OpenGLTextureRef new_tex = opengl_engine.getTextureIfLoaded(opengl_mat.lightmap_path);
					if(!new_tex) // If this texture is not loaded into the OpenGL engine:
					{
						// Get local base tex path for lightmap URL
						glare::STLArenaAllocator<char> stl_allocator(allocator);
						OpenGLTextureKey base_tex_path(stl_allocator);
						resource_manager.getTexPathForURL(ob->lightmap_url, /*path out=*/base_tex_path);

						new_tex = getBestLightmapLOD(base_tex_path, opengl_engine); // Try and use a different LOD level of the lightmap, that is actually loaded.
					}

					if(new_tex != opengl_mat.lightmap_texture)
					{
						opengl_mat.lightmap_texture = new_tex;
						mat_changed = true;
					}
				}
			}
			catch(glare::Exception& e)
			{
				conPrint("error loading texture: " + e.what());
			}
		}
		else
			opengl_mat.lightmap_texture = NULL;

		if(mat_changed)
			opengl_engine.materialTextureChanged(*ob->opengl_engine_ob, opengl_mat);
	}
}


void GUIClient::assignLoadedOpenGLTexturesToMats(WorldObject* ob)
{
	ZoneScoped; // Tracy profiler

	glare::ArenaAllocator use_arena = arena_allocator.getFreeAreaArenaAllocator();

	doAssignLoadedOpenGLTexturesToMats(ob, /*use_basis=*/this->server_has_basis_textures, this->use_lightmaps, *opengl_engine, *resource_manager, *animated_texture_manager, &use_arena);
}


// For avatars
static void assignLoadedOpenGLTexturesToAvatarMats(Avatar* av, bool use_basis, OpenGLEngine& opengl_engine, ResourceManager& resource_manager, AnimatedTextureManager& animated_texture_manager, glare::ArenaAllocator* allocator)
{
	ZoneScoped; // Tracy profiler

	GLObject* gl_ob = av->graphics.skinned_gl_ob.ptr();
	if(!gl_ob)
		return;

	for(size_t z=0; z<gl_ob->materials.size(); ++z)
	{
		OpenGLMaterial& opengl_mat = gl_ob->materials[z];
		const WorldMaterial* world_mat = (z < av->avatar_settings.materials.size()) ? av->avatar_settings.materials[z].ptr() : NULL;

		bool mat_changed = false;

		checkAssignBestOpenGLTexture(opengl_mat.albedo_texture, world_mat, world_mat ? world_mat->colour_texture_url : URLString(), opengl_mat.tex_path, gl_ob, z, opengl_engine, resource_manager, animated_texture_manager, allocator, use_basis, 
			/*tex has alpha=*/world_mat ? world_mat->colourTexHasAlpha() : false, /*use sRBB=*/true, mat_changed);

		checkAssignBestOpenGLTexture(opengl_mat.emission_texture, world_mat, world_mat ? world_mat->emission_texture_url : URLString(), opengl_mat.emission_tex_path, gl_ob, z, opengl_engine, resource_manager, animated_texture_manager, allocator, use_basis, 
			/*tex has alpha=*/false, /*use sRBB=*/true, mat_changed);

		checkAssignBestOpenGLTexture(opengl_mat.metallic_roughness_texture, world_mat, world_mat ? world_mat->roughness.texture_url : URLString(), opengl_mat.metallic_roughness_tex_path, gl_ob, z, opengl_engine, resource_manager, animated_texture_manager, allocator, use_basis, 
			/*tex has alpha=*/false, /*use sRBB=*/false, mat_changed);

		checkAssignBestOpenGLTexture(opengl_mat.normal_map, world_mat, world_mat ? world_mat->normal_map_url : URLString(), opengl_mat.normal_map_path, gl_ob, z, opengl_engine, resource_manager, animated_texture_manager, allocator, use_basis, 
			/*tex has alpha=*/false, /*use_sRGB=*/false, mat_changed);
		
		if(mat_changed)
			opengl_engine.materialTextureChanged(*gl_ob, opengl_mat);
	}
}


// Compute approximate spectral radiance of the emitter, from the given luminous flux, multiplied by 1.0e-9 (to avoid precision issues in shaders)
static Colour4f computeSpotlightColour(const WorldObject& ob, float cone_cos_angle_start, float cone_cos_angle_end, float& scale_out)
{
	if(ob.materials.size() >= 1 && ob.materials[0].nonNull())
	{
		/*
		Compute approximate spectral radiance of the emitter, from the given luminous flux.
		Assume emitter spectral radiance L_e is constant (independent of wavelength).
		Let cone (half) angle = alpha.
		Solid angle of cone = 2pi(1 - cos(alpha))

		Phi_v = 683 integral(y_bar(lambda) L_e A 2pi(1 - cos(alpha))) dlambda						[683 comes from definition of luminous flux]
		Phi_v = 683 L_e A 2pi(1 - cos(alpha)) integral(y_bar(lambda)) dlambda
		
		integral(y_bar(lambda)) dlambda ~= 106 nm [From Indigo - e.g. average luminous efficiency is ~ 0.3 over 400 to 700 nm]
		so

		Phi_v = 683 L_e A 2pi(1 - cos(alpha)) (106 * 10^-9)
		
		Assume A = 1, then

		Phi_v = 683 * 106 * 10^-9 * L_e * 2pi(1 - cos(alpha))

		or

		L_e = 1 / (683 * 106 * 10^-9 * 2pi(1 - cos(alpha))
		*/
		const float use_cone_angle = (std::acos(cone_cos_angle_start) + std::acos(cone_cos_angle_end)) * 0.5f; // Average of start and end cone angles.
		const float L_e = ob.materials[0]->emission_lum_flux_or_lum / (683.002f * 106.856e-9f * Maths::get2Pi<float>() * (1 - cos(use_cone_angle))) * 1.0e-9f;
		scale_out = L_e;
		return Colour4f(ob.materials[0]->colour_rgb.r * L_e, ob.materials[0]->colour_rgb.g * L_e, ob.materials[0]->colour_rgb.b * L_e, 1.f);
	}
	else
	{
//		assert(0);
		scale_out = 0;
		return Colour4f(0.f);
	}
}


void GUIClient::createGLAndPhysicsObsForText(const Matrix4f& ob_to_world_matrix, WorldObject* ob, bool use_materialise_effect, PhysicsObjectRef& physics_ob_out, GLObjectRef& opengl_ob_out)
{
	ZoneScoped; // Tracy profiler

	Rect2f rect_os;
	OpenGLTextureRef atlas_texture;

	const std::string use_text = ob->content.empty() ? " " : UTF8Utils::sanitiseUTF8String(ob->content);

	const int font_size_px = 42;

	std::vector<GLUIText::CharPositionInfo> char_positions_font_coords;
	Reference<OpenGLMeshRenderData> meshdata = GLUIText::makeMeshDataForText(opengl_engine, gl_ui->font_char_text_cache.ptr(), gl_ui->getFonts(), gl_ui->getEmojiFonts(), use_text, 
		/*font size px=*/font_size_px, /*vert_pos_scale=*/(1.f / font_size_px), /*render SDF=*/true, this->stack_allocator, rect_os, atlas_texture, char_positions_font_coords);

	// We will make a physics object that has the same dimensions in object space as the text mesh vertices.  This means we can use the same pos, rot and scale
	// for the physics object as for the opengl object.
	// We will do this by starting with the unit quad (text_quad_shape) and applying scaling and translating decorators to it (in createScaledAndTranslatedShapeForShape)
	// The other way we could do it is by creating a non-unit quad mesh Jolt shape, but I think the decorated unit quad will be faster.

	PhysicsObjectRef physics_ob = new PhysicsObject(/*collidable=*/false);
	const Vec3f scale(rect_os.getWidths().x, rect_os.getWidths().y, 1.f);
	const Vec3f translation(rect_os.getMin().x, rect_os.getMin().y, 0);
	physics_ob->shape = PhysicsWorld::createScaledAndTranslatedShapeForShape(this->text_quad_shape, translation, scale);
	physics_ob->is_sensor = ob->isSensor();
	physics_ob->userdata = ob;
	physics_ob->userdata_type = 0;
	physics_ob->ob_uid = ob->uid;
	physics_ob->pos = ob->pos.toVec4fPoint();
	physics_ob->rot = Quatf::fromAxisAndAngle(normalise(ob->axis), ob->angle);
	physics_ob->scale = useScaleForWorldOb(/*physics_quad_scale*/ob->scale);


	GLObjectRef opengl_ob = opengl_engine->allocateObject();
	opengl_ob->mesh_data = meshdata;
	opengl_ob->materials.resize(1);
	OpenGLMaterial& gl_mat_0 = opengl_ob->materials[0];

	if(ob->materials.size() >= 1)
		ModelLoading::setGLMaterialFromWorldMaterial(*ob->materials[0], /*ob_lod_level*/0, ob->lightmap_url, /*use_basis=*/this->server_has_basis_textures, *this->resource_manager, gl_mat_0);


	gl_mat_0.alpha_blend = true; // Make use alpha blending
	gl_mat_0.sdf_text = true;

	if(ob->materials.size() >= 1)
	{
		gl_mat_0.alpha = ob->materials[0]->opacity.val;
		gl_mat_0.transparent = BitUtils::isBitSet(ob->materials[0]->flags, WorldMaterial::HOLOGRAM_FLAG);

		if(ob->materials[0]->emission_lum_flux_or_lum > 0)
			gl_mat_0.emission_texture = atlas_texture;

		if(BitUtils::isBitSet(ob->materials[0]->flags, WorldMaterial::HOLOGRAM_FLAG))
			gl_mat_0.alpha_blend = false;

	}

	gl_mat_0.tex_matrix = Matrix2f::identity();
	//gl_mat_0.albedo_texture = atlas_texture;
	gl_mat_0.transmission_texture = atlas_texture;



	opengl_ob->ob_to_world_matrix = ob_to_world_matrix;

	gl_mat_0.materialise_effect = use_materialise_effect;
	gl_mat_0.materialise_start_time = ob->materialise_effect_start_time;

	physics_ob_out = physics_ob;
	opengl_ob_out = opengl_ob;
}


void GUIClient::printFromLuaScript(LuaScript* script, const char* s, size_t len)
{
	// If this is our script, print message to console and log

	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;
	if(script_evaluator->world_object->creator_id == this->logged_in_user_id)
	{
		const std::string msg(s, len);

		const std::string augmented_msg = "Lua (ob " + script_evaluator->world_object->uid.toString() + "): " + std::string(s, len);
		conPrint(augmented_msg);
		logMessage(augmented_msg);

		// Pass to UIInterface to show on script editor window if applicable
		ui_interface->printFromLuaScript(msg, script_evaluator->world_object->uid);
	}
}


void GUIClient::errorOccurredFromLuaScript(LuaScript* script, const std::string& msg)
{
	// If this is our script, print message to console and log

	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;
	if(script_evaluator->world_object->creator_id == this->logged_in_user_id)
	{
		const std::string full_msg = "Lua error (ob " + script_evaluator->world_object->uid.toString() + "): " + msg + "\nScript will be disabled.";
		conPrint(full_msg);
		logMessage(full_msg);

		// Pass to UIInterface to show on script editor window if applicable
		ui_interface->luaErrorOccurred(msg, script_evaluator->world_object->uid);
	}
}


// Load or reload an object's 3d model.
// 
// Check if the model file is downloaded.
// If so, load the model into the OpenGL and physics engines.
// If not, set a placeholder model and queue up the model download.
// Also enqueue any downloads for missing resources such as textures.
//
// Also called from checkForLODChanges() when the object LOD level changes, and so we may need to load a new model and/or textures.
void GUIClient::loadModelForObject(WorldObject* ob, WorldStateLock& world_state_lock)
{
	// conPrint("loadModelForObject(): UID: " + ob->uid.toString());
	const Vec4f campos = cam_controller.getPosition().toVec4fPoint();

	// Check object is in proximity.  Otherwise we might load objects outside of proximity, for example large objects transitioning from LOD level 1 to LOD level 2 or vice-versa.
	if(!ob->in_proximity)
		return;

	const int ob_lod_level = ob->getLODLevel(campos);
	
	// If we have a model loaded, that is not the placeholder model, and it has the correct LOD level, we don't need to do anything.
	//if(ob->opengl_engine_ob.nonNull() && !ob->using_placeholder_model && (ob->loaded_model_lod_level == ob_model_lod_level) && (ob->/*loaded_lod_level*/loading_lod_level == ob_lod_level))
	//	return;

	// If ob->loaded_model_lod_level == ob_model_lod_level, we may still have a different object lod level, and hence want to continue as may be loading different texture lod levels.

	if(ob->loading_or_loaded_lod_level == ob_lod_level)
		return;

	ob->loading_or_loaded_lod_level = ob_lod_level;

	// Object LOD level is in [-1, 2].
	// model LOD level is in [0, ob->max_model_lod_level], which is {0} or [0, 2].

	const int ob_model_lod_level = myClamp(ob_lod_level, 0, ob->max_model_lod_level);

	// Compute the maximum distance from the camera at which the object LOD level will remain what it currently is.
	const float max_dist_for_ob_lod_level = ob->getMaxDistForLODLevel(ob_lod_level);
	
	assert(max_dist_for_ob_lod_level >= campos.getDist(ob->getCentroidWS()));

	// Compute a similar value, the maximum distance from the camera at which the object LOD level will remain what it currently is, or at which the object LOD level is zero.
	// This is used for materials that have a minimum lod level of 0.  In this case textures loaded at lod level -1 can still be used at lod level 0.
	float max_dist_for_ob_lod_level_clamped_0;
	if(ob_lod_level == -1)
		max_dist_for_ob_lod_level_clamped_0 = ob->getMaxDistForLODLevel(0);
	else
		max_dist_for_ob_lod_level_clamped_0 = max_dist_for_ob_lod_level;

	// For objects with max_model_lod_level=0 (e.g. objects with simple meshes like cubes), the mesh is valid at all object LOD levels, so has no max distance.
	float max_dist_for_ob_model_lod_level;
	if(ob->max_model_lod_level == 0)
		max_dist_for_ob_model_lod_level = std::numeric_limits<float>::max();
	else
		max_dist_for_ob_model_lod_level = ob->getMaxDistForLODLevel(ob_model_lod_level); // NOTE that ob_lod_level may be -1 and ob_model_lod_level = 0, so can't just use max_dist_for_ob_lod_level.


	// conPrint("Loading model for ob: UID: " + ob->uid.toString() + ", type: " + WorldObject::objectTypeString((WorldObject::ObjectType)ob->object_type) + ", model URL: " + ob->model_url + ", ob_model_lod_level: " + toString(ob_model_lod_level));
	Timer timer;

	//ui->glWidget->makeCurrent();

	try
	{
		checkTransformOK(ob); // Throws glare::Exception if not ok.

		const Matrix4f ob_to_world_matrix = obToWorldMatrix(*ob);

		// Start downloading any resources we don't have that the object uses.
		startDownloadingResourcesForObject(ob, ob_lod_level);

		startLoadingTexturesForObject(*ob, ob_lod_level, max_dist_for_ob_lod_level, max_dist_for_ob_lod_level_clamped_0);

		//ob->/*loaded_lod_level*/loading_lod_level = ob_lod_level;

		// Add any objects with gif or mp4 textures to the set of animated objects. (if not already)
		for(size_t i=0; i<ob->materials.size(); ++i)
		{
			if(	::hasExtension(ob->materials[i]->colour_texture_url,   "mp4") ||
				::hasExtension(ob->materials[i]->emission_texture_url, "mp4"))
			{
				if(ob->animated_tex_data.isNull())
				{
					ob->animated_tex_data = new AnimatedTexObData();
					this->obs_with_animated_tex.insert(ob);
				}

				ob->animated_tex_data->rescanObjectForAnimatedTextures(opengl_engine.ptr(), ob, rng, *animated_texture_manager);
			}
		}

		const float current_time = (float)Clock::getTimeSinceInit();
		const bool use_materialise_effect = ob->use_materialise_effect_on_load && (current_time - ob->materialise_effect_start_time < 2.0f);
		
		bool load_placeholder = false;

		if(ob->object_type == WorldObject::ObjectType_Hypercard)
		{
			if(ob->opengl_engine_ob.isNull())
			{
				assert(ob->physics_object.isNull());

				PhysicsObjectRef physics_ob = new PhysicsObject(/*collidable=*/true);
				physics_ob->shape = this->hypercard_quad_shape;
				physics_ob->is_sensor = ob->isSensor();
				physics_ob->userdata = ob;
				physics_ob->userdata_type = 0;
				physics_ob->ob_uid = ob->uid;
				physics_ob->pos = ob->pos.toVec4fPoint();
				physics_ob->rot = Quatf::fromAxisAndAngle(normalise(ob->axis), ob->angle);
				physics_ob->scale = useScaleForWorldOb(ob->scale);

				GLObjectRef opengl_ob = opengl_engine->allocateObject();
				opengl_ob->mesh_data = this->hypercard_quad_opengl_mesh;
				opengl_ob->materials.resize(1);
				opengl_ob->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(0.85f));
				opengl_ob->materials[0].tex_matrix = Matrix2f(1, 0, 0, -1); // OpenGL expects texture data to have bottom left pixel at offset 0, we have top left pixel, so flip
				opengl_ob->materials[0].simple_double_sided = true;
				opengl_ob->materials[0].convert_albedo_from_srgb = true; // Since we use a single-channel texture for hypercards, we can't use a GL sRGB format, so need to enable this conversion option.
				opengl_ob->ob_to_world_matrix = ob_to_world_matrix;


				const OpenGLTextureKey tex_key = OpenGLTextureKey("_HYPCRD_") + OpenGLTextureKey(toString(XXH64(ob->content.data(), ob->content.size(), 1)));

				// If the hypercard texture is already loaded, use it
				opengl_ob->materials[0].albedo_texture = opengl_engine->getTextureIfLoaded(tex_key);
				opengl_ob->materials[0].tex_path = tex_key;

				if(opengl_ob->materials[0].albedo_texture.isNull())
				{
					const bool just_added = checkAddTextureToProcessingSet(tex_key);
					if(just_added) // not being loaded already:
					{
						Reference<MakeHypercardTextureTask> task = new MakeHypercardTextureTask();
						task->tex_key = tex_key;
						task->result_msg_queue = &this->msg_queue;
						task->hypercard_content = ob->content;
						task->opengl_engine = opengl_engine;
						task->fonts = this->gl_ui->getFonts();
						task->worker_allocator = worker_allocator;
						task->texture_loaded_msg_allocator = texture_loaded_msg_allocator;
						task->upload_thread = opengl_upload_thread;
						load_item_queue.enqueueItem(/*key=*/URLString(tex_key), *ob, task, /*max_dist_for_ob_lod_level=*/200.f);
					}

					loading_texture_key_to_hypercard_UID_map[tex_key].insert(ob->uid);
				}

				opengl_ob->materials[0].materialise_effect = use_materialise_effect;
				opengl_ob->materials[0].materialise_start_time = ob->materialise_effect_start_time;


				ob->opengl_engine_ob = opengl_ob;
				ob->physics_object = physics_ob;

				opengl_engine->addObject(ob->opengl_engine_ob);

				physics_world->addObject(ob->physics_object);
			}
		}
		else if(ob->object_type == WorldObject::ObjectType_Text)
		{
			if(ob->opengl_engine_ob.isNull())
			{
				assert(ob->physics_object.isNull());

				BitUtils::zeroBit(ob->changed_flags, WorldObject::CONTENT_CHANGED);

				recreateTextGraphicsAndPhysicsObs(ob);

				loadScriptForObject(ob, world_state_lock); // Load any script for the object.
			}
		}
		else if(ob->object_type == WorldObject::ObjectType_Spotlight)
		{
			if(ob->opengl_engine_ob.isNull())
			{
				assert(ob->physics_object.isNull());

				PhysicsObjectRef physics_ob = new PhysicsObject(/*collidable=*/true);
				physics_ob->shape = this->spotlight_shape;
				physics_ob->is_sensor = ob->isSensor();
				physics_ob->userdata = ob;
				physics_ob->userdata_type = 0;
				physics_ob->ob_uid = ob->uid;
				physics_ob->pos = ob->pos.toVec4fPoint();
				physics_ob->rot = Quatf::fromAxisAndAngle(normalise(ob->axis), ob->angle);
				physics_ob->scale = useScaleForWorldOb(ob->scale);

				GLObjectRef opengl_ob = opengl_engine->allocateObject();
				opengl_ob->mesh_data = this->spotlight_opengl_mesh;
				
				// Use material[1] from the WorldObject as the light housing GL material.
				opengl_ob->materials.resize(2);
				if(ob->materials.size() >= 2)
					ModelLoading::setGLMaterialFromWorldMaterial(*ob->materials[1], /*lod level=*/ob_lod_level, /*lightmap URL=*/"", /*use_basis=*/this->server_has_basis_textures, *resource_manager, /*open gl mat=*/opengl_ob->materials[0]);
				else
					opengl_ob->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(0.85f));

				opengl_ob->materials[0].materialise_effect = use_materialise_effect;
				opengl_ob->materials[0].materialise_start_time = ob->materialise_effect_start_time;

				opengl_ob->ob_to_world_matrix = ob_to_world_matrix;

				GLLightRef light = new GLLight();
				light->gpu_data.pos = ob->pos.toVec4fPoint();
				light->gpu_data.dir = normalise(ob_to_world_matrix * Vec4f(0, 0, -1, 0));
				light->gpu_data.light_type = 1; // spotlight
				light->gpu_data.cone_cos_angle_start = 0.9f;
				light->gpu_data.cone_cos_angle_end = 0.95f;
				float scale;
				light->gpu_data.col = computeSpotlightColour(*ob, light->gpu_data.cone_cos_angle_start, light->gpu_data.cone_cos_angle_end, scale);
				light->max_light_dist = myMin(15.f, 4.f * myMax(light->gpu_data.col[0], light->gpu_data.col[1], light->gpu_data.col[2]));
				
				// Apply a light emitting material to the light surface material in the spotlight model.
				if(ob->materials.size() >= 1)
				{
					opengl_ob->materials[1].emission_linear_rgb = toLinearSRGB(ob->materials[0]->colour_rgb);
					opengl_ob->materials[1].emission_scale = scale;
				}


				ob->opengl_engine_ob = opengl_ob;
				ob->opengl_light = light;
				ob->physics_object = physics_ob;

				opengl_engine->addObject(ob->opengl_engine_ob);
				opengl_engine->addLight(ob->opengl_light);

				physics_world->addObject(ob->physics_object);

				loadScriptForObject(ob, world_state_lock); // Load any script for the object.
			}
		}
		else if(ob->object_type == WorldObject::ObjectType_WebView)
		{
			if(ob->opengl_engine_ob.isNull())
			{
				assert(ob->physics_object.isNull());

				PhysicsObjectRef physics_ob = new PhysicsObject(/*collidable=*/true);
				physics_ob->shape = this->image_cube_shape;
				physics_ob->is_sensor = ob->isSensor();
				physics_ob->userdata = ob;
				physics_ob->userdata_type = 0;
				physics_ob->ob_uid = ob->uid;
				physics_ob->pos = ob->pos.toVec4fPoint();
				physics_ob->rot = Quatf::fromAxisAndAngle(normalise(ob->axis), ob->angle);
				physics_ob->scale = useScaleForWorldOb(ob->scale);

				GLObjectRef opengl_ob = opengl_engine->allocateObject();
				opengl_ob->mesh_data = this->image_cube_opengl_mesh;
				opengl_ob->materials.resize(2);
				opengl_ob->materials[0].albedo_linear_rgb = Colour3f(0.f);
				opengl_ob->materials[0].emission_linear_rgb = Colour3f(1.f);
				const float luminance = 24000; // nits.  Chosen so videos look about the right brightness in daylight.
				opengl_ob->materials[0].emission_scale = luminance / (683.002f * 106.856e-9f) * 1.0e-9f; // See ModelLoading::setGLMaterialFromWorldMaterialWithLocalPaths().  1.0e-9f factor to avoid floating point issues.
				opengl_ob->materials[0].tex_matrix = Matrix2f(1, 0, 0, -1); // OpenGL expects texture data to have bottom left pixel at offset 0, we have top left pixel, so flip
				opengl_ob->materials[0].materialise_effect = use_materialise_effect;
				opengl_ob->materials[0].materialise_start_time = ob->materialise_effect_start_time;
				opengl_ob->ob_to_world_matrix = ob_to_world_matrix;

				ob->opengl_engine_ob = opengl_ob;
				ob->physics_object = physics_ob;

				opengl_engine->addObject(ob->opengl_engine_ob);

				physics_world->addObject(ob->physics_object);

				ob->web_view_data = new WebViewData();
				//connect(ob->web_view_data.ptr(), SIGNAL(linkHoveredSignal(const QString&)), this, SLOT(webViewDataLinkHovered(const QString&)));
				//connect(ob->web_view_data.ptr(), SIGNAL(mouseDoubleClickedSignal(QMouseEvent*)), this, SLOT(webViewMouseDoubleClicked(QMouseEvent*)));
				this->web_view_obs.insert(ob);
			}
		}
		else if(ob->object_type == WorldObject::ObjectType_Video)
		{
			if(ob->opengl_engine_ob.isNull())
			{
				assert(ob->physics_object.isNull());

				PhysicsObjectRef physics_ob = new PhysicsObject(/*collidable=*/true);
				physics_ob->shape = this->image_cube_shape;
				physics_ob->is_sensor = ob->isSensor();
				physics_ob->userdata = ob;
				physics_ob->userdata_type = 0;
				physics_ob->ob_uid = ob->uid;
				physics_ob->pos = ob->pos.toVec4fPoint();
				physics_ob->rot = Quatf::fromAxisAndAngle(normalise(ob->axis), ob->angle);
				physics_ob->scale = useScaleForWorldOb(ob->scale);

				GLObjectRef opengl_ob = opengl_engine->allocateObject();
				opengl_ob->mesh_data = this->image_cube_opengl_mesh;
				opengl_ob->materials.resize(2);
				opengl_ob->materials[0].albedo_linear_rgb = Colour3f(0.f);
				opengl_ob->materials[0].emission_linear_rgb = Colour3f(1.f);
				const float luminance = 24000; // nits.  Chosen so videos look about the right brightness in daylight.
				opengl_ob->materials[0].emission_scale = luminance / (683.002f * 106.856e-9f) * 1.0e-9f; // See ModelLoading::setGLMaterialFromWorldMaterialWithLocalPaths()  1.0e-9f factor to avoid floating point issues.
				opengl_ob->materials[0].tex_matrix = Matrix2f(1, 0, 0, -1); // OpenGL expects texture data to have bottom left pixel at offset 0, we have top left pixel, so flip
				opengl_ob->materials[0].materialise_effect = use_materialise_effect;
				opengl_ob->materials[0].materialise_start_time = ob->materialise_effect_start_time;
				opengl_ob->ob_to_world_matrix = ob_to_world_matrix;

				ob->opengl_engine_ob = opengl_ob;
				ob->physics_object = physics_ob;

				opengl_engine->addObject(ob->opengl_engine_ob);

				physics_world->addObject(ob->physics_object);

				ob->browser_vid_player = new BrowserVidPlayer();
				this->browser_vid_player_obs.insert(ob);
			}
		}
		else if(ob->object_type == WorldObject::ObjectType_VoxelGroup)
		{
			if(ob->loading_or_loaded_model_lod_level != ob_model_lod_level) // We may already have the correct LOD model loaded (or being loaded), don't reload if so.
			{
				ob->loading_or_loaded_model_lod_level = ob_model_lod_level;

				if(!ob->getCompressedVoxels() || ob->getCompressedVoxels()->size() == 0)
					throw glare::Exception("zero voxels");

				const uint64 hash = ob->compressed_voxels_hash;
				assert(hash == XXH64(ob->getCompressedVoxels()->data(), ob->getCompressedVoxels()->dataSizeBytes(), 1));

				bool added_opengl_ob = false;
				if(hash == 1933977005784225319ull)
				{
					//conPrint("Using single voxel meshdata");
				
					// If this is a single voxel at (0,0,0) with mat index 0:
					loadPresentObjectGraphicsAndPhysicsModels(ob, single_voxel_meshdata, single_voxel_shapedata, ob_lod_level, ob_model_lod_level, /*voxel_subsample_factor=*/1, world_state_lock);
					added_opengl_ob = true;
				}
				else
				{
					const URLString pseudo_lod_model_url = toURLString("__voxel__" + toString(hash) + "_" + toString(ob_model_lod_level));

					Reference<MeshData>         mesh_data          = mesh_manager.getMeshData(pseudo_lod_model_url);
					Reference<PhysicsShapeData> physics_shape_data = mesh_manager.getPhysicsShapeData(MeshManagerPhysicsShapeKey(pseudo_lod_model_url, ob->isDynamic()));
				
					if(mesh_data && physics_shape_data)
					{
						//conPrint("Meshdata for voxel " + pseudo_lod_model_url + " already in mesh manager");
						const bool is_meshdata_loaded_into_opengl = mesh_data->gl_meshdata->vbo_handle.valid();
						assert(is_meshdata_loaded_into_opengl);
						if(is_meshdata_loaded_into_opengl)
						{
							loadPresentObjectGraphicsAndPhysicsModels(ob, mesh_data, physics_shape_data, ob_lod_level, ob_model_lod_level, mesh_data->voxel_subsample_factor, world_state_lock);
							added_opengl_ob = true;
						}
					}
					else // else if mesh data is not in mesh manager:
					{
						const bool just_added = this->checkAddModelToProcessingSet(pseudo_lod_model_url, /*dynamic_physics_shape=*/ob->isDynamic()); // Avoid making multiple LoadModelTasks for this mesh.
						if(just_added)
						{
							//conPrint("Making LoadModelTask for voxel " + pseudo_lod_model_url);

							js::Vector<bool, 16> mat_transparent(ob->materials.size());
							for(size_t i=0; i<ob->materials.size(); ++i)
								mat_transparent[i] = ob->materials[i]->opacity.val < 1.f;

							// Do the model loading (conversion of voxel group to triangle mesh) in a different thread
							Reference<LoadModelTask> load_model_task = new LoadModelTask();

							load_model_task->lod_model_url = pseudo_lod_model_url;
							load_model_task->model_lod_level = ob_model_lod_level;
							load_model_task->opengl_engine = opengl_engine;
							load_model_task->result_msg_queue = &this->msg_queue;
							load_model_task->resource_manager = resource_manager;
							load_model_task->compressed_voxels = ob->getCompressedVoxels();
							load_model_task->ob_to_world_matrix = obToWorldMatrix(*ob);
							load_model_task->voxel_hash = hash;
							load_model_task->mat_transparent = mat_transparent;
							load_model_task->need_lightmap_uvs = !ob->lightmap_url.empty();
							load_model_task->build_dynamic_physics_ob = ob->isDynamic();
							load_model_task->worker_allocator = worker_allocator;
							load_model_task->upload_thread = opengl_upload_thread;

							load_item_queue.enqueueItem(/*key=*/pseudo_lod_model_url, *ob, load_model_task, max_dist_for_ob_model_lod_level);
						}

						load_placeholder = !added_opengl_ob;
					}

					// If the mesh wasn't loaded onto the GPU yet, add this object to the wait list, for when the mesh is loaded.
					if(!added_opengl_ob)
						this->loading_model_URL_to_world_ob_UID_map[ModelProcessingKey(pseudo_lod_model_url, ob->isDynamic())].insert(ob->uid);
				}
			}
			else
			{
				// Update textures to correct LOD-level textures.
				if(ob->opengl_engine_ob.nonNull() && !ob->using_placeholder_model)
				{
					ModelLoading::setMaterialTexPathsForLODLevel(*ob->opengl_engine_ob, ob_lod_level, ob->materials, ob->lightmap_url, /*use_basis=*/this->server_has_basis_textures, *resource_manager);
					assignLoadedOpenGLTexturesToMats(ob);
					for(size_t z=0; z<ob->opengl_engine_ob->materials.size(); ++z)
					{
						ob->opengl_engine_ob->materials[z].materialise_effect = use_materialise_effect;
						ob->opengl_engine_ob->materials[z].materialise_start_time = ob->materialise_effect_start_time;
					}
				}
			}
		}
		else if(ob->object_type == WorldObject::ObjectType_Generic)
		{
			assert(ob->object_type == WorldObject::ObjectType_Generic);

			
			//if(::hasPrefix(ob->content, "biome:")) // If we want to scatter on this object:
			//{
			//	if(ob->scattering_info.isNull()) // if scattering info is not computed for this object yet:
			//	{
			//		const bool already_procesing = scatter_info_processing.count(ob->uid) > 0; // And we aren't already building scattering info for this object (e.g. task is in a queue):
			//		if(!already_procesing)
			//		{
			//			Reference<BuildScatteringInfoTask> scatter_task = new BuildScatteringInfoTask();
			//			scatter_task->ob_uid = ob->uid;
			//			scatter_task->lod_model_url = ob->model_url; // Use full res model URL
			//			scatter_task->ob_to_world = ob_to_world_matrix;
			//			scatter_task->result_msg_queue = &this->msg_queue;
			//			scatter_task->resource_manager = resource_manager;
			//			load_item_queue.enqueueItem(*ob, scatter_task, /*task max dist=*/1.0e10f);

			//			scatter_info_processing.insert(ob->uid);
			//		}
			//	}
			//}


			if(!ob->model_url.empty() && 
				(ob->loading_or_loaded_model_lod_level != ob_model_lod_level))  // We may already have the correct LOD model loaded, don't reload if so.
				// (The object LOD level might have changed, but the model LOD level may be the same due to max model lod level, for example for simple cube models.)
			{
				ob->loading_or_loaded_model_lod_level = ob_model_lod_level;

				bool added_opengl_ob = false;

				WorldObject::GetLODModelURLOptions options(/*get_optimised_mesh=*/this->server_has_optimised_meshes, this->server_opt_mesh_version);
				options.get_optimised_mesh = this->server_has_optimised_meshes;
				const URLString lod_model_url = WorldObject::getLODModelURLForLevel(ob->model_url, ob_model_lod_level, options);

				// print("Loading model for ob: UID: " + ob->uid.toString() + ", type: " + WorldObject::objectTypeString((WorldObject::ObjectType)ob->object_type) + ", lod_model_url: " + lod_model_url);

				Reference<MeshData> mesh_data = mesh_manager.getMeshData(lod_model_url);
				Reference<PhysicsShapeData> physics_shape_data = mesh_manager.getPhysicsShapeData(MeshManagerPhysicsShapeKey(lod_model_url, ob->isDynamic()));
				if(mesh_data.nonNull() && physics_shape_data.nonNull())
				{
					const bool is_meshdata_loaded_into_opengl = mesh_data->gl_meshdata->vbo_handle.valid();
					assert(is_meshdata_loaded_into_opengl);
					if(is_meshdata_loaded_into_opengl)
					{
						loadPresentObjectGraphicsAndPhysicsModels(ob, mesh_data, physics_shape_data, ob_lod_level, ob_model_lod_level, /*voxel_subsample_factor=*/1, world_state_lock);
						added_opengl_ob = true;
					}
				}
				else // else if mesh data is not in mesh manager:
				{
					if(resource_manager->isFileForURLPresent(lod_model_url))
					{
						const bool just_added = this->checkAddModelToProcessingSet(lod_model_url, /*dynamic_physics_shape=*/ob->isDynamic()); // Avoid making multiple LoadModelTasks for this mesh.
						if(just_added)
						{
							// Do the model loading in a different thread
							Reference<LoadModelTask> load_model_task = new LoadModelTask();

							load_model_task->resource = resource_manager->getOrCreateResourceForURL(lod_model_url);
							load_model_task->lod_model_url = lod_model_url;
							load_model_task->model_lod_level = ob_model_lod_level;
							load_model_task->opengl_engine = this->opengl_engine;
							load_model_task->result_msg_queue = &this->msg_queue;
							load_model_task->resource_manager = resource_manager;
							load_model_task->build_dynamic_physics_ob = ob->isDynamic();
							load_model_task->worker_allocator = worker_allocator;
							load_model_task->upload_thread = opengl_upload_thread;

							load_item_queue.enqueueItem(/*key=*/lod_model_url, *ob, load_model_task, max_dist_for_ob_model_lod_level);
						}
						else
							load_item_queue.checkUpdateItemPosition(/*key=*/lod_model_url, *ob);
					}
				}

				// If the mesh wasn't loaded onto the GPU yet, add this object to the wait list, for when the mesh is loaded.
				if(!added_opengl_ob)
					this->loading_model_URL_to_world_ob_UID_map[ModelProcessingKey(lod_model_url, ob->isDynamic())].insert(ob->uid);

				load_placeholder = !added_opengl_ob;
			}
			else
			{
				// Update textures to correct LOD-level textures.
				if(ob->opengl_engine_ob.nonNull() && !ob->using_placeholder_model)
				{
					ModelLoading::setMaterialTexPathsForLODLevel(*ob->opengl_engine_ob, ob_lod_level, ob->materials, ob->lightmap_url, /*use_basis=*/this->server_has_basis_textures, *resource_manager);
					assignLoadedOpenGLTexturesToMats(ob);
				}
			}
		}
		else
		{
			throw glare::Exception("Invalid object_type: " + toString((int)(ob->object_type)));
		}

		if(load_placeholder)
		{
			if(ob->opengl_engine_ob.isNull() && !BitUtils::isBitSet(ob->flags, WorldObject::EXCLUDE_FROM_LOD_CHUNK_MESH))
			{
				// We will use part of the chunk geometry as the placeholder graphics for this object, while it is loading.
				// Use the sub-range of the indices from the LOD chunk geometry that correspond to this object.
				const bool ob_batch0_nonempty = ob->chunk_batch0_end > ob->chunk_batch0_start;
				const bool ob_batch1_nonempty = ob->chunk_batch1_end > ob->chunk_batch1_start;
				if(ob_batch0_nonempty || ob_batch1_nonempty) // If object has a chunk sub-range set:
				{
					// Get the chunk this object is in, if any
					const Vec4f centroid = ob->getCentroidWS();
					const Vec3i chunk_coords(Maths::floorToInt(centroid[0] / chunk_w), Maths::floorToInt(centroid[1] / chunk_w), 0);

					auto res = world_state->lod_chunks.find(chunk_coords);
					if(res != world_state->lod_chunks.end())
					{
						const LODChunk* chunk = res->second.ptr();
						if(chunk->graphics_ob)
						{
							assignLODChunkSubMeshPlaceholderToOb(chunk, ob);
						}
					}
				}
			}
		}

		//print("\tModel loaded. (Elapsed: " + timer.elapsedStringNSigFigs(4) + ")");
	}
	catch(glare::Exception& e)
	{
		print("Error while loading object with UID " + ob->uid.toString() + ", model_url='" + toStdString(ob->model_url) + "': " + e.what());
	}
}


// Create OpenGL and Physics objects for the WorldObject, given that the OpenGL and physics meshes are present in memory.
void GUIClient::loadPresentObjectGraphicsAndPhysicsModels(WorldObject* ob, const Reference<MeshData>& mesh_data, const Reference<PhysicsShapeData>& physics_shape_data, int ob_lod_level, int ob_model_lod_level, 
	int voxel_subsample_factor, WorldStateLock& world_state_lock)
{
	removeAndDeleteGLObjectsForOb(*ob); // Remove any existing OpenGL model

	// Remove previous physics object. If this is a dynamic or kinematic object, don't delete old object though, unless it's a placeholder.
	if(ob->physics_object.nonNull() && (ob->using_placeholder_model || !(ob->physics_object->dynamic || ob->physics_object->kinematic)))
	{
		destroyVehiclePhysicsControllingObject(ob); // Destroy any vehicle controller controlling this object, as vehicle controllers have pointers to physics bodies, which we can't leave dangling.
		physics_world->removeObject(ob->physics_object);
		ob->physics_object = NULL;
	}

	// Create gl and physics object now
	Matrix4f ob_to_world_matrix = obToWorldMatrix(*ob);

	if(voxel_subsample_factor != 1)
		ob_to_world_matrix = ob_to_world_matrix * Matrix4f::uniformScaleMatrix((float)voxel_subsample_factor);

	ob->opengl_engine_ob = ModelLoading::makeGLObjectForMeshDataAndMaterials(*opengl_engine, mesh_data->gl_meshdata, ob_lod_level, ob->materials, ob->lightmap_url, /*use_basis=*/this->server_has_basis_textures, *resource_manager, ob_to_world_matrix);

	if(ob->object_type == WorldObject::ObjectType_VoxelGroup)
		for(size_t z=0; z<ob->opengl_engine_ob->materials.size(); ++z)
			ob->opengl_engine_ob->materials[z].gen_planar_uvs = true;
						
	mesh_data->meshDataBecameUsed();
	ob->mesh_manager_data = mesh_data;// Hang on to a reference to the mesh data, so when object-uses of it are removed, it can be removed from the MeshManager with meshDataBecameUnused().

	physics_shape_data->shapeDataBecameUsed();
	ob->mesh_manager_shape_data = physics_shape_data; // Likewise for the physics mesh data.

	const float current_time = (float)Clock::getTimeSinceInit();
	const bool use_materialise_effect = ob->use_materialise_effect_on_load && (current_time - ob->materialise_effect_start_time < 2.0f);


	assignLoadedOpenGLTexturesToMats(ob);

	if(use_materialise_effect)
		for(size_t z=0; z<ob->opengl_engine_ob->materials.size(); ++z)
		{
			ob->opengl_engine_ob->materials[z].materialise_effect = use_materialise_effect;
			ob->opengl_engine_ob->materials[z].materialise_start_time = ob->materialise_effect_start_time;
		}


	ob->loading_or_loaded_model_lod_level = ob_model_lod_level; // NOTE: probably not needed as should have been set when loading started

	// Create physics object 
	if(ob->physics_object.isNull()) // if object was dynamic, we may not have unloaded its physics object above, check.
	{
		ob->physics_object = new PhysicsObject(/*collidable=*/ob->isCollidable());

		PhysicsShape use_shape = physics_shape_data->physics_shape;
		if(ob->centre_of_mass_offset_os != Vec3f(0.f))
			use_shape = PhysicsWorld::createCOMOffsetShapeForShape(physics_shape_data->physics_shape, ob->centre_of_mass_offset_os.toVec4fVector());

		ob->physics_object->shape = use_shape;
		ob->physics_object->is_sensor = ob->isSensor();
		ob->physics_object->userdata = ob;
		ob->physics_object->userdata_type = 0;
		ob->physics_object->ob_uid = ob->uid;
		ob->physics_object->pos = ob->pos.toVec4fPoint();
		ob->physics_object->rot = Quatf::fromAxisAndAngle(normalise(ob->axis), ob->angle);
		ob->physics_object->scale = useScaleForWorldOb(ob->scale);

		// TEMP HACK
		ob->physics_object->kinematic = !ob->script.empty();
		ob->physics_object->dynamic = ob->isDynamic();
		ob->physics_object->is_sphere = ob->model_url == "Icosahedron_obj_136334556484365507.bmesh";
		ob->physics_object->is_cube = ob->model_url == "Cube_obj_11907297875084081315.bmesh";

		ob->physics_object->mass = ob->mass;
		ob->physics_object->friction = ob->friction;
		ob->physics_object->restitution = ob->restitution;

		physics_world->addObject(ob->physics_object);

		if(ob->was_just_created && ob->isDynamic())
			physics_world->activateObject(ob->physics_object);
	}


	if(hasPrefix(ob->content, "biome: park"))
	{
		ob->opengl_engine_ob->draw_to_mask_map = true;

		if(this->terrain_system.nonNull())
			this->terrain_system->invalidateVegetationMap(ob->getAABBWS());
	}


	//Timer timer;
	opengl_engine->addObject(ob->opengl_engine_ob);
	//if(timer.elapsed() > 0.01) conPrint("addObject took                    " + timer.elapsedStringNSigFigs(5));


	// Add any objects with mp4 textures to the set of animated objects. (if not already)
	for(size_t i=0; i<ob->materials.size(); ++i)
	{
		if(	::hasExtension(ob->materials[i]->colour_texture_url,   "mp4") ||
			::hasExtension(ob->materials[i]->emission_texture_url, "mp4"))
		{
			if(ob->animated_tex_data.isNull())
			{
				ob->animated_tex_data = new AnimatedTexObData();
				this->obs_with_animated_tex.insert(ob);
			}

			ob->animated_tex_data->rescanObjectForAnimatedTextures(opengl_engine.ptr(), ob, rng, *animated_texture_manager);
		}
	}


	//ui->indigoView->objectAdded(*ob, *this->resource_manager);

	loadScriptForObject(ob, world_state_lock); // Load any script for the object.

	// If we replaced the model for selected_ob, reselect it in the OpenGL engine
	if(this->selected_ob == ob)
		opengl_engine->selectObject(ob->opengl_engine_ob);
}


void GUIClient::loadPresentAvatarModel(Avatar* avatar, int av_lod_level, const Reference<MeshData>& mesh_data)
{
	// conPrint("GUIClient::loadPresentAvatarModel");

	removeAndDeleteGLObjectForAvatar(*avatar);

	const Matrix4f ob_to_world_matrix = obToWorldMatrix(*avatar);

	// Create gl and physics object now
	avatar->graphics.skinned_gl_ob = ModelLoading::makeGLObjectForMeshDataAndMaterials(*opengl_engine, mesh_data->gl_meshdata, av_lod_level, avatar->avatar_settings.materials, /*lightmap_url=*/URLString(), 
		/*use_basis=*/this->server_has_basis_textures, *resource_manager, ob_to_world_matrix);

	mesh_data->meshDataBecameUsed();
	avatar->mesh_data = mesh_data; // Hang on to a reference to the mesh data, so when object-uses of it are removed, it can be removed from the MeshManager with meshDataBecameUnused().

	// Load animation data for ready-player-me type avatars
	if(!avatar->graphics.skinned_gl_ob->mesh_data->animation_data.retarget_adjustments_set)
	{
#if EMSCRIPTEN
		FileInStream file("/extracted_avatar_anim.bin");
#else
		FileInStream file(resources_dir_path + "/extracted_avatar_anim.bin");
#endif
		const GLMemUsage old_mem_usage = avatar->graphics.skinned_gl_ob->mesh_data->getTotalMemUsage();

		avatar->graphics.skinned_gl_ob->mesh_data->animation_data.loadAndRetargetAnim(file);

		const GLMemUsage new_mem_usage = avatar->graphics.skinned_gl_ob->mesh_data->getTotalMemUsage();

		// The mesh manager keeps a running total of the amount of memory used by inserted meshes.  Therefore it needs to be informed if the size of one of them changes.
		mesh_manager.meshMemoryAllocatedChanged(old_mem_usage, new_mem_usage);
	}

	avatar->graphics.build();

	glare::ArenaAllocator use_arena = arena_allocator.getFreeAreaArenaAllocator();
	assignLoadedOpenGLTexturesToAvatarMats(avatar, /*use_basis=*/this->server_has_basis_textures, *opengl_engine, *resource_manager, *animated_texture_manager, &use_arena);

	// Enable materialise effect if needed
	const float current_time = (float)Clock::getTimeSinceInit();
	const bool use_materialise_effect = avatar->use_materialise_effect_on_load && (current_time - avatar->materialise_effect_start_time < 2.0f);

	for(size_t z=0; z<avatar->graphics.skinned_gl_ob->materials.size(); ++z)
	{
		avatar->graphics.skinned_gl_ob->materials[z].materialise_effect = use_materialise_effect;
		avatar->graphics.skinned_gl_ob->materials[z].materialise_start_time = avatar->materialise_effect_start_time;
	}

	avatar->graphics.loaded_lod_level = av_lod_level;

	opengl_engine->addObject(avatar->graphics.skinned_gl_ob);

	// If we just loaded the graphics for our own avatar, see if there is a gesture animation we should be playing, and if so, play it.
	const bool our_avatar = avatar->uid == this->client_avatar_uid;
	if(our_avatar)
	{
		std::string gesture_name;
		bool animate_head, loop_anim;
		if(gesture_ui.getCurrentGesturePlaying(gesture_name, animate_head, loop_anim)) // If we should be playing a gesture according to the UI:
		{
			const double cur_time = Clock::getTimeSinceInit(); // Used for animation, interpolation etc..
			avatar->graphics.performGesture(cur_time, gesture_name, animate_head, loop_anim);
		}
	}

	// conPrint("GUIClient::loadPresentAvatarModel done");
}


// Check if the avatar model file is downloaded.
// If so, load the model into the OpenGL engine.
// If not, queue up the model download.
// Also enqueue any downloads for missing resources such as textures.
void GUIClient::loadModelForAvatar(Avatar* avatar)
{
	const bool our_avatar = avatar->uid == this->client_avatar_uid;

	const int ob_lod_level = avatar->getLODLevel(cam_controller.getPosition());
	const int ob_model_lod_level = ob_lod_level;

	const float max_dist_for_ob_lod_level = avatar->getMaxDistForLODLevel(ob_lod_level);
	const float max_dist_for_ob_model_lod_level = avatar->getMaxDistForLODLevel(ob_model_lod_level);


	// If we have a model loaded, that is not the placeholder model, and it has the correct LOD level, we don't need to do anything.
	if(avatar->graphics.skinned_gl_ob.nonNull() && /*&& !ob->using_placeholder_model && */(avatar->graphics.loaded_lod_level == ob_lod_level))
		return;

	Timer timer;
	

	// If the avatar model URL is empty, we will be using the default xbot model.  Need to make it be rotated from y-up to z-up, and assign materials.
	if(avatar->avatar_settings.model_url.empty())
	{
		avatar->avatar_settings.model_url = DEFAULT_AVATAR_MODEL_URL;
		avatar->avatar_settings.materials.resize(2);

		avatar->avatar_settings.materials[0] = new WorldMaterial();
		avatar->avatar_settings.materials[0]->colour_rgb = Avatar::defaultMat0Col();
		avatar->avatar_settings.materials[0]->metallic_fraction.val = Avatar::default_mat0_metallic_frac;
		avatar->avatar_settings.materials[0]->roughness.val = Avatar::default_mat0_roughness;

		avatar->avatar_settings.materials[1] = new WorldMaterial();
		avatar->avatar_settings.materials[1]->colour_rgb = Avatar::defaultMat1Col();
		avatar->avatar_settings.materials[1]->metallic_fraction.val = Avatar::default_mat1_metallic_frac;

		const float EYE_HEIGHT = 1.67f;
		const Matrix4f to_z_up(Vec4f(1,0,0,0), Vec4f(0, 0, 1, 0), Vec4f(0, -1, 0, 0), Vec4f(0,0,0,1));
		avatar->avatar_settings.pre_ob_to_world_matrix = Matrix4f::translationMatrix(0, 0, -EYE_HEIGHT) * to_z_up;
	}


	try
	{
		const bool avatar_is_default_model = avatar->avatar_settings.model_url == DEFAULT_AVATAR_MODEL_URL;

		// Start downloading any resources we don't have that the object uses.
		if(!avatar_is_default_model) // Avoid downloading optimised version of default avatar; is already optimised.
			startDownloadingResourcesForAvatar(avatar, ob_lod_level, our_avatar);

		startLoadingTexturesForAvatar(*avatar, ob_lod_level, max_dist_for_ob_lod_level, our_avatar);

		// Add any objects with gif or mp4 textures to the set of animated objects.
		/*for(size_t i=0; i<avatar->materials.size(); ++i)
		{
			if(::hasExtension(avatar->materials[i]->colour_texture_url, "gif") || ::hasExtensionStringView(avatar->materials[i]->colour_texture_url, "mp4"))
			{
				//Reference<AnimatedTexObData> anim_data = new AnimatedTexObData();
				this->obs_with_animated_tex.insert(std::make_pair(ob, AnimatedTexObData()));
			}
		}*/


		bool added_opengl_ob = false;

		WorldObject::GetLODModelURLOptions options(/*get_optimised_mesh=*/this->server_has_optimised_meshes, this->server_opt_mesh_version);
		const URLString lod_model_url = avatar_is_default_model ? DEFAULT_AVATAR_MODEL_URL : WorldObject::getLODModelURLForLevel(avatar->avatar_settings.model_url, ob_model_lod_level, options);

		avatar->graphics.loaded_lod_level = ob_lod_level;


		Reference<MeshData> mesh_data = mesh_manager.getMeshData(lod_model_url);
		if(mesh_data.nonNull())
		{
			const bool is_meshdata_loaded_into_opengl = mesh_data->gl_meshdata->vbo_handle.valid();
			if(is_meshdata_loaded_into_opengl)
			{
				loadPresentAvatarModel(avatar, ob_lod_level, mesh_data);

				added_opengl_ob = true;
			}
		}
		else
		{
			if(resource_manager->isFileForURLPresent(lod_model_url))
			{
				const bool just_added = this->checkAddModelToProcessingSet(lod_model_url, /*dynamic_physics_shape=*/false); // Avoid making multiple LoadModelTasks for this mesh.
				if(just_added)
				{
					// Do the model loading in a different thread
					Reference<LoadModelTask> load_model_task = new LoadModelTask();

					load_model_task->resource = resource_manager->getOrCreateResourceForURL(lod_model_url);
					load_model_task->lod_model_url = lod_model_url;
					load_model_task->model_lod_level = ob_model_lod_level;
					load_model_task->opengl_engine = this->opengl_engine;
					load_model_task->result_msg_queue = &this->msg_queue;
					load_model_task->resource_manager = resource_manager;
					load_model_task->build_physics_ob = false; // Don't build physics object for avatar mesh, as it isn't used, and can be slow to build.
					load_model_task->worker_allocator = worker_allocator;
					load_model_task->upload_thread = opengl_upload_thread;

					load_item_queue.enqueueItem(/*key=*/lod_model_url, *avatar, load_model_task, max_dist_for_ob_model_lod_level, our_avatar);
				}
				else
					load_item_queue.checkUpdateItemPosition(/*key=*/lod_model_url, *avatar, our_avatar);
			}
		}

		if(!added_opengl_ob)
		{
			this->loading_model_URL_to_avatar_UID_map[lod_model_url].insert(avatar->uid);
		}

		//print("\tModel loaded. (Elapsed: " + timer.elapsedStringNSigFigs(4) + ")");
	}
	catch(glare::Exception& e)
	{
		print("Error while loading avatar with UID " + avatar->uid.toString() + ", model_url='" + toStdString(avatar->avatar_settings.model_url) + "': " + e.what());
	}
}


// Remove any existing instances of this object from the instance set, also from 3d engine and physics engine.
void GUIClient::removeInstancesOfObject(WorldObject* prototype_ob)
{
	for(size_t z=0; z<prototype_ob->instances.size(); ++z)
	{
		InstanceInfo& instance = prototype_ob->instances[z];
		
		if(instance.physics_object.nonNull())
		{
			physics_world->removeObject(instance.physics_object); // Remove from physics engine
			instance.physics_object = NULL;
		}
	}

	prototype_ob->instances.clear();
	prototype_ob->instance_matrices.clear();
}


void GUIClient::removeObScriptingInfo(WorldObject* ob)
{
	removeInstancesOfObject(ob);
	if(ob->script_evaluator)
	{
		sendWinterShaderEvaluatorToGarbageDeleterThread(ob->script_evaluator);
		ob->script_evaluator = NULL;
		this->obs_with_scripts.erase(ob);
	}

	// Remove any path controllers for this object
	for(int i=(int)path_controllers.size() - 1; i >= 0; --i)
	{
		if(path_controllers[i]->controlled_ob.ptr() == ob)
		{
			path_controllers.erase(path_controllers.begin() + i);
		}
	}
	ob->is_path_controlled = false;
}


void GUIClient::loadScriptForObject(WorldObject* ob, WorldStateLock& world_state_lock)
{
	PERFORMANCEAPI_INSTRUMENT_FUNCTION();
	ZoneScoped; // Tracy profiler

	// If the script changed bit was set, destroy the script evaluator, we will create a new one.
 	if(BitUtils::isBitSet(ob->changed_flags, WorldObject::SCRIPT_CHANGED))
	{
		// Clear SCRIPT_CHANGED flag first.  That way if an exception is thrown below, we won't try and create the script again repeatedly on LOD changes etc.
		BitUtils::zeroBit(ob->changed_flags, WorldObject::SCRIPT_CHANGED);

		// conPrint("GUIClient::loadScriptForObject(): SCRIPT_CHANGED bit was set, destroying script_evaluator.");
		
		removeObScriptingInfo(ob);

		if(hasPrefix(ob->script, "<?xml"))
		{
			const double global_time = this->world_state->getCurrentGlobalTime();

			Reference<ObjectPathController> path_controller;
			Reference<Scripting::VehicleScript> vehicle_script;
			Scripting::parseXMLScript(ob, ob->script, global_time, path_controller, vehicle_script);

			if(path_controller.nonNull())
			{
				path_controllers.push_back(path_controller);

				ObjectPathController::sortPathControllers(path_controllers);

				ob->is_path_controlled = true;

				// conPrint("Added path controller, path_controllers.size(): " + toString(path_controllers.size()));
				if(ob->isDynamic() && (ob->creator_id == this->logged_in_user_id) && (selected_ob == ob))
					showErrorNotification("Object with follow-path script has the 'dynamic' physics option enabled.  Disable it for the follow-path script to work.");
			}

			if(vehicle_script.nonNull())
			{
				ob->vehicle_script = vehicle_script;
				// conPrint("Added hover car script to object");
			}

			if(ob == selected_ob.ptr())
				createPathControlledPathVisObjects(*ob); // Create or update 3d path visualisation.
		}

		if(!ob->script.empty() && ob->script_evaluator.isNull())
		{
			const bool just_inserted = checkAddScriptToProcessingSet(ob->script); // Mark script as being processed so another LoadScriptTask doesn't try and process it also.
			if(just_inserted)
			{
				Reference<LoadScriptTask> task = new LoadScriptTask();
				task->base_dir_path = base_dir_path;
				task->result_msg_queue = &msg_queue;
				task->script_content = ob->script;
				load_item_queue.enqueueItem(/*key=*/toURLString("script_" + ob->uid.toString()), *ob, task, /*task max dist=*/std::numeric_limits<float>::infinity());
			}
		}


		if(hasPrefix(ob->script, "--lua"))
		{
			ob->lua_script_evaluator = NULL;
			scripted_ob_proximity_checker.removeObject(ob);

			try
			{
				if((ob->creator_id == this->logged_in_user_id) && (selected_ob == ob))
					ui_interface->printFromLuaScript("Running script at " + Clock::get12HourClockLocalTimeOfDayString(), ob->uid);

				ob->lua_script_evaluator = new LuaScriptEvaluator(this->lua_vm, /*script output handler=*/this, ob->script, ob, world_state_lock);

				// Add this object to scripted_ob_proximity_checker if it has any spatial event handlers.  TEMP: just add in all cases.
				scripted_ob_proximity_checker.addObject(ob);
			}
			catch(glare::Exception& e)
			{
				throw glare::Exception("Error while creating Lua script: " + e.what());
			}
		}
	}

	
	// If we have a script evaluator already, but the opengl ob has been recreated (due to LOD level changes), we need to recreate the instance_matrices VBO
	if(ob->script_evaluator.nonNull() && ob->opengl_engine_ob.nonNull() && ob->opengl_engine_ob->instance_matrix_vbo.isNull() && !ob->instance_matrices.empty())
	{
		ob->opengl_engine_ob->enableInstancing(*opengl_engine->vert_buf_allocator, ob->instance_matrices.data(), sizeof(Matrix4f) * ob->instance_matrices.size());

		opengl_engine->objectMaterialsUpdated(*ob->opengl_engine_ob); // Reload mat to enable instancing
	}
}


void GUIClient::handleScriptLoadedForObUsingScript(ScriptLoadedThreadMessage* loaded_msg, WorldObject* ob)
{
	assert(loaded_msg->script == ob->script);
	assert(loaded_msg->script_evaluator.nonNull());

	try
	{
		if(ob->script_evaluator)
		{
			sendWinterShaderEvaluatorToGarbageDeleterThread(ob->script_evaluator);
			ob->script_evaluator = nullptr;
		}

		ob->script_evaluator = loaded_msg->script_evaluator;

		const std::string script_content = loaded_msg->script;

		// Handle instancing command if present
		int count = 0;
		if(ob->object_type == WorldObject::ObjectType_Generic) // Only allow instancing on objects (not spotlights etc. yet)
		{
			const std::vector<std::string> lines = StringUtils::splitIntoLines(script_content);
			for(size_t z=0; z<lines.size(); ++z)
			{
				if(::hasPrefix(lines[z], "#instancing"))
				{
					Parser parser(lines[z]);
					parser.parseString("#instancing");
					parser.parseWhiteSpace();
					if(!parser.parseInt(count))
						throw glare::Exception("Failed to parse count after #instancing.");
				}
			}
		}

		const int MAX_COUNT = 100;
		count = myMin(count, MAX_COUNT);

		this->obs_with_scripts.insert(ob);

		if(count > 0) // If instancing was requested:
		{
			// conPrint("Doing instancing with count " + toString(count));

			removeInstancesOfObject(ob); // Make sure we remove any existing physics objects for existing instances.

			ob->instance_matrices.resize(count);
			ob->instances.resize(count);

			// Create a bunch of copies of this object
			for(size_t z=0; z<(size_t)count; ++z)
			{
				InstanceInfo* instance = &ob->instances[z];

				assert(instance->physics_object.isNull());

				instance->instance_index = (int)z;
				instance->num_instances = count;
				instance->script_evaluator = ob->script_evaluator;
				instance->prototype_object = ob;

				instance->pos = ob->pos;
				instance->axis = ob->axis;
				instance->angle = ob->angle;
				instance->scale = ob->scale;

				// Make physics object
				if(ob->physics_object.nonNull())
				{
					PhysicsObjectRef physics_ob = new PhysicsObject(/*collidable=*/ob->isCollidable());
					physics_ob->shape = ob->physics_object->shape;
					physics_ob->kinematic = true;

					instance->physics_object = physics_ob;

					physics_ob->userdata = instance;
					physics_ob->userdata_type = 2;
					physics_ob->ob_uid = UID(6666666);

					physics_ob->pos = ob->pos.toVec4fPoint();
					physics_ob->rot = Quatf::fromAxisAndAngle(normalise(ob->axis), ob->angle);
					physics_ob->scale = useScaleForWorldOb(ob->scale);

					physics_world->addObject(physics_ob);
				}

				ob->instance_matrices[z] = obToWorldMatrix(*ob); // Use transform of prototype object for now.
			}

			if(ob->opengl_engine_ob.nonNull())
			{
				ob->opengl_engine_ob->enableInstancing(*opengl_engine->vert_buf_allocator, ob->instance_matrices.data(), sizeof(Matrix4f) * count);
						
				opengl_engine->objectMaterialsUpdated(*ob->opengl_engine_ob); // Reload mat to enable instancing
			}
		}
	}
	catch(glare::Exception& e)
	{
		// If this user created this model, show the error message.
		//if(ob->creator_id == this->logged_in_user_id)
		//{
		//	// showErrorNotification("Error while loading script '" + ob->script + "': " + e.what());
		//}

		print("Error while loading script '" + ob->script + "': " + e.what());
	}
}


// Object model has been loaded, now do biome scattering over it, if not done already for this object
void GUIClient::doBiomeScatteringForObject(WorldObject* ob)
{
#if 0
	PERFORMANCEAPI_INSTRUMENT_FUNCTION();
	ZoneScoped; // Tracy profiler

	if(::hasPrefix(ob->content, "biome:"))
	{
		if(!biome_manager->isObjectInBiome(ob))
		{
			biome_manager->initTexturesAndModels(resources_dir_path, *opengl_engine, *resource_manager);
		}
	}
#endif
}


// Hangs on to the LoadedBuffer reference to keep it alive.
class LoadedBufferAudioDataSource : public glare::MP3AudioStreamerDataSource
{
public:
	virtual ~LoadedBufferAudioDataSource() {}

	virtual const uint8* getData() { return (const uint8*)loaded_buffer->buffer; }
	virtual size_t getSize() { return loaded_buffer->buffer_size; }

	Reference<LoadedBuffer> loaded_buffer;
};


// Try and start loading the audio file for the world object, as specified by ob->audio_source_url.
// If the audio file is already loaded, (e.g. ob->loaded_audio_source_url == ob->audio_source_url), then do nothing.
// If the object is further than MAX_AUDIO_DIST from the camera, don't load the audio.
//
// loaded_buffer is for emscripten, when resource is loaded directly from memory instead of disk.   may be null.
void GUIClient::loadAudioForObject(WorldObject* ob, const Reference<LoadedBuffer>& loaded_buffer)
{
	ZoneScoped; // Tracy profiler

	// conPrint("GUIClient::loadAudioForObject(), audio_source_url: " + ob->audio_source_url);

	if(BitUtils::isBitSet(ob->changed_flags, WorldObject::AUDIO_SOURCE_URL_CHANGED))
	{
		//conPrint("GUIClient::loadAudioForObject(): AUDIO_SOURCE_URL_CHANGED bit was set, setting state to AudioState_NotLoaded.");
		ob->audio_state = WorldObject::AudioState_NotLoaded;
		BitUtils::zeroBit(ob->changed_flags, WorldObject::AUDIO_SOURCE_URL_CHANGED);
	}

	try
	{
		if(ob->audio_source_url.empty())
		{
			// Remove any existing audio source
			if(ob->audio_source.nonNull())
				audio_engine.removeSource(ob->audio_source);
			ob->audio_source = NULL;
			//ob->loaded_audio_source_url = ob->audio_source_url;
		}
		else
		{
			if(ob->audio_state == WorldObject::AudioState_NotLoaded || ob->audio_state == WorldObject::AudioState_Loading)
			{
				//if(ob->loaded_audio_source_url == ob->audio_source_url) // If the audio file is already loaded, (e.g. ob->loaded_audio_source_url == ob->audio_source_url), then do nothing.
				//	return;
			
				// If the object is further than MAX_AUDIO_DIST from the camera, don't load the audio.
				const float dist = cam_controller.getPosition().toVec4fVector().getDist(ob->pos.toVec4fVector());
				if(dist > maxAudioDistForSourceVolFactor(ob->audio_volume))
					return;

				// Remove any existing audio source
				if(ob->audio_source.nonNull())
				{
					audio_engine.removeSource(ob->audio_source);
					ob->audio_source = NULL;
				}

				ob->audio_state = WorldObject::AudioState_Loading;


				Reference<LoadedBufferAudioDataSource> data_source;
#if EMSCRIPTEN
				// conPrint("GUIClient::loadAudioForObject(), loaded_buffer.nonNUll(): " + toString(loaded_buffer.nonNull()));
				if(!loaded_buffer)
					return;
				data_source = new LoadedBufferAudioDataSource();
				data_source->loaded_buffer = loaded_buffer;
				loaded_buffer->considerMemUsed();
#else
				if(!resource_manager->isFileForURLPresent(ob->audio_source_url))
					return;
#endif

				{
					//if(!isAudioProcessed(ob->audio_source_url)) // If we are not already loading the audio:

					if(hasExtension(ob->audio_source_url, "mp3"))
					{
						// Make a new audio source
						glare::AudioEngine::AddSourceFromStreamingSoundFileParams params;
						params.sound_file_path = resource_manager->pathForURL(ob->audio_source_url);
						params.sound_data_source = data_source;
						params.source_volume = ob->audio_volume;
						params.global_time = this->world_state->getCurrentGlobalTime();
						params.looping =  BitUtils::isBitSet(ob->flags, WorldObject::AUDIO_LOOP);
						params.paused = !BitUtils::isBitSet(ob->flags, WorldObject::AUDIO_AUTOPLAY);

						if(!params.sound_data_source && audio_engine.needNewStreamerForPath(params.sound_file_path, params.paused))
						{
							// New streams using an mp3 streaming from disk can block while creating MemMappedFile, so do in a task
							const bool just_inserted = checkAddAudioToProcessingSet(ob->audio_source_url); // Mark audio as being processed so another LoadAudioTask doesn't try and process it also.
							if(just_inserted)
							{
								// conPrint("Launching LoadAudioTask");
								// Do the audio file loading in a different thread
								Reference<LoadAudioTask> load_audio_task = new LoadAudioTask();

								load_audio_task->mem_map_file = true;
								load_audio_task->audio_source_url = ob->audio_source_url;
								load_audio_task->audio_source_path = resource_manager->pathForURL(ob->audio_source_url);
								load_audio_task->result_msg_queue = &this->msg_queue;

								load_item_queue.enqueueItem(/*key=*/ob->audio_source_url, *ob, load_audio_task, /*task max dist=*/maxAudioDistForSourceVolFactor(ob->audio_volume));
							}
							else
							{
								load_item_queue.checkUpdateItemPosition(/*key=*/ob->audio_source_url, *ob);
							}
						}
						else
						{
							glare::AudioSourceRef source = audio_engine.addSourceFromStreamingSoundFile(params, ob->pos.toVec4fPoint());

							Lock lock(world_state->mutex);
							const Parcel* parcel = world_state->getParcelPointIsIn(ob->pos);
							source->userdata_1 = parcel ? parcel->id.value() : ParcelID::invalidParcelID().value(); // Save the ID of the parcel the object is in, in userdata_1 field of the audio source.

							ob->audio_source = source;
							ob->audio_state = WorldObject::AudioState_Loaded;

							//---------------- Mute audio sources outside the parcel we are in, if required ----------------
							// Find out which parcel we are in, if any.
							ParcelID in_parcel_id = ParcelID::invalidParcelID(); // Which parcel camera is in
							bool mute_outside_audio = false; // Does the parcel the camera is in have 'mute outside audio' set?
							const Parcel* cam_parcel = world_state->getParcelPointIsIn(this->cam_controller.getFirstPersonPosition());
							if(cam_parcel)
							{
								in_parcel_id = cam_parcel->id;
								if(BitUtils::isBitSet(cam_parcel->flags, Parcel::MUTE_OUTSIDE_AUDIO_FLAG))
									mute_outside_audio = true;
							}

							if(mute_outside_audio && // If we are in a parcel, which has the mute-outside-audio option enabled:
								(source->userdata_1 != in_parcel_id.value())) // And the source is in another parcel (or not in any parcel):
							{
								source->setMuteVolumeFactorImmediately(0.f); // Mute it (set mute volume factor)
								audio_engine.sourceVolumeUpdated(*source); // Tell audio engine to mute it.
							}
							//----------------------------------------------------------------------------------------------
						}
					}
					else // else loading a non-streaming source, such as a WAV file.
					{
						const bool just_inserted = checkAddAudioToProcessingSet(ob->audio_source_url); // Mark audio as being processed so another LoadAudioTask doesn't try and process it also.
						if(just_inserted)
						{
							// conPrint("Launching LoadAudioTask");
							// Do the audio file loading in a different thread
							Reference<LoadAudioTask> load_audio_task = new LoadAudioTask();

							load_audio_task->mem_map_file = false;
							load_audio_task->resource = resource_manager->getOrCreateResourceForURL(ob->audio_source_url);
							load_audio_task->audio_source_url = ob->audio_source_url;
							load_audio_task->audio_source_path = resource_manager->pathForURL(ob->audio_source_url);
							load_audio_task->resource_manager = this->resource_manager;
							load_audio_task->result_msg_queue = &this->msg_queue;
							load_audio_task->loaded_buffer = loaded_buffer;

							load_item_queue.enqueueItem(/*key=*/ob->audio_source_url, *ob, load_audio_task, /*task max dist=*/maxAudioDistForSourceVolFactor(ob->audio_volume));
						}
						else
						{
							load_item_queue.checkUpdateItemPosition(/*key=*/ob->audio_source_url, *ob);
						}
					}

					//ob->loaded_audio_source_url = ob->audio_source_url;
				}
			}
		}
	}
	catch(glare::Exception& e)
	{
		print("Error while loading audio for object with UID " + ob->uid.toString() + ", audio_source_url='" + toStdString(ob->audio_source_url) + "': " + e.what());
		ob->audio_state = WorldObject::AudioState_ErrorWhileLoading; // Go to the error state, so we don't try and keep loading this audio.
	}
}


void GUIClient::updateInstancedCopiesOfObject(WorldObject* ob)
{
	for(size_t z=0; z<ob->instances.size(); ++z)
	{
		InstanceInfo* instance = &ob->instances[z];

		instance->angle = ob->angle;
		instance->pos = ob->pos;
		instance->scale = ob->scale;

		// TODO: update physics ob?
		//if(instance->physics_object.nonNull())
		//{
		//	//TEMP physics_world->updateObjectTransformData(*instance->physics_object);
		//}
	}
}


void GUIClient::logMessage(const std::string& msg) // Append to LogWindow log display
{
	//if(this->log_window && !running_destructor)
	//	this->log_window->appendLine(msg);
	if(ui_interface)
		ui_interface->logMessage(msg);
}


void GUIClient::logAndConPrintMessage(const std::string& msg) // Print to stdout and append to LogWindow log display
{
	conPrint(msg);
	//if(this->log_window && !running_destructor)
	//	this->log_window->appendLine(msg);
	if(ui_interface)
		ui_interface->logMessage(msg);
}


void GUIClient::print(const std::string& s) // Print a message and a newline character.
{
	logMessage(s);
}


void GUIClient::printStr(const std::string& s) // Print a message without a newline character.
{
	logMessage(s);
}


// Avoids NaNs
static float safeATan2(float y, float x)
{
	const float a = std::atan2(y, x);
	if(!isFinite(a))
		return 0.f;
	else
		return a;
}


// For each direction x, y, z, the two other basis vectors. 
static const Vec4f basis_vectors[6] = { Vec4f(0,1,0,0), Vec4f(0,0,1,0), Vec4f(0,0,1,0), Vec4f(1,0,0,0), Vec4f(1,0,0,0), Vec4f(0,1,0,0) };


// Update object placement beam - a beam that goes from the object to what's below it.
// Also updates axis arrows and rotation arc handles.
// Also updates preview AABB for decal objects.
void GUIClient::updateSelectedObjectPlacementBeamAndGizmos()
{
	if(selected_ob && this->selected_ob->opengl_engine_ob)
	{
		//-------------------- Update object placement beam - a beam that goes from the object to what's below it. -----------------------
		GLObjectRef opengl_ob = this->selected_ob->opengl_engine_ob;
		const Matrix4f& to_world = opengl_ob->ob_to_world_matrix;

		const js::AABBox new_aabb_ws = opengl_engine->getAABBWSForObjectWithTransform(*opengl_ob, to_world);

		// We need to determine where to trace down from.
		// To find this point, first trace up *just* against the selected object.
		// NOTE: With introduction of Jolt, we don't have just tracing against a single object, trace against world for now.
		RayTraceResult trace_results;
		Vec4f start_trace_pos = new_aabb_ws.centroid();
		start_trace_pos[2] = new_aabb_ws.min_[2] - 0.001f;
		//this->selected_ob->physics_object->traceRay(Ray(start_trace_pos, Vec4f(0, 0, 1, 0), 0.f, 1.0e5f), trace_results);
		this->physics_world->traceRay(start_trace_pos, Vec4f(0, 0, 1, 0), /*max_t=*/1.0e3f, /*ignore body id=*/JPH::BodyID(), trace_results);
		const float up_beam_len = trace_results.hit_object ? trace_results.hit_t : new_aabb_ws.axisLength(2) * 0.5f;

		// Now Trace ray downwards.  Start from just below where we got to in upwards trace.
		const Vec4f down_beam_startpos = start_trace_pos + Vec4f(0, 0, 1, 0) * (up_beam_len - 0.001f);
		this->physics_world->traceRay(down_beam_startpos, Vec4f(0, 0, -1, 0), /*max_t=*/1.0e3f, /*ignore body id=*/JPH::BodyID(), trace_results);
		const float down_beam_len = trace_results.hit_object ? trace_results.hit_t : 1000.0f;
		const Vec4f lower_hit_normal = trace_results.hit_object ? normalise(trace_results.hit_normal_ws) : Vec4f(0, 0, 1, 0);

		const Vec4f down_beam_hitpos = down_beam_startpos + Vec4f(0, 0, -1, 0) * down_beam_len;

		Matrix4f scale_matrix = Matrix4f::scaleMatrix(/*radius=*/0.05f, /*radius=*/0.05f, down_beam_len);
		Matrix4f ob_to_world = Matrix4f::translationMatrix(down_beam_hitpos) * scale_matrix;
		ob_placement_beam->ob_to_world_matrix = ob_to_world;
		if(opengl_engine->isObjectAdded(ob_placement_beam))
			opengl_engine->updateObjectTransformData(*ob_placement_beam);

		// Place hit marker
		const Matrix4f marker_scale_matrix = Matrix4f::scaleMatrix(0.2f, 0.2f, 0.01f);
		const Matrix4f orientation = Matrix4f::constructFromVectorStatic(lower_hit_normal);
		ob_placement_marker->ob_to_world_matrix = Matrix4f::translationMatrix(down_beam_hitpos) *
			orientation * marker_scale_matrix;
		if(opengl_engine->isObjectAdded(ob_placement_marker))
			opengl_engine->updateObjectTransformData(*ob_placement_marker);

		//----------------------- Place x, y, z axis arrows. -----------------------
		if(axis_and_rot_obs_enabled)
		{
			const Vec4f use_ob_origin = opengl_ob->ob_to_world_matrix.getColumn(3);
			const Vec4f cam_to_ob = use_ob_origin - cam_controller.getPosition().toVec4fPoint();
			const float control_scale = cam_to_ob.length() * 0.2f;

			const Vec4f arrow_origin = use_ob_origin;
			const float arrow_len = control_scale;

			axis_arrow_segments[0] = LineSegment4f(arrow_origin, arrow_origin + Vec4f(cam_to_ob[0] > 0 ? -arrow_len : arrow_len, 0, 0, 0)); // Put arrows on + or - x axis, facing towards camera.
			axis_arrow_segments[1] = LineSegment4f(arrow_origin, arrow_origin + Vec4f(0, cam_to_ob[1] > 0 ? -arrow_len : arrow_len, 0, 0));
			axis_arrow_segments[2] = LineSegment4f(arrow_origin, arrow_origin + Vec4f(0, 0, cam_to_ob[2] > 0 ? -arrow_len : arrow_len, 0));

			//axis_arrow_segments[3] = LineSegment4f(arrow_origin, arrow_origin + Vec4f(arrow_len, 0, 0, 0)); // Put arrows on + or - x axis, facing towards camera.
			//axis_arrow_segments[4] = LineSegment4f(arrow_origin, arrow_origin + Vec4f(0, arrow_len, 0, 0));
			//axis_arrow_segments[5] = LineSegment4f(arrow_origin, arrow_origin + Vec4f(0, 0, arrow_len, 0));

			for(int i=0; i<NUM_AXIS_ARROWS; ++i)
			{
				axis_arrow_objects[i]->ob_to_world_matrix = OpenGLEngine::arrowObjectTransform(axis_arrow_segments[i].a, axis_arrow_segments[i].b, arrow_len);
				if(opengl_engine->isObjectAdded(axis_arrow_objects[i]))
					opengl_engine->updateObjectTransformData(*axis_arrow_objects[i]);
			}

			//----------------------- Update rotation control handle arcs -----------------------
			const Vec4f arc_centre = use_ob_origin;
			const float arc_radius = control_scale * 0.7f; // Make the arcs not stick out so far from the centre as the arrows.

			for(int i=0; i<3; ++i)
			{
				const Vec4f basis_a = basis_vectors[i*2];
				const Vec4f basis_b = basis_vectors[i*2 + 1];

				const Vec4f to_cam = cam_controller.getPosition().toVec4fPoint() - arc_centre;
				const float to_cam_angle = safeATan2(dot(basis_b, to_cam), dot(basis_a, to_cam)); // angle in basis_a-basis_b plane

				// Position the rotation arc so its oriented towards the camera, unless the user is currently holding and dragging the arc.
				float angle = to_cam_angle;
				if(grabbed_axis >= NUM_AXIS_ARROWS)
				{
					int grabbed_rot_axis = grabbed_axis - NUM_AXIS_ARROWS;
					if(i == grabbed_rot_axis)
						angle = grabbed_angle + grabbed_arc_angle_offset;
				}

				// Position the arc line segments used for mouse picking.
				const float start_angle = angle - arc_handle_half_angle - 0.1f; // Extend a little so the arrow heads can be selected
				const float end_angle   = angle + arc_handle_half_angle + 0.1f;

				const size_t N = 32;
				rot_handle_lines[i].resize(N);
				for(size_t z=0; z<N; ++z)
				{
					const float theta_0 = start_angle + (end_angle - start_angle) * z       / N;
					const float theta_1 = start_angle + (end_angle - start_angle) * (z + 1) / N;

					const Vec4f p0 = arc_centre + basis_a * cos(theta_0) * arc_radius + basis_b * sin(theta_0) * arc_radius;
					const Vec4f p1 = arc_centre + basis_a * cos(theta_1) * arc_radius + basis_b * sin(theta_1) * arc_radius;

					(rot_handle_lines[i])[z] = LineSegment4f(p0, p1);
				}

				rot_handle_arc_objects[i]->ob_to_world_matrix = Matrix4f::translationMatrix(arc_centre) *
					Matrix4f::rotationMatrix(crossProduct(basis_a, basis_b), angle - arc_handle_half_angle) * Matrix4f(basis_a, basis_b, crossProduct(basis_a, basis_b), Vec4f(0, 0, 0, 1))
					* Matrix4f::uniformScaleMatrix(arc_radius);

				if(opengl_engine->isObjectAdded(rot_handle_arc_objects[i]))
					opengl_engine->updateObjectTransformData(*rot_handle_arc_objects[i]);
			}
		}
	}

	if(selected_ob && selected_ob->edit_aabb)
	{
		selected_ob->edit_aabb->ob_to_world_matrix = obToWorldMatrix(*selected_ob) * Matrix4f::translationMatrix(-DECAL_EDGE_AABB_WIDTH, -DECAL_EDGE_AABB_WIDTH, -DECAL_EDGE_AABB_WIDTH);
		opengl_engine->updateObjectTransformData(*selected_ob->edit_aabb);
	}
}



bool GUIClient::objectIsInParcelForWhichLoggedInUserHasWritePerms(const WorldObject& ob) const
{
	assert(this->logged_in_user_id.valid());

	const Vec4f ob_pos = ob.pos.toVec4fPoint();

	Lock lock(world_state->mutex);
	for(auto& it : world_state->parcels)
	{
		const Parcel* parcel = it.second.ptr();
		if(parcel->pointInParcel(ob_pos) && parcel->userHasWritePerms(this->logged_in_user_id))
			return true;
	}

	return false;
}


bool GUIClient::objectModificationAllowed(const WorldObject& ob)
{
	if(!this->logged_in_user_id.valid())
	{
		return false;
	}
	else
	{
		return (this->logged_in_user_id == ob.creator_id) || // If the logged in user created the object,
			isGodUser(this->logged_in_user_id) || // Or the user is the 'god' (superadmin) user,
			(this->connected_world_details.owner_id == this->logged_in_user_id) || // If this is the world of the user:
			objectIsInParcelForWhichLoggedInUserHasWritePerms(ob);
	}
}


bool GUIClient::connectedToUsersWorldOrGodUser()
{
	if(!this->logged_in_user_id.valid())
	{
		return false;
	}
	else
	{
		return isGodUser(this->logged_in_user_id) || // The logged in user is the 'god' (superadmin) user,
			(this->connected_world_details.owner_id == this->logged_in_user_id); // or if this is the world of the user:
	}
}


// Similar to objectModificationAllowed() above, but also shows error notifications if modification is not allowed
bool GUIClient::objectModificationAllowedWithMsg(const WorldObject& ob, const std::string& action)
{
	bool allow_modification = true;
	if(!this->logged_in_user_id.valid())
	{
		allow_modification = false;

		// Display an error message if we have not already done so since selecting this object.
		if(!shown_object_modification_error_msg)
		{
			showErrorNotification("You must be logged in to " + action + " an object.");
			shown_object_modification_error_msg = true;
		}
	}
	else
	{
		const bool logged_in_user_can_modify = (this->logged_in_user_id == ob.creator_id) || // If the logged in user created the object
			isGodUser(this->logged_in_user_id) || // Or the user is the 'god' (superadmin) user,
			(this->connected_world_details.owner_id == this->logged_in_user_id) || // If this is the world of the user:
			objectIsInParcelForWhichLoggedInUserHasWritePerms(ob); // Can modify objects owned by other people if they are in parcels you have write permissions for.
		
		if(!logged_in_user_can_modify)
		{
			allow_modification = false;

			// Display an error message if we have not already done so since selecting this object.
			if(!shown_object_modification_error_msg)
			{
				showErrorNotification("You must be the owner of this object to " + action + " it.  This object is owned by '" + ob.creator_name + "'.");
				shown_object_modification_error_msg = true;
			}
		}
	}
	return allow_modification;
}


// ObLoadingCallbacks interface callback function:
// NOTE: not currently called.
//void GUIClient::loadObject(WorldObjectRef ob)
//{
//	loadModelForObject(ob.ptr());
//
//	loadAudioForObject(ob.ptr());
//}


// ObLoadingCallbacks interface callback function:
void GUIClient::unloadObject(WorldObjectRef ob)
{
	//conPrint("unloadObject");
	removeAndDeleteGLAndPhysicsObjectsForOb(*ob);

	if(ob->audio_source.nonNull())
	{
		audio_engine.removeSource(ob->audio_source);
		ob->audio_source = NULL;
		ob->audio_state = WorldObject::AudioState_NotLoaded;
	}
}


// ObLoadingCallbacks interface callback function:
void GUIClient::newCellInProximity(const Vec3<int>& cell_coords)
{
	if(this->client_thread.nonNull())
	{
		// Make QueryObjects packet and enqueue to send to server
		MessageUtils::initPacket(scratch_packet, Protocol::QueryObjects);
		writeToStream<double>(this->cam_controller.getPosition(), scratch_packet); // Send camera position
		scratch_packet.writeUInt32(1); // Num cells to query
		scratch_packet.writeInt32(cell_coords.x);
		scratch_packet.writeInt32(cell_coords.y);
		scratch_packet.writeInt32(cell_coords.z);

		enqueueMessageToSend(*this->client_thread, scratch_packet);
	}
}


void GUIClient::tryToMoveObject(WorldObjectRef ob, /*const Matrix4f& tentative_new_to_world*/const Vec4f& desired_new_ob_pos)
{
	Lock lock(world_state->mutex);

	GLObjectRef opengl_ob = this->selected_ob->opengl_engine_ob;
	if(opengl_ob.isNull())
	{
		// conPrint("GUIClient::tryToMoveObject: opengl_ob is NULL");
		return;
	}

	Matrix4f tentative_new_to_world = opengl_ob->ob_to_world_matrix;
	tentative_new_to_world.setColumn(3, desired_new_ob_pos);

	const js::AABBox tentative_new_aabb_ws = opengl_engine->getAABBWSForObjectWithTransform(*opengl_ob, tentative_new_to_world);

	// Check parcel permissions for this object
	bool ob_pos_in_parcel;
	const bool have_creation_perms = haveObjectWritePermissions(*this->selected_ob, tentative_new_aabb_ws, ob_pos_in_parcel);
	if(!have_creation_perms)
	{
		if(ob_pos_in_parcel)
			showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
		else
			showErrorNotification("You can only move objects in a parcel that you have write permissions for.");
	}

	// Constrain the new position of the selected object so it stays inside the parcel it is currently in.
	js::Vector<EdgeMarker, 16> edge_markers;
	Vec3d new_ob_pos;
	const bool new_transform_valid = clampObjectPositionToParcelForNewTransform(*this->selected_ob, opengl_ob, 
		this->selected_ob->pos, // old ob pos
		tentative_new_to_world, // tentative new transfrom
		edge_markers, // edge markers out.
		new_ob_pos // new_ob_pos_out
	);
	if(new_transform_valid)
	{
		//----------- Display any edge markers -----------
		// Add new edge markers if needed
		while(ob_denied_move_markers.size() < edge_markers.size())
		{
			GLObjectRef new_marker = opengl_engine->allocateObject();
			new_marker->mesh_data = this->ob_denied_move_marker->mesh_data; // copy mesh ref from prototype gl ob.
			new_marker->materials = this->ob_denied_move_marker->materials; // copy materials
			new_marker->ob_to_world_matrix = Matrix4f::identity();
			ob_denied_move_markers.push_back(new_marker);

			opengl_engine->addObject(new_marker);
		}

		// Remove any surplus edge markers
		while(ob_denied_move_markers.size() > edge_markers.size())
		{
			opengl_engine->removeObject(ob_denied_move_markers.back());
			ob_denied_move_markers.pop_back();
		}

		assert(ob_denied_move_markers.size() == edge_markers.size());

		// Set edge marker gl object transforms
		for(size_t i=0; i<ob_denied_move_markers.size(); ++i)
		{
			const float use_scale = myMax(0.5f, edge_markers[i].scale * 1.4f);
			const Matrix4f marker_scale_matrix = Matrix4f::scaleMatrix(use_scale, use_scale, 0.01f);
			const Matrix4f orientation = Matrix4f::constructFromVectorStatic(edge_markers[i].normal);

			ob_denied_move_markers[i]->ob_to_world_matrix = Matrix4f::translationMatrix(edge_markers[i].pos) * 
				orientation * marker_scale_matrix;

			opengl_engine->updateObjectTransformData(*ob_denied_move_markers[i]);
		}
		//----------- End display edge markers -----------

		runtimeCheck(opengl_ob.nonNull() && opengl_ob->mesh_data.nonNull());

		doMoveObject(ob, new_ob_pos, opengl_ob->mesh_data->aabb_os);
	} 
	else // else if new transfrom not valid
	{
		showErrorNotification("New object position is not valid - You can only move objects in a parcel that you have write permissions for.");
	}
}


void GUIClient::doMoveObject(WorldObjectRef ob, const Vec3d& new_ob_pos, const js::AABBox& aabb_os)
{
	doMoveAndRotateObject(ob, new_ob_pos, ob->axis, ob->angle, aabb_os, /*summoning_object=*/false);
}


// Sets object velocity to zero also.
void GUIClient::doMoveAndRotateObject(WorldObjectRef ob, const Vec3d& new_ob_pos, const Vec3f& new_axis, float new_angle, const js::AABBox& aabb_os, bool summoning_object)
{
	GLObjectRef opengl_ob = ob->opengl_engine_ob;

	// Set world object pos
	ob->setTransformAndHistory(new_ob_pos, new_axis, new_angle);

	ob->transformChanged();

	ob->last_modified_time = TimeStamp::currentTime(); // Gets set on server as well, this is just for updating the local display.

	// Set graphics object pos and update in opengl engine.
	const Matrix4f new_to_world = obToWorldMatrix(*ob);

	if(opengl_ob.nonNull())
	{
		opengl_ob->ob_to_world_matrix = new_to_world;
		opengl_engine->updateObjectTransformData(*opengl_ob);
	}

	// Update physics object
	if(ob->physics_object.nonNull())
	{
		physics_world->setNewObToWorldTransform(*ob->physics_object, new_ob_pos.toVec4fPoint(), Quatf::fromAxisAndAngle(normalise(ob->axis.toVec4fVector()), ob->angle), useScaleForWorldOb(ob->scale).toVec4fVector());

		physics_world->setLinearAndAngularVelToZero(*ob->physics_object);
	}

	// Update in Indigo view
	//ui->indigoView->objectTransformChanged(*ob);

	// Set a timer to call updateObjectEditorObTransformSlot() later. Not calling this every frame avoids stutters with webviews playing back videos interacting with Qt updating spinboxes.
	ui_interface->startObEditorTimerIfNotActive();

	
	if(summoning_object)
	{
		// Send a single SummonObject message to server.
		MessageUtils::initPacket(scratch_packet, Protocol::SummonObject);
		writeToStream(ob->uid, scratch_packet);
		writeToStream(Vec3d(ob->pos), scratch_packet);
		writeToStream(Vec3f(ob->axis), scratch_packet);
		scratch_packet.writeFloat(ob->angle);

		enqueueMessageToSend(*this->client_thread, scratch_packet);
	}
	else
	{
		// Mark as from-local-dirty to send an object transform updated message to the server
		ob->from_local_transform_dirty = true;
		this->world_state->dirty_from_local_objects.insert(ob);
	}


	if(ob->isDynamic() && !isObjectPhysicsOwnedBySelf(*ob, world_state->getCurrentGlobalTime()) && !isObjectVehicleBeingDrivenByOther(*ob))
	{
		// conPrint("==Taking ownership of physics object in tryToMoveObject()...==");
		takePhysicsOwnershipOfObject(*ob, world_state->getCurrentGlobalTime());
	}

	// Trigger sending update-lightmap update flag message later.
	//ob->flags |= WorldObject::LIGHTMAP_NEEDS_COMPUTING_FLAG;
	//objs_with_lightmap_rebuild_needed.insert(ob);
	//lightmap_flag_timer->start(/*msec=*/2000); 

	// Update audio source position in audio engine.
	if(ob->audio_source.nonNull())
	{
		ob->audio_source->pos = ob->getCentroidWS();
		audio_engine.sourcePositionUpdated(*ob->audio_source);
	}

	if(this->terrain_system.nonNull() && ::hasPrefix(ob->content, "biome:"))
		this->terrain_system->invalidateVegetationMap(ob->getAABBWS());

	if(ob->object_type == WorldObject::ObjectType_Spotlight)
	{
		GLLightRef light = ob->opengl_light;
		if(light.nonNull())
		{
			opengl_engine->setLightPos(light, new_ob_pos.toVec4fPoint());
		}
	}
}


static inline float xyDist2(const Vec4f& a, const Vec4f& b)
{
	Vec4f a_to_b = b - a;
	return _mm_cvtss_f32(_mm_dp_ps(a_to_b.v, a_to_b.v, 0x3F));
}

static inline bool shouldDisplayLODChunk(const Vec3i& chunk_coords, const Vec4f& campos)
{
	const Vec4f chunk_centre = Vec4f((chunk_coords.x + 0.5f) * chunk_w, (chunk_coords.y + 0.5f) * chunk_w, 0, 1);
	
	const float CHUNK_DIST_THRESHOLD = 150.f;
	
	const float dist_to_chunk2 = xyDist2(campos, chunk_centre);

	return dist_to_chunk2 > Maths::square(CHUNK_DIST_THRESHOLD);
}


void GUIClient::checkForLODChanges(Timer& timer_event_timer)
{
	ZoneScoped; // Tracy profiler

	if(world_state.isNull())
		return;
		
	Timer timer;
	{
		WorldStateLock lock(this->world_state->mutex);

		// Make sure server_using_lod_chunks is up-to-date.
		if(!this->world_state->lod_chunks.empty() && LOD_CHUNK_SUPPORT)
			this->server_using_lod_chunks = true;
		const bool use_server_using_lod_chunks = this->server_using_lod_chunks;


		const Vec4f cam_pos = cam_controller.getPosition().toVec4fPoint();
#if EMSCRIPTEN
		const float proj_len_viewable_threshold = 0.02f;
		const float min_load_distance2 = Maths::square(60.f); // Load everything <= this distance.
#endif
		const float load_distance2_ = this->load_distance2;

		glare::FastIterMapValueInfo<UID, WorldObjectRef>* const objects_data = this->world_state->objects.vector.data();
		const size_t objects_size                                            = this->world_state->objects.vector.size();

		// Process just some of the objects each frame, to reduce CPU time and ease pressure on the cache.
		const size_t num_slices = 4;
		const size_t max_num = Maths::roundedUpDivide(objects_size, num_slices);
		const size_t begin_i = myMin(objects_size, this->next_lod_changes_begin_i);
		const size_t end_i   = myMin(objects_size, begin_i + max_num);

		//conPrint("checking slice from " + toString(begin_i) + " to " + toString(end_i));
		size_t num_object_changes = 0;
		size_t i;
		for(i = begin_i; i<end_i; ++i)
		{
			if(i + 16 < end_i)
				_mm_prefetch((const char*)(&objects_data[i + 16].value->centroid_ws), _MM_HINT_T0);

			WorldObject* const ob = objects_data[i].value.ptr();

			const Vec4f centroid = ob->getCentroidWS();

			const float cam_to_ob_d2 = ob->getCentroidWS().getDist2(cam_pos);
			//const float cam_to_ob_d2 = ob->getAABBWS().getClosestPointInAABB(cam_pos).getDist2(cam_pos);
#if EMSCRIPTEN
			const float recip_dist = _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(cam_to_ob_d2)));
			//assert(epsEqual(recip_dist, 1 / sqrt(cam_to_ob_d2)));

			const float proj_len = ob->getBiasedAABBLength() * recip_dist;

			bool in_proximity = (proj_len > proj_len_viewable_threshold) || (cam_to_ob_d2 <= min_load_distance2);
#else
			bool in_proximity = cam_to_ob_d2 < load_distance2_;
#endif

			// If this object is in a chunk region, and we are displaying the chunk, then don't show the object.
			const Vec3i chunk_coords(Maths::floorToInt(centroid[0] / chunk_w), Maths::floorToInt(centroid[1] / chunk_w), 0);
			if(use_server_using_lod_chunks && shouldDisplayLODChunk(chunk_coords, cam_pos) && !ob->exclude_from_lod_chunk_mesh)
				in_proximity = false;

			assert(ob->exclude_from_lod_chunk_mesh == BitUtils::isBitSet(ob->flags, WorldObject::EXCLUDE_FROM_LOD_CHUNK_MESH));

			bool object_changed = false;
			if(!in_proximity) // If object is out of load distance:
			{
				if(ob->in_proximity) // If an object was in proximity to the camera, and moved out of load distance:
				{
					unloadObject(ob);
					ob->in_proximity = false;
					object_changed = true;
				}
			}
			else // Else if object is within load distance:
			{
				const int lod_level = ob->getLODLevel(cam_to_ob_d2);

				if((lod_level != ob->current_lod_level)/* || ob->opengl_engine_ob.isNull()*/)
				{
					loadModelForObject(ob, lock);
					ob->current_lod_level = lod_level;
					object_changed = true;
					// conPrint("Changing LOD level for object " + ob->uid.toString() + " to " + toString(lod_level));
				}

				if(!ob->in_proximity) // If an object was out of load distance, and moved within load distance:
				{
					ob->in_proximity = true;
					loadModelForObject(ob, lock);
					ob->current_lod_level = lod_level;
					object_changed = true;
				}
			}

			if(object_changed)
			{
				num_object_changes++;
				const double elapsed = /*timer*/timer_event_timer.elapsed();
				if(elapsed > 0.0035f)
				{
					//conPrint("checkForLODChanges(): breaking after " + toString(num_object_changes) + " changes, timer_event_timer: " + doubleToStringNDecimalPlaces(elapsed * 1.0e3, 2) + " ms");
					break;
				}
			}
		}

		this->next_lod_changes_begin_i = i;
		if(this->next_lod_changes_begin_i >= objects_size)
			this->next_lod_changes_begin_i = 0;

		//conPrint("checkForLODChanges took " + timer.elapsedStringMSWIthNSigFigs(4) + ", " + toString(num_object_changes) + " changes, " + toString(world_state->objects.size()) + " obs, begin: " + toString(begin_i) + 
		//	", end i: " + toString(i));

	} // End lock scope
}


void GUIClient::checkForAudioRangeChanges()
{
	ZoneScoped; // Tracy profiler

	if(world_state.isNull())
		return;

	//Timer timer;
	{
		Lock lock(this->world_state->mutex);

		const Vec4f cam_pos = cam_controller.getPosition().toVec4fPoint();

		/*{
			for(auto it = this->world_state->objects.valuesBegin(); it != this->world_state->objects.valuesEnd(); ++it)
			{
				WorldObject* ob = it.getValue().ptr();
				if(!ob->audio_source_url.empty() || ob->audio_source.nonNull())
				{
					runtimeCheck(this->audio_obs.count(ob) > 0);
				}
			}
		}*/

		for(auto it = this->audio_obs.begin(); it != audio_obs.end(); ++it)
		{
			WorldObject* ob = it->ptr();

			const float dist2 = cam_pos.getDist2(ob->pos.toVec4fPoint());
			const float max_audio_dist2 = Maths::square(maxAudioDistForSourceVolFactor(ob->audio_volume)); // MAX_AUDIO_DIST
			
			if(ob->in_audio_proximity && (dist2 > max_audio_dist2)) // If object was in audio proximity, and moved out of it:
			{
				// conPrint("Object moved out of audio range, removing audio object.  ob->audio_source_url: '" + ob->audio_source_url + "'");
				if(ob->audio_source)
					audio_engine.removeSource(ob->audio_source);
				ob->audio_source = NULL;
				ob->audio_state = WorldObject::AudioState_NotLoaded;
				
				ob->in_audio_proximity = false;
			}
			else if(!ob->in_audio_proximity && (dist2 <= max_audio_dist2)) // If object was out of audio proximity, and moved into it:
			{
				if(!ob->audio_source_url.empty())
				{
					// conPrint("Object moved in to audio range, loading audio object.  ob->audio_source_url: '" + ob->audio_source_url + "'");
					loadAudioForObject(ob, /*loaded buffer=*/nullptr);

					// Trigger downloading of resources, now we are in audio range, since audio resource won't have been downloaded before when out of range.
					// TODO: process audio resource only?
					startDownloadingResourcesForObject(ob, ob->current_lod_level);
				}
				
				ob->in_audio_proximity = true;
			}
		}
	} // End lock scope
	//conPrint("checkForAudioRangeChanges took " + timer.elapsedStringMSWIthNSigFigs(4) + " (" + toString(audio_obs.size()) + " audio obs)");
}


struct CloserToCamComparator
{
	CloserToCamComparator(const Vec4f& campos_) : campos(campos_) {}

	bool operator () (const Vec4f& a, const Vec4f& b)
	{
		return a.getDist2(campos) < b.getDist2(campos);
	}

	Vec4f campos;
};



void GUIClient::handleUploadedMeshData(const URLString& lod_model_url, int loaded_model_lod_level, bool dynamic_physics_shape, OpenGLMeshRenderDataRef mesh_data, PhysicsShape& physics_shape, 
	int voxel_subsample_factor, uint64 voxel_hash)
{
	// conPrint("handleUploadedMeshData(): lod_model_url: " + lod_model_url);
	ZoneScoped; // Tracy profiler

	// Now that this model is loaded, remove from models_processing set.
	// If the model is unloaded, then this will allow it to be reprocessed and reloaded.
	ModelProcessingKey key(lod_model_url, dynamic_physics_shape);
	models_processing.erase(key);


	// Add meshes to mesh manager
	Reference<MeshData> the_mesh_data					= mesh_manager.insertMesh(lod_model_url, mesh_data);
	Reference<PhysicsShapeData> physics_shape_data		= mesh_manager.insertPhysicsShape(MeshManagerPhysicsShapeKey(lod_model_url, /*is dynamic=*/dynamic_physics_shape), physics_shape);

	the_mesh_data->voxel_subsample_factor = voxel_subsample_factor;


	// Data is uploaded - assign the loaded model for any objects using waiting for this model:
	WorldStateLock lock(this->world_state->mutex);

	const ModelProcessingKey model_loading_key(lod_model_url, dynamic_physics_shape);
	auto res = this->loading_model_URL_to_world_ob_UID_map.find(model_loading_key);
	if(res != this->loading_model_URL_to_world_ob_UID_map.end())
	{
		std::set<UID>& waiting_obs = res->second;
		for(auto it = waiting_obs.begin(); it != waiting_obs.end(); ++it)
		{
			const UID waiting_uid = *it;

			auto res2 = this->world_state->objects.find(waiting_uid);
			if(res2 != this->world_state->objects.end())
			{
				WorldObject* ob = res2.getValue().ptr();

				//ob->aabb_os = mesh_data->aabb_os;

				if(ob->in_proximity)
				{
					const int ob_lod_level = ob->getLODLevel(cam_controller.getPosition());
					const int ob_model_lod_level = myClamp(ob_lod_level, 0, ob->max_model_lod_level);
								
					// Check the object wants this particular LOD level model right now:
					//const std::string current_desired_model_LOD_URL = ob->getLODModelURLForLevel(ob->model_url, ob_model_lod_level);
					if(/*(current_desired_model_LOD_URL == lod_model_url)*/(ob_model_lod_level == loaded_model_lod_level) && (ob->isDynamic() == dynamic_physics_shape))
					{
						try
						{
							if(!isFinite(ob->angle) || !ob->axis.isFinite())
								throw glare::Exception("Invalid angle or axis");

							loadPresentObjectGraphicsAndPhysicsModels(ob, the_mesh_data, physics_shape_data, ob_lod_level, ob_model_lod_level, voxel_subsample_factor, lock);
						}
						catch(glare::Exception& e)
						{
							print("Error while loading model: " + e.what());
						}
					}
				}
			}
		}

		loading_model_URL_to_world_ob_UID_map.erase(model_loading_key); // Now that this model has been downloaded, remove from map
	}

	// Assign the loaded model to any avatars waiting for this model:
	auto waiting_av_res = this->loading_model_URL_to_avatar_UID_map.find(lod_model_url);
	if(waiting_av_res != this->loading_model_URL_to_avatar_UID_map.end())
	{
		const std::set<UID>& waiting_avatars = waiting_av_res->second;
		for(auto it = waiting_avatars.begin(); it != waiting_avatars.end(); ++it)
		{
			const UID waiting_uid = *it;

			auto res2 = this->world_state->avatars.find(waiting_uid);
			if(res2 != this->world_state->avatars.end())
			{
				Avatar* av = res2->second.ptr();
						
				const bool our_avatar = av->uid == this->client_avatar_uid;
				if(cam_controller.thirdPersonEnabled() || !our_avatar) // Don't load graphics for our avatar if first person perspective
				{
					const int av_lod_level = av->getLODLevel(cam_controller.getPosition());

					// Check the avatar wants this particular LOD level model right now:
					// If we are using the default avatar, make sure this check doesn't fail due to getLODModelURLForLevel() appending "_optX" suffix.
					Avatar::GetLODModelURLOptions options(this->server_has_optimised_meshes, this->server_opt_mesh_version);
					const URLString current_desired_model_LOD_URL = av->getLODModelURLForLevel(av->avatar_settings.model_url, av_lod_level, options);
					if((current_desired_model_LOD_URL == lod_model_url) || (av->avatar_settings.model_url == DEFAULT_AVATAR_MODEL_URL))
					{
						try
						{
							loadPresentAvatarModel(av, av_lod_level, the_mesh_data);
						}
						catch(glare::Exception& e)
						{
							print("Error while loading avatar model: " + e.what());
						}
					}
				}
			}
		}
	}

	// Assign to any LOD chunks using this model
	if(hasPrefix(lod_model_url, "chunk_")) // Bit of a hack to distinguish chunk mesh URLs.  Could do this by passing around a 'bool loading_chunk' instead.
		handleLODChunkMeshLoaded(lod_model_url, the_mesh_data, lock);
}


void GUIClient::handleUploadedTexture(const OpenGLTextureKey& path, const URLString& URL, const OpenGLTextureRef& opengl_tex, const TextureDataRef& tex_data, const Map2DRef& terrain_map)
{
	ZoneScoped; // Tracy profiler

	// Assign to terrain
	if(terrain_map && terrain_system)
		terrain_system->handleTextureLoaded(path, terrain_map);

	// Assign to minimap tiles
	if(minimap)
		minimap->handleUploadedTexture(path, URL, opengl_tex);

	// Look up any LODChunks, objects or avatars using this texture, and assign the newly loaded texture to them.
	{
		WorldStateLock lock(this->world_state->mutex);

		//---------------------------- Assign to hypercards ----------------------------
		{
			ZoneScopedN("Assign to hypercards");
			if(hasPrefix(path, "_HYPCRD"))
			{
				auto res = loading_texture_key_to_hypercard_UID_map.find(path);
				if(res != this->loading_texture_key_to_hypercard_UID_map.end())
				{
					const std::set<UID>& waiting_obs = res->second;
					for(auto it = waiting_obs.begin(); it != waiting_obs.end(); ++it)
					{
						const UID waiting_uid = *it;
						auto res2 = this->world_state->objects.find(waiting_uid);
						if(res2 != this->world_state->objects.end())
						{
							WorldObject* ob = res2.getValue().ptr();
							if(ob->opengl_engine_ob)
							{
								ob->opengl_engine_ob->materials[0].albedo_texture = opengl_tex;
								opengl_engine->objectMaterialsUpdated(*ob->opengl_engine_ob);
							}
						}
					}
					loading_texture_key_to_hypercard_UID_map.erase(res); // Now that this texture has been loaded, remove from map
				}
			}
		}

		//---------------------------- Assign to LOD chunks ----------------------------
		{ 
			ZoneScopedN("Assign to LOD chunks");

			auto res = loading_texture_URL_to_chunk_coords_map.find(URL);
			if(res != loading_texture_URL_to_chunk_coords_map.end())
			{
				const Vec3i coords = res->second;
				auto res2 = world_state->lod_chunks.find(coords);
				if(res2 != world_state->lod_chunks.end())
				{
					LODChunk* chunk = res2->second.ptr();
					if(path == chunk->combined_array_texture_path)
					{
						if(chunk->graphics_ob)
						{
							// conPrint("handleLODChunkTextureLoaded(): Loading combined_array_texture " + path);

							if(opengl_tex->getTextureTarget() != GL_TEXTURE_2D_ARRAY)
								conPrint("Error, loaded chunk combined texture is not a GL_TEXTURE_2D_ARRAY (path: " + std::string(path) + ")");

							chunk->graphics_ob->materials[0].combined_array_texture = opengl_tex;

							if(chunk->graphics_ob_in_engine)
								opengl_engine->materialTextureChanged(*chunk->graphics_ob, chunk->graphics_ob->materials[0]);
						}
					}
				}
				loading_texture_URL_to_chunk_coords_map.erase(res); // Now that this texture has been loaded, remove from map
			}
		}

		//---------------------------- Assign to objects ----------------------------
		{
			ZoneScopedN("Assign to objects");

			auto res = this->loading_texture_URL_to_world_ob_UID_map.find(URL);
			if(res != this->loading_texture_URL_to_world_ob_UID_map.end())
			{
				const std::set<UID>& waiting_obs = res->second;
				for(auto it = waiting_obs.begin(); it != waiting_obs.end(); ++it)
				{
					const UID waiting_uid = *it;
					auto res2 = this->world_state->objects.find(waiting_uid);
					if(res2 != this->world_state->objects.end())
					{
						WorldObject* ob = res2.getValue().ptr();
						assignLoadedOpenGLTexturesToMats(ob);
					}
				}
				loading_texture_URL_to_world_ob_UID_map.erase(res); // Now that this texture has been loaded, remove from map
			}
		}

		//---------------------------- Assign to avatars ----------------------------
		{
			ZoneScopedN("Assign to avatars");

			auto res = this->loading_texture_URL_to_avatar_UID_map.find(URL);
			if(res != this->loading_texture_URL_to_avatar_UID_map.end())
			{
				const std::set<UID>& waiting_avs = res->second;
				for(auto it = waiting_avs.begin(); it != waiting_avs.end(); ++it)
				{
					const UID waiting_uid = *it;
					auto res2 = this->world_state->avatars.find(waiting_uid);
					if(res2 != this->world_state->avatars.end())
					{
						Avatar* av = res2->second.ptr();

						glare::ArenaAllocator use_arena = arena_allocator.getFreeAreaArenaAllocator();
						assignLoadedOpenGLTexturesToAvatarMats(av, /*use basis=*/this->server_has_basis_textures, *opengl_engine, *resource_manager, *animated_texture_manager, &use_arena);
					}
				}
				loading_texture_URL_to_avatar_UID_map.erase(res); // Now that this texture has been loaded, remove from map
			}
		}
	}
}


// loaded_mesh may be null for the default xbot model.
void GUIClient::updateOurAvatarModel(BatchedMeshRef loaded_mesh, const std::string& local_model_path, const Matrix4f& pre_ob_to_world_matrix, const std::vector<WorldMaterialRef>& materials)
{
	conPrint("GUIClient::updateOurAvatarModel()");

	if(!logged_in_user_id.valid())
		throw glare::Exception("You must be logged in to set your avatar model");

	URLString mesh_URL;
	if(local_model_path != "")
	{
		// If the user selected a mesh that is not a bmesh, convert it to bmesh
		std::string bmesh_disk_path = local_model_path;
		if(!hasExtension(local_model_path, "bmesh"))
		{
			// Save as bmesh in temp location
			bmesh_disk_path = PlatformUtils::getTempDirPath() + "/temp.bmesh";

			BatchedMesh::WriteOptions write_options;
			write_options.compression_level = 9; // Use a somewhat high compression level, as this mesh is likely to be read many times, and only encoded here.
			// TODO: show 'processing...' dialog while it compresses and saves?
			loaded_mesh->writeToFile(bmesh_disk_path, write_options);
		}
		else
		{
			bmesh_disk_path = local_model_path;
		}

		// Compute hash over model
		const uint64 model_hash = FileChecksum::fileChecksum(bmesh_disk_path);

		const std::string original_filename = FileUtils::getFilename(local_model_path); // Use the original filename, not 'temp.igmesh'.
		mesh_URL = ResourceManager::URLForNameAndExtensionAndHash(original_filename, ::getExtension(bmesh_disk_path), model_hash); // ResourceManager::URLForPathAndHash(igmesh_disk_path, model_hash);

		// Copy model to local resources dir.  UploadResourceThread will read from here.
		conPrint("updateOurAvatarModel(): copying " + bmesh_disk_path + " to local resource dir for URL " + toStdString(mesh_URL));
		conPrint("model_hash: " + toString(model_hash));
		resource_manager->copyLocalFileToResourceDir(bmesh_disk_path, mesh_URL);
	}

	const Vec3d cam_angles = cam_controller.getAvatarAngles();
	Avatar avatar;
	avatar.uid = client_avatar_uid;
	avatar.pos = Vec3d(cam_controller.getFirstPersonPosition());
	avatar.rotation = Vec3f(0, (float)cam_angles.y, (float)cam_angles.x);
	avatar.name = logged_in_user_name;
	avatar.avatar_settings.model_url = mesh_URL;
	avatar.avatar_settings.pre_ob_to_world_matrix = pre_ob_to_world_matrix;
	avatar.avatar_settings.materials = materials;


	// Copy all dependencies (textures etc..) to resources dir.  UploadResourceThread will read from here.
	// Don't transform to basis URLs, the server will want the original PNG/JPEGs.
	Avatar::GetDependencyOptions options;
	options.get_optimised_mesh = false;
	options.use_basis = false;

	DependencyURLSet paths;
	avatar.getDependencyURLSet(/*ob_lod_level=*/0, options, paths);
	for(auto it = paths.begin(); it != paths.end(); ++it)
	{
		const auto path = it->URL;
		if(FileUtils::fileExists(path))
		{
			const URLString resource_URL = resource_manager->copyLocalFileToResourceDirAndReturnURL(toStdString(path));
			// conPrint("updateOurAvatarModel(): copied " + path + " to local resource dir for URL '" + resource_URL + "'.");
		}
	}

	// Convert texture paths on the object to URLs
	avatar.convertLocalPathsToURLS(*resource_manager);

	//if(!gui_client.task_manager)
	//	gui_client.task_manager = new glare::TaskManager("mainwindow general task manager", myClamp<size_t>(PlatformUtils::getNumLogicalProcessors() / 2, 1, 8)), // Currently just used for LODGeneration::generateLODTexturesForMaterialsIfNotPresent().

	// Generate LOD textures for materials, if not already present on disk.
	// LODGeneration::generateLODTexturesForMaterialsIfNotPresent(avatar.avatar_settings.materials, *gui_client.resource_manager, *gui_client.task_manager);

	// Send AvatarFullUpdate message to server
	MessageUtils::initPacket(scratch_packet, Protocol::AvatarFullUpdate);
	writeAvatarToNetworkStream(avatar, scratch_packet);

	enqueueMessageToSend(*client_thread, scratch_packet);

	showInfoNotification("Updated avatar.");

	conPrint("GUIClient::updateOurAvatarModel() done.");
}


void GUIClient::processLoading(Timer& timer_event_timer)
{
	ZoneScoped; // Tracy profiler

	if(!opengl_engine)
		return;

	//double frame_loading_time = 0;
	//std::vector<std::string> loading_times; // TEMP just for profiling/debugging
	if(world_state.nonNull())
	{
		ZoneScopedN("Process loaded messages"); // Tracy profiler

		// Process ModelLoadedThreadMessages and TextureLoadedThreadMessages until we have consumed a certain amount of time.
		// We don't want to do too much at one time or it will cause hitches.
		// We'll alternate between processing model loaded and texture loaded messages, using process_model_loaded_next.
		// We alternate for fairness.
		const double MAX_LOADING_TIME = 0.002;
		Timer loading_timer;
		//int max_items_to_process = 10;
		//int num_items_processed = 0;
		
		// Also limit to a total number of bytes of data uploaded to OpenGL / the GPU per frame.
		size_t total_bytes_uploaded = 0;
		const size_t max_total_upload_bytes = 1024 * 1024;

		//int num_models_loaded = 0;
		//int num_textures_loaded = 0;
		//while((cur_loading_mesh_data.nonNull() || !model_loaded_messages_to_process.empty() || !texture_loaded_messages_to_process.empty()) && (loading_timer.elapsed() < MAX_LOADING_TIME))
		while((tex_loading_progress.loadingInProgress() || cur_loading_mesh_data.nonNull() || !model_loaded_messages_to_process.empty() || !texture_loaded_messages_to_process.empty()) && 
			(total_bytes_uploaded < max_total_upload_bytes) && 
			(/*loading_timer*/timer_event_timer.elapsed() < MAX_LOADING_TIME) //&&
			/*(num_items_processed < max_items_to_process)*/)
		{
			//num_items_processed++;

			if(cur_loading_mesh_data)
			{
				try
				{
					opengl_engine->partialLoadOpenGLMeshDataIntoOpenGL(*opengl_engine->vert_buf_allocator, *cur_loading_mesh_data, mesh_data_loading_progress,
						total_bytes_uploaded, max_total_upload_bytes);
				}
				catch(glare::Exception& /*e*/)
				{
					//logMessage("Error while loading mesh '" + loading_item_name + "' into OpenGL: " + e.what());
					cur_loading_mesh_data = NULL;
				}

				if(mesh_data_loading_progress.done())
				{
					handleUploadedMeshData(cur_loading_lod_model_url, cur_loading_model_lod_level, cur_loading_dynamic_physics_shape, cur_loading_mesh_data, cur_loading_physics_shape, 
						cur_loading_voxel_subsample_factor, cur_loading_voxel_hash);

					cur_loading_mesh_data = NULL;
					cur_loading_lod_model_url.clear();
					cur_loading_model_lod_level = -1;
					cur_loading_physics_shape = PhysicsShape();
				}
			}
			// Else if we are still loading some texture data into OpenGL (uploading to GPU):
			else if(tex_loading_progress.loadingInProgress())
			{
				Timer load_item_timer;

				// Upload a chunk of data to the GPU
				try
				{
					TextureLoading::partialLoadTextureIntoOpenGL(tex_loading_progress, total_bytes_uploaded, max_total_upload_bytes);
				}
				catch(glare::Exception& e)
				{
					logMessage("Error while loading texture '" + std::string(tex_loading_progress.opengl_tex->key) + "' into OpenGL: " + e.what());
					tex_loading_progress.tex_data = NULL;
					tex_loading_progress.opengl_tex = NULL;
				}

				if(tex_loading_progress.done() || !tex_loading_progress.loadingInProgress())
				{
					// conPrint("Finished loading texture '" + tex_loading_progress.path + "' into OpenGL.  Was terrain: " + toString(cur_loading_terrain_map.nonNull()));

					opengl_engine->addOpenGLTexture(tex_loading_progress.path, tex_loading_progress.opengl_tex);

					handleUploadedTexture(tex_loading_progress.path, toURLString(tex_loading_progress.URL), tex_loading_progress.opengl_tex, tex_loading_progress.tex_data, cur_loading_terrain_map);

					tex_loading_progress.tex_data = NULL;
					tex_loading_progress.opengl_tex = NULL;
					
					// Now that this texture is loaded, remove from textures_processing set.
					// If the texture is unloaded, then this will allow it to be reprocessed and reloaded.
					//assert(textures_processing.count(tex_loading_progress.path) >= 1);
					textures_processing.erase(tex_loading_progress.path);
				}
			}
			else // else if !loading_mesh_data:
			{
				// ui->glWidget->makeCurrent();

				if(process_model_loaded_next && !model_loaded_messages_to_process.empty())
				{
					const Reference<ModelLoadedThreadMessage> message = model_loaded_messages_to_process.front();
					model_loaded_messages_to_process.pop_front();

					Timer load_item_timer;
					//size_t loaded_size_B = 0;

					// conPrint("Handling model loaded message, lod_model_url: " + message->lod_model_url);
					//num_models_loaded++;

					try
					{
						// Start loading mesh data into OpenGL.
						if(!message->gl_meshdata->vbo_handle.valid()) // Mesh data may already be loaded into OpenGL, in that case we don't need to start loading it.
						{
							this->cur_loading_mesh_data              = message->gl_meshdata;
							this->cur_loading_voxel_hash             = message->voxel_hash;
							this->cur_loading_voxel_subsample_factor = message->subsample_factor;
							this->cur_loading_physics_shape          = message->physics_shape;
							this->cur_loading_lod_model_url          = message->lod_model_url;
							this->cur_loading_model_lod_level        = message->model_lod_level;
							this->cur_loading_dynamic_physics_shape  = message->built_dynamic_physics_ob;
							opengl_engine->initialiseMeshDataLoadingProgress(*this->cur_loading_mesh_data, mesh_data_loading_progress);

							//logMessage("Initialised loading of mesh '" + message->lod_model_url + "': " + mesh_data_loading_progress.summaryString());
						}
						else
						{
							//logMessage("Mesh '" + message->lod_model_url + "' was already loaded into OpenGL");
						}
					}
					catch(glare::Exception& e)
					{
						print("Error while loading model: " + e.what());
					}

					//const std::string loading_item = "Initialised load of " + (message->voxel_ob.nonNull() ? "voxels" : message->lod_model_url);
					//loading_times.push_back(doubleToStringNSigFigs(load_item_timer.elapsed() * 1.0e3, 3) + " ms, " + getNiceByteSize(loaded_size_B) + ", " + loading_item);
				}

				if(!process_model_loaded_next && !texture_loaded_messages_to_process.empty())
				{
					//Timer load_item_timer;

					const Reference<TextureLoadedThreadMessage> message = texture_loaded_messages_to_process.front();
					texture_loaded_messages_to_process.pop_front();

					// conPrint("Handling texture loaded message " + message->tex_path + ", use_sRGB: " + toString(message->use_sRGB));
					//num_textures_loaded++;

					this->cur_loading_terrain_map = message->terrain_map;

					try
					{
						TextureLoading::initialiseTextureLoadingProgress(/*message->tex_path, */opengl_engine, message->tex_path, message->tex_params,
							message->texture_data, this->tex_loading_progress);
						tex_loading_progress.URL = message->tex_URL;
					}
					catch(glare::Exception& e)
					{
						conPrint("Error while creating texture '" + std::string(message->tex_path) + "': " + e.what());
						this->tex_loading_progress.tex_data = NULL;
						this->tex_loading_progress.opengl_tex = NULL;
					}

					//conPrint("textureLoaded took                " + timer.elapsedStringNSigFigs(5));
					//size_t tex_size_B = 0;
					//{
					//	Reference<OpenGLTexture> tex = opengl_engine->getTextureIfLoaded(OpenGLTextureKey(message->tex_key), /*use srgb=*/true);
					//	tex_size_B = tex->getByteSize();
					//}
					//loading_times.push_back(doubleToStringNSigFigs(load_item_timer.elapsed() * 1.0e3, 3) + " ms, " + getNiceByteSize(tex_size_B) + ", Texture " + message->tex_key);
				}

				process_model_loaded_next = !process_model_loaded_next;
			}
		}

		//if(num_models_loaded > 0 || num_textures_loaded > 0)
		//	conPrint("Done loading, num_textures_loaded: " + toString(num_textures_loaded) + ", num_models_loaded: " + toString(num_models_loaded) + ", elapsed: " + loading_timer.elapsedStringNPlaces(4));

		//frame_loading_time = loading_timer.elapsed();
		//conPrint("loading_timer: " + loading_timer.elapsedStringMSWIthNSigFigs(4) + ", total_bytes_uploaded: " + getNiceByteSize(total_bytes_uploaded));
		
		this->last_model_and_tex_loading_time = loading_timer.elapsed();
	}

	
	


	

	// Check async geometry uploading queue and async texture uploading queue for any completed uploads
	// We will keep looping until either there are no more completed uploads to process, or we have exceeded a certain amount of time.
	if(vbo_pool && pbo_pool)
	{
		const double MAX_CHECK_FOR_UPLOADED_GEOM_TIME = 0.002;
		Timer timer;
		while(timer.elapsed() < MAX_CHECK_FOR_UPLOADED_GEOM_TIME)
		{
			ZoneScopedN("checking for uploaded geom and textures"); // Tracy profiler

			bool at_least_one_geom_or_tex_uploaded = false;

			//------------------------------------------- Check any current geometry uploads to see if they have completed ------------------------------------------- 
			//Timer timer2;
			async_geom_loader.checkForUploadedGeometry(opengl_engine.ptr(), opengl_engine->getCurrentScene()->frame_num, /*loaded_geom_out=*/temp_uploaded_geom_infos);
			//const double elapsed = timer2.elapsed();
			//if(elapsed > 0.0001)
			//	conPrint("-----------checkForUploadedGeometry() took " + doubleToStringNSigFigs(elapsed * 1.0e3, 4) + " ms------------------");
			for(size_t i=0; i<temp_uploaded_geom_infos.size(); ++i) // Process any completed uploaded geometry
			{
				vbo_pool->vboBecameUnused(temp_uploaded_geom_infos[i].vert_vbo); // Return VBO to pool of unused VBOs
				if(temp_uploaded_geom_infos[i].index_vbo)
					index_vbo_pool->vboBecameUnused(temp_uploaded_geom_infos[i].index_vbo); // Return VBO to pool of unused VBOs


				Reference<AsyncGeometryUploading> loading_info_ref = temp_uploaded_geom_infos[i].user_info.downcast<AsyncGeometryUploading>(); // Get our info about this upload
				AsyncGeometryUploading& loading_info = *loading_info_ref;

				try
				{
					// Process the finished upload (assign mesh to objects etc.)
					handleUploadedMeshData(loading_info.lod_model_url, loading_info.ob_model_lod_level, loading_info.dynamic_physics_shape, temp_uploaded_geom_infos[i].meshdata, loading_info.physics_shape,
						loading_info.voxel_subsample_factor, loading_info.voxel_hash);
				}
				catch(glare::Exception& e)
				{
					logMessage("Error while handling uploaded mesh data: " + e.what());
				}

				at_least_one_geom_or_tex_uploaded = true;
			}
			temp_uploaded_geom_infos.clear();


			//------------------------------------------- Check any current texture uploads to see if they have completed ------------------------------------------- 
			//Timer timer;
			pbo_async_tex_loader.checkForUploadedTexture(opengl_engine->getCurrentScene()->frame_num, temp_loaded_texture_infos);
			//conPrint("checkForUploadedTexture took " + timer.elapsedStringMSWIthNSigFigs());
			for(size_t i=0; i<temp_loaded_texture_infos.size(); ++i)
			{
				pbo_pool->pboBecameUnused(temp_loaded_texture_infos[i].pbo); // Return PBO to pool of unused PBOs

				Reference<PBOAsyncTextureUploading> loading_info_ref = temp_loaded_texture_infos[i].user_info.downcast<PBOAsyncTextureUploading>(); // Get our info about this upload
				PBOAsyncTextureUploading& loading_info = *loading_info_ref;
			
				// Now that we have loaded all the texture data into OpenGL, if we didn't compute all mipmap level data ourselves, and we need it for trilinear filtering, then get the driver to do it.
				//if(loading_info.need_mipmap_build) // (texture_data->W > 1 || texture_data->H > 1) && texture_data->numMipLevels() == 1 && loading_info.opengl_tex->getFiltering() == OpenGLTexture::Filtering_Fancy)
				if((loading_info.tex_data->W > 1 || loading_info.tex_data->H > 1) && loading_info.tex_data->numMipLevels() == 1 && loading_info.opengl_tex->getFiltering() == OpenGLTexture::Filtering_Fancy)
				{
					conPrint("INFO: Getting driver to build MipMaps for texture '" + std::string(loading_info.path) + "'...");
					loading_info.opengl_tex->buildMipMaps();
				}

				if(loading_info.loading_into_existing_opengl_tex)
				{
					// Nothing to do here, texture should already be added to the opengl_engine and applied to any objects using it.
				}
				else
				{
					// If this is texture data for an animated texture (Gif), then keep it around.
					// We need to keep around animated texture data like this, for now, since during animation different frames will be loaded into the OpenGL texture from the tex data.
					// Other texture data can be discarded now it has been uploaded to the GPU/OpenGL.
					if(loading_info.tex_data->isMultiFrame())
					{
						assert(loading_info.tex_data);
						loading_info.opengl_tex->texture_data = loading_info.tex_data;
					}
			
					try
					{
						opengl_engine->addOpenGLTexture(loading_info.opengl_tex->key, loading_info.opengl_tex);

						// Process the finished upload
						handleUploadedTexture(loading_info.path, loading_info.URL, loading_info.opengl_tex, loading_info.tex_data, loading_info.terrain_map);
					}
					catch(glare::Exception& e)
					{
						logMessage("Error while handling uploaded texture: " + e.what());
					}

					// Now that this texture is loaded, remove from textures_processing set.
					// If the texture is unloaded, then this will allow it to be reprocessed and reloaded.
					//assert(textures_processing.count(loading_info.path) >= 1);
					textures_processing.erase(loading_info.path);
				}

				at_least_one_geom_or_tex_uploaded = true;
			}
			temp_loaded_texture_infos.clear();

			if(!at_least_one_geom_or_tex_uploaded)
				break;
		}

		//------------------------------------------- Start uploading any geometry that is ready to upload ------------------------------------------- 
		// Read from async_model_loaded_messages_to_process queue which contains ModelLoadedThreadMessages with geometry data ready to upload
		const double MAX_START_UPLOADING_TIME = 0.001;
		Timer uploading_timer;

		// We will remove items from the front of the queue.  If we can't start uploading the item currently (no free usable VBO), the item will be appended to the end of the list to try again later.
		// So we have to be careful not to loop infinitely.  Ensure this by doing at most async_model_loaded_messages_to_process.size() iterations.

		const size_t max_num_model_loaded_msgs_to_process = async_model_loaded_messages_to_process.size();
		for(size_t i=0; (i < max_num_model_loaded_msgs_to_process) && (uploading_timer.elapsed() < MAX_START_UPLOADING_TIME); ++i)
		{
			ZoneScopedN("Process async_model_loaded_messages_to_process"); // Tracy profiler

			Reference<ModelLoadedThreadMessage> message = async_model_loaded_messages_to_process.front();
			async_model_loaded_messages_to_process.pop_front();

			if(!dummy_vert_vbo)
				dummy_vert_vbo = new VBO(nullptr, 1024, GL_ARRAY_BUFFER);
			if(!dummy_index_vbo)
				dummy_index_vbo = new VBO(nullptr, 1024, GL_ELEMENT_ARRAY_BUFFER);

			bool uploading = false;

			// We want to get a free VBO, memcpy our geometry data to it, and then start uploading it to the GPU.
			// Use separate buffers for vert and index data for async uploads, in the non-mem-mapped case, as required by WebGL.

			VBORef vert_vbo  = vbo_pool      ->getUnusedVBO(message->vert_data_size_B);
			VBORef index_vbo = index_vbo_pool->getUnusedVBO(message->index_data_size_B);
			if(vert_vbo && index_vbo)
			{
				ArrayRef<uint8> vert_data, index_data;
				message->gl_meshdata->getVertAndIndexArrayRefs(vert_data, index_data);

				// Copy vertex data
				vert_vbo->updateData(/*offset=*/0, vert_data.data(), vert_data.size());
				vert_vbo->unbind();

				// Copy index data
				index_vbo->updateData(/*offset=*/0, index_data.data(), index_data.size());
				index_vbo->unbind();

				// Free geometry memory in another thread to avoid blocking while memory is zeroed.
				sendGeometryDataToGarbageDeleterThread(message->gl_meshdata);


				Reference<AsyncGeometryUploading> uploading_info = new AsyncGeometryUploading();
				uploading_info->lod_model_url = message->lod_model_url;
				uploading_info->ob_model_lod_level = message->model_lod_level;
				uploading_info->dynamic_physics_shape = message->built_dynamic_physics_ob;
				uploading_info->physics_shape = message->physics_shape;
				uploading_info->voxel_subsample_factor = message->subsample_factor;
				uploading_info->voxel_hash = message->voxel_hash;

				// Start asynchronous load from VBO
				async_geom_loader.startUploadingGeometry(message->gl_meshdata, /*source VBO=*/vert_vbo, index_vbo, dummy_vert_vbo, dummy_index_vbo, 
					/*vert_data_src_offset_B=*/0, /*index_data_src_offset_B=*/0, message->vert_data_size_B, message->index_data_size_B, message->total_geom_size_B, 
					opengl_engine->getCurrentScene()->frame_num, uploading_info);

				uploading = true;
			}
			//else
			//	conPrint("Failed to get free vert and index VBOs for " + toString(message->total_geom_size_B) + " B");

			if(!uploading)
				async_model_loaded_messages_to_process.push_back(message); // If we failed to upload this geometry, add to back of queue to try again later
		}


		//------------------------------------------- Start uploading any textures that are ready to upload ------------------------------------------- 
		// Read from async_texture_loaded_messages_to_process which contains TextureLoadedThreadMessage with texture data ready to upload
		if(!dummy_opengl_tex)
			dummy_opengl_tex = new OpenGLTexture(16, 16, opengl_engine.ptr(), ArrayRef<uint8>(), OpenGLTextureFormat::Format_RGBA_Linear_Uint8, OpenGLTexture::Filtering::Filtering_Nearest);
	
		if(!dummy_pbo)
			dummy_pbo = new PBO(1024);

		// We will remove items from the front of the queue.  If we can't start uploading the item currently (no free usable PBO), the item will be appended to the end of the list to try again later.
		// So we have to be careful not to loop infinitely.  Ensure this by doing at most async_texture_loaded_messages_to_process.size() iterations.

		const size_t max_num_texture_loaded_msgs_to_process = async_texture_loaded_messages_to_process.size();

		for(size_t i=0; (i < max_num_texture_loaded_msgs_to_process) && (uploading_timer.elapsed() < MAX_START_UPLOADING_TIME); ++i)
		{
			ZoneScopedN("Process async_texture_loaded_messages_to_process"); // Tracy profiler

			Reference<TextureLoadedThreadMessage> message = async_texture_loaded_messages_to_process.front();
			async_texture_loaded_messages_to_process.pop_front();

			// conPrint("Handling TextureLoadedThreadMessage from async_texture_loaded_messages_to_process");

			try
			{
				// Work out texture to upload to.  If uploading to an existing texture, use it.  If uploading to a new texture, create it.
				Reference<OpenGLTexture> opengl_tex;
				if(message->existing_opengl_tex)
					opengl_tex = message->existing_opengl_tex;
				else
				{
					//Timer timer2;
					opengl_tex = TextureLoading::createUninitialisedOpenGLTexture(*message->texture_data, opengl_engine, message->tex_params);
					//const double elapsed = timer2.elapsed();
					//if(elapsed > 0.0001)
					//		conPrint("    createUninitialisedOpenGLTexture() took " + doubleToStringNSigFigs(elapsed * 1.0e3, 4) + " ms");
					opengl_tex->key = message->/*tex_key*/tex_path;
				}


				bool uploading = false;
				// Get a free PBO, memcpy our texture data to it, and then start uploading it to the GPU.

				Reference<TextureData> texture_data = message->texture_data;
				
				ArrayRef<uint8> source_data = texture_data->getDataArrayRef();

				if(texture_data->isMultiFrame())
				{
					// Just upload a single frame
					runtimeCheck(texture_data->frame_size_B * message->load_into_frame_i + texture_data->frame_size_B <= source_data.size());
					source_data = source_data.getSlice(/*offset=*/texture_data->frame_size_B * message->load_into_frame_i, /*slice len=*/texture_data->frame_size_B);
				}

				runtimeCheck(source_data.data());
				if(source_data.data())
				{
					PBORef pbo = pbo_pool->getUnusedVBO(source_data.size());
					if(pbo)
					{
						//conPrint("------- Uploading texture of " + uInt64ToStringCommaSeparated(source_data.size()) + " B using PBO " + toHexString((uint64)pbo.ptr()) + "-------");
						//Timer timer2;
						pbo->updateData(/*offset=*/0, source_data.data(), source_data.size());
						//const double elapsed = timer2.elapsed();
						//if(elapsed > 0.0001)
						//	conPrint("pbo->updateData() for texture of " + uInt64ToStringCommaSeparated(source_data.size()) + " B took " + doubleToStringNSigFigs(elapsed * 1.0e3, 4) + " ms (" + doubleToStringNSigFigs(source_data.size() / elapsed * 1.0e-9, 4) + " GB/s)");
							

						// Free image texture memory now it has been copied to the PBO.
						if(!texture_data->isMultiFrame())
						{
							texture_data->mipmap_data.clearAndFreeMem();
							if(texture_data->converted_image)
								texture_data->converted_image = nullptr;
						}

						Reference<PBOAsyncTextureUploading> uploading_info = new PBOAsyncTextureUploading();
						uploading_info->path = message->tex_path;
						uploading_info->URL = message->tex_URL;
						uploading_info->tex_data = message->texture_data;
						uploading_info->opengl_tex = opengl_tex;
						uploading_info->terrain_map = message->terrain_map;
						uploading_info->loading_into_existing_opengl_tex = message->existing_opengl_tex.nonNull();
						//uploading_info->ob_uid = message->ob_uid;

						//timer2.reset();
						// Start asynchronous load from PBO
						pbo_async_tex_loader.startUploadingTexture(pbo, message->texture_data, opengl_tex, dummy_opengl_tex, dummy_pbo, opengl_engine->getCurrentScene()->frame_num, uploading_info);
						//conPrint("    startUploadingTexture() took  " + timer2.elapsedStringMSWIthNSigFigs());

						uploading = true;
					}
					else
						conPrint("LoadTextureTask: Failed to get free PBO for " + uInt32ToStringCommaSeparated((uint32)source_data.size()) + " B");
				}
			
				if(!uploading)
					async_texture_loaded_messages_to_process.push_back(message); // If we failed to upload this texture, add to back of queue to try again later
			}
			catch(glare::Exception& e)
			{
				conPrint("Excep while creating or starting to upload texture: " + e.what());
			}
		}
	}
}


void GUIClient::sendGeometryDataToGarbageDeleterThread(const Reference<OpenGLMeshRenderData>& gl_meshdata)
{
	if(gl_meshdata->batched_mesh)
	{
		Reference<DeleteGarbageMessage> msg = new DeleteGarbageMessage();
		msg->garbage.uint8_data. takeFrom(gl_meshdata->batched_mesh->vertex_data);
		msg->garbage.uint8_data2.takeFrom(gl_meshdata->batched_mesh->index_data);

		assert(gl_meshdata->batched_mesh->vertex_data.empty());
		assert(gl_meshdata->batched_mesh->index_data .empty());

		this->garbage_deleter_thread_manager.enqueueMessage(msg);
	}
	else
	{
		Reference<DeleteGarbageMessage> msg = new DeleteGarbageMessage();
		msg->garbage.uint8_data.takeFrom(gl_meshdata->vert_data);
		msg->garbage.uint8_data2.takeFrom(gl_meshdata->vert_index_buffer_uint8);
		msg->garbage.uint16_data.takeFrom(gl_meshdata->vert_index_buffer_uint16);
		msg->garbage.uint32_data.takeFrom(gl_meshdata->vert_index_buffer);

		assert(gl_meshdata->vert_data.empty());
		assert(gl_meshdata->vert_index_buffer_uint8.empty());
		assert(gl_meshdata->vert_index_buffer_uint16.empty());
		assert(gl_meshdata->vert_index_buffer.empty());

		this->garbage_deleter_thread_manager.enqueueMessage(msg);
	}
}


void GUIClient::sendWinterShaderEvaluatorToGarbageDeleterThread(const Reference<WinterShaderEvaluator>& script_evaluator)
{
	Reference<DeleteGarbageMessage> msg = new DeleteGarbageMessage();

	msg->garbage.winter_shader_evaluator = script_evaluator;

	this->garbage_deleter_thread_manager.enqueueMessage(msg);
}


// For visualising physics ownership
void GUIClient::updateDiagnosticAABBForObject(WorldObject* ob)
{
	if(ob->opengl_engine_ob.nonNull())
	{
		if(!isObjectPhysicsOwned(*ob, world_state->getCurrentGlobalTime())) //   ob->physics_owner_id == std::numeric_limits<uint32>::max()) // If object is unowned:
		{
			// Remove any existing visualisation AABB.
			if(ob->diagnostics_gl_ob.nonNull())
			{
				opengl_engine->removeObject(ob->diagnostics_gl_ob);
				ob->diagnostics_gl_ob = NULL;
			}
			if(ob->diagnostics_unsmoothed_gl_ob.nonNull())
			{
				opengl_engine->removeObject(ob->diagnostics_unsmoothed_gl_ob);
				ob->diagnostics_unsmoothed_gl_ob = NULL;
			}

			if(ob->diagnostic_text_view.nonNull())
			{
				this->gl_ui->removeWidget(ob->diagnostic_text_view);
				ob->diagnostic_text_view = NULL;
			}
		}
		else
		{
			const Vec4f aabb_min_ws = ob->opengl_engine_ob->aabb_ws.min_;
			const Vec4f aabb_max_ws = ob->opengl_engine_ob->aabb_ws.max_;

			const uint64 hashval = XXH64(&ob->physics_owner_id, sizeof(ob->physics_owner_id), 1);
			const Colour4f col((hashval % 3) / 3.0f, (hashval % 5) / 5.0f, (hashval % 7) / 7.0f, 0.5f);

			// Show bounding box with smoothed transformation (OpenGL ob to-world matrix is smoothed)
			{
				const Matrix4f to_world = ob->opengl_engine_ob->ob_to_world_matrix  * OpenGLEngine::AABBObjectTransform(ob->opengl_engine_ob->mesh_data->aabb_os.min_, ob->opengl_engine_ob->mesh_data->aabb_os.max_);

				if(ob->diagnostics_gl_ob.isNull())
				{
					ob->diagnostics_gl_ob = opengl_engine->makeAABBObject(Vec4f(0,0,0,1), Vec4f(1,1,1,1), col);
					opengl_engine->addObject(ob->diagnostics_gl_ob);
				}
			
				ob->diagnostics_gl_ob->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3(col[0], col[1], col[2]));
				opengl_engine->objectMaterialsUpdated(*ob->diagnostics_gl_ob);
				ob->diagnostics_gl_ob->ob_to_world_matrix = to_world;
				opengl_engine->updateObjectTransformData(*ob->diagnostics_gl_ob);
			}

			// Show bounding box with unsmoothed transformation
			{
				const Matrix4f to_world = (ob->physics_object.nonNull() ? ob->physics_object->getObToWorldMatrix() : Matrix4f::identity()) * OpenGLEngine::AABBObjectTransform(ob->opengl_engine_ob->mesh_data->aabb_os.min_, ob->opengl_engine_ob->mesh_data->aabb_os.max_);

				if(ob->diagnostics_unsmoothed_gl_ob.isNull())
				{
					ob->diagnostics_unsmoothed_gl_ob = opengl_engine->makeAABBObject(Vec4f(0,0,0,1), Vec4f(1,1,1,1), col);
					opengl_engine->addObject(ob->diagnostics_unsmoothed_gl_ob);
				}

				ob->diagnostics_unsmoothed_gl_ob->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3(col[0] + 0.5f, col[1], col[2]));
				opengl_engine->objectMaterialsUpdated(*ob->diagnostics_gl_ob);
				ob->diagnostics_unsmoothed_gl_ob->ob_to_world_matrix = to_world;
				opengl_engine->updateObjectTransformData(*ob->diagnostics_unsmoothed_gl_ob);
			}


			const std::string diag_text = "physics_owner_id: " + toString(ob->physics_owner_id) + " since " + doubleToStringNSigFigs(world_state->getCurrentGlobalTime() - ob->last_physics_ownership_change_global_time, 2) + " s";
			if(ob->diagnostic_text_view.isNull())
			{
				GLUITextView::CreateArgs create_args;
				ob->diagnostic_text_view = new GLUITextView(*this->gl_ui, this->opengl_engine, diag_text, Vec2f(0.f, 0.f), create_args);
			}
			else
			{
				ob->diagnostic_text_view->setText(*this->gl_ui, diag_text);

				Vec2f normed_coords;
				const bool visible = getGLUICoordsForPoint((aabb_min_ws + aabb_max_ws) * 0.5f, normed_coords);
				if(visible)
				{
					Vec2f botleft(normed_coords.x, normed_coords.y);

					ob->diagnostic_text_view->setPos(*gl_ui, botleft);
				}
			}
		}
	}
}


void GUIClient::updateObjectsWithDiagnosticVis()
{
	for(auto it = obs_with_diagnostic_vis.begin(); it != obs_with_diagnostic_vis.end(); )
	{
		WorldObjectRef ob = *it;
		updateDiagnosticAABBForObject(ob.ptr());

		const bool remove = ob->diagnostics_gl_ob.isNull();
		if(remove)
			it = obs_with_diagnostic_vis.erase(it);
		else
			++it;
	}
}


void GUIClient::processPlayerPhysicsInput(float dt, bool world_render_has_keyboard_focus, PlayerPhysicsInput& input_out)
{
	bool move_key_pressed = false;

	input_out.SHIFT_down =	false;
	input_out.CTRL_down =	false;
	input_out.W_down =		false;
	input_out.S_down =		false;
	input_out.A_down =		false;
	input_out.D_down =		false;
	input_out.space_down =	false;
	input_out.C_down =		false;
	input_out.left_down =	false;
	input_out.right_down =	false;
	input_out.up_down =		false;
	input_out.down_down =	false;
	input_out.B_down =		false;


	// On Windows we will use GetAsyncKeyState() to test if a key is down.
	// On Mac OS / Linux we will use our W_down etc.. state.
	// This isn't as good because if we miss the keyReleaseEvent due to not having focus when the key is released, the key will act as if it's stuck down.
	// TODO: Find an equivalent solution to GetAsyncKeyState on Mac/Linux.
	if(ui_interface->hasFocus() && ui_interface->isKeyboardCameraMoveEnabled()/*cam_move_on_key_input_enabled*/)
	{
		if(world_render_has_keyboard_focus)
		{
#ifdef _WIN32
			SHIFT_down =	GetAsyncKeyState(VK_SHIFT);
			CTRL_down	=	GetAsyncKeyState(VK_CONTROL);
			W_down =		GetAsyncKeyState('W');
			S_down =		GetAsyncKeyState('S');
			A_down =		GetAsyncKeyState('A');
			D_down =		GetAsyncKeyState('D');
			space_down =	GetAsyncKeyState(' ');
			C_down =		GetAsyncKeyState('C');
			left_down =		GetAsyncKeyState(VK_LEFT);
			right_down =	GetAsyncKeyState(VK_RIGHT);
			up_down =		GetAsyncKeyState(VK_UP);
			down_down =		GetAsyncKeyState(VK_DOWN);
			B_down = 		GetAsyncKeyState('B');
#else
			// CTRL_down = QApplication::keyboardModifiers().testFlag(Qt::ControlModifier);
#endif
		}
		else
		{
			SHIFT_down = false;
			CTRL_down = false;
			A_down = false;
			W_down = false;
			S_down = false;
			D_down = false;
			space_down = false;
			C_down = false; 
			left_down = false;
			right_down = false;
			up_down = false;
			down_down = false;
			B_down = false;
		}

		const float selfie_move_factor = cam_controller.selfieModeEnabled() ? -1.f : 1.f;

		if(W_down || up_down)
		{	player_physics.processMoveForwards(1.f * selfie_move_factor, SHIFT_down, this->cam_controller); move_key_pressed = true; }
		if(S_down || down_down)
		{	player_physics.processMoveForwards(-1.f * selfie_move_factor, SHIFT_down, this->cam_controller); move_key_pressed = true; }
		if(A_down)
		{	player_physics.processStrafeRight(-1.f * selfie_move_factor, SHIFT_down, this->cam_controller); move_key_pressed = true; }
		if(D_down)
		{	player_physics.processStrafeRight(1.f * selfie_move_factor, SHIFT_down, this->cam_controller); move_key_pressed = true; }

		// Move vertically up or down in flymode.
		if(space_down)
		{	player_physics.processMoveUp(1.f, SHIFT_down, this->cam_controller); move_key_pressed = true; }
		if(C_down && !CTRL_down) // Check CTRL_down to prevent CTRL+C shortcut moving camera up.
		{	player_physics.processMoveUp(-1.f, SHIFT_down, this->cam_controller); move_key_pressed = true; }

		// Turn left or right
		const float base_rotate_speed = 200;
		if(left_down)
		{	this->cam_controller.updateRotation(/*pitch_delta=*/0, /*heading_delta=*/dt * -base_rotate_speed * (SHIFT_down ? 3.0 : 1.0)); }
		if(right_down)
		{	this->cam_controller.updateRotation(/*pitch_delta=*/0, /*heading_delta=*/dt *  base_rotate_speed * (SHIFT_down ? 3.0 : 1.0)); }

		if(misc_info_ui.movement_button && misc_info_ui.movement_button->pressed)
		{
			const Vec2f frac_coords = div((gl_ui->getLastMouseUICoords() - misc_info_ui.movement_button->rect.getMin()), misc_info_ui.movement_button->rect.getWidths());

			player_physics.processStrafeRight (selfie_move_factor * 4 * (frac_coords.x - 0.5f), SHIFT_down, this->cam_controller);
			player_physics.processMoveForwards(selfie_move_factor * 4 * (frac_coords.y - 0.5f), SHIFT_down, this->cam_controller);
			
			move_key_pressed = true;
		}


		input_out.SHIFT_down =	SHIFT_down;
		input_out.CTRL_down =	CTRL_down;
		input_out.W_down =		W_down;
		input_out.S_down =		S_down;
		input_out.A_down =		A_down;
		input_out.D_down =		D_down;
		input_out.space_down =	space_down;
		input_out.C_down =		C_down;
		input_out.left_down =	left_down;
		input_out.right_down =	right_down;
		input_out.up_down =		up_down;
		input_out.down_down =	down_down;
		input_out.B_down	=	B_down;
	}

	if(ui_interface->gamepadAttached())
	{
		// Since we don't have the shift key available, move a bit faster in flymode
		const float gamepad_move_speed_factor = player_physics.flyModeEnabled() ? 4.f : 2.f;
		const float gamepad_rotate_speed = 400;

		// Move vertically up or down in flymode.
		if(ui_interface->gamepadButtonL2() != 0) // Left trigger
		{	
			player_physics.processMoveUp(gamepad_move_speed_factor * -pow(ui_interface->gamepadButtonL2(), 3.f), SHIFT_down, this->cam_controller); move_key_pressed = true; last_cursor_movement_was_from_mouse = false;
		}
		input_out.left_trigger = ui_interface->gamepadButtonL2();

		if(ui_interface->gamepadButtonR2() != 0) // Right trigger
		{	
			player_physics.processMoveUp(gamepad_move_speed_factor * pow(ui_interface->gamepadButtonR2(), 3.f), SHIFT_down, this->cam_controller); move_key_pressed = true; last_cursor_movement_was_from_mouse = false;
		}
		input_out.right_trigger = ui_interface->gamepadButtonR2();

		const float axis_left_x = ui_interface->gamepadAxisLeftX();
		const float axis_left_y = ui_interface->gamepadAxisLeftY();
		if(axis_left_x != 0)
		{	
			player_physics.processStrafeRight(gamepad_move_speed_factor * pow(axis_left_x, 3.f), SHIFT_down, this->cam_controller); move_key_pressed = true; last_cursor_movement_was_from_mouse = false;
		}
		if(axis_left_y != 0)
		{	
			player_physics.processMoveForwards(gamepad_move_speed_factor * -pow(axis_left_y, 3.f), SHIFT_down, this->cam_controller); move_key_pressed = true; last_cursor_movement_was_from_mouse = false;
		}

		input_out.axis_left_x = axis_left_x;
		input_out.axis_left_y = axis_left_y;


		const float axis_right_x = ui_interface->gamepadAxisRightX();
		if(axis_right_x != 0)
		{
			this->cam_controller.updateRotation(/*pitch_delta=*/0, /*heading_delta=*/dt * gamepad_rotate_speed * pow(axis_right_x, 3.0f));
			
			if(std::fabs(axis_right_x) > 0.5f) // If definitely a player-initiated command (as opposed to stick drift)
				last_cursor_movement_was_from_mouse = false; // then last cursor movement is from gamepad
		}

		const float axis_right_y = ui_interface->gamepadAxisRightY();
		if(axis_right_y != 0)
		{
			this->cam_controller.updateRotation(/*pitch_delta=*/dt *  gamepad_rotate_speed * -pow(axis_right_y, 3.f), /*heading_delta=*/0);
		
			if(std::fabs(axis_right_y) > 0.5f) // If definitely a player-initiated command (as opposed to stick drift)
				last_cursor_movement_was_from_mouse = false; // then last cursor movement is from gamepad
		}

		hud_ui.setCrosshairDotVisible(!last_cursor_movement_was_from_mouse);
	}

	if(cam_controller.current_cam_mode == CameraController::CameraMode_FreeCamera)
	{
		this->cam_controller.setFreeCamMovementDesiredVel(player_physics.getMoveDesiredVel());
		player_physics.zeroMoveDesiredVel();
		input_out.clear();
	}

	if(move_key_pressed)
	{
		stopGesture();
		gesture_ui.stopAnyGesturePlaying();
	}
}


void GUIClient::timerEvent(const MouseCursorState& mouse_cursor_state)
{
	ZoneScoped; // Tracy profiler
	Timer timer_event_timer;

	if((connection_state == ServerConnectionState_NotConnected) && (retry_connection_timer.elapsed() > 10.0))
	{
		// Try and connect
		connectToServer(last_url_parse_results);

		retry_connection_timer.reset();
	}


	if(world_state)
	{
		WorldStateLock lock(this->world_state->mutex);
		scripted_ob_proximity_checker.think(cam_controller.getPosition().toVec4fPoint(), lock);
	}

	// Do Lua timer callbacks
	if(false) // TEMP 
	{
		WorldStateLock lock(this->world_state->mutex);

		const double cur_time = total_timer.elapsed();
		timer_queue.update(cur_time, /*triggered_timers_out=*/temp_triggered_timers);

		for(size_t i=0; i<temp_triggered_timers.size(); ++i)
		{
			TimerQueueTimer& timer = temp_triggered_timers[i];
			
			LuaScriptEvaluator* script_evaluator = timer.lua_script_evaluator.getPtrIfAlive();
			if(script_evaluator)
			{
				// Check timer is still valid (has not been destroyed by destroyTimer), by checking the timer id with the same index is still equal to our timer id.
				assert(timer.timer_index >= 0 && timer.timer_index <= LuaScriptEvaluator::MAX_NUM_TIMERS);
				if(timer.timer_id == script_evaluator->timers[timer.timer_index].id)
				{
					script_evaluator->doOnTimerEvent(timer.onTimerEvent_ref, lock); // Execute the Lua timer event callback function

					if(timer.repeating)
					{
						// Re-insert timer with updated trigger time
						timer.tigger_time = cur_time + timer.period;
						timer_queue.addTimer(cur_time, timer);
					}
					else // Else if timer was a one-shot timer, 'destroy' it.
					{
						script_evaluator->destroyTimer(timer.timer_index);
					}
				}
			}
		}
	}


	if(gl_ui.nonNull())
		gl_ui->think();

	// If we are connected to a server, send a UDP packet to it occasionally, so the server can work out which UDP port
	// we are listening on.
#if !defined(EMSCRIPTEN)
	if(client_avatar_uid.valid())
	{
		if(discovery_udp_packet_timer.elapsed() > 2.0)
		{
			// conPrint("Sending discovery UDP packet to server");
			{
				scratch_packet.clear();
				scratch_packet.writeUInt32(2); // Packet type
				writeToStream(this->client_avatar_uid, scratch_packet);

				try
				{
					udp_socket->sendPacket(scratch_packet.buf.data(), scratch_packet.buf.size(), server_ip_addr, server_UDP_port);
				}
				catch(glare::Exception& e)
				{
					conPrint("Error while sending discovery UDP packet: " + e.what());
				}
			}
			discovery_udp_packet_timer.reset();
		}
	}
#endif


#if defined(EMSCRIPTEN)
	emscripten_resource_downloader.think();
#endif


	mesh_manager.trimMeshMemoryUsage();


	{
		Lock lock(particles_creation_buf_mutex);
		
		for(size_t i=0; i<this->particles_creation_buf.size(); ++i)
			this->particle_manager->addParticle(this->particles_creation_buf[i]);
		
		this->particles_creation_buf.clear();
	}



	processLoading(timer_event_timer);

	
	
	/*
	Flow of loading models, textures etc.

	|
	|	load_item_queue.enqueueItem()
	|
	v
	load_item_queue
	|
	|	code below to add items to model_and_texture_loader_task_manager
	|
	v
	model_and_texture_loader_task_manager
	|
	|	TaskManager queue code
	|
	v
	LoadTextureTask					LoadModelTask				etc..
	|
	|	Code in LoadTextureTask etc.. adds to main_window->msg_queue
	|
	v
	main_window->msg_queue
	|
	|	Code further below in timerEvent() reads messages from msg_queue, appends to texture_loaded_messages_to_process etc.
	|
	v
	model_loaded_messages_to_process, texture_loaded_messages_to_process
	|
	|	Code to load OpenGL data to device mem
	|
	v

	*/

	TracyPlot("load_item_queue.size()", (int64)load_item_queue.size());
	TracyPlot("model_loaded_messages_to_process.size()", (int64)model_loaded_messages_to_process.size());
	TracyPlot("texture_loaded_messages_to_process.size()", (int64)texture_loaded_messages_to_process.size());

	
	while(!load_item_queue.empty() &&  // While there are items to remove from the load item queue,
		(model_and_texture_loader_task_manager.getNumUnfinishedTasks() < 32) &&  // and we don't have too many tasks queued and ready to be executed by the task manager
		(msg_queue.size() + model_loaded_messages_to_process.size() + texture_loaded_messages_to_process.size() < 32) // And we don't have too many completed load tasks:
		)
	{
		// Pop a task from the load item queue, and pass it to the model_and_texture_loader_task_manager.
		LoadItemQueueItem item;
		load_item_queue.dequeueFront(/*item out=*/item); 

		// Discard task if it is now too far from the camera.  Do this so we don't load e.g. high detail models when we
		// are no longer close to them.
		const float dist_from_item = item.getDistanceToCamera(cam_controller.getPosition().toVec4fPoint());
		if(dist_from_item > item.task_max_dist)
		{
			if(dynamic_cast<const LoadTextureTask*>(item.task.ptr()))
			{
				const LoadTextureTask* task = static_cast<const LoadTextureTask*>(item.task.ptr());
				assert(textures_processing.count(task->path) > 0);
				textures_processing.erase(task->path);

				//conPrint("Discarding texture load task '" + task->path + "' as too far away. (dist_from_item: " + doubleToStringNSigFigs(dist_from_item, 4) + ", task max dist: " + doubleToStringNSigFigs(item.task_max_dist, 3) + ")");
			}
			else if(dynamic_cast<const MakeHypercardTextureTask*>(item.task.ptr()))
			{
				const MakeHypercardTextureTask* task = static_cast<const MakeHypercardTextureTask*>(item.task.ptr());
				assert(textures_processing.count(task->tex_key) > 0);
				textures_processing.erase(task->tex_key);

				//conPrint("Discarding MakeHypercardTextureTask '" + task->tex_key + "' as too far away. (dist_from_item: " + doubleToStringNSigFigs(dist_from_item, 4) + ", task max dist: " + doubleToStringNSigFigs(item.task_max_dist, 3) + ")");
			}
			else if(dynamic_cast<const LoadModelTask*>(item.task.ptr()))
			{
				const LoadModelTask* task = static_cast<const LoadModelTask*>(item.task.ptr());
				if(!task->lod_model_url.empty()) // Will be empty for voxel models
				{
					ModelProcessingKey key(task->lod_model_url, task->build_dynamic_physics_ob);
					//assert(models_processing.count(key) > 0);
					models_processing.erase(key);
				}
				
				//conPrint("Discarding model load task '" + task->lod_model_url + "' as too far away. (dist_from_item: " + doubleToStringNSigFigs(dist_from_item, 4) + ", task max dist: " + doubleToStringNSigFigs(item.task_max_dist, 3) + ")");
			}
			else if(dynamic_cast<const LoadScriptTask*>(item.task.ptr()))
			{
				const LoadScriptTask* task = static_cast<const LoadScriptTask*>(item.task.ptr());
				assert(script_content_processing.count(task->script_content) > 0);
				script_content_processing.erase(task->script_content);
				
				//conPrint("Discarding LoadScriptTask as too far away. (dist_from_item: " + doubleToStringNSigFigs(dist_from_item, 4) + ", task max dist: " + doubleToStringNSigFigs(item.task_max_dist, 3) + ")");
			}
			else if(dynamic_cast<const LoadAudioTask*>(item.task.ptr()))
			{
				const LoadAudioTask* task = static_cast<const LoadAudioTask*>(item.task.ptr());
				assert(audio_processing.count(task->audio_source_url) > 0);
				audio_processing.erase(task->audio_source_url);
				
				//conPrint("Discarding LoadAudioTask '" + task->audio_source_url + "' as too far away. (dist_from_item: " + doubleToStringNSigFigs(dist_from_item, 4) + ", task max dist: " + doubleToStringNSigFigs(item.task_max_dist, 3) + ")");
			}
		}
		else
		{
			model_and_texture_loader_task_manager.addTask(item.task);
		}
	}


	// Sort load_item_queue every now and then
	if(load_item_queue_sort_timer.elapsed() > 0.1)
	{
		this->load_item_queue.sortQueue(cam_controller.getPosition());
		load_item_queue_sort_timer.reset();
	}


	// Sort download queue every now and then
	if(download_queue_sort_timer.elapsed() > 2.0)
	{
		this->download_queue.sortQueue(cam_controller.getPosition());
		download_queue_sort_timer.reset();
	}

	if(connection_state == ServerConnectionState_Connected)
		checkForLODChanges(timer_event_timer);


	
	gesture_ui.think();
	hud_ui.think();
	if(minimap)
		minimap->think();

	updateObjectsWithDiagnosticVis();


	// Update info UI with stuff like drawing the URL for objects with target URLs, and showing the 'press [E] to enter vehicle' message.
	// Don't do this if the mouse cursor is hidden, because that implies we are click-dragging to change the camera orientation, and we don't want to show mouse over messages while doing that.
	if(!ui_interface->isCursorHidden())
	{
		//const QPoint widget_pos = ui->glWidget->mapFromGlobal(QCursor::pos());
		const bool use_mouse_cursor_as_cursor = last_cursor_movement_was_from_mouse;
		Vec2i cursor_pos;
		Vec2f cursor_gl_coords;
		if(use_mouse_cursor_as_cursor)
		{
			cursor_pos = mouse_cursor_state.cursor_pos;
			cursor_gl_coords = mouse_cursor_state.gl_coords;
		}
		else // Else use crosshair as cursor:
		{
			const float gl_w = (float)opengl_engine->getMainViewPortWidth();
			const float gl_h = (float)opengl_engine->getMainViewPortHeight();

			cursor_pos = Vec2i((int)(gl_w / 2), (int)(gl_h / 2));
			cursor_gl_coords = Vec2f(0.f);
		}

		updateInfoUIForMousePosition(cursor_pos, cursor_gl_coords, /*mouse_event=*/NULL, /*cursor_is_mouse_cursor=*/use_mouse_cursor_as_cursor);
	}

	// Update AABB visualisation, if we are showing one.
	if(aabb_os_vis_gl_ob.nonNull() && selected_ob.nonNull())
	{
		aabb_os_vis_gl_ob->ob_to_world_matrix = selected_ob->obToWorldMatrix() * OpenGLEngine::AABBObjectTransform(selected_ob->getAABBOS().min_, selected_ob->getAABBOS().max_);
		opengl_engine->updateObjectTransformData(*aabb_os_vis_gl_ob);
	}
	if(aabb_ws_vis_gl_ob.nonNull() && selected_ob.nonNull())
	{
		aabb_ws_vis_gl_ob->ob_to_world_matrix = OpenGLEngine::AABBObjectTransform(selected_ob->getAABBWS().min_, selected_ob->getAABBWS().max_);
		opengl_engine->updateObjectTransformData(*aabb_ws_vis_gl_ob);
	}

	const double dt = time_since_last_timer_ev.elapsed();
	time_since_last_timer_ev.reset();
	const double global_time = world_state.nonNull() ? this->world_state->getCurrentGlobalTime() : 0.0; // Used as input into script functions

	// Set current animation frame for objects with animated textures
	//double animated_tex_time = 0;
	if(world_state.nonNull())
	{
		ZoneScopedN("process animated textures"); // Tracy profiler
		Timer timer;
		//Timer tex_upload_timer;
		//tex_upload_timer.pause();

		int num_gif_textures_processed = 0;
		int num_mp4_textures_processed = 0;
		int num_gif_frames_advanced = 0;

		const double anim_time = total_timer.elapsed();

		{
			Lock lock(this->world_state->mutex); // NOTE: This lock needed?

			for(auto it = this->obs_with_animated_tex.begin(); it != this->obs_with_animated_tex.end(); ++it)
			{
				WorldObject* ob = it->ptr();
				AnimatedTexObData& animation_data = *ob->animated_tex_data;

				try
				{
					const AnimatedTexObDataProcessStats stats = animation_data.process(this, opengl_engine.ptr(), ob, anim_time, dt);
					num_gif_textures_processed += stats.num_gif_textures_processed;
					num_mp4_textures_processed += stats.num_mp4_textures_processed;
					num_gif_frames_advanced    += stats.num_gif_frames_advanced;
				}
				catch(glare::Exception& e)
				{
					logMessage("Excep while processing animation data: " + e.what());
				}
			}
		 } // End lock scope

		this->last_num_gif_textures_processed = num_gif_textures_processed;
		this->last_num_mp4_textures_processed = num_mp4_textures_processed;


		animated_texture_manager->think(this, opengl_engine.ptr(), anim_time, dt);


		// Process web-view objects
		for(auto it = web_view_obs.begin(); it != web_view_obs.end(); ++it)
		{
			WorldObject* ob = it->ptr();

			try
			{
				ob->web_view_data->process(this, opengl_engine.ptr(), ob, anim_time, dt);
			}
			catch(glare::Exception& e)
			{
				logMessage("Excep while processing webview: " + e.what());
			}
		}

		// Process browser vid player objects
		for(auto it = browser_vid_player_obs.begin(); it != browser_vid_player_obs.end(); ++it)
		{
			WorldObject* ob = it->ptr();

			try
			{
				ob->browser_vid_player->process(this, opengl_engine.ptr(), ob, anim_time, dt);
			}
			catch(glare::Exception& e)
			{
				logMessage("Excep while processing browser vid player: " + e.what());
			}
		}

		this->last_animated_tex_time = timer.elapsed();
	}

	// NOTE: goes after screenshot code, which might update campos.
	Vec4f campos = this->cam_controller.getFirstPersonPosition().toVec4fPoint();

	const double cur_time = Clock::getTimeSinceInit(); // Used for animation, interpolation etc..

	//ui->indigoView->timerThink();

	updateNotifications(cur_time);

	updateGroundPlane();


	num_frames_since_fps_timer_reset++;
	if(fps_display_timer.elapsed() > 1.0)
	{
		last_fps = num_frames_since_fps_timer_reset / fps_display_timer.elapsed();
		//conPrint("FPS: " + doubleToStringNSigFigs(fps, 4));
		num_frames_since_fps_timer_reset = 0;
		fps_display_timer.reset();
	}

	handleMessages(global_time, cur_time);
	
	// Evaluate scripts on objects
	{
		ZoneScopedN("script eval"); // Tracy profiler

		Timer timer;
		Scripting::evaluateObjectScripts(this->obs_with_scripts, global_time, dt, world_state.ptr(), opengl_engine.ptr(), this->physics_world.ptr(), &this->audio_engine,
			/*num_scripts_processed_out=*/this->last_num_scripts_processed
		);
		this->last_eval_script_time = timer.elapsed();
	}

	if(opengl_engine)
		opengl_engine->setCurrentTime((float)cur_time);


	UpdateEvents physics_events;

	PlayerPhysicsInput physics_input;

	const bool world_render_has_keyboard_focus = gl_ui ? gl_ui->getKeyboardFocusWidget().isNull() : false;

	{
		ZoneScopedN("processPlayerPhysicsInput"); // Tracy profiler
		processPlayerPhysicsInput((float)dt, world_render_has_keyboard_focus, /*input_out=*/physics_input); // sets player physics move impulse.
	}

	const bool our_move_impulse_zero = !player_physics.isMoveDesiredVelNonZero();

	// Advance physics sim and player physics with a maximum timestep size.
	// We advance both together, otherwise if there is a large dt, the physics engine can advance objects past what the player physics can keep up with.
	// This prevents stuff like the player falling off the back of a train when loading stutters occur.
	const double MAX_SUBSTEP_DT = 1.0 / 60.0;
	const int unclamped_num_substeps = (int)std::ceil(dt / MAX_SUBSTEP_DT); // May get very large.
	const int num_substeps  = myMin(unclamped_num_substeps, 500); // Only do up to N steps
	const double substep_dt = myMin(dt / num_substeps, MAX_SUBSTEP_DT); // Don't make the substep time > 1/60s.

	assert(substep_dt <= MAX_SUBSTEP_DT);
	//printVar(num_substeps);
	//conPrint("substep_dt: " + doubleToStringMaxNDecimalPlaces(substep_dt * 1000.0, 3) + " ms");

	{
		PERFORMANCEAPI_INSTRUMENT("physics sim");
		ZoneScopedN("physics sim"); // Tracy profiler

		for(int i=0; i<num_substeps; ++i)
		{
			if(physics_world && world_state)
			{
				// Do path controllers (which call MoveKinematic()), then player physics, then main physics update.
				// Path controller goes before player physics, so that the player velocity can match the newly computed platform velocity if standing on a kinematic platform.
				// Player physics goes before main physics update because that's what the Jolt example code does.  It also results in less spurious jumping in the air when moving on an upwards moving platform.
				//
				// Path controllers are updated in this substep-loop as well as player physics and the main physics engine.
				// This means if that many substeps need to be run at once, for example if there is a large stutter of e.g. 1 second, then the player won't fall off a moving platform.
				// The disadvantage however is that path-controlled objects can potentially get out of sync with the global time, for exmaple if the num_substeps limit is hit.

				// Run path controllers
				{
					ZoneScopedN("path_controllers eval"); // Tracy profiler
					Lock lock(this->world_state->mutex);
					for(size_t z=0; z<path_controllers.size(); ++z)
						path_controllers[z]->update(*world_state, *physics_world, opengl_engine.ptr(), (float)substep_dt);
				}

				// Process player physics
				if(vehicle_controller_inside.nonNull()) // If we are inside a vehicle:
				{
					if(this->cur_seat_index == 0) // If we are driving it:
						vehicle_controller_inside->update(*this->physics_world, physics_input, (float)substep_dt);

					// Note that the player position and velocity is set later with player_physics.setCapsuleBottomPosition()
					player_physics.updateForInVehicle(*this->physics_world, physics_input, (float)substep_dt, /*vehicle body id=*/vehicle_controller_inside->getBodyID());
				}
				else
				{
					const UpdateEvents substep_physics_events = player_physics.update(*this->physics_world, physics_input, (float)substep_dt, cur_time, /*campos out=*/campos);
					physics_events.jumped = physics_events.jumped || substep_physics_events.jumped;
				}

				physics_world->think(substep_dt); // Advance physics simulation

				// Process contact events for objects that the player touched.
				// Take physics ownership of any such object if needed.
				{
					Lock world_state_lock(this->world_state->mutex);
					for(size_t z=0; z<player_physics.contacted_events.size(); ++z)
					{
						PhysicsObject* physics_ob = player_physics.contacted_events[z].ob;
						if(physics_ob->userdata_type == 0 && physics_ob->userdata != 0) // If userdata type is WorldObject:
						{
							WorldObject* ob = (WorldObject*)physics_ob->userdata;

							// conPrint("Player contacted object " + ob->uid.toString());
						
							if(!isObjectPhysicsOwnedBySelf(*ob, global_time) && !isObjectVehicleBeingDrivenByOther(*ob))
							{
								// conPrint("==Taking ownership of physics object from avatar physics contact...==");
								takePhysicsOwnershipOfObject(*ob, global_time);
							}
						}
					}
				}

				// Execute script events on any player contacted events.
				// These are events generated in PlayerPhysics::update() by contact between the virtual character controller and Jolt bodies.
				{
					WorldStateLock world_state_lock(this->world_state->mutex);
					for(size_t z=0; z<player_physics.contacted_events.size(); ++z)
					{
						PhysicsObject* physics_ob = player_physics.contacted_events[z].ob;
						if(physics_ob->userdata && (physics_ob->userdata_type == 0)) // WorldObject
						{
							WorldObject* ob = (WorldObject*)physics_ob->userdata;
					
							// conPrint("timerEvent: player hit ob UID: " + ob->uid.toString());
					
							// Run script
							if(ob->event_handlers && ob->event_handlers->onUserTouchedObject_handlers.nonEmpty())
							{
								// Jolt creates contact added events very fast, so limit how often we call onUserTouchedObject.
								const double time_since_last_touch_event = cur_time - ob->last_touch_event_time;

								if(time_since_last_touch_event > 0.5)
								{
									ob->last_touch_event_time = cur_time;

									// Execute doOnUserTouchedObject event handler in any scripts that are listening for onUserTouchedObject for this object
									ob->event_handlers->executeOnUserTouchedObjectHandlers(this->client_avatar_uid, ob->uid, world_state_lock);

									// Send message to server to execute onUserTouchedObject on the server
									MessageUtils::initPacket(scratch_packet, Protocol::UserTouchedObjectMessage);
									writeToStream(ob->uid, scratch_packet);
									enqueueMessageToSend(*client_thread, scratch_packet);
								}
							}
						}
					}
				}

				player_physics.contacted_events.resize(0);


				// Process vehicle controllers for any vehicles we are not driving:
				for(auto it = vehicle_controllers.begin(); it != vehicle_controllers.end(); ++it)
				{
					VehiclePhysics* controller = it->second.ptr();
					if((controller != vehicle_controller_inside.ptr()) || (this->cur_seat_index != 0)) // If this is not the controller for the vehicle we are inside of, or if we are not driving it:
					{
						PlayerPhysicsInput controller_physics_input;
						controller_physics_input.setFromBitFlags(controller->last_physics_input_bitflags);
						controller->update(*this->physics_world, controller_physics_input, (float)substep_dt);
					}
				}
			}
		}
	}


	player_physics.zeroMoveDesiredVel();

	// Force player above terrain surface.
	// Useful to prevent player falling down to infinity if they fall below the terrain surface before it is loaded.
	if(terrain_system.nonNull())
	{
		const Vec3d player_pos = player_physics.getCapsuleBottomPosition();

		const float terrain_h = terrain_system->evalTerrainHeight((float)player_pos.x, (float)player_pos.y, 1.0);

		if((float)player_pos.z < (terrain_h - 0.5f))
		{
			// logMessage("Player was below terrain, moving up");
			Vec3d new_player_pos = player_pos;
			new_player_pos.z = terrain_h + player_physics.getEyeHeight() + 0.5f;

			Vec4f new_vel = player_physics.getLinearVel();
			new_vel[2] = 0; // Zero out vertical velocity.

			player_physics.setEyePosition(new_player_pos, /*new linear_vel=*/new_vel);
		}
	}


	// Compute Doppler-effect factor for vehicle controllers, also set wind audio source volume.
	if(physics_world.nonNull())
	{
		const Vec4f listener_linear_vel = vehicle_controller_inside.nonNull() ? vehicle_controller_inside->getLinearVel(*this->physics_world) : player_physics.getLinearVel();

		for(auto it = vehicle_controllers.begin(); it != vehicle_controllers.end(); ++it)
		{
			VehiclePhysics* controller = it->second.ptr();
			controller->updateDopplerEffect(/*listener linear vel=*/listener_linear_vel, /*listener pos=*/cam_controller.getFirstPersonPosition().toVec4fPoint());
		}

		if(wind_audio_source)
		{
			const float old_volume = wind_audio_source->volume;

			// Increase wind volume as speed increases, but only once we exceed a certain speed, since we don't really want wind sounds playing when we run + jump around (approx < 20 m/s).
			const float WIND_VOLUME_FACTOR = 0.7f;
			wind_audio_source->volume = myClamp((listener_linear_vel.length() - 20.f) * 0.015f * WIND_VOLUME_FACTOR, 0.f, WIND_VOLUME_FACTOR);

			if(wind_audio_source->volume != old_volume)
				audio_engine.sourceVolumeUpdated(*wind_audio_source);
		}
	}


	updateSelectedObjectPlacementBeamAndGizmos();

	
	if(physics_world.nonNull())
	{
		Lock world_state_lock(this->world_state->mutex);

		// Update transforms in OpenGL of objects the physics engine has moved.
		JPH::BodyInterface& body_interface = physics_world->physics_system->GetBodyInterface();

		{
			Lock lock(physics_world->activated_obs_mutex);
			for(auto it = physics_world->activated_obs.begin(); it != physics_world->activated_obs.end(); ++it)
			{
				PhysicsObject* physics_ob = *it;

				JPH::Vec3 pos;
				JPH::Quat rot;
				body_interface.GetPositionAndRotation(physics_ob->jolt_body_id, pos, rot);

				//conPrint("Setting active object " + toString(ob->jolt_body_id.GetIndex()) + " state from jolt: " + toString(pos.GetX()) + ", " + toString(pos.GetY()) + ", " + toString(pos.GetZ()));

				const Vec4f new_pos = toVec4fPos(pos);
				const Quatf new_rot = toQuat(rot);

				physics_ob->rot = new_rot;
				physics_ob->pos = new_pos;

				if(physics_ob->userdata_type == 0 && physics_ob->userdata != 0) // If userdata type is WorldObject:
				{
#ifndef NDEBUG
					if(world_state->objects.find(physics_ob->ob_uid) == world_state->objects.end())
					{
						conPrint("Error: UID " + physics_ob->ob_uid.toString() + " not found for physics ob");
						assert(0);
					}
#endif
					WorldObject* ob = (WorldObject*)physics_ob->userdata;
					assert(ob->physics_object == physics_ob);

					// Scripted objects have their opengl transform set directly in evalObjectScript(), so we don't need to set it from the physics object.
					// We will set the opengl transform in Scripting::evalObjectScript() as it should be slightly more efficient (due to computing ob_to_world_inv_transpose directly).
					// There is also code in Scripting::evalObjectScript that computes a custom world space AABB that doesn't oscillate in size with animations.
					// For path-controlled objects, however, we will set the OpenGL transform from the physics engine.
					if(physics_ob->dynamic || (physics_ob->kinematic && ob->is_path_controlled))
					{
						// conPrint("Setting object state for ob " + ob->uid.toString() + " from jolt");

						const bool ob_picked_up = (this->selected_ob.ptr() == ob) && this->selected_ob_picked_up;

						if(!ob_picked_up || getPathControllerForOb(*ob)) // Don't update selected object with physics engine state, unless it is path controlled.
						{
							// Set object world state.  We want to do this for dynamic objects, so that if they are reloaded on LOD changes, the position is correct.
							// We will also reduce smooth_translation and smooth_rotation over time here.
							if(physics_ob->dynamic)
							{
								Vec4f unit_axis;
								float angle;
								physics_ob->rot.toAxisAndAngle(unit_axis, angle);

								ob->pos = Vec3d(pos.GetX(), pos.GetY(), pos.GetZ());
								ob->axis = Vec3f(unit_axis);
								ob->angle = angle;

								// Reduce smooth_translation and smooth_rotation over time to zero / identity rotation.  NOTE: This is deliberately before the getSmoothedObToWorldMatrix() call below,
								// so that getSmoothedObToWorldMatrix() result is unchanged over the rest of this frame.
								const float smooth_change_factor = (1 - 3.f * myMin(0.1f, (float)dt));
								assert(smooth_change_factor >= 0 && smooth_change_factor < 1);
								physics_ob->smooth_translation *= smooth_change_factor;
								physics_ob->smooth_rotation = Quatf::nlerp(Quatf::identity(), physics_ob->smooth_rotation, smooth_change_factor);
							}

							const Matrix4f ob_to_world = physics_ob->getSmoothedObToWorldMatrix();

							// Update OpenGL object
							if(ob->opengl_engine_ob.nonNull())
							{
								ob->opengl_engine_ob->ob_to_world_matrix = ob_to_world;

								const js::AABBox prev_gl_aabb_ws = ob->opengl_engine_ob->aabb_ws;
								opengl_engine->updateObjectTransformData(*ob->opengl_engine_ob);

								// For objects with instances (which will have a non-null instance_matrix_vbo), we want to use the AABB we computed in evalObjectScript(), which contains all the instance AABBs,
								// and will have been overwritten in updateObjectTransformData().
								if(ob->opengl_engine_ob->instance_matrix_vbo.nonNull())
									ob->opengl_engine_ob->aabb_ws = prev_gl_aabb_ws;
								else
								{
									ob->doTransformChanged(ob_to_world, ob->scale.toVec4fVector()); // Update info used for computing LOD level.
								}
							}

							// Update audio source for the object, if it has one.
							if(ob->audio_source.nonNull())
							{
								ob->audio_source->pos = ob->getCentroidWS();
								audio_engine.sourcePositionUpdated(*ob->audio_source);
							}

							// For dynamic objects that we are physics-owner of, get some extra state needed for physics snaphots
							if(physics_ob->dynamic && isObjectPhysicsOwnedBySelf(*ob, global_time))
							{
								JPH::Vec3 linear_vel, angular_vel;
								body_interface.GetLinearAndAngularVelocity(physics_ob->jolt_body_id, linear_vel, angular_vel);

								ob->linear_vel = toVec4fVec(linear_vel);
								ob->angular_vel = toVec4fVec(angular_vel);

								// Mark as from-local-physics-dirty to send a physics transform updated message to the server
								ob->from_local_physics_dirty = true;
								this->world_state->dirty_from_local_objects.insert(ob);

								// Check for sending of renewal of object physics ownership message
								checkRenewalOfPhysicsOwnershipOfObject(*ob, global_time);
							}
						}
					}
				}
				// Note that for instances, their OpenGL ob transform has effectively been set when instance_matrices was updated when evaluating scripts.
				// So we don't need to set it from the physics object.
			}

			// Process newly activated physics objects
			for(auto it = physics_world->newly_activated_obs.begin(); it != physics_world->newly_activated_obs.end(); ++it)
			{
				PhysicsObject* physics_ob = *it;
				if(physics_ob->userdata_type == 0 && physics_ob->userdata != 0) // If userdata type is WorldObject:
				{
#ifndef NDEBUG
					if(world_state->objects.find(physics_ob->ob_uid) == world_state->objects.end())
					{
						conPrint("Error: UID " + physics_ob->ob_uid.toString() + " not found for physics ob");
						assert(0);
					}
#endif
					WorldObject* ob = (WorldObject*)physics_ob->userdata;

					if(ob->isDynamic())
					{
						// If this object is already owned by another user, let them continue to own it. 
						// If it is unowned, however, take ownership of it.
						if(!isObjectPhysicsOwned(*ob, global_time) && !isObjectVehicleBeingDrivenByOther(*ob))
						{
							// conPrint("==Taking ownership of physics object...==");
							takePhysicsOwnershipOfObject(*ob, global_time);
						}
					}

					// If the showPhysicsObOwnershipCheckBox is checked, show an AABB visualisation.
					if(ui_interface->showPhysicsObOwnershipEnabled())
						obs_with_diagnostic_vis.insert(ob);
				}
			}
			physics_world->newly_activated_obs.clear();

		} // End activated_obs_mutex scope


		// Get camera position, if we are in a vehicle.
		if(vehicle_controller_inside.nonNull()) // If we are inside a vehicle:
		{
			// If we are driving the vehicle, use local physics transform, otherwise use smoothed network transformation, so that camera position is consistent with the vehicle model.
			const bool use_smoothed_network_transform = cur_seat_index != 0;
			campos = vehicle_controller_inside->getFirstPersonCamPos(*this->physics_world, cur_seat_index, use_smoothed_network_transform);

			const Vec4f linear_vel = vehicle_controller_inside->getLinearVel(*this->physics_world);

			player_physics.setCapsuleBottomPosition(Vec3d(campos) + Vec3d(0,0,-0.75f), linear_vel); // Hack an approximate sitting position
		}

		this->cam_controller.setFirstPersonPosition(toVec3d(campos));

		// Show vehicle speed on UI
		if(vehicle_controller_inside.nonNull()) // If we are inside a vehicle:
		{
			const float speed_km_h = vehicle_controller_inside->getLinearVel(*this->physics_world).length() * (3600.0f / 1000.f);
			misc_info_ui.showVehicleSpeed(speed_km_h);
			//misc_info_ui.showVehicleInfo(vehicle_controller_inside->getUIInfoMsg());
		}
		else
			misc_info_ui.hideVehicleSpeed();

		// Update debug player-physics visualisation spheres
		if(ui_interface->showPlayerPhysicsVisEnabled())
		{
			std::vector<js::BoundingSphere> spheres;
			player_physics.debugGetCollisionSpheres(campos, spheres);

			player_phys_debug_spheres.resize(spheres.size());
			
			for(size_t i=0; i<spheres.size(); ++i)
			{
				if(!player_phys_debug_spheres[i])
				{
					player_phys_debug_spheres[i] = opengl_engine->allocateObject();
					player_phys_debug_spheres[i]->ob_to_world_matrix = Matrix4f::identity();
					player_phys_debug_spheres[i]->mesh_data = opengl_engine->getSphereMeshData();

					OpenGLMaterial material;
					material.albedo_linear_rgb = (i < 3) ? Colour3f(0.3f, 0.8f, 0.3f) : Colour3f(0.8f, 0.3f, 0.3f);
					
					material.alpha = 0.5f;
					material.transparent = true;
					/*if(i >= 4)
					{
						material.albedo_rgb = Colour3f(0.1f, 0.1f, 0.9f);
						material.transparent = false;
					}*/

					player_phys_debug_spheres[i]->setSingleMaterial(material);

					opengl_engine->addObject(player_phys_debug_spheres[i]);
				}

				player_phys_debug_spheres[i]->ob_to_world_matrix = Matrix4f::translationMatrix(spheres[i].getCenter()) * Matrix4f::uniformScaleMatrix(spheres[i].getRadius());
				opengl_engine->updateObjectTransformData(*player_phys_debug_spheres[i]);
			}
		}
		else
		{
			// Remove any existing vis spheres
			if(!player_phys_debug_spheres.empty())
			{
				for(size_t i=0; i<player_phys_debug_spheres.size(); ++i)
				{
					if(player_phys_debug_spheres[i])
						opengl_engine->removeObject(player_phys_debug_spheres[i]);
				}
				player_phys_debug_spheres.resize(0);
			}
		}


		// Update debug visualisations on vehicle controllers. NOTE: needs to go after physics update or vis will lag behind.
		for(auto it = vehicle_controllers.begin(); it != vehicle_controllers.end(); ++it)
			it->second->updateDebugVisObjects();


		// Set some basic 3rd person cam variables that will be updated below if we are connected to a server
		if(false)
		{
			const Vec3d cam_back_dir = cam_controller.getForwardsVec() * -3.0 + cam_controller.getUpVec() * 0.2;
			this->cam_controller.setThirdPersonCamPosition(toVec3d(campos) + Vec3d(cam_back_dir));
		}


		// TODO: If we are using 3rd person can, use animation events from the walk/run cycle animations to trigger sounds.
		// Adapted from AvatarGraphics::setOverallTransform():
		// Only consider speed in x-y plane when deciding whether to play walk/run anim etc..
		// This is because the stair-climbing code may make jumps in the z coordinate which means a very high z velocity.
		const Vec3f xyplane_vel = player_physics.getLastXYPlaneVelRelativeToGround();
		float xyplane_speed = xyplane_vel.length();

		if(player_physics.onGroundRecently() && our_move_impulse_zero && !player_physics.flyModeEnabled()) // Suppress footsteps when on ground and not trying to move (walk anims should not be played in this case)
			xyplane_speed = 0;

		if((xyplane_speed > 0.1f) && vehicle_controller_inside.isNull()) // Don't play footstep sounds if sitting in vehicle.
		{
			const float walk_run_cycle_period = player_physics.isRunPressed() ? AvatarGraphics::runCyclePeriod() : AvatarGraphics::walkCyclePeriod();
			if(player_physics.onGroundRecently() && (last_footstep_timer.elapsed() > (walk_run_cycle_period * 0.5f)))
			{
				last_foostep_side = (last_foostep_side + 1) % 2;

				// 4cm left/right, 40cm forwards.
				const Vec4f footstrike_pos = campos - Vec4f(0, 0, 1.72f, 0) +
					cam_controller.getForwardsVec().toVec4fVector() * 0.4f +
					cam_controller.getRightVec().toVec4fVector() * 0.04f * (last_foostep_side == 1 ? 1.f : -1.f);
				
				// conPrint("footstrike_pos: " + footstrike_pos.toStringNSigFigs(3) + ", playing " + last_footstep_timer.elapsedStringNSigFigs(3) + " after last footstep");

				const int rnd_src_i = rng.nextUInt(4);
				audio_engine.playOneShotSound(resources_dir_path + "/sounds/footstep_mono" + toString(rnd_src_i) + ".wav", footstrike_pos);

				last_footstep_timer.reset();
			}
		}

		if(physics_events.jumped)
		{
			const Vec4f jump_sound_pos = campos - Vec4f(0, 0, 0.1f, 0) +
				cam_controller.getForwardsVec().toVec4fVector() * 0.1f;

			const int rnd_src_i = rng.nextUInt(4);
			audio_engine.playOneShotSound(resources_dir_path + "/sounds/jump" + toString(rnd_src_i) + ".wav", jump_sound_pos);
		}
	}
	proximity_loader.updateCamPos(campos);

	

	const Vec3d avatar_cam_angles = this->cam_controller.getAvatarAngles();

	// Find out which parcel we are in, if any.
	ParcelID new_in_parcel_id = ParcelID::invalidParcelID();
	bool mute_outside_audio = false;
	if(world_state.nonNull())
	{
		//Timer timer;
		const Vec3d campos_vec3d = this->cam_controller.getFirstPersonPosition();
		WorldStateLock lock(world_state->mutex);
		const Parcel* parcel = world_state->getParcelPointIsIn(campos_vec3d, /*guess parcel id=*/this->cur_in_parcel_id);
		if(parcel)
		{
			// Set audio source room effects
			audio_engine.setCurentRoomDimensions(js::AABBox(
				Vec4f((float)parcel->aabb_min.x, (float)parcel->aabb_min.y, (float)parcel->aabb_min.z, 1.f),
				Vec4f((float)parcel->aabb_max.x, (float)parcel->aabb_max.y, (float)parcel->aabb_max.z, 1.f)));

			new_in_parcel_id = parcel->id;

			if(BitUtils::isBitSet(parcel->flags, Parcel::MUTE_OUTSIDE_AUDIO_FLAG))
				mute_outside_audio = true;
		}

		audio_engine.setRoomEffectsEnabled(parcel != NULL);

		//conPrint("Setting room effects took " + timer.elapsedStringNSigFigs(4));

		// Check to see if we have changed the current parcel, and run any onUserEnteredParcel and onUserExitedParcel events
		// for objects in those parcels
		// cur_in_parcel_id is the id of the parcel we were in previously.
		if(new_in_parcel_id != this->cur_in_parcel_id) // If we moved to a new parcel (or out of any parcel)
		{
			// Execute onUserExitedParcel events for any objects in parcel we just left
			if(this->cur_in_parcel_id.valid())
			{
				// conPrint("User exited parcel: " + cur_in_parcel_id.toString());
				auto res = world_state->parcels.find(this->cur_in_parcel_id);
				if(res != world_state->parcels.end())
				{
					const Parcel* cur_parcel = res->second.ptr();
					WorldObjectRef* const objects_data = scripted_ob_proximity_checker.objects.vector.data();
					const size_t objects_size          = scripted_ob_proximity_checker.objects.vector.size();
					for(size_t i=0; i<objects_size; ++i)
					{
						WorldObject* ob = objects_data[i].ptr();
						if(cur_parcel->pointInParcel(ob->getCentroidWS())) // If object is in parcel we just left:
						{
							if(ob->event_handlers && ob->event_handlers->onUserExitedParcel_handlers.nonEmpty())
							{
								// Execute onUserExitedParcel event handler in any scripts that are listening for onUserExitedParcel for this object
								ob->event_handlers->executeOnUserExitedParcelHandlers(this->client_avatar_uid, ob->uid, cur_in_parcel_id, lock);

								// Send msg to server to execute on server as well.
								MessageUtils::initPacket(scratch_packet, Protocol::UserExitedParcelMessage);
								writeToStream(ob->uid, scratch_packet);
								writeToStream(cur_in_parcel_id, scratch_packet);
								enqueueMessageToSend(*client_thread, scratch_packet);
							}
						}
					}
				}
			}

			if(parcel)
			{
				// conPrint("User entered new parcel: " + parcel->id.toString());
				WorldObjectRef* const objects_data = scripted_ob_proximity_checker.objects.vector.data();
				const size_t objects_size          = scripted_ob_proximity_checker.objects.vector.size();
				for(size_t i=0; i<objects_size; ++i)
				{
					WorldObject* ob = objects_data[i].ptr();
					if(parcel->pointInParcel(ob->getCentroidWS())) // If object is in parcel we just entered:
					{
						if(ob->event_handlers && ob->event_handlers->onUserEnteredParcel_handlers.nonEmpty())
						{
							// Execute onUserEnteredParcel event handler in any scripts that are listening for onUserEnteredParcel for this object
							ob->event_handlers->executeOnUserEnteredParcelHandlers(this->client_avatar_uid, ob->uid, new_in_parcel_id, lock);

							// Send msg to server to execute on server as well.
							MessageUtils::initPacket(scratch_packet, Protocol::UserEnteredParcelMessage);
							writeToStream(ob->uid, scratch_packet);
							writeToStream(parcel->id, scratch_packet);
							enqueueMessageToSend(*client_thread, scratch_packet);
						}
					}
				}

				// Send UserEnteredParcelMessage without an object UID.  This will be used for adding the user to social event attendee lists.
				MessageUtils::initPacket(scratch_packet, Protocol::UserEnteredParcelMessage);
				writeToStream(UID::invalidUID(), scratch_packet);
				writeToStream(parcel->id, scratch_packet);
				enqueueMessageToSend(*client_thread, scratch_packet);
			}

			this->cur_in_parcel_id = new_in_parcel_id;
		}
	}

	//printVar(in_parcel_id.value());

	// Set audio source occlusions and check for muting audio sources not in current parcel.
	if(physics_world.nonNull())
	{
		PERFORMANCEAPI_INSTRUMENT("audio occlusions");
		ZoneScopedN("audio occlusions"); // Tracy profiler

		Lock lock(audio_engine.mutex);
		for(auto it = audio_engine.audio_sources.begin(); it != audio_engine.audio_sources.end(); ++it)
		{
			glare::AudioSource* source = it->ptr();

			const float dist = source->pos.getDist(campos); // Dist from camera to source position
			if(dist < maxAudioDistForSourceVolFactor(source->volume)) // Only do tracing for nearby objects
			{
				const Vec4f trace_dir = (dist == 0) ? Vec4f(1,0,0,0) : ((source->pos - campos) / dist); // Trace from camera to source position
				assert(trace_dir.isUnitLength());

				const float max_trace_dist = 60.f; // Limit the distance we trace, so that very loud sources very far away don't do expensive traces right through the world.

				const float trace_dist = myClamp(dist - 1.f, 0.f, max_trace_dist); // Ignore intersections with x metres of the source.  This is so meshes that contain the source (e.g. speaker models)
				// don't occlude the source.

				const Vec4f trace_start = campos;

				const bool hit_object = physics_world->doesRayHitAnything(trace_start, trace_dir, trace_dist);
				if(hit_object)
				{
					//conPrint("hit aabb: " + results.hit_object->aabb_ws.toStringNSigFigs(4));
					//printVar(results.hit_object->userdata_type);
					source->num_occlusions = 1;
				}
				else
					source->num_occlusions = 0;

				//conPrint("source: " + toString((uint64)source) + ", hit_object: " + boolToString(hit_object) + ", source parcel: " + toString(source->userdata_1));

				
				const bool source_is_one_shot = (source->type == glare::AudioSource::SourceType_NonStreaming) && !source->looping;
				if(!source_is_one_shot) // We won't be muting footsteps etc.
				{
					const float old_mute_volume_factor = source->getMuteVolumeFactor();
					if(mute_outside_audio) // If we are in a parcel, which has the mute-outside-audio option enabled:
					{
						if(source->userdata_1 != new_in_parcel_id.value()) // And the source is in another parcel (or not in any parcel):
							source->startMuting(cur_time, 1);
						else
							source->startUnmuting(cur_time, 1);
					}
					else
						source->startUnmuting(cur_time, 1);

					source->updateCurrentMuteVolumeFactor(cur_time);

					if(old_mute_volume_factor != source->getMuteVolumeFactor())
						audio_engine.sourceVolumeUpdated(*source);
				}


				// printVar(source->num_occlusions);
				audio_engine.sourceNumOcclusionsUpdated(*source);
			}
		}
	}


	updateAvatarGraphics(cur_time, dt, avatar_cam_angles, our_move_impulse_zero);


	// Set camera controller target transform (used for tracking and fixed-angle camera modes)
	if(vehicle_controller_inside)
	{
		const Quatf model_to_y_forward_quat = vehicle_controller_inside->getSettings().model_to_y_forwards_rot_2 * vehicle_controller_inside->getSettings().model_to_y_forwards_rot_1;
		const Matrix4f y_forward_to_model_space_rot = (model_to_y_forward_quat.conjugate()).toMatrix();

		cam_controller.setTargetObjectTransform(vehicle_controller_inside->getControlledObject()->opengl_engine_ob->ob_to_world_matrix, y_forward_to_model_space_rot);
	}
	else
	{
		cam_controller.setTargetObjectTransform(Matrix4f::translationMatrix(cam_controller.getFirstPersonPosition().toVec4fVector()), /*y_forward_to_model_space_rot=*/Matrix4f::identity());
	}



	cam_controller.think(dt);


	// Set third-person camera position.  NOTE: this goes after updateAvatarGraphics since it depends on where the player's avatar is,
	// which can depend on interpolated vehicle physics etc.
	setThirdPersonCameraPosition(dt);




	// Resonance seems to want a to-world transformation
	// It also seems to use the OpenGL camera convention (x = right, y = up, -z = forwards)

	const Vec3d cam_angles = this->cam_controller.getAngles();
	const Quatf z_axis_rot_q = Quatf::fromAxisAndAngle(Vec3f(0,0,1), (float)cam_angles.x - Maths::pi_2<float>());
	const Quatf x_axis_rot_q = Quatf::fromAxisAndAngle(Vec3f(1,0,0), Maths::pi<float>() - (float)cam_angles.y);
	const Quatf q = z_axis_rot_q * x_axis_rot_q;
	audio_engine.setHeadTransform(this->cam_controller.getPosition().toVec4fPoint(), q);


	// Send a AvatarEnteredVehicle to server with renewal bit set, occasionally.
	// This is so any new player joining the world after we entered the vehicle can receive the information that we are inside it.
	if(vehicle_controller_inside.nonNull() && ((cur_time - last_vehicle_renewal_msg_time) > 4.0))
	{
		// conPrint("sending AvatarEnteredVehicle renewal msg");

		// Send AvatarEnteredVehicle message to server
		MessageUtils::initPacket(scratch_packet, Protocol::AvatarEnteredVehicle);
		writeToStream(this->client_avatar_uid, scratch_packet);
		writeToStream(this->vehicle_controller_inside->getControlledObject()->uid, scratch_packet); // Write vehicle object UID
		scratch_packet.writeUInt32(this->cur_seat_index); // Seat index.
		scratch_packet.writeUInt32(1); // Write flags.  Set renewal bit.
		enqueueMessageToSend(*this->client_thread, scratch_packet);

		last_vehicle_renewal_msg_time = cur_time;
	}

	

	//TEMP
#if 0
	for(size_t i=0; i<test_avatars.size(); ++i)
	{
		AvatarRef test_avatar = test_avatars[i];
		
		/*double phase_speed = 0.5;
		if((int)(cur_time * 0.2) % 2 == 0)
		{
			phase_speed = 0;
		}*/
		//double phase_speed = 0;


		PoseConstraint pose_constraint;
		pose_constraint.sitting = false;
		pose_constraint.seat_to_world = Matrix4f::translationMatrix(0,0,1.7f);

		AnimEvents anim_events;
		//test_avatar_phase += phase_speed * dt;
		const double r = 5 + (i % 10);
		//Vec3d pos(0, 0, 1.67);//cos(test_avatar_phase) * r, sin(test_avatar_phase) * r, 1.67);
		const double phase = Maths::get2Pi<double>() * i / test_avatars.size() + cur_time * 0.1;
		Vec3d pos(r * cos(phase), r * sin(phase), 1.67);//cos(test_avatar_phase) * r, sin(test_avatar_phase) * r, 1.67);
		const int anim_state = 0;
		float xyplane_speed_rel_ground = 0;
		test_avatar->graphics.setOverallTransform(*opengl_engine, pos, 
			Vec3f(0, /*pitch=*/Maths::pi_2<float>(), (float)phase + Maths::pi_2<float>()), 
			/*use_xyplane_speed_rel_ground_override=*/false, xyplane_speed_rel_ground, test_avatar->avatar_settings.pre_ob_to_world_matrix, anim_state, cur_time, dt, pose_constraint, anim_events);
		if(anim_events.footstrike)
		{
			//conPrint("footstrike");
			//footstep_source->cur_read_i = 0;
			//audio_engine.setSourcePosition(footstep_source, anim_events.footstrike_pos.toVec4fPoint());
			const int rnd_src_i = rng.nextUInt(4);// (uint32)footstep_sources.size());
			//audio_engine.setSourcePosition(footstep_sources[rnd_src_i], anim_events.footstrike_pos.toVec4fPoint());
			//footstep_sources[rnd_src_i]->cur_read_i = 0;

			//audio_engine.playOneShotSound(base_dir_path + "/resources/sounds/footstep_mono" + toString(rnd_src_i) + ".wav", anim_events.footstrike_pos.toVec4fPoint());
		}
	}
#endif
	
	const Vec4f first_or_third_person_campos = this->cam_controller.getPosition().toVec4fPoint();

	// Update world object graphics and physics models that have been marked as from-server-dirty based on incoming network messages from server.
	// Only do this once connected, to avoid a race condition where we start adding objects without first receiving the server capabilities in the ClientConnectedToServerMessage.
	if(world_state && (connection_state == ServerConnectionState_Connected))
	{
		ZoneScopedN("processing dirty_from_remote_objects"); // Tracy profiler

		try
		{
			WorldStateLock lock(this->world_state->mutex);

			// Make sure server_using_lod_chunks is set before we start calling shouldDisplayLODChunk() below
			if(!this->world_state->lod_chunks.empty() && LOD_CHUNK_SUPPORT)
				this->server_using_lod_chunks = true;

			for(auto it = this->world_state->dirty_from_remote_objects.begin(); it != this->world_state->dirty_from_remote_objects.end(); ++it)
			{
				WorldObject* ob = it->ptr();

				assert((this->world_state->objects.find(ob->uid) != this->world_state->objects.end()) && (this->world_state->objects.find(ob->uid).getValue().ptr() == ob)); // Make sure this object in the dirty set is in our set of objects.

				// conPrint("Processing dirty object.");

				if(ob->from_remote_other_dirty || ob->from_remote_model_url_dirty)
				{
					if(ob->state == WorldObject::State_Dead)
					{
						print("Removing WorldObject.");

						removeAndDeleteGLAndPhysicsObjectsForOb(*ob);

						//proximity_loader.removeObject(ob);

						// ui->indigoView->objectRemoved(*ob);

						ob->web_view_data = NULL;
						ob->browser_vid_player = NULL;

						if(ob->audio_source.nonNull())
						{
							audio_engine.removeSource(ob->audio_source);
							ob->audio_source = NULL;
							ob->audio_state = WorldObject::AudioState_NotLoaded;
						}

						removeInstancesOfObject(ob);
						//removeObScriptingInfo(ob);

						this->world_state->objects.erase(ob->uid);

						active_objects.erase(ob);
						obs_with_animated_tex.erase(ob);
						web_view_obs.erase(ob);
						browser_vid_player_obs.erase(ob);
						audio_obs.erase(ob);
						obs_with_scripts.erase(ob);
						obs_with_diagnostic_vis.erase(ob);

						scripted_ob_proximity_checker.removeObject(ob);
					}
					else // Else if not dead:
					{
						if(ob->state == WorldObject::State_JustCreated)
							ob->was_just_created = true;
						else if(ob->state == WorldObject::State_InitialSend)
							ob->was_just_created = false;

						// Decompress voxel group
						//ob->decompressVoxels();

						//proximity_loader.checkAddObject(ob); // Calls loadModelForObject() and loadAudioForObject() if it is within load distance.

						if(ob->state == WorldObject::State_JustCreated)
							enableMaterialisationEffectOnOb(*ob); // Enable materialisation effect before we call loadModelForObject() below.

						// Make sure lod level is set before calling loadModelForObject(), which will start downloads based on the lod level.
						ob->current_lod_level = ob->getLODLevel(cam_controller.getPosition());

						ob->in_proximity = ob->getCentroidWS().getDist2(campos) < this->load_distance2;
						const Vec3i chunk_coords(Maths::floorToInt(ob->getCentroidWS()[0] / chunk_w), Maths::floorToInt(ob->getCentroidWS()[1] / chunk_w), 0);
						if(this->server_using_lod_chunks && shouldDisplayLODChunk(chunk_coords, first_or_third_person_campos) && !ob->exclude_from_lod_chunk_mesh)
							ob->in_proximity = false;
						assert(ob->exclude_from_lod_chunk_mesh == BitUtils::isBitSet(ob->flags, WorldObject::EXCLUDE_FROM_LOD_CHUNK_MESH));

						if(ob->in_proximity)
						{
							loadModelForObject(ob, lock);
							loadAudioForObject(ob, /*loaded buffer=*/nullptr);
						}
						
						if(!ob->audio_source_url.empty() || ob->object_type == WorldObject::ObjectType_WebView || ob->object_type == WorldObject::ObjectType_Video)
							this->audio_obs.insert(ob);

						const bool ob_just_created_or_initially_sent = (ob->state == WorldObject::State_JustCreated) || (ob->state == WorldObject::State_InitialSend);

						if(!ob_just_created_or_initially_sent) // Don't reload materials when we just created the object locally.
						{
							// Update transform for object and object materials in OpenGL engine
							if(ob->opengl_engine_ob.nonNull() && (ob != selected_ob.getPointer())) // Don't update the selected object based on network messages, we will consider the local transform for it authoritative.
							{
								GLObjectRef opengl_ob = ob->opengl_engine_ob;

								// Update transform
								opengl_ob->ob_to_world_matrix = ob->obToWorldMatrix();
								opengl_engine->updateObjectTransformData(*opengl_ob);

								// Update materials in opengl engine.
								const int ob_lod_level = ob->getLODLevel(cam_controller.getPosition());
								for(size_t i=0; i<ob->materials.size(); ++i)
									if(i < opengl_ob->materials.size())
										ModelLoading::setGLMaterialFromWorldMaterial(*ob->materials[i], ob_lod_level, ob->lightmap_url, /*use_basis=*/this->server_has_basis_textures, *this->resource_manager, opengl_ob->materials[i]);

								opengl_engine->objectMaterialsUpdated(*opengl_ob);

								if(ob->object_type == WorldObject::ObjectType_Spotlight)
									updateSpotlightGraphicsEngineData(opengl_ob->ob_to_world_matrix, ob);

								updateInstancedCopiesOfObject(ob);
							}

							if(ob == selected_ob.ptr())
								ui_interface->objectModelURLUpdated(*ob); // Update model URL in UI if we have selected the object.


							if(ob->object_type == WorldObject::ObjectType_Text)
							{
								if(BitUtils::isBitSet(ob->changed_flags, WorldObject::CONTENT_CHANGED))
								{
									BitUtils::zeroBit(ob->changed_flags, WorldObject::CONTENT_CHANGED);
									recreateTextGraphicsAndPhysicsObs(ob);
								}
							}

							loadAudioForObject(ob, /*loaded buffer=*/nullptr); // Check for re-loading audio if audio URL changed.

							// Update audio volume if the audio_source has been created
							if(ob->audio_source && (ob->audio_source->volume != ob->audio_volume))
							{
								ob->audio_source->volume = ob->audio_volume;
								audio_engine.sourceVolumeUpdated(*ob->audio_source);
							}
						}

						// If we just created an object, and there are pending event handlers for the object, assign to the object, and remove from the pending event handler map.
						if(ob_just_created_or_initially_sent)
						{
							auto res = world_state->pending_event_handlers.find(ob->uid);
							if(res != world_state->pending_event_handlers.end())
							{
								ob->event_handlers = res->second; // Assign handlers to object

								world_state->pending_event_handlers.erase(ob->uid);

								// TEMP Just add to scripted_ob_proximity_checker for all event types for now.
								scripted_ob_proximity_checker.addObject(ob);
							}
						}


						if(ob->state == WorldObject::State_JustCreated)
						{
							// Got just created object

							if(last_restored_ob_uid_in_edit.valid())
							{
								//conPrint("Adding mapping from " + last_restored_ob_uid_in_edit.toString() + " to " + ob->uid.toString());
								recreated_ob_uid[last_restored_ob_uid_in_edit] = ob->uid;
								last_restored_ob_uid_in_edit = UID::invalidUID();
							}

							// If this object was (just) created by this user, select it.  NOTE: bit of a hack distinguishing newly created objects by checking numSecondsAgo().
							// Don't select summoned vehicles though, as the intent is probably to ride them, not edit them.
							if((ob->creator_id == this->logged_in_user_id) && (ob->created_time.numSecondsAgo() < 30) && !BitUtils::isBitSet(ob->flags, WorldObject::SUMMONED_FLAG))
								selectObject(ob, /*selected_mat_index=*/0); // select it

							ob->state = WorldObject::State_Alive;
						}
						else if(ob->state == WorldObject::State_InitialSend)
						{
							ob->state = WorldObject::State_Alive;
						}

						ob->from_remote_other_dirty = false;
						ob->from_remote_model_url_dirty = false;
					}
				}
				else if(ob->from_remote_lightmap_url_dirty)
				{
					// Try and download any resources we don't have for this object
					const int ob_lod_level = ob->getLODLevel(cam_controller.getPosition());
					startDownloadingResourcesForObject(ob, ob_lod_level);

					// Update materials in opengl engine, so it picks up the new lightmap URL
					GLObjectRef opengl_ob = ob->opengl_engine_ob;
					if(opengl_ob.nonNull())
					{
						for(size_t i=0; i<ob->materials.size(); ++i)
							if(i < opengl_ob->materials.size())
								ModelLoading::setGLMaterialFromWorldMaterial(*ob->materials[i], ob_lod_level, ob->lightmap_url, /*use_basis=*/this->server_has_basis_textures, *this->resource_manager, opengl_ob->materials[i]);
						opengl_engine->objectMaterialsUpdated(*opengl_ob);
					}

					ob->lightmap_baking = false; // Since the lightmap URL has changed, we will assume that means the baking is done for this object.

					if(ob == selected_ob.ptr())
						ui_interface->objectLightmapURLUpdated(*ob); // Update lightmap URL in UI if we have selected the object.

					ob->from_remote_lightmap_url_dirty = false;
				}
				else if(ob->from_remote_content_dirty)
				{
					// conPrint("GUICLIENT: timerEvent(): handling from_remote_content_dirty..");

					if(ob->object_type == WorldObject::ObjectType_Text)
					{
						recreateTextGraphicsAndPhysicsObs(ob);
						BitUtils::zeroBit(ob->changed_flags, WorldObject::CONTENT_CHANGED);
					}
					// TODO: handle non-text objects.  Also move this code into some kind of objectChanged() function?

					ob->from_remote_content_dirty = false;
				}
				else if(ob->from_remote_physics_ownership_dirty)
				{
					if(ui_interface->showPhysicsObOwnershipEnabled())
						obs_with_diagnostic_vis.insert(ob);

					ob->from_remote_physics_ownership_dirty = false;
				}
				
				if(ob->from_remote_transform_dirty)
				{
					active_objects.insert(ob); // Add to active_objects: objects that have moved recently and so need interpolation done on them.

					ob->from_remote_transform_dirty = false;
				}

				if(ob->from_remote_physics_transform_dirty)
				{
					active_objects.insert(ob); // Add to active_objects: objects that have moved recently and so need interpolation done on them.

					ob->from_remote_physics_transform_dirty = false;
				}

				if(ob->from_remote_summoned_dirty)
				{
					enableMaterialisationEffectOnOb(*ob);

					// Set physics and object transforms here explictly instead of relying on interpolation or whatever.
					// This is because summoning moves objects discontinuously, so we don't want to interpolate.
					if(ob->physics_object.nonNull())
					{
						physics_world->setNewObToWorldTransform(*ob->physics_object, ob->pos.toVec4fVector(), Quatf::fromAxisAndAngle(normalise(ob->axis), ob->angle), 
							Vec4f(0), Vec4f(0));

						ob->physics_object->smooth_rotation = Quatf::identity(); // We don't want to smooth the transformation change.
						ob->physics_object->smooth_translation = Vec4f(0);
					}

					if(ob->opengl_engine_ob.nonNull())
					{
						ob->opengl_engine_ob->ob_to_world_matrix = obToWorldMatrix(*ob);
						opengl_engine->updateObjectTransformData(*ob->opengl_engine_ob);
					}

					ob->from_remote_summoned_dirty = false;
				}
			}

			this->world_state->dirty_from_remote_objects.clear();
		}
		catch(glare::Exception& e)
		{
			print("Error while Updating object graphics: " + e.what());
		}


		updateParcelGraphics();

		updateLODChunkGraphics();
	}

	if(world_state)
	{
		// Interpolate any active objects (Objects that have moved recently and so need interpolation done on them.)
		{
			Lock lock(this->world_state->mutex);
			for(auto it = active_objects.begin(); it != active_objects.end();)
			{
				WorldObject* ob = it->ptr();

				// See if object should be removed from the active set - an object should be removed if it has been a while since the last transform snapshot has been received.
				const uint32 last_snapshot_mod_i = Maths::intMod(ob->next_snapshot_i - 1, WorldObject::HISTORY_BUF_SIZE);
				const bool inactive = (cur_time - ob->snapshots[last_snapshot_mod_i].local_time) > 1.0;
				if(inactive)
				{
					// conPrint("------Removing inactive object-------");
					// Object is not active any more, remove from active_objects set.
					auto to_erase = it;
					it++;
					active_objects.erase(to_erase);
				}
				else
				{
					if(ob->isDynamic() && isObjectPhysicsOwnedBySelf(*ob, global_time)) // If this is a dynamic physics object that we are the current physics owner of:
					{
						// Don't update its transform from physics snapshots, let the physics engine set it directly.
						// conPrint("Skipping interpolation of dynamic ob - we own it");
					}
					else
					{
						if(ob->snapshots_are_physics_snapshots)
						{
							// See if it's time to feed a physics snapshot into the physics system.  See 'docs\networked physics.txt' for more details.
							const double padding_delay = 0.1;

							// conPrint("next_insertable_snapshot_i: " + toString(ob->next_insertable_snapshot_i) + ", next_snapshot_i: " + toString(ob->next_snapshot_i));

							if(ob->next_insertable_snapshot_i < ob->next_snapshot_i) // If we have at least one snapshot that has not been inserted:
							{
								const uint32 next_insertable_snapshot_mod_i = Maths::intMod(ob->next_insertable_snapshot_i, WorldObject::HISTORY_BUF_SIZE);
								const WorldObject::Snapshot& snapshot = ob->snapshots[next_insertable_snapshot_mod_i];
								const double desired_insertion_time = snapshot.client_time + ob->transmission_time_offset + padding_delay;
								// conPrint("------------------------------------");
								// conPrint("snapshot.client_time: " + toString(snapshot.client_time));
								// conPrint("ob->transmission_time_offset: " + toString(ob->transmission_time_offset));
								// conPrint("desired_insertion_time: " + toString(desired_insertion_time) + ", global_time: " + toString(global_time) + "(" + toString(desired_insertion_time - global_time) + " s in future)");
								if(global_time >= desired_insertion_time)
								{
									// conPrint("Inserting physics snapshot " + toString(ob->next_insertable_snapshot_i) + " into physics system at time " + toString(global_time));
									if(ob->physics_object.nonNull())
									{
										const Vec4f old_effective_pos = ob->physics_object->smooth_translation + ob->physics_object->pos;
										const Quatf old_effective_rot = ob->physics_object->smooth_rotation    * ob->physics_object->rot;
										physics_world->setNewObToWorldTransform(*ob->physics_object, snapshot.pos, snapshot.rotation, snapshot.linear_vel, snapshot.angular_vel);

										// Compute smoothing translation and rotation transforms that will map the snapshot position to the current effective position.
										ob->physics_object->smooth_translation = old_effective_pos - snapshot.pos;
										ob->physics_object->smooth_rotation    = old_effective_rot * snapshot.rotation.conjugate();
									}

									ob->next_insertable_snapshot_i++;
								}
							}
						}
						else
						{
							// conPrint("Getting interpolated transform");
							Vec3d pos;
							Quatf rot;
							ob->getInterpolatedTransform(cur_time, pos, rot);

							if(ob->opengl_engine_ob.nonNull())
							{
								ob->opengl_engine_ob->ob_to_world_matrix = Matrix4f::translationMatrix((float)pos.x, (float)pos.y, (float)pos.z) * 
									rot.toMatrix() *
									Matrix4f::scaleMatrix(ob->scale.x, ob->scale.y, ob->scale.z);

								opengl_engine->updateObjectTransformData(*ob->opengl_engine_ob);
							}

							if(ob->physics_object.nonNull())
							{
								// Update in physics engine
								physics_world->setNewObToWorldTransform(*ob->physics_object, Vec4f((float)pos.x, (float)pos.y, (float)pos.z, 0.f), rot, useScaleForWorldOb(ob->scale).toVec4fVector());
							}

							if(ob->audio_source.nonNull())
							{
								// Update in audio engine
								ob->audio_source->pos = ob->getCentroidWS();
								audio_engine.sourcePositionUpdated(*ob->audio_source);
							}
						}

						if(ui_interface->showPhysicsObOwnershipEnabled())
							obs_with_diagnostic_vis.insert(ob);
					}
					it++;
				}
			}
		}
	} // end if(world_state)


	// Move selected object if there is one and it is picked up, based on direction camera is currently facing.
	if(this->selected_ob.nonNull() && selected_ob_picked_up)
	{
		const bool allow_modification = objectModificationAllowedWithMsg(*this->selected_ob, "move");
		if(allow_modification)
		{
			// Get direction for current mouse cursor position
			const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
			const Vec4f forwards = cam_controller.getForwardsVec().toVec4fVector();
			const Vec4f right = cam_controller.getRightVec().toVec4fVector();
			const Vec4f up = cam_controller.getUpVec().toVec4fVector();

			// Convert selection vector from camera space to world space
			const Vec4f selection_vec_ws = right*selection_vec_cs[0] + forwards*selection_vec_cs[1] + up*selection_vec_cs[2];

			// Get the target position for the new selection point in world space.
			const Vec4f new_sel_point_ws = origin + selection_vec_ws;

			// Get the current position for the selection point on the object in world-space.
			const Vec4f selection_point_ws = obToWorldMatrix(*this->selected_ob) * this->selection_point_os;

			const Vec4f desired_new_ob_pos = this->selected_ob->pos.toVec4fPoint() + (new_sel_point_ws - selection_point_ws);

			assert(desired_new_ob_pos.isFinite());

			//Matrix4f tentative_new_to_world = this->selected_ob->opengl_engine_ob->ob_to_world_matrix;
			//tentative_new_to_world.setColumn(3, desired_new_ob_pos);
			//tryToMoveObject(tentative_new_to_world);
			tryToMoveObject(this->selected_ob, desired_new_ob_pos);
		}
	}

	updateVoxelEditMarkers(mouse_cursor_state);

	

	// Send an AvatarTransformUpdate packet to the server if needed.
	if(client_thread.nonNull() && (time_since_update_packet_sent.elapsed() > 0.1))
	{
		PERFORMANCEAPI_INSTRUMENT("sending packets");
		ZoneScopedN("sending packets"); // Tracy profiler

		// Send AvatarTransformUpdate packet
		{
			const uint32 anim_state = 
				(player_physics.onGroundRecently() ? 0 : AvatarGraphics::ANIM_STATE_IN_AIR) | 
				(player_physics.flyModeEnabled() ? AvatarGraphics::ANIM_STATE_FLYING : 0) |
				(our_move_impulse_zero ? AvatarGraphics::ANIM_STATE_MOVE_IMPULSE_ZERO : 0);

			const uint32 input_bitmask = physics_input.toBitFlags();

			MessageUtils::initPacket(scratch_packet, Protocol::AvatarTransformUpdate);
			writeToStream(this->client_avatar_uid, scratch_packet);
			writeToStream(Vec3d(this->cam_controller.getFirstPersonPosition()), scratch_packet);
			writeToStream(Vec3f(0, (float)avatar_cam_angles.y, (float)avatar_cam_angles.x), scratch_packet);
			scratch_packet.writeUInt32(anim_state | (input_bitmask << 16));

			enqueueMessageToSend(*this->client_thread, scratch_packet);
		}

		
		if(world_state.nonNull())
		{
			Lock lock(this->world_state->mutex);

			//============ Send any object updates needed ===========
			for(auto it = this->world_state->dirty_from_local_objects.begin(); it != this->world_state->dirty_from_local_objects.end(); ++it)
			{
				WorldObject* world_ob = it->getPointer();
				if(world_ob->from_local_other_dirty)
				{
					// Enqueue ObjectFullUpdate
					MessageUtils::initPacket(scratch_packet, Protocol::ObjectFullUpdate);
					world_ob->writeToNetworkStream(scratch_packet);

					enqueueMessageToSend(*this->client_thread, scratch_packet);

					world_ob->from_local_other_dirty = false;
					world_ob->from_local_transform_dirty = false; // We sent all information, including transform, so transform is no longer dirty.
					world_ob->from_local_physics_dirty = false;
				}
				else if(world_ob->from_local_transform_dirty)
				{
					// Enqueue ObjectTransformUpdate
					MessageUtils::initPacket(scratch_packet, Protocol::ObjectTransformUpdate);
					writeToStream(world_ob->uid, scratch_packet);
					writeToStream(Vec3d(world_ob->pos), scratch_packet);
					writeToStream(Vec3f(world_ob->axis), scratch_packet);
					scratch_packet.writeFloat(world_ob->angle);
					writeToStream(Vec3f(world_ob->scale), scratch_packet);

					enqueueMessageToSend(*this->client_thread, scratch_packet);

					world_ob->from_local_transform_dirty = false;
				}
				else if(world_ob->from_local_physics_dirty)
				{
					// Send ObjectPhysicsTransformUpdate packet
					MessageUtils::initPacket(scratch_packet, Protocol::ObjectPhysicsTransformUpdate);
					writeToStream(world_ob->uid, scratch_packet);
					writeToStream(world_ob->pos, scratch_packet);

					const Quatf rot = Quatf::fromAxisAndAngle(world_ob->axis, world_ob->angle);
					scratch_packet.writeData(&rot.v.x, sizeof(float) * 4);

					scratch_packet.writeData(world_ob->linear_vel.x, sizeof(float) * 3);
					scratch_packet.writeData(world_ob->angular_vel.x, sizeof(float) * 3);

					scratch_packet.writeDouble(global_time); // Write last_transform_client_time

					enqueueMessageToSend(*this->client_thread, scratch_packet);

					world_ob->from_local_physics_dirty = false;
				}
			}

			this->world_state->dirty_from_local_objects.clear();

			//============ Send any parcel updates needed ===========
			for(auto it = this->world_state->dirty_from_local_parcels.begin(); it != this->world_state->dirty_from_local_parcels.end(); ++it)
			{
				const Parcel* parcel= it->getPointer();
			
				// Enqueue ParcelFullUpdate
				MessageUtils::initPacket(scratch_packet, Protocol::ParcelFullUpdate);
				writeToNetworkStream(*parcel, scratch_packet, /*peer_protocol_version=*/Protocol::CyberspaceProtocolVersion);

				enqueueMessageToSend(*this->client_thread, scratch_packet);
			}

			this->world_state->dirty_from_local_parcels.clear();
		}


		time_since_update_packet_sent.reset();
	}

	{
		// Show a decibel level: http://msp.ucsd.edu/techniques/v0.08/book-html/node6.html
		// cur_level = 0.01 gives log_10(0.01 / 0.01) = 0.
		// cur_level = 1 gives log_10(1 / 0.01) = 2.  We want to map this to 1 so multiply by 0.5.
		const float a_0 = 1.0e-2f;
		const float d = 0.5f * std::log10(mic_read_status.cur_level / a_0);
		const float display_level = myClamp(d, 0.f, 1.f);
		gesture_ui.setCurrentMicLevel(mic_read_status.cur_level, display_level);
	}

	
	if(frame_num % 8 == 0)
		checkForAudioRangeChanges();

	if(terrain_system.nonNull())
		terrain_system->updateCampos(this->cam_controller.getPosition(), stack_allocator);

	if(terrain_decal_manager.nonNull())
		terrain_decal_manager->think((float)dt);
	if(particle_manager.nonNull())
		particle_manager->think((float)dt);

	if(opengl_engine.nonNull())
	{
		opengl_engine->getCurrentScene()->wind_strength = (float)(0.25 * (1.0 + std::sin(global_time * 0.1234) + std::sin(global_time * 0.23543)));
	}

	assert(arena_allocator.currentOffset() == 0);
	arena_allocator.clear();

	frame_num++;
}


void GUIClient::setGLWidgetContextAsCurrent()
{
	ui_interface->setGLWidgetContextAsCurrent();
}


Vec2i GUIClient::getGlWidgetPosInGlobalSpace()
{
	return ui_interface->getGlWidgetPosInGlobalSpace();
}


void GUIClient::webViewDataLinkHovered(const std::string& text)
{
	ui_interface->webViewDataLinkHovered(text);
}


void GUIClient::updateParcelGraphics()
{
	// Update parcel graphics and physics models that have been marked as from-server-dirty based on incoming network messages from server.
	try
	{
		PERFORMANCEAPI_INSTRUMENT("parcel graphics");
		ZoneScopedN("parcel graphics"); // Tracy profiler

		Lock lock(this->world_state->mutex);

		for(auto it = this->world_state->dirty_from_remote_parcels.begin(); it != this->world_state->dirty_from_remote_parcels.end(); ++it)
		{
			Parcel* parcel = it->getPointer();
			if(parcel->from_remote_dirty)
			{
				if(parcel->state == Parcel::State_Dead)
				{
					print("Removing Parcel.");
					
					// Remove any OpenGL object for it
					if(parcel->opengl_engine_ob.nonNull())
						opengl_engine->removeObject(parcel->opengl_engine_ob);

					// Remove physics object
					if(parcel->physics_object.nonNull())
					{
						physics_world->removeObject(parcel->physics_object);
						parcel->physics_object = NULL;
					}

					this->world_state->parcels.erase(parcel->id);
				}
				else
				{
					const Vec4f aabb_min((float)parcel->aabb_min.x, (float)parcel->aabb_min.y, (float)parcel->aabb_min.z, 1.0f);
					const Vec4f aabb_max((float)parcel->aabb_max.x, (float)parcel->aabb_max.y, (float)parcel->aabb_max.z, 1.0f);

					if(ui_interface->isShowParcelsEnabled())
					{
						if(parcel->opengl_engine_ob.isNull())
						{
							// Make OpenGL model for parcel:
							const bool write_perms = parcel->userHasWritePerms(this->logged_in_user_id);

							bool use_write_perms = write_perms;
							if(ui_interface->inScreenshotTakingMode()) // If we are in screenshot-taking mode, don't highlight writable parcels.
								use_write_perms = false;

							parcel->opengl_engine_ob = parcel->makeOpenGLObject(opengl_engine, use_write_perms);
							parcel->opengl_engine_ob->materials[0].shader_prog = this->parcel_shader_prog;
							parcel->opengl_engine_ob->materials[0].auto_assign_shader = false;
							opengl_engine->addObject(parcel->opengl_engine_ob);

							// Make physics object for parcel:
							assert(parcel->physics_object.isNull());
							parcel->physics_object = parcel->makePhysicsObject(this->unit_cube_shape);
							physics_world->addObject(parcel->physics_object);
						}
						else // else if opengl ob is not null:
						{
							// Update transform for object in OpenGL engine.  See OpenGLEngine::makeAABBObject() for transform details.
							//const Vec4f span = aabb_max - aabb_min;
							//parcel->opengl_engine_ob->ob_to_world_matrix.setColumn(0, Vec4f(span[0], 0, 0, 0));
							//parcel->opengl_engine_ob->ob_to_world_matrix.setColumn(1, Vec4f(0, span[1], 0, 0));
							//parcel->opengl_engine_ob->ob_to_world_matrix.setColumn(2, Vec4f(0, 0, span[2], 0));
							//parcel->opengl_engine_ob->ob_to_world_matrix.setColumn(3, aabb_min); // set origin
							//opengl_engine->updateObjectTransformData(*parcel->opengl_engine_ob);
							//
							//// Update in physics engine
							//parcel->physics_object->ob_to_world = parcel->opengl_engine_ob->ob_to_world_matrix;
							//physics_world->updateObjectTransformData(*parcel->physics_object);
						}
					}

					// If we want to move to this parcel based on the URL entered:
					if(this->url_parcel_uid == (int)parcel->id.value())
					{
						cam_controller.setFirstAndThirdPersonPositions(parcel->getVisitPosition());
						player_physics.setEyePosition(parcel->getVisitPosition());
						this->url_parcel_uid = -1;

						showInfoNotification("Jumped to parcel " + parcel->id.toString());
					}


					parcel->from_remote_dirty = false;
				}
			} // end if(parcel->from_remote_dirty)
		}

		this->world_state->dirty_from_remote_parcels.clear();
	}
	catch(glare::Exception& e)
	{
		print("Error while updating parcel graphics: " + e.what());
	}
}


void GUIClient::assignLODChunkSubMeshPlaceholderToOb(const LODChunk* chunk, WorldObject* const ob)
{
	assert(chunk->graphics_ob);
	if(!ob->opengl_engine_ob && !BitUtils::isBitSet(ob->flags, WorldObject::EXCLUDE_FROM_LOD_CHUNK_MESH))
	{
		// We will use part of the chunk geometry as the placeholder graphics for this object, while it is loading.
		// Use the sub-range of the indices from the LOD chunk geometry that correspond to this object.
		const bool ob_batch0_nonempty = ob->chunk_batch0_end > ob->chunk_batch0_start;
		const bool ob_batch1_nonempty = ob->chunk_batch1_end > ob->chunk_batch1_start;
		if(ob_batch0_nonempty || ob_batch1_nonempty) // If object has a chunk sub-range set:
		{
			const uint32 index_type_size_B = chunk->graphics_ob->mesh_data->getIndexTypeSize();

			runtimeCheck(chunk->graphics_ob->mesh_data->batches.size() >= 1);
			const size_t chunk_batch_0_start_index = chunk->graphics_ob->mesh_data->batches[0].prim_start_offset_B / index_type_size_B;
			const size_t chunk_batch_0_end_index   = chunk_batch_0_start_index + chunk->graphics_ob->mesh_data->batches[0].num_indices;

			const size_t chunk_batch_1_start_index = (chunk->graphics_ob->mesh_data->batches.size() >= 2) ? (chunk->graphics_ob->mesh_data->batches[1].prim_start_offset_B / index_type_size_B) : 0;
			const size_t chunk_batch_1_end_index   = (chunk->graphics_ob->mesh_data->batches.size() >= 2) ? (chunk_batch_1_start_index + chunk->graphics_ob->mesh_data->batches[1].num_indices) : 0;

			// If the object sub-chunk vertex indices ranges are valid (e.g. in bounds of original chunk mesh): (Note that an empty range is considered valid)
			const bool batch0_valid = !ob_batch0_nonempty || ((ob->chunk_batch0_start >= chunk_batch_0_start_index) && (ob->chunk_batch0_end <= chunk_batch_0_end_index));
			const bool batch1_valid = !ob_batch1_nonempty || ((ob->chunk_batch1_start >= chunk_batch_1_start_index) && (ob->chunk_batch1_end <= chunk_batch_1_end_index));

			if(batch0_valid && batch1_valid)
			{
				ob->opengl_engine_ob = opengl_engine->allocateObject();
				ob->opengl_engine_ob->mesh_data = chunk->graphics_ob->mesh_data; // Share the chunk's mesh data
				ob->opengl_engine_ob->ob_to_world_matrix = Matrix4f::identity();
						
				ob->opengl_engine_ob->materials = chunk->graphics_ob->materials;

				int new_num_batches = 0;
				if(ob->chunk_batch0_end > ob->chunk_batch0_start) // If batch 0 range is non-empty:
					new_num_batches++;
				if(ob->chunk_batch1_end > ob->chunk_batch1_start) // If batch 1 range is non-empty:
					new_num_batches++;
				assert(new_num_batches > 0);

				ob->opengl_engine_ob->use_batches.resizeNoCopy(new_num_batches);

				if(ob_batch0_nonempty) // If batch 0 range is non-empty:
				{
					ob->opengl_engine_ob->use_batches[0].material_index = 0;
					ob->opengl_engine_ob->use_batches[0].prim_start_offset_B = ob->chunk_batch0_start * index_type_size_B;
					ob->opengl_engine_ob->use_batches[0].num_indices         = ob->chunk_batch0_end - ob->chunk_batch0_start;
				}

				if(ob_batch1_nonempty) // If batch 1 range is non-empty:
				{
					ob->opengl_engine_ob->use_batches.back().material_index = 1;
					ob->opengl_engine_ob->use_batches.back().prim_start_offset_B = ob->chunk_batch1_start * index_type_size_B;
					ob->opengl_engine_ob->use_batches.back().num_indices         = ob->chunk_batch1_end - ob->chunk_batch1_start;
				}

				opengl_engine->addObject(ob->opengl_engine_ob);

				ob->using_placeholder_model = true;
			}
			else
			{
				conPrint("ERROR: invalid chunk sub-range");
			}
		}
	}
}


static inline Vec4i chunkCoordsVec4iForObCentroid(const Vec4f& centroid)
{
	return floorToVec4i(centroid * Vec4f(recip_chunk_w));
}


void GUIClient::updateLODChunkGraphics()
{
	const Vec4f campos = this->cam_controller.getPosition().toVec4fPoint();

	Lock lock(this->world_state->mutex);
	
	// Set chunk visibility based on distance from camera
	for(auto it = world_state->lod_chunks.begin(); it != world_state->lod_chunks.end(); ++it)
	{
		LODChunk* chunk = it->second.ptr();
		if(chunk->graphics_ob)
		{
			const bool should_show = shouldDisplayLODChunk(chunk->coords, campos);
			if(!should_show)
			{
				// Hide the chunk graphics object
				if(chunk->graphics_ob_in_engine)
				{
					opengl_engine->removeObject(chunk->graphics_ob);

					if(chunk->diagnostics_gl_ob)
						opengl_engine->removeObject(chunk->diagnostics_gl_ob);

					chunk->graphics_ob_in_engine = false;

					// Iterate over all objects, if they are in the chunk we just hid, then assign chunk placeholder sub-meshes to them.
					{
						//Timer timer;
						const Vec4i chunk_coords = Vec4i(chunk->coords.x, chunk->coords.y, chunk->coords.z, 0);

						glare::FastIterMapValueInfo<UID, WorldObjectRef>* const objects_data = this->world_state->objects.vector.data();
						const size_t objects_size                                            = this->world_state->objects.vector.size();

						for(size_t i = 0; i<objects_size; ++i)
						{
							if(i + 16 < objects_size)
								_mm_prefetch((const char*)(&objects_data[i + 16].value->centroid_ws), _MM_HINT_T0);

							WorldObject* const ob = objects_data[i].value.ptr();

							// Get the chunk this object is in, if any
							const Vec4f centroid = ob->getCentroidWS();
							const Vec4i ob_chunk_coords = chunkCoordsVec4iForObCentroid(centroid);
							if(ob_chunk_coords == chunk_coords)
								assignLODChunkSubMeshPlaceholderToOb(chunk, ob);
						}
						//conPrint("updateLODChunkGraphics(): assigning chunk placeholders took " + timer.elapsedStringMS());
					}
				}
			}
			else
			{
				// Show the chunk graphics object
				if(!chunk->graphics_ob_in_engine)
				{
					opengl_engine->addObject(chunk->graphics_ob);

					if(chunk->diagnostics_gl_ob)
						opengl_engine->addObject(chunk->diagnostics_gl_ob);

					chunk->graphics_ob_in_engine = true;
				}
			}
		}
	}



	// Update LOD chunk graphics that have been marked as from-server-dirty based on incoming network messages from server.
	try
	{
		ZoneScopedN("LODChunk graphics"); // Tracy profiler

		for(auto it = this->world_state->dirty_from_remote_lod_chunks.begin(); it != this->world_state->dirty_from_remote_lod_chunks.end(); ++it)
		{
			LODChunk* chunk = it->getPointer();

			const Vec4f centroid_ws((chunk->coords.x + 0.5f) * chunk_w, (chunk->coords.y + 0.5f) * chunk_w, 0.f, 1.f);

			URLString use_mesh_url;
			if(!chunk->getMeshURL().empty())
				use_mesh_url = chunk->computeMeshURL(this->server_has_optimised_meshes, this->server_opt_mesh_version);

			if(!use_mesh_url.empty())
			{
				DownloadingResourceInfo info;
				info.pos = Vec3d(centroid_ws);
				info.size_factor = LoadItemQueueItem::sizeFactorForAABBWS(chunk_w, /*importance_factor=*/1.f);
				info.build_physics_ob = false;
				info.used_by_other = true;
				startDownloadingResource(use_mesh_url, centroid_ws, chunk_w, info);

				this->loading_mesh_URL_to_chunk_coords_map[use_mesh_url] = chunk->coords; // Loading will begin when mesh is downloaded, so set this now.
			}

			if(!chunk->combined_array_texture_url.empty())
			{
				DownloadingResourceInfo info;
				info.pos = Vec3d(centroid_ws);
				info.size_factor = LoadItemQueueItem::sizeFactorForAABBWS(chunk_w, /*importance_factor=*/1.f);
				info.used_by_other = true;
				startDownloadingResource(chunk->combined_array_texture_url, centroid_ws, chunk_w, info);
			}

			//----------------------------- Start loading model and texture, if the resource is already present on disk -----------------------------
			if(!use_mesh_url.empty())
			{
				ResourceRef resource = this->resource_manager->getExistingResourceForURL(use_mesh_url);
				if(resource && (resource->getState() == Resource::State_Present))
				{
					const std::string path = resource_manager->getLocalAbsPathForResource(*resource);

					Reference<LoadModelTask> load_model_task = new LoadModelTask();
					
					load_model_task->resource = resource;
					load_model_task->lod_model_url = use_mesh_url;
					load_model_task->model_lod_level = 0;
					load_model_task->opengl_engine = this->opengl_engine;
					load_model_task->result_msg_queue = &this->msg_queue;
					load_model_task->resource_manager = resource_manager;
					load_model_task->build_physics_ob = false;
					load_model_task->build_dynamic_physics_ob = false;
					load_model_task->worker_allocator = worker_allocator;
					load_model_task->upload_thread = opengl_upload_thread;

					load_item_queue.enqueueItem(use_mesh_url, centroid_ws, chunk_w, 
						load_model_task, 
						/*max_dist_for_ob_lod_level=*/std::numeric_limits<float>::max(), /*importance_factor=*/1.f);
				}
			}


			if(!chunk->combined_array_texture_url.empty())
			{
				this->loading_texture_URL_to_chunk_coords_map[chunk->combined_array_texture_url] = chunk->coords;

				ResourceRef resource = this->resource_manager->getOrCreateResourceForURL(chunk->combined_array_texture_url);
				
				const OpenGLTextureKey path = OpenGLTextureKey(resource_manager->getLocalAbsPathForResource(*resource));
				chunk->combined_array_texture_path = path;

				if(resource->getState() == Resource::State_Present)
				{
					TextureParams tex_params;
					load_item_queue.enqueueItem(chunk->combined_array_texture_url, centroid_ws, chunk_w, 
						new LoadTextureTask(opengl_engine, resource_manager, &this->msg_queue, path, resource, tex_params, /*is terrain map=*/false, worker_allocator, texture_loaded_msg_allocator, opengl_upload_thread), 
						/*max_dist_for_ob_lod_level=*/std::numeric_limits<float>::max(), /*importance_factor=*/1.f);
				}
			}

			if(LOD_CHUNK_SUPPORT)
				this->server_using_lod_chunks = true;
		}

		this->world_state->dirty_from_remote_lod_chunks.clear();
	}
	catch(glare::Exception& e)
	{
		print("Error while updating LODChunk graphics: " + e.what());
	}
}


void GUIClient::handleLODChunkMeshLoaded(const URLString& mesh_URL, Reference<MeshData> mesh_data, WorldStateLock& lock)
{
	auto loading_res = loading_mesh_URL_to_chunk_coords_map.find(mesh_URL);
	if(loading_res != loading_mesh_URL_to_chunk_coords_map.end())
	{
		const Vec3i chunk_coords = loading_res->second;

		// Look up chunk
		auto chunk_res = world_state->lod_chunks.find(chunk_coords);
		if(chunk_res != world_state->lod_chunks.end())
		{
			LODChunk* chunk = chunk_res->second.ptr();
	
			assert(mesh_URL == chunk->computeMeshURL(this->server_has_optimised_meshes, this->server_opt_mesh_version));

			try
			{
				if(!chunk->graphics_ob)
				{
					mesh_data->meshDataBecameUsed();
					chunk->mesh_manager_data = mesh_data;// Hang on to a reference to the mesh data, so when object-uses of it are removed, it can be removed from the MeshManager with meshDataBecameUnused().

					chunk->graphics_ob = opengl_engine->allocateObject();
					chunk->graphics_ob->mesh_data = mesh_data->gl_meshdata;
					chunk->graphics_ob->ob_to_world_matrix = Matrix4f::identity();
		
					chunk->graphics_ob->materials.resize(myMax<size_t>(1, mesh_data->gl_meshdata->num_materials_referenced));
					chunk->graphics_ob->materials[0].combined = true;

					chunk->graphics_ob->materials[0].combined_array_texture = this->default_array_tex;
					if(!chunk->combined_array_texture_path.empty())
					{
						OpenGLTextureRef combined_tex = opengl_engine->getTextureIfLoaded(chunk->combined_array_texture_path);
						if(combined_tex)
						{
							if(combined_tex->getTextureTarget() != GL_TEXTURE_2D_ARRAY)
								conPrint("Error, loaded chunk combined texture is not a GL_TEXTURE_2D_ARRAY (path: " + std::string(chunk->combined_array_texture_path) + ")");

							chunk->graphics_ob->materials[0].combined_array_texture = combined_tex;
						}
					}


				
					//--------------------------- Get decompressed mat info ---------------------------
					const uint64 decompressed_size = ZSTD_getFrameContentSize(chunk->compressed_mat_info.data(), chunk->compressed_mat_info.size());
					if(decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN || decompressed_size == ZSTD_CONTENTSIZE_ERROR)
						throw glare::Exception(std::string("ZSTD_getFrameContentSize failed: ") + ZSTD_getErrorName(decompressed_size));

					// sanity check size
					if(decompressed_size > 1000000)
						throw glare::Exception("Invalid decompressed mat info size.");

					js::Vector<uint8> decompressed(decompressed_size);

					const size_t res = ZSTD_decompress(/*dest=*/decompressed.begin(), /*dest capacity=*/decompressed.size(), /*src=*/chunk->compressed_mat_info.data(), /*compressed size=*/chunk->compressed_mat_info.size());
					if(ZSTD_isError(res))
						throw glare::Exception(std::string("Decompression of index buffer failed: ") + ZSTD_getErrorName(res));
					//-------------------------------------------------------------------------------

					const size_t num_floats = decompressed.size() / sizeof(float);
					checkProperty(num_floats > 0, "invalid mat_info num floats");
					checkProperty((num_floats % 12) == 0, "invalid mat_info num floats");

					ImageMapFloatRef map = new ImageMapFloat(128, /*height=*/Maths::roundedUpDivide<size_t>(num_floats, 128), /*N=*/1, /*allocator=*/NULL);

					std::memcpy(map->getData(), decompressed.data(), decompressed.dataSizeBytes());

					TextureParams mat_info_tex_params;
					mat_info_tex_params.allow_compression = false;
					mat_info_tex_params.use_sRGB = false;
					mat_info_tex_params.filtering = OpenGLTexture::Filtering::Filtering_Nearest;
					mat_info_tex_params.use_mipmaps = false;

					// Hash data to get a unique texture key
					const uint64 hash = XXH64(decompressed.data(), decompressed.size(), /*seed=*/1);

					const OpenGLTextureKey use_mat_info_path = OpenGLTextureKey("mat_info_") + OpenGLTextureKey(toString(hash));
					chunk->graphics_ob->materials[0].backface_albedo_texture = opengl_engine->getOrLoadOpenGLTextureForMap2D(use_mat_info_path, *map, mat_info_tex_params);
					if(opengl_engine->runningInRenderDoc())
						chunk->graphics_ob->materials[0].backface_albedo_texture->setDebugName(std::string(use_mat_info_path));

					if(chunk->graphics_ob->materials.size() >= 2)
					{
						chunk->graphics_ob->materials[1].combined = true;
						chunk->graphics_ob->materials[1].transparent = true;
					}

					const Vec4f campos = this->cam_controller.getPosition().toVec4fPoint();
					const bool should_show = shouldDisplayLODChunk(chunk->coords, campos);
					if(should_show)
					{
						opengl_engine->addObject(chunk->graphics_ob);
						chunk->graphics_ob_in_engine = true;
					}
				}
			}
			catch(glare::Exception& e)
			{
				conPrint("Error while loading LOD chunk graphics: " + e.what());
			}
		}
	}
}


// Update avatar graphics (transform, animation pose etc.) for all avatars.
void GUIClient::updateAvatarGraphics(double cur_time, double dt, const Vec3d& our_cam_angles, bool our_move_impulse_zero)
{
#if EMSCRIPTEN
	// Wait until we have done the async load of extracted_anim_data.bin before we start loading avatars.
	// This is not optimal (could download models/textures while we are loading extracted_anim_data.bin), but that would be complicated to code 
	// so this will do for now.
	if(!extracted_anim_data_loaded)
	{
		// conPrint("GUIClient::updateAvatarGraphics(): waiting until extracted_anim_data_loaded...");
		return;
	}
#endif

	// Update avatar graphics
	temp_av_positions.clear();
	if(world_state.nonNull())
	{
		PERFORMANCEAPI_INSTRUMENT("avatar graphics");
		ZoneScopedN("avatar graphics"); // Tracy profiler

		try
		{
			WorldStateLock lock(this->world_state->mutex);

			for(auto it = this->world_state->avatars.begin(); it != this->world_state->avatars.end();)
			{
				Avatar* avatar = it->second.getPointer();
				const bool our_avatar = avatar->isOurAvatar();

				if(avatar->state == Avatar::State_Dead)
				{
					print("Removing avatar.");

					ui_interface->appendChatMessage("<i><span style=\"color:rgb(" + 
						toString(avatar->name_colour.r * 255) + ", " + toString(avatar->name_colour.g * 255) + ", " + toString(avatar->name_colour.b * 255) + ")\">" + 
						web::Escaping::HTMLEscape(avatar->name) + "</span> left.</i>");

					chat_ui.appendMessage(avatar->name, avatar->name_colour, " left.");

					// Remove any OpenGL object for it
					avatar->graphics.destroy(*opengl_engine);

					// Remove nametag OpenGL object
					if(avatar->nametag_gl_ob.nonNull())
						opengl_engine->removeObject(avatar->nametag_gl_ob);
					avatar->nametag_gl_ob = NULL;
					if(avatar->speaker_gl_ob.nonNull())
						opengl_engine->removeObject(avatar->speaker_gl_ob);
					avatar->speaker_gl_ob = NULL;

					hud_ui.removeMarkerForAvatar(avatar); // Remove any marker for the avatar from the HUD
					if(minimap)
						minimap->removeMarkerForAvatar(avatar); // Remove any marker for the avatar from the minimap

					// Remove avatar from avatar map
					auto old_avatar_iterator = it;
					it++;
					this->world_state->avatars.erase(old_avatar_iterator);

					ui_interface->updateOnlineUsersList();

					world_state->avatars_changed = 1;
				}
				else
				{
					bool reload_opengl_model = false; // load or reload model?

					if(avatar->state == Avatar::State_JustCreated)
					{
						enableMaterialisationEffectOnAvatar(*avatar); // Enable materialisation effect before we call loadModelForAvatar() below.

						//// Add audio source for voice chat
						//if(!our_avatar)
						//{
						//	avatar->audio_source = new glare::AudioSource();
						//	avatar->audio_source->type = glare::AudioSource::SourceType_Streaming;
						//	avatar->audio_source->pos = avatar->pos.toVec4fPoint();

						//	audio_engine.addSource(avatar->audio_source);
						//}
					}

					if(avatar->other_dirty)
					{
						reload_opengl_model = true;

						ui_interface->updateOnlineUsersList();
					}

					if((cam_controller.thirdPersonEnabled() || !our_avatar) && reload_opengl_model) // Don't load graphics for our avatar unless we are in third-person cam view mode
					{
						print("(Re)Loading avatar model. model URL: " + toStdString(avatar->avatar_settings.model_url) + ", Avatar name: " + avatar->name);

						// Remove any existing model and nametag
						avatar->graphics.destroy(*opengl_engine);
						
						if(avatar->nametag_gl_ob.nonNull()) // Remove nametag ob
							opengl_engine->removeObject(avatar->nametag_gl_ob);
						avatar->nametag_gl_ob = NULL;
						if(avatar->speaker_gl_ob.nonNull())
							opengl_engine->removeObject(avatar->speaker_gl_ob);
						avatar->speaker_gl_ob = NULL;

						hud_ui.removeMarkerForAvatar(avatar); // Remove any marker for the avatar from the HUD
						if(minimap)
							minimap->removeMarkerForAvatar(avatar); // Remove any marker for the avatar from the minimap

						print("Adding Avatar to OpenGL Engine, UID " + toString(avatar->uid.value()));

						loadModelForAvatar(avatar);

						if(!our_avatar)
						{
							// Add nametag object for avatar
							avatar->nametag_gl_ob = makeNameTagGLObject(avatar->name);

							// Set transform to be above avatar.  This transform will be updated later.
							avatar->nametag_gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(avatar->pos.toVec4fVector());

							opengl_engine->addObject(avatar->nametag_gl_ob); // Add to 3d engine

							// Play entry teleport sound
							audio_engine.playOneShotSound(resources_dir_path + "/sounds/462089__newagesoup__ethereal-woosh_normalised_mono.mp3", avatar->pos.toVec4fVector());
						}
					} // End if reload_opengl_model


					// Update transform if we have an avatar or placeholder OpenGL model.
					Vec3d pos;
					Vec3f rotation;
					avatar->getInterpolatedTransform(cur_time, pos, rotation);

					bool use_xyplane_speed_rel_ground_override = false;
					float xyplane_speed_rel_ground_override = 0;

					// Override some variables for our avatar:
					if(our_avatar)
					{
						pos = cam_controller.getFirstPersonPosition();
						rotation = Vec3f(0, (float)our_cam_angles.y, (float)our_cam_angles.x);

						use_xyplane_speed_rel_ground_override = true;
						xyplane_speed_rel_ground_override = player_physics.getLastXYPlaneVelRelativeToGround().length();

						avatar->anim_state = 
							(player_physics.onGroundRecently() ? 0 : AvatarGraphics::ANIM_STATE_IN_AIR) | 
							(player_physics.flyModeEnabled() ? AvatarGraphics::ANIM_STATE_FLYING : 0) | 
							(our_move_impulse_zero ? AvatarGraphics::ANIM_STATE_MOVE_IMPULSE_ZERO : 0);
					}

					{
						// Seat to world = object to world * seat to object
						PoseConstraint pose_constraint;
						pose_constraint.sitting = false;
						if(our_avatar)
						{
							if(vehicle_controller_inside.nonNull())
							{
								if(cur_seat_index < vehicle_controller_inside->getSettings().seat_settings.size())
								{
									// If we are driving the vehicle, use local physics transform, otherwise use smoothed network transformation, so that the avatar position is consistent with the vehicle model.
									const bool use_smoothed_network_transform = cur_seat_index != 0;
									pose_constraint.sitting = true;
									pose_constraint.seat_to_world							= vehicle_controller_inside->getSeatToWorldTransform(*this->physics_world, cur_seat_index, use_smoothed_network_transform);
									pose_constraint.model_to_y_forwards_rot_1				= vehicle_controller_inside->getSettings().model_to_y_forwards_rot_1;
									pose_constraint.model_to_y_forwards_rot_2				= vehicle_controller_inside->getSettings().model_to_y_forwards_rot_2;
									pose_constraint.upper_body_rot_angle					= vehicle_controller_inside->getSettings().seat_settings[cur_seat_index].upper_body_rot_angle;
									pose_constraint.upper_leg_rot_angle						= vehicle_controller_inside->getSettings().seat_settings[cur_seat_index].upper_leg_rot_angle;
									pose_constraint.upper_leg_rot_around_thigh_bone_angle	= vehicle_controller_inside->getSettings().seat_settings[cur_seat_index].upper_leg_rot_around_thigh_bone_angle;
									pose_constraint.upper_leg_apart_angle					= vehicle_controller_inside->getSettings().seat_settings[cur_seat_index].upper_leg_apart_angle;
									pose_constraint.lower_leg_rot_angle						= vehicle_controller_inside->getSettings().seat_settings[cur_seat_index].lower_leg_rot_angle;
									pose_constraint.lower_leg_apart_angle					= vehicle_controller_inside->getSettings().seat_settings[cur_seat_index].lower_leg_apart_angle;
									pose_constraint.rotate_foot_out_angle					= vehicle_controller_inside->getSettings().seat_settings[cur_seat_index].rotate_foot_out_angle;
									pose_constraint.arm_down_angle							= vehicle_controller_inside->getSettings().seat_settings[cur_seat_index].arm_down_angle;
									pose_constraint.arm_out_angle							= vehicle_controller_inside->getSettings().seat_settings[cur_seat_index].arm_out_angle;
								}
								else
								{
									assert(0); // Invalid seat index
								}
							}
						}
						else
						{
							if(avatar->pending_vehicle_transition == Avatar::EnterVehicle)
							{
								assert(avatar->entered_vehicle.nonNull());
								if(avatar->entered_vehicle.nonNull()) // If the other avatar is, or should be in a vehicle:
								{
									// If the physics object, opengl object and script are all loaded:
									if(avatar->entered_vehicle->physics_object.nonNull() && avatar->entered_vehicle->opengl_engine_ob.nonNull() && avatar->entered_vehicle->vehicle_script.nonNull())
									{
										const auto controller_res = vehicle_controllers.find(avatar->entered_vehicle.ptr());
										if(controller_res == vehicle_controllers.end()) // if there is no vehicle controller for this object :
										{
											// Create Vehicle controller
											Reference<VehiclePhysics> new_controller = createVehicleControllerForScript(avatar->entered_vehicle.ptr());
											vehicle_controllers.insert(std::make_pair(avatar->entered_vehicle.ptr(), new_controller));

											new_controller->userEnteredVehicle(avatar->vehicle_seat_index);

											conPrint("Avatar entered vehicle with new physics controller in seat " + toString(avatar->vehicle_seat_index));
										}
										else // Else if there is already a vehicle controller for the object:
										{
											conPrint("Avatar entered vehicle with existing physics controller in seat " + toString(avatar->vehicle_seat_index));

											VehiclePhysics* controller = controller_res->second.ptr();
											controller->userEnteredVehicle(avatar->vehicle_seat_index);
										}

										// Execute event handlers in any scripts that are listening for the onUserEnteredVehicle event from this object.
										if(avatar->entered_vehicle->event_handlers)
											avatar->entered_vehicle->event_handlers->executeOnUserEnteredVehicleHandlers(avatar->uid, avatar->entered_vehicle->uid, lock);

										avatar->pending_vehicle_transition = Avatar::VehicleNoChange;
									}
								}
							}


							if(avatar->entered_vehicle.nonNull()) // If the other avatar is, or should be in a vehicle:
							{
								const auto controller_res = vehicle_controllers.find(avatar->entered_vehicle.ptr()); // Find a vehicle controller for the avatar 'entered_vehicle' object.
								if(controller_res != vehicle_controllers.end())
								{
									VehiclePhysics* controller = controller_res->second.ptr();

									if(avatar->vehicle_seat_index == 0) // If avatar is driving vehicle:
										controller->last_physics_input_bitflags = avatar->last_physics_input_bitflags; // Pass last physics input flags to the controller.

									if(avatar->vehicle_seat_index < controller->getSettings().seat_settings.size())
									{
										pose_constraint.sitting = true;
										pose_constraint.seat_to_world							= controller->getSeatToWorldTransform(*this->physics_world, avatar->vehicle_seat_index, /*use_smoothed_network_transform=*/true);
										pose_constraint.model_to_y_forwards_rot_1				= controller->getSettings().model_to_y_forwards_rot_1;
										pose_constraint.model_to_y_forwards_rot_2				= controller->getSettings().model_to_y_forwards_rot_2;
										pose_constraint.upper_body_rot_angle					= controller->getSettings().seat_settings[avatar->vehicle_seat_index].upper_body_rot_angle;
										pose_constraint.upper_leg_rot_angle						= controller->getSettings().seat_settings[avatar->vehicle_seat_index].upper_leg_rot_angle;
										pose_constraint.upper_leg_rot_around_thigh_bone_angle	= controller->getSettings().seat_settings[avatar->vehicle_seat_index].upper_leg_rot_around_thigh_bone_angle;
										pose_constraint.upper_leg_apart_angle					= controller->getSettings().seat_settings[avatar->vehicle_seat_index].upper_leg_apart_angle;
										pose_constraint.lower_leg_rot_angle						= controller->getSettings().seat_settings[avatar->vehicle_seat_index].lower_leg_rot_angle;
										pose_constraint.lower_leg_apart_angle					= controller->getSettings().seat_settings[avatar->vehicle_seat_index].lower_leg_apart_angle;
										pose_constraint.rotate_foot_out_angle					= controller->getSettings().seat_settings[avatar->vehicle_seat_index].rotate_foot_out_angle;
										pose_constraint.arm_down_angle							= controller->getSettings().seat_settings[avatar->vehicle_seat_index].arm_down_angle;
										pose_constraint.arm_out_angle							= controller->getSettings().seat_settings[avatar->vehicle_seat_index].arm_out_angle;
									}
									else
									{
										assert(0); // Seat index was invalid.
									}
								}


								if(avatar->pending_vehicle_transition == Avatar::ExitVehicle)
								{
									conPrint("Avatar exited vehicle from seat " + toString(avatar->vehicle_seat_index));
									if(controller_res != vehicle_controllers.end())
									{
										VehiclePhysics* controller = controller_res->second.ptr();
										controller->userExitedVehicle(avatar->vehicle_seat_index);

										// Execute event handlers in any scripts that are listening for the onUserExitedVehicle event from this object.
										WorldObject* vehicle_ob = controller->getControlledObject();
										if(vehicle_ob->event_handlers)
											vehicle_ob->event_handlers->executeOnUserExitedVehicleHandlers(avatar->uid, vehicle_ob->uid, lock);
									}
									avatar->entered_vehicle = NULL;
									avatar->pending_vehicle_transition = Avatar::VehicleNoChange;
								}
							}
						}
						 
						AnimEvents anim_events;
						avatar->graphics.setOverallTransform(*opengl_engine, pos, rotation, use_xyplane_speed_rel_ground_override, xyplane_speed_rel_ground_override,
							avatar->avatar_settings.pre_ob_to_world_matrix, avatar->anim_state, cur_time, dt, pose_constraint, anim_events);
						
						if(!BitUtils::isBitSet(avatar->anim_state, AvatarGraphics::ANIM_STATE_IN_AIR) && anim_events.footstrike && !pose_constraint.sitting) // If avatar is on ground, and the anim played a footstrike
						{
							//const int rnd_src_i = rng.nextUInt((uint32)footstep_sources.size());
							//footstep_sources[rnd_src_i]->cur_read_i = 0;
							//audio_engine.setSourcePosition(footstep_sources[rnd_src_i], anim_events.footstrike_pos.toVec4fPoint());
							const int rnd_src_i = rng.nextUInt(4);
							audio_engine.playOneShotSound(resources_dir_path + "/sounds/footstep_mono" + toString(rnd_src_i) + ".wav", anim_events.footstrike_pos.toVec4fPoint());
						}

						for(int i=0; i<anim_events.num_blobs; ++i)
							temp_av_positions.push_back(anim_events.blob_sphere_positions[i]);
					}

					
					// If the avatar is in a vehicle, use the vehicle transform, which can be somewhat different from the avatar location due to different interpolation methods.
					Vec4f use_nametag_pos = pos.toVec4fPoint(); // Also used for red dot in HeadUpDisplay
					if(avatar->entered_vehicle)
					{
						const auto controller_res = vehicle_controllers.find(avatar->entered_vehicle.ptr()); // Find a vehicle controller for the avatar 'entered_vehicle' object.
						if(controller_res != vehicle_controllers.end())
						{
							VehiclePhysics* controller = controller_res->second.ptr();
							const Matrix4f seat_to_world = controller->getSeatToWorldTransform(*this->physics_world, avatar->vehicle_seat_index, /*use_smoothed_network_transform=*/true);

							use_nametag_pos = seat_to_world * Vec4f(0,0,1.0f,1);
						}
					}

					// Update nametag transform also
					if(avatar->nametag_gl_ob.nonNull())
					{
						// We want to rotate the nametag towards the camera.
						Vec4f to_cam = normalise(use_nametag_pos - this->cam_controller.getPosition().toVec4fPoint());
						if(!isFinite(to_cam[0]))
							to_cam = Vec4f(1, 0, 0, 0); // Handle case where to_cam was zero.

						const Vec4f axis_k = Vec4f(0, 0, 1, 0);
						if(std::fabs(dot(to_cam, axis_k)) > 0.999f) // Make vectors linearly independent.
							to_cam[0] += 0.1;

						const Vec4f axis_j = normalise(removeComponentInDir(to_cam, axis_k));
						const Vec4f axis_i = crossProduct(axis_j, axis_k);
						const Matrix4f rot_matrix(axis_i, axis_j, axis_k, Vec4f(0, 0, 0, 1));

						const float ws_height = 0.2f; // world space height of nametag in metres
						const float ws_width = ws_height * avatar->nametag_gl_ob->mesh_data->aabb_os.axisLength(0) / avatar->nametag_gl_ob->mesh_data->aabb_os.axisLength(2);

						const float total_w = ws_width + (avatar->speaker_gl_ob.nonNull() ? (0.05f + ws_height) : 0.f); // Width of nametag and speaker icon (and spacing between them).

						// If avatar is flying (e.g playing floating anim) move nametag up so it isn't blocked by the avatar head, which is higher in floating anim.
						const float flying_z_offset = ((avatar->anim_state & AvatarGraphics::ANIM_STATE_IN_AIR) != 0) ? 0.3f : 0.f;

						// Blend in new z offset, don't immediately jump to it.
						const float blend_speed = 0.1f;
						avatar->nametag_z_offset = avatar->nametag_z_offset * (1 - blend_speed) + flying_z_offset * blend_speed;

						// Rotate around z-axis, then translate to just above the avatar's head.
						avatar->nametag_gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(use_nametag_pos + Vec4f(0, 0, 0.45f + avatar->nametag_z_offset, 0)) *
							rot_matrix * Matrix4f::translationMatrix(-total_w/2, 0.f, 0.f) * Matrix4f::uniformScaleMatrix(ws_width);

						assert(isFinite(avatar->nametag_gl_ob->ob_to_world_matrix.e[0]));
						opengl_engine->updateObjectTransformData(*avatar->nametag_gl_ob); // Update transform in 3d engine

						// Set speaker icon transform and colour
						if(avatar->speaker_gl_ob.nonNull())
						{
							const float vol_padding_frac = 0.f;
							const float vol_h = ws_height * (1 - vol_padding_frac * 2);
							const float vol_padding = ws_height * vol_padding_frac;
							avatar->speaker_gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(use_nametag_pos + Vec4f(0, 0, 0.45f + avatar->nametag_z_offset, 0)) *
								rot_matrix * Matrix4f::translationMatrix(-total_w/2 + ws_width + 0.05f, 0.f, vol_padding) * Matrix4f::scaleMatrix(vol_h, 1, vol_h);

							opengl_engine->updateObjectTransformData(*avatar->speaker_gl_ob); // Update transform in 3d engine

							if(avatar->audio_source.nonNull())
							{
								const float a_0 = 1.0e-2f;
								const float d = 0.5f * std::log10(avatar->audio_source->smoothed_cur_level / a_0);
								const float display_level = myClamp(d, 0.f, 1.f);

								// Show a white/grey icon that changes to green when the user is speaking, and changes to red if the amplitude gets too close to 1.
								const Colour3f default_col = toLinearSRGB(Colour3f(0.8f));
								const Colour3f green       = toLinearSRGB(Colour3f(0, 54.5f/100, 8.6f/100));
								const Colour3f red         = toLinearSRGB(Colour3f(78.7f / 100, 0, 0));

								const Colour3f col = Maths::uncheckedLerp(
									Maths::uncheckedLerp(default_col, green, display_level),
									red,
									Maths::smoothStep(0.97f, 1.f, avatar->audio_source->smoothed_cur_level)
								);

								avatar->speaker_gl_ob->materials[0].albedo_linear_rgb = col;
								opengl_engine->objectMaterialsUpdated(*avatar->speaker_gl_ob);
							}
						}
					}

					// Make foam decal if object just entered water
					if(BitUtils::isBitSet(this->connected_world_settings.terrain_spec.flags, TerrainSpec::WATER_ENABLED_FLAG) &&
						(pos.z - PlayerPhysics::getEyeHeight()) < this->connected_world_settings.terrain_spec.water_z)
					{
						// Avatar is partially or completely in water

						const float foam_width = myClamp((float)avatar->graphics.getLastVel().length() * 0.1f, 0.5f, 3.f);

						if(!avatar->underwater) // If just entered water:
						{
							// Create a big 'splash' foam decal
							Vec4f foam_pos = pos.toVec4fPoint();
							foam_pos[2] = this->connected_world_settings.terrain_spec.water_z;

							terrain_decal_manager->addFoamDecal(foam_pos, foam_width, /*opacity=*/1.f, TerrainDecalManager::DecalType_ThickFoam);

							// Add splash particle(s)
							for(int i=0; i<10; ++i)
							{
								Particle particle;
								particle.pos = foam_pos;
								particle.area = 0.000001f;
								const float xy_spread = 1.f;
								const float splash_particle_speed = myClamp((float)avatar->graphics.getLastVel().length() * 0.1f, 1.f, 6.f);
								particle.vel = Vec4f(xy_spread * (-0.5f + rng.unitRandom()), xy_spread * (-0.5f + rng.unitRandom()), rng.unitRandom() * 2, 0) * splash_particle_speed;
								particle.colour = Colour3f(1.f);
								particle.particle_type = Particle::ParticleType_Foam;
								particle.theta = rng.unitRandom() * Maths::get2Pi<float>();
								particle.width = 0.5f;
								particle.dwidth_dt = 1.f;
								particle.die_when_hit_surface = true;
								particle_manager->addParticle(particle);
							}

							avatar->underwater = true;
						}

						if(pos.z + 0.1 > this->connected_world_settings.terrain_spec.water_z) // If avatar intersects the surface (approximately)
						{
							if(vehicle_controller_inside.isNull() && // If avatar is not inside a vehicle:
								(avatar->graphics.getLastVel().length() > 5)) // If avatar is roughly going above walking speed: walking speed is ~2.9 m/s, running ~14 m/s
							{
								if(avatar->last_foam_decal_creation_time + 0.02 < cur_time)
								{
									Vec4f foam_pos = pos.toVec4fPoint();
									foam_pos[2] = this->connected_world_settings.terrain_spec.water_z;

									terrain_decal_manager->addFoamDecal(foam_pos, 0.75f, /*opacity=*/0.4f, TerrainDecalManager::DecalType_ThickFoam);


									// Add splash particle(s)
									Particle particle;
									particle.pos = foam_pos;
									particle.area = 0.000001f;
									const float xy_spread = 1.f;
									particle.vel = Vec4f(xy_spread * (-0.5f + rng.unitRandom()), xy_spread * (-0.5f + rng.unitRandom()), rng.unitRandom() * 2, 0) * 2.f;
									particle.colour = Colour3f(0.7f);
									particle.particle_type = Particle::ParticleType_Foam;
									particle.theta = rng.unitRandom() * Maths::get2Pi<float>();
									particle.width = 0.5f;
									particle.dwidth_dt = 1.f;
									particle.die_when_hit_surface = true;
									particle_manager->addParticle(particle);


									avatar->last_foam_decal_creation_time = cur_time;
								}
							}
						}
					}
					else
					{
						if(avatar->underwater)
							avatar->underwater = false;
					}

					// Update avatar audio source position
					if(avatar->audio_source.nonNull())
					{
						avatar->audio_source->pos = avatar->pos.toVec4fPoint();
						audio_engine.sourcePositionUpdated(*avatar->audio_source);
					}

					// Update selected object beam for the avatar, if it has an object selected
					// TEMP: Disabled this code as it was messing with objects being edited.
					/*if(avatar->selected_object_uid.valid())
					{
						auto selected_it = world_state->objects.find(avatar->selected_object_uid);
						if(selected_it != world_state->objects.end())
						{
							WorldObject* their_selected_ob = selected_it->second.getPointer();
							Vec3d selected_pos;
							Vec3f axis;
							float angle;
							their_selected_ob->getInterpolatedTransform(cur_time, selected_pos, axis, angle);

							// Replace pos with the centre of the AABB (instead of the object space origin)
							if(their_selected_ob->opengl_engine_ob.nonNull())
							{
								their_selected_ob->opengl_engine_ob->ob_to_world_matrix = Matrix4f::translationMatrix((float)selected_pos.x, (float)selected_pos.y, (float)selected_pos.z) *
									Matrix4f::rotationMatrix(normalise(axis.toVec4fVector()), angle) *
									Matrix4f::scaleMatrix(their_selected_ob->scale.x, their_selected_ob->scale.y, their_selected_ob->scale.z);

								opengl_engine->updateObjectTransformData(*their_selected_ob->opengl_engine_ob);

								selected_pos = toVec3d(their_selected_ob->opengl_engine_ob->aabb_ws.centroid());
							}

							avatar->graphics.setSelectedObBeam(*opengl_engine, selected_pos);
						}
					}
					else
					{
						avatar->graphics.hideSelectedObBeam(*opengl_engine);
					}*/


					if(!our_avatar)
					{
						hud_ui.updateMarkerForAvatar(avatar, Vec3d(use_nametag_pos)); // Update marker on HUD
						if(minimap)
							minimap->updateMarkerForAvatar(avatar, Vec3d(use_nametag_pos)); // Update marker on minimap
					}


					avatar->other_dirty = false;
					avatar->transform_dirty = false;

					assert(avatar->state == Avatar::State_JustCreated || avatar->state == Avatar::State_Alive);
					if(avatar->state == Avatar::State_JustCreated)
					{
						avatar->state = Avatar::State_Alive;

						world_state->avatars_changed = 1;
					}

					++it;
				} // End if avatar state != dead.
			} // end for each avatar

			// Sort avatar positions based on distance from camera
			CloserToCamComparator comparator(cam_controller.getPosition().toVec4fPoint());
			std::sort(temp_av_positions.begin(), temp_av_positions.end(), comparator);

			const size_t use_num_av_positions = myMin((size_t)8, temp_av_positions.size());
			opengl_engine->getCurrentScene()->blob_shadow_locations.resize(use_num_av_positions);
			for(size_t i=0; i<use_num_av_positions; ++i)
				opengl_engine->getCurrentScene()->blob_shadow_locations[i] = temp_av_positions[i];

			opengl_engine->getCurrentScene()->grass_pusher_sphere_pos = cam_controller.getFirstPersonPosition().toVec4fPoint() + Vec4f(0, 0, -PlayerPhysics::getEyeHeight(), 0);
		}
		catch(glare::Exception& e)
		{
			print("Error while Updating avatar graphics: " + e.what());
		}
	}
}


void GUIClient::setThirdPersonCameraPosition(double dt)
{
	if(cam_controller.thirdPersonEnabled())
	{
		const bool selfie_mode = this->cam_controller.selfieModeEnabled();

		Vec4f head_pos;
		Vec4f left_eye_pos, right_eye_pos;
		head_pos = cam_controller.getFirstPersonPosition().toVec4fPoint(); // default
		if(world_state.nonNull())
		{
			Lock lock(this->world_state->mutex);
			for(auto it = this->world_state->avatars.begin(); it != this->world_state->avatars.end(); ++it)
			{
				const Avatar* avatar = it->second.getPointer();
				if(avatar->isOurAvatar())
				{

					head_pos = avatar->graphics.getLastHeadPosition();
					left_eye_pos = avatar->graphics.getLastLeftEyePosition();
					right_eye_pos = avatar->graphics.getLastRightEyePosition();
				}
			}
		}
		
		Vec4f use_target_pos;
		if(selfie_mode)
		{
			// Search avatars for our avatar, get its last head position, which will be our target position.
			// This differs from the cam controller first person position due to animations, like sitting animations, which move the head a lot.

			use_target_pos = head_pos;
		}
		else
		{
			const Vec4f offset = vehicle_controller_inside.nonNull() ? vehicle_controller_inside->getThirdPersonCamTargetTranslation() : Vec4f(0);
			use_target_pos = cam_controller.getFirstPersonPosition().toVec4fPoint() + offset;
		}
		
		Vec4f cam_back_dir;
		if(selfie_mode)
		{
			// Slowly blend towards use_target_pos as in selfie mode it comes from getLastHeadPosition() which can vary rapidly frame to frame.
			const float target_lerp_frac = myMin(0.2f, (float)dt * 20);
			cam_controller.current_third_person_target_pos = cam_controller.current_third_person_target_pos * (1 - target_lerp_frac) + Vec3d(use_target_pos) * target_lerp_frac;
		
			cam_back_dir = (cam_controller.getForwardsVec() * -cam_controller.getThirdPersonCamDist()).toVec4fVector();
		}
		else
		{
			cam_controller.current_third_person_target_pos = Vec3d(use_target_pos);
		
			cam_back_dir = (cam_controller.getForwardsVec() * -cam_controller.getThirdPersonCamDist() + cam_controller.getUpVec() * 0.2).toVec4fVector();
		}
		
		// Don't start tracing the ray back immediately or we may hit the vehicle.
		const float initial_ignore_dist = vehicle_controller_inside.nonNull() ? myMin(cam_controller.getThirdPersonCamDist(), vehicle_controller_inside->getThirdPersonCamTraceSelfAvoidanceDist()) : 0.f;
		// We want to make sure the 3rd-person camera view is not occluded by objects behind the avatar's head (walls etc..)
		// So trace a ray backwards, and position the camera on the ray path before it hits the wall.
		RayTraceResult trace_results;
		if(physics_world)
			physics_world->traceRay(/*origin=*/use_target_pos + normalise(cam_back_dir) * initial_ignore_dist, 
				/*dir=*/normalise(cam_back_dir), /*max_t=*/cam_back_dir.length() - initial_ignore_dist + 1.f, /*ignore body id=*/JPH::BodyID(), trace_results);
		else
			trace_results.hit_object = NULL;

		
		if(trace_results.hit_object)
		{
			const float use_dist = myClamp(initial_ignore_dist + trace_results.hit_t - 0.05f, 0.5f, myMax(0.5f, cam_back_dir.length()));
			cam_back_dir = normalise(cam_back_dir) * use_dist;
		}
		
		//cam_controller.setThirdPersonCamTranslation(Vec3d(cam_back_dir));
		if(cam_controller.current_cam_mode != CameraController::CameraMode_FreeCamera && cam_controller.current_cam_mode != CameraController::CameraMode_TrackingCamera)
			cam_controller.setThirdPersonCamPosition(cam_controller.current_third_person_target_pos + Vec3d(cam_back_dir));


		if(cam_controller.autofocus_mode == CameraController::AutofocusMode_Eye)
		{
			const float cam_eye_dist = myMin(
				cam_controller.getPosition().toVec4fPoint().getDist(left_eye_pos),
				cam_controller.getPosition().toVec4fPoint().getDist(right_eye_pos)
			);

			opengl_engine->getCurrentScene()->dof_blur_focus_distance = myMax(0.01f, cam_eye_dist - 0.015f);

			photo_mode_ui.autofocusDistSet(cam_eye_dist);
		}
	}
}


// Handle any messages (chat messages etc..)
void GUIClient::handleMessages(double global_time, double cur_time)
{
	PERFORMANCEAPI_INSTRUMENT("handle msgs");
	ZoneScopedN("handle msgs"); // Tracy profiler

	// Remove any messages from the message queue, store in temp_msgs.
	this->msg_queue.dequeueAnyQueuedItems(temp_msgs);

	for(size_t msg_i=0; msg_i<temp_msgs.size(); ++msg_i)
	{
		ThreadMessage* const msg = temp_msgs[msg_i].ptr();

		if(dynamic_cast<ModelLoadedThreadMessage*>(msg))
		{
			ModelLoadedThreadMessage* loaded_msg = static_cast<ModelLoadedThreadMessage*>(msg);

			if(loaded_msg->total_geom_size_B <= vbo_pool->getLargestVBOSize())
				async_model_loaded_messages_to_process.push_back(loaded_msg);
			else
				model_loaded_messages_to_process.push_back(loaded_msg);
		}
		else if(dynamic_cast<TextureLoadedThreadMessage*>(msg))
		{
			Reference<TextureLoadedThreadMessage> loaded_msg = temp_msgs[msg_i].downcast<TextureLoadedThreadMessage>();
			
			if(opengl_upload_thread)
			{
				// If we are using an OpenGLUploadThread, then LoadTextureTask etc will pass messages directly to the OpenGLUploadThread, instead 
				// of sending a TextureLoadedThreadMessage back to this thread.
				assert(0);
			}
			else
			{
				if(loaded_msg->texture_data->frame_size_B <= pbo_pool->getLargestPBOSize())
					async_texture_loaded_messages_to_process.push_back(loaded_msg);
				else
					texture_loaded_messages_to_process.push_back(loaded_msg);
			}
		}
		else if(dynamic_cast<TextureUploadedMessage*>(msg))
		{
			const TextureUploadedMessage* m = static_cast<const TextureUploadedMessage*>(msg);

			runtimeCheck(m->user_info.nonNull());
			LoadTextureTaskUploadingUserInfo* user_info = m->user_info.downcastToPtr<LoadTextureTaskUploadingUserInfo>();

			opengl_engine->addOpenGLTexture(m->opengl_tex->key, m->opengl_tex);

			this->handleUploadedTexture(m->tex_path, user_info->tex_URL, m->opengl_tex, m->texture_data, user_info->terrain_map);

			// Now that this texture is loaded, remove from textures_processing set.
			// If the texture is unloaded, then this will allow it to be reprocessed and reloaded.
			//assert(textures_processing.count(m->tex_path) >= 1);
			textures_processing.erase(m->tex_path);
		}
		else if(dynamic_cast<AnimatedTextureUpdated*>(msg))
		{
			const AnimatedTextureUpdated* m = static_cast<const AnimatedTextureUpdated*>(msg);

			animated_texture_manager->doTextureSwap(opengl_engine.ptr(), m->old_tex, m->new_tex);
		}
		else if(dynamic_cast<GeometryUploadedMessage*>(msg))
		{
			const GeometryUploadedMessage* m = static_cast<const GeometryUploadedMessage*>(msg);

			LoadModelTaskUploadingUserInfo* user_info = m->user_info.downcastToPtr<LoadModelTaskUploadingUserInfo>();
			try
			{
				opengl_engine->vert_buf_allocator->getOrCreateAndAssignVAOForMesh(*m->meshdata, m->meshdata->vertex_spec);

				// Process the finished upload (assign mesh to objects etc.)
				handleUploadedMeshData(user_info->lod_model_url, user_info->model_lod_level, user_info->built_dynamic_physics_ob, m->meshdata, user_info->physics_shape,
					user_info->voxel_subsample_factor, user_info->voxel_hash);
			}
			catch(glare::Exception& e)
			{
				logMessage("Error while handling uploaded mesh data: " + e.what());
			}
		}
		/*else if(dynamic_cast<BuildScatteringInfoDoneThreadMessage*>(msg))
		{
			BuildScatteringInfoDoneThreadMessage* loaded_msg = static_cast<BuildScatteringInfoDoneThreadMessage*>(msg);

			// Look up object
			Lock lock(this->world_state->mutex);

			auto res = this->world_state->objects.find(loaded_msg->ob_uid);
			if(res != this->world_state->objects.end())
			{
				WorldObject* ob = res.getValue().ptr();

				ob->scattering_info = loaded_msg->ob_scattering_info;

				doBiomeScatteringForObject(ob);
			}
		}*/
		else if(dynamic_cast<AudioLoadedThreadMessage*>(msg))
		{
			AudioLoadedThreadMessage* loaded_msg = static_cast<AudioLoadedThreadMessage*>(msg);

			// conPrint("AudioLoadedThreadMessage: loaded_msg->audio_source_url: " + loaded_msg->audio_source_url);

			if(world_state.nonNull())
			{
				// Iterate over objects and load an audio source for any object using this audio URL.
				try
				{
					Lock lock(this->world_state->mutex);

					for(auto it = this->world_state->objects.valuesBegin(); it != this->world_state->objects.valuesEnd(); ++it)
					{
						WorldObject* ob = it.getValue().ptr();

						if(ob->audio_source_url == loaded_msg->audio_source_url)
						{
							// Remove any existing audio source for the object
							if(ob->audio_source.nonNull())
							{
								audio_engine.removeSource(ob->audio_source);
								ob->audio_source = NULL;
							}

							if(loaded_msg->mapped_file)
							{
								// Make a new audio source
								glare::AudioEngine::AddSourceFromStreamingSoundFileParams params;
								params.sound_file_path = resource_manager->pathForURL(ob->audio_source_url);
								params.mem_mapped_sound_file = loaded_msg->mapped_file;
								//params.sound_data_source = data_source;
								params.source_volume = ob->audio_volume;
								params.global_time = this->world_state->getCurrentGlobalTime();
								params.looping =  BitUtils::isBitSet(ob->flags, WorldObject::AUDIO_LOOP);
								params.paused = !BitUtils::isBitSet(ob->flags, WorldObject::AUDIO_AUTOPLAY);

								glare::AudioSourceRef source = audio_engine.addSourceFromStreamingSoundFile(params, ob->pos.toVec4fPoint());

								const Parcel* parcel = world_state->getParcelPointIsIn(ob->pos);
								source->userdata_1 = parcel ? parcel->id.value() : ParcelID::invalidParcelID().value(); // Save the ID of the parcel the object is in, in userdata_1 field of the audio source.

								ob->audio_source = source;
								ob->audio_state = WorldObject::AudioState_Loaded;

								//---------------- Mute audio sources outside the parcel we are in, if required ----------------
								// Find out which parcel we are in, if any.
								ParcelID in_parcel_id = ParcelID::invalidParcelID(); // Which parcel camera is in
								bool mute_outside_audio = false; // Does the parcel the camera is in have 'mute outside audio' set?
								const Parcel* cam_parcel = world_state->getParcelPointIsIn(this->cam_controller.getFirstPersonPosition());
								if(cam_parcel)
								{
									in_parcel_id = cam_parcel->id;
									if(BitUtils::isBitSet(cam_parcel->flags, Parcel::MUTE_OUTSIDE_AUDIO_FLAG))
										mute_outside_audio = true;
								}

								if(mute_outside_audio && // If we are in a parcel, which has the mute-outside-audio option enabled:
									(source->userdata_1 != in_parcel_id.value())) // And the source is in another parcel (or not in any parcel):
								{
									source->setMuteVolumeFactorImmediately(0.f); // Mute it (set mute volume factor)
									audio_engine.sourceVolumeUpdated(*source); // Tell audio engine to mute it.
								}
								//----------------------------------------------------------------------------------------------

							}
							else if(loaded_msg->sound_file && loaded_msg->sound_file->buf->buffer.size() > 0) // Avoid divide by zero.
							{
								// Timer timer;
								// Add a non-streaming audio source
								ob->audio_source = new glare::AudioSource();
								ob->audio_source->type = glare::AudioSource::SourceType_NonStreaming;
								ob->audio_source->looping = BitUtils::isBitSet(ob->flags, WorldObject::AUDIO_LOOP);
								ob->audio_source->paused = !BitUtils::isBitSet(ob->flags, WorldObject::AUDIO_AUTOPLAY);
								ob->audio_source->shared_buffer = loaded_msg->sound_file->buf;
								ob->audio_source->sampling_rate = loaded_msg->sound_file->sample_rate;
								ob->audio_source->pos = ob->getCentroidWS();
								ob->audio_source->volume = ob->audio_volume;
								const double audio_len_s = loaded_msg->sound_file->buf->buffer.size() / (double)loaded_msg->sound_file->sample_rate;
								const double source_time_offset = Maths::doubleMod(global_time, audio_len_s);
								ob->audio_source->cur_read_i = Maths::intMod((int)(source_time_offset * loaded_msg->sound_file->sample_rate), (int)loaded_msg->sound_file->buf->buffer.size());
								ob->audio_source->debugname = ob->audio_source_url;

								const Parcel* parcel = world_state->getParcelPointIsIn(ob->pos);
								ob->audio_source->userdata_1 = parcel ? parcel->id.value() : ParcelID::invalidParcelID().value(); // Save the ID of the parcel the object is in, in userdata_1 field of the audio source.

								audio_engine.addSource(ob->audio_source);

								ob->audio_state = WorldObject::AudioState_Loaded;
								//ob->loaded_audio_source_url = ob->audio_source_url;

								// conPrint("Added AudioSource " + loaded_msg->audio_source_url + ".  loaded_msg->data.size(): " + toString(loaded_msg->audio_buffer->buffer.size()) + " (Elapsed: " + timer.elapsedStringNSigFigs(4) + ")");
							}
						}

						//loadAudioForObject(ob);
						//if(ob_lod_model_url == URL)
						//	loadModelForObject(ob);
					}
				}
				catch(glare::Exception& e)
				{
					print("Error while loading object: " + e.what());
				}
			}

			// Now that this audio is loaded, removed from audio_processing set.
			// If the audio is unloaded, then this will allow it to be reprocessed and reloaded.
			audio_processing.erase(loaded_msg->audio_source_url);
		}
		else if(dynamic_cast<ScriptLoadedThreadMessage*>(msg))
		{
			ScriptLoadedThreadMessage* loaded_msg = static_cast<ScriptLoadedThreadMessage*>(msg);

			// conPrint("ScriptLoadedThreadMessage");

			if(world_state.nonNull())
			{
				// Iterate over objects and assign the script evaluator for any object using this script.
				{
					Lock lock(this->world_state->mutex);

					for(auto it = this->world_state->objects.valuesBegin(); it != this->world_state->objects.valuesEnd(); ++it)
					{
						WorldObject* ob = it.getValue().ptr();
						if(ob->script == loaded_msg->script)
							handleScriptLoadedForObUsingScript(loaded_msg, ob);
					}
				}
			}

			// Now that this script is loaded, removed from script_content_processing set.
			// If the script is unloaded, then this will allow it to be reprocessed and reloaded.
			script_content_processing.erase(loaded_msg->script);
		}
		else if(dynamic_cast<const ClientConnectingToServerMessage*>(msg))
		{
			this->connection_state = ServerConnectionState_Connecting;
			//ui_interface->updateStatusBar();

			this->server_ip_addr = static_cast<const ClientConnectingToServerMessage*>(msg)->server_ip;

			// Now that we have the server IP address, start UDP thread.
			try
			{
#if !defined(EMSCRIPTEN)
				logAndConPrintMessage("Creating UDP socket...");

				udp_socket = new UDPSocket();
				udp_socket->createClientSocket(/*use_IPv6=*/server_ip_addr.getVersion() == IPAddress::Version_6);

				// Send dummy packet to server to make the OS bind the socket to a local port.
				uint32 dummy_type = 6;
				udp_socket->sendPacket(&dummy_type, sizeof(dummy_type), server_ip_addr, server_UDP_port);


				//const int local_UDP_port = udp_socket->getThisEndPort(); // UDP socket should be bound now; get port.
				//logAndConPrintMessage("Created UDP socket, local_UDP_port: " + toString(local_UDP_port));

				// Create ClientUDPHandlerThread for handling incoming UDP messages from server
				Reference<ClientUDPHandlerThread> udp_handler_thread = new ClientUDPHandlerThread(udp_socket, server_hostname, this->world_state.ptr(), &this->audio_engine);
				client_udp_handler_thread_manager.addThread(udp_handler_thread);

				// Send ClientUDPSocketOpen message
				MessageUtils::initPacket(scratch_packet, Protocol::ClientUDPSocketOpen);
				scratch_packet.writeUInt32(0/*local_UDP_port*/);
				enqueueMessageToSend(*this->client_thread, scratch_packet);
#endif
			}
			catch(glare::Exception& e)
			{
				logAndConPrintMessage(e.what());
				//QMessageBox msgBox;
				//msgBox.setText(QtUtils::toQString(e.what()));
				//msgBox.exec();
				//return;
			}
		}
		else if(const ClientConnectedToServerMessage* connected_msg = dynamic_cast<const ClientConnectedToServerMessage*>(msg))
		{
			this->connection_state = ServerConnectionState_Connected;
			//updateStatusBar();

			this->client_avatar_uid       = connected_msg->client_avatar_uid;
			this->server_protocol_version = connected_msg->server_protocol_version;
			this->server_capabilities     = connected_msg->server_capabilities;
			this->server_opt_mesh_version = connected_msg->server_mesh_optimisation_version;

			logMessage("Connected to server.  server_protocol_version: " + toString(server_protocol_version) + ", server_capabilities: " + toString(server_capabilities) + ", server_opt_mesh_version: " + toString(server_opt_mesh_version));

			TracyMessageL("ClientConnectedToServerMessage received");
			
			this->server_has_basis_textures             = BitUtils::isBitSet(this->server_capabilities, Protocol::OBJECT_TEXTURE_BASISU_SUPPORT);
			this->server_has_basisu_terrain_detail_maps = BitUtils::isBitSet(this->server_capabilities, Protocol::TERRAIN_DETAIL_MAPS_BASISU_SUPPORT);
			this->server_has_optimised_meshes           = BitUtils::isBitSet(this->server_capabilities, Protocol::OPTIMISED_MESH_SUPPORT);

			// Try and log in automatically if we have saved credentials for this domain, and auto_login is true.
			if(settings->getBoolValue("LoginDialog/auto_login", /*default=*/true))
			{
				const std::string username = ui_interface->getUsernameForDomain(server_hostname);
				if(!username.empty())
				{
					const std::string password = ui_interface->getDecryptedPasswordForDomain(server_hostname); // manager.getDecryptedPasswordForDomain(server_hostname);

					// Make LogInMessage packet and enqueue to send
					MessageUtils::initPacket(scratch_packet, Protocol::LogInMessage);
					scratch_packet.writeStringLengthFirst(username);
					scratch_packet.writeStringLengthFirst(password);

					enqueueMessageToSend(*this->client_thread, scratch_packet);
				}
			}
				
			// Send CreateAvatar packet for this client's avatar
			{
				MessageUtils::initPacket(scratch_packet, Protocol::CreateAvatar);

				const Vec3d cam_angles = this->cam_controller.getAvatarAngles();
				Avatar avatar;
				avatar.uid = this->client_avatar_uid;
				avatar.pos = Vec3d(this->cam_controller.getFirstPersonPosition());
				avatar.rotation = Vec3f(0, (float)cam_angles.y, (float)cam_angles.x);
				writeAvatarToNetworkStream(avatar, scratch_packet);

				enqueueMessageToSend(*this->client_thread, scratch_packet);
			}

			audio_engine.playOneShotSound(resources_dir_path + "/sounds/462089__newagesoup__ethereal-woosh_normalised_mono.wav", 
				(this->cam_controller.getFirstPersonPosition() + Vec3d(0, 0, -1)).toVec4fPoint());
		}
		else if(dynamic_cast<const AudioStreamToServerStartedMessage*>(msg))
		{
			// Sent by MicReadThread to indicate that streaming audio to the server has started.   Also sent periodically during streaming as well.

			const AudioStreamToServerStartedMessage* m = static_cast<const AudioStreamToServerStartedMessage*>(msg);

			// Make AudioStreamToServerStarted packet and enqueue to send
			MessageUtils::initPacket(scratch_packet, Protocol::AudioStreamToServerStarted);
			scratch_packet.writeUInt32(m->sampling_rate);
			scratch_packet.writeUInt32(m->flags);
			scratch_packet.writeUInt32(m->stream_id);

			enqueueMessageToSend(*this->client_thread, scratch_packet);
		}
		else if(dynamic_cast<const AudioStreamToServerEndedMessage*>(msg))
		{
			// Sent by MicReadThread to indicate that streaming audio to the server has ended.

			// Make AudioStreamToServerEnded packet and enqueue to send
			MessageUtils::initPacket(scratch_packet, Protocol::AudioStreamToServerEnded);

			enqueueMessageToSend(*this->client_thread, scratch_packet);
		}
		else if(dynamic_cast<const RemoteClientAudioStreamToServerStarted*>(msg))
		{
			// Sent by ClientThread to this GUIClient after receiving AudioStreamToServerStarted message from server.
			// Indicates a client has started streaming audio to the server.
			// Create an audio source to play spatial audio from the avatar, if there isn't one already.

			const RemoteClientAudioStreamToServerStarted* m = static_cast<const RemoteClientAudioStreamToServerStarted*>(msg);

			if(m->flags == 0)
				conPrint("Received RemoteClientAudioStreamToServerStarted, avatar_uid: " + m->avatar_uid.toString() + ", sampling_rate: " + toString(m->sampling_rate) + ", flags: " + toString(m->flags) + ", stream_id: " + toString(m->stream_id));

			if((m->sampling_rate == 8000) || (m->sampling_rate == 12000) || (m->sampling_rate == 16000) || (m->sampling_rate == 24000) || (m->sampling_rate == 48000)) // Sampling rates Opus encoder supports
			{
				if(world_state.nonNull())
				{
					Lock lock(this->world_state->mutex);

					auto res = this->world_state->avatars.find(m->avatar_uid);
					if(res != this->world_state->avatars.end())
					{
						Avatar* avatar = res->second.getPointer();

						if(avatar->audio_source.isNull() && !avatar->isOurAvatar())
						{
							avatar->audio_stream_sampling_rate = m->sampling_rate;
							avatar->audio_stream_id = m->stream_id;

							// Add audio source for voice chat
							avatar->audio_source = new glare::AudioSource();
							avatar->audio_source->type = glare::AudioSource::SourceType_Streaming;
							avatar->audio_source->pos = avatar->pos.toVec4fPoint();
							avatar->audio_source->sampling_rate = m->sampling_rate;

							audio_engine.addSource(avatar->audio_source);

							world_state->avatars_changed = 1; // Inform ClientUDPHandlerThread

							if(avatar->speaker_gl_ob.isNull())
							{
								avatar->speaker_gl_ob = makeSpeakerGLObject();
								opengl_engine->addObject(avatar->speaker_gl_ob); // Add to 3d engine
							}
						}
					}
				}
			}
			else
				conPrint("Invalid sampling rate, ignoring RemoteClientAudioStreamToServerStarted message");
		}
		else if(dynamic_cast<const RemoteClientAudioStreamToServerEnded*>(msg))
		{
			// Sent by ClientThread to this GUIClient after receiving AudioStreamToServerEnded message from server.
			// Indicates a client has finished streaming audio to the server.

			const RemoteClientAudioStreamToServerEnded* m = static_cast<const RemoteClientAudioStreamToServerEnded*>(msg);

			conPrint("Received RemoteClientAudioStreamToServerEnded, avatar_uid: " + m->avatar_uid.toString());

			if(world_state.nonNull())
			{
				Lock lock(this->world_state->mutex);

				auto res = this->world_state->avatars.find(m->avatar_uid);
				if(res != this->world_state->avatars.end())
				{
					Avatar* avatar = res->second.getPointer();

					// Remove audio source for voice chat, if it exists
					if(avatar->audio_source.nonNull() && !avatar->isOurAvatar())
					{
						audio_engine.removeSource(avatar->audio_source);
						avatar->audio_source = NULL;

						world_state->avatars_changed = 1; // Inform ClientUDPHandlerThread
					}

					// Remove speaker icon by nametag.
					if(avatar->speaker_gl_ob.nonNull())
					{
						opengl_engine->removeObject(avatar->speaker_gl_ob);
						avatar->speaker_gl_ob = NULL;
					}
				}
			}
		}
		else if(dynamic_cast<const ClientProtocolTooOldMessage*>(msg))
		{
			ui_interface->showHTMLMessageBox("Client too old", "<p>Sorry, your Substrata client is too old.</p><p>Please download and install an updated client from <a href=\"https://substrata.info/\">substrata.info</a></p>");
		}
		else if(dynamic_cast<const ClientDisconnectedFromServerMessage*>(msg))
		{
			const ClientDisconnectedFromServerMessage* m = static_cast<const ClientDisconnectedFromServerMessage*>(msg);
			if(!m->error_message.empty() && !m->closed_gracefully)
			{
				showErrorNotification(m->error_message);
			}
			this->connection_state = ServerConnectionState_NotConnected;

			this->logged_in_user_id = UserID::invalidUserID();
			this->logged_in_user_name = "";
			this->logged_in_user_flags = 0;

			ui_interface->setTextAsNotLoggedIn();

			ui_interface->updateWorldSettingsControlsEditable();

			//updateStatusBar();
		}
		else if(dynamic_cast<const AvatarIsHereMessage*>(msg))
		{
			const AvatarIsHereMessage* m = static_cast<const AvatarIsHereMessage*>(msg);

			if(world_state.nonNull())
			{
				Lock lock(this->world_state->mutex);

				auto res = this->world_state->avatars.find(m->avatar_uid);
				if(res != this->world_state->avatars.end())
				{
					Avatar* avatar = res->second.getPointer();

					ui_interface->appendChatMessage("<i><span style=\"color:rgb(" + 
						toString(avatar->name_colour.r * 255) + ", " + toString(avatar->name_colour.g * 255) + ", " + toString(avatar->name_colour.b * 255) +
						")\">" + web::Escaping::HTMLEscape(avatar->name) + "</span> is here.</i>");
					ui_interface->updateOnlineUsersList();

					chat_ui.appendMessage(avatar->name, avatar->name_colour, " is here.");
				}
			}
		}
		else if(dynamic_cast<const AvatarCreatedMessage*>(msg))
		{
			const AvatarCreatedMessage* m = static_cast<const AvatarCreatedMessage*>(msg);

			if(world_state.nonNull())
			{
				Lock lock(this->world_state->mutex);

				auto res = this->world_state->avatars.find(m->avatar_uid);
				if(res != this->world_state->avatars.end())
				{
					const Avatar* avatar = res->second.getPointer();
					ui_interface->appendChatMessage("<i><span style=\"color:rgb(" + 
						toString(avatar->name_colour.r * 255) + ", " + toString(avatar->name_colour.g * 255) + ", " + toString(avatar->name_colour.b * 255) +
						")\">" + web::Escaping::HTMLEscape(avatar->name) + "</span> joined.</i>");
					ui_interface->updateOnlineUsersList();

					chat_ui.appendMessage(avatar->name, avatar->name_colour, " joined.");
				}
			}
		}
		else if(dynamic_cast<const AvatarPerformGestureMessage*>(msg))
		{
			const AvatarPerformGestureMessage* m = static_cast<const AvatarPerformGestureMessage*>(msg);

			if(m->avatar_uid != client_avatar_uid) // Ignore messages about our own avatar
			{
				if(world_state.nonNull())
				{
					Lock lock(this->world_state->mutex);

					auto res = this->world_state->avatars.find(m->avatar_uid);
					if(res != this->world_state->avatars.end())
					{
						Avatar* avatar = res->second.getPointer();
						avatar->graphics.performGesture(cur_time, m->gesture_name, GestureUI::animateHead(m->gesture_name), GestureUI::loopAnim(m->gesture_name));
					}
				}
			}
		}
		else if(dynamic_cast<const AvatarStopGestureMessage*>(msg))
		{
			const AvatarStopGestureMessage* m = static_cast<const AvatarStopGestureMessage*>(msg);

			if(m->avatar_uid != client_avatar_uid) // Ignore messages about our own avatar
			{
				if(world_state.nonNull())
				{
					Lock lock(this->world_state->mutex);

					auto res = this->world_state->avatars.find(m->avatar_uid);
					if(res != this->world_state->avatars.end())
					{
						Avatar* avatar = res->second.getPointer();
						avatar->graphics.stopGesture(cur_time);
					}
				}
			}
		}
		else if(dynamic_cast<const ChatMessage*>(msg))
		{
			const ChatMessage* m = static_cast<const ChatMessage*>(msg);

			if(world_state.nonNull())
			{
				// Look up sending avatar name colour.  TODO: could do this with sending avatar UID, would be faster + simpler.
				Colour3f col(0.8f);
				{
					Lock lock(this->world_state->mutex);

					for(auto it = this->world_state->avatars.begin(); it != this->world_state->avatars.end(); ++it)
					{
						const Avatar* avatar = it->second.getPointer();
						if(avatar->name == m->name)
							col = avatar->name_colour;
					}
				}

				ui_interface->appendChatMessage(
					"<p><span style=\"color:rgb(" + toString(col.r * 255) + ", " + toString(col.g * 255) + ", " + toString(col.b * 255) + ")\">" + web::Escaping::HTMLEscape(m->name) + "</span>: " +
					web::Escaping::HTMLEscape(m->msg) + "</p>");

				chat_ui.appendMessage(m->name, col, ": " + m->msg);
			}
		}
		else if(dynamic_cast<const InfoMessage*>(msg))
		{
			const InfoMessage* m = static_cast<const InfoMessage*>(msg);
			showInfoNotification(m->msg);
		}
		else if(dynamic_cast<const ErrorMessage*>(msg))
		{
			const ErrorMessage* m = static_cast<const ErrorMessage*>(msg);
			showErrorNotification(m->msg);
		}
		else if(dynamic_cast<const LogMessage*>(msg))
		{
			const LogMessage* m = static_cast<const LogMessage*>(msg);
			logMessage(m->msg);
		}
		else if(dynamic_cast<const LoggedInMessage*>(msg))
		{
			const LoggedInMessage* m = static_cast<const LoggedInMessage*>(msg);

			ui_interface->setTextAsLoggedIn(m->username);
			this->logged_in_user_id = m->user_id;
			this->logged_in_user_name = m->username;
			this->logged_in_user_flags = m->user_flags;

			conPrint("Logged in as user with id " + toString(this->logged_in_user_id.value()));

			recolourParcelsForLoggedInState();
			ui_interface->updateWorldSettingsControlsEditable();

			misc_info_ui.showLoggedInButton(m->username);

			// Send AvatarFullUpdate message, to change the nametag on our avatar.
			const Vec3d cam_angles = this->cam_controller.getAvatarAngles();
			Avatar avatar;
			avatar.uid = this->client_avatar_uid;
			avatar.pos = Vec3d(this->cam_controller.getFirstPersonPosition());
			avatar.rotation = Vec3f(0, (float)cam_angles.y, (float)cam_angles.x);
			avatar.avatar_settings = m->avatar_settings;
			avatar.name = m->username;

			MessageUtils::initPacket(scratch_packet, Protocol::AvatarFullUpdate);
			writeAvatarToNetworkStream(avatar, scratch_packet);
				
			enqueueMessageToSend(*this->client_thread, scratch_packet);
		}
		else if(dynamic_cast<const LoggedOutMessage*>(msg))
		{
			ui_interface->setTextAsNotLoggedIn();
			this->logged_in_user_id = UserID::invalidUserID();
			this->logged_in_user_name = "";
			this->logged_in_user_flags = 0;

			recolourParcelsForLoggedInState();
			ui_interface->updateWorldSettingsControlsEditable();

			misc_info_ui.showLogInAndSignUpButtons();

			// Send AvatarFullUpdate message, to change the nametag on our avatar.
			const Vec3d cam_angles = this->cam_controller.getAvatarAngles();
			Avatar avatar;
			avatar.uid = this->client_avatar_uid;
			avatar.pos = Vec3d(this->cam_controller.getFirstPersonPosition());
			avatar.rotation = Vec3f(0, (float)cam_angles.y, (float)cam_angles.x);
			avatar.avatar_settings.model_url = "";
			avatar.name = "Anonymous";

			MessageUtils::initPacket(scratch_packet, Protocol::AvatarFullUpdate);
			writeAvatarToNetworkStream(avatar, scratch_packet);

			enqueueMessageToSend(*this->client_thread, scratch_packet);
		}
		else if(dynamic_cast<const SignedUpMessage*>(msg))
		{
			const SignedUpMessage* m = static_cast<const SignedUpMessage*>(msg);
			ui_interface->showPlainTextMessageBox("Signed up", "Successfully signed up and logged in.");

			ui_interface->setTextAsLoggedIn(m->username);
			this->logged_in_user_id = m->user_id;
			this->logged_in_user_name = m->username;
			this->logged_in_user_flags = 0;

			misc_info_ui.showLoggedInButton(m->username);

			// Send AvatarFullUpdate message, to change the nametag on our avatar.
			const Vec3d cam_angles = this->cam_controller.getAvatarAngles();
			Avatar avatar;
			avatar.uid = this->client_avatar_uid;
			avatar.pos = Vec3d(this->cam_controller.getFirstPersonPosition());
			avatar.rotation = Vec3f(0, (float)cam_angles.y, (float)cam_angles.x);
			avatar.avatar_settings.model_url = "";
			avatar.name = m->username;

			MessageUtils::initPacket(scratch_packet, Protocol::AvatarFullUpdate);
			writeAvatarToNetworkStream(avatar, scratch_packet);

			enqueueMessageToSend(*this->client_thread, scratch_packet);
		}
		else if(dynamic_cast<const ServerAdminMessage*>(msg))
		{
			const ServerAdminMessage* m = static_cast<const ServerAdminMessage*>(msg);
				
			misc_info_ui.showServerAdminMessage(m->msg);
		}
		else if(dynamic_cast<const WorldSettingsReceivedMessage*>(msg))
		{
			const WorldSettingsReceivedMessage* m = static_cast<const WorldSettingsReceivedMessage*>(msg);

			this->connected_world_settings.copyNetworkStateFrom(m->world_settings); // Store world settings to be used later

			this->ui_interface->updateWorldSettingsUIFromWorldSettings(); // Update UI

			if(!m->is_initial_send)
				showInfoNotification("World settings updated");

			// Reload terrain by shutting it down, will be recreated in GUIClient::updateGroundPlane().
			if(this->terrain_system.nonNull())
			{
				terrain_system->shutdown();
				terrain_system = NULL;
			}

			if(physics_world.nonNull())
			{
				physics_world->setWaterBuoyancyEnabled(BitUtils::isBitSet(this->connected_world_settings.terrain_spec.flags, TerrainSpec::WATER_ENABLED_FLAG));
				const float use_water_z = myClamp(this->connected_world_settings.terrain_spec.water_z, -1.0e8f, 1.0e8f); // Avoid NaNs, Infs etc.
				physics_world->setWaterZ(use_water_z);
			}
		}
		else if(dynamic_cast<const WorldDetailsReceivedMessage*>(msg))
		{
			const WorldDetailsReceivedMessage* m = static_cast<const WorldDetailsReceivedMessage*>(msg);

			this->connected_world_details = m->world_details;
		}
		else if(dynamic_cast<const MapTilesResultReceivedMessage*>(msg))
		{
			const MapTilesResultReceivedMessage* m = static_cast<const MapTilesResultReceivedMessage*>(msg);

			if(minimap)
				this->minimap->handleMapTilesResultReceivedMessage(*m);
		}
		else if(dynamic_cast<const UserSelectedObjectMessage*>(msg))
		{
			if(world_state.nonNull())
			{
				//print("GUIClient: Received UserSelectedObjectMessage");
				const UserSelectedObjectMessage* m = static_cast<const UserSelectedObjectMessage*>(msg);
				Lock lock(this->world_state->mutex);
				const bool is_ob_with_uid_inserted = this->world_state->objects.find(m->object_uid) != this->world_state->objects.end();
				if(this->world_state->avatars.count(m->avatar_uid) != 0 && is_ob_with_uid_inserted)
				{
					this->world_state->avatars[m->avatar_uid]->selected_object_uid = m->object_uid;
				}
			}
		}
		else if(dynamic_cast<const UserDeselectedObjectMessage*>(msg))
		{
			if(world_state.nonNull())
			{	
				//print("GUIClient: Received UserDeselectedObjectMessage");
				const UserDeselectedObjectMessage* m = static_cast<const UserDeselectedObjectMessage*>(msg);
				Lock lock(this->world_state->mutex);
				if(this->world_state->avatars.count(m->avatar_uid) != 0)
				{
					this->world_state->avatars[m->avatar_uid]->selected_object_uid = UID::invalidUID();
				}
			}
		}
		else if(dynamic_cast<const GetFileMessage*>(msg))
		{
			// When the server wants a file from the client, it will send the client a GetFile protocol message.
			const GetFileMessage* m = static_cast<const GetFileMessage*>(msg);

			if(ResourceManager::isValidURL(m->URL))
			{
				if(resource_manager->isFileForURLPresent(m->URL))
				{
					const std::string path = resource_manager->pathForURL(m->URL);

					const std::string username = ui_interface->getUsernameForDomain(server_hostname);
					const std::string password = ui_interface->getDecryptedPasswordForDomain(server_hostname);

					this->num_resources_uploading++;
#if EMSCRIPTEN
					const size_t max_num_upload_threads = 1;
#else
					const size_t max_num_upload_threads = 4;
#endif
					if(resource_upload_thread_manager.getNumThreads() == 0)
					{
						for(size_t q=0; q<max_num_upload_threads; ++q)
							resource_upload_thread_manager.addThread(new UploadResourceThread(&this->msg_queue, &upload_queue, server_hostname, server_port, username, password, this->client_tls_config, 
								&this->num_resources_uploading));
					}

					upload_queue.enqueue(new ResourceToUpload(path, m->URL));

					print("Received GetFileMessage, Uploading resource with URL '" + toStdString(m->URL) + "' to server.");
				}
				else
					print("Could not upload resource with URL '" + toStdString(m->URL) + "' to server, not present on client.");
			}
		}
		else if(dynamic_cast<const NewResourceOnServerMessage*>(msg))
		{
			// When the server has a file uploaded to it, it will send a NewResourceOnServer message to clients, so they can download it.

			const NewResourceOnServerMessage* m = static_cast<const NewResourceOnServerMessage*>(msg);

			if(world_state.nonNull())
			{
				conPrint("Got NewResourceOnServerMessage, URL: " + toStdString(m->URL));

				// A download of this resource may have failed earlier, but should succeed now.
				resource_manager->removeFromDownloadFailedURLs(m->URL);

				if(ResourceManager::isValidURL(m->URL))
				{
					if(!resource_manager->isFileForURLPresent(m->URL)) // If we don't have this file yet:
					{
						conPrint("Do not have resource.");

						DownloadingResourceInfo downloading_info;
						Vec4f centroid_ws(0,0,0,1);
						float aabb_ws_longest_len = 1.f;

						// Iterate over objects and see if they were using a placeholder model for this resource.
						Lock lock(this->world_state->mutex);
						bool need_resource = false;
						for(auto it = this->world_state->objects.valuesBegin(); it != this->world_state->objects.valuesEnd(); ++it)
						{
							WorldObject* ob = it.getValue().ptr();

							const int ob_lod_level = ob->getLODLevel(cam_controller.getPosition());

							//if(ob->using_placeholder_model)
							{
								glare::ArenaAllocator use_arena = arena_allocator.getFreeAreaArenaAllocator();
								glare::STLArenaAllocator<DependencyURL> stl_arena_allocator(&use_arena);

								WorldObject::GetDependencyOptions options;
								options.use_basis = this->server_has_basis_textures;
								options.include_lightmaps = this->use_lightmaps;
								options.get_optimised_mesh = this->server_has_optimised_meshes;
								options.opt_mesh_version = this->server_opt_mesh_version;
								options.allocator = &use_arena;

								DependencyURLSet URL_set(std::less<DependencyURL>(), stl_arena_allocator);
								ob->getDependencyURLSet(ob_lod_level, options, URL_set);

								if(URL_set.count(DependencyURL(m->URL)) != 0)
								{
									downloading_info.texture_params.use_sRGB = true; // TEMP HACK
									downloading_info.build_dynamic_physics_ob = ob->isDynamic();
									downloading_info.pos = ob->pos;
									downloading_info.size_factor = LoadItemQueueItem::sizeFactorForAABBWS(ob->getAABBWSLongestLength(), /*importance_factor=*/1.f);
									downloading_info.using_objects.using_object_uids.push_back(ob->uid);
									centroid_ws = ob->getCentroidWS();
									aabb_ws_longest_len = ob->getAABBWSLongestLength();

									need_resource = true;
								}
							}
						}

						for(auto it = this->world_state->avatars.begin(); it != this->world_state->avatars.end(); ++it)
						{
							Avatar* av = it->second.getPointer();

							const int av_lod_level = av->getLODLevel(cam_controller.getPosition());

							//if(ob->using_placeholder_model)
							{
								glare::ArenaAllocator use_arena = arena_allocator.getFreeAreaArenaAllocator();
								glare::STLArenaAllocator<DependencyURL> stl_arena_allocator(&use_arena);

								Avatar::GetDependencyOptions options;
								options.get_optimised_mesh = this->server_has_optimised_meshes;
								options.opt_mesh_version = this->server_opt_mesh_version;
								options.use_basis = this->server_has_basis_textures;

								DependencyURLSet URL_set(std::less<DependencyURL>(), stl_arena_allocator);
								av->getDependencyURLSet(av_lod_level, options, URL_set);

								if(URL_set.count(DependencyURL(m->URL)) != 0)
								{
									downloading_info.texture_params.use_sRGB = true; // TEMP HACK
									downloading_info.build_physics_ob = false;
									downloading_info.pos = av->pos;
									downloading_info.size_factor = LoadItemQueueItem::sizeFactorForAABBWS(1.8f, /*importance_factor=*/1.f);
									downloading_info.used_by_other = true;
									centroid_ws = av->pos.toVec4fPoint();
									aabb_ws_longest_len = 1.8f;

									need_resource = true;
								}
							}
						}

						const bool valid_extension = FileTypes::hasSupportedExtension(m->URL);
						conPrint("need_resource: " + boolToString(need_resource) + " valid_extension: " + boolToString(valid_extension));

						if(need_resource && valid_extension)// && !shouldStreamResourceViaHTTP(m->URL))
						{
							conPrint("Need resource, downloading: " + toStdString(m->URL));

							startDownloadingResource(m->URL, centroid_ws, aabb_ws_longest_len, downloading_info);
						}
					}
				}
			}
		}
		/*else if(dynamic_cast<const ResourceDownloadingStatus*>(msg))
		{
			const ResourceDownloadingStatus* m = msg.downcastToPtr<const ResourceDownloadingStatus>();
			this->total_num_res_to_download = m->total_to_download;
			updateStatusBar();
		}*/
		else if(dynamic_cast<const ResourceDownloadedMessage*>(msg))
		{
			const ResourceDownloadedMessage* m = static_cast<const ResourceDownloadedMessage*>(msg);
			const URLString& URL = m->URL;
			ResourceRef resource = m->resource;
			logMessage("Resource downloaded: '" + toStdString(URL) + "'");
			//conPrint("Resource downloaded: '" + URL + "'");

			if(world_state.nonNull())
			{
				assert(resource.nonNull()); // The downloaded file should have been added as a resource in DownloadResourcesThread or NetDownloadResourcesThread.
				if(resource.nonNull())
				{
					// Get the local path, we will check the file type of the local path when determining what to do with the file, as the local path will have an extension given by the mime type
					// in the net download case.
					const std::string local_path = resource_manager->getLocalAbsPathForResource(*resource);

					auto res = URL_to_downloading_info.find(URL);
					if(res != URL_to_downloading_info.end())
					{
						const DownloadingResourceInfo& info = res->second;
						const Vec3d pos = info.pos;
						const float size_factor = info.size_factor;

						if(isDownloadingResourceCurrentlyNeeded(URL))
						{
							// If we just downloaded a texture, start loading it.
							// NOTE: Do we want to check this texture is actually used by an object?
							if(ImageDecoding::hasSupportedImageExtension(local_path))
							{
								//conPrint("Downloaded texture resource, loading it...");
						
								const OpenGLTextureKey tex_path(local_path);

								if(!opengl_engine->isOpenGLTextureInsertedForKey(tex_path)) // If texture is not uploaded to GPU already:
								{
									const bool just_added = checkAddTextureToProcessingSet(tex_path); // If not being loaded already:
									if(just_added)
									{
										const bool used_by_terrain = this->terrain_system.nonNull() && this->terrain_system->isTextureUsedByTerrain(tex_path);

										Reference<LoadTextureTask> task = new LoadTextureTask(opengl_engine, resource_manager, &this->msg_queue, tex_path, resource, info.texture_params, used_by_terrain, worker_allocator, 
											texture_loaded_msg_allocator, opengl_upload_thread);
										task->loaded_buffer = m->loaded_buffer;
										load_item_queue.enqueueItem(/*key=*/URL, pos.toVec4fPoint(), size_factor, task, /*max task dist=*/std::numeric_limits<float>::infinity()); // NOTE: inf dist is a bit of a hack.
									}
									else
										load_item_queue.checkUpdateItemPosition(/*key=*/URL, pos.toVec4fPoint(), size_factor);
								}
							}
							else if(FileTypes::hasAudioFileExtension(local_path))
							{
								Lock lock(this->world_state->mutex);
								for(size_t i=0; i<info.using_objects.using_object_uids.size(); ++i)
								{
									// Lookup WorldObject for UID
									auto ob_res = world_state->objects.find(info.using_objects.using_object_uids[i]);
									if(ob_res != world_state->objects.end())
									{
										WorldObject* ob = ob_res.getValue().ptr();
										if(ob->audio_source_url == URL)
											loadAudioForObject(ob, m->loaded_buffer);
									}
								}
							}
							else if(ModelLoading::hasSupportedModelExtension(local_path)) // Else we didn't download a texture, but maybe a model:
							{
								try
								{
									// Start loading the model
									Reference<LoadModelTask> load_model_task = new LoadModelTask();

									load_model_task->resource = resource;
									load_model_task->lod_model_url = URL;
									load_model_task->model_lod_level = WorldObject::getLODLevelForURL(URL);
									load_model_task->opengl_engine = this->opengl_engine;
									load_model_task->result_msg_queue = &this->msg_queue;
									load_model_task->resource_manager = resource_manager;
									load_model_task->build_physics_ob = info.build_physics_ob;
									load_model_task->build_dynamic_physics_ob = info.build_dynamic_physics_ob;
									load_model_task->loaded_buffer = m->loaded_buffer;
									load_model_task->worker_allocator = worker_allocator;
									load_model_task->upload_thread = opengl_upload_thread;

									// conPrint("handling ResourceDownloadedMessage: making LoadModelTask for " + URL);

									load_item_queue.enqueueItem(/*key=*/URL, pos.toVec4fPoint(), size_factor, load_model_task, 
										/*max task dist=*/std::numeric_limits<float>::infinity()); // NOTE: inf dist is a bit of a hack.
								}
								catch(glare::Exception& e)
								{
									print("Error while loading object: " + e.what());
								}
							}
							else
							{
								// TODO: Handle video files here?
							
								//print("file did not have a supported image, audio, or model extension: '" + getExtension(local_path) + "'");
							}
						}
						else
						{
							// conPrint("GUIClient handleMessage(): downloaded resource '" + URL + "' is not currently used, not loading.");
						}
					}
					else
					{
						assert(0); // If we downloaded the resource we should have added it to URL_to_downloading_info.  NOTE: will this work with NewResourceOnServerMessage tho?
					}
				}
			}
		}
		else if(dynamic_cast<TerrainChunkGeneratedMsg*>(msg))
		{
			const TerrainChunkGeneratedMsg* m = static_cast<const TerrainChunkGeneratedMsg*>(msg);
			if(terrain_system.nonNull())
				terrain_system->handleCompletedMakeChunkTask(*m);
		}
		else if(dynamic_cast<WindNoiseLoaded*>(msg))
		{
			const WindNoiseLoaded* m = static_cast<const WindNoiseLoaded*>(msg);

			assert(!wind_audio_source);
			wind_audio_source = new glare::AudioSource();
			wind_audio_source->type = glare::AudioSource::SourceType_NonStreaming;
			wind_audio_source->spatial_type = glare::AudioSource::SourceSpatialType_NonSpatial;
			wind_audio_source->shared_buffer = m->sound->buf;
			wind_audio_source->sampling_rate = m->sound->sample_rate;
			wind_audio_source->volume = 0;
			
			audio_engine.addSource(wind_audio_source);
		}
	}

	temp_msgs.clear();
}


std::string GUIClient::getDiagnosticsString(bool do_graphics_diagnostics, bool do_physics_diagnostics, bool do_terrain_diagnostics, double last_timerEvent_CPU_work_elapsed, double last_updateGL_time)
{
	std::string msg;

	if(selected_ob.nonNull())
	{
		msg += std::string("\nSelected object: \n");

		msg += "UID: " + selected_ob->uid.toString() + "\n";
		msg += "pos: " + selected_ob->pos.toStringMaxNDecimalPlaces(3) + "\n";
		msg += "centroid: " + selected_ob->getCentroidWS().toStringMaxNDecimalPlaces(3) + "\n";
		msg += "aabb os: " + selected_ob->getAABBOS().toStringMaxNDecimalPlaces(3) + "\n";
		msg += "aabb ws: " + selected_ob->getAABBWS().toStringMaxNDecimalPlaces(3) + "\n";
		msg += "aabb_ws_longest_len: " + doubleToStringMaxNDecimalPlaces(selected_ob->getAABBWSLongestLength(), 2) + "\n";
		msg += "biased aabb longest len: " + doubleToStringMaxNDecimalPlaces(selected_ob->getBiasedAABBLength(), 2) + "\n";

		msg += "max_model_lod_level: " + toString(selected_ob->max_model_lod_level) + "\n";
		msg += "current_lod_level: " + toString(selected_ob->current_lod_level) + "\n";
		msg += "loading_or_loaded_model_lod_level: " + toString(selected_ob->loading_or_loaded_model_lod_level) + "\n";
		msg += "loading_or_loaded_lod_level: " + toString(selected_ob->loading_or_loaded_lod_level) + "\n";

		if(selected_ob->opengl_engine_ob.nonNull())
		{
			msg += 
				"num tris: " + toString(selected_ob->opengl_engine_ob->mesh_data->getNumTris()) + " (" + getNiceByteSize(selected_ob->opengl_engine_ob->mesh_data->GPUIndicesMemUsage()) + ")\n" + 
				"num verts: " + toString(selected_ob->opengl_engine_ob->mesh_data->getNumVerts()) + " (" + getNiceByteSize(selected_ob->opengl_engine_ob->mesh_data->GPUVertMemUsage()) + ")\n" +
				"num batches (draw calls): " + toString(selected_ob->opengl_engine_ob->mesh_data->batches.size()) + "\n" +
				"num materials: " + toString(selected_ob->opengl_engine_ob->materials.size()) + "\n" +
				"shading normals: " + boolToString(selected_ob->opengl_engine_ob->mesh_data->has_shading_normals) + "\n" + 
				"vert colours: " + boolToString(selected_ob->opengl_engine_ob->mesh_data->has_vert_colours) + "\n";

			if(!selected_ob->opengl_engine_ob->materials.empty() && !selected_ob->materials.empty())
			{
				for(int i=0; i<2 && i<selected_ob->opengl_engine_ob->materials.size() && i<selected_ob->materials.size(); ++i)
				{
					OpenGLMaterial& mat = selected_ob->opengl_engine_ob->materials[i];
					if(mat.albedo_texture)
					{
						if(!selected_ob->materials.empty() && selected_ob->materials[i])
							msg += "mat " + toString(i) + " min lod level: " + toString(selected_ob->materials[i]->minLODLevel()) + "\n";
						msg += "mat " + toString(i) + " tex: " + toString(mat.albedo_texture->xRes()) + "x" + toString(mat.albedo_texture->yRes()) + " (" + getNiceByteSize(mat.albedo_texture->getTotalStorageSizeB()) + "), " + 
							getStringForGLInternalFormat(mat.albedo_texture->getInternalFormat()) + " \n";
					}
					msg += "mat " + toString(i) + " colourTexHasAlpha(): " + toString(selected_ob->materials[i]->colourTexHasAlpha()) + "\n";

					if(mat.lightmap_texture)
					{
						msg += "\n";
						msg += "lightmap: " + toString(mat.lightmap_texture->xRes()) + "x" + toString(mat.lightmap_texture->yRes()) + " (" + getNiceByteSize(mat.lightmap_texture->getTotalStorageSizeB()) + ")\n";
					}
				}
			}
		}
	}

#if TRACE_ALLOCATIONS
	msg += "---------------------\n";
	msg += "total allocated B: " + ::getMBSizeString(MemAlloc::getTotalAllocatedB()) + "\n";
	msg += "high water mark B: " + ::getMBSizeString(MemAlloc::getHighWaterMarkB()) + "\n";
	msg += "num active allocs: " + ::toString(MemAlloc::getNumActiveAllocations()) + "\n";
	msg += "num allocs:        " + ::toString(MemAlloc::getNumAllocations()) + "\n";
	msg += "---------------------\n";
#endif

#if EMSCRIPTEN
	const size_t total_memory = (size_t)EM_ASM_PTR(return HEAP8.length);
	const uintptr_t dynamic_top = (uintptr_t)sbrk(0);
	struct mallinfo meminfo = mallinfo();
	const size_t free_memory = total_memory - dynamic_top + meminfo.fordblks;
	const size_t total_used = dynamic_top - meminfo.fordblks;

	msg += "---------------------\n";
	msg += "Emscripten total memory: " + ::getMBSizeString(total_memory) + "\n";
	msg += "Emscripten dynamic_top:  " + ::getMBSizeString((size_t)dynamic_top) + "\n";
	msg += "Emscripten free_memory:  " + ::getMBSizeString(free_memory) + "\n";
	msg += "Emscripten total used mem:  " + ::getMBSizeString(total_used) + "\n";
	msg += "---------------------\n";
#endif

	if(opengl_engine.nonNull() && do_graphics_diagnostics)
	{
		msg += "------------Graphics------------\n";
		msg += opengl_engine->getDiagnostics() + "\n";
		//msg += "GL widget valid: " + boolToString(isValid()) + "\n";
		//msg += "GL format has OpenGL: " + boolToString(format().hasOpenGL()) + "\n";
		//msg += "GL format OpenGL profile: " + toString((int)ui->glWidget->format().profile()) + "\n";
		//msg += "OpenGL engine initialised: " + boolToString(opengl_engine->initSucceeded()) + "\n";
		msg += "-------------------------------\n";
	}

	// Only show physics details when physicsDiagnosticsCheckBox is checked.  Works around problem of physics_world->getDiagnostics() being slow, which causes stutters.
	if(physics_world.nonNull() && do_physics_diagnostics)
	{
		msg += "------------Physics------------\n";
		msg += physics_world->getDiagnostics();
		msg += "------------------------------\n";
	}

	if(terrain_system.nonNull() && do_terrain_diagnostics)
	{
		msg += "------------Terrain------------\n";
		msg += terrain_system->getDiagnostics();
		msg += "-------------------------------\n";
	}

	size_t num_lod_chunks = 0;
	if(this->world_state)
	{
		WorldStateLock lock(this->world_state->mutex);
		num_lod_chunks = this->world_state->lod_chunks.size();
	}

	if(animated_texture_manager)
		msg += animated_texture_manager->diagnostics();

	msg += "FPS: " + doubleToStringNDecimalPlaces(this->last_fps, 1) + "\n";
	msg += "main loop CPU time: " + doubleToStringNSigFigs(last_timerEvent_CPU_work_elapsed * 1000, 3) + " ms\n";
	msg += "main loop updateGL time: " + doubleToStringNSigFigs(last_updateGL_time * 1000, 3) + " ms\n";
	msg += "last_animated_tex_time: " + doubleToStringNSigFigs(this->last_animated_tex_time * 1000, 3) + " ms\n";
	msg += "last_num_gif_textures_processed: " + toString(last_num_gif_textures_processed) + "\n";
	msg += "last_num_mp4_textures_processed: " + toString(last_num_mp4_textures_processed) + "\n";
	msg += "last_eval_script_time: " + doubleToStringNSigFigs(last_eval_script_time * 1000, 3) + "ms\n";
	msg += "num obs with scripts: " + toString(obs_with_scripts.size()) + "\n";
	msg += "last_num_scripts_processed: " + toString(last_num_scripts_processed) + "\n";
	msg += "num LOD chunks: " + toString(num_lod_chunks) + "\n";
	msg += "download queue size: " + toString(this->download_queue.size()) + "\n";
	msg += "last_model_and_tex_loading_time: " + doubleToStringNSigFigs(this->last_model_and_tex_loading_time * 1000, 3) + " ms\n";
	msg += "load_item_queue: " + toString(load_item_queue.size()) + "\n";
	msg += "model_and_texture_loader_task_manager unfinished tasks: " + toString(model_and_texture_loader_task_manager.getNumUnfinishedTasks()) + "\n";
	msg += "model_loaded_messages_to_process: " + toString(model_loaded_messages_to_process.size()) + "\n";
	msg += "texture_loaded_messages_to_process: " + toString(texture_loaded_messages_to_process.size()) + "\n";
	msg += "stack allocator high water mark: " + getNiceByteSize(stack_allocator.highWaterMark()) + " / " + getNiceByteSize(stack_allocator.size()) + "\n";


#ifdef NDEBUG
	#ifdef BUILD_TESTS
	msg += "Build config: RelWithDebInfo (NDEBUG and BUILD_TESTS defined)\n";
	#else
	msg += "Build config: Release (NDEBUG defined and BUILD_TESTS not defined)\n";
	#endif
#else
	msg += "Build config: Debug (NDEBUG not defined)\n";
#endif

	if(opengl_engine.nonNull() && opengl_engine->mem_allocator.nonNull())
	{
		msg += "---------------OpenGL Mem Allocator-----------------\n";
		msg += std::string(opengl_engine->mem_allocator->getDiagnostics().c_str());
		msg += "---------------------------------------------\n";
	}
	if(worker_allocator)
	{
		msg += "---------------Worker Mem Allocator-----------------\n";
		msg += std::string(worker_allocator->getDiagnostics().c_str());
		msg += "---------------------------------------------\n";
	}

	if(texture_server.nonNull())
		msg += "texture_server total mem usage:         " + getNiceByteSize(this->texture_server->getTotalMemUsage()) + "\n";

	msg += this->mesh_manager.getDiagnostics();

	msg += "------------Resource Manager------------\n";
	msg += resource_manager->getDiagnostics();
	msg += "----------------------------------------\n";

	{
		msg += "\nAudio engine:\n";
		{
			Lock lock(audio_engine.mutex);
			msg += "Num audio obs: " + toString(audio_obs.size()) + "\n";
			msg += "Num active audio sources: " + toString(audio_engine.audio_sources.size()) + "\n";
		}
		/*msg += "Audio sources\n";
		Lock lock(audio_engine.mutex);
		for(auto it = audio_engine.audio_sources.begin(); it != audio_engine.audio_sources.end(); ++it)
		{
		msg += (*it)->debugname + "\n";
		}*/
	}

	/*{ // Proximity loader is currently disabled.
		msg += "Proximity loader:\n";
		msg += proximity_loader.getDiagnostics() + "\n";
	}*/

	

	return msg;
}


void GUIClient::diagnosticsSettingsChanged()
{
	const bool show_chunk_vis_aabb = ui_interface->diagnosticsVisible() && ui_interface->showLodChunksVisEnabled();

	WorldStateLock lock(this->world_state->mutex);
	
	for(auto it = world_state->lod_chunks.begin(); it != world_state->lod_chunks.end(); ++it)
	{
		LODChunk* chunk = it->second.ptr();
		
		if(show_chunk_vis_aabb)
		{
			if(!chunk->getMeshURL().empty()) // Don't visualise empty chunks
			{
				const Vec4f chunk_min = Vec4f(chunk->coords.x * chunk_w, chunk->coords.y * chunk_w, -20, 1);
				const Vec4f chunk_max = Vec4f((chunk->coords.x + 1) * chunk_w, (chunk->coords.y + 1) * chunk_w, 30, 1);

				chunk->diagnostics_gl_ob = opengl_engine->makeCuboidEdgeAABBObject(chunk_min, chunk_max, Colour4f(0.3f, 0.8f, 0.3f, 1.f));

				if(chunk->graphics_ob_in_engine)
					opengl_engine->addObject(chunk->diagnostics_gl_ob);
			}
		}
		else
		{
			if(chunk->diagnostics_gl_ob && chunk->graphics_ob_in_engine)
				opengl_engine->removeObject(chunk->diagnostics_gl_ob);
		}
	}

	for(auto it = vehicle_controllers.begin(); it != vehicle_controllers.end(); ++it)
	{
		VehiclePhysics* controller = it->second.ptr();
		controller->setDebugVisEnabled(ui_interface->showVehiclePhysicsVisEnabled(), *opengl_engine);
	}
}


void GUIClient::physicsObjectEnteredWater(PhysicsObject& physics_ob)
{
	const float ob_width = myMax(physics_ob.getAABBoxWS().axisLength(0), physics_ob.getAABBoxWS().axisLength(1));

	Vec4f foam_pos = physics_ob.getAABBoxWS().centroid();
	foam_pos[2] = physics_world->getWaterZ();

	terrain_decal_manager->addFoamDecal(foam_pos, ob_width, /*opacity=*/1.f, TerrainDecalManager::DecalType_ThickFoam);

	const float ob_speed = physics_world->getObjectLinearVelocity(physics_ob).length();

	// Add splash particle(s)
	for(int i=0; i<40; ++i)
	{
		Vec4f splash_pos = foam_pos + Vec4f(
			(-0.5f + rng.unitRandom()) * physics_ob.getAABBoxWS().axisLength(0),
			(-0.5f + rng.unitRandom()) * physics_ob.getAABBoxWS().axisLength(1),
			0,0
		);

		Particle particle;
		particle.pos = splash_pos;
		particle.area = 0.000001f;
		const float xy_spread = 1.f;
		const float splash_particle_speed = ob_speed * 0.5f;
		particle.vel = Vec4f(xy_spread * (-0.5f + rng.unitRandom()), xy_spread * (-0.5f + rng.unitRandom()), rng.unitRandom() * 2, 0) * splash_particle_speed;
		particle.colour = Colour3f(0.7f);
		particle.dopacity_dt = -0.6f;
		particle.particle_type = Particle::ParticleType_Foam;
		particle.theta = rng.unitRandom() * Maths::get2Pi<float>();
		particle.width = 0.5f;
		particle.dwidth_dt = splash_particle_speed * 0.5f;
		particle.die_when_hit_surface = true;
		particle_manager->addParticle(particle);
	}
}


// NOTE: called from threads other than the main thread, needs to be threadsafe
void GUIClient::contactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& contact_manifold)
{
	const Vec4f av_vel = toVec4fVec(inBody1.GetLinearVelocity() + inBody2.GetLinearVelocity()) * 0.5f;
	for(JPH::uint i=0; i<contact_manifold.mRelativeContactPointsOn1.size(); ++i)
	{
		Particle particle;
		particle.pos = toVec4fPos(contact_manifold.mBaseOffset + contact_manifold.mRelativeContactPointsOn1[i]);
		particle.area = 0.0001f;
		particle.vel = av_vel + Vec4f(-1 + 2*rng.unitRandom(),-1 + 2*rng.unitRandom(), rng.unitRandom() * 2, 0) * 1.f;
		particle.colour = Colour3f(0.6f, 0.4f, 0.3f);
		particle.width = 0.1f;

		// Add to particles_creation_buf to create in main thread later
		{
			Lock lock(particles_creation_buf_mutex);
			this->particles_creation_buf.push_back(particle);
		}
	}
}


// NOTE: called from threads other than the main thread, needs to be threadsafe
void GUIClient::contactPersisted(const JPH::Body &inBody1, const JPH::Body &inBody2, const JPH::ContactManifold& contact_manifold)
{
	const JPH::Vec3 relative_vel = inBody1.GetLinearVelocity() - inBody2.GetLinearVelocity();
	if(relative_vel.LengthSq() > Maths::square(3.0f))
	{
		const Vec4f av_vel = toVec4fVec(inBody1.GetLinearVelocity() + inBody2.GetLinearVelocity()) * 0.5f;
		for(JPH::uint i=0; i<contact_manifold.mRelativeContactPointsOn1.size(); ++i)
		{
			Particle particle;
			particle.pos = toVec4fPos(contact_manifold.mBaseOffset + contact_manifold.mRelativeContactPointsOn1[i]);
			particle.area = 0.0001f;
			particle.vel = av_vel + Vec4f(-1 + 2*rng.unitRandom(),-1 + 2*rng.unitRandom(), rng.unitRandom() * 2, 0) * 1.f;
			particle.colour = Colour3f(0.6f, 0.4f, 0.3f);
			particle.width = 0.1f;

			// Add to particles_creation_buf to create in main thread later
			{
				Lock lock(particles_creation_buf_mutex);
				this->particles_creation_buf.push_back(particle);
			}
		}
	}
}


Reference<TextureLoadedThreadMessage> GUIClient::allocTextureLoadedThreadMessage()
{
	glare::FastPoolAllocator::AllocResult res = this->texture_loaded_msg_allocator->alloc();
	Reference<TextureLoadedThreadMessage> msg = new (res.ptr) TextureLoadedThreadMessage();
	
	msg->allocator = texture_loaded_msg_allocator.ptr();
	msg->allocation_index = res.index;
	return msg;
}


static const double PHYSICS_ONWERSHIP_PERIOD = 10.0;

bool GUIClient::isObjectPhysicsOwnedBySelf(WorldObject& ob, double global_time) const
{
	return (ob.physics_owner_id == (uint32)this->client_avatar_uid.value()) && 
		((global_time - ob.last_physics_ownership_change_global_time) < PHYSICS_ONWERSHIP_PERIOD);
}


bool GUIClient::isObjectPhysicsOwnedByOther(WorldObject& ob, double global_time) const
{
	return (ob.physics_owner_id != std::numeric_limits<uint32>::max()) && // If the owner is a valid UID,
		(ob.physics_owner_id != (uint32)this->client_avatar_uid.value()) && // and the owner is not us,
		((global_time - ob.last_physics_ownership_change_global_time) < PHYSICS_ONWERSHIP_PERIOD); // And the ownership is still valid
}


bool GUIClient::isObjectPhysicsOwned(WorldObject& ob, double global_time)
{
	return (ob.physics_owner_id != std::numeric_limits<uint32>::max()) && // If the owner is a valid UID,
		((global_time - ob.last_physics_ownership_change_global_time) < PHYSICS_ONWERSHIP_PERIOD); // And the ownership is still valid
}


bool GUIClient::isObjectVehicleBeingDrivenByOther(WorldObject& ob)
{
	return doesVehicleHaveAvatarInSeat(ob, /*seat_index=*/0);
}


bool GUIClient::doesVehicleHaveAvatarInSeat(WorldObject& ob, uint32 seat_index) const
{
	// Iterate over all avatars (slow linear time of course!), see if any are in the drivers seat of this vehicle object.
	for(auto it = this->world_state->avatars.begin(); it != this->world_state->avatars.end(); ++it)
	{
		const Avatar* avatar = it->second.getPointer();
		if(avatar->entered_vehicle.ptr() == &ob && avatar->vehicle_seat_index == seat_index)
			return true;
	}
	return false;
}


// Destroy vehicle controllers that are controlling the world object 'ob', as Jolt hits asserts if physics object is swapped out under it.
void GUIClient::destroyVehiclePhysicsControllingObject(WorldObject* ob)
{
	vehicle_controllers.erase(ob);

	if(vehicle_controller_inside.nonNull() && vehicle_controller_inside->getControlledObject() == ob)
		vehicle_controller_inside = NULL;
}


void GUIClient::takePhysicsOwnershipOfObject(WorldObject& ob, double global_time)
{
	ob.physics_owner_id = (uint32)this->client_avatar_uid.value();
	ob.last_physics_ownership_change_global_time = global_time;


	// Send ObjectPhysicsOwnershipTaken message to server.
	MessageUtils::initPacket(scratch_packet, Protocol::ObjectPhysicsOwnershipTaken);
	writeToStream(ob.uid, scratch_packet);
	scratch_packet.writeUInt32((uint32)this->client_avatar_uid.value());
	scratch_packet.writeDouble(global_time);
	scratch_packet.writeUInt32(0); // Write flags.  Don't set renewal bit.
	enqueueMessageToSend(*this->client_thread, scratch_packet);
}


void GUIClient::checkRenewalOfPhysicsOwnershipOfObject(WorldObject& ob, double global_time)
{
	assert(isObjectPhysicsOwnedBySelf(ob, global_time)); // This should only be called when we already own the object

	if((global_time - ob.last_physics_ownership_change_global_time) > PHYSICS_ONWERSHIP_PERIOD / 2)
	{
		// conPrint("Renewing physics ownership of object");

		// Time to renew:
		ob.last_physics_ownership_change_global_time = global_time;

		// Send ObjectPhysicsOwnershipTaken message to server.
		MessageUtils::initPacket(scratch_packet, Protocol::ObjectPhysicsOwnershipTaken);
		writeToStream(ob.uid, scratch_packet);
		scratch_packet.writeUInt32((uint32)this->client_avatar_uid.value());
		scratch_packet.writeDouble(global_time);
		scratch_packet.writeUInt32(1); // Write flags.  1: renewal flag bit.
		enqueueMessageToSend(*this->client_thread, scratch_packet);
	}
}


// Update position of voxel edit markers (and add/remove them as needed) if we are editing voxels
void GUIClient::updateVoxelEditMarkers(const MouseCursorState& mouse_cursor_state)
{
	bool should_display_voxel_edit_marker = false;
	bool should_display_voxel_edit_face_marker = false;
	if(selectedObjectIsVoxelOb())
	{
		// NOTE: Stupid qt: QApplication::keyboardModifiers() doesn't update properly when just CTRL is pressed/released, without any other events.
		// So use GetAsyncKeyState on Windows, since it actually works.
#if defined(_WIN32)
		//const bool ctrl_key_down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
		//const bool alt_key_down = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0; // alt = VK_MENU
#else
		//const Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
		//const bool ctrl_key_down = (modifiers & Qt::ControlModifier) != 0;
		//const bool alt_key_down  = (modifiers & Qt::AltModifier)     != 0;
#endif

		if(mouse_cursor_state.ctrl_key_down || mouse_cursor_state.alt_key_down)
		{
			//const QPoint mouse_point = ui->glWidget->mapFromGlobal(QCursor::pos());

			const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
			const Vec4f dir = getDirForPixelTrace(mouse_cursor_state.cursor_pos.x, mouse_cursor_state.cursor_pos.y);
			RayTraceResult results;
			this->physics_world->traceRay(origin, dir, /*max_t=*/1.0e5f, /*ignore body id=*/JPH::BodyID(), results);
			if(results.hit_object)
			{
				const Vec4f hitpos_ws = origin + dir*results.hit_t;

				if(selected_ob.nonNull())
				{
					const bool have_edit_permissions = objectModificationAllowedWithMsg(*this->selected_ob, "edit");
					if(have_edit_permissions)
					{
						const float current_voxel_w = 1;

						Matrix4f ob_to_world = obToWorldMatrix(*selected_ob);
						Matrix4f world_to_ob = worldToObMatrix(*selected_ob);

						if(mouse_cursor_state.ctrl_key_down)
						{
							const Vec4f point_off_surface = hitpos_ws + results.hit_normal_ws * (current_voxel_w * 1.0e-3f);
							const Vec4f point_os = world_to_ob * point_off_surface;
							const Vec4f point_os_voxel_space = point_os / current_voxel_w;
							Vec3<int> voxel_indices((int)floor(point_os_voxel_space[0]), (int)floor(point_os_voxel_space[1]), (int)floor(point_os_voxel_space[2]));

							this->voxel_edit_marker->ob_to_world_matrix = ob_to_world * Matrix4f::translationMatrix(voxel_indices.x * current_voxel_w, voxel_indices.y * current_voxel_w, voxel_indices.z * current_voxel_w) *
								Matrix4f::uniformScaleMatrix(current_voxel_w);
							if(!voxel_edit_marker_in_engine)
							{
								opengl_engine->addObject(this->voxel_edit_marker);
								this->voxel_edit_marker_in_engine = true;
							}
							else
							{
								opengl_engine->updateObjectTransformData(*this->voxel_edit_marker);
							}
							
							// Work out transform matrix so that the voxel_edit_face_marker (a quad) is rotated and placed against the voxel face that the ray trace hit.
							// The quad lies on the z-plane in object space.
							const Vec4f normal_os = normalise(ob_to_world.transposeMult3Vector(results.hit_normal_ws));
							const float off_surf_nudge = 0.01f;
							Matrix4f m;
							if(fabs(normal_os[0]) > fabs(normal_os[1]) && fabs(normal_os[0]) > fabs(normal_os[2])) // If largest magnitude component is x:
							{
								if(normal_os[0] > 0) // if normal is +x:
									m = Matrix4f::translationMatrix(off_surf_nudge, 0, 0)     * Matrix4f::rotationAroundYAxis(-Maths::pi_2<float>());
								else // else if normal is -x:
									m = Matrix4f::translationMatrix(1 - off_surf_nudge, 0, 0) * Matrix4f::rotationAroundYAxis(-Maths::pi_2<float>());
							}
							else if(fabs(normal_os[1]) > fabs(normal_os[0]) && fabs(normal_os[1]) > fabs(normal_os[2])) // If largest magnitude component is y:
							{
								if(normal_os[1] > 0) // if normal is +y:
									m = Matrix4f::translationMatrix(0, off_surf_nudge, 0)     * Matrix4f::rotationAroundXAxis(Maths::pi_2<float>());
								else // else if normal is -y:
									m = Matrix4f::translationMatrix(0, 1 - off_surf_nudge, 0) * Matrix4f::rotationAroundXAxis(Maths::pi_2<float>());
							}
							else // Else if largest magnitude component is z:
							{
								if(normal_os[2] > 0) // if normal is +Z:
									m = Matrix4f::translationMatrix(0, 0, off_surf_nudge);
								else // else if normal is -z:
									m = Matrix4f::translationMatrix(0, 0, 1 - off_surf_nudge);
							}
							this->voxel_edit_face_marker->ob_to_world_matrix = ob_to_world * Matrix4f::translationMatrix(voxel_indices.x * current_voxel_w, voxel_indices.y * current_voxel_w, voxel_indices.z * current_voxel_w) *
								Matrix4f::uniformScaleMatrix(current_voxel_w) * m;

							if(!voxel_edit_face_marker_in_engine)
							{
								opengl_engine->addObject(this->voxel_edit_face_marker);
								voxel_edit_face_marker_in_engine = true;
							}
							else
							{
								opengl_engine->updateObjectTransformData(*this->voxel_edit_face_marker);
							}

							should_display_voxel_edit_marker = true;
							should_display_voxel_edit_face_marker = true;

							this->voxel_edit_marker->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(0.1, 0.9, 0.2));
							opengl_engine->objectMaterialsUpdated(*this->voxel_edit_marker);

						}
						else if(mouse_cursor_state.alt_key_down)
						{
							const Vec4f point_under_surface = hitpos_ws - results.hit_normal_ws * (current_voxel_w * 1.0e-3f);
							const Vec4f point_os = world_to_ob * point_under_surface;
							const Vec4f point_os_voxel_space = point_os / current_voxel_w;
							Vec3<int> voxel_indices((int)floor(point_os_voxel_space[0]), (int)floor(point_os_voxel_space[1]), (int)floor(point_os_voxel_space[2]));

							
							const float extra_voxel_w = 0.01f; // Make scale a bit bigger so can be seen around target voxel.
							this->voxel_edit_marker->ob_to_world_matrix = ob_to_world * Matrix4f::translationMatrix(
								voxel_indices.x * current_voxel_w - current_voxel_w * extra_voxel_w,
								voxel_indices.y * current_voxel_w - current_voxel_w * extra_voxel_w,
								voxel_indices.z * current_voxel_w - current_voxel_w * extra_voxel_w
								) *
								Matrix4f::uniformScaleMatrix(current_voxel_w * (1 + extra_voxel_w*2));
							if(!voxel_edit_marker_in_engine)
							{
								opengl_engine->addObject(this->voxel_edit_marker);
								this->voxel_edit_marker_in_engine = true;
							}
							else
							{
								opengl_engine->updateObjectTransformData(*this->voxel_edit_marker);
							}

							should_display_voxel_edit_marker = true;
							
							this->voxel_edit_marker->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(0.9, 0.1, 0.1));
							opengl_engine->objectMaterialsUpdated(*this->voxel_edit_marker);
						}
					}
				}
			}
		}
	}

	// Remove edit markers from 3d engine if they shouldn't be displayed currently.
	if(voxel_edit_marker_in_engine && !should_display_voxel_edit_marker)
	{
		opengl_engine->removeObject(this->voxel_edit_marker);
		voxel_edit_marker_in_engine = false;
	}
	if(voxel_edit_face_marker_in_engine && !should_display_voxel_edit_face_marker)
	{
		opengl_engine->removeObject(this->voxel_edit_face_marker);
		voxel_edit_face_marker_in_engine = false;
	}
}


// Returns true if this user has permissions to create an object at new_ob_pos
bool GUIClient::haveParcelObjectCreatePermissions(const Vec3d& new_ob_pos, bool& ob_pos_in_parcel_out)
{
	ob_pos_in_parcel_out = false;

	if(isGodUser(this->logged_in_user_id))
	{
		ob_pos_in_parcel_out = true; // Just treat as in parcel.
		return true;
	}

	// If this is the world of the user:
	if(this->connected_world_details.owner_id == this->logged_in_user_id)
	{
		ob_pos_in_parcel_out = true; // Just treat as in parcel.
		return true;
	}

	// World-gardeners can create objects anywhere.
	if(BitUtils::isBitSet(logged_in_user_flags, User::WORLD_GARDENER_FLAG))
	{
		ob_pos_in_parcel_out = true; // Just treat as in parcel.
		return true;
	}

	// See if the user is in a parcel that they have write permissions for.
	// For now just do a linear scan over parcels
	bool have_creation_perms = false;
	{
		Lock lock(world_state->mutex);
		for(auto& it : world_state->parcels)
		{
			const Parcel* parcel = it.second.ptr();

			if(parcel->pointInParcel(new_ob_pos))
			{
				ob_pos_in_parcel_out = true;

				// Is this user one of the writers or admins for this parcel?
				if(parcel->userHasWritePerms(this->logged_in_user_id))
				{
					have_creation_perms = true;
					break;
				}
				else
				{
					//showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
				}
			}
		}
	}

	//if(!in_parcel)
	//	showErrorNotification("You can only create objects in a parcel that you have write permissions for.");

	return have_creation_perms;
}


bool GUIClient::haveObjectWritePermissions(const WorldObject& ob, const js::AABBox& new_aabb_ws, bool& ob_pos_in_parcel_out)
{
	ob_pos_in_parcel_out = false;

	if(isGodUser(this->logged_in_user_id))
	{
		ob_pos_in_parcel_out = true; // Just treat as in parcel.
		return true;
	}

	// If this is the world of the user:
	if(this->connected_world_details.owner_id == this->logged_in_user_id)
	{
		ob_pos_in_parcel_out = true; // Just treat as in parcel.
		return true;
	}

	// World-gardeners can move objects that they created anywhere.
	if(BitUtils::isBitSet(logged_in_user_flags, User::WORLD_GARDENER_FLAG) &&
		(ob.creator_id == this->logged_in_user_id))
	{
		ob_pos_in_parcel_out = true; // Just treat as in parcel.
		return true;
	}

	// We have write permissions for the current transform iff we have write permissions
	// for every parcel the AABB of the object intersects.
	bool have_creation_perms = true;
	{
		Lock lock(world_state->mutex);
		for(auto& it : world_state->parcels)
		{
			const Parcel* parcel = it.second.ptr();

			if(parcel->AABBIntersectsParcel(new_aabb_ws))
			{
				ob_pos_in_parcel_out = true;

				// Is this user one of the writers or admins for this parcel?
				if(!parcel->userHasWritePerms(this->logged_in_user_id))
					have_creation_perms = false;
			}
		}
	}

	return have_creation_perms;
}


// If the object was not in a parcel with write permissions at all, returns false.
// If the object can not be made to fit in the current parcel, returns false.
// new_ob_pos_out is set to new, clamped position.
bool GUIClient::clampObjectPositionToParcelForNewTransform(const WorldObject& ob, GLObjectRef& opengl_ob, const Vec3d& old_ob_pos,
	const Matrix4f& tentative_to_world_matrix,
	js::Vector<EdgeMarker, 16>& edge_markers_out, Vec3d& new_ob_pos_out)
{
	edge_markers_out.resize(0);
	bool have_creation_perms = false;
	Vec3d parcel_aabb_min;
	Vec3d parcel_aabb_max;

	if(isGodUser(this->logged_in_user_id) || (this->connected_world_details.owner_id == this->logged_in_user_id) || // If god user, or if this is the world of the user:
		(BitUtils::isBitSet(logged_in_user_flags, User::WORLD_GARDENER_FLAG) && (ob.creator_id == this->logged_in_user_id)) || // Or if the user is a world-gardener, and they created this object
		((ob.creator_id == this->logged_in_user_id) && BitUtils::isBitSet(ob.flags, WorldObject::SUMMONED_FLAG))) // Or if the user created this object by summoning it (e.g. this is their bike or hovercar).
	{
		const Vec4f newpos = tentative_to_world_matrix.getColumn(3);
		new_ob_pos_out = Vec3d(newpos[0], newpos[1], newpos[2]); // New object position
		return true;
	}

	// Work out what parcel the object is in currently (e.g. what parcel old_ob_pos is in)
	{
		const Parcel* ob_parcel = NULL;
		Lock lock(world_state->mutex);
		for(auto& it : world_state->parcels)
		{
			const Parcel* parcel = it.second.ptr();

			if(parcel->pointInParcel(old_ob_pos))
			{
				// Is this user one of the writers or admins for this parcel?

				if(parcel->userHasWritePerms(this->logged_in_user_id))
				{
					have_creation_perms = true;
					ob_parcel = parcel;
					parcel_aabb_min = parcel->aabb_min;
					parcel_aabb_max = parcel->aabb_max;
					break;
				}
			}
		}

		// Work out if there are any adjacent parcels to ob_parcel.
		if(ob_parcel)
		{
			for(auto& it : world_state->parcels)
			{
				const Parcel* parcel = it.second.ptr();
				if(parcel->isAdjacentTo(*ob_parcel) && parcel->userHasWritePerms(this->logged_in_user_id))
				{
					// Enlarge AABB to include parcel AABB
					parcel_aabb_min = min(parcel_aabb_min, parcel->aabb_min);
					parcel_aabb_max = max(parcel_aabb_max, parcel->aabb_max);
				}
			}
		}
	} // End lock scope

	if(have_creation_perms)
	{
		// Get the AABB corresponding to tentative_new_ob_pos.
		const js::AABBox ten_new_aabb_ws = opengl_engine->getAABBWSForObjectWithTransform(*opengl_ob, 
			tentative_to_world_matrix);

		// Constrain tentative ob pos so that the tentative new aabb lies in parcel.
		// This will have no effect if tentative new AABB is already in the parcel.
		Vec4f dpos(0.0f);
		if(ten_new_aabb_ws.min_[0] < (float)parcel_aabb_min.x) dpos[0] += ((float)parcel_aabb_min.x - ten_new_aabb_ws.min_[0]);
		if(ten_new_aabb_ws.min_[1] < (float)parcel_aabb_min.y) dpos[1] += ((float)parcel_aabb_min.y - ten_new_aabb_ws.min_[1]);
		if(ten_new_aabb_ws.min_[2] < (float)parcel_aabb_min.z) dpos[2] += ((float)parcel_aabb_min.z - ten_new_aabb_ws.min_[2]);
			
		if(ten_new_aabb_ws.max_[0] > (float)parcel_aabb_max.x) dpos[0] += ((float)parcel_aabb_max.x - ten_new_aabb_ws.max_[0]);
		if(ten_new_aabb_ws.max_[1] > (float)parcel_aabb_max.y) dpos[1] += ((float)parcel_aabb_max.y - ten_new_aabb_ws.max_[1]);
		if(ten_new_aabb_ws.max_[2] > (float)parcel_aabb_max.z) dpos[2] += ((float)parcel_aabb_max.z - ten_new_aabb_ws.max_[2]);

		const js::AABBox new_aabb(ten_new_aabb_ws.min_ + dpos, ten_new_aabb_ws.max_ + dpos);
		if(!Parcel::AABBInParcelBounds(new_aabb, parcel_aabb_min, parcel_aabb_max))
			return false; // We can't fit object with new transform in parcel AABB.

		// Compute positions and normals of edge markers - visual aids to show how an object is constrained to a parcel.
		// Put them on the sides of the constrained AABB.
		const Vec4f cen = new_aabb.centroid();
		const Vec4f diff = new_aabb.max_ - new_aabb.min_;
		const Vec4f scales(myMax(diff[1], diff[2])*0.5f, myMax(diff[0], diff[1])*0.5f, myMax(diff[0], diff[1])*0.5f, 0.f);
		if(dpos[0] > 0) edge_markers_out.push_back(EdgeMarker(Vec4f(new_aabb.min_[0], cen[1], cen[2], 1.f), Vec4f(1,0,0,0), scales[0]));
		if(dpos[1] > 0) edge_markers_out.push_back(EdgeMarker(Vec4f(cen[0], new_aabb.min_[1], cen[2], 1.f), Vec4f(0,1,0,0), scales[1]));
		if(dpos[2] > 0) edge_markers_out.push_back(EdgeMarker(Vec4f(cen[0], cen[1], new_aabb.min_[2], 1.f), Vec4f(0,0,1,0), scales[2]));

		if(dpos[0] < 0) edge_markers_out.push_back(EdgeMarker(Vec4f(new_aabb.max_[0], cen[1], cen[2], 1.f), Vec4f(-1,0,0,0), scales[0]));
		if(dpos[1] < 0) edge_markers_out.push_back(EdgeMarker(Vec4f(cen[0], new_aabb.max_[1], cen[2], 1.f), Vec4f(0,-1,0,0), scales[1]));
		if(dpos[2] < 0) edge_markers_out.push_back(EdgeMarker(Vec4f(cen[0], cen[1], new_aabb.max_[2], 1.f), Vec4f(0,0,-1,0), scales[2]));

		const Vec4f newpos = tentative_to_world_matrix.getColumn(3) + dpos;
		new_ob_pos_out = Vec3d(newpos[0], newpos[1], newpos[2]); // New object position
		return true;
	}
	else
		return false;
}


// Set material COLOUR_TEX_HAS_ALPHA_FLAG and MIN_LOD_LEVEL_IS_NEGATIVE_1 as applicable
void GUIClient::setMaterialFlagsForObject(WorldObject* ob)
{
	for(size_t z=0; z<ob->materials.size(); ++z)
	{
		WorldMaterial* mat = ob->materials[z].ptr();
		if(mat)
		{
			if(!mat->colour_texture_url.empty())
			{
				if(FileUtils::fileExists(mat->colour_texture_url)) // If this was a local path:
				{
					try
					{
						const std::string local_tex_path = toStdString(mat->colour_texture_url);
						Map2DRef tex = texture_server->getTexForPath(base_dir_path, local_tex_path); // Get from texture server so it's cached.

						const bool has_alpha = LODGeneration::textureHasAlphaChannel(local_tex_path, tex);
						BitUtils::setOrZeroBit(mat->flags, WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG, has_alpha);

						// If the texture is very high res, set minimum texture lod level to -1.  Lod level 0 will be the texture resized to 1024x1024 or below.
						const bool is_hi_res = tex->getMapWidth() > 1024 || tex->getMapHeight() > 1024;
						BitUtils::setOrZeroBit(mat->flags, WorldMaterial::MIN_LOD_LEVEL_IS_NEGATIVE_1, is_hi_res);
					}
					catch(glare::Exception& e)
					{
						conPrint("Error while trying to load texture: " + e.what());
					}
				}
			}
		}
	}
}


// Create a voxel or generic (mesh) object on server.
// Convert mesh to bmesh if needed, Generate mesh LODs if needed.
// Copy files to resource dir if not there already.
// Generate referenced texture LODs.
// Send CreateObject message to server
// Throws glare::Exception on failure.
void GUIClient::createObject(const std::string& mesh_path, BatchedMeshRef loaded_mesh, bool loaded_mesh_is_image_cube,
	const glare::AllocatorVector<Voxel, 16>& decompressed_voxels, const Vec3d& ob_pos, const Vec3f& scale, const Vec3f& axis, float angle, const std::vector<WorldMaterialRef>& materials)
{
	WorldObjectRef new_world_object = new WorldObject();

	js::AABBox aabb_os;
	if(loaded_mesh.nonNull())
	{
		// If the user wants to load a mesh that is not a bmesh file already, convert it to bmesh.
		std::string bmesh_disk_path;
		if(!hasExtension(mesh_path, "bmesh")) 
		{
			// Save as bmesh in temp location
			bmesh_disk_path = PlatformUtils::getTempDirPath() + "/temp.bmesh";

			BatchedMesh::WriteOptions write_options;
			write_options.compression_level = 9; // Use a somewhat high compression level, as this mesh is likely to be read many times, and only encoded here.
			// TODO: show 'processing...' dialog while it compresses and saves?
			loaded_mesh->writeToFile(bmesh_disk_path, write_options);
		}
		else
		{
			bmesh_disk_path = mesh_path;
		}

		// Compute hash over model
		const uint64 model_hash = FileChecksum::fileChecksum(bmesh_disk_path);

		const std::string original_filename = loaded_mesh_is_image_cube ? "image_cube" : FileUtils::getFilename(mesh_path); // Use the original filename, not 'temp.bmesh'.
		const URLString mesh_URL = ResourceManager::URLForNameAndExtensionAndHash(original_filename, ::getExtension(bmesh_disk_path), model_hash); // Make a URL like "projectdog_png_5624080605163579508.png"

		// Copy model to local resources dir if not already there.  UploadResourceThread will read from here.
		if(!this->resource_manager->isFileForURLPresent(mesh_URL))
			this->resource_manager->copyLocalFileToResourceDir(bmesh_disk_path, mesh_URL);

		new_world_object->model_url = mesh_URL;

		aabb_os = loaded_mesh->aabb_os;

		new_world_object->max_model_lod_level = (loaded_mesh->numVerts() <= 4 * 6) ? 0 : 2; // If this is a very small model (e.g. a cuboid), don't generate LOD versions of it.
	}
	else
	{
		// We loaded a voxel model.
		new_world_object->getDecompressedVoxels() = decompressed_voxels;
		new_world_object->compressVoxels();
		new_world_object->object_type = WorldObject::ObjectType_VoxelGroup;
		new_world_object->max_model_lod_level = (new_world_object->getDecompressedVoxels().size() > 256) ? 2 : 0;

		aabb_os = new_world_object->getDecompressedVoxelGroup().getAABB();
	}

	new_world_object->uid = UID(0); // A new UID will be assigned by server
	new_world_object->materials = materials;
	new_world_object->pos = ob_pos;
	new_world_object->axis = axis;
	new_world_object->angle = angle;
	new_world_object->scale = scale;
	new_world_object->setAABBOS(aabb_os);

	setMaterialFlagsForObject(new_world_object.ptr());


	// Copy all dependencies (textures etc..) to resources dir.  UploadResourceThread will read from here.
	WorldObject::GetDependencyOptions options;
	options.use_basis = false; // Server will want the original non-basis textures.
	options.include_lightmaps = false;
	options.get_optimised_mesh = false; // Server will want the original unoptimised mesh.
	DependencyURLSet paths;
	new_world_object->getDependencyURLSetBaseLevel(options, paths);
	for(auto it = paths.begin(); it != paths.end(); ++it)
	{
		const URLString path = it->URL;
		if(FileUtils::fileExists(path))
		{
			const uint64 hash = FileChecksum::fileChecksum(path);
			const URLString resource_URL = ResourceManager::URLForPathAndHash(toStdString(path), hash);
			this->resource_manager->copyLocalFileToResourceDir(toStdString(path), resource_URL);
		}
	}

	// Convert texture paths on the object to URLs
	new_world_object->convertLocalPathsToURLS(*this->resource_manager);

	//if(!task_manager)
	//	task_manager = new glare::TaskManager("GUIClient general task manager", myClamp<size_t>(PlatformUtils::getNumLogicalProcessors() / 2, 1, 8)); // Currently just used for LODGeneration::generateLODTexturesForMaterialsIfNotPresent().

	// Generate LOD textures for materials, if not already present on disk.
	// Note that server will also generate LOD textures, however the client may want to display a particular LOD texture immediately, so generate on the client as well.
	// 
	// LODGeneration::generateLODTexturesForMaterialsIfNotPresent(new_world_object->materials, *resource_manager, *task_manager);
	//
	// NOTE: disabled while adding basisu stuff.  For now we will just wait for the server to generate the needed files and then download them.

	// Send CreateObject message to server
	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		new_world_object->writeToNetworkStream(scratch_packet);

		enqueueMessageToSend(*this->client_thread, scratch_packet);
	}
}


static inline bool transformsEqual(const WorldObject& a, const WorldObject& b)
{
	return a.pos == b.pos &&
		a.scale == b.scale &&
		a.axis == b.axis && 
		a.angle == b.angle;
}


// NOTE: Will probably be called from not the main thread!!!
void GUIClient::createObjectLoadedFromXML(WorldObjectRef new_world_object, PrintOutput& use_print_output)
{
	if(!this->world_state)
		return;

	if(connection_state != ServerConnectionState_Connected)
	{
		use_print_output.print("Can't create object while not connected to server.");
		return;
	}

	if(new_world_object->object_type == WorldObject::ObjectType_Generic)
	{
		BatchedMeshRef batched_mesh;
	
		if(FileUtils::fileExists(new_world_object->model_url)) // If model_url is a local file path:
		{
			const std::string original_mesh_path = toStdString(new_world_object->model_url);

			// If the user wants to load a mesh that is not a bmesh file already, convert it to bmesh.
			std::string bmesh_disk_path;
			if(!hasExtension(original_mesh_path, "bmesh")) 
			{
				// Save as bmesh in temp location
				bmesh_disk_path = PlatformUtils::getTempDirPath() + "/temp.bmesh";

				// Use makeGLObjectForModelFile() which will load materials too, unpack GLBs etc.  We don't actually need the gl object.
				ModelLoading::MakeGLObjectResults results;
				ModelLoading::makeGLObjectForModelFile(*opengl_engine, *opengl_engine->vert_buf_allocator, worker_allocator.ptr(), original_mesh_path, /*do_opengl_stuff=*/false, results);

				batched_mesh = results.batched_mesh;
				new_world_object->materials = results.materials;
				use_print_output.print("Using materials from '" + original_mesh_path + "' instead of from XML.");

				BatchedMesh::WriteOptions write_options;
				write_options.compression_level = 9; // Use a somewhat high compression level, as this mesh is likely to be read many times, and only encoded here.
				
				batched_mesh->writeToFile(bmesh_disk_path, write_options); // TODO: show 'processing...' dialog while it compresses and saves?
			}
			else
			{
				batched_mesh = BatchedMesh::readFromFile(original_mesh_path, /*mem allocator=*/NULL);
				bmesh_disk_path = original_mesh_path;
			}

			// Compute hash over model
			const uint64 model_hash = FileChecksum::fileChecksum(bmesh_disk_path);

			const std::string original_filename = FileUtils::getFilename(original_mesh_path); // Use the original filename, not 'temp.bmesh'.
			const URLString mesh_URL = ResourceManager::URLForNameAndExtensionAndHash(::eatExtension(original_filename), "bmesh", model_hash); // Make a URL like "house_5624080605163579508.bmesh"

			// Copy model to local resources dir if not already there.  UploadResourceThread will read from here.
			if(!this->resource_manager->isFileForURLPresent(mesh_URL))
				this->resource_manager->copyLocalFileToResourceDir(bmesh_disk_path, mesh_URL);

			new_world_object->model_url = mesh_URL;
		}
		else // else if model URL is an actual Substrata URL:
		{
			// We need to download it if it's not present already.
			if(!resource_manager->isFileForURLPresent(new_world_object->model_url))
			{
				use_print_output.print("Downloading model '" + toStdString(new_world_object->model_url) + "'...");
				DownloadingResourceInfo info;
				// NOTE: don't have valid object UID here.  
				// Just hack isDownloadingResourceCurrentlyNeeded() to return true by setting used_by_other.
				info.used_by_other = true;
				startDownloadingResource(new_world_object->model_url, this->cam_controller.getPosition().toVec4fPoint(), 1.f, DownloadingResourceInfo());
				
				// Wait until downloaded...
				Timer timer;
				while(!resource_manager->isFileForURLPresent(new_world_object->model_url))
				{
					if(timer.elapsed() > 30)
						throw glare::Exception("Failed to download model resource from URL '" + toStdString(new_world_object->model_url) + "' in 30 s, abandoning object creation. (ob UID: " + new_world_object->uid.toString() + ")");
					if(resource_manager->isInDownloadFailedURLs(new_world_object->model_url))
						throw glare::Exception("Failed to download model resource from URL '" + toStdString(new_world_object->model_url) + "', abandoning object creation. (ob UID: " + new_world_object->uid.toString() + ")");

					PlatformUtils::Sleep(5);
				}
			}


			// Resource is present locally.
			// Load mesh from disk.  TODO: cache loaded meshes for this method
			batched_mesh = BatchedMesh::readFromFile(resource_manager->pathForURL(new_world_object->model_url), /*mem allocator=*/NULL); // NOTE: assuming URLs are bmeshes.
		}

		new_world_object->setAABBOS(batched_mesh->aabb_os);
		new_world_object->max_model_lod_level = (batched_mesh->numVerts() <= 4 * 6) ? 0 : 2; // If this is a very small model (e.g. a cuboid), don't generate LOD versions of it.
	}

	// Search for an existing object with the same model url or voxel group and transform.
	// If found, don't create a new object, as we assume the user doesn't want duplicate objects with the same transform.
	{
		WorldStateLock lock(this->world_state->mutex);
		for(auto it = world_state->objects.valuesBegin(); it != world_state->objects.valuesEnd(); ++it)
		{
			const WorldObject* ob = it.getValue().ptr();
			if(ob->object_type == new_world_object->object_type)
			{
				if(ob->object_type == WorldObject::ObjectType_Generic)
				{
					if(transformsEqual(*ob, *new_world_object) && (ob->model_url == new_world_object->model_url))
					{
						use_print_output.print("An object with this model_url ('" + toStdString(new_world_object->model_url) + "') and position " + new_world_object->pos.toStringMaxNDecimalPlaces(2) + " is already present in world, not adding.");
						return;
					}
				}
				else if(ob->object_type == WorldObject::ObjectType_VoxelGroup)
				{
					if(transformsEqual(*ob, *new_world_object) && 
						ob->getCompressedVoxels() &&
						new_world_object->getCompressedVoxels() && 
						(*ob->getCompressedVoxels() == *new_world_object->getCompressedVoxels()))
					{
						use_print_output.print("An object with this voxel group and position " + new_world_object->pos.toStringMaxNDecimalPlaces(2) + " is already present in world, not adding.");
						return;
					}
				}
				else
				{
					// For other object types (spotlights etc.) just check position for now.
					if(transformsEqual(*ob, *new_world_object))
					{
						use_print_output.print("An object with this position " + new_world_object->pos.toStringMaxNDecimalPlaces(2) + " is already present in world, not adding.");
						return;
					}
				}
			}
		}
	}



	setMaterialFlagsForObject(new_world_object.ptr());

	{
		// Copy all dependencies (textures etc..) to resources dir.  UploadResourceThread will read from here.
		WorldObject::GetDependencyOptions options;
		options.use_basis = false; // Server will want the original non-basis textures.
		options.include_lightmaps = false;
		options.get_optimised_mesh = false; // Server will want the original unoptimised mesh.
		DependencyURLSet paths;
		new_world_object->getDependencyURLSetBaseLevel(options, paths);
		for(auto it = paths.begin(); it != paths.end(); ++it)
		{
			const URLString path_or_URL = it->URL;
			if(FileUtils::fileExists(path_or_URL)) // If the URL is a local path:
			{
				const URLString path = path_or_URL;
				const uint64 hash = FileChecksum::fileChecksum(path);
				const URLString resource_URL = ResourceManager::URLForPathAndHash(toStdString(path), hash);
				this->resource_manager->copyLocalFileToResourceDir(toStdString(path), resource_URL);
			}
		}
	}

	// Convert texture paths on the object to URLs
	new_world_object->convertLocalPathsToURLS(*this->resource_manager);

	//if(!task_manager)
	//	task_manager = new glare::TaskManager("GUIClient general task manager", myClamp<size_t>(PlatformUtils::getNumLogicalProcessors() / 2, 1, 8)), // Currently just used for LODGeneration::generateLODTexturesForMaterialsIfNotPresent().
	//glare::TaskManager temp_task_manager("GUIClient temp task manager", myClamp<size_t>(PlatformUtils::getNumLogicalProcessors() / 2, 1, 8));

	// Generate LOD textures for materials, if not already present on disk.
	// Note that server will also generate LOD textures, however the client may want to display a particular LOD texture immediately, so generate on the client as well.
	//
	// LODGeneration::generateLODTexturesForMaterialsIfNotPresent(new_world_object->materials, *resource_manager, temp_task_manager);
	//
	// NOTE: disabled while adding basisu stuff.  For now we will just wait for the server to generate the needed files and then download them.

	// Send CreateObject message to server
	{
		use_print_output.print("Sending Create Object message to server..");
		SocketBufferOutStream temp_packet(SocketBufferOutStream::DontUseNetworkByteOrder);

		MessageUtils::initPacket(temp_packet, Protocol::CreateObject);
		new_world_object->writeToNetworkStream(temp_packet);

		enqueueMessageToSend(*this->client_thread, temp_packet);
	}
}


bool GUIClient::selectedObjectIsVoxelOb() const
{
	return this->selected_ob.nonNull() && this->selected_ob->object_type == WorldObject::ObjectType_VoxelGroup;
}


bool GUIClient::isObjectWithPosition(const Vec3d& pos)
{
	Lock lock(world_state->mutex);

	for(auto it = world_state->objects.valuesBegin(); it != world_state->objects.valuesEnd(); ++it)
	{
		WorldObject* ob = it.getValue().ptr();
		if(ob->pos == pos)
			return true;
	}
	
	return false;
}


void GUIClient::addParcelObjects()
{
	if(this->world_state.isNull())
		return;

	// Iterate over all parcels, add models for them
	Lock lock(this->world_state->mutex);
	try
	{
		for(auto& it : this->world_state->parcels)
		{
			Parcel* parcel = it.second.getPointer();
			if(parcel->opengl_engine_ob.isNull())
			{
				// Make OpenGL model for parcel:
				const bool write_perms = parcel->userHasWritePerms(this->logged_in_user_id);

				bool use_write_perms = write_perms;
				if(ui_interface->inScreenshotTakingMode()) // If we are in screenshot-taking mode, don't highlight writable parcels.
					use_write_perms = false;

				parcel->opengl_engine_ob = parcel->makeOpenGLObject(opengl_engine, use_write_perms);
				parcel->opengl_engine_ob->materials[0].shader_prog = this->parcel_shader_prog;
				parcel->opengl_engine_ob->materials[0].auto_assign_shader = false;
				opengl_engine->addObject(parcel->opengl_engine_ob); // Add to engine

				// Make physics object for parcel:
				assert(parcel->physics_object.isNull());
				parcel->physics_object = parcel->makePhysicsObject(this->unit_cube_shape);
				physics_world->addObject(parcel->physics_object);
			}
		}
	}
	catch(glare::Exception& e)
	{
		print("Error while updating parcel graphics: " + e.what());
	}
}


void GUIClient::removeParcelObjects()
{
	// Iterate over all parcels, add models for them
	try
	{
		// Iterate over all parcels, remove models for them.
		Lock lock(this->world_state->mutex);
		for(auto& it : this->world_state->parcels)
		{
			Parcel* parcel = it.second.getPointer();
			if(parcel->opengl_engine_ob.nonNull())
			{
				opengl_engine->removeObject(parcel->opengl_engine_ob);
				parcel->opengl_engine_ob = NULL;
			}

			if(parcel->physics_object.nonNull())
			{
				physics_world->removeObject(parcel->physics_object);
				parcel->physics_object = NULL;
			}
		}
	}
	catch(glare::Exception& e)
	{
		print("Error while updating parcel graphics: " + e.what());
	}
}


void GUIClient::recolourParcelsForLoggedInState()
{
	Lock lock(this->world_state->mutex);
	for(auto& it : this->world_state->parcels)
	{
		Parcel* parcel = it.second.getPointer();
		if(parcel->opengl_engine_ob.nonNull())
		{
			const bool write_perms = parcel->userHasWritePerms(this->logged_in_user_id);

			bool use_write_perms = write_perms;
			if(ui_interface->inScreenshotTakingMode()) // If we are in screenshot-taking mode, don't highlight writable parcels.
				use_write_perms = false;

			parcel->setColourForPerms(use_write_perms);
		}
	}
}


void GUIClient::thirdPersonCameraToggled(bool enabled)
{
	this->cam_controller.setThirdPersonEnabled(enabled);

	// Add or remove our avatar opengl model.
	if(this->cam_controller.thirdPersonEnabled()) // If we just enabled third person camera:
	{
		// Add our avatar model. Do this by marking it as dirty.
		Lock lock(this->world_state->mutex);
		auto res = this->world_state->avatars.find(this->client_avatar_uid);
		if(res != this->world_state->avatars.end())
		{
			Avatar* avatar = res->second.getPointer();
			avatar->other_dirty = true;
		}
	}
	else
	{
		// Remove our avatar model
		Lock lock(this->world_state->mutex);
		auto res = this->world_state->avatars.find(this->client_avatar_uid);
		if(res != this->world_state->avatars.end())
		{
			Avatar* avatar = res->second.getPointer();

			avatar->graphics.destroy(*opengl_engine);

			// Remove nametag OpenGL object
			if(avatar->nametag_gl_ob.nonNull())
				opengl_engine->removeObject(avatar->nametag_gl_ob);
			avatar->nametag_gl_ob = NULL;
			if(avatar->speaker_gl_ob.nonNull())
				opengl_engine->removeObject(avatar->speaker_gl_ob);
			avatar->speaker_gl_ob = NULL;
		}

		// Turn off selfie mode if it was enabled.
		//gesture_ui.turnOffSelfieMode(); // calls setSelfieModeEnabled(false);
	}
}


void GUIClient::applyUndoOrRedoObject(const WorldObjectRef& restored_ob)
{
	if(restored_ob.nonNull())
	{
		{
			Lock lock(this->world_state->mutex);

			WorldObjectRef in_world_ob;
			bool voxels_different = false;

			UID use_uid;
			if(recreated_ob_uid.find(restored_ob->uid) == recreated_ob_uid.end())
				use_uid = restored_ob->uid;
			else
			{
				use_uid = recreated_ob_uid[restored_ob->uid];
				//conPrint("Using recreated UID of " + use_uid.toString());
			}


			auto res = this->world_state->objects.find(use_uid);
			if(res != this->world_state->objects.end())
			{
				in_world_ob = res.getValue().ptr();

				const bool voxels_same = 
					in_world_ob->getCompressedVoxels() &&
					restored_ob->getCompressedVoxels() && 
					(*in_world_ob->getCompressedVoxels() == *restored_ob->getCompressedVoxels());

				voxels_different = !voxels_same;

				in_world_ob->copyNetworkStateFrom(*restored_ob);

				in_world_ob->decompressVoxels();
			}

			if(in_world_ob.nonNull())
			{
				if(voxels_different)
					updateObjectModelForChangedDecompressedVoxels(in_world_ob);
				else
				{
					GLObjectRef opengl_ob = in_world_ob->opengl_engine_ob;
					if(opengl_ob.nonNull())
					{
						// Update transform of OpenGL object
						opengl_ob->ob_to_world_matrix = obToWorldMatrix(*in_world_ob);
						opengl_engine->updateObjectTransformData(*opengl_ob);

						const int ob_lod_level = in_world_ob->getLODLevel(cam_controller.getPosition());

						// Update materials in opengl engine.
						for(size_t i=0; i<in_world_ob->materials.size(); ++i)
							if(i < opengl_ob->materials.size())
								ModelLoading::setGLMaterialFromWorldMaterial(*in_world_ob->materials[i], ob_lod_level, in_world_ob->lightmap_url, /*use_basis=*/this->server_has_basis_textures, *this->resource_manager, opengl_ob->materials[i]);

						opengl_engine->objectMaterialsUpdated(*opengl_ob);
					}

					// Update physics object transform
					if(in_world_ob->physics_object.nonNull())
					{
						physics_world->setNewObToWorldTransform(*in_world_ob->physics_object, in_world_ob->pos.toVec4fVector(), Quatf::fromAxisAndAngle(normalise(in_world_ob->axis.toVec4fVector()), in_world_ob->angle), 
							useScaleForWorldOb(in_world_ob->scale).toVec4fVector());
					}

					if(in_world_ob->audio_source.nonNull())
					{
						// Update in audio engine
						in_world_ob->audio_source->pos = in_world_ob->getCentroidWS();
						audio_engine.sourcePositionUpdated(*in_world_ob->audio_source);
					}

					// Update in Indigo view
					//ui->indigoView->objectTransformChanged(*in_world_ob);

					// Update object values in editor
					//ui->objectEditor->setFromObject(*in_world_ob, ui->objectEditor->getSelectedMatIndex(), /*ob in editing user's world=*/connectedToUsersPersonalWorldOrGodUser());
					ui_interface->setObjectEditorFromOb(*in_world_ob, ui_interface->getSelectedMatIndex(), /*ob in editing user's world=*/connectedToUsersWorldOrGodUser());

					// updateInstancedCopiesOfObject(ob); // TODO: enable + test this
					in_world_ob->transformChanged();

					// Mark as from-local-dirty to send an object updated message to the server
					in_world_ob->from_local_other_dirty = true;
					this->world_state->dirty_from_local_objects.insert(in_world_ob);
				}
			}
			else
			{
				// Object had been deleted.  Re-create it, by sending CreateObject message to server
				// Note that the recreated object will have a different ID.
				// To apply more undo edits to the recreated object, use recreated_ob_uid to map from edit UID to recreated object UID.
				{
					MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
					restored_ob->writeToNetworkStream(scratch_packet);

					this->last_restored_ob_uid_in_edit = restored_ob->uid; // Store edit UID, will be used when receiving new object to add entry to recreated_ob_uid map.

					enqueueMessageToSend(*this->client_thread, scratch_packet);
				}
			}
		}
	}

	force_new_undo_edit = true;
}


void GUIClient::bakeLightmapsForAllObjectsInParcel(uint32 lightmap_flag)
{
	int num_lightmaps_to_bake = 0;
	const Parcel* cur_parcel = NULL;
	{
		Lock lock(world_state->mutex);

		// Get current parcel
		for(auto& it : world_state->parcels)
		{
			const Parcel* parcel = it.second.ptr();

			if(parcel->pointInParcel(cam_controller.getFirstPersonPosition()))
			{
				cur_parcel = parcel;
				break;
			}
		}

		if(cur_parcel)
		{
			for(auto it = world_state->objects.valuesBegin(); it != world_state->objects.valuesEnd(); ++it)
			{
				WorldObject* ob = it.getValue().ptr();

				if(cur_parcel->pointInParcel(ob->pos) && objectModificationAllowed(*ob))
				{
					// Don't bake lightmaps for objects with sketal animation for now (creating second UV set removes joints and weights).
					const bool has_skeletal_anim = ob->opengl_engine_ob.nonNull() && ob->opengl_engine_ob->mesh_data.nonNull() &&
						!ob->opengl_engine_ob->mesh_data->animation_data.animations.empty();

					if(!has_skeletal_anim)
					{
						// Don't bake lightmap for objects with only transparent materials, as the lightmap won't be used.
						bool has_non_transparent_mat = false;
						for(size_t i=0; i<ob->materials.size(); ++i)
							if(ob->materials[i].nonNull())
								if(ob->materials[i]->opacity.val == 1.f)
									has_non_transparent_mat = true;

						if(has_non_transparent_mat)
						{
							BitUtils::setBit(ob->flags, WorldObject::LIGHTMAP_NEEDS_COMPUTING_FLAG);
							objs_with_lightmap_rebuild_needed.insert(ob);
							num_lightmaps_to_bake++;
						}
					}
				}
			}
		}
	} // End lock scope

	if(cur_parcel)
	{
		ui_interface->startLightmapFlagTimer(); // Trigger sending update-lightmap update flag message later.

		showInfoNotification("Baking lightmaps for " + toString(num_lightmaps_to_bake) + " objects in current parcel...");
	}
	else
		showErrorNotification("You must be in a parcel to trigger lightmapping on it.");
}


std::string GUIClient::serialiseAllObjectsInParcelToXML(size_t& num_obs_serialised_out)
{
	std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
	xml += "<objects>\n";
	
	num_obs_serialised_out = 0;

	Lock lock(world_state->mutex);

	// Get current parcel
	const Parcel* cur_parcel = NULL;
	for(auto& it : world_state->parcels)
	{
		const Parcel* parcel = it.second.ptr();
		if(parcel->pointInParcel(cam_controller.getFirstPersonPosition()))
		{
			cur_parcel = parcel;
			break;
		}
	}

	if(cur_parcel)
	{
		for(auto it = world_state->objects.valuesBegin(); it != world_state->objects.valuesEnd(); ++it)
		{
			const WorldObject* ob = it.getValue().ptr();
			if(cur_parcel->pointInParcel(ob->pos))
			{
				xml += ob->serialiseToXML(/*tab_depth=*/1);
				xml += "\n";
				num_obs_serialised_out++;
			}
		}
	}
	else
		throw glare::Exception("You are not in a parcel, cannot save objects");
	
	xml += "</objects>\n";
	return xml;
}


void GUIClient::deleteAllParcelObjects(size_t& num_obs_deleted_out)
{
	num_obs_deleted_out = 0;

	Lock lock(world_state->mutex);

	// Get current parcel
	const Parcel* cur_parcel = NULL;
	for(auto& it : world_state->parcels)
	{
		const Parcel* parcel = it.second.ptr();
		if(parcel->pointInParcel(cam_controller.getFirstPersonPosition()))
		{
			cur_parcel = parcel;
			break;
		}
	}

	if(cur_parcel)
	{
		for(auto it = world_state->objects.valuesBegin(); it != world_state->objects.valuesEnd(); ++it)
		{
			const WorldObject* ob = it.getValue().ptr();
			if(cur_parcel->pointInParcel(ob->pos))
			{
				// Send DestroyObject packet for this object
				MessageUtils::initPacket(scratch_packet, Protocol::DestroyObject);
				writeToStream(ob->uid, scratch_packet);

				enqueueMessageToSend(*this->client_thread, scratch_packet);

				num_obs_deleted_out++;
			}
		}
	}
	else
		throw glare::Exception("You are not in a parcel, cannot delete objects");
}


void GUIClient::enableMaterialisationEffectOnOb(WorldObject& ob)
{
	// conPrint("GUIClient::enableMaterialisationEffectOnOb()");

	const float materialise_effect_start_time = (float)Clock::getTimeSinceInit();

	if(ob.opengl_engine_ob.nonNull())
	{
		for(size_t i=0; i<ob.opengl_engine_ob->materials.size(); ++i)
		{
			ob.opengl_engine_ob->materials[i].materialise_effect = true;
			ob.opengl_engine_ob->materials[i].materialise_start_time = materialise_effect_start_time;
		}

		opengl_engine->objectMaterialsUpdated(*ob.opengl_engine_ob);
	}

	ob.use_materialise_effect_on_load = true;
	ob.materialise_effect_start_time = materialise_effect_start_time;
}


void GUIClient::enableMaterialisationEffectOnAvatar(Avatar& avatar)
{
	// conPrint("GUIClient::enableMaterialisationEffectOnAvatar()");

	const float materialise_effect_start_time = (float)Clock::getTimeSinceInit();

	if(avatar.graphics.skinned_gl_ob.nonNull())
	{
		for(size_t i=0; i<avatar.graphics.skinned_gl_ob->materials.size(); ++i)
		{
			avatar.graphics.skinned_gl_ob->materials[i].materialise_effect = true;
			avatar.graphics.skinned_gl_ob->materials[i].materialise_start_time = materialise_effect_start_time;
		}

		opengl_engine->objectMaterialsUpdated(*avatar.graphics.skinned_gl_ob);
	}

	avatar.use_materialise_effect_on_load = true;
	avatar.materialise_effect_start_time = materialise_effect_start_time;
}


void GUIClient::summonBike()
{
	if(!this->logged_in_user_id.valid())
		throw glare::Exception("You must be logged in to summon a bike.");

	//TEMP: Save out bike mats
	if(false)
	{
		ModelLoading::MakeGLObjectResults results;
		ModelLoading::makeGLObjectForModelFile(*opengl_engine, *opengl_engine->vert_buf_allocator, worker_allocator.ptr(),
			"D:\\models\\BMWCONCEPTBIKE\\optimized-dressed_fix7_offset4.glb", // This is exported from Blender from source_resources/bike-optimized-dressed_fix7_offset.blend
			/*do_opengl_stuff=*/true,
			results);

		std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<materials>\n";
		for(size_t i=0; i<results.materials.size(); ++i)
		{
			results.materials[i]->convertLocalPathsToURLS(*this->resource_manager);
			xml += results.materials[i]->serialiseToXML(/*tab_depth=*/1);

			conPrint("Colour3f" + results.materials[i]->colour_rgb.toString() + ","); // Print out colours
		}
		xml += "</materials>";
		FileUtils::writeEntireFileTextMode("bike_mats.xml", xml);
	}


	const Vec3d pos = this->cam_controller.getFirstPersonPosition() + 
		::removeComponentInDir(this->cam_controller.getForwardsVec(), Vec3d(0,0,1)) * 2 +
		Vec3d(0,0,-1.67) + // Move down by eye height to ground
		Vec3d(0, 0, 0.55f); // Move up off ground.  TEMP HARDCODED

	const js::AABBox aabb_os(Vec4f(-2.432, -3.09, -4.352, 1), Vec4f(2.431, 4.994, 2.432, 1)); // Got from diagnostics widget after setting transform to identity.

	// Get desired rotation
	const Quatf rot = Quatf::fromAxisAndAngle(Vec3f(1,0,0), Maths::pi_2<float>()); // NOTE: from bike script
	const Quatf to_face_camera_rot = Quatf::fromAxisAndAngle(Vec3f(0,0,1), (float)this->cam_controller.getAvatarAngles().x);
	Vec4f axis;
	float angle;
	(to_face_camera_rot * rot).toAxisAndAngle(axis, angle);

	// Search for existing summoned bike, if we find it, move it to in front of user.
	{
		Lock lock(world_state->mutex);

		WorldObject* existing_ob_to_summon = NULL;
		for(auto it = world_state->objects.valuesBegin(); it != world_state->objects.valuesEnd(); ++it)
		{
			WorldObject* ob = it.getValue().ptr();
			if(ob->creator_id == logged_in_user_id && // If we created this object
				//BitUtils::isBitSet(ob->flags, WorldObject::SUMMONED_FLAG) && // And this object was summoned
				ob->vehicle_script.nonNull() && ob->vehicle_script.isType<Scripting::BikeScript>()) // And it has a bike script
			{
				if((existing_ob_to_summon == NULL) || (ob->uid.value() > existing_ob_to_summon->uid.value())) // Summon object with greatest UID
					existing_ob_to_summon = ob;
			}
		}

		if(existing_ob_to_summon)
		{
			conPrint("Found summoned object, moving to in front of user.");

			runtimeCheck(existing_ob_to_summon->vehicle_script.nonNull() && existing_ob_to_summon->vehicle_script.isType<Scripting::BikeScript>());

			doMoveAndRotateObject(existing_ob_to_summon, pos, /*axis=*/Vec3f(axis), /*angle=*/angle, aabb_os, /*summoning_object=*/true);

			enableMaterialisationEffectOnOb(*existing_ob_to_summon);

			// Tell controller its vehicle has been summoned, to reset engine revs etc.
			const auto res = vehicle_controllers.find(existing_ob_to_summon);
			if(res != vehicle_controllers.end())
			{
				res->second->vehicleSummoned();
			}

			return;
		}
	}

	conPrint("Creating new bike object...");




	// Load materials:
	IndigoXMLDoc doc(this->resources_dir_path + "/bike_mats.xml");

	pugi::xml_node root = doc.getRootElement();

	std::vector<WorldMaterialRef> materials;
	for(pugi::xml_node n = root.child("material"); n; n = n.next_sibling("material"))
	{
		materials.push_back(WorldMaterial::loadFromXMLElem("bike_mats.xml", /*convert_rel_paths_to_abs_disk_paths=*/false, n));
	}
		


	WorldObjectRef new_world_object = new WorldObject();

	new_world_object->model_url = "optimized_dressed_fix7_offset4_glb_4474648345850208925.bmesh";
	new_world_object->max_model_lod_level = 2;

	new_world_object->flags = WorldObject::COLLIDABLE_FLAG | WorldObject::DYNAMIC_FLAG | WorldObject::SUMMONED_FLAG | WorldObject::EXCLUDE_FROM_LOD_CHUNK_MESH;

	new_world_object->uid = UID(0); // A new UID will be assigned by server
	new_world_object->materials = materials;
	new_world_object->pos = pos;
	new_world_object->axis = Vec3f(axis);
	new_world_object->angle = angle;
	new_world_object->scale = Vec3f(0.18f);
	new_world_object->setAABBOS(aabb_os);

	new_world_object->script = FileUtils::readEntireFileTextMode(this->resources_dir_path + "/summoned_bike_script.xml");

	new_world_object->mass = 200;

	setMaterialFlagsForObject(new_world_object.ptr());

	// Send CreateObject message to server
	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		new_world_object->writeToNetworkStream(scratch_packet);

		enqueueMessageToSend(*this->client_thread, scratch_packet);
	}
}


void GUIClient::summonHovercar()
{
	if(!this->logged_in_user_id.valid())
		throw glare::Exception("You must be logged in to summon a hovercar.");

	//TEMP: Save out hovercar mats
	if(false)
	{
		ModelLoading::MakeGLObjectResults results;
		ModelLoading::makeGLObjectForModelFile(*opengl_engine, *opengl_engine->vert_buf_allocator, worker_allocator.ptr(),
			"D:\\models\\peugot_closed.glb",
			/*do_opengl_stuff=*/true,
			results);

		std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<materials>\n";
		for(size_t i=0; i<results.materials.size(); ++i)
		{
			results.materials[i]->convertLocalPathsToURLS(*this->resource_manager);
			xml += results.materials[i]->serialiseToXML(/*tab_depth=*/1);

			conPrint("Colour3f" + results.materials[i]->colour_rgb.toString() + ","); // Print out colours
		}
		xml += "</materials>";
		FileUtils::writeEntireFileTextMode("hovercar_mats.xml", xml);
	}

	const js::AABBox aabb_os(Vec4f(-1.053, -0.058, -2.163, 1), Vec4f(1.053, 1.284, 2.225, 1)); // Got from diagnostics widget after setting transform to identity.

	const Vec3d pos = this->cam_controller.getFirstPersonPosition() + 
		::removeComponentInDir(this->cam_controller.getForwardsVec(), Vec3d(0,0,1)) * 3 +
		Vec3d(0,0,-1.67) + // Move down by eye height to ground
		Vec3d(0, 0, 0.0493f); // Move up off ground.  TEMP HARDCODED

	// Get desired rotation from script.
	const Quatf rot = Quatf::fromAxisAndAngle(Vec3f(0,0,1), Maths::pi<float>()) * Quatf::fromAxisAndAngle(Vec3f(1,0,0), Maths::pi_2<float>()); // NOTE: from hovercar script

	const Quatf to_face_camera_rot = Quatf::fromAxisAndAngle(Vec3f(0,0,1), (float)this->cam_controller.getAvatarAngles().x);

	Vec4f axis;
	float angle;
	(to_face_camera_rot * rot).toAxisAndAngle(axis, angle);

	// Search for existing summoned hovercar, if we find it, move it to in front of user.
	{
		Lock lock(world_state->mutex);

		WorldObject* existing_ob_to_summon = NULL;
		for(auto it = world_state->objects.valuesBegin(); it != world_state->objects.valuesEnd(); ++it)
		{
			WorldObject* ob = it.getValue().ptr();
			if(ob->creator_id == logged_in_user_id && // If we created this object
				//BitUtils::isBitSet(ob->flags, WorldObject::SUMMONED_FLAG) && // And this object was summoned
				ob->vehicle_script.nonNull() && ob->vehicle_script.isType<Scripting::HoverCarScript>()) // And it has a hovercar script
			{
				if((existing_ob_to_summon == NULL) || (ob->uid.value() > existing_ob_to_summon->uid.value())) // Summon object with greatest UID
					existing_ob_to_summon = ob;
			}
		}

		if(existing_ob_to_summon)
		{
			conPrint("Found summoned object, moving to in front of user.");

			runtimeCheck(existing_ob_to_summon->vehicle_script.nonNull());

			doMoveAndRotateObject(existing_ob_to_summon, pos, /*axis=*/Vec3f(axis), /*angle=*/angle, aabb_os, /*summoning_object=*/true);

			enableMaterialisationEffectOnOb(*existing_ob_to_summon);

			return;
		}
	}

	conPrint("Creating new hovercar object...");




	// Load materials:
	IndigoXMLDoc doc(this->resources_dir_path + "/hovercar_mats.xml");

	pugi::xml_node root = doc.getRootElement();

	std::vector<WorldMaterialRef> materials;
	for(pugi::xml_node n = root.child("material"); n; n = n.next_sibling("material"))
	{
		materials.push_back(WorldMaterial::loadFromXMLElem("hovercar_mats.xml", /*convert_rel_paths_to_abs_disk_paths=*/false, n));
	}



	WorldObjectRef new_world_object = new WorldObject();

	new_world_object->model_url = "peugot_closed_glb_2887717763908023194.bmesh";
	new_world_object->max_model_lod_level = 2;

	new_world_object->flags = WorldObject::COLLIDABLE_FLAG | WorldObject::DYNAMIC_FLAG | WorldObject::SUMMONED_FLAG | WorldObject::EXCLUDE_FROM_LOD_CHUNK_MESH;

	new_world_object->uid = UID(0); // A new UID will be assigned by server
	new_world_object->materials = materials;
	new_world_object->pos = pos;
	new_world_object->axis = Vec3f(axis);
	new_world_object->angle = angle;
	new_world_object->scale = Vec3f(1.f);
	new_world_object->setAABBOS(aabb_os);

	new_world_object->script = FileUtils::readEntireFileTextMode(this->resources_dir_path + "/summoned_hovercar_script.xml");

	new_world_object->mass = 1000;

	setMaterialFlagsForObject(new_world_object.ptr());

	// Send CreateObject message to server
	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		new_world_object->writeToNetworkStream(scratch_packet);

		enqueueMessageToSend(*this->client_thread, scratch_packet);
	}
}


void GUIClient::summonBoat()
{
	if(!this->logged_in_user_id.valid())
		throw glare::Exception("You must be logged in to summon a boat.");

	const js::AABBox aabb_os(Vec4f(-0.281f, -0.165f, -1.25f, 1), Vec4f(0.281f, 0.25f, 1.25f, 1)); // Got from diagnostics widget after setting transform to identity.

	const Vec3d pos = this->cam_controller.getFirstPersonPosition() + 
		::removeComponentInDir(this->cam_controller.getForwardsVec(), Vec3d(0,0,1)) * 3 +
		Vec3d(0,0,-1.67) + // Move down by eye height to ground
		Vec3d(0, 0, 1.0493f); // Move up off ground.  TEMP HARDCODED

	// Get desired rotation from script.
	const Quatf rot = Quatf::fromAxisAndAngle(Vec3f(0,0,1), Maths::pi<float>()) * Quatf::fromAxisAndAngle(Vec3f(1,0,0), Maths::pi_2<float>()); // NOTE: from boat script (model_to_y_forwards_rot_1 etc.)

	const Quatf to_face_camera_rot = Quatf::fromAxisAndAngle(Vec3f(0,0,1), (float)this->cam_controller.getAvatarAngles().x);

	Vec4f axis;
	float angle;
	(to_face_camera_rot * rot).toAxisAndAngle(axis, angle);

	// Search for existing summoned boat, if we find it, move it to in front of user.
	{
		Lock lock(world_state->mutex);

		WorldObject* existing_ob_to_summon = NULL;
		for(auto it = world_state->objects.valuesBegin(); it != world_state->objects.valuesEnd(); ++it)
		{
			WorldObject* ob = it.getValue().ptr();
			if(ob->creator_id == logged_in_user_id && // If we created this object
				//BitUtils::isBitSet(ob->flags, WorldObject::SUMMONED_FLAG) && // And this object was summoned
				ob->vehicle_script.nonNull() && ob->vehicle_script.isType<Scripting::BoatScript>()) // And it has a boat script
			{
				if((existing_ob_to_summon == NULL) || (ob->uid.value() > existing_ob_to_summon->uid.value())) // Summon object with greatest UID
					existing_ob_to_summon = ob;
			}
		}

		if(existing_ob_to_summon)
		{
			conPrint("Found summoned object, moving to in front of user.");

			runtimeCheck(existing_ob_to_summon->vehicle_script.nonNull());

			doMoveAndRotateObject(existing_ob_to_summon, pos, /*axis=*/Vec3f(axis), /*angle=*/angle, aabb_os, /*summoning_object=*/true);

			enableMaterialisationEffectOnOb(*existing_ob_to_summon);

			return;
		}
	}

	conPrint("Creating new boat object...");




	// Load materials:
	IndigoXMLDoc doc(this->resources_dir_path + "/boat_mats.xml");

	pugi::xml_node root = doc.getRootElement();

	std::vector<WorldMaterialRef> materials;
	for(pugi::xml_node n = root.child("material"); n; n = n.next_sibling("material"))
	{
		materials.push_back(WorldMaterial::loadFromXMLElem("boat_mats.xml", /*convert_rel_paths_to_abs_disk_paths=*/false, n));
	}



	WorldObjectRef new_world_object = new WorldObject();

	new_world_object->model_url = "poweryacht3_2_glb_17116251394697619807.bmesh";
	new_world_object->max_model_lod_level = 2;

	new_world_object->flags = WorldObject::COLLIDABLE_FLAG | WorldObject::DYNAMIC_FLAG | WorldObject::SUMMONED_FLAG | WorldObject::EXCLUDE_FROM_LOD_CHUNK_MESH;

	new_world_object->uid = UID(0); // A new UID will be assigned by server
	new_world_object->materials = materials;
	new_world_object->pos = pos;
	new_world_object->axis = Vec3f(axis);
	new_world_object->angle = angle;
	new_world_object->scale = Vec3f(5.5f, 4.2f, 4.7f);
	new_world_object->setAABBOS(aabb_os);
	new_world_object->centre_of_mass_offset_os = Vec3f(0, -0.1f, 0);

	new_world_object->script = FileUtils::readEntireFileTextMode(this->resources_dir_path + "/summoned_boat_script.xml");

	new_world_object->mass = 5000;

	setMaterialFlagsForObject(new_world_object.ptr());

	// Send CreateObject message to server
	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		new_world_object->writeToNetworkStream(scratch_packet);

		enqueueMessageToSend(*this->client_thread, scratch_packet);
	}
}


void GUIClient::summonCar()
{
	if(!this->logged_in_user_id.valid())
		throw glare::Exception("You must be logged in to summon a car.");

	const js::AABBox aabb_os(Vec4f(-1.3898703f, -0.9157071f, -2.365502f, 1), Vec4f(1.3898582f, 0.8355373f, 3.8338537f, 1)); // From D:\files\substrata objects\delorean 2.xml

	const Vec3d pos = this->cam_controller.getFirstPersonPosition() + 
		::removeComponentInDir(this->cam_controller.getForwardsVec(), Vec3d(0,0,1)) * 3 +
		Vec3d(0,0,-1.67) + // Move down by eye height to ground
		Vec3d(0, 0, 1.0493f); // Move up off ground.  TEMP HARDCODED

	// Get desired rotation from script.
	const Quatf rot = Quatf::fromAxisAndAngle(Vec3f(0,0,1), Maths::pi<float>()) * Quatf::fromAxisAndAngle(Vec3f(1,0,0), Maths::pi_2<float>()); // NOTE: from car script (model_to_y_forwards_rot_1 etc.)

	const Quatf to_face_camera_rot = Quatf::fromAxisAndAngle(Vec3f(0,0,1), (float)this->cam_controller.getAvatarAngles().x);

	Vec4f axis;
	float angle;
	(to_face_camera_rot * rot).toAxisAndAngle(axis, angle);

	// Search for existing summoned boat, if we find it, move it to in front of user.
	{
		Lock lock(world_state->mutex);

		WorldObject* existing_ob_to_summon = NULL;
		for(auto it = world_state->objects.valuesBegin(); it != world_state->objects.valuesEnd(); ++it)
		{
			WorldObject* ob = it.getValue().ptr();
			if(ob->creator_id == logged_in_user_id && // If we created this object
				//BitUtils::isBitSet(ob->flags, WorldObject::SUMMONED_FLAG) && // And this object was summoned
				ob->vehicle_script.nonNull() && ob->vehicle_script.isType<Scripting::CarScript>()) // And it has a boat script
			{
				if((existing_ob_to_summon == NULL) || (ob->uid.value() > existing_ob_to_summon->uid.value())) // Summon object with greatest UID
					existing_ob_to_summon = ob;
			}
		}

		if(existing_ob_to_summon)
		{
			conPrint("Found summoned object, moving to in front of user.");

			runtimeCheck(existing_ob_to_summon->vehicle_script.nonNull());

			doMoveAndRotateObject(existing_ob_to_summon, pos, /*axis=*/Vec3f(axis), /*angle=*/angle, aabb_os, /*summoning_object=*/true);

			enableMaterialisationEffectOnOb(*existing_ob_to_summon);

			return;
		}
	}

	conPrint("Creating new car object...");




	// Load materials:
	IndigoXMLDoc doc(this->resources_dir_path + "/car_mats.xml");

	pugi::xml_node root = doc.getRootElement();

	std::vector<WorldMaterialRef> materials;
	for(pugi::xml_node n = root.child("material"); n; n = n.next_sibling("material"))
	{
		materials.push_back(WorldMaterial::loadFromXMLElem("car_mats.xml", /*convert_rel_paths_to_abs_disk_paths=*/false, n));
		// conPrint("Colour3f" + materials.back()->colour_rgb.toString() + ","); // Print out colours
	}



	WorldObjectRef new_world_object = new WorldObject();

	new_world_object->model_url = "deLorean2_0_glb_5923323464955550713.bmesh";
	new_world_object->max_model_lod_level = 2;

	new_world_object->flags = WorldObject::COLLIDABLE_FLAG | WorldObject::DYNAMIC_FLAG | WorldObject::SUMMONED_FLAG | WorldObject::EXCLUDE_FROM_LOD_CHUNK_MESH;

	new_world_object->uid = UID(0); // A new UID will be assigned by server
	new_world_object->materials = materials;
	new_world_object->pos = pos;
	new_world_object->axis = Vec3f(axis);
	new_world_object->angle = angle;
	new_world_object->scale = Vec3f(1.f);
	new_world_object->setAABBOS(aabb_os);
	new_world_object->centre_of_mass_offset_os = Vec3f(0, -0.3f, 1.0f);

	new_world_object->script = FileUtils::readEntireFileTextMode(this->resources_dir_path + "/summoned_car_script.xml");

	new_world_object->mass = 1233.f; // https://en.wikipedia.org/wiki/DMC_DeLorean

	setMaterialFlagsForObject(new_world_object.ptr());

	// Send CreateObject message to server
	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		new_world_object->writeToNetworkStream(scratch_packet);

		enqueueMessageToSend(*this->client_thread, scratch_packet);
	}
}


// Object transform has been edited, e.g. by the object editor.
void GUIClient::objectTransformEdited()
{
	if(this->selected_ob.nonNull())
	{
		// Multiple edits using the object editor, in a short timespan, will be merged together,
		// unless force_new_undo_edit is true (is set when undo or redo is issued).
		const bool start_new_edit = force_new_undo_edit || (time_since_object_edited.elapsed() > 5.0);

		if(start_new_edit)
			undo_buffer.startWorldObjectEdit(*this->selected_ob);

		ui_interface->writeTransformMembersToObject(*this->selected_ob);

		this->selected_ob->last_modified_time = TimeStamp::currentTime(); // Gets set on server as well, this is just for updating the local display.
		ui_interface->objectLastModifiedUpdated(*this->selected_ob);

		if(start_new_edit)
			undo_buffer.finishWorldObjectEdit(*this->selected_ob);
		else
			undo_buffer.replaceFinishWorldObjectEdit(*this->selected_ob);

		time_since_object_edited.reset();
		force_new_undo_edit = false;

		Matrix4f new_ob_to_world_matrix = obToWorldMatrix(*this->selected_ob); // Tentative new transform before it is possibly constrained to a parcel.

		GLObjectRef opengl_ob = selected_ob->opengl_engine_ob;
		if(opengl_ob.nonNull())
		{
			//ui->glWidget->makeCurrent();

			js::Vector<EdgeMarker, 16> edge_markers;
			Vec3d new_ob_pos;
			const bool valid = clampObjectPositionToParcelForNewTransform(
				*this->selected_ob,
				opengl_ob,
				this->selected_ob->pos, 
				new_ob_to_world_matrix,
				edge_markers, 
				new_ob_pos);
			if(valid)
			{
				new_ob_to_world_matrix.setColumn(3, new_ob_pos.toVec4fPoint());
				selected_ob->setTransformAndHistory(new_ob_pos, this->selected_ob->axis, this->selected_ob->angle);

				// Update spotlight data in opengl engine.
				if(this->selected_ob->object_type == WorldObject::ObjectType_Spotlight)
				{
					/*GLLightRef light = this->selected_ob->opengl_light;
					if(light.nonNull())
					{
						light->gpu_data.dir = normalise(new_ob_to_world_matrix * Vec4f(0, 0, -1, 0));
						opengl_engine->setLightPos(light, new_ob_pos.toVec4fPoint());
					}*/
					updateSpotlightGraphicsEngineData(new_ob_to_world_matrix, selected_ob.ptr());
				}

				// Update transform of OpenGL object
				opengl_ob->ob_to_world_matrix = new_ob_to_world_matrix;
				opengl_engine->updateObjectTransformData(*opengl_ob);

				// Update physics object transform
				physics_world->setNewObToWorldTransform(*selected_ob->physics_object, selected_ob->pos.toVec4fVector(), Quatf::fromAxisAndAngle(normalise(selected_ob->axis.toVec4fVector()), selected_ob->angle),
					useScaleForWorldOb(selected_ob->scale).toVec4fVector());

				selected_ob->transformChanged(); // Recompute centroid_ws, biased_aabb_len etc..

				Lock lock(this->world_state->mutex);

				if(this->selected_ob->isDynamic() && !isObjectPhysicsOwnedBySelf(*this->selected_ob, world_state->getCurrentGlobalTime()) && !isObjectVehicleBeingDrivenByOther(*this->selected_ob))
				{
					// conPrint("==Taking ownership of physics object in objectEditedSlot()...==");
					takePhysicsOwnershipOfObject(*this->selected_ob, world_state->getCurrentGlobalTime());
				}

				// Mark as from-local-dirty to send an object updated message to the server
				this->selected_ob->from_local_transform_dirty = true;
				this->world_state->dirty_from_local_objects.insert(this->selected_ob);

				// Update any instanced copies of object
				updateInstancedCopiesOfObject(this->selected_ob.ptr());
			}
			else // Else if new transform is not valid
			{
				showErrorNotification("New object transform is not valid - Object must be entirely in a parcel that you have write permissions for.");
			}
		}

		if(this->selected_ob->audio_source.nonNull())
		{
			this->selected_ob->audio_source->pos = this->selected_ob->getCentroidWS();
			this->audio_engine.sourcePositionUpdated(*this->selected_ob->audio_source);
		}

		if(this->terrain_system.nonNull() && ::hasPrefix(selected_ob->content, "biome:"))
			this->terrain_system->invalidateVegetationMap(selected_ob->getAABBWS());
	}

}


// Object property (that is not a transform property) has been edited, e.g. by the object editor.
void GUIClient::objectEdited()
{
	// Update object material(s) with values from editor.
	if(this->selected_ob.nonNull())
	{
		// Multiple edits using the object editor, in a short timespan, will be merged together,
		// unless force_new_undo_edit is true (is set when undo or redo is issued).
		const bool start_new_edit = force_new_undo_edit || (time_since_object_edited.elapsed() > 5.0);

		if(start_new_edit)
			undo_buffer.startWorldObjectEdit(*this->selected_ob);

		//ui->objectEditor->toObject(*this->selected_ob); // Sets changed_flags on object as well.
		ui_interface->objectEditorToObject(*this->selected_ob); // Sets changed_flags on object as well.
			
		this->selected_ob->last_modified_time = TimeStamp::currentTime(); // Gets set on server as well, this is just for updating the local display.
		ui_interface->objectLastModifiedUpdated(*this->selected_ob);

		if(start_new_edit)
			undo_buffer.finishWorldObjectEdit(*this->selected_ob);
		else
			undo_buffer.replaceFinishWorldObjectEdit(*this->selected_ob);
		
		time_since_object_edited.reset();
		force_new_undo_edit = false;

		setMaterialFlagsForObject(selected_ob.ptr());

		if((selected_ob->object_type == WorldObject::ObjectType_VoxelGroup) && 
			(BitUtils::isBitSet(this->selected_ob->changed_flags, WorldObject::DYNAMIC_CHANGED) || BitUtils::isBitSet(this->selected_ob->changed_flags, WorldObject::PHYSICS_VALUE_CHANGED)))
		{
			if(vehicle_controllers.find(selected_ob.ptr()) != vehicle_controllers.end()) // If there is a vehicle controller for selected_ob:
			{
				showErrorNotification("Can't edit vehicle physics properties while user is in vehicle");
			}
			else
			{
				// Rebuild physics object
				const Matrix4f ob_to_world = obToWorldMatrix(*selected_ob);

				js::Vector<bool, 16> mat_transparent(selected_ob->materials.size());
				for(size_t i=0; i<selected_ob->materials.size(); ++i)
					mat_transparent[i] = selected_ob->materials[i]->opacity.val < 1.f;

				PhysicsShape physics_shape;
				const int subsample_factor = 1;
				Reference<OpenGLMeshRenderData> gl_meshdata = ModelLoading::makeModelForVoxelGroup(selected_ob->getDecompressedVoxelGroup(), subsample_factor, ob_to_world,
					opengl_engine->vert_buf_allocator.ptr(), /*do_opengl_stuff=*/true, /*need_lightmap_uvs=*/false, mat_transparent, /*build_dynamic_physics_ob=*/selected_ob->isDynamic(),
					worker_allocator.ptr(), 
					physics_shape);

				// Remove existing physics object
				if(selected_ob->physics_object.nonNull())
				{
					physics_world->removeObject(selected_ob->physics_object);
					selected_ob->physics_object = NULL;
				}

				// Make new physics object
				assert(selected_ob->physics_object.isNull());
				selected_ob->physics_object = new PhysicsObject(/*collidable=*/selected_ob->isCollidable());

				PhysicsShape use_shape = physics_shape;
				if(selected_ob->centre_of_mass_offset_os != Vec3f(0.f))
					use_shape = PhysicsWorld::createCOMOffsetShapeForShape(physics_shape, selected_ob->centre_of_mass_offset_os.toVec4fVector());

				selected_ob->physics_object->shape = use_shape;
				selected_ob->physics_object->is_sensor = selected_ob->isSensor();
				selected_ob->physics_object->userdata = selected_ob.ptr();
				selected_ob->physics_object->userdata_type = 0;
				selected_ob->physics_object->ob_uid = selected_ob->uid;
				selected_ob->physics_object->pos = selected_ob->pos.toVec4fPoint();
				selected_ob->physics_object->rot = Quatf::fromAxisAndAngle(normalise(selected_ob->axis), selected_ob->angle);
				selected_ob->physics_object->scale = useScaleForWorldOb(selected_ob->scale);

				selected_ob->physics_object->kinematic = !selected_ob->script.empty();
				selected_ob->physics_object->dynamic = selected_ob->isDynamic();

				selected_ob->physics_object->mass = selected_ob->mass;
				selected_ob->physics_object->friction = selected_ob->friction;
				selected_ob->physics_object->restitution = selected_ob->restitution;

				physics_world->addObject(selected_ob->physics_object);

				BitUtils::zeroBit(this->selected_ob->changed_flags, WorldObject::DYNAMIC_CHANGED);
				BitUtils::zeroBit(this->selected_ob->changed_flags, WorldObject::PHYSICS_VALUE_CHANGED);
			}
		}

		// Scripted objects (e.g. objects being path controlled), need to be kinematic.  If we enabled a script, but the existing physics object is not kinematic, reload the physics object.
		const bool physics_rebuild_needed_for_script_enabling = BitUtils::isBitSet(this->selected_ob->changed_flags, WorldObject::SCRIPT_CHANGED) &&
			selected_ob->physics_object && !selected_ob->physics_object->kinematic && !selected_ob->script.empty() &&
			(selected_ob->object_type == WorldObject::ObjectType_Generic ||selected_ob->object_type == WorldObject::ObjectType_VoxelGroup);
		
		if(BitUtils::isBitSet(this->selected_ob->changed_flags, WorldObject::MODEL_URL_CHANGED) || 
			(BitUtils::isBitSet(this->selected_ob->changed_flags, WorldObject::DYNAMIC_CHANGED) || BitUtils::isBitSet(this->selected_ob->changed_flags, WorldObject::PHYSICS_VALUE_CHANGED)) ||
			physics_rebuild_needed_for_script_enabling)
		{
			if(vehicle_controllers.find(selected_ob.ptr()) != vehicle_controllers.end()) // If there is a vehicle controller for selected_ob:
			{
				showErrorNotification("Can't edit vehicle physics properties while user is in vehicle");
			}
			else
			{
				removeAndDeleteGLAndPhysicsObjectsForOb(*this->selected_ob); // Remove old opengl and physics objects

				const std::string mesh_path = FileUtils::fileExists(this->selected_ob->model_url) ? toStdString(this->selected_ob->model_url) : resource_manager->pathForURL(this->selected_ob->model_url);

				ModelLoading::MakeGLObjectResults results;
				ModelLoading::makeGLObjectForModelFile(*opengl_engine, *opengl_engine->vert_buf_allocator, worker_allocator.ptr(), mesh_path,
					/*do_opengl_stuff=*/true,
					results
				);
			
				this->selected_ob->opengl_engine_ob = results.gl_ob;
				this->selected_ob->opengl_engine_ob->ob_to_world_matrix = obToWorldMatrix(*this->selected_ob);

				opengl_engine->addObject(this->selected_ob->opengl_engine_ob);

				opengl_engine->selectObject(this->selected_ob->opengl_engine_ob);

				if(BitUtils::isBitSet(this->selected_ob->changed_flags, WorldObject::MODEL_URL_CHANGED))
				{
					// If the user selected a mesh that is not a bmesh, convert it to bmesh.
					std::string bmesh_disk_path;
					if(!hasExtension(mesh_path, "bmesh")) 
					{
						// Save as bmesh in temp location
						bmesh_disk_path = PlatformUtils::getTempDirPath() + "/temp.bmesh";

						BatchedMesh::WriteOptions write_options;
						write_options.compression_level = 9; // Use a somewhat high compression level, as this mesh is likely to be read many times, and only encoded here.
						// TODO: show 'processing...' dialog while it compresses and saves?
						results.batched_mesh->writeToFile(bmesh_disk_path, write_options);
					}
					else
						bmesh_disk_path = mesh_path;

					// Compute hash over model
					const uint64 model_hash = FileChecksum::fileChecksum(bmesh_disk_path);

					const std::string original_filename = FileUtils::getFilename(mesh_path); // Use the original filename, not 'temp.bmesh'.
					const URLString mesh_URL = ResourceManager::URLForNameAndExtensionAndHash(original_filename, ::getExtension(bmesh_disk_path), model_hash); // Make a URL like "projectdog_png_5624080605163579508.png"

					// Copy model to local resources dir if not already there.  UploadResourceThread will read from here.
					if(!this->resource_manager->isFileForURLPresent(mesh_URL))
						this->resource_manager->copyLocalFileToResourceDir(bmesh_disk_path, mesh_URL);

					this->selected_ob->model_url = mesh_URL;
					this->selected_ob->max_model_lod_level = (results.batched_mesh->numVerts() <= 4 * 6) ? 0 : 2; // If this is a very small model (e.g. a cuboid), don't generate LOD versions of it.
					this->selected_ob->setAABBOS(results.batched_mesh->aabb_os);
				}
				else
				{
//						assert(BitUtils::isBitSet(this->selected_ob->changed_flags, WorldObject::DYNAMIC_CHANGED));
				}


				// NOTE: do we want to update materials and scale etc. on object, given that we have a new mesh now?

				// Make new physics object
				assert(selected_ob->physics_object.isNull());
				selected_ob->physics_object = new PhysicsObject(/*collidable=*/selected_ob->isCollidable());

				PhysicsShape use_shape = PhysicsWorld::createJoltShapeForBatchedMesh(*results.batched_mesh, selected_ob->isDynamic(), worker_allocator.ptr());
				if(selected_ob->centre_of_mass_offset_os != Vec3f(0.f))
					use_shape = PhysicsWorld::createCOMOffsetShapeForShape(use_shape, selected_ob->centre_of_mass_offset_os.toVec4fVector());

				selected_ob->physics_object->shape = use_shape;
				selected_ob->physics_object->is_sensor = selected_ob->isSensor();
				selected_ob->physics_object->userdata = selected_ob.ptr();
				selected_ob->physics_object->userdata_type = 0;
				selected_ob->physics_object->ob_uid = selected_ob->uid;
				selected_ob->physics_object->pos = selected_ob->pos.toVec4fPoint();
				selected_ob->physics_object->rot = Quatf::fromAxisAndAngle(normalise(selected_ob->axis), selected_ob->angle);
				selected_ob->physics_object->scale = useScaleForWorldOb(selected_ob->scale);
			
				selected_ob->physics_object->kinematic = !selected_ob->script.empty();
				selected_ob->physics_object->dynamic = selected_ob->isDynamic();
				selected_ob->physics_object->is_sphere = FileUtils::getFilenameStringView(selected_ob->model_url) == "Icosahedron_obj_136334556484365507.bmesh";
				selected_ob->physics_object->is_cube = FileUtils::getFilenameStringView(selected_ob->model_url) == "Cube_obj_11907297875084081315.bmesh";

				selected_ob->physics_object->mass = selected_ob->mass;
				selected_ob->physics_object->friction = selected_ob->friction;
				selected_ob->physics_object->restitution = selected_ob->restitution;
			
				physics_world->addObject(selected_ob->physics_object);


				BitUtils::zeroBit(this->selected_ob->changed_flags, WorldObject::MODEL_URL_CHANGED);
				BitUtils::zeroBit(this->selected_ob->changed_flags, WorldObject::DYNAMIC_CHANGED);
				BitUtils::zeroBit(this->selected_ob->changed_flags, WorldObject::PHYSICS_VALUE_CHANGED);
			}
		}


		// Copy all dependencies into resource directory if they are not there already.
		// URLs will actually be paths from editing for now.
		WorldObject::GetDependencyOptions options;
		options.use_basis = this->server_has_basis_textures;
		options.include_lightmaps = this->use_lightmaps;
		options.get_optimised_mesh = false;//this->server_has_optimised_meshes;
		DependencyURLVector URLs;
		this->selected_ob->appendDependencyURLsBaseLevel(options, URLs);

		for(size_t i=0; i<URLs.size(); ++i)
		{
			if(FileUtils::fileExists(URLs[i].URL)) // If this was a local path:
			{
				const URLString local_path = URLs[i].URL;
				const URLString URL = ResourceManager::URLForPathAndHash(toStdString(local_path), FileChecksum::fileChecksum(local_path));

				// Copy model to local resources dir.
				resource_manager->copyLocalFileToResourceDir(toStdString(local_path), URL);
			}
		}
		


		this->selected_ob->convertLocalPathsToURLS(*this->resource_manager);

		//if(!task_manager)
		//	task_manager = new glare::TaskManager("GUIClient general task manager", myClamp<size_t>(PlatformUtils::getNumLogicalProcessors() / 2, 1, 8)); // Currently just used for LODGeneration::generateLODTexturesForMaterialsIfNotPresent().

		// Generate LOD textures for materials, if not already present on disk.
		// Note that server will also generate LOD textures, however the client may want to display a particular LOD texture immediately, so generate on the client as well.
		//TEMP LODGeneration::generateLODTexturesForMaterialsIfNotPresent(selected_ob->materials, *resource_manager, *task_manager);

		const int ob_lod_level = this->selected_ob->getLODLevel(cam_controller.getPosition());
		const float max_dist_for_ob_lod_level = selected_ob->getMaxDistForLODLevel(ob_lod_level);

		startLoadingTexturesForObject(*this->selected_ob, ob_lod_level, max_dist_for_ob_lod_level, max_dist_for_ob_lod_level/*TEMP*/);

		startDownloadingResourcesForObject(this->selected_ob.ptr(), ob_lod_level);

		if(selected_ob->model_url.empty() || resource_manager->isFileForURLPresent(selected_ob->model_url))
		{
			Matrix4f new_ob_to_world_matrix = obToWorldMatrix(*this->selected_ob);

			GLObjectRef opengl_ob = selected_ob->opengl_engine_ob;
			if(opengl_ob.nonNull())
			{
				js::Vector<EdgeMarker, 16> edge_markers;
				Vec3d new_ob_pos;
				const bool valid = clampObjectPositionToParcelForNewTransform(
					*this->selected_ob,
					opengl_ob,
					this->selected_ob->pos, 
					new_ob_to_world_matrix,
					edge_markers, 
					new_ob_pos);
				if(valid)
				{
					new_ob_to_world_matrix.setColumn(3, new_ob_pos.toVec4fPoint());
					selected_ob->setTransformAndHistory(new_ob_pos, this->selected_ob->axis, this->selected_ob->angle);

					// Update in opengl engine.
					if(this->selected_ob->object_type == WorldObject::ObjectType_Generic || this->selected_ob->object_type == WorldObject::ObjectType_VoxelGroup)
					{
						// Update materials
						if(opengl_ob.nonNull())
						{
							if(!opengl_ob->materials.empty())
							{
								opengl_ob->materials.resize(myMax(opengl_ob->materials.size(), this->selected_ob->materials.size()));

								for(size_t i=0; i<myMin(opengl_ob->materials.size(), this->selected_ob->materials.size()); ++i)
									ModelLoading::setGLMaterialFromWorldMaterial(*this->selected_ob->materials[i], ob_lod_level, this->selected_ob->lightmap_url, /*use_basis=*/this->server_has_basis_textures, *this->resource_manager,
										opengl_ob->materials[i]
									);

								assignLoadedOpenGLTexturesToMats(selected_ob.ptr());
							}
						}

						opengl_engine->objectMaterialsUpdated(*opengl_ob);
					}
					else if(this->selected_ob->object_type == WorldObject::ObjectType_Hypercard)
					{
						if(BitUtils::isBitSet(selected_ob->changed_flags, WorldObject::CONTENT_CHANGED))
						{
							BitUtils::zeroBit(selected_ob->changed_flags, WorldObject::CONTENT_CHANGED);

							// Re-create opengl-ob
							//ui->glWidget->makeCurrent();

							opengl_ob->materials.resize(1);
							opengl_ob->materials[0].tex_matrix = Matrix2f(1, 0, 0, -1); // OpenGL expects texture data to have bottom left pixel at offset 0, we have top left pixel, so flip

							const OpenGLTextureKey tex_key = OpenGLTextureKey("_HYPCRD_") + OpenGLTextureKey(toString(XXH64(selected_ob->content.data(), selected_ob->content.size(), 1)));

							// If the hypercard texture is already loaded, use it
							opengl_ob->materials[0].albedo_texture = opengl_engine->getTextureIfLoaded(tex_key);
							opengl_ob->materials[0].tex_path = tex_key;

							if(opengl_ob->materials[0].albedo_texture.isNull())
							{
								const bool just_added = checkAddTextureToProcessingSet(tex_key);
								if(just_added) // not being loaded already:
								{
									Reference<MakeHypercardTextureTask> task = new MakeHypercardTextureTask();
									task->tex_key = tex_key;
									task->result_msg_queue = &this->msg_queue;
									task->hypercard_content = selected_ob->content;
									task->opengl_engine = opengl_engine;
									task->fonts = this->gl_ui->getFonts();
									task->worker_allocator = worker_allocator;
									task->texture_loaded_msg_allocator = texture_loaded_msg_allocator;
									task->upload_thread = opengl_upload_thread;
									load_item_queue.enqueueItem(/*key=*/URLString(tex_key), *this->selected_ob, task, /*max task dist=*/200.f);
								}

								loading_texture_key_to_hypercard_UID_map[tex_key].insert(selected_ob->uid);
							}

							opengl_ob->ob_to_world_matrix = new_ob_to_world_matrix;
							opengl_engine->objectMaterialsUpdated(*opengl_ob);

							selected_ob->opengl_engine_ob = opengl_ob;
						}
					}
					else if(this->selected_ob->object_type == WorldObject::ObjectType_Text)
					{
						// Re-create opengl and physics objects
						recreateTextGraphicsAndPhysicsObs(selected_ob.ptr());

						BitUtils::zeroBit(selected_ob->changed_flags, WorldObject::CONTENT_CHANGED);

						opengl_ob = selected_ob->opengl_engine_ob;//new_opengl_ob;

						opengl_engine->selectObject(/*new_opengl_ob*/selected_ob->opengl_engine_ob);
					}
					else if(this->selected_ob->object_type == WorldObject::ObjectType_Spotlight)
					{
						updateSpotlightGraphicsEngineData(new_ob_to_world_matrix, this->selected_ob.ptr());
					}

					// Update transform of OpenGL object
					opengl_ob->ob_to_world_matrix = new_ob_to_world_matrix;
					opengl_engine->updateObjectTransformData(*opengl_ob);

					// Update physics object transform
					selected_ob->physics_object->collidable = selected_ob->isCollidable();
					physics_world->setNewObToWorldTransform(*selected_ob->physics_object, selected_ob->pos.toVec4fVector(), Quatf::fromAxisAndAngle(normalise(selected_ob->axis.toVec4fVector()), selected_ob->angle),
						useScaleForWorldOb(selected_ob->scale).toVec4fVector());

					// Update in Indigo view
					//ui->indigoView->objectTransformChanged(*selected_ob);

					selected_ob->transformChanged();

					Lock lock(this->world_state->mutex);

					if(this->selected_ob->isDynamic() && !isObjectPhysicsOwnedBySelf(*this->selected_ob, world_state->getCurrentGlobalTime()) && !isObjectVehicleBeingDrivenByOther(*this->selected_ob))
					{
						// conPrint("==Taking ownership of physics object in objectEditedSlot()...==");
						takePhysicsOwnershipOfObject(*this->selected_ob, world_state->getCurrentGlobalTime());
					}


					// Mark as from-local-dirty to send an object updated message to the server
					this->selected_ob->from_local_other_dirty = true;
					this->world_state->dirty_from_local_objects.insert(this->selected_ob);


					//this->selected_ob->flags |= WorldObject::LIGHTMAP_NEEDS_COMPUTING_FLAG;
					//objs_with_lightmap_rebuild_needed.insert(this->selected_ob);
					//lightmap_flag_timer->start(/*msec=*/2000); // Trigger sending update-lightmap update flag message later.


					// Update any instanced copies of object
					updateInstancedCopiesOfObject(this->selected_ob.ptr());
				}
				else // Else if new transform is not valid
				{
					showErrorNotification("New object transform is not valid - Object must be entirely in a parcel that you have write permissions for.");
				}
			}
		}

		if(BitUtils::isBitSet(selected_ob->changed_flags, WorldObject::AUDIO_SOURCE_URL_CHANGED))
		{
			this->audio_obs.insert(this->selected_ob);
			loadAudioForObject(this->selected_ob.getPointer(), /*loaded buffer=*/nullptr);
		}

		if(BitUtils::isBitSet(selected_ob->changed_flags, WorldObject::SCRIPT_CHANGED))
		{
			try
			{
				WorldStateLock lock(this->world_state->mutex);

				// Try loading script first
				loadScriptForObject(this->selected_ob.ptr(), lock);

				// If we got here, new script was valid and loaded successfully.
					
				// If we were controlling a vehicle whose script just changed, we want to update the vehicle controller.  We will do this by destroying it and creating a new one,
				// then making the user enter the vehicle again.
				// If the script failed to load, due to e.g. a syntax error, then we don't want to destroy the vehicle controller, hence this code is after loadScriptForObject().
				// See if we were in a vehicle whose script just changed.  Set prev_seat_index to the seat we were in if so.
				int prev_seat_index = -1;
				if(vehicle_controller_inside.nonNull() && vehicle_controller_inside->getControlledObject() == selected_ob.ptr())
					prev_seat_index = cur_seat_index;

				destroyVehiclePhysicsControllingObject(selected_ob.ptr()); // Destroy old vehicle controller for object

				if(prev_seat_index >= 0) // If we were in the vehicle for this object:
				{
					if(this->selected_ob->vehicle_script.nonNull())
					{
						Reference<VehiclePhysics> controller = createVehicleControllerForScript(this->selected_ob.ptr());
						vehicle_controllers.insert(std::make_pair(this->selected_ob.ptr(), controller));
						this->vehicle_controller_inside = controller;
						this->vehicle_controller_inside->userEnteredVehicle(/*seat index=*/prev_seat_index);
					}
				}
			}
			catch(glare::Exception& e)
			{
				// Don't show a modal message box on script error, display non-modal error notification (and write to log) instead.
				logMessage("Error while loading script: " + e.what());
				showErrorNotification("Error while loading script: " + e.what());

				// Pass to UIInterface to show on script editor window if applicable
				ui_interface->luaErrorOccurred(e.what(), selected_ob->uid);
			}
		}

		if(selected_ob->browser_vid_player.nonNull())
			selected_ob->browser_vid_player->videoURLMayHaveChanged(this, opengl_engine.ptr(), selected_ob.ptr());

		if(this->selected_ob->audio_source.nonNull())
		{
			this->selected_ob->audio_source->pos = this->selected_ob->getCentroidWS();
			this->audio_engine.sourcePositionUpdated(*this->selected_ob->audio_source);

			this->selected_ob->audio_source->volume = this->selected_ob->audio_volume;
			this->audio_engine.sourceVolumeUpdated(*this->selected_ob->audio_source);
		}

		if(this->terrain_system.nonNull() && ::hasPrefix(selected_ob->content, "biome:"))
			this->terrain_system->invalidateVegetationMap(selected_ob->getAABBWS());
	}
}


void GUIClient::updateSpotlightGraphicsEngineData(const Matrix4f& ob_to_world_matrix, WorldObject* ob)
{
	GLObjectRef opengl_ob = ob->opengl_engine_ob;
	GLLightRef light = ob->opengl_light;
	if(opengl_ob.nonNull() && light.nonNull())
	{
		light->gpu_data.dir = normalise(ob_to_world_matrix * Vec4f(0, 0, -1, 0));

		float scale;
		light->gpu_data.col = computeSpotlightColour(*ob, light->gpu_data.cone_cos_angle_start, light->gpu_data.cone_cos_angle_end, scale);

		opengl_engine->setLightPos(light, ob->pos.toVec4fPoint());


		// Use material[1] from the WorldObject as the light housing GL material.
		opengl_ob->materials.resize(2);
		if(ob->materials.size() >= 2)
			ModelLoading::setGLMaterialFromWorldMaterial(*ob->materials[1], /*lod level=*//*ob_lod_level*/0, /*lightmap URL=*/"", /*use_basis=*/this->server_has_basis_textures, *resource_manager, /*open gl mat=*/opengl_ob->materials[0]);
		else
			opengl_ob->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(0.85f));

		// Apply a light emitting material to the light surface material in the spotlight model.
		if(ob->materials.size() >= 1)
		{
			opengl_ob->materials[1].emission_linear_rgb = toLinearSRGB(ob->materials[0]->colour_rgb);
			opengl_ob->materials[1].emission_scale = scale;
		}

		opengl_engine->objectMaterialsUpdated(*opengl_ob);
	}
}


void GUIClient::recreateTextGraphicsAndPhysicsObs(WorldObject* ob)
{
	// conPrint("recreateTextGraphicsAndPhysicsObs");

	removeAndDeleteGLAndPhysicsObjectsForOb(*ob);

	const Matrix4f ob_to_world_matrix = obToWorldMatrix(*ob);

	PhysicsObjectRef new_physics_ob;
	GLObjectRef new_opengl_ob;
	createGLAndPhysicsObsForText(ob_to_world_matrix, ob, /*use_materialise_effect*/false, new_physics_ob, new_opengl_ob);

	ob->opengl_engine_ob = new_opengl_ob;
	ob->physics_object = new_physics_ob;
	ob->setAABBOS(new_opengl_ob->mesh_data->aabb_os);

	opengl_engine->addObject(ob->opengl_engine_ob);

	physics_world->addObject(ob->physics_object);
}


Reference<VehiclePhysics> GUIClient::createVehicleControllerForScript(WorldObject* ob)
{
	Reference<VehiclePhysics> controller;

	if(ob->vehicle_script.isType<Scripting::HoverCarScript>())
	{
		const Scripting::HoverCarScript* hover_car_script = ob->vehicle_script.downcastToPtr<Scripting::HoverCarScript>();

		HoverCarPhysicsSettings hover_car_physics_settings;
		hover_car_physics_settings.hovercar_mass = ob->mass;
		hover_car_physics_settings.script_settings = hover_car_script->settings.downcast<Scripting::HoverCarScriptSettings>();

		physics_world->setObjectLayer(ob->physics_object, Layers::VEHICLES);

		controller = new HoverCarPhysics(ob, ob->physics_object->jolt_body_id, hover_car_physics_settings, particle_manager.ptr());
	}
	else if(ob->vehicle_script.isType<Scripting::BoatScript>())
	{
		const Scripting::BoatScript* boat_script = ob->vehicle_script.downcastToPtr<Scripting::BoatScript>();

		BoatPhysicsSettings physics_settings;
		physics_settings.boat_mass = ob->mass;
		physics_settings.script_settings = boat_script->settings.downcast<Scripting::BoatScriptSettings>();

		physics_world->setObjectLayer(ob->physics_object, Layers::VEHICLES);

		controller = new BoatPhysics(ob, ob->physics_object->jolt_body_id, physics_settings, particle_manager.ptr());
	}
	else if(ob->vehicle_script.isType<Scripting::BikeScript>())
	{
		const Scripting::BikeScript* bike_script = ob->vehicle_script.downcastToPtr<Scripting::BikeScript>();

		BikePhysicsSettings bike_physics_settings;
		bike_physics_settings.bike_mass = ob->mass;
		bike_physics_settings.script_settings = bike_script->settings.downcast<Scripting::BikeScriptSettings>();

		controller = new BikePhysics(ob, bike_physics_settings, *physics_world, &audio_engine, base_dir_path, particle_manager.ptr());
	}
	else if(ob->vehicle_script.isType<Scripting::CarScript>())
	{
		const Scripting::CarScript* car_script = ob->vehicle_script.downcastToPtr<Scripting::CarScript>();

		CarPhysicsSettings car_physics_settings;
		//car_physics_settings.bike_mass = ob->mass;
		car_physics_settings.script_settings = car_script->settings.downcast<Scripting::CarScriptSettings>();

		controller = new CarPhysics(ob, ob->physics_object->jolt_body_id, car_physics_settings, *physics_world, &audio_engine, base_dir_path, particle_manager.ptr());
	}
	else
		throw glare::Exception("invalid vehicle_script type");

	controller->setDebugVisEnabled(ui_interface->showVehiclePhysicsVisEnabled(), *opengl_engine);
	return controller;
}


void GUIClient::posAndRot3DControlsToggled(bool enabled)
{
	if(enabled)
	{
		if(selected_ob.nonNull())
		{
			const bool have_edit_permissions = objectModificationAllowed(*this->selected_ob);

			// Add an object placement beam
			if(have_edit_permissions)
			{
				for(int i = 0; i < NUM_AXIS_ARROWS; ++i)
					opengl_engine->addObject(axis_arrow_objects[i]);

				for(int i = 0; i < 3; ++i)
					opengl_engine->addObject(rot_handle_arc_objects[i]);

				axis_and_rot_obs_enabled = true;
			}
		}
	}
	else
	{
		for(int i = 0; i < NUM_AXIS_ARROWS; ++i)
			opengl_engine->removeObject(this->axis_arrow_objects[i]);

		for(int i = 0; i < 3; ++i)
			opengl_engine->removeObject(this->rot_handle_arc_objects[i]);

		axis_and_rot_obs_enabled = false;
	}
}


void GUIClient::sendLightmapNeededFlagsSlot()
{
	conPrint("GUIClient::sendLightmapNeededFlagsSlot");

	// Go over set of objects to lightmap (objs_with_lightmap_rebuild_needed) and add any object within the lightmap effect distance.
	if(false)
	{
		const float D = 100.f;

		Lock lock(this->world_state->mutex);

		std::vector<WorldObject*> other_obs_to_lightmap;

		for(auto it = objs_with_lightmap_rebuild_needed.begin(); it != objs_with_lightmap_rebuild_needed.end(); ++it)
		{
			WorldObjectRef ob = *it;

			for(auto other_it = this->world_state->objects.valuesBegin(); other_it != this->world_state->objects.valuesEnd(); ++other_it)
			{
				WorldObject* other_ob = other_it.getValue().ptr();

				const float dist = (float)other_ob->pos.getDist(ob->pos);
				if(dist < D)
					other_obs_to_lightmap.push_back(other_ob);
			}
		}

		// Append other_obs_to_lightmap to objs_with_lightmap_rebuild_needed
		for(size_t i=0; i<other_obs_to_lightmap.size(); ++i)
		{
			objs_with_lightmap_rebuild_needed.insert(other_obs_to_lightmap[i]);
			conPrint("Adding object with UID " + other_obs_to_lightmap[i]->uid.toString());

			other_obs_to_lightmap[i]->flags |= WorldObject::LIGHTMAP_NEEDS_COMPUTING_FLAG;
		}
	}


	
	for(auto it = objs_with_lightmap_rebuild_needed.begin(); it != objs_with_lightmap_rebuild_needed.end(); ++it)
	{
		WorldObjectRef ob = *it;

		// Enqueue ObjectFlagsChanged
		MessageUtils::initPacket(scratch_packet, Protocol::ObjectFlagsChanged);
		writeToStream(ob->uid, scratch_packet);
		scratch_packet.writeUInt32(ob->flags);

		enqueueMessageToSend(*this->client_thread, scratch_packet);
	}

	objs_with_lightmap_rebuild_needed.clear();
}


void GUIClient::visitSubURL(const std::string& URL) // Visit a substrata 'sub://' URL.  Checks hostname and only reconnects if the hostname is different from the current one.
{
	URLParseResults parse_res = URLParser::parseURL(::stripHeadAndTailWhitespace(URL));

	const std::string hostname = parse_res.hostname;
	const std::string worldname = parse_res.worldname;

	if(parse_res.parsed_parcel_uid)
		this->url_parcel_uid = parse_res.parcel_uid;
	else
		this->url_parcel_uid = -1;

	if(hostname != this->server_hostname || worldname != this->server_worldname)
	{
		// Connect to a different server!
		connectToServer(parse_res);
	}

	// If we had a URL with a parcel UID, like sub://substrata.info/parcel/10, then look up the parcel to get its position, then go there.
	// Note that this could fail if the parcels are not loaded yet.
	if(parse_res.parsed_parcel_uid)
	{
		Lock lock(this->world_state->mutex);
		const auto res = this->world_state->parcels.find(ParcelID(parse_res.parcel_uid));
		if(res != this->world_state->parcels.end())
		{
			this->cam_controller.setFirstAndThirdPersonPositions(res->second->getVisitPosition());
			this->player_physics.setEyePosition(res->second->getVisitPosition());
			showInfoNotification("Jumped to parcel " + toString(parse_res.parcel_uid));
		}
		else
			throw glare::Exception("Could not find parcel with id " + toString(parse_res.parcel_uid));
	}
	else
	{
		this->cam_controller.setAngles(Vec3d(/*heading=*/::degreeToRad(parse_res.heading), /*pitch=*/Maths::pi_2<double>(), /*roll=*/0));
		this->cam_controller.setFirstAndThirdPersonPositions(Vec3d(parse_res.x, parse_res.y, parse_res.z));
		this->player_physics.setEyePosition(Vec3d(parse_res.x, parse_res.y, parse_res.z));
	}
}


void GUIClient::disconnectFromServerAndClearAllObjects() // Remove any WorldObjectRefs held by GUIClient.
{
	udp_socket = NULL;

	load_item_queue.clear();
	model_and_texture_loader_task_manager.cancelAndWaitForTasksToComplete(); 
	model_loaded_messages_to_process.clear();
	texture_loaded_messages_to_process.clear();

	upload_queue.clear();

	// Kill any existing threads connected to the server
	net_resource_download_thread_manager.killThreadsBlocking();
	client_udp_handler_thread_manager.killThreadsBlocking();
	mic_read_thread_manager.killThreadsBlocking();

#if defined(EMSCRIPTEN)
	emscripten_resource_downloader.shutdown();
#endif

	this->client_thread_manager.killThreadsNonBlocking(); // Suggests to client_thread to quit, by calling ClientThread::kill(), which sets should_die = 1.
	resource_download_thread_manager.killThreadsNonBlocking(); // Suggests to DownloadResourcesThreads to quit, by calling DownloadResourcesThread::kill(), which sets should_die = 1.
	resource_upload_thread_manager.killThreadsNonBlocking(); // Suggests to UploadResourcesThreads to quit, by calling UploadResourcesThread::kill(), which sets should_die = 1.

	// Wait for some period of time to see if client_thread and resource download threads quit.  If not, hard-kill them by calling killConnection().
	Timer timer;
	while((this->client_thread_manager.getNumThreads() > 0) || (resource_download_thread_manager.getNumThreads() > 0) || (resource_upload_thread_manager.getNumThreads() > 0)) // While client_thread or a resource download thread is still running:
	{
		if(timer.elapsed() > 1.0)
		{
			logAndConPrintMessage("Reached time limit waiting for client_thread or resource download threads to close.  Hard-killing connection(s)");
			logAndConPrintMessage("this->client_thread_manager.getNumThreads(): " + toString(this->client_thread_manager.getNumThreads()));
			logAndConPrintMessage("this->resource_download_thread_manager.getNumThreads(): " + toString(this->resource_download_thread_manager.getNumThreads()));
			logAndConPrintMessage("this->resource_upload_thread_manager.getNumThreads(): " + toString(this->resource_upload_thread_manager.getNumThreads()));

			if(this->client_thread_manager.getNumThreads() > 0)
			{
				if(client_thread.nonNull())
					this->client_thread->killConnection(); // Calls ungracefulShutdown on socket, which should interrupt any blocking socket calls.

				this->client_thread = NULL;
				this->client_thread_manager.killThreadsBlocking();
			}

			if(resource_download_thread_manager.getNumThreads() > 0)
			{
				Lock lock(resource_download_thread_manager.getMutex());
				for(auto it = resource_download_thread_manager.getThreads().begin(); it != resource_download_thread_manager.getThreads().end(); ++it)
				{
					Reference<MessageableThread> thread = *it;
					assert(thread.isType<DownloadResourcesThread>());
					if(thread.isType<DownloadResourcesThread>())
						thread.downcastToPtr<DownloadResourcesThread>()->killConnection();
				}
			}

			if(resource_upload_thread_manager.getNumThreads() > 0)
			{
				Lock lock(resource_upload_thread_manager.getMutex());
				for(auto it = resource_upload_thread_manager.getThreads().begin(); it != resource_upload_thread_manager.getThreads().end(); ++it)
				{
					Reference<MessageableThread> thread = *it;
					assert(thread.isType<UploadResourceThread>());
					if(thread.isType<UploadResourceThread>())
						thread.downcastToPtr<UploadResourceThread>()->killConnection();
				}
			}

			break;
		}

		PlatformUtils::Sleep(10);
	}
	this->client_thread = NULL; // Need to make sure client_thread is destroyed, since it hangs on to a bunch of references.
	resource_download_thread_manager.killThreadsBlocking();
	resource_upload_thread_manager.killThreadsBlocking();

	this->client_avatar_uid = UID::invalidUID();
	this->server_protocol_version = 0;


	this->logged_in_user_id = UserID::invalidUserID();
	this->logged_in_user_name = "";
	this->logged_in_user_flags = 0;

	this->server_using_lod_chunks = false;

	ui_interface->setTextAsNotLoggedIn();

	ui_interface->updateWorldSettingsControlsEditable();

	ui_interface->updateOnlineUsersList();
	//ui->onlineUsersTextEdit->clear();
	ui_interface->clearChatMessages();
	//ui->chatMessagesTextEdit->clear();

	gesture_ui.untoggleMicButton(); // Since mic_read_thread_manager has thread killed above.

	minimap = nullptr;

	deselectObject();

	vehicle_controller_inside = NULL;
	vehicle_controllers.clear();

	// Remove all objects, parcels, avatars etc.. from OpenGL engine and physics engine
	if(world_state.nonNull())
	{
		Lock lock(this->world_state->mutex);

		for(auto it = world_state->objects.valuesBegin(); it != world_state->objects.valuesEnd(); ++it)
		{
			WorldObject* ob = it.getValue().ptr();

			if(ob->opengl_engine_ob)
			{
				removeAnimatedTextureUse(*ob->opengl_engine_ob, *animated_texture_manager);
				opengl_engine->removeObject(ob->opengl_engine_ob);
			}

			if(ob->opengl_light.nonNull())
				opengl_engine->removeLight(ob->opengl_light);

			if(ob->physics_object.nonNull())
			{
				this->physics_world->removeObject(ob->physics_object);
				ob->physics_object = NULL;
			}

			if(ob->audio_source.nonNull())
			{
				this->audio_engine.removeSource(ob->audio_source);
				ob->audio_state = WorldObject::AudioState_NotLoaded;
			}

			removeInstancesOfObject(ob);
		}

		for(auto it = world_state->parcels.begin(); it != world_state->parcels.end(); ++it)
		{
			Parcel* parcel = it->second.ptr();

			if(parcel->opengl_engine_ob.nonNull())
			{
				opengl_engine->removeObject(parcel->opengl_engine_ob);
				parcel->opengl_engine_ob = NULL;
			}

			if(parcel->physics_object.nonNull())
			{
				this->physics_world->removeObject(parcel->physics_object);
				parcel->physics_object = NULL;
			}
		}

		for(auto it = world_state->avatars.begin(); it != world_state->avatars.end(); ++it)
		{
			Avatar* avatar = it->second.ptr();

			avatar->entered_vehicle = NULL;

			if(avatar->nametag_gl_ob.nonNull())
				opengl_engine->removeObject(avatar->nametag_gl_ob);
			avatar->nametag_gl_ob = NULL;
			if(avatar->speaker_gl_ob.nonNull())
				opengl_engine->removeObject(avatar->speaker_gl_ob);
			avatar->speaker_gl_ob = NULL;

			avatar->graphics.destroy(*opengl_engine);

			hud_ui.removeMarkerForAvatar(avatar); // Remove any marker for the avatar from the HUD
			if(minimap)
				minimap->removeMarkerForAvatar(avatar); // Remove any marker for the avatar from the minimap
		}

		for(auto it = world_state->lod_chunks.begin(); it != world_state->lod_chunks.end(); ++it)
		{
			LODChunk* chunk = it->second.ptr();
			if(chunk->graphics_ob)
			{
				opengl_engine->removeObject(chunk->graphics_ob);
				chunk->graphics_ob = NULL;
				chunk->mesh_manager_data = NULL;
			}
		}
	}

	if(biome_manager && opengl_engine.nonNull() && physics_world.nonNull())
		biome_manager->clear(*opengl_engine, *physics_world);

	selected_ob = NULL;
	selected_parcel = NULL;


	active_objects.clear();
	obs_with_animated_tex.clear();
	web_view_obs.clear();
	browser_vid_player_obs.clear();
	audio_obs.clear();
	obs_with_scripts.clear();
	obs_with_diagnostic_vis.clear();

	scripted_ob_proximity_checker.clear();

	path_controllers.clear();

	objs_with_lightmap_rebuild_needed.clear();

	proximity_loader.clearAllObjects();

	cur_loading_mesh_data = NULL;


	if(terrain_decal_manager.nonNull())
	{
		terrain_decal_manager->clear();
		terrain_decal_manager = NULL;
	}

	if(particle_manager.nonNull())
	{
		particle_manager->clear();
		particle_manager = NULL;
	}

#if EMSCRIPTEN
	async_texture_loader = NULL;
#endif

	if(terrain_system.nonNull())
	{
		terrain_system->shutdown();
		terrain_system = NULL;
	}

	//this->ui->indigoView->shutdown();

	// Clear textures_processing set etc.
	textures_processing.clear();
	models_processing.clear();
	audio_processing.clear();
	script_content_processing.clear();
	//scatter_info_processing.clear();

	texture_server->clear();

	world_state = NULL;

	if(physics_world.nonNull())
	{
		assert(physics_world->getNumObjects() == 0);
		physics_world->clear();
	}
}


void GUIClient::connectToServer(const URLParseResults& parse_res)
{
	this->last_url_parse_results = parse_res;

	// By default, randomly vary the spawn position a bit so players don't spawn inside other players.
	const double spawn_r = 4.0;
	Vec3d spawn_pos = Vec3d(-spawn_r + 2 * spawn_r * rng.unitRandom(), -spawn_r + 2 * spawn_r * rng.unitRandom(), PlayerPhysics::getEyeHeight());

	this->server_hostname = parse_res.hostname;
	this->server_worldname = parse_res.worldname;

	if(parse_res.parsed_parcel_uid)
		this->url_parcel_uid = parse_res.parcel_uid;
	else
		this->url_parcel_uid = -1;

	if(parse_res.parsed_x)
		spawn_pos.x = parse_res.x;
	if(parse_res.parsed_y)
		spawn_pos.y = parse_res.y;
	if(parse_res.parsed_z)
		spawn_pos.z = parse_res.z;

	//-------------------------------- Do disconnect process --------------------------------
	disconnectFromServerAndClearAllObjects();
	//-------------------------------- End disconnect process --------------------------------


	//-------------------------------- Do connect process --------------------------------

	// Start downloading extracted_avatar_anim.bin with an Async HTTP request.
	// When it's done this->extracted_anim_data_loaded will be set in onAnimDataLoad().
	// We will postpone loading avatar models until extracted_avatar_anim.bin is loaded.
#if EMSCRIPTEN
	if(!extracted_anim_data_loaded)
	{
		const std::string http_URL = "/webclient/data/resources/extracted_avatar_anim.bin";

		const std::string local_abs_path = "/extracted_avatar_anim.bin";

		emscripten_async_wget2(http_URL.c_str(), local_abs_path.c_str(), /*requesttype =*/"GET", /*POST params=*/"", /*userdata arg=*/this, onAnimDataLoad, onAnimDataError, onAnimDataProgress);
	}
#endif

	// Move player position back to near origin.
	this->cam_controller.setAngles(Vec3d(/*heading=*/::degreeToRad(parse_res.heading), /*pitch=*/Maths::pi_2<double>(), /*roll=*/0));
	this->cam_controller.setFirstAndThirdPersonPositions(spawn_pos);

	if(parse_res.parsed_sun_vert_angle || parse_res.parsed_sun_azimuth_angle)
	{
		const float theta = myClamp(::degreeToRad((float)parse_res.sun_vert_angle), 0.01f, Maths::pi<float>() - 0.01f);
		const float phi   = ::degreeToRad((float)parse_res.sun_azimuth_angle);
		const Vec4f sundir = GeometrySampling::dirForSphericalCoords(phi, theta);
		opengl_engine->setSunDir(sundir);
	}

	world_state = new WorldState();
	world_state->url_whitelist->loadDefaultWhitelist();

	TracyMessageL("Creating ClientThread");

	client_thread = new ClientThread(&msg_queue, server_hostname, server_port, server_worldname, this->client_tls_config, this->world_ob_pool_allocator);
	client_thread->world_state = world_state;
	client_thread_manager.addThread(client_thread);

#if defined(EMSCRIPTEN)
	emscripten_resource_downloader.init(&msg_queue, resource_manager, server_hostname, server_port, &this->num_non_net_resources_downloading, &this->download_queue, this);
#else
	for(int z=0; z<4; ++z)
		resource_download_thread_manager.addThread(new DownloadResourcesThread(&msg_queue, resource_manager, server_hostname, server_port, &this->num_non_net_resources_downloading, this->client_tls_config,
			&this->download_queue));

	for(int i=0; i<4; ++i)
		net_resource_download_thread_manager.addThread(new NetDownloadResourcesThread(&msg_queue, resource_manager, &num_net_resources_downloading));
#endif // end if !defined(EMSCRIPTEN)

	if(physics_world.isNull())
	{
		physics_world = new PhysicsWorld(high_priority_task_manager, &this->stack_allocator);
		physics_world->event_listener = this;
		player_physics.init(*physics_world, spawn_pos);

		//car_physics.init(*physics_world);
	}
	else
	{
		this->player_physics.setEyePosition(spawn_pos);
	}

	// When the player spawns, gravity will be turned off, so they don't e.g. fall through buildings before they have been loaded.
	// Turn it on as soon as the player tries to move.
	this->player_physics.setGravityEnabled(false);

	this->sent_perform_gesture_without_stop_gesture = false;

#if EMSCRIPTEN
	// For Emscripten, since this data is loaded from the webserver, now we know the server hostname, start loading these textures asynchronously.
	this->async_texture_loader = new AsyncTextureLoader(this->server_hostname, /*url_path_prefix=*/"/webclient/data", opengl_engine.ptr());
	opengl_engine->startAsyncLoadingData(this->async_texture_loader.ptr());
#endif

	assert(terrain_decal_manager.isNull());
	terrain_decal_manager = new TerrainDecalManager(this->base_dir_path, async_texture_loader.ptr(), opengl_engine.ptr());

	assert(particle_manager.isNull());
	particle_manager = new ParticleManager(this->base_dir_path, async_texture_loader.ptr(), opengl_engine.ptr(), physics_world.ptr(), terrain_decal_manager.ptr());

	// Note that getFirstPersonPosition() is used for consistency with proximity_loader.updateCamPos() calls, where getFirstPersonPosition() is used also.
	const js::AABBox initial_aabb = proximity_loader.setCameraPosForNewConnection(this->cam_controller.getFirstPersonPosition().toVec4fPoint());

	// Send QueryObjectsInAABB for initial volume around camera to server
	{
		// Make QueryObjectsInAABB packet and enqueue to send
		MessageUtils::initPacket(scratch_packet, Protocol::QueryObjectsInAABB);
		writeToStream<double>(this->cam_controller.getPosition(), scratch_packet); // Send camera position
		scratch_packet.writeFloat((float)initial_aabb.min_[0]);
		scratch_packet.writeFloat((float)initial_aabb.min_[1]);
		scratch_packet.writeFloat((float)initial_aabb.min_[2]);
		scratch_packet.writeFloat((float)initial_aabb.max_[0]);
		scratch_packet.writeFloat((float)initial_aabb.max_[1]);
		scratch_packet.writeFloat((float)initial_aabb.max_[2]);

		enqueueMessageToSend(*this->client_thread, scratch_packet);
	}


	updateGroundPlane();

	minimap = nullptr;
	minimap = new MiniMap(opengl_engine, /*gui_client_=*/this, gl_ui);

	// Init indigoView
	/*this->ui->indigoView->initialise(this->base_dir_path);
	{
		Lock lock(this->world_state->mutex);
		this->ui->indigoView->addExistingObjects(*this->world_state, *this->resource_manager);
	}*/

	this->connection_state = ServerConnectionState_Connecting;
}


static float sensorWidth() { return 0.035f; }
static float lensSensorDist() { return 0.025f; }


Vec4f GUIClient::getDirForPixelTrace(int pixel_pos_x, int pixel_pos_y) const
{
	const Vec4f forwards = cam_controller.getForwardsVec().toVec4fVector();
	const Vec4f right = cam_controller.getRightVec().toVec4fVector();
	const Vec4f up = cam_controller.getUpVec().toVec4fVector();

	const float sensor_width = ::sensorWidth();
	const float sensor_height = sensor_width / opengl_engine->getViewPortAspectRatio();//ui->glWidget->viewport_aspect_ratio;
	const float lens_sensor_dist = ::lensSensorDist();

	const float gl_w = (float)opengl_engine->getMainViewPortWidth();
	const float gl_h = (float)opengl_engine->getMainViewPortHeight();

	const float s_x = sensor_width  * (float)(pixel_pos_x - gl_w/2) / gl_w;  // dist right on sensor from centre of sensor
	const float s_y = sensor_height * (float)(pixel_pos_y - gl_h/2) / gl_h; // dist down on sensor from centre of sensor

	const float r_x = s_x / lens_sensor_dist;
	const float r_y = s_y / lens_sensor_dist;

	const Vec4f dir = normalise(forwards + right * r_x - up * r_y);
	return dir;
}


/*
Let line coords in ws be p_ws(t) = a + b * t

pixel coords for a point p_ws are

cam_to_p = p_ws - cam_origin

r_x =  dot(cam_to_p, cam_right) / dot(cam_to_p, cam_forw)
r_y = -dot(cam_to_p, cam_up)    / dot(cam_to_p, cam_forw)

and

pixel_x = gl_w * (lens_sensor_dist / sensor_width  * r_x + 1/2)
pixel_y = gl_h * (lens_sensor_dist / sensor_height * r_y + 1/2)

let R = lens_sensor_dist / sensor_width

so 

pixel_x = gl_w * (R *  dot(p_ws - cam_origin, cam_right) / dot(p_ws - cam_origin, cam_forw) + 1/2)
pixel_y = gl_h * (R * -dot(p_ws - cam_origin, cam_up)    / dot(p_ws - cam_origin, cam_forw) + 1/2)

pixel_x = gl_w * (R *  dot(a + b * t - cam_origin, cam_right) / dot(a + b * t - cam_origin, cam_forw) + 1/2)
pixel_y = gl_h * (R * -dot(a + b * t - cam_origin, cam_up)    / dot(a + b * t - cam_origin, cam_forw) + 1/2)

We know pixel_x and pixel_y, want to solve for t.

pixel_x = gl_w * (R * dot(a + b * t - cam_origin, cam_right) / dot(a + b * t - cam_origin, cam_forw) + 1/2)
pixel_x/gl_w = R * dot(a + b * t - cam_origin, cam_right) / dot(a + b * t - cam_origin, cam_forw) + 1/2
pixel_x/gl_w = R * [dot(a - cam_origin, cam_right) + dot(b * t, cam_right)] / [dot(a - cam_origin, cam_forw) + dot(b * t, cam_forw)] + 1/2
pixel_x/gl_w - 1/2 = R  * [dot(a - cam_origin, cam_right) + dot(b * t, cam_right)] / [dot(a - cam_origin, cam_forw) + dot(b * t, cam_forw)]
(pixel_x/gl_w - 1/2) / R = [dot(a - cam_origin, cam_right) + dot(b * t, cam_right)] / [dot(a - cam_origin, cam_forw) + dot(b * t, cam_forw)]

let A = dot(a - cam_origin, cam_forw)
let B = dot(b, cam_forw)
let C = (pixel_x/gl_w - 1/2) / R
let D = dot(a - cam_origin, cam_right)
let E = dot(b, cam_right)

so we get

C = [D + dot(b * t, cam_right)] / [A + dot(b * t, cam_forw)]
C = [D + dot(b, cam_right) * t] / [A + dot(b, cam_forw) * t]
C = [D + E * t] / [A + B * t]
[A + B * t] C = D + E * t
AC + BCt = D + Et
BCt - Et = D - AC
t(BC - E) = D - AC
t = (D - AC) / (BC - E)


For y (used when all x coordinates are ~ the same)
pixel_y = gl_h * (R * -dot(a + b * t - cam_origin, cam_up) / dot(a + b * t - cam_origin, cam_forw) + 1/2)
pixel_y/gl_h = R * -dot(a + b * t - cam_origin, cam_up) / dot(a + b * t - cam_origin, cam_forw) + 1/2
pixel_y/gl_h = R * -[dot(a - cam_origin, cam_up) + dot(b * t, cam_up)] / [dot(a - cam_origin, cam_forw) + dot(b * t, cam_forw)] + 1/2
pixel_x/gl_w - 1/2 = R  * -[dot(a - cam_origin, cam_up) + dot(b * t, cam_up)] / [dot(a - cam_origin, cam_forw) + dot(b * t, cam_forw)]
(pixel_x/gl_w - 1/2) / R = -[dot(a - cam_origin, cam_right) + dot(b * t, cam_right)] / [dot(a - cam_origin, cam_forw) + dot(b * t, cam_forw)]

let A = dot(a - cam_origin, cam_forw)
let B = dot(b, cam_forw)
let C = (pixel_y/gl_h - 1/2) / R
let D = dot(a - cam_origin, cam_up)
let E = dot(b, cam_up)

C = -[D + dot(b * t, cam_up)] / [A + dot(b * t, cam_forw)]
C = -[D + dot(b, cam_right) * t] / [A + dot(b, cam_forw) * t]
C = -[D + E * t] / [A + B * t]
[A + B * t] C = -[D + E * t]
AC + BCt = -D - Et
BCt + Et = -D - AC
t(BC + E) = -D - AC
t = (-D - AC) / (BC + E)

*/

Vec4f GUIClient::pointOnLineWorldSpace(const Vec4f& p_a_ws, const Vec4f& p_b_ws, const Vec2f& pixel_coords) const
{
	const Vec4f cam_origin = cam_controller.getPosition().toVec4fPoint();
	const Vec4f cam_forw   = cam_controller.getForwardsVec().toVec4fVector();
	const Vec4f cam_right  = cam_controller.getRightVec().toVec4fVector();
	const Vec4f cam_up     = cam_controller.getUpVec().toVec4fVector();

	const float sensor_width  = ::sensorWidth();
	const float sensor_height = sensor_width / opengl_engine->getViewPortAspectRatio();//ui->glWidget->viewport_aspect_ratio;
	const float lens_sensor_dist = ::lensSensorDist();

	const float gl_w = (float)opengl_engine->getMainViewPortWidth(); // ui->glWidget->geometry().width();
	const float gl_h = (float)opengl_engine->getMainViewPortHeight(); // ui->glWidget->geometry().height();

	const Vec4f a = p_a_ws;
	const Vec4f b = normalise(p_b_ws - p_a_ws);

	float A = dot(a - cam_origin, cam_forw);
	float B = dot(b, cam_forw);
	float C = (pixel_coords.x/gl_w - 0.5f) * sensor_width / lens_sensor_dist;
	float D = dot(a - cam_origin, cam_right);
	float E = dot(b, cam_right);

	const float denom = B*C - E;
	float t;
	if(fabs(denom) > 1.0e-4f)
	{
		t = (D - A*C) / denom;
	}
	else
	{
		// Work with y instead

		A = dot(a - cam_origin, cam_forw);
		B = dot(b, cam_forw);
		C = (pixel_coords.y/gl_h - 0.5f) * sensor_height / lens_sensor_dist;
		D = dot(a - cam_origin, cam_up);
		E = dot(b, cam_up);

		t = (-D - A*C) / (B*C + E);
	}

	return a + b * t;
}


/*
s_x is distance left on sensor:
s_x = sensor_width * (pixel_x - gl_w/2) / gl_w

Let r_x = (cam_to_point, forw) / (cam_to_point, right)
From similar triangles,
r_x = s_x / lens_sensor_dist, where s_x is distance left on sensor.

so
r_x = sensor_width * (pixel_x - gl_w/2) / (gl_w * lens_sensor_dist)

(gl_w * lens_sensor_dist) * r_x = sensor_width * (pixel_x - gl_w/2)
gl_w * lens_sensor_dist * r_x = sensor_width * pixel_x - sensor_width * gl_w/2
gl_w * lens_sensor_dist * r_x + sensor_width * gl_w/2 = sensor_width * pixel_x

pixel_x = (gl_w * lens_sensor_dist * r_x + sensor_width * gl_w/2) / sensor_width
pixel_x = gl_w * (lens_sensor_dist * r_x + sensor_width / 2) / sensor_width;
pixel_x = gl_w * (lens_sensor_dist * r_x / sensor_width + 1/2);
pixel_x = gl_w * (lens_sensor_dist / sensor_width * r_x + 1/2);
*/
bool GUIClient::getPixelForPoint(const Vec4f& point_ws, Vec2f& pixel_coords_out) const// Returns true if point is visible from camera.
{
	const Vec4f forwards = cam_controller.getForwardsVec().toVec4fVector();
	const Vec4f right = cam_controller.getRightVec().toVec4fVector();
	const Vec4f up = cam_controller.getUpVec().toVec4fVector();

	const float sensor_width  = ::sensorWidth();
	const float sensor_height = sensor_width / opengl_engine->getViewPortAspectRatio();//ui->glWidget->viewport_aspect_ratio;
	const float lens_sensor_dist = ::lensSensorDist();

	const float gl_w = (float)opengl_engine->getMainViewPortWidth(); // ui->glWidget->geometry().width();
	const float gl_h = (float)opengl_engine->getMainViewPortHeight(); // ui->glWidget->geometry().height();

	const Vec4f cam_to_point = point_ws - this->cam_controller.getPosition().toVec4fPoint();
	if(dot(cam_to_point, forwards) < 0.001)
		return false; // point behind camera.

	const float r_x =  dot(cam_to_point, right) / dot(cam_to_point, forwards);
	const float r_y = -dot(cam_to_point, up)    / dot(cam_to_point, forwards);

	const float pixel_x = (gl_w * lens_sensor_dist * r_x + sensor_width  * gl_w/2) / sensor_width;
	const float pixel_y = (gl_h * lens_sensor_dist * r_y + sensor_height * gl_h/2) / sensor_height;

	pixel_coords_out = Vec2f(pixel_x, pixel_y);
	return true;
}


/*
Returns OpenGL UI coords on GL widget, for a world space point.   See GLUI.h for a description of the GL UI coordinate space.
Let r_x = (cam_to_point, forw) / (cam_to_point, right)

From similar triangles,
r_x = s_x / lens_sensor_dist, where s_x is distance left on sensor.
so s_x = r_x lens_sensor_dist

Let normalised coord left on sensor  n_x = s_x / (sensor_width/2)

so n_x = (r_x lens_sensor_dist) / (sensor_width/2) = 2 r_x lens_sensor_dist / sensor_width
*/
bool GUIClient::getGLUICoordsForPoint(const Vec4f& point_ws, Vec2f& coords_out) const// Returns true if point is visible from camera.
{
	const Vec4f forwards = cam_controller.getForwardsVec().toVec4fVector();
	const Vec4f right = cam_controller.getRightVec().toVec4fVector();
	const Vec4f up = cam_controller.getUpVec().toVec4fVector();

	const float sensor_width  = ::sensorWidth();
	const float lens_sensor_dist = ::lensSensorDist();

	const Vec4f cam_to_point = point_ws - this->cam_controller.getPosition().toVec4fPoint();
	if(dot(cam_to_point, forwards) < 0.001)
		return false; // point behind camera.

	const float r_x = dot(cam_to_point, right) / dot(cam_to_point, forwards);
	const float r_y = dot(cam_to_point, up)    / dot(cam_to_point, forwards);

	const float n_x = 2.f * (lens_sensor_dist * r_x) / sensor_width;
	const float n_y = 2.f * (lens_sensor_dist * r_y) / sensor_width;

	coords_out = Vec2f(n_x, n_y);
	return true;
}


// See https://math.stackexchange.com/questions/1036959/midpoint-of-the-shortest-distance-between-2-rays-in-3d
// In particular this answer: https://math.stackexchange.com/a/2371053
static inline Vec4f closestPointOnLineToRay(const LineSegment4f& line, const Vec4f& origin, const Vec4f& unitdir)
{
	const Vec4f a = line.a;
	const Vec4f b = normalise(line.b - line.a);

	const Vec4f c = origin;
	const Vec4f d = unitdir;

	const float t = (dot(c - a, b) + dot(a - c, d) * dot(b, d)) / (1 - Maths::square(dot(b, d)));

	return a + b * t;
}


static LineSegment4f clipLineSegmentToCameraFrontHalfSpace(const LineSegment4f& segment, const Planef& cam_front_plane)
{
	const float d_a = cam_front_plane.signedDistToPoint(segment.a);
	const float d_b = cam_front_plane.signedDistToPoint(segment.b);

	// If both endpoints are in front half-space, no clipping is required.  If both points are in back half-space, line segment is completely clipped.
	// In this case just return the unclipped line segment.
	if((d_a < 0 && d_b < 0) || (d_a > 0 && d_b > 0))
		return segment;

	/*
	
	a                  /         b
	------------------/----------
	d_a              /   d_b

	*/
	if(d_a > 0)
	{
		assert(d_b < 0);
		const float frac = d_a / (d_a - d_b); // = d_a / (d_a + |d_b|)
		return LineSegment4f(segment.a, Maths::lerp(segment.a, segment.b, frac));
	}
	else
	{
		assert(d_a < 0);
		assert(d_b >= 0);
		const float frac = -d_a / (-d_a + d_b); // = |d_a| / (|d_a| + d_b)
		return LineSegment4f(segment.b, Maths::lerp(segment.a, segment.b, frac));
	}
}


// Returns the axis index (integer in [0, 3)) of the closest axis arrow, or the axis index of the closest rotation arc handle (integer in [3, 6))
// or -1 if no arrow or rotation arc close to pixel coords.
// Also returns world space coords of the closest point.
int GUIClient::mouseOverAxisArrowOrRotArc(const Vec2f& pixel_coords, Vec4f& closest_seg_point_ws_out) 
{
	if(!axis_and_rot_obs_enabled)
		return -1;

	const Vec2f clickpos = pixel_coords;

	float closest_dist = 10000;
	int closest_axis = -1;
	const float max_selection_dist = 12;

	// Test against axis arrows
	for(int i=0; i<NUM_AXIS_ARROWS; ++i)
	{
		const LineSegment4f unclipped_segment = axis_arrow_segments[i];

		// Clip line segment to camera front half-space, otherwise projection of segment endpoints to screenspace will fail.
		const Planef cam_front_plane(/*point=*/this->cam_controller.getPosition().toVec4fPoint() + cam_controller.getForwardsVec().toVec4fVector() * 0.01f, /*normal=*/cam_controller.getForwardsVec().toVec4fVector());
		const LineSegment4f segment = clipLineSegmentToCameraFrontHalfSpace(unclipped_segment, cam_front_plane);

		Vec2f start_pixelpos, end_pixelpos; // pixel coords of line segment start and end.
		bool start_visible = getPixelForPoint(segment.a, start_pixelpos);
		bool end_visible   = getPixelForPoint(segment.b, end_pixelpos);

		if(start_visible && end_visible)
		{
			const float d = pointLineSegmentDist(clickpos, start_pixelpos, end_pixelpos);

			const Vec4f dir = getDirForPixelTrace((int)pixel_coords.x, (int)pixel_coords.y);
			const Vec4f origin = cam_controller.getPosition().toVec4fPoint();

			const Vec4f closest_line_pt = closestPointOnLineToRay(segment, origin, dir);

			// As the axis arrow gets closer to the camera, it will appear larger.  Increase the selection distance (from arrow centre line to mouse point) accordingly.
			const float cam_dist = closest_line_pt.getDist(origin);

			const float gl_w = (float)opengl_engine->getMainViewPortWidth(); // ui->glWidget->geometry().width();
			const float approx_radius_px = 0.03f * gl_w / cam_dist;
			const float use_max_select_dist = myMax(max_selection_dist, approx_radius_px);

			if(d <= closest_dist && d < use_max_select_dist)
			{
				closest_seg_point_ws_out = closest_line_pt;
				closest_dist = d;
				closest_axis = i;
			}
		}
	}

	// Test against rotation arc handles
	for(int i=0; i<3; ++i)
	{
		for(size_t z=0; z<rot_handle_lines[i].size(); ++z)
		{
			const LineSegment4f segment = (rot_handle_lines[i])[z];

			Vec2f start_pixelpos, end_pixelpos; // pixel coords of line segment start and end.
			bool start_visible = getPixelForPoint(segment.a, start_pixelpos);
			bool end_visible   = getPixelForPoint(segment.b, end_pixelpos);

			if(start_visible && end_visible)
			{
				const float d = pointLineSegmentDist(clickpos, start_pixelpos, end_pixelpos);

				const Vec4f dir = getDirForPixelTrace((int)pixel_coords.x, (int)pixel_coords.y);
				const Vec4f origin = cam_controller.getPosition().toVec4fPoint();

				const Vec4f closest_line_pt = closestPointOnLineToRay(segment, origin, dir);

				// As the line segment gets closer to the camera, it will appear larger.  Increase the selection distance (from line to mouse point) accordingly.
				const float cam_dist = closest_line_pt.getDist(origin);

				const float gl_w = (float)opengl_engine->getMainViewPortWidth(); // ui->glWidget->geometry().width();
				const float approx_radius_px = 0.02f * gl_w / cam_dist;
				const float use_max_select_dist = myMax(max_selection_dist, approx_radius_px);

				if(d <= closest_dist && d < use_max_select_dist)
				{
					closest_seg_point_ws_out = closest_line_pt;
					closest_dist = d;
					closest_axis = NUM_AXIS_ARROWS + i;
				}
			}
		}
	}

	return closest_axis;
}


void GUIClient::mousePressed(MouseEvent& e)
{
	// conPrint("GUIClient::mousePressed");

	if(gl_ui.nonNull())
	{
		gl_ui->handleMousePress(e);
		if(e.accepted)
		{
			ui_interface->setCamRotationOnMouseDragEnabled(false); // If the user clicked on a UI widget, we don't want click+mouse dragging to move the camera.
			return;
		}
	}

	ui_interface->setCamRotationOnMouseDragEnabled(true);

	// Trace through scene to see if we are clicking on a web-view.  Send mousePressed events to the web view if so.
	if(this->physics_world.nonNull())
	{
		// Trace ray through scene
		const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
		const Vec4f dir = getDirForPixelTrace(e.cursor_pos.x, e.cursor_pos.y);

		RayTraceResult results;
		this->physics_world->traceRay(origin, dir, /*max_t=*/1.0e5f, /*ignore body id=*/JPH::BodyID(), results);

		if(results.hit_object && results.hit_object->userdata && results.hit_object->userdata_type == 0)
		{
			WorldObject* ob = static_cast<WorldObject*>(results.hit_object->userdata);

			const Vec4f hitpos_ws = origin + dir * results.hit_t;
			const Vec4f hitpos_os = results.hit_object->getWorldToObMatrix() * hitpos_ws;

			const Vec2f uvs = (hitpos_os[1] < 0.5f) ? // if y coordinate is closer to y=0 than y=1:
				Vec2f(hitpos_os[0],     hitpos_os[2]) : // y=0 face:
				Vec2f(1 - hitpos_os[0], hitpos_os[2]); // y=1 face:

			if(ob->web_view_data.nonNull()) // If this is a web-view object:
			{
				ob->web_view_data->mousePressed(&e, uvs);
			}
			else if(ob->browser_vid_player.nonNull()) // If this is a video object:
			{
				ob->browser_vid_player->mousePressed(&e, uvs);
			}
		}
	}

	if(this->selected_ob.nonNull() && this->selected_ob->opengl_engine_ob.nonNull())
	{
		// Don't try and grab an axis etc.. when we are clicking on a voxel group to add/remove voxels.
		//bool mouse_trace_hit_selected_ob = false;
		//if(areEditingVoxels())
		//{
		//	RayTraceResult results;
		//	this->physics_world->traceRay(cam_controller.getPosition().toVec4fPoint(), getDirForPixelTrace(e->pos().x(), e->pos().y()), /*max_t=*/1.0e10f, results);
		//	
		//	mouse_trace_hit_selected_ob = results.hit_object && results.hit_object->userdata && results.hit_object->userdata_type == 0 && // If we hit an object,
		//		static_cast<WorldObject*>(results.hit_object->userdata) == this->selected_ob.ptr(); // and it was the selected ob
		//}

		const bool have_edit_permissions = objectModificationAllowed(*this->selected_ob);

		//if(!mouse_trace_hit_selected_ob)
		if(have_edit_permissions) // The axis arrows and rotation arcs are only visible if we have object modification permissions.
		{
			grabbed_axis = mouseOverAxisArrowOrRotArc(Vec2f((float)e.cursor_pos.x, (float)e.cursor_pos.y), /*closest_seg_point_ws_out=*/this->grabbed_point_ws);

			if(grabbed_axis >= 0) // If we grabbed an arrow or rotation arc:
			{
				this->ob_origin_at_grab = this->selected_ob->pos.toVec4fPoint();

				// Usually when the mouse button is held down, moving the mouse rotates the camera.
				// But when we have grabbed an arrow or rotation arc, it moves the object instead.  So don't rotate the camera.
				ui_interface->setCamRotationOnMouseDragEnabled(false);

				undo_buffer.startWorldObjectEdit(*this->selected_ob);
			}

			if(grabbed_axis >= NUM_AXIS_ARROWS) // If we grabbed a rotation arc:
			{
				const Vec4f arc_centre = this->selected_ob->opengl_engine_ob->ob_to_world_matrix.getColumn(3);

				const int rot_axis = grabbed_axis - NUM_AXIS_ARROWS;
				const Vec4f basis_a = basis_vectors[rot_axis*2];
				const Vec4f basis_b = basis_vectors[rot_axis*2 + 1];

				// Intersect ray from current mouse position with plane formed by rotation basis vectors
				const Vec4f origin = cam_controller.getPosition().toVec4fPoint();
				const Vec4f dir = getDirForPixelTrace(e.cursor_pos.x, e.cursor_pos.y);

				Planef plane(arc_centre, crossProduct(basis_a, basis_b));

				const float t = plane.rayIntersect(origin, dir);
				const Vec4f plane_p = origin + dir * t;

				const float angle = safeATan2(dot(plane_p - arc_centre, basis_b), dot(plane_p - arc_centre, basis_a));

				const Vec4f to_cam = cam_controller.getPosition().toVec4fPoint() - arc_centre;
				const float to_cam_angle = safeATan2(dot(basis_b, to_cam), dot(basis_a, to_cam)); // angle in basis_a-basis_b plane

				this->grabbed_angle = this->original_grabbed_angle = angle;
				this->grabbed_arc_angle_offset = to_cam_angle - this->original_grabbed_angle;

				//opengl_engine->addObject(opengl_engine->makeAABBObject(plane_p, plane_p + Vec4f(0.05f, 0.05f, 0.05f, 0), Colour4f(1, 0, 1, 1)));
			}
		}
	}

	if(selectedObjectIsVoxelOb())
	{
		const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
		const Vec4f dir = getDirForPixelTrace(e.cursor_pos.x, e.cursor_pos.y);
		RayTraceResult results;
		this->physics_world->traceRay(origin, dir, /*max_t=*/1.0e5f, /*ignore body id=*/JPH::BodyID(), results);
		if(results.hit_object)
		{
			const Vec4f hitpos_ws = origin + dir*results.hit_t;

			Vec2f pixel_coords;
			/*const bool visible = */getPixelForPoint(hitpos_ws, pixel_coords);

			if(selected_ob.nonNull())
			{
				selected_ob->decompressVoxels(); // Make sure voxels are decompressed for this object.

				if(BitUtils::isBitSet(e.modifiers, (uint32)Modifiers::Ctrl) || BitUtils::isBitSet(e.modifiers, (uint32)Modifiers::Alt)) // If user is trying to edit voxels:
				{
					const bool have_edit_permissions = objectModificationAllowedWithMsg(*selected_ob, "edit");
					if(have_edit_permissions)
					{
						const float current_voxel_w = 1;

						const Matrix4f world_to_ob = worldToObMatrix(*selected_ob);

						bool voxels_changed = false;

						if(BitUtils::isBitSet(e.modifiers, (uint32)Modifiers::Ctrl)) //e->modifiers & Qt::ControlModifier)
						{
							const Vec4f point_off_surface = hitpos_ws + results.hit_normal_ws * (current_voxel_w * 1.0e-3f);

							// Don't allow voxel creation if it is too far from existing voxels.
							// This is to prevent misclicks where the mouse pointer is just off an existing object, which may sometimes create a voxel very far away (after the ray intersects the ground plane for example)
							const float dist_from_aabb = selected_ob->opengl_engine_ob.nonNull() ? selected_ob->opengl_engine_ob->aabb_ws.distanceToPoint(point_off_surface) : 0.f;
							if(dist_from_aabb < 2.f)
							{
								undo_buffer.startWorldObjectEdit(*selected_ob);

								const Vec4f point_os = world_to_ob * point_off_surface;
								const Vec4f point_os_voxel_space = point_os / current_voxel_w;
								Vec3<int> voxel_indices((int)floor(point_os_voxel_space[0]), (int)floor(point_os_voxel_space[1]), (int)floor(point_os_voxel_space[2]));

								// Add the voxel!
								this->selected_ob->getDecompressedVoxels().push_back(Voxel());
								this->selected_ob->getDecompressedVoxels().back().pos = voxel_indices;
								this->selected_ob->getDecompressedVoxels().back().mat_index = ui_interface->getSelectedMatIndex();

								voxels_changed = true;

								undo_buffer.finishWorldObjectEdit(*selected_ob);
							}
							else
							{
								showErrorNotification("Can't create voxel that far away from rest of voxels.");
							}
						}
						if(BitUtils::isBitSet(e.modifiers, (uint32)Modifiers::Alt))
						{
							if(this->selected_ob->getDecompressedVoxels().size() > 1)
							{
								undo_buffer.startWorldObjectEdit(*selected_ob);

								const Vec4f point_under_surface = hitpos_ws - results.hit_normal_ws * (current_voxel_w * 1.0e-3f);

								const Vec4f point_os = world_to_ob * point_under_surface;
								const Vec4f point_os_voxel_space = point_os / current_voxel_w;
								Vec3<int> voxel_indices((int)floor(point_os_voxel_space[0]), (int)floor(point_os_voxel_space[1]), (int)floor(point_os_voxel_space[2]));

								// Remove the voxel, if present
								for(size_t z=0; z<this->selected_ob->getDecompressedVoxels().size(); ++z)
								{
									if(this->selected_ob->getDecompressedVoxels()[z].pos == voxel_indices)
										this->selected_ob->getDecompressedVoxels().erase(this->selected_ob->getDecompressedVoxels().begin() + z);
								}

								voxels_changed = true;

								undo_buffer.finishWorldObjectEdit(*selected_ob);
							}
							else
							{
								showErrorNotification("Can't delete last voxel in voxel group.  Delete entire voxel object ('delete' key) to remove it.");
							}
						}

						if(voxels_changed)
						{
							updateObjectModelForChangedDecompressedVoxels(this->selected_ob);
						}
					}
				}
			}
		}
	}

	// If we didn't grab any control, we will be in camera-rotate mode, so hide the mouse cursor.
	if(grabbed_axis < 0)
	{
		ui_interface->hideCursor();
	}
}


void GUIClient::mouseReleased(MouseEvent& e)
{
	if(gl_ui.nonNull())
	{
		gl_ui->handleMouseRelease(e);
		if(e.accepted)
			return;
	}

	// If we were dragging an object along a movement axis, we have released the mouse button and hence finished the movement.  un-grab the axis.
	if(grabbed_axis != -1 && selected_ob.nonNull())
	{
		undo_buffer.finishWorldObjectEdit(*selected_ob);
		grabbed_axis = -1;
	}

	// Trace through scene to see if we are clicking on a web-view.  Send mouseReleased events to the web view if so.
	if(this->physics_world.nonNull())
	{
		// Trace ray through scene
		const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
		const Vec4f dir = getDirForPixelTrace(e.cursor_pos.x, e.cursor_pos.y);

		RayTraceResult results;
		this->physics_world->traceRay(origin, dir, /*max_t=*/1.0e5f, /*ignore body id=*/JPH::BodyID(), results);

		if(results.hit_object && results.hit_object->userdata && results.hit_object->userdata_type == 0)
		{
			WorldObject* ob = static_cast<WorldObject*>(results.hit_object->userdata);

			const Vec4f hitpos_ws = origin + dir * results.hit_t;
			const Vec4f hitpos_os = results.hit_object->getWorldToObMatrix() * hitpos_ws;

			const Vec2f uvs = (hitpos_os[1] < 0.5f) ? // if y coordinate is closer to y=0 than y=1:
				Vec2f(hitpos_os[0],     hitpos_os[2]) : // y=0 face:
				Vec2f(1 - hitpos_os[0], hitpos_os[2]); // y=1 face:

			if(ob->web_view_data.nonNull()) // If this is a web-view object:
			{
				ob->web_view_data->mouseReleased(&e, uvs);
			}
			else if(ob->browser_vid_player.nonNull()) // If this is a video object:
			{
				ob->browser_vid_player->mouseReleased(&e, uvs);
			}
		}
	}
}


void GUIClient::mouseDoubleClicked(MouseEvent& mouse_event)
{
	if(gl_ui.nonNull())
	{
		gl_ui->handleMouseDoubleClick(mouse_event);
		if(mouse_event.accepted)
			return;
	}

	doObjectSelectionTraceForMouseEvent(mouse_event);
}


void GUIClient::updateObjectModelForChangedDecompressedVoxels(WorldObjectRef& ob)
{
	Lock lock(this->world_state->mutex);

	ob->compressVoxels();

	ob->last_modified_time = TimeStamp::currentTime(); // Gets set on server as well, this is just for updating the local display.

	// Clear lightmap URL, since the lightmap will be invalid now the voxels (and hence the UV map) will have changed.
	ob->lightmap_url = "";

	// Remove any existing OpenGL and physics model
	if(ob->opengl_engine_ob)
	{
		removeAnimatedTextureUse(*ob->opengl_engine_ob, *animated_texture_manager);
		opengl_engine->removeObject(ob->opengl_engine_ob);
	}

	if(ob->opengl_light.nonNull())
		opengl_engine->removeLight(ob->opengl_light);

	destroyVehiclePhysicsControllingObject(ob.ptr()); // Destroy any vehicle controller controlling this object, as vehicle controllers have pointers to physics bodies.
	if(ob->physics_object.nonNull())
	{
		physics_world->removeObject(ob->physics_object);
		ob->physics_object = NULL;
	}

	// Update in Indigo view
	//ui->indigoView->objectRemoved(*ob);

	if(!ob->getDecompressedVoxels().empty())
	{
		const Matrix4f ob_to_world = obToWorldMatrix(*ob);

		const int ob_lod_level = ob->getLODLevel(cam_controller.getPosition());

		js::Vector<bool, 16> mat_transparent(ob->materials.size());
		for(size_t i=0; i<ob->materials.size(); ++i)
			mat_transparent[i] = ob->materials[i]->opacity.val < 1.f;

		// Add updated model!
		PhysicsShape physics_shape;
		const int subsample_factor = 1;
		Reference<OpenGLMeshRenderData> gl_meshdata = ModelLoading::makeModelForVoxelGroup(ob->getDecompressedVoxelGroup(), subsample_factor, ob_to_world,
			opengl_engine->vert_buf_allocator.ptr(), /*do_opengl_stuff=*/true, /*need_lightmap_uvs=*/false, mat_transparent, /*build_dynamic_physics_ob=*/ob->isDynamic(),
			worker_allocator.ptr(),
			physics_shape);

		GLObjectRef gl_ob = opengl_engine->allocateObject();
		gl_ob->ob_to_world_matrix = ob_to_world;
		gl_ob->mesh_data = gl_meshdata;

		gl_ob->materials.resize(ob->materials.size());
		for(uint32 i=0; i<ob->materials.size(); ++i)
		{
			ModelLoading::setGLMaterialFromWorldMaterial(*ob->materials[i], ob_lod_level, ob->lightmap_url, /*use_basis=*/this->server_has_basis_textures, *this->resource_manager, gl_ob->materials[i]);
			gl_ob->materials[i].gen_planar_uvs = true;
			gl_ob->materials[i].draw_planar_uv_grid = true;
		}

		Reference<PhysicsObject> physics_ob = new PhysicsObject(/*collidable=*/ob->isCollidable());

		PhysicsShape use_shape = physics_shape;
		if(ob->centre_of_mass_offset_os != Vec3f(0.f))
			use_shape = PhysicsWorld::createCOMOffsetShapeForShape(use_shape, ob->centre_of_mass_offset_os.toVec4fVector());

		physics_ob->shape = use_shape;
		physics_ob->pos = ob->pos.toVec4fPoint();
		physics_ob->rot = Quatf::fromAxisAndAngle(normalise(ob->axis), ob->angle);
		physics_ob->scale = useScaleForWorldOb(ob->scale);

		ob->opengl_engine_ob = gl_ob;
		//opengl_engine->addObjectAndLoadTexturesImmediately(gl_ob);
		assignLoadedOpenGLTexturesToMats(ob.ptr()); // TEMP TEST

		// Update in Indigo view
		//ui->indigoView->objectAdded(*ob, *this->resource_manager);

		opengl_engine->selectObject(gl_ob);

		physics_ob->is_sensor = ob->isSensor();

		assert(ob->physics_object.isNull());
		ob->physics_object = physics_ob;
		physics_ob->userdata = (void*)(ob.ptr());
		physics_ob->userdata_type = 0;
		physics_ob->ob_uid = ob->uid;

		physics_ob->kinematic = !ob->script.empty();
		physics_ob->dynamic = ob->isDynamic();

		physics_world->addObject(physics_ob);

		ob->setAABBOS(gl_meshdata->aabb_os);
	}

	// Mark as from-local-dirty to send an object updated message to the server
	ob->from_local_other_dirty = true;
	this->world_state->dirty_from_local_objects.insert(ob);
}


void GUIClient::pickUpSelectedObject()
{
	if(selected_ob.nonNull())
	{
		const bool have_edit_permissions = objectModificationAllowedWithMsg(*this->selected_ob, "move");
		if(have_edit_permissions)
		{
			// Get selection_vec_cs
			const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
			const Vec4f forwards = cam_controller.getForwardsVec().toVec4fVector();
			const Vec4f right = cam_controller.getRightVec().toVec4fVector();
			const Vec4f up = cam_controller.getUpVec().toVec4fVector();

			const Vec4f selection_point_ws = obToWorldMatrix(*this->selected_ob) * this->selection_point_os;

			const Vec4f selection_vec_ws = selection_point_ws - origin;
			this->selection_vec_cs = Vec4f(dot(selection_vec_ws, right), dot(selection_vec_ws, forwards), dot(selection_vec_ws, up), 0.f);

			opengl_engine->setSelectionOutlineColour(PICKED_UP_OUTLINE_COLOUR);

			// Send UserSelectedObject message to server
			MessageUtils::initPacket(scratch_packet, Protocol::UserSelectedObject);
			writeToStream(selected_ob->uid, scratch_packet);
			enqueueMessageToSend(*this->client_thread, scratch_packet);

			showInfoNotification("Picked up object.");

			ui_interface->objectEditorObjectPickedUp();

			selected_ob_picked_up = true;

			undo_buffer.startWorldObjectEdit(*selected_ob);

			// Play pick up sound, in the direction of the selection point
			const Vec4f to_pickup_point = normalise(selection_point_ws - origin);
			audio_engine.playOneShotSound(resources_dir_path + "/sounds/select_mono.wav", origin + to_pickup_point * 0.4f);
		}
	}
}


void GUIClient::dropSelectedObject()
{
	if(selected_ob.nonNull() && selected_ob_picked_up)
	{
		// Send UserDeselectedObject message to server
		MessageUtils::initPacket(scratch_packet, Protocol::UserDeselectedObject);
		writeToStream(selected_ob->uid, scratch_packet);

		if(client_thread.nonNull())
			enqueueMessageToSend(*this->client_thread, scratch_packet);

		opengl_engine->setSelectionOutlineColour(DEFAULT_OUTLINE_COLOUR);

		showInfoNotification("Dropped object.");

		ui_interface->objectEditorObjectDropped();

		selected_ob_picked_up = false;

		undo_buffer.finishWorldObjectEdit(*selected_ob);

		// Play drop item sound, in the direction of the selection point.
		const Vec4f campos = this->cam_controller.getPosition().toVec4fPoint();
		const Vec4f selection_point_ws = obToWorldMatrix(*this->selected_ob) * this->selection_point_os;
		const Vec4f to_pickup_point = normalise(selection_point_ws - campos);

		audio_engine.playOneShotSound(base_dir_path + "/resources/sounds/deselect_mono.wav", campos + to_pickup_point * 0.4f);
	}
}


void GUIClient::doObjectSelectionTraceForMouseEvent(MouseEvent& e)
{
	// Trace ray through scene, select object (if any) that is clicked on.
	const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
	const Vec4f dir = getDirForPixelTrace(e.cursor_pos.x, e.cursor_pos.y);

	RayTraceResult results;
	this->physics_world->traceRay(origin, dir, /*max_t=*/1.0e5f, /*ignore body id=*/JPH::BodyID(), results);

	if(results.hit_object)
	{
		// Debugging: Add an object at the hit point
		//this->glWidget->addObject(glWidget->opengl_engine->makeAABBObject(this->selection_point_ws - Vec4f(0.03f, 0.03f, 0.03f, 0.f), this->selection_point_ws + Vec4f(0.03f, 0.03f, 0.03f, 0.f), Colour4f(0.6f, 0.6f, 0.2f, 1.f)));

		// Deselect any currently selected object
		if(this->selected_ob.nonNull())
			deselectObject();

		if(this->selected_parcel.nonNull())
			deselectParcel();

		if(results.hit_object->userdata && results.hit_object->userdata_type == 0) // If we hit an object:
		{
			selectObject(static_cast<WorldObject*>(results.hit_object->userdata), results.hit_mat_index);
		}
		else if(results.hit_object->userdata && results.hit_object->userdata_type == 1) // Else if we hit a parcel:
		{
			this->selected_parcel = static_cast<Parcel*>(results.hit_object->userdata);

			opengl_engine->selectObject(selected_parcel->opengl_engine_ob);
			opengl_engine->setSelectionOutlineColour(PARCEL_OUTLINE_COLOUR);

			// Show parcel editor, hide object editor.
			ui_interface->setParcelEditorForParcel(*selected_parcel);
			ui_interface->setParcelEditorEnabled(true);
			ui_interface->showParcelEditor();
			ui_interface->showEditorDockWidget(); // Show the object editor dock widget if it is hidden.
		}
		else if(results.hit_object->userdata && results.hit_object->userdata_type == 2) // If we hit an instance:
		{
			InstanceInfo* instance = static_cast<InstanceInfo*>(results.hit_object->userdata);
			selectObject(instance->prototype_object, results.hit_mat_index); // Select the original prototype object that the hit object is an instance of.
		}
		else // Else if the trace didn't hit anything:
		{
			ui_interface->setObjectEditorEnabled(false);
		}

		const Vec4f selection_point_ws = origin + dir*results.hit_t;

		// Store the object-space selection point.  This will be used for moving the object.
		// Note: we set this after the selectObject() call above, which sets selection_point_os to (0,0,0).
		this->selection_point_os = results.hit_object->getWorldToObMatrix() * selection_point_ws;

		// Add gl object to show selection position:
		// opengl_engine->addObject(opengl_engine->makeAABBObject(selection_point_ws - Vec4f(0.05, 0.05, 0.05, 0), selection_point_ws + Vec4f(0.05, 0.05, 0.05, 0), Colour4f(0,0,1,1)));
	}
	else
	{
		// Deselect any currently selected object
		deselectObject();
		deselectParcel();
	}
}


inline static bool clipLineToPlaneBackHalfSpace(const Planef& plane, Vec4f& a, Vec4f& b)
{
	const float ad = plane.signedDistToPoint(a);
	const float bd = plane.signedDistToPoint(b);
	if(ad > 0 && bd > 0) // If both endpoints not in back half space:
		return false;

	if(ad <= 0 && bd <= 0) // If both endpoints in back half space:
		return true;

	// Else line straddles plane
	// ad + (bd - ad) * t = 0
	// t = -ad / (bd - ad)
	// t = ad / -(bd - ad)
	// t = ad / (-bd + ad)
	// t = ad / (ad - bd)

	const float t = ad / (ad - bd);
	const Vec4f on_plane_p = a + (b - a) * t;
	//assert(epsEqual(plane.signedDistToPoint(on_plane_p), 0.f));

	if(ad <= 0) // If point a lies in back half space:
		b = on_plane_p; // update point b
	else
		a = on_plane_p; // else point b lies in back half space, so update point a
	return true;
}


// cursor_pos is in glWidget local coordinates.
// mouse_event is non-null if this is called from a mouse-move event
// If cursor_is_mouse_cursor is false, the cursor is the crosshair.
void GUIClient::updateInfoUIForMousePosition(const Vec2i& cursor_pos, const Vec2f& cursor_gl_coords, MouseEvent* mouse_event, bool cursor_is_mouse_cursor)
{
	// New for object mouseover hyperlink showing, and webview mouse-move events:
	if(this->physics_world.nonNull())
	{
		// Trace ray through scene
		const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
		const Vec4f dir = getDirForPixelTrace(cursor_pos.x, cursor_pos.y);

		RayTraceResult results;
		this->physics_world->traceRay(origin, dir, /*max_t=*/1.0e5f, /*ignore body id=*/JPH::BodyID(), results);

		bool show_mouseover_info_ui = false;
		if(results.hit_object)
		{
			if(results.hit_object->userdata && results.hit_object->userdata_type == 0) // If we hit an object:
			{
				WorldObject* ob = static_cast<WorldObject*>(results.hit_object->userdata);

				const Vec4f hitpos_ws = origin + dir * results.hit_t;
				const Vec4f hitpos_os = results.hit_object->getWorldToObMatrix() * hitpos_ws;

				const Vec2f uvs = (hitpos_os[1] < 0.5f) ? // if y coordinate is closer to y=0 than y=1:
					Vec2f(hitpos_os[0],     hitpos_os[2]) : // y=0 face:
					Vec2f(1 - hitpos_os[0], hitpos_os[2]); // y=1 face:

				if(ob->web_view_data.nonNull() && mouse_event) // If this is a web-view object:
				{
					ob->web_view_data->mouseMoved(mouse_event, uvs);
				}
				else if(ob->browser_vid_player.nonNull() && mouse_event) // If this is a video object:
				{
					ob->browser_vid_player->mouseMoved(mouse_event, uvs);
				}
				else 
				{
					if(!ob->target_url.empty() && (ob->web_view_data.isNull() && ob->browser_vid_player.isNull())) // If the object has a target URL (and is not a web-view and not a video object):
					{
						// If the mouse-overed ob is currently selected, and is editable, don't show the hyperlink, because 'E' is the key to pick up the object.
						const bool selected_editable_ob = (selected_ob.ptr() == ob) && objectModificationAllowed(*ob);

						if(!selected_editable_ob)
						{
							ob_info_ui.showHyperLink(ob->target_url, cursor_gl_coords);
							show_mouseover_info_ui = true;
						}
					}

					if(ob->vehicle_script.nonNull() && vehicle_controller_inside.isNull()) // If this is a vehicle, and we are not already in a vehicle:
					{
						// If the vehicle is rightable (e.g. bike), display righting message if the vehicle is upside down.  Otherwise just display enter message.
						const Vec4f up_z_up(0,0,1,0);
						const Vec4f vehicle_up_os = ob->vehicle_script->getZUpToModelSpaceTransform() * up_z_up;
						const Vec4f vehicle_up_ws = normalise(obToWorldMatrix(*ob) * vehicle_up_os);
						const bool upright = dot(vehicle_up_ws, up_z_up) > 0.5f;

						if(upright || !ob->vehicle_script->isRightable())
							ob_info_ui.showMessage(cursor_is_mouse_cursor ? "Press [E] to enter vehicle" : "Press [A] on gamepad to enter vehicle", cursor_gl_coords);
						else
							ob_info_ui.showMessage(cursor_is_mouse_cursor ? "Press [E] to right vehicle" : "Press [A] on gamepad to right vehicle", cursor_gl_coords);
						show_mouseover_info_ui = true;
					}

					if(ob->event_handlers && ob->event_handlers->onUserUsedObject_handlers.nonEmpty())
					{
						ob_info_ui.showMessage(cursor_is_mouse_cursor ? "Press [E] to use" : "Press [A] on gamepad to use", cursor_gl_coords);
						show_mouseover_info_ui = true;
					}

					if(show_mouseover_info_ui)
					{
						// Remove outline around any previously mouse-overed object (unless it is the main selected ob)
						if(this->mouseover_selected_gl_ob.nonNull())
						{
							if(ob != this->selected_ob.ptr()) 
								opengl_engine->deselectObject(this->mouseover_selected_gl_ob);
							this->mouseover_selected_gl_ob = NULL;
						}

						// Add outline around object
						if(ob->opengl_engine_ob.nonNull())
						{
							this->mouseover_selected_gl_ob = ob->opengl_engine_ob;
							opengl_engine->selectObject(ob->opengl_engine_ob);
						}
					}
				}
			}
		}

		if(!show_mouseover_info_ui)
		{
			// Remove outline around any previously mouse-overed object (unless it is the main selected ob)
			if(this->mouseover_selected_gl_ob.nonNull())
			{
				const bool mouseover_is_selected_ob = this->selected_ob.nonNull() && this->selected_ob->opengl_engine_ob.nonNull() && (this->selected_ob->opengl_engine_ob == this->mouseover_selected_gl_ob);
				if(!mouseover_is_selected_ob)
					opengl_engine->deselectObject(this->mouseover_selected_gl_ob);
				this->mouseover_selected_gl_ob = NULL;
			}
			ob_info_ui.hideMessage();
		}
	}
}


void GUIClient::mouseMoved(MouseEvent& mouse_event)
{
	last_cursor_movement_was_from_mouse = true;
	hud_ui.setCrosshairDotVisible(false);

	if(gl_ui.nonNull())
	{
		const bool accepted = gl_ui->handleMouseMoved(mouse_event);
		if(accepted)
		{
			mouse_event.accepted = true;
			return;
		}

		chat_ui.handleMouseMoved(mouse_event);
		if(minimap)
			minimap->handleMouseMoved(mouse_event);
	}

	if(!ui_interface->isCursorHidden())
		updateInfoUIForMousePosition(mouse_event.cursor_pos, mouse_event.gl_coords, &mouse_event, /*cursor_is_mouse_cursor=*/true);


	if(selected_ob.nonNull() && grabbed_axis >= 0 && grabbed_axis < NUM_AXIS_ARROWS)
	{
		// If we have have grabbed an axis and are moving it:
		//conPrint("Grabbed axis " + toString(grabbed_axis));

		const Vec4f origin = cam_controller.getPosition().toVec4fPoint();
		//const Vec4f dir = getDirForPixelTrace(e->pos().x(), e->pos().y());

		Vec2f start_pixelpos, end_pixelpos; // pixel coords of line segment start and end.

		// Get line segment in world space along the grabbed axis, extended out in each direction for some distance.
		const float MAX_MOVE_DIST = 100;
		const Vec4f line_dir = normalise(axis_arrow_segments[grabbed_axis].b - axis_arrow_segments[grabbed_axis].a);
		Vec4f use_line_start = axis_arrow_segments[grabbed_axis].a - line_dir * MAX_MOVE_DIST;
		Vec4f use_line_end   = axis_arrow_segments[grabbed_axis].a + line_dir * MAX_MOVE_DIST;

		// Clip line in 3d world space to the half-space in front of camera.
		// We do this so we can get a valid projection of the line into 2d pixel space.
		const Vec4f camforw_ws = cam_controller.getForwardsVec().toVec4fVector();
		Planef plane(origin + camforw_ws * 0.1f, -camforw_ws);
		const bool visible = clipLineToPlaneBackHalfSpace(plane, use_line_start, use_line_end);
		assertOrDeclareUsed(visible);

		// Project 3d world space line segment into 2d pixel space.
		bool start_visible = getPixelForPoint(use_line_start, start_pixelpos);
		bool end_visible   = getPixelForPoint(use_line_end,   end_pixelpos);

		assert(start_visible && end_visible);
		if(start_visible && end_visible)
		{
			const Vec2f mousepos((float)mouse_event.cursor_pos.x, (float)mouse_event.cursor_pos.y);

			const Vec2f closest_pixel = closestPointOnLineSegment(mousepos, start_pixelpos, end_pixelpos); // Closest pixel coords of point on 2d line to mouse pointer.

			// Project point on 2d line into 3d space along the line
			Vec4f new_p = pointOnLineWorldSpace(axis_arrow_segments[grabbed_axis].a, axis_arrow_segments[grabbed_axis].b, closest_pixel);

			// opengl_engine->addObject(opengl_engine->makeAABBObject(new_p, new_p + Vec4f(0.1f,0.1f,0.1f,0), Colour4f(0.9, 0.2, 0.5, 1.f)));

			Vec4f delta_p = new_p - grabbed_point_ws; // Desired change in position from when we grabbed the object

			assert(new_p.isFinite());

			Vec4f tentative_new_ob_p = ob_origin_at_grab + delta_p;

			if(tentative_new_ob_p.getDist(ob_origin_at_grab) > MAX_MOVE_DIST)
				tentative_new_ob_p = ob_origin_at_grab + (tentative_new_ob_p - ob_origin_at_grab) * MAX_MOVE_DIST / (tentative_new_ob_p - ob_origin_at_grab).length();

			assert(tentative_new_ob_p.isFinite());

			// Snap to grid
			if(ui_interface->snapToGridCheckBoxChecked())
			{
				const double grid_spacing = ui_interface->gridSpacing();
				if(grid_spacing > 1.0e-5)
					tentative_new_ob_p[grabbed_axis] = (float)Maths::roundToMultipleFloating((double)tentative_new_ob_p[grabbed_axis], grid_spacing);
			}

			//Matrix4f tentative_new_to_world = this->selected_ob->opengl_engine_ob->ob_to_world_matrix;
			//tentative_new_to_world.setColumn(3, tentative_new_ob_p);
			//tryToMoveObject(tentative_new_to_world);
			tryToMoveObject(this->selected_ob, tentative_new_ob_p);

			if(this->selected_ob_picked_up)
			{
				// Update selection_vec_cs if we have picked up this object.
				const Vec4f selection_point_ws = obToWorldMatrix(*this->selected_ob) * this->selection_point_os;

				const Vec4f selection_vec_ws = selection_point_ws - origin;
				this->selection_vec_cs = cam_controller.vectorToCamSpace(selection_vec_ws);
			}
		}
	}
	else if(selected_ob.nonNull() && grabbed_axis >= NUM_AXIS_ARROWS && grabbed_axis < (NUM_AXIS_ARROWS + 3)) // If we have grabbed a rotation arc and are moving it:
	{
		const Vec4f arc_centre = ob_origin_at_grab;// this->selected_ob->opengl_engine_ob->ob_to_world_matrix.getColumn(3);

		const int rot_axis = grabbed_axis - NUM_AXIS_ARROWS;
		const Vec4f basis_a = basis_vectors[rot_axis*2];
		const Vec4f basis_b = basis_vectors[rot_axis*2 + 1];

		// Intersect ray from current mouse position with plane formed by rotation basis vectors
		const Vec4f origin = cam_controller.getPosition().toVec4fPoint();
		const Vec4f dir = getDirForPixelTrace(mouse_event.cursor_pos.x, mouse_event.cursor_pos.y);

		Planef plane(arc_centre, crossProduct(basis_a, basis_b));

		const float t = plane.rayIntersect(origin, dir);
		const Vec4f plane_p = origin + dir * t;

		//opengl_engine->addObject(opengl_engine->makeAABBObject(plane_p, plane_p + Vec4f(0.05f, 0.05f, 0.05f, 0), Colour4f(1, 0, 1, 1)));

		const float angle = safeATan2(dot(plane_p - arc_centre, basis_b), dot(plane_p - arc_centre, basis_a));

		const float delta = angle - grabbed_angle;

		//Matrix4f tentative_new_to_world = this->selected_ob->opengl_engine_ob->ob_to_world_matrix;
		//tentative_new_to_world = Matrix4f::rotationMatrix(crossProduct(basis_a, basis_b), delta) * tentative_new_to_world;
		//tryToMoveObject(tentative_new_to_world);

		rotateObject(this->selected_ob, crossProduct(basis_a, basis_b), delta);

		grabbed_angle = angle;
	}
	else
	{
		// Set mouseover colour if we have moused over a grabbable axis.
		if(axis_and_rot_obs_enabled)
		{
			// Don't try and grab an axis etc.. when we are clicking on a voxel group to add/remove voxels.
			//bool mouse_trace_hit_selected_ob = false;
			//if(areEditingVoxels())
			//{
			//	RayTraceResult results;
			//	this->physics_world->traceRay(cam_controller.getPosition().toVec4fPoint(), getDirForPixelTrace(e->pos().x(), e->pos().y()), /*max_t=*/1.0e10f, results);
			//
			//	mouse_trace_hit_selected_ob = results.hit_object && results.hit_object->userdata && results.hit_object->userdata_type == 0 && // If we hit an object,
			//		static_cast<WorldObject*>(results.hit_object->userdata) == this->selected_ob.ptr(); // and it was the selected ob
			//}

			// Set grab controls to default colours
			for(int i=0; i<NUM_AXIS_ARROWS; ++i)
			{
				axis_arrow_objects[i]->materials[0].albedo_linear_rgb = toLinearSRGB(axis_arrows_default_cols[i % 3]);
				opengl_engine->objectMaterialsUpdated(*axis_arrow_objects[i]);
			}

			for(int i=0; i<3; ++i)
			{
				rot_handle_arc_objects[i]->materials[0].albedo_linear_rgb = toLinearSRGB(axis_arrows_default_cols[i]);
				opengl_engine->objectMaterialsUpdated(*rot_handle_arc_objects[i]);
			}

			//if(!mouse_trace_hit_selected_ob)
			{
				Vec4f dummy_grabbed_point_ws;
				const int axis = mouseOverAxisArrowOrRotArc(Vec2f((float)mouse_event.cursor_pos.x, (float)mouse_event.cursor_pos.y), dummy_grabbed_point_ws);
		
				if(axis >= 0 && axis < NUM_AXIS_ARROWS)
				{
					axis_arrow_objects[axis]->materials[0].albedo_linear_rgb = toLinearSRGB(axis_arrows_mouseover_cols[axis % 3]);
					opengl_engine->objectMaterialsUpdated(*axis_arrow_objects[axis]);
				}

				if(axis >= NUM_AXIS_ARROWS && axis < NUM_AXIS_ARROWS + 3)
				{
					const int grabbed_rot_axis = axis - NUM_AXIS_ARROWS;
					rot_handle_arc_objects[grabbed_rot_axis]->materials[0].albedo_linear_rgb = toLinearSRGB(axis_arrows_mouseover_cols[grabbed_rot_axis]);
					opengl_engine->objectMaterialsUpdated(*rot_handle_arc_objects[grabbed_rot_axis]);
				}
			}
		}
	}
}


// The user wants to rotate the object 'ob'.
void GUIClient::rotateObject(WorldObjectRef ob, const Vec4f& axis, float angle)
{
	const bool allow_modification = objectModificationAllowedWithMsg(*ob, "rotate");
	if(allow_modification)
	{
		const Quatf current_q = Quatf::fromAxisAndAngle(normalise(ob->axis), ob->angle);
		const Quatf new_q     = Quatf::fromAxisAndAngle(toVec3f(normalise(axis)), angle) * current_q;

		Vec4f new_axis;
		new_q.toAxisAndAngle(new_axis, ob->angle);
		ob->axis = toVec3f(new_axis);

		const Matrix4f new_ob_to_world = obToWorldMatrix(*ob);

		// Update in opengl engine.
		GLObjectRef opengl_ob = ob->opengl_engine_ob;
		if(!opengl_ob)
			return;

		opengl_ob->ob_to_world_matrix = new_ob_to_world;
		opengl_engine->updateObjectTransformData(*opengl_ob);

		// Update physics object
		physics_world->setNewObToWorldTransform(*ob->physics_object, ob->pos.toVec4fVector(), new_q, useScaleForWorldOb(ob->scale).toVec4fVector());

		// Update in Indigo view
		//ui->indigoView->objectTransformChanged(*ob);

		// Set a timer to call updateObjectEditorObTransformSlot() later.  Not calling this every frame avoids stutters with webviews playing back videos interacting with Qt updating spinboxes.
		ui_interface->startObEditorTimerIfNotActive();

		ob->transformChanged();

		ob->last_modified_time = TimeStamp::currentTime(); // Gets set on server as well, this is just for updating the local display.

		// Mark as from-local-dirty to send an object updated message to the server.
		{
			Lock lock(world_state->mutex);
			ob->from_local_transform_dirty = true;
			this->world_state->dirty_from_local_objects.insert(ob);
		}

		if(this->selected_ob->object_type == WorldObject::ObjectType_Spotlight)
		{
			GLLightRef light = this->selected_ob->opengl_light;
			if(light.nonNull())
			{
				light->gpu_data.dir = normalise(new_ob_to_world * Vec4f(0, 0, -1, 0));
				opengl_engine->lightUpdated(light);
			}
		}

		if(this->terrain_system.nonNull() && ::hasPrefix(selected_ob->content, "biome:"))
			this->terrain_system->invalidateVegetationMap(selected_ob->getAABBWS());

		// Trigger sending update-lightmap update flag message later.
		//ob->flags |= WorldObject::LIGHTMAP_NEEDS_COMPUTING_FLAG;
		//objs_with_lightmap_rebuild_needed.insert(ob);
		//lightmap_flag_timer->start(/*msec=*/2000); 
	}
}


void GUIClient::deleteSelectedObject()
{
	if(this->selected_ob.nonNull())
	{
		if(objectModificationAllowedWithMsg(*this->selected_ob, "delete"))
		{
			undo_buffer.startWorldObjectEdit(*selected_ob);
			undo_buffer.finishWorldObjectEdit(*selected_ob);

			// Send DestroyObject packet
			MessageUtils::initPacket(scratch_packet, Protocol::DestroyObject);
			writeToStream(selected_ob->uid, scratch_packet);

			enqueueMessageToSend(*this->client_thread, scratch_packet);

			deselectObject();

			showInfoNotification("Object deleted.");
		}
	}
}


ObjectPathController* GUIClient::getPathControllerForOb(const WorldObject& ob)
{
	for(size_t i=0; i<path_controllers.size(); ++i)
		if(path_controllers[i]->controlled_ob.ptr() == &ob)
			return path_controllers[i].ptr();
	return NULL;
}


void GUIClient::createPathControlledPathVisObjects(const WorldObject& ob)
{
	// Remove any existing ones
	for(size_t i=0; i<selected_ob_vis_gl_obs.size(); ++i)
		opengl_engine->removeObject(this->selected_ob_vis_gl_obs[i]);
	selected_ob_vis_gl_obs.clear();

	{
		ObjectPathController* path_controller = getPathControllerForOb(ob);
		if(path_controller)
		{
			OpenGLMaterial material;
			material.albedo_linear_rgb = toLinearSRGB(Colour3f(0.8f, 0.3f, 0.3f));
			material.transparent = true;
			material.alpha = 0.9f;

			Reference<OpenGLMeshRenderData> cylinder_meshdata = MeshPrimitiveBuilding::makeCylinderMesh(*opengl_engine->vert_buf_allocator, /*end caps=*/false);

			js::AABBox waypoints_aabb = js::AABBox::emptyAABBox();
			for(size_t i=0; i<path_controller->waypoints.size(); ++i)
				waypoints_aabb.enlargeToHoldPoint(path_controller->waypoints[i].pos);

			const float half_w = myClamp(waypoints_aabb.longestLength() * 0.01f, 0.2f, 2.f);
			const float cylinder_r = myClamp(waypoints_aabb.longestLength() * 0.002f, 0.03f, 0.3f);

			// Draw path by making opengl objects
			for(size_t i=0; i<path_controller->waypoints.size(); ++i)
			{
				const Vec4f begin_pos = path_controller->waypoints[i].pos;

				// Add cube at vertex
				const Colour4f col = 
					(path_controller->waypoints[i].waypoint_type == PathWaypointIn::CurveIn) ? Colour4f(0.1f, 0.8f, 0.1f, 0.9f) : // green
					((path_controller->waypoints[i].waypoint_type == PathWaypointIn::CurveOut) ? Colour4f(0.1f, 0.1f, 0.8f, 0.9f) :  // blue
						 Colour4f(0.8f, 0.1f, 0.1f, 0.9f)); // red

				GLObjectRef vert_gl_ob = opengl_engine->makeAABBObject(begin_pos - Vec4f(half_w, half_w, half_w, 0), begin_pos + Vec4f(half_w, half_w, half_w, 0), col);
				opengl_engine->addObject(vert_gl_ob);
				opengl_engine->selectObject(vert_gl_ob);
				selected_ob_vis_gl_obs.push_back(vert_gl_ob); // Keep track of these objects we added.


				// Add text above cube saying the index
				{
					const int font_size_px = 42;
					const std::string use_text = "Waypoint " + toString(i);

					std::vector<GLUIText::CharPositionInfo> char_positions_font_coords;
					Rect2f rect_os;
					OpenGLTextureRef atlas_texture;
					Reference<OpenGLMeshRenderData> meshdata = GLUIText::makeMeshDataForText(opengl_engine, gl_ui->font_char_text_cache.ptr(), gl_ui->getFonts(), gl_ui->getEmojiFonts(), use_text, 
						/*font size px=*/font_size_px, /*vert_pos_scale=*/(1.f / font_size_px), /*render SDF=*/true, this->stack_allocator, rect_os, atlas_texture, char_positions_font_coords);

					GLObjectRef opengl_ob = opengl_engine->allocateObject();
					opengl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(begin_pos) * Matrix4f::uniformScaleMatrix(half_w);
					opengl_ob->mesh_data = meshdata;
					opengl_ob->materials.resize(1);
					opengl_ob->materials[0].alpha_blend = true; // Make use alpha blending
					opengl_ob->materials[0].sdf_text = true;
					opengl_ob->materials[0].transmission_texture = atlas_texture;
					opengl_ob->materials[0].albedo_linear_rgb = Colour3f(0.0f, 0.2f, 0.0f);
					opengl_ob->materials[0].fresnel_scale = 0;

					opengl_engine->addObject(opengl_ob);
					opengl_engine->selectObject(opengl_ob);
					selected_ob_vis_gl_obs.push_back(opengl_ob); // Keep track of these objects we added.
				}

				if(path_controller->waypoints[i].waypoint_type == PathWaypointIn::CurveIn)
				{
					// Divide curve into multiple segments
					const int N = 16;
					for(size_t n=0; n<N; ++n)
					{
						const Vec4f curve_pos_n   = path_controller->evalSegmentCurvePos((int)i, /*frac=*/(float)n       / N);
						const Vec4f curve_pos_n_1 = path_controller->evalSegmentCurvePos((int)i, /*frac=*/(float)(n + 1) / N);

						if(curve_pos_n_1.getDist(curve_pos_n) > 1.0e-6f)
						{
							GLObjectRef edge_gl_ob = opengl_engine->allocateObject();

							const Matrix4f dir_matrix = Matrix4f::constructFromVectorStatic(normalise(curve_pos_n_1 - curve_pos_n));
							const Matrix4f scale_matrix = Matrix4f::scaleMatrix(/*radius=*/cylinder_r,/*radius=*/cylinder_r, curve_pos_n.getDist(curve_pos_n_1));
							const Matrix4f ob_to_world = Matrix4f::translationMatrix(curve_pos_n) * dir_matrix * scale_matrix;

							edge_gl_ob->ob_to_world_matrix = ob_to_world;
							edge_gl_ob->mesh_data = cylinder_meshdata;
							edge_gl_ob->setSingleMaterial(material);

							opengl_engine->addObject(edge_gl_ob);
							opengl_engine->selectObject(edge_gl_ob);

							// Keep track of these objects we added.
							selected_ob_vis_gl_obs.push_back(edge_gl_ob);
						}
					}
				}
				else
				{
					
					const Vec4f end_pos = path_controller->waypoints[Maths::intMod((int)i + 1, (int)path_controller->waypoints.size())].pos;

					GLObjectRef edge_gl_ob = opengl_engine->allocateObject();

					const Matrix4f dir_matrix = Matrix4f::constructFromVectorStatic(normalise(end_pos - begin_pos));
					const Matrix4f scale_matrix = Matrix4f::scaleMatrix(/*radius=*/cylinder_r,/*radius=*/cylinder_r, begin_pos.getDist(end_pos));
					const Matrix4f ob_to_world = Matrix4f::translationMatrix(begin_pos) * dir_matrix * scale_matrix;

					edge_gl_ob->ob_to_world_matrix = ob_to_world;
					edge_gl_ob->mesh_data = cylinder_meshdata;
					edge_gl_ob->setSingleMaterial(material);

					opengl_engine->addObject(edge_gl_ob);
					opengl_engine->selectObject(edge_gl_ob);

					// Keep track of these objects we added.
					selected_ob_vis_gl_obs.push_back(edge_gl_ob);
				}
			}
		}
	}
}


static bool isObjectDecal(const WorldObjectRef& ob)
{
	for(size_t i=0; i<ob->materials.size(); ++i)
		if(ob->materials[i]->isDecal())
			return true;
	return false;
}


void GUIClient::selectObject(const WorldObjectRef& ob, int selected_mat_index)
{
	assert(ob.nonNull());

	// Deselect any existing object
	deselectObject();


	this->selected_ob = ob;
	assert(this->selected_ob->getRefCount() >= 0);

	this->selected_ob->is_selected = true;

	this->selection_point_os = Vec4f(0, 0, 0, 1); // Store a default value for this (kind of a pivot point).


	// If diagnostics widget is shown, show an AABB visualisation as well.
	if(ui_interface->diagnosticsVisible() && ui_interface->showObAABBsEnabled())
	{
		// Add object-space AABB visualisation gl ob.
		this->aabb_os_vis_gl_ob = opengl_engine->makeAABBObject(this->selected_ob->getAABBOS().min_, this->selected_ob->getAABBOS().max_, Colour4f(0.3f, 0.7f, 0.3f, 0.5f));
		aabb_os_vis_gl_ob->ob_to_world_matrix = selected_ob->obToWorldMatrix() * OpenGLEngine::AABBObjectTransform(selected_ob->getAABBOS().min_, selected_ob->getAABBOS().max_);
		opengl_engine->addObject(this->aabb_os_vis_gl_ob);

		// Add world-space AABB visualisation gl ob.
		this->aabb_ws_vis_gl_ob = opengl_engine->makeAABBObject(this->selected_ob->getAABBWS().min_, this->selected_ob->getAABBWS().max_, Colour4f(0.7f, 0.3f, 0.3f, 0.5f));
		opengl_engine->addObject(this->aabb_ws_vis_gl_ob);
	}

	createPathControlledPathVisObjects(*this->selected_ob);

	// Mark the materials on the hit object as selected
	if(this->selected_ob->opengl_engine_ob.nonNull())
	{
		opengl_engine->selectObject(selected_ob->opengl_engine_ob);
		opengl_engine->setSelectionOutlineColour(DEFAULT_OUTLINE_COLOUR);
	}

	// Turn on voxel grid drawing if this is a voxel object
	if((this->selected_ob->object_type == WorldObject::ObjectType_VoxelGroup) && this->selected_ob->opengl_engine_ob.nonNull())
	{
		for(size_t z=0; z<this->selected_ob->opengl_engine_ob->materials.size(); ++z)
			this->selected_ob->opengl_engine_ob->materials[z].draw_planar_uv_grid = true;

		opengl_engine->objectMaterialsUpdated(*this->selected_ob->opengl_engine_ob);
	}

	const bool have_edit_permissions = objectModificationAllowed(*this->selected_ob);

	// Add an object placement beam
	if(have_edit_permissions)
	{
		opengl_engine->addObject(ob_placement_beam);
		opengl_engine->addObject(ob_placement_marker);

		if(ui_interface->posAndRot3DControlsEnabled())
		{
			for(int i=0; i<NUM_AXIS_ARROWS; ++i)
				opengl_engine->addObject(axis_arrow_objects[i]);

			for(int i=0; i<3; ++i)
				opengl_engine->addObject(rot_handle_arc_objects[i]);

			axis_and_rot_obs_enabled = true;
		}
	}

	if(isObjectDecal(ob))
	{
		const float edge_w = DECAL_EDGE_AABB_WIDTH;
		ob->edit_aabb = opengl_engine->makeCuboidEdgeAABBObject(
			ob->getAABBOS().min_, 
			ob->getAABBOS().max_ + Vec4f(2*edge_w, 2*edge_w, 2*edge_w, 0),  // Extend cube slightly so decal isn't applied to edges.
			Colour4f(0.6f, 0.8f, 0.7f, 1.f), 
			/*edge_width_scale=*/edge_w
		);

		ob->edit_aabb->ob_to_world_matrix = obToWorldMatrix(*ob) * Matrix4f::translationMatrix(-edge_w, -edge_w, -edge_w);
		opengl_engine->addObject(ob->edit_aabb);
	}


	// Show object editor, hide parcel editor.
	ui_interface->setObjectEditorFromOb(*selected_ob, selected_mat_index, /*ob in editing user's world=*/connectedToUsersWorldOrGodUser()); // Update the editor widget with values from the selected object
	ui_interface->setObjectEditorEnabled(true);
	ui_interface->showObjectEditor();

	ui_interface->setUIForSelectedObject();

	ui_interface->setObjectEditorControlsEditable(have_edit_permissions);
	ui_interface->showEditorDockWidget(); // Show the object editor dock widget if it is hidden.

	// Update help text
	if(have_edit_permissions)
	{
		std::string text;
		if(selected_ob->object_type == WorldObject::ObjectType_VoxelGroup)
			text += "Ctrl + left-click: Add voxel.\n"
			"Alt + left-click: Delete voxel.\n"
			"\n";
	
		text += "'P' key: Pick up/drop object.\n"
			"Click and drag the mouse to move the object around when picked up.\n"
			"'[' and  ']' keys rotate the object.\n"
			"PgUp and  pgDown keys rotate the object.\n"
			"'-' and '+' keys move object near/far.\n"
			"Esc key: deselect object.";
	
		ui_interface->setHelpInfoLabel(text);
	}
}
		

void GUIClient::deselectObject()
{
	if(this->selected_ob.nonNull())
	{
		this->selected_ob->is_selected = false;
		dropSelectedObject();

		// Remove placement beam from 3d engine
		opengl_engine->removeObject(this->ob_placement_beam);
		opengl_engine->removeObject(this->ob_placement_marker);

		for(int i=0; i<NUM_AXIS_ARROWS; ++i)
			opengl_engine->removeObject(this->axis_arrow_objects[i]);

		for(int i=0; i<3; ++i)
			opengl_engine->removeObject(this->rot_handle_arc_objects[i]);

		axis_and_rot_obs_enabled = false;

		// Remove any edge markers
		while(ob_denied_move_markers.size() > 0)
		{
			opengl_engine->removeObject(ob_denied_move_markers.back());
			ob_denied_move_markers.pop_back();
		}

		// Remove edit box
		if(selected_ob->edit_aabb)
		{
			opengl_engine->removeObject(selected_ob->edit_aabb);
			selected_ob->edit_aabb = NULL;
		}

		// Remove visualisation objects
		if(this->aabb_os_vis_gl_ob.nonNull())
		{
			opengl_engine->removeObject(this->aabb_os_vis_gl_ob);
			this->aabb_os_vis_gl_ob = NULL;
		}
		if(this->aabb_ws_vis_gl_ob.nonNull())
		{
			opengl_engine->removeObject(this->aabb_ws_vis_gl_ob);
			this->aabb_ws_vis_gl_ob = NULL;
		}

		for(size_t i=0; i<selected_ob_vis_gl_obs.size(); ++i)
			opengl_engine->removeObject(this->selected_ob_vis_gl_obs[i]);
		selected_ob_vis_gl_obs.clear();



		// Deselect any currently selected object
		opengl_engine->deselectObject(this->selected_ob->opengl_engine_ob);

		// Turn off voxel grid drawing if this is a voxel object
		if((this->selected_ob->object_type == WorldObject::ObjectType_VoxelGroup) && this->selected_ob->opengl_engine_ob.nonNull())
		{
			for(size_t z=0; z<this->selected_ob->opengl_engine_ob->materials.size(); ++z)
				this->selected_ob->opengl_engine_ob->materials[z].draw_planar_uv_grid = false;

			opengl_engine->objectMaterialsUpdated(*this->selected_ob->opengl_engine_ob);
		}

		ui_interface->setObjectEditorEnabled(false);

		this->selected_ob = NULL;

		grabbed_axis = -1;

		this->shown_object_modification_error_msg = false;

		ui_interface->setHelpInfoLabelToDefaultText();

		ui_interface->setUIForSelectedObject();
	}
}


void GUIClient::deselectParcel()
{
	if(this->selected_parcel.nonNull())
	{
		// Deselect any currently selected object
		opengl_engine->deselectObject(this->selected_parcel->opengl_engine_ob);

		ui_interface->setParcelEditorEnabled(false);

		this->selected_parcel = NULL;

		//this->shown_object_modification_error_msg = false;

		ui_interface->setHelpInfoLabelToDefaultText();
	}
}


void GUIClient::onMouseWheelEvent(MouseWheelEvent& e)
{
	if(gl_ui)
	{
		gl_ui->handleMouseWheelEvent(e);
		if(e.accepted)
			return;
	}

	// Trace through scene to see if the mouse is over a web-view
	if(this->physics_world.nonNull())
	{
		const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
		const Vec4f dir = getDirForPixelTrace(e.cursor_pos.x, e.cursor_pos.y);
	
		RayTraceResult results;
		this->physics_world->traceRay(origin, dir, /*max_t=*/1.0e5f, /*ignore body id=*/JPH::BodyID(), results);
	
		if(results.hit_object && results.hit_object->userdata && results.hit_object->userdata_type == 0)
		{
			WorldObject* ob = static_cast<WorldObject*>(results.hit_object->userdata);
	
			const Vec4f hitpos_ws = origin + dir * results.hit_t;
			const Vec4f hitpos_os = results.hit_object->getWorldToObMatrix() * hitpos_ws;

			const Vec2f uvs = (hitpos_os[1] < 0.5f) ? // if y coordinate is closer to y=0 than y=1:
				Vec2f(hitpos_os[0],     hitpos_os[2]) : // y=0 face:
				Vec2f(1 - hitpos_os[0], hitpos_os[2]); // y=1 face:

			if(ob->web_view_data.nonNull()) // If this is a web-view object:
			{
				ob->web_view_data->wheelEvent(&e, uvs);
				e.accepted = true;
				return;
			}
			else if(ob->browser_vid_player.nonNull()) // If this is a video object:
			{
				ob->browser_vid_player->wheelEvent(&e, uvs);
				e.accepted = true;
				return;
			}
		}
	}

	if(this->selected_ob.nonNull() && selected_ob_picked_up)
	{
		this->selection_vec_cs[1] *= (1.0f + e.angle_delta.y * 0.004f);
	}
	else
	{
		if(!cam_controller.thirdPersonEnabled() && (e.angle_delta.y < 0)) // If we were in first person view, and scrolled down, change to third-person view.
		{
			ui_interface->enableThirdPersonCamera();
		}
		else if(cam_controller.thirdPersonEnabled())
		{
			const bool scrolled_all_way_in = cam_controller.handleScrollWheelEvent((float)e.angle_delta.y);
			const bool change_to_first_person = scrolled_all_way_in && !photo_mode_ui.isPhotoModeEnabled();
			if(change_to_first_person)
			{
				ui_interface->enableFirstPersonCamera();
			}
		}
	}
}


void GUIClient::gamepadButtonXChanged(bool pressed)
{
	//if(pressed)
	//	useActionTriggered(/*use_mouse_cursor=*/false);
}


void GUIClient::gamepadButtonAChanged(bool pressed)
{
	if(pressed)
		useActionTriggered(/*use_mouse_cursor=*/false);
}


void GUIClient::viewportResized(int w, int h)
{
	if(gl_ui.nonNull())
		gl_ui->viewportResized(w, h);
	gesture_ui.viewportResized(w, h);
	ob_info_ui.viewportResized(w, h);
	misc_info_ui.viewportResized(w, h);
	hud_ui.viewportResized(w, h);
	chat_ui.viewportResized(w, h);
	photo_mode_ui.viewportResized(w, h);
	if(minimap)
		minimap->viewportResized(w, h);
}


GLObjectRef GUIClient::makeNameTagGLObject(const std::string& nametag)
{
	ZoneScopedN("makeNameTagGLObject"); // Tracy profiler

	TextRendererFontFace* font = gl_ui->getFont(/*font_size_px=*/36, /*emoji=*/false);

	const TextRendererFontFace::SizeInfo size_info = font->getTextSize(nametag);

	const int use_font_height = size_info.max_bounds.y; //text_renderer_font->getFontSizePixels();
	const int padding_x = (int)(use_font_height * 1.0f);
	const int padding_y = (int)(use_font_height * 0.6f);

	ImageMapUInt8Ref map = new ImageMapUInt8(size_info.glyphSize().x + padding_x * 2, use_font_height + padding_y * 2, 3);
	map->set(240);

	font->drawText(*map, nametag, padding_x, padding_y + use_font_height, Colour3f(0.05f), /*render SDF=*/false);


	GLObjectRef gl_ob = opengl_engine->allocateObject();
	const float mesh_h = (float)map->getHeight() / (float)map->getWidth();
	gl_ob->mesh_data = MeshPrimitiveBuilding::makeRoundedCornerRect(*opengl_engine->vert_buf_allocator, Vec4f(1,0,0,0), Vec4f(0,0,1,0), /*w=*/1.f, /*h=*/mesh_h, /*corner radius=*/mesh_h / 4.f, /*tris_per_corner=*/8);
	gl_ob->materials.resize(1);
	gl_ob->materials[0].fresnel_scale = 0.1f;
	gl_ob->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(0.8f));
	TextureParams tex_params;
	tex_params.allow_compression = false;
	gl_ob->materials[0].albedo_texture = opengl_engine->getOrLoadOpenGLTextureForMap2D("nametag_" + OpenGLTextureKey(nametag), *map, tex_params);
	gl_ob->materials[0].cast_shadows = false;
	gl_ob->materials[0].tex_matrix = Matrix2f(1,0,0,-1); // Compensate for OpenGL loading textures upside down (row 0 in OpenGL is considered to be at the bottom of texture)
	gl_ob->materials[0].tex_translation = Vec2f(0, 1);
	return gl_ob;
}


GLObjectRef GUIClient::makeSpeakerGLObject()
{
	GLObjectRef gl_ob = opengl_engine->allocateObject();
	gl_ob->mesh_data = MeshPrimitiveBuilding::makeRoundedCornerRect(*opengl_engine->vert_buf_allocator, Vec4f(1,0,0,0), Vec4f(0,0,1,0), /*w=*/1.f, /*h=*/1.f, /*corner radius=*/1.f / 4.f, /*tris_per_corner=*/8);
	gl_ob->materials.resize(1);
	gl_ob->materials[0].fresnel_scale = 0.1f;
	gl_ob->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(0.8f));
	gl_ob->materials[0].albedo_texture = opengl_engine->getTexture(resources_dir_path + "/buttons/vol_icon.png");
	gl_ob->materials[0].cast_shadows = false;
	return gl_ob;
}


void GUIClient::updateGroundPlane()
{
	if(this->world_state.isNull())
		return;

	if(opengl_engine.isNull() || !opengl_engine->initSucceeded())
		return;

	if(terrain_system.isNull())
	{
		try
		{
			biome_manager->initTexturesAndModels(resources_dir_path, *opengl_engine, *resource_manager); // TEMP NEW
		}
		catch(glare::Exception& e)
		{
			/*conPrint(e.what());
			QMessageBox msgBox;
			msgBox.setText(QtUtils::toQString(e.what()));
			msgBox.exec();*/
			ui_interface->showHTMLMessageBox("Error", e.what());
			return;
		}


		// Convert URL-based terrain spec to path-based spec
		const TerrainSpec& spec = this->connected_world_settings.terrain_spec;
		TerrainPathSpec path_spec;
		path_spec.section_specs.resize(spec.section_specs.size());
		for(size_t i=0; i<spec.section_specs.size(); ++i)
		{
			path_spec.section_specs[i].x = spec.section_specs[i].x;
			path_spec.section_specs[i].y = spec.section_specs[i].y;
			if(!spec.section_specs[i].heightmap_URL.empty())
				path_spec.section_specs[i].heightmap_path = resource_manager->pathForURL(spec.section_specs[i].heightmap_URL);
			if(!spec.section_specs[i].mask_map_URL.empty())
				path_spec.section_specs[i].mask_map_path  = resource_manager->pathForURL(spec.section_specs[i].mask_map_URL);
			if(!spec.section_specs[i].tree_mask_map_URL.empty())
				path_spec.section_specs[i].tree_mask_map_path  = resource_manager->pathForURL(spec.section_specs[i].tree_mask_map_URL);
		}

		// Convert to .basis extensions if the server supports basis generation of terrain detail maps.
		URLString use_detail_col_map_URLs[4];
		URLString use_detail_height_map_URLs[4];
		for(int i=0; i<4; ++i)
		{
			if(!spec.detail_col_map_URLs[i].empty())
				use_detail_col_map_URLs[i] = this->server_has_basisu_terrain_detail_maps ? (toURLString(eatExtension(toStdString(spec.detail_col_map_URLs[i])) + "basis")) : spec.detail_col_map_URLs[i];
			if(!spec.detail_height_map_URLs[i].empty())
				use_detail_col_map_URLs[i] = this->server_has_basisu_terrain_detail_maps ? (toURLString(eatExtension(toStdString(spec.detail_height_map_URLs[i])) + "basis")) : spec.detail_height_map_URLs[i];
		}

		for(int i=0; i<4; ++i)
		{
			if(!use_detail_col_map_URLs[i].empty())
				path_spec.detail_col_map_paths[i]    = resource_manager->pathForURL(use_detail_col_map_URLs[i]);
			if(!use_detail_height_map_URLs[i].empty())
				path_spec.detail_height_map_paths[i] = resource_manager->pathForURL(use_detail_height_map_URLs[i]);
		}

		const float terrain_section_width_m = myClamp(spec.terrain_section_width_m, 8.f, 1000000.f);

		path_spec.terrain_section_width_m = terrain_section_width_m;
		path_spec.default_terrain_z = spec.default_terrain_z;
		path_spec.water_z = spec.water_z;
		path_spec.flags = spec.flags;


		const float aabb_ws_longest_len = terrain_section_width_m;

		//----------------------------- Start downloading textures, if not already present on disk in resource manager -----------------------------
		TextureParams heightmap_tex_params;
		heightmap_tex_params.use_sRGB = false;
		heightmap_tex_params.allow_compression = false;
		heightmap_tex_params.filtering = OpenGLTexture::Filtering_Bilinear;
		heightmap_tex_params.use_mipmaps = false;

		TextureParams maskmap_tex_params;
		maskmap_tex_params.use_sRGB = false;
		maskmap_tex_params.allow_compression = false;
		maskmap_tex_params.wrapping = OpenGLTexture::Wrapping::Wrapping_Clamp;

		TextureParams detail_colourmap_tex_params;

		for(size_t i=0; i<spec.section_specs.size(); ++i)
		{
			const TerrainSpecSection& section_spec = spec.section_specs[i];
			const Vec4f centroid_ws(section_spec.x  * terrain_section_width_m, section_spec.y  * terrain_section_width_m, 0, 1);

			if(!section_spec.heightmap_URL.empty())
			{
				DownloadingResourceInfo info;
				info.texture_params = heightmap_tex_params;
				info.pos = Vec3d(centroid_ws);
				info.size_factor = LoadItemQueueItem::sizeFactorForAABBWS(aabb_ws_longest_len, /*importance_factor=*/1.f);
				info.used_by_terrain = true;
				startDownloadingResource(section_spec.heightmap_URL, centroid_ws, aabb_ws_longest_len, info);
			}
			if(!section_spec.mask_map_URL.empty())
			{
				DownloadingResourceInfo info;
				info.texture_params = maskmap_tex_params;
				info.pos = Vec3d(centroid_ws);
				info.size_factor = LoadItemQueueItem::sizeFactorForAABBWS(aabb_ws_longest_len, /*importance_factor=*/1.f);
				info.used_by_terrain = true;
				startDownloadingResource(section_spec.mask_map_URL, centroid_ws, aabb_ws_longest_len, info);
			}
			if(!section_spec.tree_mask_map_URL.empty())
			{
				DownloadingResourceInfo info;
				info.texture_params = maskmap_tex_params;
				info.pos = Vec3d(centroid_ws);
				info.size_factor = LoadItemQueueItem::sizeFactorForAABBWS(aabb_ws_longest_len, /*importance_factor=*/1.f);
				info.used_by_terrain = true;
				startDownloadingResource(section_spec.tree_mask_map_URL, centroid_ws, aabb_ws_longest_len, info);
			}
		}

		for(int i=0; i<4; ++i)
		{
			if(i == 0) // TEMP: don't load detail colour + height map 0 (rock), as it's not used currently in the terrain shader (see phong_frag_shader.glsl #if TERRAIN section)
				continue; 

			if(!use_detail_col_map_URLs[i].empty())
			{
				DownloadingResourceInfo info;
				info.texture_params = detail_colourmap_tex_params;
				info.pos = Vec3d(0,0,0);
				info.size_factor = LoadItemQueueItem::sizeFactorForAABBWS(aabb_ws_longest_len, /*importance_factor=*/1.f);
				info.used_by_terrain = true;
				startDownloadingResource(use_detail_col_map_URLs[i], /*centroid_ws=*/Vec4f(0,0,0,1), aabb_ws_longest_len, info);
			}
			if(!use_detail_height_map_URLs[i].empty())
			{
				DownloadingResourceInfo info;
				info.texture_params = heightmap_tex_params;
				info.pos = Vec3d(0,0,0);
				info.size_factor = LoadItemQueueItem::sizeFactorForAABBWS(aabb_ws_longest_len, /*importance_factor=*/1.f);
				info.used_by_terrain = true;
				startDownloadingResource(use_detail_height_map_URLs[i], /*centroid_ws=*/Vec4f(0,0,0,1), aabb_ws_longest_len, info);
			}
		}
		//--------------------------------------------------------------------------------------------------------------------------


		//----------------------------- Start loading textures, if the resource is already present on disk -----------------------------
		for(size_t i=0; i<spec.section_specs.size(); ++i)
		{
			const TerrainSpecSection& section_spec = spec.section_specs[i];
			TerrainPathSpecSection& path_section = path_spec.section_specs[i];

			const Vec4f centroid_ws(section_spec.x  * terrain_section_width_m, section_spec.y  * terrain_section_width_m, 0, 1);
			
			if(!section_spec.heightmap_URL.empty() && this->resource_manager->isFileForURLPresent(section_spec.heightmap_URL))
				load_item_queue.enqueueItem(section_spec.heightmap_URL, centroid_ws, aabb_ws_longest_len, 
					new LoadTextureTask(opengl_engine, resource_manager, &this->msg_queue, path_section.heightmap_path, this->resource_manager->getOrCreateResourceForURL(section_spec.heightmap_URL),
						heightmap_tex_params, /*is terrain map=*/true, worker_allocator, texture_loaded_msg_allocator, opengl_upload_thread), 
					/*max_dist_for_ob_lod_level=*/std::numeric_limits<float>::max(), /*importance_factor=*/1.f);

			if(!section_spec.mask_map_URL.empty() && this->resource_manager->isFileForURLPresent(section_spec.mask_map_URL))
				load_item_queue.enqueueItem(section_spec.mask_map_URL, centroid_ws, aabb_ws_longest_len, 
					new LoadTextureTask(opengl_engine, resource_manager, &this->msg_queue, path_section.mask_map_path, this->resource_manager->getOrCreateResourceForURL(section_spec.mask_map_URL),
						maskmap_tex_params, /*is terrain map=*/true, worker_allocator, texture_loaded_msg_allocator, opengl_upload_thread), 
					/*max_dist_for_ob_lod_level=*/std::numeric_limits<float>::max(), /*importance_factor=*/1.f);

			if(!section_spec.tree_mask_map_URL.empty() && this->resource_manager->isFileForURLPresent(section_spec.tree_mask_map_URL))
				load_item_queue.enqueueItem(section_spec.tree_mask_map_URL, centroid_ws, aabb_ws_longest_len, 
					new LoadTextureTask(opengl_engine, resource_manager, &this->msg_queue, path_section.tree_mask_map_path, this->resource_manager->getOrCreateResourceForURL(section_spec.tree_mask_map_URL), 
						maskmap_tex_params, /*is terrain map=*/true, worker_allocator, texture_loaded_msg_allocator, opengl_upload_thread), 
					/*max_dist_for_ob_lod_level=*/std::numeric_limits<float>::max(), /*importance_factor=*/1.f);
		}

		for(int i=0; i<4; ++i)
		{
			if(i == 0) // TEMP: don't load detail colour + height map 0 (rock), as it's not used currently in the terrain shader (see phong_frag_shader.glsl #if TERRAIN section)
				continue; 

			if(!use_detail_col_map_URLs[i].empty() && this->resource_manager->isFileForURLPresent(use_detail_col_map_URLs[i]))
				load_item_queue.enqueueItem(use_detail_col_map_URLs[i], Vec4f(0,0,0,1), aabb_ws_longest_len, 
					new LoadTextureTask(opengl_engine, resource_manager, &this->msg_queue, path_spec.detail_col_map_paths[i], this->resource_manager->getOrCreateResourceForURL(use_detail_col_map_URLs[i]), 
						detail_colourmap_tex_params, /*is terrain map=*/true, worker_allocator, texture_loaded_msg_allocator, opengl_upload_thread), 
					/*max_dist_for_ob_lod_level=*/std::numeric_limits<float>::max(), /*importance_factor=*/1.f);

			if(!use_detail_height_map_URLs[i].empty() && this->resource_manager->isFileForURLPresent(use_detail_height_map_URLs[i]))
				load_item_queue.enqueueItem(use_detail_height_map_URLs[i], Vec4f(0,0,0,1), aabb_ws_longest_len, 
					new LoadTextureTask(opengl_engine, resource_manager, &this->msg_queue, path_spec.detail_height_map_paths[i], this->resource_manager->getOrCreateResourceForURL(use_detail_height_map_URLs[i]), 
						heightmap_tex_params, /*is terrain map=*/true, worker_allocator, texture_loaded_msg_allocator, opengl_upload_thread), 
					/*max_dist_for_ob_lod_level=*/std::numeric_limits<float>::max(), /*importance_factor=*/1.f);
		}
		//--------------------------------------------------------------------------------------------------------------------------------


		terrain_system = new TerrainSystem();
		terrain_system->init(path_spec, this->base_dir_path, opengl_engine.ptr(), this->physics_world.ptr(), biome_manager, this->cam_controller.getPosition(), &this->model_and_texture_loader_task_manager, stack_allocator, &this->msg_queue);
	}

#if 0
	// Trace some rays against the ground, in order to test terrain physics.  Place a sphere where the rays intersect the ground.
	if(!done_terrain_test && total_timer.elapsed() > 6)
	{
		for(int x=0; x<100; ++x)
		for(int y=0; y<100; ++y)
		{
			float px = -500.f + x * 10;
			float py = -500.f + y * 10;
			RayTraceResult results;
			physics_world->traceRay(Vec4f(px,py,500,1), Vec4f(0,0,-1,0), 10000.f, results);

			GLObjectRef ob = opengl_engine->makeSphereObject(0.02f, Colour4f(1,0,0,1));
			ob->ob_to_world_matrix = Matrix4f::translationMatrix(Vec4f(px,py,500,1) + Vec4f(0,0,-1,0) * results.hitdist_ws) *  Matrix4f::uniformScaleMatrix(0.3f);
			opengl_engine->addObject(ob);
		}

		done_terrain_test = true;
	}
#endif
}


void GUIClient::performGestureClicked(const std::string& gesture_name, bool animate_head, bool loop_anim)
{
	const double cur_time = Clock::getTimeSinceInit(); // Used for animation, interpolation etc..

	// Change camera view to third person if it's not already, so we can see the gesture
	ui_interface->enableThirdPersonCameraIfNotAlreadyEnabled();

	{
		Lock lock(this->world_state->mutex);

		for(auto it = this->world_state->avatars.begin(); it != this->world_state->avatars.end(); ++it)
		{
			Avatar* av = it->second.getPointer();

			const bool our_avatar = av->uid == this->client_avatar_uid;
			if(our_avatar)
			{
				av->graphics.performGesture(cur_time, gesture_name, animate_head, loop_anim);
			}
		}
	}

	// Send AvatarPerformGesture message
	{
		MessageUtils::initPacket(scratch_packet, Protocol::AvatarPerformGesture);
		writeToStream(this->client_avatar_uid, scratch_packet);
		scratch_packet.writeStringLengthFirst(gesture_name);

		enqueueMessageToSend(*this->client_thread, scratch_packet);
	}
	sent_perform_gesture_without_stop_gesture = true;
}


void GUIClient::stopGesture()
{
	const double cur_time = Clock::getTimeSinceInit(); // Used for animation, interpolation etc..

	{
		Lock lock(this->world_state->mutex);

		for(auto it = this->world_state->avatars.begin(); it != this->world_state->avatars.end(); ++it)
		{
			Avatar* av = it->second.getPointer();

			const bool our_avatar = av->uid == this->client_avatar_uid;
			if(our_avatar)
			{
				av->graphics.stopGesture(cur_time/*, gesture_name*/);
			}
		}
	}

	// Send AvatarStopGesture message
	// If we are not logged in, we can't perform a gesture, so don't send a AvatarStopGesture message or we will just get error messages back from the server.
	//if(this->logged_in_user_id.valid())
	if(sent_perform_gesture_without_stop_gesture) // Make sure we don't spam AvatarStopGesture messages.
	{
		MessageUtils::initPacket(scratch_packet, Protocol::AvatarStopGesture);
		writeToStream(this->client_avatar_uid, scratch_packet);

		enqueueMessageToSend(*this->client_thread, scratch_packet);

		sent_perform_gesture_without_stop_gesture = false;
	}
}


void GUIClient::stopGestureClicked(const std::string& gesture_name)
{
	stopGesture();
}


void GUIClient::setSelfieModeEnabled(bool enabled)
{
	const double cur_time = Clock::getTimeSinceInit(); // Used for animation, interpolation etc..
	this->cam_controller.setSelfieModeEnabled(cur_time, enabled);

	if(enabled)
	{
		// Enable third-person camera view if not already enabled.
		ui_interface->enableThirdPersonCameraIfNotAlreadyEnabled();
	}
}


void GUIClient::setPhotoModeEnabled(bool enabled)
{
	if(enabled)
		this->photo_mode_ui.enablePhotoModeUI();
	else
		this->photo_mode_ui.disablePhotoModeUI();
}


void GUIClient::setMicForVoiceChatEnabled(bool enabled)
{
	if(enabled)
	{
		if(mic_read_thread_manager.getNumThreads() == 0)
		{
			try
			{
				Reference<glare::MicReadThread> mic_read_thread = new glare::MicReadThread(&this->msg_queue, this->udp_socket, this->client_avatar_uid, server_hostname, server_UDP_port,
					settings->getStringValue("setting/input_device_name", "Default"), //MainOptionsDialog::getInputDeviceName(settings),
					settings->getIntValue("setting/input_scale_factor_name", /*default val=*/100) * 0.01f, // NOTE: stored in percent in settings //MainOptionsDialog::getInputScaleFactor(settings), // input_vol_scale_factor
					&mic_read_status
				);
				mic_read_thread_manager.addThread(mic_read_thread);
			}
			catch(glare::Exception& e)
			{
				showInfoNotification("Failed to enable microphone input: " + e.what());
			}
		}
	}
	else
	{
		if(mic_read_thread_manager.getNumThreads() > 0)
		{
			mic_read_thread_manager.killThreadsNonBlocking();
		}
	}
}


void GUIClient::createImageObject(const std::string& local_image_path)
{
	Reference<Map2D> im = ImageDecoding::decodeImage(base_dir_path, local_image_path);

	createImageObjectForWidthAndHeight(local_image_path, (int)im->getMapWidth(), (int)im->getMapHeight(),
		LODGeneration::textureHasAlphaChannel(local_image_path, im) // has alpha
	);
}


// A model path has been drag-and-dropped or pasted.
void GUIClient::createModelObject(const std::string& local_model_path)
{
	const Vec3d ob_pos = cam_controller.getFirstPersonPosition() + cam_controller.getForwardsVec() * 2.0f;

	// Check permissions
	bool ob_pos_in_parcel;
	const bool have_creation_perms = haveParcelObjectCreatePermissions(ob_pos, ob_pos_in_parcel);
	if(!have_creation_perms)
	{
		if(ob_pos_in_parcel)
			showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
		else
			showErrorNotification("You can only create objects in a parcel that you have write permissions for.");
		return;
	}

	ModelLoading::MakeGLObjectResults results;
	ModelLoading::makeGLObjectForModelFile(*opengl_engine, *opengl_engine->vert_buf_allocator, worker_allocator.ptr(), local_model_path, /*do_opengl_stuff=*/false,
		results
	);

	const Vec3d adjusted_ob_pos = ob_pos;

	createObject(
		local_model_path, // mesh path
		results.batched_mesh,
		false, // loaded_mesh_is_image_cube
		results.voxels.voxels,
		adjusted_ob_pos,
		results.scale,
		results.axis,
		results.angle,
		results.materials
	);
}


void GUIClient::createImageObjectForWidthAndHeight(const std::string& local_image_path, int w, int h, bool has_alpha)
{
	// NOTE: adapted from AddObjectDialog::makeMeshForWidthAndHeight()

	BatchedMeshRef batched_mesh;
	std::vector<WorldMaterialRef> world_materials;
	Vec3f scale;
	GLObjectRef gl_ob = ModelLoading::makeImageCube(*opengl_engine, *opengl_engine->vert_buf_allocator, local_image_path, w, h, batched_mesh, world_materials, scale);

	WorldObjectRef new_world_object = new WorldObject();
	new_world_object->materials = world_materials;
	new_world_object->scale = scale;


	const float ob_cam_right_translation = -scale.x/2;
	const float ob_cam_up_translation    = -scale.z/2;

	const Vec3d ob_pos = cam_controller.getFirstPersonPosition() + cam_controller.getForwardsVec() * 2.0f + 
		cam_controller.getRightVec() * ob_cam_right_translation + cam_controller.getUpVec() * ob_cam_up_translation; // Centre object in front of camera

	// Check permissions
	bool ob_pos_in_parcel;
	const bool have_creation_perms = haveParcelObjectCreatePermissions(ob_pos, ob_pos_in_parcel);
	if(!have_creation_perms)
	{
		if(ob_pos_in_parcel)
			showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
		else
			showErrorNotification("You can only create objects in a parcel that you have write permissions for.");
		return;
	}
	
	new_world_object->pos = ob_pos;
	new_world_object->axis = Vec3f(0,0,1);
	new_world_object->angle = Maths::roundToMultipleFloating((float)cam_controller.getAngles().x - Maths::pi_2<float>(), Maths::pi_4<float>()); // Round to nearest 45 degree angle, facing camera.

	new_world_object->setAABBOS(batched_mesh->aabb_os);

	new_world_object->model_url = "image_cube_5438347426447337425.bmesh";

	BitUtils::setOrZeroBit(new_world_object->materials[0]->flags, WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG, has_alpha); // Set COLOUR_TEX_HAS_ALPHA_FLAG flag

	// Copy all dependencies (textures etc..) to resources dir.  UploadResourceThread will read from here.
	WorldObject::GetDependencyOptions options;
	options.use_basis = false; // Server will want the original non-basis textures.
	options.include_lightmaps = false;
	options.get_optimised_mesh = false; // Server will want the original unoptimised mesh.
	DependencyURLSet paths;
	new_world_object->getDependencyURLSet(/*ob_lod_level=*/0, options, paths);
	for(auto it = paths.begin(); it != paths.end(); ++it)
	{
		const URLString& path = it->URL;
		if(FileUtils::fileExists(path))
		{
			const uint64 hash = FileChecksum::fileChecksum(path);
			const URLString resource_URL = ResourceManager::URLForPathAndHash(toStdString(path), hash);
			resource_manager->copyLocalFileToResourceDir(toStdString(path), resource_URL);
		}
	}

	// Convert texture paths on the object to URLs
	new_world_object->convertLocalPathsToURLS(*resource_manager);

	// Send CreateObject message to server
	MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
	new_world_object->writeToNetworkStream(scratch_packet);
	enqueueMessageToSend(*client_thread, scratch_packet);

	showInfoNotification("Object created.");
}


// On Mac laptops, the delete key sends a Key_Backspace keycode for some reason.  So check for that as well.
static bool keyIsDeleteKey(int keycode)
{
#if defined(OSX)
	return 
		keycode == Key::Key_Backspace || 
		keycode == Key::Key_Delete;
#else
	return keycode == Key::Key_Delete;
#endif
}


// E.g. 'E' key was pressed, or gamepad X button was pressed.
// if use_mouse_cursor is false, use crosshair as cursor instead.
void GUIClient::useActionTriggered(bool use_mouse_cursor)
{
	// If we are controlling a vehicle, exit it
	if(vehicle_controller_inside.nonNull())
	{
		const Vec4f last_hover_car_pos = vehicle_controller_inside->getBodyTransform(*this->physics_world) * Vec4f(0,0,0,1);
		const Vec4f last_hover_car_linear_vel = vehicle_controller_inside->getLinearVel(*this->physics_world);

		const Vec4f last_hover_car_right_ws = vehicle_controller_inside->getSeatToWorldTransform(*this->physics_world, this->cur_seat_index, /*use_smoothed_network_transform=*/true) * Vec4f(-1,0,0,0);
		// TODO: make this programmatically the same side as the seat, or make the exit position scriptable?

		vehicle_controller_inside->userExitedVehicle(this->cur_seat_index);
			
		// Execute event handlers in any scripts that are listening for the onUserExitedVehicle event from this object.
		{
			WorldStateLock lock(world_state->mutex);
			WorldObject* vehicle_ob = vehicle_controller_inside->getControlledObject();
			if(vehicle_ob->event_handlers)
				vehicle_ob->event_handlers->executeOnUserExitedVehicleHandlers(this->client_avatar_uid, vehicle_ob->uid, lock);
		}

		vehicle_controller_inside = NULL; // Null out vehicle_controller_inside reference.  Note that the controller will still exist and be referenced from vehicle_controllers.

		Vec4f new_player_pos = last_hover_car_pos + last_hover_car_right_ws * 2 + Vec4f(0,0,1.7f,0);
		new_player_pos[2] = myMax(new_player_pos[2], 1.67f); // Make sure above ground

		player_physics.setEyePosition(Vec3d(new_player_pos), /*linear vel=*/last_hover_car_linear_vel);

		player_physics.setStandingShape(*physics_world);


		// Send AvatarExitedVehicle message to server
		MessageUtils::initPacket(scratch_packet, Protocol::AvatarExitedVehicle);
		writeToStream(this->client_avatar_uid, scratch_packet);
		enqueueMessageToSend(*this->client_thread, scratch_packet);

		return;
	}
	else
	{
		WorldStateLock lock(world_state->mutex);

		const Vec2i widget_pos = use_mouse_cursor ? ui_interface->getMouseCursorWidgetPos() : Vec2i(opengl_engine->getMainViewPortWidth() / 2, opengl_engine->getMainViewPortHeight() / 2);

		// conPrint("glWidgetKeyPressed: widget_pos: " + toString(widget_pos.x()) + ", " + toString(widget_pos.y()));

		// Trace ray through scene
		const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
		const Vec4f dir = getDirForPixelTrace(widget_pos.x, widget_pos.y);

		RayTraceResult results;
		this->physics_world->traceRay(origin, dir, /*max_t=*/1.0e5f, /*ignore body id=*/JPH::BodyID(), results);

		if(results.hit_object)
		{
			if(results.hit_object->userdata && results.hit_object->userdata_type == 0) // If we hit an object:
			{
				WorldObject* ob = static_cast<WorldObject*>(results.hit_object->userdata);

				if(ob->vehicle_script.nonNull() && ob->physics_object.nonNull())
				{
					if(ob->isDynamic()) // Make sure object is dynamic, which is needed for vehicles
					{
						if(vehicle_controller_inside.isNull()) // If we are not in a vehicle already:
						{
							// Try to enter the vehicle.
							const Vec4f up_z_up(0,0,1,0);
							const Vec4f bike_up_os = ob->vehicle_script->getZUpToModelSpaceTransform() * up_z_up;
							const Vec4f bike_up_ws = normalise(obToWorldMatrix(*ob) * bike_up_os);
							const bool upright = dot(bike_up_ws, up_z_up) > 0.5f;

							// See if there are any spare seats
							int free_seat_index = -1;
							for(size_t z=0; z<ob->vehicle_script->settings->seat_settings.size(); ++z)
							{
								if(!doesVehicleHaveAvatarInSeat(*ob, (uint32)z))
								{
									free_seat_index = (int)z;
									break;
								}
							}

							Reference<VehiclePhysics> controller_for_ob;
							if((ob->vehicle_script->isRightable() && !upright) || (free_seat_index >= 0)) // If we want to have a vehicle controller (for righting vehicle or entering it)
							{
								// If there is not a controller already existing that is controlling the hit object, create the vehicle controller based on the script:
								const auto controller_res = vehicle_controllers.find(ob);
								if(controller_res == vehicle_controllers.end()) // If there is not a vehicle controller for this object yet:
								{
									controller_for_ob = createVehicleControllerForScript(ob);
									vehicle_controllers.insert(std::make_pair(ob, controller_for_ob));
								}
								else // Else if there is already a vehicle controller for this object:
								{
									controller_for_ob = controller_res->second;
								}
							}


							if(!ob->vehicle_script->isRightable() || upright)
							{
								if(free_seat_index == -1) // If we did not find an empty seat:
									showInfoNotification("Vehicle is full, cannot enter");
								else
								{
									// Enter vehicle
									runtimeCheck(controller_for_ob.nonNull()); // Should have been created above if was null, as free_seat_index >= 0
										
									this->cur_seat_index = (uint32)free_seat_index;
										
									this->vehicle_controller_inside = controller_for_ob;
									this->vehicle_controller_inside->userEnteredVehicle(/*seat index=*/free_seat_index);
								
									if(free_seat_index == 0) // If taking driver's seat:
										takePhysicsOwnershipOfObject(*ob, world_state->getCurrentGlobalTime());

									player_physics.setSittingShape(*physics_world);

									// Execute event handlers in any scripts that are listening for the onUserEnteredVehicle event from this object.
									if(ob->event_handlers)
										ob->event_handlers->executeOnUserEnteredVehicleHandlers(this->client_avatar_uid, ob->uid, lock);

									// Send AvatarEnteredVehicle message to server
									MessageUtils::initPacket(scratch_packet, Protocol::AvatarEnteredVehicle);
									writeToStream(this->client_avatar_uid, scratch_packet);
									writeToStream(ob->uid, scratch_packet); // Write vehicle object UID
									scratch_packet.writeUInt32((uint32)free_seat_index); // Seat index.
									scratch_packet.writeUInt32(0); // Write flags.  Don't set renewal bit.
									enqueueMessageToSend(*this->client_thread, scratch_packet);
								}
							}
							else // Else if vehicle is not upright:
							{
								runtimeCheck(controller_for_ob.nonNull()); // Should have been created above if was null, as upright == false.
								controller_for_ob->startRightingVehicle();
							}

							return;
						}
					}
					else // else if !ob->isDyanmic():
					{
						showErrorNotification("Object dynamic checkbox must be checked to drive");
					}
				}

				if(!ob->target_url.empty()) // And the object has a target URL:
				{
					// If the mouse-overed ob is currently selected, and is editable, don't show the hyperlink, because 'E' is the key to pick up the object.
					const bool selected_editable_ob = (selected_ob.ptr() == ob) && objectModificationAllowed(*ob);

					if(!selected_editable_ob)
					{
						const std::string url = ob->target_url;
						if(StringUtils::containsString(url, "://"))
						{
							// URL already has protocol prefix
							const std::string protocol = url.substr(0, url.find("://", 0));
							if(protocol == "http" || protocol == "https")
							{
								ui_interface->openURL(url);
							}
							else if(protocol == "sub")
							{
								visitSubURL(url);
							}
							else
							{
								// Don't open this URL, might be something potentially unsafe like a file on disk
								ui_interface->showPlainTextMessageBox("", "This URL is potentially unsafe and will not be opened.");
							}
						}
						else
						{
							ui_interface->openURL("http://" + url);
						}

						return;
					}
				}

				// If the object has scripts that have an onUserUsedObject event handler for the object, send a UserUsedObjectMessage to the server
				// So the script event handler can be run
				if(ob->event_handlers && ob->event_handlers->onUserUsedObject_handlers.nonEmpty())
				{
					// Execute any event handlers
					ob->event_handlers->executeOnUserUsedObjectHandlers(/*avatar_uid=*/this->client_avatar_uid, ob->uid, lock);

					// Make message packet and enqueue to send to execute event handler on server as well
					MessageUtils::initPacket(scratch_packet, Protocol::UserUsedObjectMessage);
					writeToStream(ob->uid, scratch_packet);
					enqueueMessageToSend(*client_thread, scratch_packet);
				}
			}
		}
	}
}


void GUIClient::loginButtonClicked()
{
	ui_interface->loginButtonClicked();
}


void GUIClient::signupButtonClicked()
{
	ui_interface->signUpButtonClicked();
}


void GUIClient::loggedInButtonClicked()
{
	ui_interface->loggedInButtonClicked();
}


std::string GUIClient::getCurrentWebClientURLPath() const
{
	std::string url_path = "/webclient?";

	if(!this->server_worldname.empty()) // Append world if != empty string.
		url_path += "world=" + web::Escaping::URLEscape(this->server_worldname) + '&';

	const Vec3d pos = this->cam_controller.getFirstPersonPosition();

	const double heading_deg = Maths::doubleMod(::radToDegree(this->cam_controller.getAngles().x), 360.0);

	// Use two decimal places for z coordinate so that when spawning, with gravity enabled initially, we have sufficient vertical resolution to be detected as on ground, so flying animation doesn't play.
	url_path += "x=" + doubleToStringNDecimalPlaces(pos.x, 1) + "&y=" + doubleToStringNDecimalPlaces(pos.y, 1) + "&z=" + doubleToStringNDecimalPlaces(pos.z, 2) + 
		"&heading=" + doubleToStringNDecimalPlaces(heading_deg, 1);

	// If the original URL had an explicit sun angle in it, keep it.
	if(this->last_url_parse_results.parsed_sun_azimuth_angle)
		url_path += "&sun_azimuth_angle=" + doubleToStringNDecimalPlaces(this->last_url_parse_results.sun_azimuth_angle, 1);
	if(this->last_url_parse_results.parsed_sun_vert_angle)
		url_path += "&sun_vert_angle=" + doubleToStringNDecimalPlaces(this->last_url_parse_results.sun_vert_angle, 1);

	return url_path;
}


class MySSAODebuggingDepthQuerier : public SSAODebugging::DepthQuerier
{
public:
	virtual float depthForPosSS(const Vec2f& pos_ss) // returns positive depth
	{
		const float sensor_width = ::sensorWidth();
		const float sensor_height = sensor_width / gui_client->opengl_engine->getViewPortAspectRatio();//ui->glWidget->viewport_aspect_ratio;
		const float lens_sensor_dist = ::lensSensorDist();

		const Vec4f forwards = gui_client->cam_controller.getForwardsVec().toVec4fVector();
		const Vec4f right = gui_client->cam_controller.getRightVec().toVec4fVector();
		const Vec4f up = gui_client->cam_controller.getUpVec().toVec4fVector();

		//Vec4f trace_dir = gui_client->cam_controller.getForwardsVec().toVec4fVector();
		const float s_x = sensor_width  * (pos_ss.x - 0.5f); // (float)(pixel_pos_x - gl_w/2) / gl_w;  // dist right on sensor from centre of sensor
		const float s_y = sensor_height * (pos_ss.y - 0.5f); // (float)(pixel_pos_y - gl_h/2) / gl_h; // dist down on sensor from centre of sensor

		const float r_x = s_x / lens_sensor_dist;
		const float r_y = s_y / lens_sensor_dist;

		const Vec4f trace_dir = normalise(forwards + right * r_x + up * r_y);

		RayTraceResult results;
		gui_client->physics_world->traceRay(gui_client->cam_controller.getPosition().toVec4fPoint(), trace_dir, 10000.f, JPH::BodyID(), results);


		return results.hit_t * dot(trace_dir, gui_client->cam_controller.getForwardsVec().toVec4fVector()); // Adjust from distance to depth
	}
	
	// Return normal in camera space
	virtual Vec3f normalCSForPosSS(const Vec2f& pos_ss)
	{
		const float sensor_width = ::sensorWidth();
		const float sensor_height = sensor_width / gui_client->opengl_engine->getViewPortAspectRatio();//ui->glWidget->viewport_aspect_ratio;
		const float lens_sensor_dist = ::lensSensorDist();

		const Vec4f forwards = gui_client->cam_controller.getForwardsVec().toVec4fVector();
		const Vec4f right = gui_client->cam_controller.getRightVec().toVec4fVector();
		const Vec4f up = gui_client->cam_controller.getUpVec().toVec4fVector();

		//Vec4f trace_dir = gui_client->cam_controller.getForwardsVec().toVec4fVector();
		const float s_x = sensor_width  * (pos_ss.x - 0.5f); // (float)(pixel_pos_x - gl_w/2) / gl_w;  // dist right on sensor from centre of sensor
		const float s_y = sensor_height * (pos_ss.y - 0.5f); // (float)(pixel_pos_y - gl_h/2) / gl_h; // dist down on sensor from centre of sensor

		const float r_x = s_x / lens_sensor_dist;
		const float r_y = s_y / lens_sensor_dist;

		const Vec4f trace_dir = normalise(forwards + right * r_x + up * r_y);

		RayTraceResult results;
		gui_client->physics_world->traceRay(gui_client->cam_controller.getPosition().toVec4fPoint(), trace_dir, 10000.f, JPH::BodyID(), results);

		return normalise(Vec3f(gui_client->opengl_engine->getCurrentScene()->last_view_matrix * results.hit_normal_ws));
	}
	GUIClient* gui_client;
};


void GUIClient::keyPressed(KeyEvent& e)
{
	if(gl_ui)
		gl_ui->handleKeyPressedEvent(e);
	if(e.accepted)
		return;

	// Update our key-state variables, jump if space was pressed.
	{
		SHIFT_down = BitUtils::isBitSet(e.modifiers, (uint32)Modifiers::Shift);// (e->modifiers() & ShiftModifier);
		CTRL_down  = BitUtils::isBitSet(e.modifiers, (uint32)Modifiers::Ctrl);// (e->modifiers() & ControlModifier);

		if(e.key == Key::Key_Space)
		{
			//if(cam_move_on_key_input_enabled)
			if(ui_interface->isKeyboardCameraMoveEnabled() && !(cam_controller.current_cam_mode == CameraController::CameraMode_FreeCamera))
				player_physics.processJump(this->cam_controller, /*cur time=*/Clock::getTimeSinceInit());
			space_down = true;
		}
		else if(e.key == Key::Key_W)
		{
			W_down = true;
		}
		else if(e.key == Key::Key_S)
		{
			S_down = true;
		}
		else if(e.key == Key::Key_A)
		{
			A_down = true;
		}
		if(e.key == Key::Key_D)
		{
			D_down = true;
		}
		else if(e.key == Key::Key_C)
		{
			C_down = true;
		}
		else if(e.key == Key::Key_Left)
		{
			left_down = true;
		}
		else if(e.key == Key::Key_Right)
		{
			right_down = true;
		}
		else if(e.key == Key::Key_Up)
		{
			up_down = true;
		}
		else if(e.key == Key::Key_Down)
		{
			down_down = true;
		}
		else if(e.key == Key::Key_B)
		{
			B_down = true;
		}
	}


	if(e.key == Key::Key_Escape)
	{
		if(this->selected_ob.nonNull())
			deselectObject();

		if(this->selected_parcel.nonNull())
			deselectParcel();
	}

	if(selected_ob.nonNull() && selected_ob->web_view_data.nonNull()) // If we have a web-view object selected, send keyboard input to it:
	{
		ui_interface->setKeyboardCameraMoveEnabled(false); // We don't want WASD keys etc. to move the camera while we enter text into the webview, so disable camera moving from the keyboard.
		selected_ob->web_view_data->keyPressed(&e);

		if(keyIsDeleteKey(e.key))
			showInfoNotification("Use Edit > Delete Object menu command to delete object.");
		return;
	}
	else
		ui_interface->setKeyboardCameraMoveEnabled(true);

	
	if(keyIsDeleteKey(e.key))
	{
		if(this->selected_ob.nonNull())
		{
			deleteSelectedObject();
		}
	}
	else if(e.key == Key::Key_O)
	{
		// SSAODebugging debugging;
		// MySSAODebuggingDepthQuerier depth_querier;
		// depth_querier.gui_client = this;
		// debugging.computeReferenceAO(*opengl_engine, depth_querier);
	}
	else if(e.key == Key::Key_P)
	{
		// Spawn particle for testing
		//for(int i=0; i<1; ++i)
		//{
		//	Particle particle;
		//	particle.pos = (cam_controller.getFirstPersonPosition() + cam_controller.getForwardsVec()).toVec4fPoint(); // Vec4f(0,0,1.5,1);

		//	particle.vel = Vec4f(-1 + 2*rng.unitRandom(),-1 + 2*rng.unitRandom(),rng.unitRandom() * 2,0) * 1.f;
		//
		//	particle.area = 0.01; // 0.000001f;

		//	particle.colour = Colour3f(0.75f);

		//	particle.particle_type = Particle::ParticleType_Smoke;

		//	particle.dwidth_dt = 0;
		//	particle.theta = rng.unitRandom() * Maths::get2Pi<float>();

		//	particle.dopacity_dt = 0;//-0.01f;

		//	particle_manager->addParticle(particle);
		//}


#if 0
		// TEMP HACK: play materialise effect on selected object.
		//if(selected_ob.nonNull())
		//	enableMaterialisationEffectOnOb(*selected_ob);

		//ui->glWidget->opengl_engine->add_debug_obs = true;

		//-------------------------------- Add a test decal object ----------------------------------------
		const QPoint widget_pos = ui->glWidget->mapFromGlobal(QCursor::pos());

		// conPrint("glWidgetKeyPressed: widget_pos: " + toString(widget_pos.x()) + ", " + toString(widget_pos.y()));

		// Trace ray through scene
		const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
		const Vec4f dir = getDirForPixelTrace(widget_pos.x(), widget_pos.y());

		RayTraceResult results;
		this->physics_world->traceRay(origin, dir, /*max_t=*/1.0e5f, results);

		const Planef waterplane(Vec4f(0,0, this->connected_world_settings.terrain_spec.water_z, 1), Vec4f(0,0,1,0));

		if(waterplane.rayIntersect(origin, dir) > 0 && waterplane.rayIntersect(origin, dir) < results.hitdist_ws)
		{
			results.hitdist_ws = waterplane.rayIntersect(origin, dir);
			results.hit_normal_ws = Vec4f(0,0,1,0);
		}

		if(results.hit_object)
		{
			const Vec4f hitpos = origin + dir * results.hitdist_ws;
			const Vec4f hitnormal = results.hit_normal_ws;

			// Add plane quad
			GLObjectRef ob = new GLObject();
			ob->decal = true;
			ob->mesh_data = ui->glWidget->opengl_engine->getCubeMeshData();
			ob->materials.resize(1);
			ob->materials[0].albedo_linear_rgb = Colour3f(1.f);
			ob->materials[0].alpha = 1;
			ob->materials[0].transparent = false;
			ob->materials[0].double_sided = true;
			ob->materials[0].decal = true;
			ob->materials[0].albedo_texture = ui->glWidget->opengl_engine->getTexture("C:\\programming\\cyberspace\\output\\vs2022\\cyberspace_x64\\RelWithDebInfo\\foam.png");
			ob->ob_to_world_matrix = Matrix4f::translationMatrix(hitpos) * Matrix4f::constructFromVectorStatic(hitnormal) * 
				Matrix4f::scaleMatrix(1, 1, 0.1f) * Matrix4f::translationMatrix(-0.5f, -0.5f, -0.5f);
			ui->glWidget->opengl_engine->addObject(ob);
		}
		//-------------------------------- End add a test decal object ----------------------------------------
#endif
	}
	else if(e.key == Key::Key_E)
	{
		useActionTriggered(/*use_mouse_cursor=*/true);
	}
	else if(e.key == Key::Key_F)
	{
		ui_interface->toggleFlyMode();
	}
	else if(e.key == Key::Key_V)
	{
		ui_interface->toggleThirdPersonCameraMode();
	}
	else if(e.key == Key::Key_B)
	{
		if(BitUtils::isBitSet(e.modifiers, (uint32)Modifiers::Ctrl))
		{
			if(BitUtils::isBitSet(e.modifiers, (uint32)Modifiers::Shift))
			{
				conPrint("CTRL+SHIFT+B detected, summoning boat...");
				try
				{
					summonBoat();
				}
				catch(glare::Exception& e)
				{
					showErrorNotification(e.what());
				}
			}
			else
			{
				conPrint("CTRL+B detected, summoning bike...");
				try
				{
					summonBike();
				}
				catch(glare::Exception& e)
				{
					showErrorNotification(e.what());
				}
			}
		}
	}
	else if(e.key == Key::Key_C)
	{
		if(BitUtils::isBitSet(e.modifiers, (uint32)Modifiers::Ctrl) && BitUtils::isBitSet(e.modifiers, (uint32)Modifiers::Shift))
		{
			conPrint("CTRL+SHIFT+C detected, summoning car...");
			try
			{
				summonCar();
			}
			catch(glare::Exception& e)
			{
				showErrorNotification(e.what());
			}
		}
	}
	else if(e.key == Key::Key_H)
	{
		if(BitUtils::isBitSet(e.modifiers, (uint32)Modifiers::Ctrl))
		{
			conPrint("CTRL+H detected, summoning hovercar...");
			try
			{
				summonHovercar();
			}
			catch(glare::Exception& e)
			{
				showErrorNotification(e.what());
			}
		}
	}
	if(this->selected_ob.nonNull())
	{
		const float angle_step = Maths::pi<float>() / 32;
		if(e.key == Key::Key_LeftBracket)
		{
			// Rotate object clockwise around z axis
			rotateObject(this->selected_ob, Vec4f(0,0,1,0), -angle_step);
		}
		else if(e.key == Key::Key_RightBracket)
		{
			rotateObject(this->selected_ob, Vec4f(0,0,1,0), angle_step);
		}
		else if(e.key == Key::Key_PageUp)
		{
			// Rotate object clockwise around camera right-vector
			rotateObject(this->selected_ob, this->cam_controller.getRightVec().toVec4fVector(), -angle_step);
		}
		else if(e.key == Key::Key_PageDown)
		{
			// Rotate object counter-clockwise around camera right-vector
			rotateObject(this->selected_ob, this->cam_controller.getRightVec().toVec4fVector(), angle_step);
		}
		else if(e.key == Key::Key_Equals || e.key == Key::Key_Plus)
		{
			this->selection_vec_cs[1] *= 1.05f;
		}
		else if(e.key == Key::Key_Minus)
		{
			this->selection_vec_cs[1] *= (1/1.05f);
		}
		else if(e.key == Key::Key_P)
		{
			if(!this->selected_ob_picked_up)
				pickUpSelectedObject();
			else
				dropSelectedObject();
		}
	}

	if(e.key == Key::Key_F5)
	{
		URLParseResults url_parse_results;
		url_parse_results.hostname = this->server_hostname;
		url_parse_results.worldname = this->server_worldname;
		url_parse_results.x = this->cam_controller.getPosition().x;
		url_parse_results.y = this->cam_controller.getPosition().y;
		url_parse_results.z = this->cam_controller.getPosition().z;
		url_parse_results.parsed_x = url_parse_results.parsed_y = url_parse_results.parsed_z = true;
		url_parse_results.heading =  Maths::doubleMod(::radToDegree(this->cam_controller.getAngles().x), 360.0);

		this->connectToServer(url_parse_results);
	}
}


void GUIClient::keyReleased(KeyEvent& e)
{
	SHIFT_down = BitUtils::isBitSet(e.modifiers, (uint32)Modifiers::Shift);// (e->modifiers() & ShiftModifier);
	CTRL_down  = BitUtils::isBitSet(e.modifiers, (uint32)Modifiers::Ctrl);// (e->modifiers() & ControlModifier);

	if(e.key == Key::Key_Space)
	{
		space_down = false;
	}
	else if(e.key == Key::Key_W)
	{
		W_down = false;
	}
	else if(e.key == Key::Key_S)
	{
		S_down = false;
	}
	else if(e.key == Key::Key_A)
	{
		A_down = false;
	}
	else if(e.key == Key::Key_D)
	{
		D_down = false;
	}
	else if(e.key == Key::Key_C)
	{
		C_down = false;
	}
	else if(e.key == Key::Key_Left)
	{
		left_down = false;
	}
	else if(e.key == Key::Key_Right)
	{
		right_down = false;
	}
	else if(e.key == Key::Key_Up)
	{
		up_down = false;
	}
	else if(e.key == Key::Key_Down)
	{
		down_down = false;
	}
	else if(e.key == Key::Key_B)
	{
		B_down = false;
	}
}


void GUIClient::handleTextInputEvent(TextInputEvent& text_input_event)
{
	if(gl_ui.nonNull())
	{
		gl_ui->handleTextInputEvent(text_input_event);
		if(text_input_event.accepted)
			return;
	}

	if(selected_ob.nonNull() && selected_ob->web_view_data.nonNull()) // If we have a web-view object selected, send keyboard input to it:
	{
		ui_interface->setKeyboardCameraMoveEnabled(false); // We don't want WASD keys etc. to move the camera while we enter text into the webview, so disable camera moving from the keyboard.
		selected_ob->web_view_data->handleTextInputEvent(text_input_event);
	}
}


void GUIClient::focusOut()
{
	SHIFT_down = false;
	CTRL_down = false;
	A_down = false;
	W_down = false;
	S_down = false;
	D_down = false;
	space_down = false;
	C_down = false; 
	left_down = false;
	right_down = false;
	up_down = false;
	down_down = false;
	B_down = false;
}


static const size_t MAX_NUM_NOTIFICATIONS = 5;
static const double NOTIFICATION_DISPLAY_TIME = 5.0;

static const size_t MAX_NUM_SCRIPT_MESSAGES = 1;
static const double SCRIPT_MESSAGE_DISPLAY_TIME = 3.0;


void GUIClient::updateNotifications(double cur_time)
{
	// Check to see if we should remove any old notifications
	for(auto it = notifications.begin(); it != notifications.end();)
	{
		if(cur_time >  it->creation_time + NOTIFICATION_DISPLAY_TIME)
		{
			gl_ui->removeWidget(it->text_view); // Remove the notification from the UI
			it = notifications.erase(it); // remove from list
		}
		else
			++it;
	}

	// Go through list again and update position of all notifications
	int i=0;
	for(auto it = notifications.begin(); it != notifications.end(); ++it, ++i)
	{
		it->text_view->setPos(*gl_ui, 
			Vec2f(
				myMax(-1.f, -it->text_view->getRect().getWidths().x / 2.f),
				//-gl_ui->getUIWidthForDevIndepPixelWidth(150), 
				gl_ui->getViewportMinMaxY() - gl_ui->getUIWidthForDevIndepPixelWidth(40 + i * 40.f)
			)
		);

		// Make the notification fade out when it is nearly time to remove it.
		const float frac = (float)((cur_time - it->creation_time) / NOTIFICATION_DISPLAY_TIME);
		const float alpha = myClamp(1.f - Maths::smoothStep(0.8f, 1.2f, frac) * 2, 0.f, 1.f);

		it->text_view->setTextAlpha(alpha);
		it->text_view->setBackgroundAlpha(alpha);
	}


	//------------------------- Update script messages too ------------------------------------------
	
	// Check to see if we should remove any old notifications
	for(auto it = script_messages.begin(); it != script_messages.end();)
	{
		if(cur_time >  it->creation_time + SCRIPT_MESSAGE_DISPLAY_TIME)
		{
			gl_ui->removeWidget(it->text_view); // Remove the notification from the UI
			it = script_messages.erase(it); // remove from list
		}
		else
			++it;
	}

	// Go through list again and update position of all script messages
	i=0;
	for(auto it = script_messages.begin(); it != script_messages.end(); ++it, ++i)
	{
		it->text_view->setPos(*gl_ui, 
			Vec2f(
				myMax(-1.f, -it->text_view->getRect().getWidths().x / 2.f),
				gl_ui->getViewportMinMaxY() * 0.5f //  - gl_ui->getUIWidthForDevIndepPixelWidth(40 + i * 40.f)
			)
		);

		// Make the notification fade out when it is nearly time to remove it.
		const float frac = (float)((cur_time - it->creation_time) / SCRIPT_MESSAGE_DISPLAY_TIME);
		const float alpha = myClamp(1.f - Maths::smoothStep(0.8f, 1.2f, frac) * 2, 0.f, 1.f);

		it->text_view->setTextAlpha(alpha);
		it->text_view->setBackgroundAlpha(alpha);
	}
}


void GUIClient::setNotificationsVisible(bool visible)
{
	for(auto it = notifications.begin(); it != notifications.end(); ++it)
		it->text_view->setVisible(visible);
}


void GUIClient::showErrorNotification(const std::string& message)
{
	GLUITextView::CreateArgs args;
	args.background_colour = toLinearSRGB(Colour3f(1, 200/255.f, 200/255.f));
	//args.background_alpha = 0.8f;
	args.text_colour = Colour3f(0.f);
	args.padding_px = 8;
	GLUITextViewRef text_view = new GLUITextView(*gl_ui, opengl_engine, UTF8Utils::sanitiseUTF8String(message), Vec2f(0,0), args);

	gl_ui->addWidget(text_view);

	Notification n;
	n.creation_time = Clock::getTimeSinceInit();
	n.text_view = text_view;
	notifications.push_back(n);

	if(notifications.size() > MAX_NUM_NOTIFICATIONS)
	{
		// Remove the first (oldest) notification
		Notification& notification = notifications.front();
		gl_ui->removeWidget(notification.text_view);
		notifications.pop_front(); // remove from list
	}
}


void GUIClient::showInfoNotification(const std::string& message)
{
	GLUITextView::CreateArgs args;
	args.background_colour = toLinearSRGB(Colour3f(239/255.f, 228/255.f, 176/255.f));
	//args.background_alpha = 0.8f;
	args.text_colour = Colour3f(0.f);
	args.padding_px = 8;
	GLUITextViewRef text_view = new GLUITextView(*gl_ui, opengl_engine, UTF8Utils::sanitiseUTF8String(message), Vec2f(0,0), args);

	gl_ui->addWidget(text_view);

	Notification n;
	n.creation_time = Clock::getTimeSinceInit();
	n.text_view = text_view;
	notifications.push_back(n);

	if(notifications.size() > MAX_NUM_NOTIFICATIONS)
	{
		// Remove the first (oldest) notification
		Notification& notification = notifications.front();
		gl_ui->removeWidget(notification.text_view);
		notifications.pop_front(); // remove from list
	}
}


void GUIClient::showScriptMessage(const std::string& message)
{
	GLUITextView::CreateArgs args;
	args.background_colour = toLinearSRGB(Colour3f(230 / 255.f));
	//args.background_alpha = 0.8f;
	args.text_colour = Colour3f(0.f);
	args.padding_px = 8;
	GLUITextViewRef text_view = new GLUITextView(*gl_ui, opengl_engine, UTF8Utils::sanitiseUTF8String(message), Vec2f(0,0), args);

	gl_ui->addWidget(text_view);

	Notification n;
	n.creation_time = Clock::getTimeSinceInit();
	n.text_view = text_view;
	script_messages.push_back(n);

	if(script_messages.size() > MAX_NUM_SCRIPT_MESSAGES)
	{
		// Remove the first (oldest) notification
		Notification& notification = script_messages.front();
		gl_ui->removeWidget(notification.text_view);
		script_messages.pop_front(); // remove from list
	}
}
