/*=====================================================================
MainWindow.cpp
--------------
Copyright Glare Technologies Limited 2020 -
=====================================================================*/


#ifdef _MSC_VER // Qt headers suppress some warnings on Windows, make sure the warning suppression doesn't propagate to our code. See https://bugreports.qt.io/browse/QTBUG-26877
#pragma warning(push, 0) // Disable warnings
#endif
#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "AboutDialog.h"
#include "ClientThread.h"
#include "GoToPositionDialog.h"
#include "LogWindow.h"
#include "UserDetailsWidget.h"
#include "AvatarSettingsDialog.h"
#include "AddObjectDialog.h"
#include "AddVideoDialog.h"
#include "MainOptionsDialog.h"
#include "FindObjectDialog.h"
#include "ListObjectsNearbyDialog.h"
#include "ModelLoading.h"
#include "TestSuite.h"
#include "MeshBuilding.h"
#include "ThreadMessages.h"
#include "TerrainSystem.h"
#include "TerrainDecalManager.h"
#include "LoadScriptTask.h"
#include "CredentialManager.h"
#include "UploadResourceThread.h"
#include "DownloadResourcesThread.h"
#include "NetDownloadResourcesThread.h"
#include "ObjectPathController.h"
#include "AvatarGraphics.h"
#include "GuiClientApplication.h"
#include "WinterShaderEvaluator.h"
#include "LoginDialog.h"
#include "SignUpDialog.h"
#include "GoToParcelDialog.h"
#include "ResetPasswordDialog.h"
#include "ChangePasswordDialog.h"
#include "ClientUDPHandlerThread.h"
#include "URLWidget.h"
#include "URLWhitelist.h"
#include "URLParser.h"
#include "LoadModelTask.h"
#include "BuildScatteringInfoTask.h"
#include "LoadTextureTask.h"
#include "LoadAudioTask.h"
#include "../audio/MP3AudioFileReader.h"
#include "../audio/MicReadThread.h"
#include "MakeHypercardTextureTask.h"
#include "SaveResourcesDBThread.h"
#include "CameraController.h"
#include "BiomeManager.h"
#include "WebViewData.h"
#include "BrowserVidPlayer.h"
#include "AnimatedTextureManager.h"
#include "CEF.h"
#include "ParticleManager.h"
#include "../shared/Protocol.h"
#include "../shared/Version.h"
#include "../shared/LODGeneration.h"
#include "../shared/ImageDecoding.h"
#include "../shared/MessageUtils.h"
#include "../shared/FileTypes.h"
#include "../server/User.h"
#include "../shared/WorldSettings.h"
#include <QtCore/QProcess>
#include <QtCore/QMimeData>
#include <QtCore/QSettings>
#include <QtWidgets/QApplication>
#include <QtGui/QMouseEvent>
#include <QtGui/QClipboard>
#include <QtGui/QDesktopServices>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QGraphicsTextItem>
#include <QtWidgets/QMessageBox>
#include <QtGui/QImageWriter>
#include <QtWidgets/QErrorMessage>
#include <QtWidgets/QSplashScreen>
//#include <QtGui/QShortcut>
#include <QtGui/QPainter>
#include <QtCore/QTimer>
//#include <QtGamepad/QGamepad>
#include "../qt/QtUtils.h"
#ifdef _MSC_VER
#pragma warning(pop) // Re-enable warnings
#endif
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
#include "../utils/ContainerUtils.h"
#include "../utils/JSONParser.h"
#include "../utils/Base64.h"
#include "../utils/OpenSSL.h"
#include "../utils/ShouldCancelCallback.h"
#include "../utils/CryptoRNG.h"
#include "../utils/FileInStream.h"
#include "../utils/FileOutStream.h"
#include "../utils/BufferOutStream.h"
#include "../utils/IncludeXXHash.h"
#include "../utils/IndigoXMLDoc.h"
#include "../utils/RuntimeCheck.h"
#include "../networking/Networking.h"
#include "../networking/SMTPClient.h" // Just for testing
#include "../networking/TLSSocket.h" // Just for testing
#include "../networking/TLSSocketTests.h" // Just for testing
#include "../networking/HTTPClient.h" // Just for testing
#include "../networking/URL.h" // Just for testing
//#include "../networking/TLSSocketTests.h" // Just for testing

#include "../simpleraytracer/ray.h"
#include "../graphics/ImageMap.h"
#include "../graphics/FormatDecoderGLTF.h"
#include "../graphics/SRGBUtils.h"
//#include "../opengl/EnvMapProcessing.h"
#include "../dll/include/IndigoMesh.h"
#include "../dll/IndigoStringUtils.h"
#include "../dll/include/IndigoException.h"
#include "../indigo/TextureServer.h"
#include "../opengl/OpenGLShader.h"
#include "../graphics/PNGDecoder.h"
#include "../graphics/jpegdecoder.h"
#include "../opengl/GLMeshBuilding.h"
#include "../opengl/MeshPrimitiveBuilding.h"
#include <opengl/OpenGLMeshRenderData.h>
#if defined(_WIN32)
#include "../video/WMFVideoReader.h"
#endif
#include "../direct3d/Direct3DUtils.h"
#include <Escaping.h>
#include <clocale>
#include <tls.h>
#include "superluminal/PerformanceAPI.h"
#if BUGSPLAT_SUPPORT
#include <BugSplat.h>
#endif
#include "JoltUtils.h"
#include <Jolt/Physics/PhysicsSystem.h>

#ifdef _WIN32
#include <d3d11.h>
#include <d3d11_4.h>
#endif

#if defined(_WIN32) || defined(_WIN64)
#else
#include <signal.h>
#endif
#include <OpenGLEngineTests.h>
#include "Scripting.h"
#include "HoverCarPhysics.h"
#include "BikePhysics.h"
#include "BoatPhysics.h"

#include <tracy/Tracy.hpp>


// If we are building on Windows, and we are not in Release mode (e.g. BUILD_TESTS is enabled), then make sure the console window is shown.
#if defined(_WIN32) && defined(BUILD_TESTS)
#pragma comment(linker, "/SUBSYSTEM:CONSOLE")
#endif


static const int server_port = 7600;
static const int server_UDP_port = 7601;


static const double ground_quad_w = 2000.f; // TEMP was 1000, 2000 is for CV rendering
static const float ob_load_distance = 600.f;
// See also  // TEMP HACK: set a smaller max loading distance for CV features in ClientThread.cpp

std::vector<AvatarRef> test_avatars;
std::vector<double> test_avatar_phases;


static const Colour4f DEFAULT_OUTLINE_COLOUR   = Colour4f::fromHTMLHexString("0ff7fb"); // light blue
static const Colour4f PICKED_UP_OUTLINE_COLOUR = Colour4f::fromHTMLHexString("69fa2d"); // light green
static const Colour4f PARCEL_OUTLINE_COLOUR    = Colour4f::fromHTMLHexString("f09a13"); // orange

static const Colour3f axis_arrows_default_cols[]   = { Colour3f(0.6f,0.2f,0.2f), Colour3f(0.2f,0.6f,0.2f), Colour3f(0.2f,0.2f,0.6f) };
static const Colour3f axis_arrows_mouseover_cols[] = { Colour3f(1,0.45f,0.3f),   Colour3f(0.3f,1,0.3f),    Colour3f(0.3f,0.45f,1) };


MainWindow::MainWindow(const std::string& base_dir_path_, const std::string& appdata_path_, const ArgumentParser& args, QWidget* parent)
:	base_dir_path(base_dir_path_),
	appdata_path(appdata_path_),
	parsed_args(args),
	QMainWindow(parent),
	connection_state(ServerConnectionState_NotConnected),
	logged_in_user_id(UserID::invalidUserID()),
	logged_in_user_flags(0),
	shown_object_modification_error_msg(false),
	need_help_info_dock_widget_position(false),
	num_frames_since_fps_timer_reset(0),
	last_fps(0),
	voxel_edit_marker_in_engine(false),
	voxel_edit_face_marker_in_engine(false),
	selected_ob_picked_up(false),
	process_model_loaded_next(true),
	done_screenshot_setup(false),
	taking_map_screenshot(false),
	run_as_screenshot_slave(false),
	test_screenshot_taking(false),
	proximity_loader(/*load distance=*/ob_load_distance),
	load_distance(ob_load_distance),
	load_distance2(ob_load_distance*ob_load_distance),
	client_tls_config(NULL),
	last_foostep_side(0),
	last_timerEvent_CPU_work_elapsed(0),
	last_updateGL_time(0),
	last_animated_tex_time(0),
	last_model_and_tex_loading_time(0),
	grabbed_axis(-1),
	grabbed_angle(0),
#if defined(_WIN32)
	//interop_device_handle(NULL),
#endif
	force_new_undo_edit(false),
	log_window(NULL),
	model_and_texture_loader_task_manager("model and texture loader task manager"),
	task_manager(NULL), // Currently just used for LODGeneration::generateLODTexturesForMaterialsIfNotPresent().
	url_parcel_uid(-1),
	running_destructor(false),
	biome_manager(NULL),
	scratch_packet(SocketBufferOutStream::DontUseNetworkByteOrder),
	screenshot_width_px(1024),
	in_CEF_message_loop(false),
	should_close(false),
	frame_num(0),
	axis_and_rot_obs_enabled(false),
	closing(false),
	last_vehicle_renewal_msg_time(-1),
	main_timer_id(0),
	bump_allocator(/*size (B)=*/16 * 1024 * 1024),
	server_protocol_version(0)
{
	model_and_texture_loader_task_manager.setThreadPriorities(MyThread::Priority_Lowest);

	this->world_ob_pool_allocator = new glare::PoolAllocator(sizeof(WorldObject), 64);

	//QGamepadManager::instance();

	ui = new Ui::MainWindow();
	ui->setupUi(this);

	setAcceptDrops(true);

	update_ob_editor_transform_timer = new QTimer(this);
	update_ob_editor_transform_timer->setSingleShot(true);
	connect(update_ob_editor_transform_timer, SIGNAL(timeout()), this, SLOT(updateObjectEditorObTransformSlot()));

	// Add dock widgets to Window menu
	ui->menuWindow->addSeparator();
	ui->menuWindow->addAction(ui->editorDockWidget->toggleViewAction());
	ui->menuWindow->addAction(ui->materialBrowserDockWidget->toggleViewAction());
	ui->menuWindow->addAction(ui->environmentDockWidget->toggleViewAction());
	ui->menuWindow->addAction(ui->worldSettingsDockWidget->toggleViewAction());
	ui->menuWindow->addAction(ui->chatDockWidget->toggleViewAction());
	ui->menuWindow->addAction(ui->helpInfoDockWidget->toggleViewAction());
	//ui->menuWindow->addAction(ui->indigoViewDockWidget->toggleViewAction());
	ui->menuWindow->addAction(ui->diagnosticsDockWidget->toggleViewAction());

	settings = new QSettings("Glare Technologies", "Cyberspace");


	proximity_loader.callbacks = this;

	if(args.isArgPresent("--no_MDI"))
		ui->glWidget->allow_multi_draw_indirect = false;
	if(args.isArgPresent("--no_bindless"))
		ui->glWidget->allow_bindless_textures = false;

	ui->glWidget->setBaseDir(base_dir_path, /*print output=*/this, settings);
	ui->objectEditor->base_dir_path = base_dir_path;
	ui->objectEditor->settings = settings;

	// Add a spacer to right-align the UserDetailsWidget (see http://www.setnode.com/blog/right-aligning-a-button-in-a-qtoolbar/)
	QWidget* spacer = new QWidget();
	spacer->setMinimumWidth(60);
	spacer->setMaximumWidth(60);
	//spacer->setGeometry(QRect()
	//spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	ui->toolBar->addWidget(spacer);

	url_widget = new URLWidget(this);
	ui->toolBar->addWidget(url_widget);

	user_details = new UserDetailsWidget(this);
	ui->toolBar->addWidget(user_details);

	// Make it so the toolbar can't be hidden, as it's confusing for users when it disappears.
	ui->toolBar->toggleViewAction()->setEnabled(false);
	
	
	// Open Log File
	//const std::string logfile_path = FileUtils::join(this->appdata_path, "log.txt");
	//this->logfile.open(StringUtils::UTF8ToPlatformUnicodeEncoding(logfile_path).c_str(), std::ios_base::out);
	//if(!logfile.good())
	//	conPrint("WARNING: Failed to open log file at '" + logfile_path + "' for writing.");
	//logfile << "============================= Cyberspace Started =============================" << std::endl;
	//logfile << Clock::getAsciiTime() << std::endl;

	

	// Create the LogWindow early so we can log stuff to it.
	log_window = new LogWindow(this, settings);


	const float dist = (float)settings->value(MainOptionsDialog::objectLoadDistanceKey(), /*default val=*/500.0).toDouble();
	proximity_loader.setLoadDistance(dist);
	this->load_distance = dist;
	this->load_distance2 = dist*dist;

	// Since we use a perspective projection matrix with infinite far distance, use a large max drawing distance.
	ui->glWidget->max_draw_dist = 100000;

	// Restore main window geometry and state
	this->restoreGeometry(settings->value("mainwindow/geometry").toByteArray());

	if(!this->restoreState(settings->value("mainwindow/windowState").toByteArray()))
	{
		// State was not restored.  This will be the case for new Substrata installs.
		// Hide some dock widgets to provide a slightly simpler user experience.
		// Hide everything but chat and help-info dock widgets.
		this->ui->editorDockWidget->hide();
		this->ui->materialBrowserDockWidget->hide();
		this->ui->environmentDockWidget->hide();
		this->ui->worldSettingsWidget->hide();
		this->ui->diagnosticsDockWidget->hide();
	}

	ui->worldSettingsWidget->init(this);

	ui->objectEditor->init();

	ui->diagnosticsWidget->init(settings);
	connect(ui->diagnosticsWidget, SIGNAL(settingsChangedSignal()), this, SLOT(diagnosticsWidgetChanged()));
	connect(ui->diagnosticsWidget, SIGNAL(reloadTerrainSignal()), this, SLOT(diagnosticsReloadTerrain()));

	ui->environmentOptionsWidget->init(settings);
	connect(ui->environmentOptionsWidget, SIGNAL(settingChanged()), this, SLOT(environmentSettingChangedSlot()));

	connect(ui->chatPushButton, SIGNAL(clicked()), this, SLOT(sendChatMessageSlot()));
	connect(ui->chatMessageLineEdit, SIGNAL(returnPressed()), this, SLOT(sendChatMessageSlot()));
	connect(ui->glWidget, SIGNAL(mouseClicked(QMouseEvent*)), this, SLOT(glWidgetMouseClicked(QMouseEvent*)));
	connect(ui->glWidget, SIGNAL(mousePressed(QMouseEvent*)), this, SLOT(glWidgetMousePressed(QMouseEvent*)));
	connect(ui->glWidget, SIGNAL(mouseDoubleClickedSignal(QMouseEvent*)), this, SLOT(glWidgetMouseDoubleClicked(QMouseEvent*)));
	connect(ui->glWidget, SIGNAL(mouseMoved(QMouseEvent*)), this, SLOT(glWidgetMouseMoved(QMouseEvent*)));
	connect(ui->glWidget, SIGNAL(keyPressed(QKeyEvent*)), this, SLOT(glWidgetKeyPressed(QKeyEvent*)));
	connect(ui->glWidget, SIGNAL(keyReleased(QKeyEvent*)), this, SLOT(glWidgetkeyReleased(QKeyEvent*)));
	connect(ui->glWidget, SIGNAL(mouseWheelSignal(QWheelEvent*)), this, SLOT(glWidgetMouseWheelEvent(QWheelEvent*)));
	connect(ui->glWidget, SIGNAL(cameraUpdated()), this, SLOT(cameraUpdated()));
	connect(ui->glWidget, SIGNAL(playerMoveKeyPressed()), this, SLOT(playerMoveKeyPressed()));
	connect(ui->glWidget, SIGNAL(viewportResizedSignal(int, int)), this, SLOT(glWidgetViewportResized(int, int)));
	connect(ui->glWidget, SIGNAL(copyShortcutActivated()), this, SLOT(on_actionCopy_Object_triggered()));
	connect(ui->glWidget, SIGNAL(pasteShortcutActivated()), this, SLOT(on_actionPaste_Object_triggered()));
	connect(ui->objectEditor, SIGNAL(objectTransformChanged()), this, SLOT(objectTransformEditedSlot()));
	connect(ui->objectEditor, SIGNAL(objectChanged()), this, SLOT(objectEditedSlot()));
	connect(ui->objectEditor, SIGNAL(bakeObjectLightmap()), this, SLOT(bakeObjectLightmapSlot()));
	connect(ui->objectEditor, SIGNAL(bakeObjectLightmapHighQual()), this, SLOT(bakeObjectLightmapHighQualSlot()));
	connect(ui->objectEditor, SIGNAL(removeLightmapSignal()), this, SLOT(removeLightmapSignalSlot()));
	connect(ui->objectEditor, SIGNAL(posAndRot3DControlsToggled()), this, SLOT(posAndRot3DControlsToggledSlot()));
	connect(ui->parcelEditor, SIGNAL(parcelChanged()), this, SLOT(parcelEditedSlot()));
	connect(ui->worldSettingsWidget, SIGNAL(settingsAppliedSignal()), this, SLOT(worldSettingsAppliedSlot()));
	connect(user_details, SIGNAL(logInClicked()), this, SLOT(on_actionLogIn_triggered()));
	connect(user_details, SIGNAL(logOutClicked()), this, SLOT(on_actionLogOut_triggered()));
	connect(user_details, SIGNAL(signUpClicked()), this, SLOT(on_actionSignUp_triggered()));
	connect(url_widget, SIGNAL(URLChanged()), this, SLOT(URLChangedSlot()));

	std::string cache_dir = appdata_path;
	if(settings->value(MainOptionsDialog::useCustomCacheDirKey(), /*default value=*/false).toBool())
	{
		const std::string custom_cache_dir = QtUtils::toStdString(settings->value(MainOptionsDialog::customCacheDirKey()).toString());
		if(!custom_cache_dir.empty()) // Don't use custom cache dir if it's the empty string (e.g. not set to something valid)
			cache_dir = custom_cache_dir;
	}

	this->resources_dir = cache_dir + "/resources";
	FileUtils::createDirIfDoesNotExist(this->resources_dir);

	print("resources_dir: " + resources_dir);
	resource_manager = new ResourceManager(this->resources_dir);

	
	// The user may have changed the resources dir (by changing the custom cache directory) since last time we ran.
	// In this case, we want to check if each resources is actually present on disk in the current resources dir.
	const std::string last_resources_dir = QtUtils::toStdString(settings->value("last_resources_dir").toString());
	const bool resources_dir_changed = last_resources_dir != this->resources_dir;
	settings->setValue("last_resources_dir", QtUtils::toQString(this->resources_dir));


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

	save_resources_db_thread_manager.addThread(new SaveResourcesDBThread(resource_manager, resources_db_path));

	cam_controller.setMouseSensitivity(-1.0);

#if !defined(_WIN32)
	// On Windows, Windows will execute substrata.exe with the -linku argument, so we don't need this technique.
	QDesktopServices::setUrlHandler("sub", /*receiver=*/this, /*method=*/"handleURL");
#endif

	try
	{
		uint64 rnd_buf;
		CryptoRNG::getRandomBytes((uint8*)&rnd_buf, sizeof(uint64));
		this->rng = PCG32(1, rnd_buf);
	}
	catch(glare::Exception& e)
	{
		conPrint(e.what());
	}

	biome_manager = new BiomeManager();

	for(int i=0; i<NUM_AXIS_ARROWS; ++i)
		axis_arrow_segments[i] = LineSegment4f(Vec4f(0, 0, 0, 1), Vec4f(1, 0, 0, 1));
}


static std::string computeWindowTitle()
{
	return "Substrata v" + ::cyberspace_version;
}


static const char* default_help_info_message = "Use the W/A/S/D keys and arrow keys to move and look around.\n"
	"Click and drag the mouse on the 3D view to look around.\n"
	"Space key: jump\n"
	"Double-click an object to select it.";


void MainWindow::startMainTimer()
{
	// Stop previous timer, if it exists.
	if(main_timer_id != 0)
		killTimer(main_timer_id);

	int use_interval = 1; // in milliseconds
	const bool limit_FPS = settings->value(MainOptionsDialog::limitFPSKey(), /*default val=*/false).toBool();
	if(limit_FPS)
	{
		const int max_FPS = myClamp(settings->value(MainOptionsDialog::FPSLimitKey(), /*default val=*/60).toInt(), 15, 1000);
		use_interval = (int)(1000.0 / max_FPS);
	}

#ifdef OSX
	// Set to at least 17ms due to this issue on Mac OS: https://bugreports.qt.io/browse/QTBUG-60346
	use_interval = myMax(use_interval, 17); 
#endif

	main_timer_id = startTimer(use_interval);
}


void MainWindow::initialise()
{
	setWindowTitle(QtUtils::toQString(computeWindowTitle()));

	ui->materialBrowserDockWidgetContents->init(this, this->base_dir_path, this->appdata_path, this->texture_server, /*print output=*/this);
	connect(ui->materialBrowserDockWidgetContents, SIGNAL(materialSelected(const std::string&)), this, SLOT(materialSelectedInBrowser(const std::string&)));

	ui->objectEditor->setControlsEnabled(false);
	ui->parcelEditor->hide();

	startMainTimer();
	
	ui->infoDockWidget->setTitleBarWidget(new QWidget());
	ui->infoDockWidget->hide();

	setUIForSelectedObject();

	// Update help text
	this->ui->helpInfoLabel->setText(default_help_info_message);

	if(!settings->contains("mainwindow/geometry"))
		need_help_info_dock_widget_position = true;

	connect(this->ui->indigoViewDockWidget, SIGNAL(visibilityChanged(bool)), this, SLOT(onIndigoViewDockWidgetVisibilityChanged(bool)));

#if INDIGO_SUPPORT
#else
	this->ui->indigoViewDockWidget->hide();
#endif

	lightmap_flag_timer = new QTimer(this);
	lightmap_flag_timer->setSingleShot(true);
	connect(lightmap_flag_timer, SIGNAL(timeout()), this, SLOT(sendLightmapNeededFlagsSlot()));


	// Create and init TLS client config
	client_tls_config = tls_config_new();
	if(!client_tls_config)
		throw glare::Exception("Failed to initialise TLS (tls_config_new failed)");
	tls_config_insecure_noverifycert(client_tls_config); // TODO: Fix this, check cert etc..
	tls_config_insecure_noverifyname(client_tls_config);


#ifdef _WIN32
	// Create a GPU device.  Needed to get hardware accelerated video decoding.
	Direct3DUtils::createGPUDeviceAndMFDeviceManager(d3d_device, device_manager);
#endif //_WIN32

	try
	{
		audio_engine.init();

		// Load a wind sound and create a non-spatial audio source, to use for a rushing effect when the player moves fast.
		// TODO: Load wind noise off main thread. (Take about 11ms to load on my 5950x).
		//Timer timer;
		glare::SoundFileRef sound = audio_engine.getOrLoadSoundFile(base_dir_path + "/resources/sounds/wind_noise_48000_hz_mono.mp3");
		//conPrint("Loading wind sound took " + timer.elapsedString());

		wind_audio_source = new glare::AudioSource();
		wind_audio_source->type = glare::AudioSource::SourceType_Looping;
		wind_audio_source->spatial_type = glare::AudioSource::SourceSpatialType_NonSpatial;
		wind_audio_source->shared_buffer = sound->buf;
		wind_audio_source->sampling_rate = sound->sample_rate;
		wind_audio_source->volume = 0;

		audio_engine.addSource(wind_audio_source);
	}
	catch(glare::Exception& e) 
	{
		logMessage("Audio engine could not be initialised: " + e.what());
	}

	//for(int i=0; i<4; ++i)
	//	this->footstep_sources.push_back(audio_engine.addSourceFromSoundFile("D:\\audio\\sound_effects\\footstep_mono" + toString(i) + ".wav"));


	if(run_as_screenshot_slave)
	{
		conPrint("Waiting for screenshot command connection...");
		MySocketRef listener = new MySocket();
		listener->bindAndListen(34534);

		screenshot_command_socket = listener->acceptConnection(); // Blocks
		screenshot_command_socket->setUseNetworkByteOrder(false);
		conPrint("Got screenshot command connection.");
	}
}


void MainWindow::afterGLInitInitialise()
{
	if(settings->value("mainwindow/showParcels", QVariant(false)).toBool())
	{
		ui->actionShow_Parcels->setChecked(true);
		addParcelObjects();
	}

	if(settings->value("mainwindow/flyMode", QVariant(false)).toBool())
	{
		ui->actionFly_Mode->setChecked(true);
		this->player_physics.setFlyModeEnabled(true);
	}

	this->cam_controller.setThirdPersonEnabled(settings->value("mainwindow/thirdPersonCamera", /*default val=*/false).toBool());
	ui->actionThird_Person_Camera->setChecked (settings->value("mainwindow/thirdPersonCamera", /*default val=*/false).toBool());

	//OpenGLEngineTests::doTextureLoadingTests(*ui->glWidget->opengl_engine);

#ifdef _WIN32
	// Prepare for D3D interoperability with opengl
	//wgl_funcs.init();
	//
	//interop_device_handle = wgl_funcs.wglDXOpenDeviceNV(d3d_device.ptr); // prepare for interoperability with opengl
	//if(interop_device_handle == 0)
	//	throw glare::Exception("wglDXOpenDeviceNV failed.");
#endif

	const auto device_pixel_ratio = ui->glWidget->devicePixelRatio(); // For retina screens this is 2, meaning the gl viewport width is in physical pixels, which have twice the density of qt pixel coordinates.

	gl_ui = new GLUI();
	gl_ui->create(ui->glWidget->opengl_engine, (float)device_pixel_ratio, /*text_renderer=*/this);

	gesture_ui.create(ui->glWidget->opengl_engine, /*main_window_=*/this, gl_ui);

	ob_info_ui.create(ui->glWidget->opengl_engine, /*main_window_=*/this, gl_ui);

	misc_info_ui.create(ui->glWidget->opengl_engine, /*main_window_=*/this, gl_ui);
	
	hud_ui.create(ui->glWidget->opengl_engine, /*main_window_=*/this, gl_ui);

	minimap.create(ui->glWidget->opengl_engine, /*main_window_=*/this, gl_ui);


	// Do auto-setting of graphics options, if they have not been set.  Otherwise apply MSAA setting.
	if(!settings->contains(MainOptionsDialog::MSAAKey())) // If the MSAA key has not been set:
	{
		const bool is_retina = device_pixel_ratio > 1;

		// We won't use MSAA by default in two cases:
		// 1) Intel drivers - which implies an integrated Intel GPU which is probably not very powerful.
		// 2) A retina monitor - the majority of which correspond to Mac laptops, which will look hopefully look alright without MSAA, and run slowly with MSAA.
		const bool is_Intel = ui->glWidget->opengl_engine->openglDriverVendorIsIntel();
		const bool no_MSAA = is_Intel || is_retina;
		const bool MSAA = !no_MSAA;
		//ui->glWidget->opengl_engine->setMSAAEnabled(MSAA);

		settings->setValue(MainOptionsDialog::MSAAKey(), MSAA); // Save MSAA setting

		settings->setValue(MainOptionsDialog::BloomKey(), MSAA); // Use the same decision for bloom

		logMessage("Auto-setting MSAA: is_retina: " + boolToString(is_retina) + ", is_Intel: " + boolToString(is_Intel) + ", MSAA: " + boolToString(MSAA));
	}
	else
	{
		// Else MSAA setting is present.
		const bool MSAA = settings->value(MainOptionsDialog::MSAAKey(), /*default=*/true).toBool();
		logMessage("Setting MSAA to " + boolToString(MSAA));
		//ui->glWidget->opengl_engine->setMSAAEnabled(MSAA);
	}

	terrain_decal_manager = new TerrainDecalManager(this->base_dir_path, opengl_engine.ptr());
	particle_manager = new ParticleManager(this->base_dir_path, opengl_engine.ptr(), physics_world.ptr(), terrain_decal_manager.ptr());
}


MainWindow::~MainWindow()
{
	// Kill various threads before we start destroying members of MainWindow they may depend on.
	// Need to kill DownloadResourcesThread before we destroy the download_queue, which it references, for example.
	if(task_manager)
	{
		delete task_manager;
		task_manager = NULL;
	}

	client_thread_manager.killThreadsBlocking();
	client_udp_handler_thread_manager.killThreadsBlocking();
	mic_read_thread_manager.killThreadsBlocking();
	resource_upload_thread_manager.killThreadsBlocking();
	resource_download_thread_manager.killThreadsBlocking();
	net_resource_download_thread_manager.killThreadsBlocking();
	save_resources_db_thread_manager.killThreadsBlocking();

	model_and_texture_loader_task_manager.cancelAndWaitForTasksToComplete();
	

	if(wind_audio_source.nonNull())
		audio_engine.removeSource(wind_audio_source);
	wind_audio_source = NULL;


	player_physics.shutdown();
	//car_physics.shutdown();


	running_destructor = true; // Set this to not append log messages during destruction, causes assert failure in Qt.

	

#if !defined(_WIN32)
	QDesktopServices::unsetUrlHandler("sub"); // Remove 'this' as an URL handler.
#endif

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



	//ui->glWidget->makeCurrent(); // This crashes on Mac

	// Kill various threads before we start destroying members of MainWindow they may depend on.
	save_resources_db_thread_manager.killThreadsBlocking();


	if(this->client_tls_config)
		tls_config_free(this->client_tls_config);


#ifdef _WIN32
	//if(this->interop_device_handle)
	//{
	//	const BOOL res = wgl_funcs.wglDXCloseDeviceNV(this->interop_device_handle); // close interoperability with opengl
	//	assertOrDeclareUsed(res);
	//}
#endif

	// Free direct3d device and device manager
#ifdef _WIN32
	device_manager.release();
	d3d_device.release();
#endif

	delete ui;
}


void MainWindow::closeEvent(QCloseEvent* event)
{
	// Don't try and close everything down while we're in the message loop in the chromium embedded framework (CEF) code,
	// because that will try and close CEF down, which leads to problems.
	// Instead set a flag (should_close), and close the mainwindow when we're back in the main message loop and not the CEF loop.
	if(in_CEF_message_loop)
	{
		should_close = true;
		event->ignore();
		return;
	}

	ui->glWidget->makeCurrent();

	// Save main window geometry and state.  See http://doc.qt.io/archives/qt-4.8/qmainwindow.html#saveState
	settings->setValue("mainwindow/geometry", saveGeometry());
	settings->setValue("mainwindow/windowState", saveState());

	// Destroy/close all OpenGL stuff, because once glWidget is destroyed, the OpenGL context is destroyed, so we can't free stuff properly.

	model_loaded_messages_to_process.clear();

	load_item_queue.clear();

	model_and_texture_loader_task_manager.removeQueuedTasks();
	model_and_texture_loader_task_manager.waitForTasksToComplete();


	texture_loaded_messages_to_process.clear();

	cur_loading_voxel_ob = NULL;


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
		test_avatars[i]->graphics.destroy(*ui->glWidget->opengl_engine); // Remove any OpenGL object for it


	disconnectFromServerAndClearAllObjects();

	if(biome_manager) delete biome_manager;

	misc_info_ui.destroy();

	ob_info_ui.destroy();

	gesture_ui.destroy();
	
	hud_ui.destroy();

	minimap.destroy();

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


	mesh_manager.clear(); // Mesh manager has references to cached/unused meshes, so need to zero out the references before we shut down the OpenGL engine.

	this->opengl_engine = NULL;
	ui->glWidget->shutdown(); // Shuts down OpenGL Engine.



	if(log_window) log_window->close();

	in_CEF_message_loop = true;
	CEF::shutdownCEF();
	in_CEF_message_loop = false;

	this->closing = true;
	QMainWindow::closeEvent(event);
}


void MainWindow::onIndigoViewDockWidgetVisibilityChanged(bool visible)
{
	conPrint("--------------------------------------- MainWindow::onIndigoViewDockWidgetVisibilityChanged (visible: " + boolToString(visible) + ") --------------");
	if(visible)
	{
		this->ui->indigoView->initialise(this->base_dir_path);

		if(this->world_state.nonNull())
		{
			Lock lock(this->world_state->mutex);
			this->ui->indigoView->addExistingObjects(*this->world_state, *this->resource_manager);
		}
	}
	else
	{
		this->ui->indigoView->shutdown();
	}
}


/*static const Matrix4f rotateThenTranslateMatrix(const Vec3d& translation, const Vec3f& rotation)
{
	Matrix4f m;
	const float rot_len2 = rotation.length2();
	if(rot_len2 < 1.0e-20f)
		m.setToIdentity();
	else
	{
		const float rot_len = std::sqrt(rot_len2);
		m.setToRotationMatrix(rotation.toVec4fVector() / rot_len, rot_len);
	}
	m.setColumn(3, Vec4f((float)translation.x, (float)translation.y, (float)translation.z, 1.f));
	return m;
}*/


// Some resources, such as MP4 videos, shouldn't be downloaded fully before displaying, but instead can be streamed and displayed when only part of the stream is downloaded.
/*static bool shouldStreamResourceViaHTTP(const std::string& url)
{
	// On Windows, use WMF's http reading to stream videos (until we get the custom byte stream working)
#ifdef _WIN32
	return ::hasExtensionStringView(url, "mp4");
#else
	return false; // On Mac/linux, we'll use the ResourceIODeviceWrapper for QMediaPlayer, so we don't need to use http.
#endif
}*/


void MainWindow::startDownloadingResource(const std::string& url, const Vec4f& centroid_ws, float aabb_ws_longest_len, DownloadingResourceInfo& resource_info)
{
	//conPrint("-------------------MainWindow::startDownloadingResource()-------------------\nURL: " + url);
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
		this->URL_to_downloading_info[url] = resource_info;

		const URL parsed_url = URL::parseURL(url);

		if(parsed_url.scheme == "http" || parsed_url.scheme == "https")
		{
			this->net_resource_download_thread_manager.enqueueMessage(new DownloadResourceMessage(url));
			num_net_resources_downloading++;
		}
		else
		{
			DownloadQueueItem item;
			item.pos = centroid_ws;
			item.size_factor = DownloadQueueItem::sizeFactorForAABBWS(aabb_ws_longest_len);
			item.URL = url;
			this->download_queue.enqueueItem(item);
		}
	}
	catch(glare::Exception& e)
	{
		conPrint("Failed to parse URL '" + url + "': " + e.what());
	}
}


bool MainWindow::checkAddTextureToProcessingSet(const std::string& path)
{
	auto res = textures_processing.insert(path);
	return res.second; // Was texture inserted? (will be false if already present in set)
}


bool MainWindow::checkAddModelToProcessingSet(const std::string& url, bool dynamic_physics_shape)
{
	ModelProcessingKey key(url, dynamic_physics_shape);
	auto res = models_processing.insert(key);
	return res.second; // Was model inserted? (will be false if already present in set)
}


bool MainWindow::checkAddAudioToProcessingSet(const std::string& url)
{
	auto res = audio_processing.insert(url);
	return res.second; // Was audio inserted? (will be false if already present in set)
}


bool MainWindow::checkAddScriptToProcessingSet(const std::string& script_content) // returns true if was not in processed set (and hence this call added it), false if it was.
{
	auto res = script_content_processing.insert(script_content);
	return res.second; // Was inserted? (will be false if already present in set)
}


//bool MainWindow::isAudioProcessed(const std::string& url) const
//{
//	Lock lock(audio_processed_mutex);
//	return audio_processed.count(url) > 0;
//}


// Is non-empty and has a supported image extension.
// Mp4 files will be handled with other code, not loaded in a LoadTextureTask, so we want to return false for mp4 extensions.
static inline bool isValidImageTextureURL(const std::string& URL)
{
	return !URL.empty() && ImageDecoding::hasSupportedImageExtension(URL);
}


static inline bool isValidLightMapURL(const std::string& URL)
{
	if(URL.empty())
		return false;
	else
	{
#ifdef OSX
		// KTX and KTX2 files used for lightmaps use bc6h compression, which mac doesn't support.  So don't try and load them on Mac.
		const string_view extension = getExtensionStringView(URL);
		return ImageDecoding::hasSupportedImageExtension(URL) && (extension != "ktx") && (extension != "ktx2");
#else
		return ImageDecoding::hasSupportedImageExtension(URL);
#endif
	}
}


void MainWindow::startLoadingTexture(const std::string& tex_url, const Vec4f& centroid_ws, float aabb_ws_longest_len, float max_task_dist, float importance_factor, 
	bool use_sRGB, bool allow_compression, bool is_terrain_map, bool is_minimap_tile)
{
	if(isValidImageTextureURL(tex_url))
	{
		ResourceRef resource = resource_manager->getExistingResourceForURL(tex_url);
		if(resource.nonNull() && (resource->getState() == Resource::State_Present)) // If the texture is present on disk:
		{
			const std::string tex_path = resource_manager->getLocalAbsPathForResource(*resource);

			if(!ui->glWidget->opengl_engine->isOpenGLTextureInsertedForKey(OpenGLTextureKey(texture_server->keyForPath(tex_path)))) // If texture is not uploaded to GPU already:
			{
				const bool just_added = checkAddTextureToProcessingSet(tex_path); // If not being loaded already:
				if(just_added)
				{
					Reference<LoadTextureTask> task = new LoadTextureTask(ui->glWidget->opengl_engine, this->texture_server, &this->msg_queue, tex_path, use_sRGB, allow_compression, is_terrain_map, is_minimap_tile);

					load_item_queue.enqueueItem(
						centroid_ws, 
						aabb_ws_longest_len, 
						task,
						max_task_dist, 
						importance_factor
					);
				}
			}
		}
	}
}


void MainWindow::startLoadingTextureForObject(const Vec4f& centroid_ws, float aabb_ws_longest_len, float max_dist_for_ob_lod_level, float importance_factor, const WorldMaterial& world_mat, int ob_lod_level, 
	const std::string& texture_url, bool tex_has_alpha, bool use_sRGB, bool allow_compression)
{
	const std::string lod_tex_url = world_mat.getLODTextureURLForLevel(texture_url, ob_lod_level, tex_has_alpha);

	startLoadingTexture(lod_tex_url,centroid_ws, aabb_ws_longest_len, max_dist_for_ob_lod_level, importance_factor, use_sRGB, allow_compression, /*is_terrain_map=*/false, /*is_minimap_tile=*/false);
}


void MainWindow::startLoadingTexturesForObject(const WorldObject& ob, int ob_lod_level, float max_dist_for_ob_lod_level)
{
	// Process model materials - start loading any textures that are present on disk, and not already loaded and processed:
	for(size_t i=0; i<ob.materials.size(); ++i)
	{
		startLoadingTextureForObject(ob.getCentroidWS(), ob.getAABBWSLongestLength(), max_dist_for_ob_lod_level, /*importance factor=*/1.f, *ob.materials[i], ob_lod_level, ob.materials[i]->colour_texture_url, ob.materials[i]->colourTexHasAlpha(), /*use_sRGB=*/true, /*allow_compression=*/true);
		startLoadingTextureForObject(ob.getCentroidWS(), ob.getAABBWSLongestLength(), max_dist_for_ob_lod_level, /*importance factor=*/1.f, *ob.materials[i], ob_lod_level, ob.materials[i]->emission_texture_url, /*has_alpha=*/false, /*use_sRGB=*/true, /*allow_compression=*/true);
		startLoadingTextureForObject(ob.getCentroidWS(), ob.getAABBWSLongestLength(), max_dist_for_ob_lod_level, /*importance factor=*/1.f, *ob.materials[i], ob_lod_level, ob.materials[i]->roughness.texture_url, /*has_alpha=*/false, /*use_sRGB=*/false, /*allow_compression=*/true);
		startLoadingTextureForObject(ob.getCentroidWS(), ob.getAABBWSLongestLength(), max_dist_for_ob_lod_level, /*importance factor=*/1.f, *ob.materials[i], ob_lod_level, ob.materials[i]->normal_map_url, /*has_alpha=*/false, /*use_sRGB=*/false, /*allow_compression=*/false);
	}

	// Start loading lightmap
	if(isValidLightMapURL(ob.lightmap_url))
	{
		const std::string lod_tex_url = WorldObject::getLODLightmapURL(ob.lightmap_url, ob_lod_level);

		ResourceRef resource = resource_manager->getExistingResourceForURL(lod_tex_url);
		if(resource.nonNull() && (resource->getState() == Resource::State_Present)) // If the texture is present on disk:
		{
			const std::string tex_path = resource_manager->getLocalAbsPathForResource(*resource);

			if(!ui->glWidget->opengl_engine->isOpenGLTextureInsertedForKey(OpenGLTextureKey(texture_server->keyForPath(tex_path)))) // If texture is not uploaded to GPU already:
			{
				const bool just_added = checkAddTextureToProcessingSet(tex_path); // If not being loaded already:
				if(just_added)
					load_item_queue.enqueueItem(ob, new LoadTextureTask(ui->glWidget->opengl_engine, this->texture_server, &this->msg_queue, tex_path, 
						/*use_sRGB=*/true, /*allow compression=*/true, /*is_terrain_map=*/false, /*is_minimap_tile=*/false), max_dist_for_ob_lod_level);
			}
		}
	}
}


void MainWindow::startLoadingTexturesForAvatar(const Avatar& av, int ob_lod_level, float max_dist_for_ob_lod_level, bool our_avatar)
{
	// Prioritise laoding our avatar first.
	const float our_avatar_importance_factor = our_avatar ? 1.0e4f : 1.f;

	// Process model materials - start loading any textures that are present on disk, and not already loaded and processed:
	for(size_t i=0; i<av.avatar_settings.materials.size(); ++i)
	{
		startLoadingTextureForObject(av.pos.toVec4fPoint(), /*aabb_ws_longest_len=*/1.8f, max_dist_for_ob_lod_level, our_avatar_importance_factor, *av.avatar_settings.materials[i], ob_lod_level, av.avatar_settings.materials[i]->colour_texture_url, av.avatar_settings.materials[i]->colourTexHasAlpha(), /*use_sRGB=*/true, /*allow compression=*/true);
		startLoadingTextureForObject(av.pos.toVec4fPoint(), /*aabb_ws_longest_len=*/1.8f, max_dist_for_ob_lod_level, our_avatar_importance_factor, *av.avatar_settings.materials[i], ob_lod_level, av.avatar_settings.materials[i]->emission_texture_url, /*has_alpha=*/false, /*use_sRGB=*/true, /*allow compression=*/true);
		startLoadingTextureForObject(av.pos.toVec4fPoint(), /*aabb_ws_longest_len=*/1.8f, max_dist_for_ob_lod_level, our_avatar_importance_factor, *av.avatar_settings.materials[i], ob_lod_level, av.avatar_settings.materials[i]->roughness.texture_url, /*has_alpha=*/false, /*use_sRGB=*/false, /*allow compression=*/true);
		startLoadingTextureForObject(av.pos.toVec4fPoint(), /*aabb_ws_longest_len=*/1.8f, max_dist_for_ob_lod_level, our_avatar_importance_factor, *av.avatar_settings.materials[i], ob_lod_level, av.avatar_settings.materials[i]->normal_map_url, /*has_alpha=*/false, /*use_sRGB=*/false, /*allow compression=*/false);
	}
}


void MainWindow::removeAndDeleteGLObjectsForOb(WorldObject& ob)
{
	if(ob.opengl_engine_ob.nonNull())
		ui->glWidget->opengl_engine->removeObject(ob.opengl_engine_ob);

	if(ob.opengl_light.nonNull())
		ui->glWidget->opengl_engine->removeLight(ob.opengl_light);

	ob.opengl_engine_ob = NULL;

	ob.mesh_manager_data = NULL;

	ob.loaded_model_lod_level = -10;
	ob.using_placeholder_model = false;
}


void MainWindow::removeAndDeleteGLAndPhysicsObjectsForOb(WorldObject& ob)
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


void MainWindow::removeAndDeleteGLObjectForAvatar(Avatar& av)
{
	av.graphics.destroy(*ui->glWidget->opengl_engine);

	av.mesh_data = NULL;
}


// Adds a temporary placeholder cube model for the object.
// Removes any existing model for the object.
void MainWindow::addPlaceholderObjectsForOb(WorldObject& ob_)
{
	WorldObject* ob = &ob_;

	// Remove any existing OpenGL and physics model
	if(ob->opengl_engine_ob.nonNull())
		ui->glWidget->opengl_engine->removeObject(ob->opengl_engine_ob);

	if(ob->opengl_light.nonNull())
		ui->glWidget->opengl_engine->removeLight(ob->opengl_light);
	
	destroyVehiclePhysicsControllingObject(ob); // Destroy any vehicle controller controlling this object, as vehicle controllers have pointers to physics bodies, which we can't leave dangling.

	if(ob->physics_object.nonNull())
	{
		physics_world->removeObject(ob->physics_object);
		ob->physics_object = NULL;
	}

	GLObjectRef cube_gl_ob = ui->glWidget->opengl_engine->makeAABBObject(/*min=*/ob->getAABBOS().min_, /*max=*/ob->getAABBOS().max_, Colour4f(0.6f, 0.2f, 0.2, 0.5f));
	cube_gl_ob->ob_to_world_matrix = ob->obToWorldMatrix() * OpenGLEngine::AABBObjectTransform(/*min=*/ob->getAABBOS().min_, /*max=*/ob->getAABBOS().max_);

	ob->opengl_engine_ob = cube_gl_ob;
	ui->glWidget->opengl_engine->addObject(cube_gl_ob);

	// Make physics object
	PhysicsObjectRef physics_ob = new PhysicsObject(/*collidable=*/false); // Make non-collidable, so avatar doesn't get stuck in large placeholder objects.
	physics_ob->shape = this->unit_cube_shape;
	physics_ob->pos = ob->pos.toVec4fPoint();
	physics_ob->rot = Quatf::fromAxisAndAngle(normalise(ob->axis), ob->angle);
	physics_ob->scale = useScaleForWorldOb(ob->scale);

	assert(ob->physics_object.isNull());
	ob->physics_object = physics_ob;
	physics_ob->userdata = ob;
	physics_ob->userdata_type = 0;
	physics_ob->ob_uid = ob->uid;
	physics_world->addObject(physics_ob);

	ob->using_placeholder_model = true;
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


// For every resource that the object uses (model, textures etc..), if the resource is not present locally, start downloading it, if we are not already downloading it.
void MainWindow::startDownloadingResourcesForObject(WorldObject* ob, int ob_lod_level)
{
	WorldObject::GetDependencyOptions options;
	std::set<DependencyURL> dependency_URLs;
	ob->getDependencyURLSet(ob_lod_level, options, dependency_URLs);
	for(auto it = dependency_URLs.begin(); it != dependency_URLs.end(); ++it)
	{
		const DependencyURL& url_info = *it;
		const std::string& url = url_info.URL;
		
		// If resources are streamable, don't download them.
		//const bool stream = shouldStreamResourceViaHTTP(url);

		const bool has_audio_extension = FileTypes::hasAudioFileExtension(url);
		const bool has_video_extension = FileTypes::hasSupportedVideoFileExtension(url);

		if(has_audio_extension || has_video_extension || ImageDecoding::hasSupportedImageExtension(url) || ModelLoading::hasSupportedModelExtension(url))
		{
			// Only download mp4s and audio files if the camera is near them in the world.
			bool in_range = true;
			if(has_video_extension)
			{
				const double ob_dist = ob->pos.getDist(cam_controller.getPosition());
				const double max_play_dist = AnimatedTexData::maxVidPlayDist();
				in_range = ob_dist < max_play_dist;
			}
			else if(has_audio_extension)
			{
				const double ob_dist = ob->pos.getDist(cam_controller.getPosition());
				in_range = ob_dist < maxAudioDistForSourceVolFactor(ob->audio_volume);
			}

			if(in_range && !resource_manager->isFileForURLPresent(url))// && !stream)
			{
				DownloadingResourceInfo info;
				info.use_sRGB = url_info.use_sRGB;
				info.build_dynamic_physics_ob = ob->isDynamic();
				info.pos = ob->pos;
				info.size_factor = LoadItemQueueItem::sizeFactorForAABBWS(ob->getAABBWSLongestLength(), /*importance_factor=*/1.f);

				startDownloadingResource(url, ob->getCentroidWS(), ob->getAABBWSLongestLength(), info);
			}
		}
	}
}


void MainWindow::startDownloadingResourcesForAvatar(Avatar* ob, int ob_lod_level, bool our_avatar)
{
	std::set<DependencyURL> dependency_URLs;
	ob->getDependencyURLSet(ob_lod_level, dependency_URLs);
	for(auto it = dependency_URLs.begin(); it != dependency_URLs.end(); ++it)
	{
		const DependencyURL& url_info = *it;
		const std::string& url = url_info.URL;

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
				info.use_sRGB = url_info.use_sRGB;
				info.build_dynamic_physics_ob = false;
				info.pos = ob->pos;
				info.size_factor = LoadItemQueueItem::sizeFactorForAABBWS(/*aabb_ws_longest_len=*/1.8f, our_avatar_importance_factor);

				startDownloadingResource(url, ob->pos.toVec4fPoint(), /*aabb_ws_longest_len=*/1.8f, info);
			}
		}
	}
}


// For when the desired texture LOD is not loaded, pick another texture LOD that is loaded (if it exists).
// Prefer lower LOD levels (more detail).
static Reference<OpenGLTexture> getBestTextureLOD(const WorldMaterial& world_mat, const std::string& base_tex_path, bool tex_has_alpha, bool use_sRGB, OpenGLEngine& opengl_engine)
{
	for(int lvl=-1; lvl<=2; ++lvl)
	{
		const std::string tex_lod_path = world_mat.getLODTextureURLForLevel(base_tex_path, lvl, tex_has_alpha);
		Reference<OpenGLTexture> tex = opengl_engine.getTextureIfLoaded(OpenGLTextureKey(tex_lod_path));
		if(tex.nonNull())
			return tex;
	}

	return Reference<OpenGLTexture>();
}


static Reference<OpenGLTexture> getBestLightmapLOD(const std::string& base_lightmap_path, OpenGLEngine& opengl_engine)
{
	for(int lvl=0; lvl<=2; ++lvl)
	{
		const std::string tex_lod_path = WorldObject::getLODLightmapURL(base_lightmap_path, lvl);
		Reference<OpenGLTexture> tex = opengl_engine.getTextureIfLoaded(OpenGLTextureKey(tex_lod_path));
		if(tex.nonNull())
			return tex;
	}

	return Reference<OpenGLTexture>();
}


// If not a mp4 texture - we won't have LOD levels for mp4 textures, just keep the texture vid playback writes to.
static inline bool isNonEmptyAndNotMp4(const std::string& path)
{
	return !path.empty() && !::hasExtensionStringView(path, "mp4");
}


// Update textures to correct LOD-level textures.
// Try and use the texture with the target LOD level first (given by e.g. opengl_mat.tex_path).
// If that texture is not currently loaded into the OpenGL Engine, then use another texture LOD that is loaded, as chosen in getBestTextureLOD().
static void assignedLoadedOpenGLTexturesToMats(WorldObject* ob, OpenGLEngine& opengl_engine, ResourceManager& resource_manager)
{
	for(size_t z=0; z<ob->opengl_engine_ob->materials.size(); ++z)
	{
		OpenGLMaterial& opengl_mat = ob->opengl_engine_ob->materials[z];
		const WorldMaterial* world_mat = (z < ob->materials.size()) ? ob->materials[z].ptr() : NULL;

		if(isNonEmptyAndNotMp4(opengl_mat.tex_path)) // We won't have LOD levels for mp4 textures, just keep the texture vid playback writes to.
		{
			try
			{
				const OpenGLTexture* prev_tex = opengl_mat.albedo_texture.ptr();
				
				opengl_mat.albedo_texture = opengl_engine.getTextureIfLoaded(OpenGLTextureKey(opengl_mat.tex_path));
				if(opengl_mat.albedo_texture.isNull() && world_mat) // If this texture is not loaded into the OpenGL engine, and the corresponding world object material is valid:
					opengl_mat.albedo_texture = getBestTextureLOD(*world_mat, resource_manager.pathForURL(world_mat->colour_texture_url), world_mat->colourTexHasAlpha(), /*use_sRGB=*/true, opengl_engine); // Try and use a different LOD level of the texture, that is actually loaded.
				
				if(opengl_mat.albedo_texture.ptr() != prev_tex)
					opengl_engine.materialTextureChanged(*ob->opengl_engine_ob, opengl_mat);
			}
			catch(glare::Exception& e)
			{
				conPrint("error loading texture: " + e.what());
			}
		}

		if(isNonEmptyAndNotMp4(opengl_mat.emission_tex_path))
		{
			try
			{
				const OpenGLTexture* prev_tex = opengl_mat.emission_texture.ptr();

				opengl_mat.emission_texture = opengl_engine.getTextureIfLoaded(OpenGLTextureKey(opengl_mat.emission_tex_path));
				if(opengl_mat.emission_texture.isNull() && world_mat) // If this texture is not loaded into the OpenGL engine:
					opengl_mat.emission_texture = getBestTextureLOD(*world_mat, resource_manager.pathForURL(world_mat->emission_texture_url), /*tex_has_alpha=*/false, /*use_sRGB=*/true, opengl_engine);

				if(opengl_mat.emission_texture.ptr() != prev_tex)
					opengl_engine.materialTextureChanged(*ob->opengl_engine_ob, opengl_mat);
			}
			catch(glare::Exception& e)
			{
				conPrint("error loading texture: " + e.what());
			}
		}

		if(isNonEmptyAndNotMp4(opengl_mat.metallic_roughness_tex_path))
		{
			try
			{
				const OpenGLTexture* prev_tex = opengl_mat.metallic_roughness_texture.ptr();

				opengl_mat.metallic_roughness_texture = opengl_engine.getTextureIfLoaded(OpenGLTextureKey(opengl_mat.metallic_roughness_tex_path));
				if(opengl_mat.metallic_roughness_texture.isNull() && world_mat) // If this texture is not loaded into the OpenGL engine:
					opengl_mat.metallic_roughness_texture = getBestTextureLOD(*world_mat, resource_manager.pathForURL(world_mat->roughness.texture_url), /*tex_has_alpha=*/false, /*use_sRGB=*/false, opengl_engine);

				if(opengl_mat.metallic_roughness_texture.ptr() != prev_tex)
					opengl_engine.materialTextureChanged(*ob->opengl_engine_ob, opengl_mat);
			}
			catch(glare::Exception& e)
			{
				conPrint("error loading texture: " + e.what());
			}
		}

		if(isNonEmptyAndNotMp4(opengl_mat.normal_map_path))
		{
			try
			{
				const OpenGLTexture* prev_tex = opengl_mat.normal_map.ptr();

				opengl_mat.normal_map = opengl_engine.getTextureIfLoaded(OpenGLTextureKey(opengl_mat.normal_map_path));
				if(opengl_mat.normal_map.isNull() && world_mat) // If this texture is not loaded into the OpenGL engine:
					opengl_mat.normal_map = getBestTextureLOD(*world_mat, resource_manager.pathForURL(world_mat->normal_map_url), /*tex_has_alpha=*/false, /*use_sRGB=*/false, opengl_engine);

				if(opengl_mat.normal_map.ptr() != prev_tex)
					opengl_engine.materialTextureChanged(*ob->opengl_engine_ob, opengl_mat);
			}
			catch(glare::Exception& e)
			{
				conPrint("error loading texture: " + e.what());
			}
		}

		if(isValidLightMapURL(opengl_mat.lightmap_path))
		{
			try
			{
				const OpenGLTexture* prev_tex = opengl_mat.lightmap_texture.ptr();

				opengl_mat.lightmap_texture = opengl_engine.getTextureIfLoaded(OpenGLTextureKey(opengl_mat.lightmap_path));
				if(opengl_mat.lightmap_texture.isNull()) // If this texture is not loaded into the OpenGL engine:
					opengl_mat.lightmap_texture = getBestLightmapLOD(resource_manager.pathForURL(ob->lightmap_url), opengl_engine); // Try and use a different LOD level of the lightmap, that is actually loaded.

				if(opengl_mat.lightmap_texture.ptr() != prev_tex)
					opengl_engine.materialTextureChanged(*ob->opengl_engine_ob, opengl_mat);
			}
			catch(glare::Exception& e)
			{
				conPrint("error loading texture: " + e.what());
			}
		}
		else
			opengl_mat.lightmap_texture = NULL;
	}
}


// For avatars
static void assignedLoadedOpenGLTexturesToMats(Avatar* av, OpenGLEngine& opengl_engine, ResourceManager& resource_manager)
{
	GLObject* gl_ob = av->graphics.skinned_gl_ob.ptr();
	if(!gl_ob)
		return;

	for(size_t z=0; z<gl_ob->materials.size(); ++z)
	{
		OpenGLMaterial& opengl_mat = gl_ob->materials[z];
		const WorldMaterial* world_mat = (z < av->avatar_settings.materials.size()) ? av->avatar_settings.materials[z].ptr() : NULL;

		if(isNonEmptyAndNotMp4(opengl_mat.tex_path))
		{
			try
			{
				const OpenGLTexture* prev_tex = opengl_mat.albedo_texture.ptr();

				opengl_mat.albedo_texture = opengl_engine.getTextureIfLoaded(OpenGLTextureKey(opengl_mat.tex_path));
				if(opengl_mat.albedo_texture.isNull() && world_mat) // If this texture is not loaded into the OpenGL engine, and the corresponding world object material is valid:
					opengl_mat.albedo_texture = getBestTextureLOD(*world_mat, resource_manager.pathForURL(world_mat->colour_texture_url), world_mat->colourTexHasAlpha(), /*use_sRGB=*/true, opengl_engine);

				if(opengl_mat.albedo_texture.ptr() != prev_tex)
					opengl_engine.materialTextureChanged(*gl_ob, opengl_mat);
			}
			catch(glare::Exception& e)
			{
				conPrint("error loading texture: " + e.what());
			}
		}

		if(isNonEmptyAndNotMp4(opengl_mat.emission_tex_path))
		{
			try
			{
				const OpenGLTexture* prev_tex = opengl_mat.emission_texture.ptr();

				opengl_mat.emission_texture = opengl_engine.getTextureIfLoaded(OpenGLTextureKey(opengl_mat.emission_tex_path));
				if(opengl_mat.emission_texture.isNull() && world_mat) // If this texture is not loaded into the OpenGL engine:
					opengl_mat.emission_texture = getBestTextureLOD(*world_mat, resource_manager.pathForURL(world_mat->emission_texture_url), /*tex_has_alpha=*/false, /*use_sRGB=*/true, opengl_engine);

				if(opengl_mat.emission_texture.ptr() != prev_tex)
					opengl_engine.materialTextureChanged(*gl_ob, opengl_mat);
			}
			catch(glare::Exception& e)
			{
				conPrint("error loading texture: " + e.what());
			}
		}

		if(isNonEmptyAndNotMp4(opengl_mat.metallic_roughness_tex_path))
		{
			try
			{
				const OpenGLTexture* prev_tex = opengl_mat.metallic_roughness_texture.ptr();

				opengl_mat.metallic_roughness_texture = opengl_engine.getTextureIfLoaded(OpenGLTextureKey(opengl_mat.metallic_roughness_tex_path));
				if(opengl_mat.metallic_roughness_texture.isNull() && world_mat) // If this texture is not loaded into the OpenGL engine:
					opengl_mat.metallic_roughness_texture = getBestTextureLOD(*world_mat, resource_manager.pathForURL(world_mat->roughness.texture_url), /*tex_has_alpha=*/false, /*use_sRGB=*/false, opengl_engine);

				if(opengl_mat.metallic_roughness_texture.ptr() != prev_tex)
					opengl_engine.materialTextureChanged(*gl_ob, opengl_mat);
			}
			catch(glare::Exception& e)
			{
				conPrint("error loading texture: " + e.what());
			}
		}

		if(isNonEmptyAndNotMp4(opengl_mat.normal_map_path))
		{
			try
			{
				const OpenGLTexture* prev_tex = opengl_mat.normal_map.ptr();

				opengl_mat.normal_map = opengl_engine.getTextureIfLoaded(OpenGLTextureKey(opengl_mat.normal_map_path));
				if(opengl_mat.normal_map.isNull() && world_mat) // If this texture is not loaded into the OpenGL engine:
					opengl_mat.normal_map = getBestTextureLOD(*world_mat, resource_manager.pathForURL(world_mat->normal_map_url), /*tex_has_alpha=*/false, /*use_sRGB=*/false, opengl_engine);

				if(opengl_mat.normal_map.ptr() != prev_tex)
					opengl_engine.materialTextureChanged(*gl_ob, opengl_mat);
			}
			catch(glare::Exception& e)
			{
				conPrint("error loading texture: " + e.what());
			}
		}
	}
}


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
		const float L_e = ob.materials[0]->emission_lum_flux_or_lum / (683.002f * 106.856e-9f * Maths::get2Pi<float>() * (1 - cos(use_cone_angle)));
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


// Check if the model file is downloaded.
// If so, load the model into the OpenGL and physics engines.
// If not, set a placeholder model and queue up the model download.
// Also enqueue any downloads for missing resources such as textures.
//
// Also called from checkForLODChanges() when the object LOD level changes, and so we may need to load a new model and/or textures.
void MainWindow::loadModelForObject(WorldObject* ob)
{
	const Vec4f campos = cam_controller.getPosition().toVec4fPoint();

	// Check object is in proximity.  Otherwise we might load objects outside of proximity, for example large objects transitioning from LOD level 1 to LOD level 2 or vice-versa.
	if(!ob->in_proximity)
		return;

	const int ob_lod_level = ob->getLODLevel(campos);
	const int ob_model_lod_level = myClamp(ob_lod_level, 0, ob->max_model_lod_level);
	
	const float max_dist_for_ob_lod_level = ob->getMaxDistForLODLevel(ob_lod_level);
	assert(max_dist_for_ob_lod_level >= campos.getDist(ob->getCentroidWS()));

	// For objects with max_model_lod_level=0 (e.g. objects with simple meshes like cubes), the mesh is valid at all object LOD levels, so has no max distance.
	const float max_dist_for_ob_model_lod_level = (ob->max_model_lod_level == 0) ? std::numeric_limits<float>::max() : max_dist_for_ob_lod_level; // We don't want to clamp with max_model_lod_level.

	// If we have a model loaded, that is not the placeholder model, and it has the correct LOD level, we don't need to do anything.
	if(ob->opengl_engine_ob.nonNull() && !ob->using_placeholder_model && (ob->loaded_model_lod_level == ob_model_lod_level) && (ob->loaded_lod_level == ob_lod_level))
		return;

	//print("Loading model for ob: UID: " + ob->uid.toString() + ", type: " + WorldObject::objectTypeString((WorldObject::ObjectType)ob->object_type) + ", model URL: " + ob->model_url + ", ob_model_lod_level: " + toString(ob_model_lod_level));
	Timer timer;

	ui->glWidget->makeCurrent();

	try
	{
		checkTransformOK(ob); // Throws glare::Exception if not ok.

		const Matrix4f ob_to_world_matrix = obToWorldMatrix(*ob);

		// Start downloading any resources we don't have that the object uses.
		startDownloadingResourcesForObject(ob, ob_lod_level);

		startLoadingTexturesForObject(*ob, ob_lod_level, max_dist_for_ob_lod_level);

		ob->loaded_lod_level = ob_lod_level;

		// Add any objects with gif or mp4 textures to the set of animated objects. (if not already)
		for(size_t i=0; i<ob->materials.size(); ++i)
		{
			if(	::hasExtensionStringView(ob->materials[i]->colour_texture_url, "gif") || ::hasExtensionStringView(ob->materials[i]->colour_texture_url, "mp4") ||
				::hasExtensionStringView(ob->materials[i]->emission_texture_url, "gif") || ::hasExtensionStringView(ob->materials[i]->emission_texture_url, "mp4"))
			{
				if(ob->animated_tex_data.isNull())
				{
					ob->animated_tex_data = new AnimatedTexObData();
					this->obs_with_animated_tex.insert(ob);
				}
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
				physics_ob->userdata = ob;
				physics_ob->userdata_type = 0;
				physics_ob->ob_uid = ob->uid;
				physics_ob->pos = ob->pos.toVec4fPoint();
				physics_ob->rot = Quatf::fromAxisAndAngle(normalise(ob->axis), ob->angle);
				physics_ob->scale = useScaleForWorldOb(ob->scale);

				GLObjectRef opengl_ob = ui->glWidget->opengl_engine->allocateObject();
				opengl_ob->mesh_data = this->hypercard_quad_opengl_mesh;
				opengl_ob->materials.resize(1);
				opengl_ob->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(0.85f));
				opengl_ob->materials[0].tex_matrix = Matrix2f(1, 0, 0, -1); // OpenGL expects texture data to have bottom left pixel at offset 0, we have top left pixel, so flip
				opengl_ob->ob_to_world_matrix = ob_to_world_matrix;


				const std::string tex_key = "hypercard_" + ob->content;

				// If the hypercard texture is already loaded, use it
				opengl_ob->materials[0].albedo_texture = ui->glWidget->opengl_engine->getTextureIfLoaded(OpenGLTextureKey(tex_key));
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
						task->opengl_engine = ui->glWidget->opengl_engine;
						load_item_queue.enqueueItem(*ob, task, /*max_dist_for_ob_lod_level=*/200.f);
					}
				}

				opengl_ob->materials[0].materialise_effect = use_materialise_effect;
				opengl_ob->materials[0].materialise_start_time = ob->materialise_effect_start_time;


				ob->opengl_engine_ob = opengl_ob;
				ob->physics_object = physics_ob;
				ob->loaded_content = ob->content;

				ui->glWidget->opengl_engine->addObject(ob->opengl_engine_ob);

				physics_world->addObject(ob->physics_object);
			}
		}
		else if(ob->object_type == WorldObject::ObjectType_Spotlight)
		{
			if(ob->opengl_engine_ob.isNull())
			{
				assert(ob->physics_object.isNull());

				PhysicsObjectRef physics_ob = new PhysicsObject(/*collidable=*/true);
				physics_ob->shape = this->spotlight_shape;
				physics_ob->userdata = ob;
				physics_ob->userdata_type = 0;
				physics_ob->ob_uid = ob->uid;
				physics_ob->pos = ob->pos.toVec4fPoint();
				physics_ob->rot = Quatf::fromAxisAndAngle(normalise(ob->axis), ob->angle);
				physics_ob->scale = useScaleForWorldOb(ob->scale);

				GLObjectRef opengl_ob = ui->glWidget->opengl_engine->allocateObject();
				opengl_ob->mesh_data = this->spotlight_opengl_mesh;
				
				// Use material[1] from the WorldObject as the light housing GL material.
				opengl_ob->materials.resize(2);
				if(ob->materials.size() >= 2)
					ModelLoading::setGLMaterialFromWorldMaterial(*ob->materials[1], /*lod level=*/ob_lod_level, /*lightmap URL=*/"", *resource_manager, /*open gl mat=*/opengl_ob->materials[0]);
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
				
				// Apply a light emitting material to the light surface material in the spotlight model.
				if(ob->materials.size() >= 1)
				{
					opengl_ob->materials[1].emission_linear_rgb = toLinearSRGB(ob->materials[0]->colour_rgb);
					opengl_ob->materials[1].emission_scale = scale;
				}


				ob->opengl_engine_ob = opengl_ob;
				ob->opengl_light = light;
				ob->physics_object = physics_ob;
				ob->loaded_content = ob->content;

				ui->glWidget->opengl_engine->addObject(ob->opengl_engine_ob);
				ui->glWidget->opengl_engine->addLight(ob->opengl_light);

				physics_world->addObject(ob->physics_object);

				loadScriptForObject(ob); // Load any script for the object.
			}
		}
		else if(ob->object_type == WorldObject::ObjectType_WebView)
		{
			if(ob->opengl_engine_ob.isNull())
			{
				assert(ob->physics_object.isNull());

				PhysicsObjectRef physics_ob = new PhysicsObject(/*collidable=*/true);
				physics_ob->shape = this->image_cube_shape;
				physics_ob->userdata = ob;
				physics_ob->userdata_type = 0;
				physics_ob->ob_uid = ob->uid;
				physics_ob->pos = ob->pos.toVec4fPoint();
				physics_ob->rot = Quatf::fromAxisAndAngle(normalise(ob->axis), ob->angle);
				physics_ob->scale = useScaleForWorldOb(ob->scale);

				GLObjectRef opengl_ob = ui->glWidget->opengl_engine->allocateObject();
				opengl_ob->mesh_data = this->image_cube_opengl_mesh;
				opengl_ob->materials.resize(2);
				opengl_ob->materials[0].albedo_linear_rgb = Colour3f(0.f);
				opengl_ob->materials[0].emission_linear_rgb = Colour3f(1.f);
				const float luminance = 24000; // nits.  Chosen so videos look about the right brightness in daylight.
				opengl_ob->materials[0].emission_scale = luminance / (683.002f * 106.856e-9f); // See ModelLoading::setGLMaterialFromWorldMaterialWithLocalPaths()
				opengl_ob->materials[0].tex_matrix = Matrix2f(1, 0, 0, -1); // OpenGL expects texture data to have bottom left pixel at offset 0, we have top left pixel, so flip
				opengl_ob->materials[0].materialise_effect = use_materialise_effect;
				opengl_ob->materials[0].materialise_start_time = ob->materialise_effect_start_time;
				opengl_ob->ob_to_world_matrix = ob_to_world_matrix;

				ob->opengl_engine_ob = opengl_ob;
				ob->physics_object = physics_ob;
				ob->loaded_content = ob->content;

				ui->glWidget->opengl_engine->addObject(ob->opengl_engine_ob);

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
				physics_ob->userdata = ob;
				physics_ob->userdata_type = 0;
				physics_ob->ob_uid = ob->uid;
				physics_ob->pos = ob->pos.toVec4fPoint();
				physics_ob->rot = Quatf::fromAxisAndAngle(normalise(ob->axis), ob->angle);
				physics_ob->scale = useScaleForWorldOb(ob->scale);

				GLObjectRef opengl_ob = ui->glWidget->opengl_engine->allocateObject();
				opengl_ob->mesh_data = this->image_cube_opengl_mesh;
				opengl_ob->materials.resize(2);
				opengl_ob->materials[0].albedo_linear_rgb = Colour3f(0.f);
				opengl_ob->materials[0].emission_linear_rgb = Colour3f(1.f);
				const float luminance = 24000; // nits.  Chosen so videos look about the right brightness in daylight.
				opengl_ob->materials[0].emission_scale = luminance / (683.002f * 106.856e-9f); // See ModelLoading::setGLMaterialFromWorldMaterialWithLocalPaths()
				opengl_ob->materials[0].tex_matrix = Matrix2f(1, 0, 0, -1); // OpenGL expects texture data to have bottom left pixel at offset 0, we have top left pixel, so flip
				opengl_ob->materials[0].materialise_effect = use_materialise_effect;
				opengl_ob->materials[0].materialise_start_time = ob->materialise_effect_start_time;
				opengl_ob->ob_to_world_matrix = ob_to_world_matrix;

				ob->opengl_engine_ob = opengl_ob;
				ob->physics_object = physics_ob;
				ob->loaded_content = ob->content;

				ui->glWidget->opengl_engine->addObject(ob->opengl_engine_ob);

				physics_world->addObject(ob->physics_object);

				ob->browser_vid_player = new BrowserVidPlayer();
				this->browser_vid_player_obs.insert(ob);
			}
		}
		else if(ob->object_type == WorldObject::ObjectType_VoxelGroup)
		{
			if(ob->loaded_model_lod_level != ob_model_lod_level) // We may already have the correct LOD model loaded, don't reload if so.
			{
				// Do the model loading (conversion of voxel group to triangle mesh) in a different thread
				Reference<LoadModelTask> load_model_task = new LoadModelTask();

				load_model_task->voxel_ob_model_lod_level = ob_model_lod_level;
				load_model_task->opengl_engine = this->ui->glWidget->opengl_engine;
				load_model_task->unit_cube_shape = this->unit_cube_shape;
				load_model_task->result_msg_queue = &this->msg_queue;
				load_model_task->resource_manager = resource_manager;
				load_model_task->voxel_ob = ob;
				load_model_task->build_dynamic_physics_ob = ob->isDynamic();

				load_item_queue.enqueueItem(*ob, load_model_task, max_dist_for_ob_model_lod_level);

				load_placeholder = ob->getCompressedVoxels().size() != 0;
			}
			else
			{
				// Update textures to correct LOD-level textures.
				if(ob->opengl_engine_ob.nonNull() && !ob->using_placeholder_model)
				{
					ModelLoading::setMaterialTexPathsForLODLevel(*ob->opengl_engine_ob, ob_lod_level, ob->materials, ob->lightmap_url, *resource_manager);
					assignedLoadedOpenGLTexturesToMats(ob, *ui->glWidget->opengl_engine, *resource_manager);
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

			
			if(::hasPrefix(ob->content, "biome:")) // If we want to scatter on this object:
			{
				if(ob->scattering_info.isNull()) // if scattering info is not computed for this object yet:
				{
					const bool already_procesing = scatter_info_processing.count(ob->uid) > 0; // And we aren't already building scattering info for this object (e.g. task is in a queue):
					if(!already_procesing)
					{
						Reference<BuildScatteringInfoTask> scatter_task = new BuildScatteringInfoTask();
						scatter_task->ob_uid = ob->uid;
						scatter_task->lod_model_url = ob->model_url; // Use full res model URL
						scatter_task->ob_to_world = ob_to_world_matrix;
						scatter_task->result_msg_queue = &this->msg_queue;
						scatter_task->resource_manager = resource_manager;
						load_item_queue.enqueueItem(*ob, scatter_task, /*task max dist=*/1.0e10f);

						scatter_info_processing.insert(ob->uid);
					}
				}
			}


			if(!ob->model_url.empty() && 
				(ob->loaded_model_lod_level != ob_model_lod_level))  // We may already have the correct LOD model loaded, don't reload if so.
				// (The object LOD level might have changed, but the model LOD level may be the same due to max model lod level, for example for simple cube models.)
			{
				bool added_opengl_ob = false;
				const std::string lod_model_url = WorldObject::getLODModelURLForLevel(ob->model_url, ob_model_lod_level);

				// print("Loading model for ob: UID: " + ob->uid.toString() + ", type: " + WorldObject::objectTypeString((WorldObject::ObjectType)ob->object_type) + ", lod_model_url: " + lod_model_url);

				Reference<MeshData> mesh_data = mesh_manager.getMeshData(lod_model_url);
				Reference<PhysicsShapeData> physics_shape_data = mesh_manager.getPhysicsShapeData(MeshManagerPhysicsShapeKey(lod_model_url, ob->isDynamic()));
				if(mesh_data.nonNull() && physics_shape_data.nonNull())
				{
					const bool is_meshdata_loaded_into_opengl = mesh_data->gl_meshdata->vbo_handle.valid();
					if(is_meshdata_loaded_into_opengl)
					{
						removeAndDeleteGLObjectsForOb(*ob);

						// Remove previous physics object. If this is a dynamic or kinematic object, don't delete old object though, unless it's a placeholder.
						if(ob->physics_object.nonNull() && (ob->using_placeholder_model || !(ob->physics_object->dynamic || ob->physics_object->kinematic)))
						{
							destroyVehiclePhysicsControllingObject(ob); // Destroy any vehicle controller controlling this object, as vehicle controllers have pointers to physics bodies, which we can't leave dangling.
							physics_world->removeObject(ob->physics_object);
							ob->physics_object = NULL;
						}

						// Create gl and physics object now
						ob->opengl_engine_ob = ModelLoading::makeGLObjectForMeshDataAndMaterials(*ui->glWidget->opengl_engine, mesh_data->gl_meshdata, ob_lod_level, ob->materials, ob->lightmap_url, *resource_manager, ob_to_world_matrix);
						
						mesh_data->meshDataBecameUsed();
						ob->mesh_manager_data = mesh_data;// Hang on to a reference to the mesh data, so when object-uses of it are removed, it can be removed from the MeshManager with meshDataBecameUnused().

						physics_shape_data->shapeDataBecameUsed();
						ob->mesh_manager_shape_data = physics_shape_data; // Likewise for the physics mesh data.

						assignedLoadedOpenGLTexturesToMats(ob, *ui->glWidget->opengl_engine, *resource_manager);
						for(size_t z=0; z<ob->opengl_engine_ob->materials.size(); ++z)
						{
							ob->opengl_engine_ob->materials[z].materialise_effect = use_materialise_effect;
							ob->opengl_engine_ob->materials[z].materialise_start_time = ob->materialise_effect_start_time;
						}


						ob->loaded_model_lod_level = ob_model_lod_level;

						if(ob->physics_object.isNull()) // if object was dynamic, we may not have unloaded its physics object above.
						{
							ob->physics_object = new PhysicsObject(/*collidable=*/ob->isCollidable());

							PhysicsShape use_shape = physics_shape_data->physics_shape;
							if(ob->centre_of_mass_offset_os != Vec3f(0.f))
								use_shape = PhysicsWorld::createCOMOffsetShapeForShape(physics_shape_data->physics_shape, ob->centre_of_mass_offset_os.toVec4fVector());

							ob->physics_object->shape = use_shape;
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
						}


						if(hasPrefix(ob->content, "biome: park"))
						{
							ob->opengl_engine_ob->draw_to_mask_map = true;

							if(this->terrain_system.nonNull())
								this->terrain_system->invalidateVegetationMap(ob->getAABBWS());
						}


						//Timer timer;
						ui->glWidget->opengl_engine->addObject(ob->opengl_engine_ob);
						//if(timer.elapsed() > 0.01) conPrint("addObject took                    " + timer.elapsedStringNSigFigs(5));


						ui->indigoView->objectAdded(*ob, *this->resource_manager);

						loadScriptForObject(ob); // Load any script for the object.

						// If we replaced the model for selected_ob, reselect it in the OpenGL engine
						if(this->selected_ob == ob)
							ui->glWidget->opengl_engine->selectObject(ob->opengl_engine_ob);

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

							load_model_task->lod_model_url = lod_model_url;
							load_model_task->opengl_engine = this->ui->glWidget->opengl_engine;
							load_model_task->unit_cube_shape = this->unit_cube_shape;
							load_model_task->result_msg_queue = &this->msg_queue;
							load_model_task->resource_manager = resource_manager;
							load_model_task->build_dynamic_physics_ob = ob->isDynamic();

							load_item_queue.enqueueItem(*ob, load_model_task, max_dist_for_ob_model_lod_level);
						}
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
					ModelLoading::setMaterialTexPathsForLODLevel(*ob->opengl_engine_ob, ob_lod_level, ob->materials, ob->lightmap_url, *resource_manager);
					assignedLoadedOpenGLTexturesToMats(ob, *ui->glWidget->opengl_engine, *resource_manager);
				}
			}
		}
		else
		{
			throw glare::Exception("Invalid object_type: " + toString((int)(ob->object_type)));
		}

		if(load_placeholder)
		{
			// Load a placeholder object (cube) if we don't have an existing model (e.g. another LOD level) being displayed.
			// We also need a valid AABB.
			if(!ob->getAABBOS().isEmpty() && ob->opengl_engine_ob.isNull())
				addPlaceholderObjectsForOb(*ob);
		}

		//print("\tModel loaded. (Elapsed: " + timer.elapsedStringNSigFigs(4) + ")");
	}
	catch(glare::Exception& e)
	{
		print("Error while loading object with UID " + ob->uid.toString() + ", model_url='" + ob->model_url + "': " + e.what());
	}
}


// Check if the avatar model file is downloaded.
// If so, load the model into the OpenGL engine.
// If not, queue up the model download.
// Also enqueue any downloads for missing resources such as textures.
void MainWindow::loadModelForAvatar(Avatar* avatar)
{
	const bool our_avatar = avatar->uid == this->client_avatar_uid;

	const int ob_lod_level = avatar->getLODLevel(cam_controller.getPosition());
	const int ob_model_lod_level = ob_lod_level;

	const float max_dist_for_ob_lod_level = avatar->getMaxDistForLODLevel(ob_lod_level);
	const float max_dist_for_ob_model_lod_level = avatar->getMaxDistForLODLevel(ob_model_lod_level);


	// If we have a model loaded, that is not the placeholder model, and it has the correct LOD level, we don't need to do anything.
	if(avatar->graphics.skinned_gl_ob.nonNull() && /*&& !ob->using_placeholder_model && */(avatar->graphics.loaded_lod_level == ob_lod_level))
		return;

	const std::string default_model_url = "xbot_glb_10972822012543217816.glb";
	//const std::string use_model_url = avatar->avatar_settings.model_url.empty() ? default_model_url : avatar->avatar_settings.model_url;
	//print("Loading model for ob: UID: " + ob->uid.toString() + ", type: " + WorldObject::objectTypeString((WorldObject::ObjectType)ob->object_type) + ", model URL: " + ob->model_url);
	Timer timer;
	

	// If the avatar model URL is empty, we will be using the default xbot model.  Need to make it be rotated from y-up to z-up, and assign materials.
	if(avatar->avatar_settings.model_url.empty())
	{
		avatar->avatar_settings.model_url = default_model_url;
		avatar->avatar_settings.materials.resize(2);

		avatar->avatar_settings.materials[0] = new WorldMaterial();
		avatar->avatar_settings.materials[0]->colour_rgb = Colour3f(0.5f, 0.6f, 0.7f);
		avatar->avatar_settings.materials[0]->metallic_fraction.val = 0.5f;
		avatar->avatar_settings.materials[0]->roughness.val = 0.3f;

		avatar->avatar_settings.materials[1] = new WorldMaterial();
		avatar->avatar_settings.materials[1]->colour_rgb = Colour3f(0.8f);
		avatar->avatar_settings.materials[1]->metallic_fraction.val = 0.0f;

		const float EYE_HEIGHT = 1.67f;
		const Matrix4f to_z_up(Vec4f(1,0,0,0), Vec4f(0, 0, 1, 0), Vec4f(0, -1, 0, 0), Vec4f(0,0,0,1));
		avatar->avatar_settings.pre_ob_to_world_matrix = Matrix4f::translationMatrix(0, 0, -EYE_HEIGHT) * to_z_up;
	}


	ui->glWidget->makeCurrent();

	try
	{
		// Start downloading any resources we don't have that the object uses.
		startDownloadingResourcesForAvatar(avatar, ob_lod_level, our_avatar);

		startLoadingTexturesForAvatar(*avatar, ob_lod_level, max_dist_for_ob_lod_level, our_avatar);

		// Add any objects with gif or mp4 textures to the set of animated objects.
		/*for(size_t i=0; i<avatar->materials.size(); ++i)
		{
			if(::hasExtensionStringView(avatar->materials[i]->colour_texture_url, "gif") || ::hasExtensionStringView(avatar->materials[i]->colour_texture_url, "mp4"))
			{
				//Reference<AnimatedTexObData> anim_data = new AnimatedTexObData();
				this->obs_with_animated_tex.insert(std::make_pair(ob, AnimatedTexObData()));
			}
		}*/


		bool added_opengl_ob = false;

		const std::string lod_model_url = WorldObject::getLODModelURLForLevel(avatar->avatar_settings.model_url, ob_model_lod_level);

		avatar->graphics.loaded_lod_level = ob_lod_level;

		// print("Loading model for ob: UID: " + ob->uid.toString() + ", type: " + WorldObject::objectTypeString((WorldObject::ObjectType)ob->object_type) + ", lod_model_url: " + lod_model_url);


		Reference<MeshData> mesh_data = mesh_manager.getMeshData(lod_model_url);
		if(mesh_data.nonNull())
		{
			const bool is_meshdata_loaded_into_opengl = mesh_data->gl_meshdata->vbo_handle.valid();
			if(is_meshdata_loaded_into_opengl)
			{
				removeAndDeleteGLObjectForAvatar(*avatar);

				const Matrix4f ob_to_world_matrix = obToWorldMatrix(*avatar);

				// Create gl and physics object now
				avatar->graphics.skinned_gl_ob = ModelLoading::makeGLObjectForMeshDataAndMaterials(*ui->glWidget->opengl_engine, mesh_data->gl_meshdata, ob_lod_level, avatar->avatar_settings.materials, /*lightmap_url=*/std::string(), 
					*resource_manager, ob_to_world_matrix);

				mesh_data->meshDataBecameUsed();
				avatar->mesh_data = mesh_data; // Hang on to a reference to the mesh data, so when object-uses of it are removed, it can be removed from the MeshManager with meshDataBecameUnused().

				// Load animation data for ready-player-me type avatars
				if(!avatar->graphics.skinned_gl_ob->mesh_data->animation_data.retarget_adjustments_set)
				{
					FileInStream file(base_dir_path + "/resources/extracted_avatar_anim.bin");
					avatar->graphics.skinned_gl_ob->mesh_data->animation_data.loadAndRetargetAnim(file);
				}

				avatar->graphics.build();

				assignedLoadedOpenGLTexturesToMats(avatar, *ui->glWidget->opengl_engine, *resource_manager);

				// Enable materalise effect if needed
				const float current_time = (float)Clock::getTimeSinceInit();
				const bool use_materialise_effect = avatar->use_materialise_effect_on_load && (current_time - avatar->materialise_effect_start_time < 2.0f);

				for(size_t z=0; z<avatar->graphics.skinned_gl_ob->materials.size(); ++z)
				{
					avatar->graphics.skinned_gl_ob->materials[z].materialise_effect = use_materialise_effect;
					avatar->graphics.skinned_gl_ob->materials[z].materialise_start_time = avatar->materialise_effect_start_time;
				}

				avatar->graphics.loaded_lod_level = ob_lod_level;

				ui->glWidget->opengl_engine->addObject(avatar->graphics.skinned_gl_ob);

				// If we just loaded the graphics for our own avatar, see if there is a gesture animation we should be playing, and if so, play it.
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

					load_model_task->lod_model_url = lod_model_url;
					load_model_task->opengl_engine = this->ui->glWidget->opengl_engine;
					load_model_task->unit_cube_shape = this->unit_cube_shape;
					load_model_task->result_msg_queue = &this->msg_queue;
					load_model_task->resource_manager = resource_manager;

					load_item_queue.enqueueItem(*avatar, load_model_task, max_dist_for_ob_model_lod_level, our_avatar);
				}
			}
		}

		if(!added_opengl_ob)
		{
			this->loading_model_URL_to_avatar_UID_map[lod_model_url].insert(avatar->uid);

			// Load a placeholder object (cube) if we don't have an existing model (e.g. another LOD level) being displayed.
			// We also need a valid AABB.
			//if(!ob->aabb_ws.isEmpty() && ob->opengl_engine_ob.isNull())
			//	addPlaceholderObjectsForOb(*ob);
		}

		//print("\tModel loaded. (Elapsed: " + timer.elapsedStringNSigFigs(4) + ")");
	}
	catch(glare::Exception& e)
	{
		print("Error while loading avatar with UID " + avatar->uid.toString() + ", model_url='" + avatar->avatar_settings.model_url + "': " + e.what());
	}
}


// Remove any existing instances of this object from the instance set, also from 3d engine and physics engine.
void MainWindow::removeInstancesOfObject(WorldObject* prototype_ob)
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


void MainWindow::removeObScriptingInfo(WorldObject* ob)
{
	removeInstancesOfObject(ob);
	if(ob->script_evaluator.nonNull())
	{
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


void MainWindow::loadScriptForObject(WorldObject* ob)
{
	PERFORMANCEAPI_INSTRUMENT_FUNCTION();
	ZoneScoped; // Tracy profiler

	// If the script changed bit was set, destroy the script evaluator, we will create a new one.
 	if(BitUtils::isBitSet(ob->changed_flags, WorldObject::SCRIPT_CHANGED))
	{
		// conPrint("MainWindow::loadScriptForObject(): SCRIPT_CHANGED bit was set, destroying script_evaluator.");
		
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
				load_item_queue.enqueueItem(*ob, task, /*task max dist=*/std::numeric_limits<float>::infinity());
			}
		}


		BitUtils::zeroBit(ob->changed_flags, WorldObject::SCRIPT_CHANGED);
	}

	
	// If we have a script evaluator already, but the opengl ob has been recreated (due to LOD level changes), we need to recreate the instance_matrices VBO
	if(ob->script_evaluator.nonNull() && ob->opengl_engine_ob.nonNull() && ob->opengl_engine_ob->instance_matrix_vbo.isNull() && !ob->instance_matrices.empty())
	{
		ob->opengl_engine_ob->enableInstancing(*ui->glWidget->opengl_engine->vert_buf_allocator, ob->instance_matrices.data(), sizeof(Matrix4f) * ob->instance_matrices.size());

		ui->glWidget->opengl_engine->objectMaterialsUpdated(*ob->opengl_engine_ob); // Reload mat to enable instancing
	}
}


void MainWindow::handleScriptLoadedForObUsingScript(ScriptLoadedThreadMessage* loaded_msg, WorldObject* ob)
{
	assert(loaded_msg->script == ob->script);
	assert(loaded_msg->script_evaluator.nonNull());

	try
	{
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
				ob->opengl_engine_ob->enableInstancing(*ui->glWidget->opengl_engine->vert_buf_allocator, ob->instance_matrices.data(), sizeof(Matrix4f) * count);
						
				ui->glWidget->opengl_engine->objectMaterialsUpdated(*ob->opengl_engine_ob); // Reload mat to enable instancing
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
void MainWindow::doBiomeScatteringForObject(WorldObject* ob)
{
#if 0
	PERFORMANCEAPI_INSTRUMENT_FUNCTION();
	ZoneScoped; // Tracy profiler

	if(::hasPrefix(ob->content, "biome:"))
	{
		if(!biome_manager->isObjectInBiome(ob))
		{
			biome_manager->initTexturesAndModels(base_dir_path, *ui->glWidget->opengl_engine, *resource_manager);
		}
	}
#endif
}


// Try and start loading the audio file for the world object, as specified by ob->audio_source_url.
// If the audio file is already loaded, (e.g. ob->loaded_audio_source_url == ob->audio_source_url), then do nothing.
// If the object is further than MAX_AUDIO_DIST from the camera, don't load the audio.
void MainWindow::loadAudioForObject(WorldObject* ob)
{
	if(BitUtils::isBitSet(ob->changed_flags, WorldObject::AUDIO_SOURCE_URL_CHANGED))
	{
		//conPrint("MainWindow::loadAudioForObject(): AUDIO_SOURCE_URL_CHANGED bit was set, setting state to AudioState_NotLoaded.");
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

				if(resource_manager->isFileForURLPresent(ob->audio_source_url))
				{
					//if(!isAudioProcessed(ob->audio_source_url)) // If we are not already loading the audio:

					if(hasExtensionStringView(ob->audio_source_url, "mp3"))
					{
						// Make a new audio source
						glare::AudioSourceRef source = audio_engine.addSourceFromStreamingSoundFile(resource_manager->pathForURL(ob->audio_source_url), ob->pos.toVec4fPoint(), ob->audio_volume, this->world_state->getCurrentGlobalTime());

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

						if((source->type != glare::AudioSource::SourceType_OneShot) && // Only mute looping/streaming sounds: (We won't be muting footsteps etc.)
							mute_outside_audio && // If we are in a parcel, which has the mute-outside-audio option enabled:
							(source->userdata_1 != in_parcel_id.value())) // And the source is in another parcel (or not in any parcel):
						{
							source->setMuteVolumeFactorImmediately(0.f); // Mute it (set mute volume factor)
							audio_engine.sourceVolumeUpdated(*source); // Tell audio engine to mute it.
						}
						//----------------------------------------------------------------------------------------------
					}
					else // else loading a non-streaming source, such as a WAV file.
					{
						const bool just_inserted = checkAddAudioToProcessingSet(ob->audio_source_url); // Mark audio as being processed so another LoadAudioTask doesn't try and process it also.
						if(just_inserted)
						{
							// conPrint("Launching LoadAudioTask");
							// Do the audio file loading in a different thread
							Reference<LoadAudioTask> load_audio_task = new LoadAudioTask();

							load_audio_task->audio_source_url = ob->audio_source_url;
							load_audio_task->audio_source_path = resource_manager->pathForURL(ob->audio_source_url);
							load_audio_task->result_msg_queue = &this->msg_queue;

							load_item_queue.enqueueItem(*ob, load_audio_task, /*task max dist=*/maxAudioDistForSourceVolFactor(ob->audio_volume));
						}
					}

					//ob->loaded_audio_source_url = ob->audio_source_url;
				}
			}
		}
	}
	catch(glare::Exception& e)
	{
		print("Error while loading audio for object with UID " + ob->uid.toString() + ", audio_source_url='" + ob->audio_source_url + "': " + e.what());
		ob->audio_state = WorldObject::AudioState_ErrorWhileLoading; // Go to the error state, so we don't try and keep loading this audio.
	}
}


void MainWindow::updateInstancedCopiesOfObject(WorldObject* ob)
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


void MainWindow::logMessage(const std::string& msg) // Append to LogWindow log display
{
	//this->logfile << msg << "\n";
	if(this->log_window && !running_destructor)
		this->log_window->appendLine(msg);
}


void MainWindow::logAndConPrintMessage(const std::string& msg) // Print to stdout and append to LogWindow log display
{
	conPrint(msg);
	//this->logfile << msg << "\n";
	if(this->log_window && !running_destructor)
		this->log_window->appendLine(msg);
}


void MainWindow::print(const std::string& s) // Print a message and a newline character.
{
	logMessage(s);
}


void MainWindow::printStr(const std::string& s) // Print a message without a newline character.
{
	logMessage(s);
}


static const size_t MAX_NUM_NOTIFICATIONS = 5;


void MainWindow::showErrorNotification(const std::string& message)
{
	QLabel* label = new QLabel(ui->notificationContainer);
	label->setText(QtUtils::toQString(message));
	label->setTextInteractionFlags(Qt::TextSelectableByMouse);

	label->setStyleSheet("QLabel { padding: 6px; background-color : rgb(255, 200, 200); }");

	ui->notificationContainer->layout()->addWidget(label);

	Notification n;
	n.creation_time = Clock::getTimeSinceInit();
	n.label = label;
	notifications.push_back(n);

	if(notifications.size() == 1)
		ui->infoDockWidget->show();
	else if(notifications.size() > MAX_NUM_NOTIFICATIONS)
	{
		// Remove the first notification
		Notification& notification = notifications.front();
		ui->notificationContainer->layout()->removeWidget(notification.label);
		notification.label->deleteLater();
		notifications.pop_front(); // remove from list
	}
}


void MainWindow::showInfoNotification(const std::string& message)
{
	QLabel* label = new QLabel(ui->notificationContainer);
	label->setText(QtUtils::toQString(message));
	label->setTextInteractionFlags(Qt::TextSelectableByMouse);

	label->setStyleSheet("QLabel { padding: 6px; background-color : rgb(239, 228, 176); }");

	ui->notificationContainer->layout()->addWidget(label);

	Notification n;
	n.creation_time = Clock::getTimeSinceInit();
	n.label = label;
	notifications.push_back(n);

	if(notifications.size() == 1)
		ui->infoDockWidget->show();
	else if(notifications.size() > MAX_NUM_NOTIFICATIONS)
	{
		// Remove the first notification
		Notification& notification = notifications.front();
		ui->notificationContainer->layout()->removeWidget(notification.label);
		notification.label->deleteLater();
		notifications.pop_front(); // remove from list
	}
}


struct AvatarNameInfo
{
	std::string name;
	Colour3f colour;

	inline bool operator < (const AvatarNameInfo& other) const { return name < other.name; }
};


void MainWindow::updateOnlineUsersList() // Works off world state avatars.
{
	if(world_state.isNull())
		return;

	std::vector<AvatarNameInfo> names;
	{
		Lock lock(world_state->mutex);
		for(auto entry : world_state->avatars)
		{
			AvatarNameInfo info;
			info.name   = entry.second->name;
			info.colour = entry.second->name_colour;
			names.push_back(info);
		}
	}

	std::sort(names.begin(), names.end());

	// Combine names into a single string, while escaping any HTML chars.
	QString s;
	for(size_t i=0; i<names.size(); ++i)
	{
		s += QtUtils::toQString("<span style=\"color:rgb(" + toString(names[i].colour.r * 255) + ", " + toString(names[i].colour.g * 255) + ", " + toString(names[i].colour.b * 255) + ")\">");
		s += QtUtils::toQString(names[i].name).toHtmlEscaped() + "</span>" + ((i + 1 < names.size()) ? "<br/>" : "");
	}

	ui->onlineUsersTextEdit->setHtml(s);
}


// For each direction x, y, z, the two other basis vectors. 
static const Vec4f basis_vectors[6] = { Vec4f(0,1,0,0), Vec4f(0,0,1,0), Vec4f(0,0,1,0), Vec4f(1,0,0,0), Vec4f(1,0,0,0), Vec4f(0,1,0,0) };

static const float arc_handle_half_angle = 1.5f;


// Avoids NaNs
static float safeATan2(float y, float x)
{
	const float a = std::atan2(y, x);
	if(!isFinite(a))
		return 0.f;
	else
		return a;
}


// Update object placement beam - a beam that goes from the object to what's below it.
// Also updates axis arrows and rotation arc handles.
void MainWindow::updateSelectedObjectPlacementBeam()
{
	// Update object placement beam - a beam that goes from the object to what's below it.
	if(selected_ob.nonNull() && this->selected_ob->opengl_engine_ob.nonNull())
	{
		GLObjectRef opengl_ob = this->selected_ob->opengl_engine_ob;
		const Matrix4f& to_world = opengl_ob->ob_to_world_matrix;

		const js::AABBox new_aabb_ws = ui->glWidget->opengl_engine->getAABBWSForObjectWithTransform(*opengl_ob, to_world);

		// We need to determine where to trace down from.  
		// To find this point, first trace up *just* against the selected object.
		// NOTE: With introduction of Jolt, we don't have just tracing against a single object, trace against world for now.
		RayTraceResult trace_results;
		Vec4f start_trace_pos = new_aabb_ws.centroid();
		start_trace_pos[2] = new_aabb_ws.min_[2] - 0.001f;
		//this->selected_ob->physics_object->traceRay(Ray(start_trace_pos, Vec4f(0, 0, 1, 0), 0.f, 1.0e5f), trace_results);
		this->physics_world->traceRay(start_trace_pos, Vec4f(0, 0, 1, 0), 1.0e5f, trace_results);
		const float up_beam_len = trace_results.hit_object ? trace_results.hit_t : new_aabb_ws.axisLength(2) * 0.5f;

		// Now Trace ray downwards.  Start from just below where we got to in upwards trace.
		const Vec4f down_beam_startpos = start_trace_pos + Vec4f(0, 0, 1, 0) * (up_beam_len - 0.001f);
		this->physics_world->traceRay(down_beam_startpos, Vec4f(0, 0, -1, 0), /*max_t=*/1.0e5f, trace_results);
		const float down_beam_len = trace_results.hit_object ? trace_results.hit_t : 1000.0f;
		const Vec4f lower_hit_normal = trace_results.hit_object ? normalise(trace_results.hit_normal_ws) : Vec4f(0, 0, 1, 0);

		const Vec4f down_beam_hitpos = down_beam_startpos + Vec4f(0, 0, -1, 0) * down_beam_len;

		Matrix4f scale_matrix = Matrix4f::scaleMatrix(/*radius=*/0.05f, /*radius=*/0.05f, down_beam_len);
		Matrix4f ob_to_world = Matrix4f::translationMatrix(down_beam_hitpos) * scale_matrix;
		ob_placement_beam->ob_to_world_matrix = ob_to_world;
		if(ui->glWidget->opengl_engine->isObjectAdded(ob_placement_beam))
			ui->glWidget->opengl_engine->updateObjectTransformData(*ob_placement_beam);

		// Place hit marker
		const Matrix4f marker_scale_matrix = Matrix4f::scaleMatrix(0.2f, 0.2f, 0.01f);
		const Matrix4f orientation = Matrix4f::constructFromVectorStatic(lower_hit_normal);
		ob_placement_marker->ob_to_world_matrix = Matrix4f::translationMatrix(down_beam_hitpos) *
			orientation * marker_scale_matrix;
		if(ui->glWidget->opengl_engine->isObjectAdded(ob_placement_marker))
			ui->glWidget->opengl_engine->updateObjectTransformData(*ob_placement_marker);

		// Place x, y, z axis arrows.
		if(axis_and_rot_obs_enabled)
		{
			const Vec4f use_ob_origin = opengl_ob->ob_to_world_matrix.getColumn(3);
			const Vec4f arrow_origin = use_ob_origin;

			// Make arrow long enough so that it sticks out of the object, if the object is large.
			// Try a bunch of different orientations of the object, computing the world space AABB for each orientation,
			// take the union of all such AABBs.
			js::AABBox use_aabb_ws = js::AABBox::emptyAABBox();
			for(int x=0; x<8; ++x)
				for(int y=0; y<5; ++y)
				{
					const float phi   = Maths::get2Pi<float>() * x / 8;
					const float theta = Maths::pi<float>() * y / 5;
					const Vec4f dir = GeometrySampling::dirForSphericalCoords(phi, theta);
					const Matrix4f rot_m = Matrix4f::constructFromVectorStatic(dir);

					const Vec4f translation((float)this->selected_ob->pos.x, (float)this->selected_ob->pos.y, (float)this->selected_ob->pos.z, 0.f);

					const Matrix4f use_to_world = leftTranslateAffine3(translation, rot_m * Matrix4f::scaleMatrix(this->selected_ob->scale.x, this->selected_ob->scale.y, this->selected_ob->scale.z));

					use_aabb_ws.enlargeToHoldAABBox(opengl_ob->mesh_data->aabb_os.transformedAABBFast(use_to_world));
				}

			const float max_control_scale = (float)this->selected_ob->pos.getDist(cam_controller.getPosition()) * 0.5f;
			const float control_scale = myMin(max_control_scale, use_aabb_ws.axisLength(use_aabb_ws.longestAxis()));

			const float arrow_len = myMax(1.f, control_scale * 0.85f);

			const Vec4f ob_origin_ws = to_world * Vec4f(0, 0, 0, 1);
			const Vec4f cam_to_ob = ob_origin_ws - cam_controller.getPosition().toVec4fPoint();

			axis_arrow_segments[0] = LineSegment4f(arrow_origin, arrow_origin + Vec4f(cam_to_ob[0] > 0 ? -arrow_len : arrow_len, 0, 0, 0)); // Put arrows on + or - x axis, facing towards camera.
			axis_arrow_segments[1] = LineSegment4f(arrow_origin, arrow_origin + Vec4f(0, cam_to_ob[1] > 0 ? -arrow_len : arrow_len, 0, 0));
			axis_arrow_segments[2] = LineSegment4f(arrow_origin, arrow_origin + Vec4f(0, 0, cam_to_ob[2] > 0 ? -arrow_len : arrow_len, 0));

			//axis_arrow_segments[3] = LineSegment4f(arrow_origin, arrow_origin + Vec4f(arrow_len, 0, 0, 0)); // Put arrows on + or - x axis, facing towards camera.
			//axis_arrow_segments[4] = LineSegment4f(arrow_origin, arrow_origin + Vec4f(0, arrow_len, 0, 0));
			//axis_arrow_segments[5] = LineSegment4f(arrow_origin, arrow_origin + Vec4f(0, 0, arrow_len, 0));

			for(int i=0; i<NUM_AXIS_ARROWS; ++i)
			{
				axis_arrow_objects[i]->ob_to_world_matrix = OpenGLEngine::arrowObjectTransform(axis_arrow_segments[i].a, axis_arrow_segments[i].b, arrow_len);
				if(ui->glWidget->opengl_engine->isObjectAdded(axis_arrow_objects[i]))
					ui->glWidget->opengl_engine->updateObjectTransformData(*axis_arrow_objects[i]);
			}

			// Update rotation control handle arcs

			const Vec4f arc_centre = use_ob_origin;// opengl_ob->ob_to_world_matrix.getColumn(3);
			const float arc_radius = myMax(0.7f, control_scale * 0.85f * 0.7f); // Make the arcs not stick out so far from the centre as the arrows.

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

				if(ui->glWidget->opengl_engine->isObjectAdded(rot_handle_arc_objects[i]))
					ui->glWidget->opengl_engine->updateObjectTransformData(*rot_handle_arc_objects[i]);
			}
		}
	}
}


bool MainWindow::objectIsInParcelForWhichLoggedInUserHasWritePerms(const WorldObject& ob) const
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


bool MainWindow::objectModificationAllowed(const WorldObject& ob)
{
	if(!this->logged_in_user_id.valid())
	{
		return false;
	}
	else
	{
		return (this->logged_in_user_id == ob.creator_id) || // If the logged in user created the object,
			isGodUser(this->logged_in_user_id) || // Or the user is the 'god' (superadmin) user,
			(!server_worldname.empty() && (server_worldname == this->logged_in_user_name)) || // If this is the personal world of the user:
			objectIsInParcelForWhichLoggedInUserHasWritePerms(ob);
	}
}


bool MainWindow::connectedToUsersPersonalWorldOrGodUser()
{
	if(!this->logged_in_user_id.valid())
	{
		return false;
	}
	else
	{
		return isGodUser(this->logged_in_user_id) || // The logged in user is the 'god' (superadmin) user,
			(!server_worldname.empty() && (server_worldname == this->logged_in_user_name)); // or if this is the personal world of the user:
	}
}


// Similar to objectModificationAllowed() above, but also shows error notifications if modification is not allowed
bool MainWindow::objectModificationAllowedWithMsg(const WorldObject& ob, const std::string& action)
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
			(!server_worldname.empty() && (server_worldname == this->logged_in_user_name)) || // If this is the personal world of the user:
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


void MainWindow::setUpForScreenshot()
{
	if(taking_map_screenshot)
		removeParcelObjects();

	// Highlight requested parcel_id
	if(screenshot_highlight_parcel_id != -1)
	{
		Lock lock(world_state->mutex);

		addParcelObjects();

		auto res = world_state->parcels.find(ParcelID(screenshot_highlight_parcel_id));
		if(res != world_state->parcels.end())
		{
			// Deselect any existing gl objects
			ui->glWidget->opengl_engine->deselectAllObjects();

			this->selected_parcel = res->second;
			ui->glWidget->opengl_engine->selectObject(selected_parcel->opengl_engine_ob);
			ui->glWidget->opengl_engine->setSelectionOutlineColour(PARCEL_OUTLINE_COLOUR);
			ui->glWidget->opengl_engine->setSelectionOutlineWidth(6.0f);
		}
	}

	ui->glWidget->take_map_screenshot = taking_map_screenshot;

	gesture_ui.destroy();

	minimap.destroy();

	done_screenshot_setup = true;
}


static ImageMapUInt8Ref convertQImageToImageMapUInt8(const QImage& qimage)
{
	// NOTE: Qt-saved images were doing weird things with parcel border alpha.  Just copy to an ImageMapUInt8 and do the image saving ourselves.
	const int W = qimage.width();
	const int H = qimage.height();
	ImageMapUInt8Ref map = new ImageMapUInt8(W, H, 3);

	const int px_byte_stride = qimage.depth() / 8; // depth = bits per pixel

	// Copy to ImageMapUInt8
	for(int y=0; y<H; ++y)
	{
		const uint8* scanline = qimage.constScanLine(y);

		for(int x=0; x<W; ++x)
		{
			const QRgb src_px = *(const QRgb*)(scanline + px_byte_stride * x);
			uint8* dst_px = map->getPixel(x, y);
			dst_px[0] = (uint8)qRed(src_px);
			dst_px[1] = (uint8)qGreen(src_px);
			dst_px[2] = (uint8)qBlue(src_px);
		}
	}
	
	return map;
}


// For screenshot bot
void MainWindow::saveScreenshot() // Throws glare::Exception on failure
{
	conPrint("Taking screenshot");

#if QT_VERSION_MAJOR >= 6
	QImage framebuffer = ui->glWidget->grabFramebuffer();
#else
	QImage framebuffer = ui->glWidget->grabFrameBuffer();
#endif

	const int target_viewport_w = taking_map_screenshot ? (screenshot_width_px * 2) : (650 * 2); // Existing screenshots are 650 px x 437 px.
	const int target_viewport_h = taking_map_screenshot ? (screenshot_width_px * 2) : (437 * 2); 
	
	if(framebuffer.width() != target_viewport_w)
		throw glare::Exception("saveScreenshot(): framebuffer width was incorrect: actual: " + toString(framebuffer.width()) + ", target: " + toString(target_viewport_w));
	if(framebuffer.height() != target_viewport_h)
		throw glare::Exception("saveScreenshot(): framebuffer height was incorrect: actual: " + toString(framebuffer.height()) + ", target: " + toString(target_viewport_h));
	
	QImage scaled_img = framebuffer.scaledToWidth(screenshot_width_px, Qt::SmoothTransformation);

	// NOTE: Qt-saved images were doing weird things with parcel border alpha.  Just copy to an ImageMapUInt8 and do the image saving ourselves.
	ImageMapUInt8Ref map = convertQImageToImageMapUInt8(scaled_img);

	JPEGDecoder::save(map, screenshot_output_path, JPEGDecoder::SaveOptions(/*quality=*/95));
}


static void enqueueMessageToSend(ClientThread& client_thread, SocketBufferOutStream& packet)
{
	MessageUtils::updatePacketLengthField(packet);

	client_thread.enqueueDataToSend(packet.buf);
}


// ObLoadingCallbacks interface callback function:
void MainWindow::loadObject(WorldObjectRef ob)
{
	loadModelForObject(ob.ptr());

	loadAudioForObject(ob.ptr());
}


// ObLoadingCallbacks interface callback function:
void MainWindow::unloadObject(WorldObjectRef ob)
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
void MainWindow::newCellInProximity(const Vec3<int>& cell_coords)
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


void MainWindow::tryToMoveObject(WorldObjectRef ob, /*const Matrix4f& tentative_new_to_world*/const Vec4f& desired_new_ob_pos)
{
	Lock lock(world_state->mutex);

	GLObjectRef opengl_ob = this->selected_ob->opengl_engine_ob;
	if(opengl_ob.isNull())
	{
		// conPrint("MainWindow::tryToMoveObject: opengl_ob is NULL");
		return;
	}

	Matrix4f tentative_new_to_world = opengl_ob->ob_to_world_matrix;
	tentative_new_to_world.setColumn(3, desired_new_ob_pos);

	const js::AABBox tentative_new_aabb_ws = ui->glWidget->opengl_engine->getAABBWSForObjectWithTransform(*opengl_ob, tentative_new_to_world);

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
			GLObjectRef new_marker = ui->glWidget->opengl_engine->allocateObject();
			new_marker->mesh_data = this->ob_denied_move_marker->mesh_data; // copy mesh ref from prototype gl ob.
			new_marker->materials = this->ob_denied_move_marker->materials; // copy materials
			new_marker->ob_to_world_matrix = Matrix4f::identity();
			ob_denied_move_markers.push_back(new_marker);

			ui->glWidget->opengl_engine->addObject(new_marker);
		}

		// Remove any surplus edge markers
		while(ob_denied_move_markers.size() > edge_markers.size())
		{
			ui->glWidget->opengl_engine->removeObject(ob_denied_move_markers.back());
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

			ui->glWidget->opengl_engine->updateObjectTransformData(*ob_denied_move_markers[i]);
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


void MainWindow::doMoveObject(WorldObjectRef ob, const Vec3d& new_ob_pos, const js::AABBox& aabb_os)
{
	doMoveAndRotateObject(ob, new_ob_pos, ob->axis, ob->angle, aabb_os, /*summoning_object=*/false);
}


// Sets object velocity to zero also.
void MainWindow::doMoveAndRotateObject(WorldObjectRef ob, const Vec3d& new_ob_pos, const Vec3f& new_axis, float new_angle, const js::AABBox& aabb_os, bool summoning_object)
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
		ui->glWidget->opengl_engine->updateObjectTransformData(*opengl_ob);
	}

	// Update physics object
	if(ob->physics_object.nonNull())
	{
		physics_world->setNewObToWorldTransform(*ob->physics_object, new_ob_pos.toVec4fPoint(), Quatf::fromAxisAndAngle(normalise(ob->axis.toVec4fVector()), ob->angle), useScaleForWorldOb(ob->scale).toVec4fVector());

		physics_world->setLinearAndAngularVelToZero(*ob->physics_object);
	}

	// Update in Indigo view
	ui->indigoView->objectTransformChanged(*ob);

	// Set a timer to call updateObjectEditorObTransformSlot() later. Not calling this every frame avoids stutters with webviews playing back videos interacting with Qt updating spinboxes.
	if(!update_ob_editor_transform_timer->isActive())
		update_ob_editor_transform_timer->start(/*msec=*/50);

	
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
			ui->glWidget->opengl_engine->setLightPos(light, new_ob_pos.toVec4fPoint());
		}
	}


	if(ob.ptr() == this->selected_ob.ptr())
		updateSelectedObjectPlacementBeam();
}


void MainWindow::checkForLODChanges()
{
	ZoneScoped; // Tracy profiler

	if(world_state.isNull())
		return;
		
	//Timer timer;
	{
		Lock lock(this->world_state->mutex);

		const Vec4f cam_pos = cam_controller.getPosition().toVec4fPoint();
		const float load_distance2_ = this->load_distance2;

		glare::FastIterMapValueInfo<UID, WorldObjectRef>* const objects_data = this->world_state->objects.vector.data();
		const size_t objects_size                                            = this->world_state->objects.vector.size();

		// Process just some of the objects each frame, to reduce CPU time and ease pressure on the cache.
		const size_t num_slices = 4;
		const size_t slice = frame_num % num_slices;
		const size_t begin_i = myMin(objects_size, Maths::roundedUpDivide(objects_size, num_slices) * slice      );
		const size_t end_i   = myMin(objects_size, Maths::roundedUpDivide(objects_size, num_slices) * (slice + 1));

		//conPrint("checking slice from " + toString(begin_i) + " to " + toString(end_i));
		for(size_t i=begin_i; i<end_i; ++i)
		{
			if(i + 16 < end_i)
				_mm_prefetch((const char*)(&objects_data[i + 16].value->centroid_ws), _MM_HINT_T0);

			WorldObject* const ob = objects_data[i].value.ptr();

			const float cam_to_ob_d2 = ob->getCentroidWS().getDist2(cam_pos);
			if(cam_to_ob_d2 > load_distance2_) // If object is out of load distance:
			{
				if(ob->in_proximity) // If an object was in proximity to the camera, and moved out of load distance:
				{
					unloadObject(ob);
					ob->in_proximity = false;
				}
			}
			else // Else if object is within load distance:
			{
				const int lod_level = ob->getLODLevel(cam_to_ob_d2);

				if((lod_level != ob->current_lod_level)/* || ob->opengl_engine_ob.isNull()*/)
				{
					loadModelForObject(ob);
					ob->current_lod_level = lod_level;
					// conPrint("Changing LOD level for object " + ob->uid.toString() + " to " + toString(lod_level));
				}

				if(!ob->in_proximity) // If an object was out of load distance, and moved within load distance:
				{
					ob->in_proximity = true;
					loadModelForObject(ob);
					ob->current_lod_level = lod_level;
				}
			}
		}
	} // End lock scope
	//conPrint("checkForLODChanges took " + timer.elapsedStringMSWIthNSigFigs(4) + " (" + toString(world_state->objects.size()) + " obs)");
}


void MainWindow::checkForAudioRangeChanges()
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

			if(!ob->audio_source_url.empty() || ob->audio_source.nonNull())
			{
				const float dist2 = cam_pos.getDist2(ob->pos.toVec4fPoint());
				const float max_audio_dist2 = Maths::square(maxAudioDistForSourceVolFactor(ob->audio_volume)); // MAX_AUDIO_DIST
				if(ob->audio_source.nonNull())
				{
					if(dist2 > max_audio_dist2)
					{
						// conPrint("Object out of range, removing audio object '" + ob->audio_source->debugname + "'.");
						audio_engine.removeSource(ob->audio_source);
						ob->audio_source = NULL;
						ob->audio_state = WorldObject::AudioState_NotLoaded;
						//ob->loaded_audio_source_url.clear();
					}
				}
				else // Else if audio source is NULL:
				{
					assert(!ob->audio_source_url.empty()); // The object has an audio URL to play:

					if(dist2 <= max_audio_dist2)
					{
						// conPrint("Object in range, loading audio object.");
						loadAudioForObject(ob);
					}
				}
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


void MainWindow::processLoading()
{
	//double frame_loading_time = 0;
	//std::vector<std::string> loading_times; // TEMP just for profiling/debugging
	if(world_state.nonNull())
	{
		PERFORMANCEAPI_INSTRUMENT("process loading msgs");

		// Process ModelLoadedThreadMessages and TextureLoadedThreadMessages until we have consumed a certain amount of time.
		// We don't want to do too much at one time or it will cause hitches.
		// We'll alternate between processing model loaded and texture loaded messages, using process_model_loaded_next.
		// We alternate for fairness.
		const double MAX_LOADING_TIME = 0.005;// 5 ms
		Timer loading_timer;
		//int max_items_to_process = 10;
		//int num_items_processed = 0;
		
		// Also limit to a total number of bytes of data uploaded to OpenGL / the GPU per frame.
		size_t total_bytes_uploaded = 0;
		const size_t max_total_upload_bytes = 1024 * 1024;

		int num_models_loaded = 0;
		int num_textures_loaded = 0;
		//while((cur_loading_mesh_data.nonNull() || !model_loaded_messages_to_process.empty() || !texture_loaded_messages_to_process.empty()) && (loading_timer.elapsed() < MAX_LOADING_TIME))
		while((tex_loading_progress.loadingInProgress() || cur_loading_mesh_data.nonNull() || !model_loaded_messages_to_process.empty() || !texture_loaded_messages_to_process.empty()) && 
			(total_bytes_uploaded < max_total_upload_bytes) && 
			(loading_timer.elapsed() < MAX_LOADING_TIME) //&&
			/*(num_items_processed < max_items_to_process)*/)
		{
			//num_items_processed++;

			// If we are still loading some mesh data into OpenGL (uploading to GPU):
			if(cur_loading_mesh_data.nonNull() && cur_loading_voxel_ob.isNull()) // If we are currently loading a non-voxel mesh:
			{
				Timer load_item_timer;
				const std::string loading_item_name = cur_loading_lod_model_url;

				// Upload a chunk of data to the GPU
				try
				{
					ui->glWidget->opengl_engine->partialLoadOpenGLMeshDataIntoOpenGL(*ui->glWidget->opengl_engine->vert_buf_allocator, *cur_loading_mesh_data, mesh_data_loading_progress,
						total_bytes_uploaded, max_total_upload_bytes);
				}
				catch(glare::Exception& e)
				{
					logMessage("Error while loading mesh '" + loading_item_name + "' into OpenGL: " + e.what());
					cur_loading_mesh_data = NULL;
				}

				//logMessage("Loaded a chunk of mesh '" + cur_loading_lod_model_url + "': " + mesh_data_loading_progress.summaryString());

				if(mesh_data_loading_progress.done())
				{
					assert(!cur_loading_lod_model_url.empty());
					//logMessage("Finished loading mesh '" + cur_loading_lod_model_url + "'.");


					// Now that this model is loaded, remove from models_processing set.
					// If the model is unloaded, then this will allow it to be reprocessed and reloaded.
					ModelProcessingKey key(cur_loading_lod_model_url, cur_loading_dynamic_physics_shape);
					models_processing.erase(key);


					// Add meshes to mesh manager
					Reference<MeshData> mesh_data						= mesh_manager.insertMesh(cur_loading_lod_model_url, cur_loading_mesh_data);
					Reference<PhysicsShapeData> physics_shape_data		= mesh_manager.insertPhysicsShape(MeshManagerPhysicsShapeKey(cur_loading_lod_model_url, /*is dynamic=*/cur_loading_dynamic_physics_shape), cur_loading_physics_shape);

					// Data is uploaded - assign to any waiting objects
					const int loaded_model_lod_level = WorldObject::getLODLevelForURL(cur_loading_lod_model_url/*message->lod_model_url*/);

					// Assign the loaded model for any objects using waiting for this model:
					Lock lock(this->world_state->mutex);

					const ModelProcessingKey model_loading_key(cur_loading_lod_model_url, cur_loading_dynamic_physics_shape);
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

								//ob->aabb_os = cur_loading_mesh_data->aabb_os;

								if(ob->in_proximity)
								{
									const int ob_lod_level = ob->getLODLevel(cam_controller.getPosition());
									const int ob_model_lod_level = myClamp(ob_lod_level, 0, ob->max_model_lod_level);
								
									// Check the object wants this particular LOD level model right now:
									const std::string current_desired_model_LOD_URL = ob->getLODModelURLForLevel(ob->model_url, ob_model_lod_level);
									if((current_desired_model_LOD_URL == cur_loading_lod_model_url) && (ob->isDynamic() == cur_loading_dynamic_physics_shape))
									{
										try
										{
											if(!isFinite(ob->angle) || !ob->axis.isFinite())
												throw glare::Exception("Invalid angle or axis");

											removeAndDeleteGLObjectsForOb(*ob); // Remove any existing OpenGL model

											// Remove previous physics object. If this is a dynamic or kinematic object, don't delete old object though, unless it's a placeholder.
											if(ob->physics_object.nonNull() && (ob->using_placeholder_model || !(ob->physics_object->dynamic || ob->physics_object->kinematic)))
											{
												destroyVehiclePhysicsControllingObject(ob); // Destroy any vehicle controller controlling this object, as vehicle controllers have pointers to physics bodies, which we can't leave dangling.
												physics_world->removeObject(ob->physics_object);
												ob->physics_object = NULL;
											}

											// Create GLObject and PhysicsObjects for this world object.  The loaded mesh should be in the mesh_manager.
											const Matrix4f ob_to_world_matrix = obToWorldMatrix(*ob);

											ob->opengl_engine_ob = ModelLoading::makeGLObjectForMeshDataAndMaterials(*ui->glWidget->opengl_engine, cur_loading_mesh_data, ob_lod_level, ob->materials, ob->lightmap_url,
												*resource_manager, ob_to_world_matrix);

											const float current_time = (float)Clock::getTimeSinceInit();
											const bool use_materialise_effect = ob->use_materialise_effect_on_load && (current_time - ob->materialise_effect_start_time < 2.0f);

											for(size_t z=0; z<ob->opengl_engine_ob->materials.size(); ++z)
											{
												ob->opengl_engine_ob->materials[z].materialise_effect = use_materialise_effect;
												ob->opengl_engine_ob->materials[z].materialise_start_time = ob->materialise_effect_start_time;
											}

											mesh_data->meshDataBecameUsed();
											ob->mesh_manager_data = mesh_data; // Hang on to a reference to the mesh data, so when object-uses of it are removed, it can be removed from the MeshManager with meshDataBecameUnused().

											physics_shape_data->shapeDataBecameUsed();
											ob->mesh_manager_shape_data = physics_shape_data; // Likewise for the physics mesh data.

											if(ob->physics_object.isNull()) // if object was dynamic, we didn't unload its physics object above.
											{
												ob->physics_object = new PhysicsObject(/*collidable=*/ob->isCollidable());

												PhysicsShape use_shape = this->cur_loading_physics_shape;
												if(ob->centre_of_mass_offset_os != Vec3f(0.f))
													use_shape = PhysicsWorld::createCOMOffsetShapeForShape(this->cur_loading_physics_shape, ob->centre_of_mass_offset_os.toVec4fVector());

												ob->physics_object->shape = use_shape;
												ob->physics_object->userdata = ob;
												ob->physics_object->userdata_type = 0;
												ob->physics_object->ob_uid = ob->uid;
												ob->physics_object->pos = ob->pos.toVec4fPoint();
												ob->physics_object->rot = Quatf::fromAxisAndAngle(normalise(ob->axis), ob->angle);
												ob->physics_object->scale = useScaleForWorldOb(ob->scale);

												ob->physics_object->kinematic = !ob->script.empty();
												ob->physics_object->dynamic = ob->isDynamic();
												ob->physics_object->is_sphere = ob->model_url == "Icosahedron_obj_136334556484365507.bmesh";
												ob->physics_object->is_cube = ob->model_url == "Cube_obj_11907297875084081315.bmesh";

												ob->physics_object->mass = ob->mass;
												ob->physics_object->friction = ob->friction;
												ob->physics_object->restitution = ob->restitution;

												physics_world->addObject(ob->physics_object);
											}


											ob->loaded_model_lod_level = loaded_model_lod_level;//message->model_lod_level;

											assert(ob->opengl_engine_ob->mesh_data->vbo_handle.valid());

											//loaded_size_B = ob->opengl_engine_ob->mesh_data->getTotalMemUsage().geom_gpu_usage;

											assignedLoadedOpenGLTexturesToMats(ob, *ui->glWidget->opengl_engine, *resource_manager);

											if(hasPrefix(ob->content, "biome: park"))
											{
												ob->opengl_engine_ob->draw_to_mask_map = true;

												if(this->terrain_system.nonNull())
													this->terrain_system->invalidateVegetationMap(ob->getAABBWS());
											}

											ui->glWidget->opengl_engine->addObject(ob->opengl_engine_ob);


											ui->indigoView->objectAdded(*ob, *this->resource_manager);

											loadScriptForObject(ob); // Load any script for the object.

											// If we replaced the model for selected_ob, reselect it in the OpenGL engine
											if(this->selected_ob == ob)
											{
												ui->glWidget->opengl_engine->selectObject(ob->opengl_engine_ob);
												updateSelectedObjectPlacementBeam(); // We may have changed from a placeholder mesh, which has a different to-world matrix due to cube offset.
												// So update the position of the object placement beam and axis arrows etc.
											}
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
					auto waiting_av_res = this->loading_model_URL_to_avatar_UID_map.find(cur_loading_lod_model_url);
					if(waiting_av_res != this->loading_model_URL_to_avatar_UID_map.end())
					{
						std::set<UID>& waiting_avatars = waiting_av_res->second;
						for(auto it = waiting_avatars.begin(); it != waiting_avatars.end(); ++it)
						{
							const UID waiting_uid = *it;

							auto res2 = this->world_state->avatars.find(waiting_uid);
							if(res2 != this->world_state->avatars.end())
							{
								Avatar* av = res2->second.ptr();
						
								const bool our_avatar = av->uid == this->client_avatar_uid;
								if((cam_controller.thirdPersonEnabled() || !our_avatar)) // Don't load graphics for our avatar
								{
									const int av_lod_level = av->getLODLevel(cam_controller.getPosition());

									// Check the avatar wants this particular LOD level model right now:
									const std::string current_desired_model_LOD_URL = av->getLODModelURLForLevel(av->avatar_settings.model_url, av_lod_level);
									if(current_desired_model_LOD_URL == cur_loading_lod_model_url)
									{
										try
										{
											// Remove any existing OpenGL and physics model
											removeAndDeleteGLObjectForAvatar(*av);

											// Create GLObject for this avatar
											const Matrix4f ob_to_world_matrix = obToWorldMatrix(*av);

											av->graphics.skinned_gl_ob = ModelLoading::makeGLObjectForMeshDataAndMaterials(*ui->glWidget->opengl_engine, cur_loading_mesh_data, av_lod_level, av->avatar_settings.materials, /*lightmap_url=*/std::string(),
												*resource_manager, ob_to_world_matrix);

											// Enable materalise effect if needed
											const float current_time = (float)Clock::getTimeSinceInit();
											const bool use_materialise_effect = av->use_materialise_effect_on_load && (current_time - av->materialise_effect_start_time < 2.0f);

											for(size_t z=0; z<av->graphics.skinned_gl_ob->materials.size(); ++z)
											{
												av->graphics.skinned_gl_ob->materials[z].materialise_effect = use_materialise_effect;
												av->graphics.skinned_gl_ob->materials[z].materialise_start_time = av->materialise_effect_start_time;
											}

											mesh_data->meshDataBecameUsed();
											av->mesh_data = mesh_data; // Hang on to a reference to the mesh data, so when object-uses of it are removed, it can be removed from the MeshManager with meshDataBecameUnused().

											// Load animation data for ready-player-me type avatars
											if(!av->graphics.skinned_gl_ob->mesh_data->animation_data.retarget_adjustments_set)
											{
												FileInStream file(base_dir_path + "/resources/extracted_avatar_anim.bin");
												av->graphics.skinned_gl_ob->mesh_data->animation_data.loadAndRetargetAnim(file);
											}

											av->graphics.build();
										
											//TEMP av->loaded_lod_level = ob_lod_level;

											assert(av->graphics.skinned_gl_ob->mesh_data->vbo_handle.valid());

											assignedLoadedOpenGLTexturesToMats(av, *ui->glWidget->opengl_engine, *resource_manager);

											ui->glWidget->opengl_engine->addObject(av->graphics.skinned_gl_ob);

											// If we just loaded the graphics for our own avatar, see if there is a gesture animation we should be playing, and if so, play it.
											if(our_avatar)
											{
												std::string gesture_name;
												bool animate_head, loop_anim;
												if(gesture_ui.getCurrentGesturePlaying(gesture_name, animate_head, loop_anim)) // If we should be playing a gesture according to the UI:
												{
													av->graphics.performGesture(world_state->getCurrentGlobalTime(), gesture_name, animate_head, loop_anim);
												}
											}
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

					cur_loading_mesh_data = NULL;
					cur_loading_lod_model_url.clear();
					cur_loading_physics_shape = PhysicsShape();
				} // end if(mesh_data_loading_progress.done())


				//const std::string loading_item = "Initialised load of " + (message->voxel_ob.nonNull() ? "voxels" : message->lod_model_url);
				//loading_times.push_back(doubleToStringNSigFigs(load_item_timer.elapsed() * 1.0e3, 3) + " ms, loaded chunk of " + loading_item_name + ": " + mesh_data_loading_progress.summaryString());
			}
			else if(cur_loading_voxel_ob.nonNull()) // else if we are currently loading a voxel mesh:
			{
				Timer load_item_timer;

				// Upload a chunk of data to the GPU
				ui->glWidget->opengl_engine->partialLoadOpenGLMeshDataIntoOpenGL(*ui->glWidget->opengl_engine->vert_buf_allocator, *cur_loading_mesh_data, mesh_data_loading_progress, 
					total_bytes_uploaded, max_total_upload_bytes);

				//logMessage("Loaded a chunk of voxel mesh: " + mesh_data_loading_progress.summaryString());

				if(mesh_data_loading_progress.done())
				{
					//logMessage("Finished loading voxel mesh.");

					WorldObjectRef voxel_ob = this->cur_loading_voxel_ob;

					if(voxel_ob->in_proximity)
					{
						const int ob_lod_level = voxel_ob->getLODLevel(cam_controller.getPosition());
						const int ob_model_lod_level = myClamp(ob_lod_level, 0, voxel_ob->max_model_lod_level);

						// Check the object wants this particular LOD level model right now:
						if(ob_model_lod_level == cur_loading_voxel_ob_model_lod_level)
						{
							removeAndDeleteGLObjectsForOb(*voxel_ob); // Remove any existing OpenGL model

							// Remove previous physics object. If this is a dynamic or kinematic object, don't delete old object though, unless it's a placeholder.
							if(voxel_ob->physics_object.nonNull() && (voxel_ob->using_placeholder_model || !(voxel_ob->physics_object->dynamic || voxel_ob->physics_object->kinematic)))
							{
								destroyVehiclePhysicsControllingObject(voxel_ob.ptr()); // Destroy any vehicle controller controlling this object, as vehicle controllers have pointers to physics bodies, which we can't leave dangling.
								physics_world->removeObject(voxel_ob->physics_object);
								voxel_ob->physics_object = NULL;
							}

							const Matrix4f ob_to_world_matrix = obToWorldMatrix(*voxel_ob);
							const Matrix4f use_ob_to_world_matrix = ob_to_world_matrix * Matrix4f::uniformScaleMatrix((float)cur_loading_voxel_subsample_factor/*message->subsample_factor*/);

							if(voxel_ob->physics_object.isNull())
							{
								PhysicsObjectRef physics_ob = new PhysicsObject(/*collidable=*/voxel_ob->isCollidable());

								PhysicsShape use_shape = cur_loading_physics_shape;
								if(voxel_ob->centre_of_mass_offset_os != Vec3f(0.f))
									use_shape = PhysicsWorld::createCOMOffsetShapeForShape(cur_loading_physics_shape, voxel_ob->centre_of_mass_offset_os.toVec4fVector());

								physics_ob->shape = use_shape;

								physics_ob->userdata = voxel_ob.ptr();
								physics_ob->userdata_type = 0;
								physics_ob->ob_uid = voxel_ob->uid;
								physics_ob->pos = voxel_ob->pos.toVec4fPoint();
								physics_ob->rot = Quatf::fromAxisAndAngle(normalise(voxel_ob->axis), voxel_ob->angle);
								physics_ob->scale = useScaleForWorldOb(voxel_ob->scale);
								physics_ob->kinematic = !voxel_ob->script.empty();
								physics_ob->dynamic = voxel_ob->isDynamic();

								physics_ob->mass = voxel_ob->mass;
								physics_ob->friction = voxel_ob->friction;
								physics_ob->restitution = voxel_ob->restitution;

								voxel_ob->physics_object = physics_ob;

								physics_world->addObject(physics_ob);
							}

							const float current_time = (float)Clock::getTimeSinceInit();
							const bool use_materialise_effect = voxel_ob->use_materialise_effect_on_load && (current_time - voxel_ob->materialise_effect_start_time < 2.0f);

							GLObjectRef opengl_ob = ui->glWidget->opengl_engine->allocateObject();
							opengl_ob->mesh_data = cur_loading_mesh_data;
							opengl_ob->materials.resize(voxel_ob->materials.size());
							for(uint32 i=0; i<voxel_ob->materials.size(); ++i)
							{
								ModelLoading::setGLMaterialFromWorldMaterial(*voxel_ob->materials[i], ob_lod_level, voxel_ob->lightmap_url, *this->resource_manager, opengl_ob->materials[i]);
								opengl_ob->materials[i].gen_planar_uvs = true;

								opengl_ob->materials[i].materialise_effect = use_materialise_effect;
								opengl_ob->materials[i].materialise_start_time = voxel_ob->materialise_effect_start_time;
							}
							opengl_ob->ob_to_world_matrix = use_ob_to_world_matrix;

							voxel_ob->opengl_engine_ob = opengl_ob;
							

							assert(opengl_ob->mesh_data->vbo_handle.valid());
							//if(!opengl_ob->mesh_data->vbo_handle.valid()) // If this data has not been loaded into OpenGL yet:
							//	OpenGLEngine::loadOpenGLMeshDataIntoOpenGL(ui->glWidget->opengl_engine->vert_buf_allocator, *opengl_ob->mesh_data); // Load mesh data into OpenGL

							//loaded_size_B = opengl_ob->mesh_data->getTotalMemUsage().geom_gpu_usage;

							

							// Add this object to the GL engine and physics engine.
							if(!ui->glWidget->opengl_engine->isObjectAdded(opengl_ob))
							{
								assignedLoadedOpenGLTexturesToMats(voxel_ob.ptr(), *ui->glWidget->opengl_engine, *resource_manager);

								if(hasPrefix(voxel_ob->content, "biome: park"))
								{
									voxel_ob->opengl_engine_ob->draw_to_mask_map = true;

									if(this->terrain_system.nonNull())
										this->terrain_system->invalidateVegetationMap(voxel_ob->getAABBWS());
								}

								ui->glWidget->opengl_engine->addObject(opengl_ob);

								ui->indigoView->objectAdded(*voxel_ob, *this->resource_manager);

								loadScriptForObject(voxel_ob.ptr()); // Load any script for the object.
							}

							voxel_ob->loaded_model_lod_level = cur_loading_voxel_ob_model_lod_level; //  message->voxel_ob_lod_level/*model_lod_level*/;

							// If we replaced the model for selected_ob, reselect it in the OpenGL engine
							if(this->selected_ob == voxel_ob)
							{
								ui->glWidget->opengl_engine->selectObject(voxel_ob->opengl_engine_ob);
								updateSelectedObjectPlacementBeam(); // We may have changed from a placeholder mesh, which has a different to-world matrix due to cube offset.
								// So update the position of the object placement beam and axis arrows etc.
							}
						}
					}	

					cur_loading_mesh_data = NULL;
					cur_loading_voxel_ob = NULL;
					cur_loading_physics_shape = PhysicsShape();
				} // end if(mesh_data_loading_progress.done())

				//loading_times.push_back(doubleToStringNSigFigs(load_item_timer.elapsed() * 1.0e3, 3) + " ms, loaded chunk of voxel mesh: " + mesh_data_loading_progress.summaryString());
			}
			// If we are still loading some texture data into OpenGL (uploading to GPU):
			else if(tex_loading_progress.loadingInProgress())
			{
				Timer load_item_timer;

				// Upload a chunk of data to the GPU
				try
				{
					TextureLoading::partialLoadTextureIntoOpenGL(ui->glWidget->opengl_engine, tex_loading_progress, total_bytes_uploaded, max_total_upload_bytes);
				}
				catch(glare::Exception& e)
				{
					logMessage("Error while loading texture '" + tex_loading_progress.path + "' into OpenGL: " + e.what());
					tex_loading_progress.tex_data = NULL;
					tex_loading_progress.opengl_tex = NULL;
				}

				if(tex_loading_progress.done() || !tex_loading_progress.loadingInProgress())
				{
					if(cur_loading_terrain_map.nonNull() && terrain_system.nonNull())
						terrain_system->handleTextureLoaded(tex_loading_progress.path, cur_loading_terrain_map);


					tex_loading_progress.tex_data = NULL;
					tex_loading_progress.opengl_tex = NULL;

					// conPrint("Finished loading texture '" + tex_loading_progress.path + "' into OpenGL");
					
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
					num_models_loaded++;

					try
					{
						if(message->voxel_ob_uid.valid()) // If we loaded a voxel object:
						{
							Lock lock(this->world_state->mutex);

							// Handle loading a voxel group
							auto res = world_state->objects.find(message->voxel_ob_uid);
							if(res != world_state->objects.end())
							{
								WorldObjectRef voxel_ob = res.getValue().ptr();

								if(voxel_ob->in_proximity) // Object may be out of load distance now that it has actually been loaded.
								{
									if(!message->gl_meshdata->vbo_handle.valid()) // Mesh data may already be loaded into OpenGL, in that case we don't need to start loading it.
									{
										this->cur_loading_mesh_data = message->gl_meshdata;
										this->cur_loading_voxel_ob = voxel_ob;
										this->cur_loading_voxel_subsample_factor = message->subsample_factor;
										this->cur_loading_physics_shape = message->physics_shape;
										this->cur_loading_voxel_ob_model_lod_level = message->voxel_ob_model_lod_level;
										ui->glWidget->opengl_engine->initialiseMeshDataLoadingProgress(*this->cur_loading_mesh_data, mesh_data_loading_progress);

										//logMessage("Initialised loading of voxel mesh: " + mesh_data_loading_progress.summaryString());
									}
									else
									{
										//logMessage("Voxel mesh '" + message->lod_model_url + "' was already loaded into OpenGL");
									}

								} // End proximity_loader.isObjectInLoadProximity()
							}
						}
						else // Else didn't load voxels, loaded a model:
						{
							// Start loading mesh data into OpenGL.
							if(!message->gl_meshdata->vbo_handle.valid()) // Mesh data may already be loaded into OpenGL, in that case we don't need to start loading it.
							{
								this->cur_loading_mesh_data = message->gl_meshdata;
								this->cur_loading_physics_shape = message->physics_shape;
								this->cur_loading_lod_model_url = message->lod_model_url;
								this->cur_loading_dynamic_physics_shape = message->built_dynamic_physics_ob;
								ui->glWidget->opengl_engine->initialiseMeshDataLoadingProgress(*this->cur_loading_mesh_data, mesh_data_loading_progress);

								//logMessage("Initialised loading of mesh '" + message->lod_model_url + "': " + mesh_data_loading_progress.summaryString());
							}
							else
							{
								//logMessage("Mesh '" + message->lod_model_url + "' was already loaded into OpenGL");
							}
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
					num_textures_loaded++;

					this->cur_loading_terrain_map = message->terrain_map;
					this->cur_loading_tex_is_minimap_tile = message->is_minimap_tile;

					try
					{
						// NOTE: bit of a hack, assuming fancy filtering (trilinear) is wanted if we have mip levels.
						const OpenGLTexture::Filtering filtering = (message->texture_data->num_mip_levels > 1) ? OpenGLTexture::Filtering_Fancy : OpenGLTexture::Filtering_Bilinear;
						const OpenGLTexture::Wrapping wrapping = message->is_minimap_tile ? OpenGLTexture::Wrapping_Clamp : OpenGLTexture::Wrapping_Repeat; // Assume repeating in general

						TextureLoading::initialiseTextureLoadingProgress(message->tex_path, ui->glWidget->opengl_engine, OpenGLTextureKey(message->tex_key), message->use_sRGB, filtering, wrapping,
							message->texture_data, this->tex_loading_progress);
					}
					catch(glare::Exception&)
					{
						this->tex_loading_progress.tex_data = NULL;
						this->tex_loading_progress.opengl_tex = NULL;
					}

					//conPrint("textureLoaded took                " + timer.elapsedStringNSigFigs(5));
					//size_t tex_size_B = 0;
					//{
					//	Reference<OpenGLTexture> tex = ui->glWidget->opengl_engine->getTextureIfLoaded(OpenGLTextureKey(message->tex_key), /*use srgb=*/true);
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

		this->last_model_and_tex_loading_time = loading_timer.elapsed();
	}
}


// For visualising physics ownership
void MainWindow::updateDiagnosticAABBForObject(WorldObject* ob)
{
	if(ob->opengl_engine_ob.nonNull())
	{
		if(!isObjectPhysicsOwned(*ob, world_state->getCurrentGlobalTime())) //   ob->physics_owner_id == std::numeric_limits<uint32>::max()) // If object is unowned:
		{
			// Remove any existing visualisation AABB.
			if(ob->diagnostics_gl_ob.nonNull())
			{
				ui->glWidget->opengl_engine->removeObject(ob->diagnostics_gl_ob);
				ob->diagnostics_gl_ob = NULL;
			}
			if(ob->diagnostics_unsmoothed_gl_ob.nonNull())
			{
				ui->glWidget->opengl_engine->removeObject(ob->diagnostics_unsmoothed_gl_ob);
				ob->diagnostics_unsmoothed_gl_ob = NULL;
			}

			if(ob->diagnostic_text_view.nonNull())
			{
				this->gl_ui->removeWidget(ob->diagnostic_text_view);
				ob->diagnostic_text_view->destroy();
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
					ob->diagnostics_gl_ob = ui->glWidget->opengl_engine->makeAABBObject(Vec4f(0,0,0,1), Vec4f(1,1,1,1), col);
					ui->glWidget->opengl_engine->addObject(ob->diagnostics_gl_ob);
				}
			
				ob->diagnostics_gl_ob->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3(col[0], col[1], col[2]));
				ui->glWidget->opengl_engine->objectMaterialsUpdated(*ob->diagnostics_gl_ob);
				ob->diagnostics_gl_ob->ob_to_world_matrix = to_world;
				ui->glWidget->opengl_engine->updateObjectTransformData(*ob->diagnostics_gl_ob);
			}

			// Show bounding box with unsmoothed transformation
			{
				const Matrix4f to_world = (ob->physics_object.nonNull() ? ob->physics_object->getObToWorldMatrix() : Matrix4f::identity()) * OpenGLEngine::AABBObjectTransform(ob->opengl_engine_ob->mesh_data->aabb_os.min_, ob->opengl_engine_ob->mesh_data->aabb_os.max_);

				if(ob->diagnostics_unsmoothed_gl_ob.isNull())
				{
					ob->diagnostics_unsmoothed_gl_ob = ui->glWidget->opengl_engine->makeAABBObject(Vec4f(0,0,0,1), Vec4f(1,1,1,1), col);
					ui->glWidget->opengl_engine->addObject(ob->diagnostics_unsmoothed_gl_ob);
				}

				ob->diagnostics_unsmoothed_gl_ob->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3(col[0] + 0.5f, col[1], col[2]));
				ui->glWidget->opengl_engine->objectMaterialsUpdated(*ob->diagnostics_gl_ob);
				ob->diagnostics_unsmoothed_gl_ob->ob_to_world_matrix = to_world;
				ui->glWidget->opengl_engine->updateObjectTransformData(*ob->diagnostics_unsmoothed_gl_ob);
			}


			const std::string diag_text = "physics_owner_id: " + toString(ob->physics_owner_id) + " since " + doubleToStringNSigFigs(world_state->getCurrentGlobalTime() - ob->last_physics_ownership_change_global_time, 2) + " s";
			const Vec2f dims(0.4f, 0.05f);
			if(ob->diagnostic_text_view.isNull())
			{
				ob->diagnostic_text_view = new GLUITextView();
				ob->diagnostic_text_view->create(*this->gl_ui, this->ui->glWidget->opengl_engine, diag_text, Vec2f(0.f, 0.f), dims, "");
			}
			else
			{
				ob->diagnostic_text_view->setText(*this->gl_ui, diag_text);

				Vec2f normed_coords;
				const bool visible = getGLUICoordsForPoint((aabb_min_ws + aabb_max_ws) * 0.5f, normed_coords);
				if(visible)
				{
					Vec2f botleft(normed_coords.x, normed_coords.y);

					ob->diagnostic_text_view->setPosAndDims(botleft, dims);
				}
			}
		}
	}
}


void MainWindow::updateObjectsWithDiagnosticVis()
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


void MainWindow::timerEvent(QTimerEvent* event)
{
	PERFORMANCEAPI_INSTRUMENT("timerEvent");
	ZoneScoped; // Tracy profiler

	if(closing || in_CEF_message_loop)
		return;

	// We don't want to do the closeEvent stuff in the CEF message loop.  
	// If we got a close event in there, handle it now when we're in the main message loop, and not the CEF message loop.
	assert(!in_CEF_message_loop);
	if(should_close)
	{
		should_close = false;
		this->close();
		return;
	}

	Timer timerEvent_timer;

	in_CEF_message_loop = true;
	CEF::doMessageLoopWork();
	in_CEF_message_loop = false;


	ui->glWidget->makeCurrent(); // Need to make this gl widget context current, before we execute OpenGL calls in processLoading.


	// If we are connected to a server, send a UDP packet to it occasionally, so the server can work out which UDP port
	// we are listening on.
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

	mesh_manager.trimMeshMemoryUsage();


	{
		Lock lock(particles_creation_buf_mutex);
		
		for(size_t i=0; i<this->particles_creation_buf.size(); ++i)
			this->particle_manager->addParticle(this->particles_creation_buf[i]);
		
		this->particles_creation_buf.clear();
	}



	processLoading();
	
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

	while(!load_item_queue.empty() &&  // While there are items to remove from the load item queue,
		(model_and_texture_loader_task_manager.getNumUnfinishedTasks() < 32) &&  // and we don't have too many tasks queued and ready to be executed by the task manager
		(msg_queue.size() + model_loaded_messages_to_process.size() + texture_loaded_messages_to_process.size() < 10) // And we don't have too many completed load tasks:
		)
	{
		// Pop a task from the load item queue, and pass it to the model_and_texture_loader_task_manager.
		LoadItemQueueItem item = load_item_queue.dequeueFront(); 

		// Discard task if it is now too far from the camera.  Do this so we don't load e.g. high detail models when we
		// are no longer close to them.
		const float dist_from_item = cam_controller.getPosition().toVec4fPoint().getDist(item.pos);
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
					assert(models_processing.count(key) > 0);
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

	checkForLODChanges();
	
	gesture_ui.think();
	hud_ui.think();
	minimap.think();

	updateObjectsWithDiagnosticVis();


	// Update info UI with stuff like drawing the URL for objects with target URLs, and showing the 'press [E] to enter vehicle' message.
	// Don't do this if the mouse cursor is hidden, because that implies we are click-dragging to change the camera orientation, and we don't want to show mouse over messages while doing that.
	if(!ui->glWidget->isCursorHidden())
	{
		const QPoint widget_pos = ui->glWidget->mapFromGlobal(QCursor::pos());
		updateInfoUIForMousePosition(widget_pos, /*mouse_event=*/NULL);
	}

	// Update AABB visualisation, if we are showing one.
	if(aabb_os_vis_gl_ob.nonNull() && selected_ob.nonNull())
	{
		aabb_os_vis_gl_ob->ob_to_world_matrix = selected_ob->obToWorldMatrix() * OpenGLEngine::AABBObjectTransform(selected_ob->getAABBOS().min_, selected_ob->getAABBOS().max_);
		ui->glWidget->opengl_engine->updateObjectTransformData(*aabb_os_vis_gl_ob);
	}
	if(aabb_ws_vis_gl_ob.nonNull() && selected_ob.nonNull())
	{
		aabb_ws_vis_gl_ob->ob_to_world_matrix = OpenGLEngine::AABBObjectTransform(selected_ob->getAABBWS().min_, selected_ob->getAABBWS().max_);
		ui->glWidget->opengl_engine->updateObjectTransformData(*aabb_ws_vis_gl_ob);
	}

	updateDiagnostics();
	
	updateStatusBar();
	
	const double dt = time_since_last_timer_ev.elapsed();
	time_since_last_timer_ev.reset();
	const double global_time = world_state.nonNull() ? this->world_state->getCurrentGlobalTime() : 0.0; // Used as input into script functions

	// Set current animation frame for objects with animated textures
	//double animated_tex_time = 0;
	if(world_state.nonNull())
	{
		PERFORMANCEAPI_INSTRUMENT("set anim data");
		ZoneScopedN("set anim data"); // Tracy profiler
		Timer timer;
		//Timer tex_upload_timer;
		//tex_upload_timer.pause();

		int num_gif_textures_processed = 0;
		int num_mp4_textures_processed = 0;

		const double anim_time = total_timer.elapsed();

		{
			Lock lock(this->world_state->mutex); // NOTE: This lock needed?

			for(auto it = this->obs_with_animated_tex.begin(); it != this->obs_with_animated_tex.end(); ++it)
			{
				WorldObject* ob = it->ptr();
				AnimatedTexObData& animation_data = *ob->animated_tex_data;

				try
				{
					const AnimatedTexObDataProcessStats stats = animation_data.process(this, ui->glWidget->opengl_engine.ptr(), ob, anim_time, dt);
					num_gif_textures_processed += stats.num_gif_textures_processed;
					num_mp4_textures_processed += stats.num_mp4_textures_processed;
				}
				catch(glare::Exception& e)
				{
					logMessage("Excep while processing animation data: " + e.what());
				}
			}
		 } // End lock scope

		this->last_num_gif_textures_processed = num_gif_textures_processed;
		this->last_num_mp4_textures_processed = num_mp4_textures_processed;

		// Process web-view objects
		for(auto it = web_view_obs.begin(); it != web_view_obs.end(); ++it)
		{
			WorldObject* ob = it->ptr();

			try
			{
				ob->web_view_data->process(this, ui->glWidget->opengl_engine.ptr(), ob, anim_time, dt);
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
				ob->browser_vid_player->process(this, ui->glWidget->opengl_engine.ptr(), ob, anim_time, dt);
			}
			catch(glare::Exception& e)
			{
				logMessage("Excep while processing browser vid player: " + e.what());
			}
		}

		this->last_animated_tex_time = timer.elapsed();
	}


	runScreenshotCode();

	
	// NOTE: goes after sceeenshot code, which might update campos.
	Vec4f campos = this->cam_controller.getFirstPersonPosition().toVec4fPoint();

	const double cur_time = Clock::getTimeSinceInit(); // Used for animation, interpolation etc..

	ui->indigoView->timerThink();

	updateGroundPlane();

	//------------- Check to see if we should remove any old notifications ------------
	const double notification_display_time = 5;
	for(auto it = notifications.begin(); it != notifications.end();)
	{
		if(cur_time >  it->creation_time + notification_display_time)
		{
			// Remove the notification
			ui->notificationContainer->layout()->removeWidget(it->label);
			it->label->deleteLater();
			it = notifications.erase(it); // remove from list

			// Make the info dock widget resize.  See https://stackoverflow.com/a/30472749/7495926
			QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
			ui->infoDockWidget->resize(ui->infoDockWidget->sizeHint());

			// Hide the info dock widget if there are no remaining widgets.
			if(notifications.empty())
				ui->infoDockWidget->hide();
		}		
		else
			++it;
	}


	// Update URL Bar
	// NOTE: use doubleToStringNDecimalPlaces instead of doubleToStringMaxNDecimalPlaces, as the latter is distracting due to flickering URL length when moving.
	if(this->url_widget->shouldBeUpdated())
		this->url_widget->setURL("sub://" + server_hostname + "/" + server_worldname +
			"?x=" + doubleToStringNDecimalPlaces(this->cam_controller.getFirstPersonPosition().x, 1) + 
			"&y=" + doubleToStringNDecimalPlaces(this->cam_controller.getFirstPersonPosition().y, 1) +
			"&z=" + doubleToStringNDecimalPlaces(this->cam_controller.getFirstPersonPosition().z, 1));

	const QPoint gl_pos = ui->glWidget->mapToGlobal(QPoint(200, 10));
	if(ui->infoDockWidget->geometry().topLeft() != gl_pos)
	{
		// conPrint("Positioning ui->infoDockWidget at " + toString(gl_pos.x()) + ", " + toString(gl_pos.y()));
		ui->infoDockWidget->setGeometry(gl_pos.x(), gl_pos.y(), 300, 1);
	}
	

	if(need_help_info_dock_widget_position)
	{
		// Position near bottom right corner of glWidget.
		ui->helpInfoDockWidget->setGeometry(QRect(ui->glWidget->mapToGlobal(ui->glWidget->geometry().bottomRight() + QPoint(-320, -120)), QSize(300, 100)));
		need_help_info_dock_widget_position = false;
	}


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
		Scripting::evaluateObjectScripts(this->obs_with_scripts, global_time, dt, world_state.ptr(), ui->glWidget->opengl_engine.ptr(), this->physics_world.ptr(), &this->audio_engine,
			/*num_scripts_processed_out=*/this->last_num_scripts_processed
		);
		this->last_eval_script_time = timer.elapsed();
	}


	ui->glWidget->opengl_engine->setCurrentTime((float)cur_time);

	if(this->world_state.nonNull())
	{
		ZoneScopedN("path_controllers eval"); // Tracy profiler

		Lock lock(this->world_state->mutex);
		for(size_t i=0; i<path_controllers.size(); ++i)
			path_controllers[i]->update(*world_state, *physics_world, ui->glWidget->opengl_engine.ptr(), (float)dt);
	}

	UpdateEvents physics_events;

	PlayerPhysicsInput physics_input;
	{
		ZoneScopedN("processPlayerPhysicsInput"); // Tracy profiler
		ui->glWidget->processPlayerPhysicsInput((float)dt, /*input_out=*/physics_input); // sets player physics move impulse.
	}

	const bool our_move_impulse_zero = !player_physics.isMoveDesiredVelNonZero();

	// Advance physics sim and player physics with a maximum timestep size.
	// We advance both together, otherwise if there is a large dt, the physics engine can advance objects past what the player physics can keep up with.
	// This prevents stuff like the player falling off the back of a train when loading stutters occur.
	const double MAX_SUBSTEP_DT = 1.0 / 60.0;
	const int unclamped_num_substeps = (int)std::ceil(dt / MAX_SUBSTEP_DT); // May get very large.
	const int num_substeps  = myMin(unclamped_num_substeps, 60); // Only do up to 60 steps
	const double substep_dt = myMin(dt / num_substeps, MAX_SUBSTEP_DT); // Don't make the substep time > 1/60s.

	assert(substep_dt <= MAX_SUBSTEP_DT);
	//printVar(num_substeps);
	//conPrint("substep_dt: " + doubleToStringMaxNDecimalPlaces(substep_dt * 1000.0, 3) + " ms");

	{
		PERFORMANCEAPI_INSTRUMENT("physics sim");
		ZoneScopedN("physics sim"); // Tracy profiler

		for(int i=0; i<num_substeps; ++i)
		{
			if(physics_world.nonNull())
			{
				physics_world->think(substep_dt); // Advance physics simulation

				if(vehicle_controller_inside.nonNull()) // If we are inside a vehicle:
				{
					if(this->cur_seat_index == 0) // If we are driving it:
						vehicle_controller_inside->update(*this->physics_world, physics_input, (float)substep_dt);
				}
				else
				{
					// Process player physics
					UpdateEvents substep_physics_events = player_physics.update(*this->physics_world, physics_input, (float)substep_dt, cur_time, /*campos out=*/campos);
					physics_events.jumped = physics_events.jumped || substep_physics_events.jumped;

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
						
								if(!isObjectPhysicsOwnedBySelf(*ob, global_time) && !isObjectVehicleBeingDrivenByOther(*ob))
								{
									// conPrint("==Taking ownership of physics object from avatar physics contact...==");
									takePhysicsOwnershipOfObject(*ob, global_time);
								}
							}
						}
					}
					player_physics.contacted_events.resize(0);
				}


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
		const Vec3d player_pos = player_physics.getBottomPosition();

		const float terrain_h = terrain_system->evalTerrainHeight((float)player_pos.x, (float)player_pos.y, 1.0);

		if((float)player_pos.z < (terrain_h - 0.5f))
		{
			logMessage("Player was below terrain, moving up");
			Vec3d new_player_pos = player_pos;
			new_player_pos.z = terrain_h + player_physics.getEyeHeight() + 0.5f;
			player_physics.setPosition(new_player_pos, player_physics.getLinearVel());
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

		if(wind_audio_source.nonNull())
		{
			const float old_volume = wind_audio_source->volume;

			// Increase wind volume as speed increases, but only once we exceed a certain speed, since we don't really want wind sounds playing when we run + jump around (approx < 20 m/s).
			const float WIND_VOLUME_FACTOR = 0.7f;
			wind_audio_source->volume = myClamp((listener_linear_vel.length() - 20.f) * 0.015f * WIND_VOLUME_FACTOR, 0.f, WIND_VOLUME_FACTOR);

			if(wind_audio_source->volume != old_volume)
				audio_engine.sourceVolumeUpdated(*wind_audio_source);
		}
	}


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
								ui->glWidget->opengl_engine->updateObjectTransformData(*ob->opengl_engine_ob);

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

							if(this->selected_ob.ptr() == ob)
							{
								updateSelectedObjectPlacementBeam();
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
					if(ui->diagnosticsWidget->showPhysicsObOwnershipCheckBox->isChecked())
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
		}

		this->cam_controller.setPosition(toVec3d(campos));

		// Show vehicle speed on UI: Disabled until we can not create a zillion textures for this.
		if(false)
		{
			if(vehicle_controller_inside.nonNull()) // If we are inside a vehicle:
			{
				//const float speed_km_h = vehicle_controller_inside->getLinearVel(*this->physics_world).length() * (3600.0f / 1000.f);
				//misc_info_ui.showVehicleSpeed(speed_km_h);
				misc_info_ui.showVehicleInfo(vehicle_controller_inside->getUIInfoMsg());
			}
			else
				misc_info_ui.hideVehicleSpeed();
		}

		// Update debug player-physics visualisation spheres
		if(false)
		{
			for(size_t i=0; i<player_phys_debug_spheres.size(); ++i)
			{
				if(player_phys_debug_spheres[i].nonNull())
					ui->glWidget->opengl_engine->removeObject(player_phys_debug_spheres[i]);

				player_phys_debug_spheres[i] = NULL;
			}
			player_phys_debug_spheres.resize(0);

			std::vector<js::BoundingSphere> spheres;
			player_physics.debugGetCollisionSpheres(campos, spheres);


			player_phys_debug_spheres.resize(spheres.size());
			
			
			for(size_t i=0; i<spheres.size(); ++i)
			{
				if(player_phys_debug_spheres[i].isNull())
				{
					player_phys_debug_spheres[i] = ui->glWidget->opengl_engine->allocateObject();
					player_phys_debug_spheres[i]->ob_to_world_matrix = Matrix4f::identity();
					player_phys_debug_spheres[i]->mesh_data = ui->glWidget->opengl_engine->getSphereMeshData();

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

					ui->glWidget->opengl_engine->addObject(player_phys_debug_spheres[i]);
				}

				player_phys_debug_spheres[i]->ob_to_world_matrix = Matrix4f::translationMatrix(spheres[i].getCenter()) * Matrix4f::uniformScaleMatrix(spheres[i].getRadius());
				ui->glWidget->opengl_engine->updateObjectTransformData(*player_phys_debug_spheres[i]);
			}
		}


		if(vehicle_controller_inside.nonNull())
		{
			const bool should_show = ui->diagnosticsWidget->showVehiclePhysicsVisCheckBox->isChecked();
			vehicle_controller_inside->updateDebugVisObjects(*ui->glWidget->opengl_engine, should_show);
		}

		//--------------------------- Car controller and graphics -------------------------------
		//car_physics.update(*this->physics_world, physics_input, (float)dt, /*campos_out=*/campos);
		//this->cam_controller.setPosition(toVec3d(campos));

		// Update car visualisation
#if 0
		if(false)
		{
			wheel_gl_objects.resize(4);

			for(size_t i=0; i<wheel_gl_objects.size(); ++i)
			{
				if(wheel_gl_objects[i].isNull())
				{
					wheel_gl_objects[i] = ui->glWidget->opengl_engine->allocateObject();
					wheel_gl_objects[i]->ob_to_world_matrix = Matrix4f::identity();
					//wheel_gl_objects[i]->mesh_data = ui->glWidget->opengl_engine->getCylinderMesh();

					GLTFLoadedData gltf_data;
					BatchedMeshRef batched_mesh = FormatDecoderGLTF::loadGLBFile("D:\\models\\lambo_wheel.glb", gltf_data);
					wheel_gl_objects[i]->mesh_data = GLMeshBuilding::buildBatchedMesh(ui->glWidget->opengl_engine->vert_buf_allocator.ptr(), batched_mesh, /*skip opengl calls=*/false, /*instancing_matrix_data=*/NULL);
					wheel_gl_objects[i]->mesh_data->num_materials_referenced = batched_mesh->numMaterialsReferenced();


					OpenGLMaterial material;
					material.albedo_linear_rgb = Colour3f(0.2f, 0.2f, 0.2f);
					//material.tex_path = "resources/obstacle.png";

					
				//	wheel_gl_objects[i]->materials = SmallArray<OpenGLMaterial, 2>(wheel_gl_objects[i]->mesh_data->num_materials_referenced, material);

					wheel_gl_objects[i]->materials[2].albedo_linear_rgb = Colour3f(0.8f, 0.8f, 0.8f);
					wheel_gl_objects[i]->materials[2].metallic_frac = 1.f; // break pads
					wheel_gl_objects[i]->materials[2].roughness = 0.2f;
					wheel_gl_objects[i]->materials[4].albedo_linear_rgb = Colour3f(0.8f, 0.8f, 0.8f);
					wheel_gl_objects[i]->materials[4].metallic_frac = 1.f; // spokes
					wheel_gl_objects[i]->materials[4].roughness = 0.2f;


					ui->glWidget->opengl_engine->addObjectAndLoadTexturesImmediately(wheel_gl_objects[i]);
				}

				//wheel_gl_objects[i]->ob_to_world_matrix = bike_physics.getWheelTransform((int)i) *
				//	Matrix4f::rotationAroundXAxis(Maths::pi_2<float>()) * Matrix4f::scaleMatrix(0.3f, 0.3f, 0.1f) * Matrix4f::translationMatrix(0, 0, -0.5f);

				wheel_gl_objects[i]->ob_to_world_matrix = car_physics.getWheelTransform((int)i) * Matrix4f::rotationAroundZAxis(Maths::pi_2<float>()) * ((i == 0 || i == 2) ?  Matrix4f::rotationAroundZAxis(Maths::pi<float>()) : Matrix4f::identity());

				ui->glWidget->opengl_engine->updateObjectTransformData(*wheel_gl_objects[i]);
			}

			if(car_body_gl_object.isNull())
			{
				car_body_gl_object = ui->glWidget->opengl_engine->allocateObject();
				car_body_gl_object->ob_to_world_matrix = Matrix4f::identity();
				//car_body_gl_object->mesh_data = ui->glWidget->opengl_engine->getCubeMeshData();

				GLTFLoadedData gltf_data;
				BatchedMeshRef batched_mesh = FormatDecoderGLTF::loadGLBFile("D:\\models\\lambo_body.glb", gltf_data);
				car_body_gl_object->mesh_data = GLMeshBuilding::buildBatchedMesh(ui->glWidget->opengl_engine->vert_buf_allocator.ptr(), batched_mesh, /*skip opengl calls=*/false, /*instancing_matrix_data=*/NULL);
				car_body_gl_object->mesh_data->num_materials_referenced = batched_mesh->numMaterialsReferenced();
				car_body_gl_object->mesh_data->animation_data = batched_mesh->animation_data;

				OpenGLMaterial material;
				material.albedo_linear_rgb = Colour3f(115 / 255.f, 187 / 255.f, 202 / 255.f);
				material.roughness = 0.6;
				material.metallic_frac = 0.0;
					
				//car_body_gl_object->materials = SmallArray<OpenGLMaterial, 2>(car_body_gl_object->mesh_data->num_materials_referenced, material);

				car_body_gl_object->materials[12].alpha = 0.5f;
				car_body_gl_object->materials[12].transparent = true;

				ui->glWidget->opengl_engine->addObjectAndLoadTexturesImmediately(car_body_gl_object);
			}


			car_body_gl_object->ob_to_world_matrix = car_physics.getBodyTransform() * Matrix4f::translationMatrix(0, 0.2f, -0.2f) * Matrix4f::rotationAroundZAxis(Maths::pi<float>()) * Matrix4f::rotationAroundXAxis(Maths::pi_2<float>());
			
			//const float half_vehicle_length = 2.0f;
			//const float half_vehicle_width = 0.9f;
			//const float half_vehicle_height = 0.2f;
			// 
			// * Matrix4f::translationMatrix(0, 0, half_vehicle_height) * Matrix4f::scaleMatrix(2 * half_vehicle_width, 2 * half_vehicle_length, 2 * half_vehicle_height) * Matrix4f::translationMatrix(-0.5f, -0.5f, -0.5f);
			//car_body_gl_object->ob_to_world_matrix = bike_physics.getBodyTransform();// * Matrix4f::translationMatrix(0, 0, half_vehicle_height) * Matrix4f::scaleMatrix(2 * half_vehicle_width, 2 * half_vehicle_length, 2 * half_vehicle_height) * Matrix4f::translationMatrix(-0.5f, -0.5f, -0.5f);

			ui->glWidget->opengl_engine->updateObjectTransformData(*car_body_gl_object);
		}
		//--------------------------- END Car controller and graphics -------------------------------
#endif

		// Set some basic 3rd person cam variables that will be updated below if we are connected to a server
		if(false)
		{
			const Vec3d cam_back_dir = cam_controller.getForwardsVec() * -3.0 + cam_controller.getUpVec() * 0.2;
			this->cam_controller.third_person_cam_position = toVec3d(campos) + Vec3d(cam_back_dir);
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
			ui->indigoView->cameraUpdated(this->cam_controller);

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
				audio_engine.playOneShotSound(base_dir_path + "/resources/sounds/footstep_mono" + toString(rnd_src_i) + ".wav", footstrike_pos);

				last_footstep_timer.reset();
			}
		}

		if(physics_events.jumped)
		{
			const Vec4f jump_sound_pos = campos - Vec4f(0, 0, 0.1f, 0) +
				cam_controller.getForwardsVec().toVec4fVector() * 0.1f;

			const int rnd_src_i = rng.nextUInt(4);
			audio_engine.playOneShotSound(base_dir_path + "/resources/sounds/jump" + toString(rnd_src_i) + ".wav", jump_sound_pos);
		}
	}
	proximity_loader.updateCamPos(campos);

	const Vec3d cam_angles = this->cam_controller.getAngles();

	// Find out which parcel we are in, if any.
	ParcelID in_parcel_id = ParcelID::invalidParcelID();
	bool mute_outside_audio = false;
	if(world_state.nonNull())
	{
		//Timer timer;
		bool in_parcel = false;
		const Vec3d campos_vec3d = this->cam_controller.getFirstPersonPosition();
		Lock lock(world_state->mutex);
		const Parcel* parcel = world_state->getParcelPointIsIn(campos_vec3d);
		if(parcel)
		{
			// Set audio source room effects
			audio_engine.setCurentRoomDimensions(js::AABBox(
				Vec4f((float)parcel->aabb_min.x, (float)parcel->aabb_min.y, (float)parcel->aabb_min.z, 1.f),
				Vec4f((float)parcel->aabb_max.x, (float)parcel->aabb_max.y, (float)parcel->aabb_max.z, 1.f)));

			in_parcel_id = parcel->id;
			in_parcel = true;

			if(BitUtils::isBitSet(parcel->flags, Parcel::MUTE_OUTSIDE_AUDIO_FLAG))
				mute_outside_audio = true;
		}

		audio_engine.setRoomEffectsEnabled(in_parcel);

		//conPrint("Setting room effects took " + timer.elapsedStringNSigFigs(4));
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

				
				if(source->type != glare::AudioSource::SourceType_OneShot) // We won't be muting footsteps etc.
				{
					const float old_mute_volume_factor = source->getMuteVolumeFactor();
					if(mute_outside_audio) // If we are in a parcel, which has the mute-outside-audio option enabled:
					{
						if(source->userdata_1 != in_parcel_id.value()) // And the source is in another parcel (or not in any parcel):
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


	updateAvatarGraphics(cur_time, dt, cam_angles, our_move_impulse_zero);
	

	// Resonance seems to want a to-world transformation
	// It also seems to use the OpenGL camera convention (x = right, y = up, -z = forwards)

	const Quatf z_axis_rot_q = Quatf::fromAxisAndAngle(Vec3f(0,0,1), (float)cam_angles.x - Maths::pi_2<float>());
	const Quatf x_axis_rot_q = Quatf::fromAxisAndAngle(Vec3f(1,0,0), Maths::pi<float>() - (float)cam_angles.y);
	const Quatf q = z_axis_rot_q * x_axis_rot_q;
	audio_engine.setHeadTransform(cam_controller.thirdPersonEnabled() ? this->cam_controller.third_person_cam_position.toVec4fPoint() : this->cam_controller.getFirstPersonPosition().toVec4fPoint(), q);


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
		const double r = 20;
		//Vec3d pos(0, 0, 1.67);//cos(test_avatar_phase) * r, sin(test_avatar_phase) * r, 1.67);
		const double phase = Maths::get2Pi<double>() * i / test_avatars.size() + cur_time * 0.1;
		Vec3d pos(r * cos(phase), r * sin(phase), 1.67);//cos(test_avatar_phase) * r, sin(test_avatar_phase) * r, 1.67);
		const int anim_state = 0;
		float xyplane_speed_rel_ground = 0;
		test_avatar->graphics.setOverallTransform(*ui->glWidget->opengl_engine, pos, 
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

			audio_engine.playOneShotSound(base_dir_path + "/resources/sounds/footstep_mono" + toString(rnd_src_i) + ".wav", anim_events.footstrike_pos.toVec4fPoint());
		}
	}
#endif


	// Update world object graphics and physics models that have been marked as from-server-dirty based on incoming network messages from server.
	if(world_state.nonNull())
	{
		PERFORMANCEAPI_INSTRUMENT("object graphics");
		ZoneScopedN("object graphics"); // Tracy profiler

		try
		{
			Lock lock(this->world_state->mutex);

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

						ui->indigoView->objectRemoved(*ob);

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
					}
					else // Else if not dead:
					{
						// Decompress voxel group
						//ob->decompressVoxels();

						//proximity_loader.checkAddObject(ob); // Calls loadModelForObject() and loadAudioForObject() if it is within load distance.

						if(ob->state == WorldObject::State_JustCreated)
							enableMaterialisationEffectOnOb(*ob); // Enable materialisation effect before we call loadModelForObject() below.

						ob->in_proximity = ob->getCentroidWS().getDist2(campos) < this->load_distance2;

						if(ob->getCentroidWS().getDist2(campos) < this->load_distance2)
						{
							loadModelForObject(ob);
							loadAudioForObject(ob);
						}

						//bool reload_opengl_model = false; // Do we need to load or reload model?
						//if(ob->opengl_engine_ob.isNull())
						//	reload_opengl_model = true;
						//
						//if(ob->object_type == WorldObject::ObjectType_Generic)
						//{
						//	if(ob->loaded_model_url != ob->model_url) // If model URL differs from what we have loaded for this model:
						//		reload_opengl_model = true;
						//}
						//else if(ob->object_type == WorldObject::ObjectType_VoxelGroup)
						//{
						//	reload_opengl_model = true;
						//}
						//else if(ob->object_type == WorldObject::ObjectType_Hypercard)
						//{
						//	if(ob->loaded_content != ob->content)
						//		reload_opengl_model = true;
						//}
						//else if(ob->object_type == WorldObject::ObjectType_Spotlight)
						//{
						//	// no reload needed
						//}

						//if(reload_opengl_model)
						//{
						//	// loadModelForObject(ob);
						//}
						//else
						//{
						
						if(!ob->audio_source_url.empty() || ob->object_type == WorldObject::ObjectType_WebView || ob->object_type == WorldObject::ObjectType_Video)
							this->audio_obs.insert(ob);


						if((ob->state != WorldObject::State_JustCreated) && (ob->state != WorldObject::State_InitialSend)) // Don't reload materials when we just created the object locally.
						{
							// Update transform for object and object materials in OpenGL engine
							if(ob->opengl_engine_ob.nonNull() && (ob != selected_ob.getPointer())) // Don't update the selected object based on network messages, we will consider the local transform for it authoritative.
							{
								GLObjectRef opengl_ob = ob->opengl_engine_ob;

								// Update transform
								opengl_ob->ob_to_world_matrix = ob->obToWorldMatrix();
								ui->glWidget->opengl_engine->updateObjectTransformData(*opengl_ob);

								// Update materials in opengl engine.
								const int ob_lod_level = ob->getLODLevel(cam_controller.getPosition());
								for(size_t i=0; i<ob->materials.size(); ++i)
									if(i < opengl_ob->materials.size())
										ModelLoading::setGLMaterialFromWorldMaterial(*ob->materials[i], ob_lod_level, ob->lightmap_url, *this->resource_manager, opengl_ob->materials[i]);

								ui->glWidget->opengl_engine->objectMaterialsUpdated(*opengl_ob);

								updateInstancedCopiesOfObject(ob);
							}

							if(ob == selected_ob.ptr())
								ui->objectEditor->objectModelURLUpdated(*ob); // Update model URL in UI if we have selected the object.

							loadAudioForObject(ob); // Check for re-loading audio if audio URL changed.
						}
						//}


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
								ModelLoading::setGLMaterialFromWorldMaterial(*ob->materials[i], ob_lod_level, ob->lightmap_url, *this->resource_manager, opengl_ob->materials[i]);
						ui->glWidget->opengl_engine->objectMaterialsUpdated(*opengl_ob);
					}

					ob->lightmap_baking = false; // Since the lightmap URL has changed, we will assume that means the baking is done for this object.

					if(ob == selected_ob.ptr())
						ui->objectEditor->objectLightmapURLUpdated(*ob); // Update lightmap URL in UI if we have selected the object.

					ob->from_remote_lightmap_url_dirty = false;
				}
				else if(ob->from_remote_physics_ownership_dirty)
				{
					if(ui->diagnosticsWidget->showPhysicsObOwnershipCheckBox->isChecked())
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
						ui->glWidget->opengl_engine->updateObjectTransformData(*ob->opengl_engine_ob);
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

								ui->glWidget->opengl_engine->updateObjectTransformData(*ob->opengl_engine_ob);
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

						if(ui->diagnosticsWidget->showPhysicsObOwnershipCheckBox->isChecked())
							obs_with_diagnostic_vis.insert(ob);
					}
					it++;
				}
			}
		}
	} // end if(world_state.nonNull())


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

	updateVoxelEditMarkers();

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
			writeToStream(Vec3f(0, (float)cam_angles.y, (float)cam_angles.x), scratch_packet);
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
		terrain_system->updateCampos(this->cam_controller.getPosition(), bump_allocator);

	if(terrain_decal_manager.nonNull())
		terrain_decal_manager->think((float)dt);
	if(particle_manager.nonNull())
		particle_manager->think((float)dt);

	if(ui->glWidget->opengl_engine.nonNull())
	{
		ui->glWidget->opengl_engine->getCurrentScene()->wind_strength = (float)(0.25 * (1.0 + std::sin(global_time * 0.1234) + std::sin(global_time * 0.23543)));
	}


	last_timerEvent_CPU_work_elapsed = timerEvent_timer.elapsed();


	/*if(last_timerEvent_CPU_work_elapsed > 0.010)
	{
		logMessage("=============Long frame==================");
		logMessage("Frame CPU time: " + doubleToStringNSigFigs(last_timerEvent_CPU_work_elapsed * 1.0e3, 4) + " ms");
		logMessage("loading time: " + doubleToStringNSigFigs(frame_loading_time * 1.0e3, 4) + " ms");
		for(size_t i=0; i<loading_times.size(); ++i)
			logMessage("\t" + loading_times[i]);
		//conPrint("\tprocessing animated textures took " + doubleToStringNSigFigs(animated_tex_time * 1.0e3, 4) + " ms");
	}*/

	ui->glWidget->makeCurrent();

	//Timer timer;
	{
		Timer timer2;
		PERFORMANCEAPI_INSTRUMENT("updateGL()");
		ZoneScopedN("updateGL"); // Tracy profiler
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
		ui->glWidget->update();
#else
		ui->glWidget->updateGL();
#endif
		//if(timer.elapsed() > 0.020)
		//	conPrint(doubleToStringNDecimalPlaces(Clock::getTimeSinceInit(), 3) + ": updateGL() took " + timer.elapsedStringNSigFigs(4));
		this->last_updateGL_time = timer2.elapsed();
	}

	frame_num++;
}


void MainWindow::updateParcelGraphics()
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
						ui->glWidget->opengl_engine->removeObject(parcel->opengl_engine_ob);

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

					if(ui->actionShow_Parcels->isChecked())
					{
						if(parcel->opengl_engine_ob.isNull())
						{
							// Make OpenGL model for parcel:
							const bool write_perms = parcel->userHasWritePerms(this->logged_in_user_id);

							bool use_write_perms = write_perms;
							if(!screenshot_output_path.empty()) // If we are in screenshot-taking mode, don't highlight writable parcels.
								use_write_perms = false;

							parcel->opengl_engine_ob = parcel->makeOpenGLObject(ui->glWidget->opengl_engine, use_write_perms);
							parcel->opengl_engine_ob->materials[0].shader_prog = this->parcel_shader_prog;
							parcel->opengl_engine_ob->materials[0].auto_assign_shader = false;
							ui->glWidget->opengl_engine->addObject(parcel->opengl_engine_ob);

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
							//ui->glWidget->opengl_engine->updateObjectTransformData(*parcel->opengl_engine_ob);
							//
							//// Update in physics engine
							//parcel->physics_object->ob_to_world = parcel->opengl_engine_ob->ob_to_world_matrix;
							//physics_world->updateObjectTransformData(*parcel->physics_object);
						}
					}

					// If we want to move to this parcel based on the URL entered:
					if(this->url_parcel_uid == (int)parcel->id.value())
					{
						cam_controller.setPosition(parcel->getVisitPosition());
						player_physics.setPosition(parcel->getVisitPosition());
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


void MainWindow::updateAvatarGraphics(double cur_time, double dt, const Vec3d& cam_angles, bool our_move_impulse_zero)
{
	// Update avatar graphics
	temp_av_positions.clear();
	if(world_state.nonNull())
	{
		PERFORMANCEAPI_INSTRUMENT("avatar graphics");
		ZoneScopedN("avatar graphics"); // Tracy profiler

		try
		{
			Lock lock(this->world_state->mutex);

			for(auto it = this->world_state->avatars.begin(); it != this->world_state->avatars.end();)
			{
				Avatar* avatar = it->second.getPointer();
				const bool our_avatar = avatar->isOurAvatar();

				if(avatar->state == Avatar::State_Dead)
				{
					print("Removing avatar.");

					ui->chatMessagesTextEdit->append(QtUtils::toQString("<i><span style=\"color:rgb(" + 
						toString(avatar->name_colour.r * 255) + ", " + toString(avatar->name_colour.g * 255) + ", " + toString(avatar->name_colour.b * 255) + ")\">" + 
						web::Escaping::HTMLEscape(avatar->name) + "</span> left.</i>"));

					// Remove any OpenGL object for it
					avatar->graphics.destroy(*ui->glWidget->opengl_engine);

					// Remove nametag OpenGL object
					if(avatar->nametag_gl_ob.nonNull())
						ui->glWidget->opengl_engine->removeObject(avatar->nametag_gl_ob);
					avatar->nametag_gl_ob = NULL;
					if(avatar->speaker_gl_ob.nonNull())
						ui->glWidget->opengl_engine->removeObject(avatar->speaker_gl_ob);
					avatar->speaker_gl_ob = NULL;

					hud_ui.removeMarkerForAvatar(avatar); // Remove any marker for the avatar from the HUD

					// Remove avatar from avatar map
					auto old_avatar_iterator = it;
					it++;
					this->world_state->avatars.erase(old_avatar_iterator);

					updateOnlineUsersList();

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

						updateOnlineUsersList();
					}

					if((cam_controller.thirdPersonEnabled() || !our_avatar) && reload_opengl_model) // Don't load graphics for our avatar unless we are in third-person cam view mode
					{
						print("(Re)Loading avatar model. model URL: " + avatar->avatar_settings.model_url + ", Avatar name: " + avatar->name);

						// Remove any existing model and nametag
						avatar->graphics.destroy(*ui->glWidget->opengl_engine);
						
						if(avatar->nametag_gl_ob.nonNull()) // Remove nametag ob
							ui->glWidget->opengl_engine->removeObject(avatar->nametag_gl_ob);
						avatar->nametag_gl_ob = NULL;
						if(avatar->speaker_gl_ob.nonNull())
							ui->glWidget->opengl_engine->removeObject(avatar->speaker_gl_ob);
						avatar->speaker_gl_ob = NULL;

						hud_ui.removeMarkerForAvatar(avatar); // Remove any marker for the avatar from the HUD

						print("Adding Avatar to OpenGL Engine, UID " + toString(avatar->uid.value()));

						loadModelForAvatar(avatar);

						if(!our_avatar)
						{
							// Add nametag object for avatar
							avatar->nametag_gl_ob = makeNameTagGLObject(avatar->name);

							// Set transform to be above avatar.  This transform will be updated later.
							avatar->nametag_gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(avatar->pos.toVec4fVector());

							ui->glWidget->opengl_engine->addObject(avatar->nametag_gl_ob); // Add to 3d engine

							// Play entry teleport sound
							audio_engine.playOneShotSound(base_dir_path + "/resources/sounds/462089__newagesoup__ethereal-woosh_normalised_mono.wav", avatar->pos.toVec4fVector());
						}
					} // End if reload_opengl_model


					// Update transform if we have an avatar or placeholder OpenGL model.
					Vec3d pos;
					Vec3f rotation;
					avatar->getInterpolatedTransform(cur_time, pos, rotation);

					bool use_xyplane_speed_rel_ground_override = false;
					float xyplane_speed_rel_ground_override = 0;

					// Do 3rd person cam stuff for our avatar:
					if(our_avatar)
					{
						pos = cam_controller.getFirstPersonPosition();
						rotation = Vec3f(0, (float)cam_angles.y, (float)cam_angles.x);

						use_xyplane_speed_rel_ground_override = true;
						xyplane_speed_rel_ground_override = player_physics.getLastXYPlaneVelRelativeToGround().length();

						const bool selfie_mode = this->cam_controller.selfieModeEnabled();

						Vec4f use_target_pos;
						if(selfie_mode)
							use_target_pos = avatar->graphics.getLastHeadPosition();
						else
						{
							const Vec4f vertical_offset = vehicle_controller_inside.nonNull() ? vehicle_controller_inside->getThirdPersonCamTargetTranslation() : Vec4f(0);
							use_target_pos = cam_controller.getFirstPersonPosition().toVec4fPoint() + vertical_offset;
						}

						//rotation = Vec3f(0, 0, 0); // just for testing
						//pos = Vec3d(0,0,1.7);

						avatar->anim_state = 
							(player_physics.onGroundRecently() ? 0 : AvatarGraphics::ANIM_STATE_IN_AIR) | 
							(player_physics.flyModeEnabled() ? AvatarGraphics::ANIM_STATE_FLYING : 0) | 
							(our_move_impulse_zero ? AvatarGraphics::ANIM_STATE_MOVE_IMPULSE_ZERO : 0);

						if(cam_controller.thirdPersonEnabled())
						{
							Vec4f cam_back_dir;
							if(selfie_mode)
							{
								// Slowly blend towards use_target_pos as in selfie mode it comes from getLastHeadPosition() which can vary rapidly frame to frame.
								const float target_lerp_frac = myMin(0.2f, (float)dt * 20);
								cam_controller.current_third_person_target_pos = cam_controller.current_third_person_target_pos * (1 - target_lerp_frac) + Vec3d(use_target_pos) * target_lerp_frac;

								cam_back_dir = (cam_controller.getForwardsVec() * cam_controller.getThirdPersonCamDist()).toVec4fVector();
							}
							else
							{
								cam_controller.current_third_person_target_pos = Vec3d(use_target_pos);

								cam_back_dir = (cam_controller.getForwardsVec() * -cam_controller.getThirdPersonCamDist() + cam_controller.getUpVec() * 0.2).toVec4fVector();
							}


							//printVar(cam_back_dir);

							// Don't start tracing the ray back immediately or we may hit the vehicle.
							const float initial_ignore_dist = vehicle_controller_inside.nonNull() ? myMin(cam_controller.getThirdPersonCamDist(), vehicle_controller_inside->getThirdPersonCamTraceSelfAvoidanceDist()) : 0.f;
							// We want to make sure the 3rd-person camera view is not occluded by objects behind the avatar's head (walls etc..)
							// So trace a ray backwards, and position the camera on the ray path before it hits the wall.
							RayTraceResult trace_results;
							physics_world->traceRay(/*origin=*/use_target_pos + normalise(cam_back_dir) * initial_ignore_dist, 
								/*dir=*/normalise(cam_back_dir), /*max_t=*/cam_back_dir.length() - initial_ignore_dist + 1.f, trace_results);

							if(trace_results.hit_object)
							{
								const float use_dist = myClamp(initial_ignore_dist + trace_results.hit_t - 0.05f, 0.5f, cam_back_dir.length());
								cam_back_dir = normalise(cam_back_dir) * use_dist;
							}

							//cam_controller.setThirdPersonCamTranslation(Vec3d(cam_back_dir));
							cam_controller.third_person_cam_position = cam_controller.current_third_person_target_pos + Vec3d(cam_back_dir);
						}
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
										controller_res->second->userExitedVehicle(avatar->vehicle_seat_index);
									avatar->entered_vehicle = NULL;
									avatar->pending_vehicle_transition = Avatar::VehicleNoChange;
								}
							}
						}
						 
						AnimEvents anim_events;
						avatar->graphics.setOverallTransform(*ui->glWidget->opengl_engine, pos, rotation, use_xyplane_speed_rel_ground_override, xyplane_speed_rel_ground_override,
							avatar->avatar_settings.pre_ob_to_world_matrix, avatar->anim_state, cur_time, dt, pose_constraint, anim_events);
						
						if(!BitUtils::isBitSet(avatar->anim_state, AvatarGraphics::ANIM_STATE_IN_AIR) && anim_events.footstrike && !pose_constraint.sitting) // If avatar is on ground, and the anim played a footstrike
						{
							//const int rnd_src_i = rng.nextUInt((uint32)footstep_sources.size());
							//footstep_sources[rnd_src_i]->cur_read_i = 0;
							//audio_engine.setSourcePosition(footstep_sources[rnd_src_i], anim_events.footstrike_pos.toVec4fPoint());
							const int rnd_src_i = rng.nextUInt(4);
							audio_engine.playOneShotSound(base_dir_path + "/resources/sounds/footstep_mono" + toString(rnd_src_i) + ".wav", anim_events.footstrike_pos.toVec4fPoint());
						}

						for(int i=0; i<anim_events.num_blobs; ++i)
							temp_av_positions.push_back(anim_events.blob_sphere_positions[i]);
					}

					
					// Update nametag transform also
					if(avatar->nametag_gl_ob.nonNull())
					{
						// If the avatar is in a vehicle, use the vehicle transform, which can be somewhat different from the avatar location due to different interpolation methods.
						Vec4f use_nametag_pos = pos.toVec4fPoint();
						if(avatar->entered_vehicle.nonNull())
						{
							const auto controller_res = vehicle_controllers.find(avatar->entered_vehicle.ptr()); // Find a vehicle controller for the avatar 'entered_vehicle' object.
							if(controller_res != vehicle_controllers.end())
							{
								VehiclePhysics* controller = controller_res->second.ptr();
								const Matrix4f seat_to_world = controller->getSeatToWorldTransform(*this->physics_world, avatar->vehicle_seat_index, /*use_smoothed_network_transform=*/true);

								use_nametag_pos = seat_to_world * Vec4f(0,0,1.0f,1);
							}
						}

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
						ui->glWidget->opengl_engine->updateObjectTransformData(*avatar->nametag_gl_ob); // Update transform in 3d engine

						// Set speaker icon transform and colour
						if(avatar->speaker_gl_ob.nonNull())
						{
							const float vol_padding_frac = 0.f;
							const float vol_h = ws_height * (1 - vol_padding_frac * 2);
							const float vol_padding = ws_height * vol_padding_frac;
							avatar->speaker_gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(use_nametag_pos + Vec4f(0, 0, 0.45f + avatar->nametag_z_offset, 0)) *
								rot_matrix * Matrix4f::translationMatrix(-total_w/2 + ws_width + 0.05f, 0.f, vol_padding) * Matrix4f::scaleMatrix(vol_h, 1, vol_h);

							ui->glWidget->opengl_engine->updateObjectTransformData(*avatar->speaker_gl_ob); // Update transform in 3d engine

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
								ui->glWidget->opengl_engine->objectMaterialsUpdated(*avatar->speaker_gl_ob);
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
							if(avatar->graphics.getLastVel().length() > 5) // If avatar is roughly going above walking speed: walking speed is ~2.9 m/s, running ~14 m/s
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

								ui->glWidget->opengl_engine->updateObjectTransformData(*their_selected_ob->opengl_engine_ob);

								selected_pos = toVec3d(their_selected_ob->opengl_engine_ob->aabb_ws.centroid());
							}

							avatar->graphics.setSelectedObBeam(*ui->glWidget->opengl_engine, selected_pos);
						}
					}
					else
					{
						avatar->graphics.hideSelectedObBeam(*ui->glWidget->opengl_engine);
					}*/


					if(!our_avatar)
						hud_ui.updateMarkerForAvatar(avatar, pos); // Update marker on HUD


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
			ui->glWidget->opengl_engine->getCurrentScene()->blob_shadow_locations.resize(use_num_av_positions);
			for(size_t i=0; i<use_num_av_positions; ++i)
				ui->glWidget->opengl_engine->getCurrentScene()->blob_shadow_locations[i] = temp_av_positions[i];

			ui->glWidget->opengl_engine->getCurrentScene()->grass_pusher_sphere_pos = cam_controller.getFirstPersonPosition().toVec4fPoint() + Vec4f(0, 0, -PlayerPhysics::getEyeHeight(), 0);
		}
		catch(glare::Exception& e)
		{
			print("Error while Updating avatar graphics: " + e.what());
		}
	}
}


void MainWindow::handleMessages(double global_time, double cur_time)
{
	// Handle any messages (chat messages etc..)
	{
		PERFORMANCEAPI_INSTRUMENT("handle msgs");
		ZoneScopedN("handle msgs"); // Tracy profiler

		// Remove any messages
		std::vector<Reference<ThreadMessage> > msgs;
		{
			Lock msg_queue_lock(this->msg_queue.getMutex());
			while(!msg_queue.unlockedEmpty())
			{
				Reference<ThreadMessage> msg;
				this->msg_queue.unlockedDequeue(msg);
				msgs.push_back(msg);
			}
		}

		for(size_t i=0; i<msgs.size(); ++i)
		{
			Reference<ThreadMessage> msg = msgs[i];

			if(dynamic_cast<ModelLoadedThreadMessage*>(msg.getPointer()))
			{
				// Add to model_loaded_messages_to_process to process later.
				model_loaded_messages_to_process.push_back((ModelLoadedThreadMessage*)msg.ptr());
			}
			else if(dynamic_cast<TextureLoadedThreadMessage*>(msg.getPointer()))
			{
				// Add to texture_loaded_messages_to_process to process later.
				texture_loaded_messages_to_process.push_back((TextureLoadedThreadMessage*)msg.ptr());
			}
			else if(dynamic_cast<BuildScatteringInfoDoneThreadMessage*>(msg.getPointer()))
			{
				BuildScatteringInfoDoneThreadMessage* loaded_msg = static_cast<BuildScatteringInfoDoneThreadMessage*>(msg.ptr());

				// Look up object
				Lock lock(this->world_state->mutex);

				auto res = this->world_state->objects.find(loaded_msg->ob_uid);
				if(res != this->world_state->objects.end())
				{
					WorldObject* ob = res.getValue().ptr();

					ob->scattering_info = loaded_msg->ob_scattering_info;

					doBiomeScatteringForObject(ob);
				}
			}
			else if(dynamic_cast<AudioLoadedThreadMessage*>(msg.getPointer()))
			{
				AudioLoadedThreadMessage* loaded_msg = static_cast<AudioLoadedThreadMessage*>(msg.ptr());

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

								if(loaded_msg->sound_file->buf->buffer.size() > 0) // Avoid divide by zero.
								{
									// Timer timer;
									// Add a looping audio source
									ob->audio_source = new glare::AudioSource();
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
			else if(dynamic_cast<ScriptLoadedThreadMessage*>(msg.getPointer()))
			{
				ScriptLoadedThreadMessage* loaded_msg = static_cast<ScriptLoadedThreadMessage*>(msg.ptr());

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
			else if(dynamic_cast<const ClientConnectingToServerMessage*>(msg.getPointer()))
			{
				this->connection_state = ServerConnectionState_Connecting;
				updateStatusBar();

				this->server_ip_addr = static_cast<const ClientConnectingToServerMessage*>(msg.getPointer())->server_ip;

				// Now that we have the server IP address, start UDP thread.
				try
				{
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
				}
				catch(glare::Exception& e)
				{
					conPrint(e.what());
					QMessageBox msgBox;
					msgBox.setText(QtUtils::toQString(e.what()));
					msgBox.exec();
					return;
				}
			}
			else if(dynamic_cast<const ClientConnectedToServerMessage*>(msg.getPointer()))
			{
				this->connection_state = ServerConnectionState_Connected;
				updateStatusBar();

				this->client_avatar_uid = static_cast<const ClientConnectedToServerMessage*>(msg.getPointer())->client_avatar_uid;
				this->server_protocol_version = static_cast<const ClientConnectedToServerMessage*>(msg.getPointer())->server_protocol_version;

				// Try and log in automatically if we have saved credentials for this domain, and auto_login is true.
				if(settings->value("LoginDialog/auto_login", /*default=*/true).toBool())
				{
					CredentialManager manager;
					manager.loadFromSettings(*settings);

					const std::string username = manager.getUsernameForDomain(server_hostname);
					if(!username.empty())
					{
						const std::string password = manager.getDecryptedPasswordForDomain(server_hostname);

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

					const Vec3d cam_angles = this->cam_controller.getAngles();
					Avatar avatar;
					avatar.uid = this->client_avatar_uid;
					avatar.pos = Vec3d(this->cam_controller.getFirstPersonPosition());
					avatar.rotation = Vec3f(0, (float)cam_angles.y, (float)cam_angles.x);
					writeAvatarToNetworkStream(avatar, scratch_packet);

					enqueueMessageToSend(*this->client_thread, scratch_packet);
				}

				audio_engine.playOneShotSound(base_dir_path + "/resources/sounds/462089__newagesoup__ethereal-woosh_normalised_mono.wav", 
					(this->cam_controller.getFirstPersonPosition() + Vec3d(0, 0, -1)).toVec4fPoint());
			}
			else if(dynamic_cast<const AudioStreamToServerStartedMessage*>(msg.getPointer()))
			{
				// Sent by MicReadThread to indicate that streaming audio to the server has started.   Also sent periodically during streaming as well.

				const AudioStreamToServerStartedMessage* m = static_cast<const AudioStreamToServerStartedMessage*>(msg.getPointer());

				// Make AudioStreamToServerStarted packet and enqueue to send
				MessageUtils::initPacket(scratch_packet, Protocol::AudioStreamToServerStarted);
				scratch_packet.writeUInt32(m->sampling_rate);
				scratch_packet.writeUInt32(m->flags);
				scratch_packet.writeUInt32(m->stream_id);

				enqueueMessageToSend(*this->client_thread, scratch_packet);
			}
			else if(dynamic_cast<const AudioStreamToServerEndedMessage*>(msg.getPointer()))
			{
				// Sent by MicReadThread to indicate that streaming audio to the server has ended.

				// Make AudioStreamToServerEnded packet and enqueue to send
				MessageUtils::initPacket(scratch_packet, Protocol::AudioStreamToServerEnded);

				enqueueMessageToSend(*this->client_thread, scratch_packet);
			}
			else if(dynamic_cast<const RemoteClientAudioStreamToServerStarted*>(msg.getPointer()))
			{
				// Sent by ClientThread to this MainWindow after receiving AudioStreamToServerStarted message from server.
				// Indicates a client has started streaming audio to the server.
				// Create an audio source to play spatial audio from the avatar, if there isn't one already.

				const RemoteClientAudioStreamToServerStarted* m = static_cast<const RemoteClientAudioStreamToServerStarted*>(msg.getPointer());

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
									ui->glWidget->opengl_engine->addObject(avatar->speaker_gl_ob); // Add to 3d engine
								}
							}
						}
					}
				}
				else
					conPrint("Invalid sampling rate, ignoring RemoteClientAudioStreamToServerStarted message");
			}
			else if(dynamic_cast<const RemoteClientAudioStreamToServerEnded*>(msg.getPointer()))
			{
				// Sent by ClientThread to this MainWindow after receiving AudioStreamToServerEnded message from server.
				// Indicates a client has finished streaming audio to the server.

				const RemoteClientAudioStreamToServerEnded* m = static_cast<const RemoteClientAudioStreamToServerEnded*>(msg.getPointer());

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
							ui->glWidget->opengl_engine->removeObject(avatar->speaker_gl_ob);
							avatar->speaker_gl_ob = NULL;
						}
					}
				}
			}
			else if(dynamic_cast<const ClientProtocolTooOldMessage*>(msg.getPointer()))
			{
				QMessageBox msgBox;
				msgBox.setWindowTitle("Client too old");
				msgBox.setTextFormat(Qt::RichText);
				msgBox.setText("<p>Sorry, your Substrata client is too old.</p><p>Please download and install an updated client from <a href=\"https://substrata.info/\">substrata.info</a></p>");
				msgBox.exec();
			}
			else if(dynamic_cast<const ClientDisconnectedFromServerMessage*>(msg.getPointer()))
			{
				const ClientDisconnectedFromServerMessage* m = static_cast<const ClientDisconnectedFromServerMessage*>(msg.getPointer());
				if(!m->error_message.empty())
				{
					showErrorNotification(m->error_message);
				}
				this->connection_state = ServerConnectionState_NotConnected;

				this->logged_in_user_id = UserID::invalidUserID();
				this->logged_in_user_name = "";
				this->logged_in_user_flags = 0;

				user_details->setTextAsNotLoggedIn();

				ui->worldSettingsWidget->updateControlsEditable();

				updateStatusBar();
			}
			else if(dynamic_cast<const AvatarIsHereMessage*>(msg.getPointer()))
			{
				const AvatarIsHereMessage* m = static_cast<const AvatarIsHereMessage*>(msg.getPointer());

				if(world_state.nonNull())
				{
					Lock lock(this->world_state->mutex);

					auto res = this->world_state->avatars.find(m->avatar_uid);
					if(res != this->world_state->avatars.end())
					{
						Avatar* avatar = res->second.getPointer();

						ui->chatMessagesTextEdit->append(QtUtils::toQString("<i><span style=\"color:rgb(" + 
							toString(avatar->name_colour.r * 255) + ", " + toString(avatar->name_colour.g * 255) + ", " + toString(avatar->name_colour.b * 255) +
							")\">" + web::Escaping::HTMLEscape(avatar->name) + "</span> is here.</i>"));
						updateOnlineUsersList();
					}
				}
			}
			else if(dynamic_cast<const AvatarCreatedMessage*>(msg.getPointer()))
			{
				const AvatarCreatedMessage* m = static_cast<const AvatarCreatedMessage*>(msg.getPointer());

				if(world_state.nonNull())
				{
					Lock lock(this->world_state->mutex);

					auto res = this->world_state->avatars.find(m->avatar_uid);
					if(res != this->world_state->avatars.end())
					{
						const Avatar* avatar = res->second.getPointer();
						ui->chatMessagesTextEdit->append(QtUtils::toQString("<i><span style=\"color:rgb(" + 
							toString(avatar->name_colour.r * 255) + ", " + toString(avatar->name_colour.g * 255) + ", " + toString(avatar->name_colour.b * 255) +
							")\">" + web::Escaping::HTMLEscape(avatar->name) + "</span> joined.</i>"));
						updateOnlineUsersList();
					}
				}
			}
			else if(dynamic_cast<const AvatarPerformGestureMessage*>(msg.getPointer()))
			{
				const AvatarPerformGestureMessage* m = static_cast<const AvatarPerformGestureMessage*>(msg.getPointer());

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
			else if(dynamic_cast<const AvatarStopGestureMessage*>(msg.getPointer()))
			{
				const AvatarStopGestureMessage* m = static_cast<const AvatarStopGestureMessage*>(msg.getPointer());

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
			else if(dynamic_cast<const ChatMessage*>(msg.getPointer()))
			{
				const ChatMessage* m = static_cast<const ChatMessage*>(msg.getPointer());

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

					ui->chatMessagesTextEdit->append(QtUtils::toQString(
						"<p><span style=\"color:rgb(" + toString(col.r * 255) + ", " + toString(col.g * 255) + ", " + toString(col.b * 255) + ")\">" + web::Escaping::HTMLEscape(m->name) + "</span>: " +
						web::Escaping::HTMLEscape(m->msg) + "</p>"));
				}
			}
			else if(dynamic_cast<const InfoMessage*>(msg.getPointer()))
			{
				const InfoMessage* m = static_cast<const InfoMessage*>(msg.getPointer());
				QMessageBox msgBox;
				msgBox.setWindowTitle("Message from server");
				msgBox.setText(QtUtils::toQString(m->msg));
				msgBox.exec();
			}
			else if(dynamic_cast<const ErrorMessage*>(msg.getPointer()))
			{
				const ErrorMessage* m = static_cast<const ErrorMessage*>(msg.getPointer());
				showErrorNotification(m->msg);
			}
			else if(dynamic_cast<const LogMessage*>(msg.getPointer()))
			{
				const LogMessage* m = static_cast<const LogMessage*>(msg.getPointer());
				logMessage(m->msg);
			}
			else if(dynamic_cast<const LoggedInMessage*>(msg.getPointer()))
			{
				const LoggedInMessage* m = static_cast<const LoggedInMessage*>(msg.getPointer());

				user_details->setTextAsLoggedIn(m->username);
				this->logged_in_user_id = m->user_id;
				this->logged_in_user_name = m->username;
				this->logged_in_user_flags = m->user_flags;

				conPrint("Logged in as user with id " + toString(this->logged_in_user_id.value()));

				recolourParcelsForLoggedInState();
				ui->worldSettingsWidget->updateControlsEditable();

				// Send AvatarFullUpdate message, to change the nametag on our avatar.
				const Vec3d cam_angles = this->cam_controller.getAngles();
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
			else if(dynamic_cast<const LoggedOutMessage*>(msg.getPointer()))
			{
				user_details->setTextAsNotLoggedIn();
				this->logged_in_user_id = UserID::invalidUserID();
				this->logged_in_user_name = "";
				this->logged_in_user_flags = 0;

				recolourParcelsForLoggedInState();
				ui->worldSettingsWidget->updateControlsEditable();

				// Send AvatarFullUpdate message, to change the nametag on our avatar.
				const Vec3d cam_angles = this->cam_controller.getAngles();
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
			else if(dynamic_cast<const SignedUpMessage*>(msg.getPointer()))
			{
				const SignedUpMessage* m = static_cast<const SignedUpMessage*>(msg.getPointer());
				QMessageBox msgBox;
				msgBox.setWindowTitle("Signed up");
				msgBox.setText("Successfully signed up and logged in.");
				msgBox.exec();

				user_details->setTextAsLoggedIn(m->username);
				this->logged_in_user_id = m->user_id;
				this->logged_in_user_name = m->username;
				this->logged_in_user_flags = 0;

				// Send AvatarFullUpdate message, to change the nametag on our avatar.
				const Vec3d cam_angles = this->cam_controller.getAngles();
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
			else if(dynamic_cast<const ServerAdminMessage*>(msg.getPointer()))
			{
				const ServerAdminMessage* m = static_cast<const ServerAdminMessage*>(msg.getPointer());
				
				misc_info_ui.showServerAdminMessage(m->msg);
			}
			else if(dynamic_cast<const WorldSettingsReceivedMessage*>(msg.getPointer()))
			{
				const WorldSettingsReceivedMessage* m = static_cast<const WorldSettingsReceivedMessage*>(msg.getPointer());

				this->connected_world_settings.copyNetworkStateFrom(m->world_settings); // Store world settings to be used later

				this->ui->worldSettingsWidget->setFromWorldSettings(this->connected_world_settings); // Update UI

				if(!m->is_initial_send)
					showInfoNotification("World settings updated");

				// Reload terrain by shutting it down, will be recreated in MainWindow::updateGroundPlane().
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
			else if(dynamic_cast<const MapTilesResultReceivedMessage*>(msg.getPointer()))
			{
				const MapTilesResultReceivedMessage* m = static_cast<const MapTilesResultReceivedMessage*>(msg.getPointer());

				this->minimap.handleMapTilesResultReceivedMessage(*m);
			}
			else if(dynamic_cast<const UserSelectedObjectMessage*>(msg.getPointer()))
			{
				if(world_state.nonNull())
				{
					//print("MainWIndow: Received UserSelectedObjectMessage");
					const UserSelectedObjectMessage* m = static_cast<const UserSelectedObjectMessage*>(msg.getPointer());
					Lock lock(this->world_state->mutex);
					const bool is_ob_with_uid_inserted = this->world_state->objects.find(m->object_uid) != this->world_state->objects.end();
					if(this->world_state->avatars.count(m->avatar_uid) != 0 && is_ob_with_uid_inserted)
					{
						this->world_state->avatars[m->avatar_uid]->selected_object_uid = m->object_uid;
					}
				}
			}
			else if(dynamic_cast<const UserDeselectedObjectMessage*>(msg.getPointer()))
			{
				if(world_state.nonNull())
				{	
					//print("MainWIndow: Received UserDeselectedObjectMessage");
					const UserDeselectedObjectMessage* m = static_cast<const UserDeselectedObjectMessage*>(msg.getPointer());
					Lock lock(this->world_state->mutex);
					if(this->world_state->avatars.count(m->avatar_uid) != 0)
					{
						this->world_state->avatars[m->avatar_uid]->selected_object_uid = UID::invalidUID();
					}
				}
			}
			else if(dynamic_cast<const GetFileMessage*>(msg.getPointer()))
			{
				// When the server wants a file from the client, it will send the client a GetFile protocol message.
				const GetFileMessage* m = static_cast<const GetFileMessage*>(msg.getPointer());

				if(ResourceManager::isValidURL(m->URL))
				{
					if(resource_manager->isFileForURLPresent(m->URL))
					{
						const std::string path = resource_manager->pathForURL(m->URL);

						CredentialManager manager;
						manager.loadFromSettings(*settings);

						const std::string username = manager.getUsernameForDomain(server_hostname);
						const std::string password = manager.getDecryptedPasswordForDomain(server_hostname);

						this->num_resources_uploading++;
						resource_upload_thread_manager.addThread(new UploadResourceThread(&this->msg_queue, path, m->URL, server_hostname, server_port, username, password, this->client_tls_config, 
							&this->num_resources_uploading));
						print("Received GetFileMessage, Uploading resource with URL '" + m->URL + "' to server.");
					}
					else
						print("Could not upload resource with URL '" + m->URL + "' to server, not present on client.");
				}
			}
			else if(dynamic_cast<const NewResourceOnServerMessage*>(msg.getPointer()))
			{
				// When the server has a file uploaded to it, it will send a NewResourceOnServer message to clients, so they can download it.

				const NewResourceOnServerMessage* m = static_cast<const NewResourceOnServerMessage*>(msg.getPointer());

				if(world_state.nonNull())
				{
					conPrint("Got NewResourceOnServerMessage, URL: " + m->URL);

					if(ResourceManager::isValidURL(m->URL))
					{
						if(!resource_manager->isFileForURLPresent(m->URL)) // If we don't have this file yet:
						{
							conPrint("Do not have resource.");

							// Iterate over objects and see if they were using a placeholder model for this resource.
							Lock lock(this->world_state->mutex);
							bool need_resource = false;
							for(auto it = this->world_state->objects.valuesBegin(); it != this->world_state->objects.valuesEnd(); ++it)
							{
								WorldObject* ob = it.getValue().ptr();

								const int ob_lod_level = ob->getLODLevel(cam_controller.getPosition());

								//if(ob->using_placeholder_model)
								{
									WorldObject::GetDependencyOptions options;
									std::set<DependencyURL> URL_set;
									ob->getDependencyURLSet(ob_lod_level, options, URL_set);
									need_resource = need_resource || (URL_set.count(DependencyURL(m->URL)) != 0);
								}
							}

							for(auto it = this->world_state->avatars.begin(); it != this->world_state->avatars.end(); ++it)
							{
								Avatar* av = it->second.getPointer();

								const int av_lod_level = av->getLODLevel(cam_controller.getPosition());

								//if(ob->using_placeholder_model)
								{
									std::set<DependencyURL> URL_set;
									av->getDependencyURLSet(av_lod_level, URL_set);
									need_resource = need_resource || (URL_set.count(DependencyURL(m->URL)) != 0);
								}
							}

							const bool valid_extension = FileTypes::hasSupportedExtension(m->URL);
							conPrint("need_resource: " + boolToString(need_resource) + " valid_extension: " + boolToString(valid_extension));

							if(need_resource && valid_extension)// && !shouldStreamResourceViaHTTP(m->URL))
							{
								conPrint("Need resource, downloading: " + m->URL);
								this->resource_download_thread_manager.enqueueMessage(new DownloadResourceMessage(m->URL));
							}
						}
					}
				}
			}
			/*else if(dynamic_cast<const ResourceDownloadingStatus*>(msg.getPointer()))
			{
				const ResourceDownloadingStatus* m = msg.downcastToPtr<const ResourceDownloadingStatus>();
				this->total_num_res_to_download = m->total_to_download;
				updateStatusBar();
			}*/
			else if(dynamic_cast<const ResourceDownloadedMessage*>(msg.getPointer()))
			{
				const ResourceDownloadedMessage* m = static_cast<const ResourceDownloadedMessage*>(msg.getPointer());
				const std::string& URL = m->URL;
				logMessage("Resource downloaded: '" + URL + "'");

				if(world_state.nonNull())
				{
					ResourceRef resource = this->resource_manager->getExistingResourceForURL(URL);
					assert(resource.nonNull()); // The downloaded file should have been added as a resource in DownloadResourcesThread or NetDownloadResourcesThread.
					if(resource.nonNull())
					{
						// Get the local path, we will check the file type of the local path when determining what to do with the file, as the local path will have an extension given by the mime type
						// in the net download case.
						const std::string local_path = resource_manager->getLocalAbsPathForResource(*resource);

						bool use_SRGB = true;
						bool allow_compression = true;
						Vec3d pos(0, 0, 0);
						float size_factor = 1;
						bool build_dynamic_physics_ob = false;
						bool is_terrain_map = false;
						bool is_minimap_tile = false;
						// Look up in our map of downloading resources
						auto res = URL_to_downloading_info.find(URL);
						if(res != URL_to_downloading_info.end())
						{
							const DownloadingResourceInfo& info = res->second;
							use_SRGB = info.use_sRGB;
							allow_compression = info.allow_compression;
							pos = info.pos;
							size_factor = info.size_factor;
							build_dynamic_physics_ob = info.build_dynamic_physics_ob;
							is_terrain_map = info.is_terrain_map;
							is_minimap_tile = info.is_minimap_tile;
						}
						else
						{
							assert(0); // If we downloaded the resource we should have added it to URL_to_downloading_info.  NOTE: will this work with NewResourceOnServerMessage tho?
						}

						// If we just downloaded a texture, start loading it.
						// NOTE: Do we want to check this texture is actually used by an object?
						if(ImageDecoding::hasSupportedImageExtension(local_path))
						{
							//conPrint("Downloaded texture resource, loading it...");
						
							const std::string tex_path = local_path;

							if(!ui->glWidget->opengl_engine->isOpenGLTextureInsertedForKey(OpenGLTextureKey(texture_server->keyForPath(tex_path)))) // If texture is not uploaded to GPU already:
							{
								const bool just_added = checkAddTextureToProcessingSet(tex_path); // If not being loaded already:
								if(just_added)
									load_item_queue.enqueueItem(pos.toVec4fPoint(), size_factor, 
										new LoadTextureTask(ui->glWidget->opengl_engine, this->texture_server, &this->msg_queue, tex_path, use_SRGB, allow_compression, is_terrain_map, is_minimap_tile),
										/*max task dist=*/std::numeric_limits<float>::infinity()); // NOTE: inf dist is a bit of a hack.
							}
						}
						else if(FileTypes::hasAudioFileExtension(local_path))
						{
							// Iterate over objects, if any object is using this audio file, load it.
							{
								Lock lock(this->world_state->mutex);

								for(auto it = this->world_state->objects.valuesBegin(); it != this->world_state->objects.valuesEnd(); ++it)
								{
									WorldObject* ob = it.getValue().ptr();

									if(ob->audio_source_url == URL)
										loadAudioForObject(ob);
								}
							}
						}
						else if(ModelLoading::hasSupportedModelExtension(local_path)) // Else we didn't download a texture, but maybe a model:
						{
							try
							{
								// Start loading the model
								Reference<LoadModelTask> load_model_task = new LoadModelTask();

								load_model_task->lod_model_url = URL;
								load_model_task->opengl_engine = this->ui->glWidget->opengl_engine;
								load_model_task->unit_cube_shape = this->unit_cube_shape;
								load_model_task->result_msg_queue = &this->msg_queue;
								load_model_task->resource_manager = resource_manager;
								load_model_task->build_dynamic_physics_ob = build_dynamic_physics_ob;

								load_item_queue.enqueueItem(pos.toVec4fPoint(), size_factor, load_model_task, 
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
				}
			}
			else if(dynamic_cast<TerrainChunkGeneratedMsg*>(msg.ptr()))
			{
				const TerrainChunkGeneratedMsg* m = static_cast<const TerrainChunkGeneratedMsg*>(msg.ptr());
				if(terrain_system.nonNull())
					terrain_system->handleCompletedMakeChunkTask(*m);
			}
		}
	}
}


void MainWindow::updateDiagnostics()
{
	if(ui->diagnosticsDockWidget->isVisible() && (num_frames_since_fps_timer_reset == 1))
	{
		ZoneScopedN("diagnostics"); // Tracy profiler

		//const double fps = num_frames / (double)fps_display_timer.elapsed();
		
		std::string msg;

		if(ui->glWidget->opengl_engine.nonNull() && ui->diagnosticsWidget->graphicsDiagnosticsCheckBox->isChecked())
		{
			msg += "\n------------Graphics------------\n";
			msg += ui->glWidget->opengl_engine->getDiagnostics() + "\n";
			msg += "GL widget valid: " + boolToString(ui->glWidget->isValid()) + "\n";
			//msg += "GL format has OpenGL: " + boolToString(ui->glWidget->format().hasOpenGL()) + "\n";
			msg += "GL format OpenGL profile: " + toString((int)ui->glWidget->format().profile()) + "\n";
			msg += "OpenGL engine initialised: " + boolToString(ui->glWidget->opengl_engine->initSucceeded()) + "\n";
			msg += "--------------------------------\n";
		}

		// Only show physics details when physicsDiagnosticsCheckBox is checked.  Works around problem of physics_world->getDiagnostics() being slow, which causes stutters.
		if(physics_world.nonNull() && ui->diagnosticsWidget->physicsDiagnosticsCheckBox->isChecked())
		{
			msg += "\n------------Physics------------\n";
			msg += physics_world->getDiagnostics();
			msg += "-------------------------------\n";
		}

		if(terrain_system.nonNull())
		{
			msg += "\n------------Terrain------------\n";
			msg += terrain_system->getDiagnostics();
			msg += "--------------------------------\n";
		}

		msg += "FPS: " + doubleToStringNDecimalPlaces(this->last_fps, 1) + "\n";
		msg += "main loop CPU time: " + doubleToStringNSigFigs(this->last_timerEvent_CPU_work_elapsed * 1000, 3) + " ms\n";
		msg += "main loop updateGL time: " + doubleToStringNSigFigs(this->last_updateGL_time * 1000, 3) + " ms\n";
		msg += "last_animated_tex_time: " + doubleToStringNSigFigs(this->last_animated_tex_time * 1000, 3) + " ms\n";
		msg += "last_num_gif_textures_processed: " + toString(last_num_gif_textures_processed) + "\n";
		msg += "last_num_mp4_textures_processed: " + toString(last_num_mp4_textures_processed) + "\n";
		msg += "last_eval_script_time: " + doubleToStringNSigFigs(last_eval_script_time * 1000, 3) + "ms\n";
		msg += "num obs with scripts: " + toString(obs_with_scripts.size()) + "\n";
		msg += "last_num_scripts_processed: " + toString(last_num_scripts_processed) + "\n";
		msg += "last_model_and_tex_loading_time: " + doubleToStringNSigFigs(this->last_model_and_tex_loading_time * 1000, 3) + " ms\n";
		msg += "load_item_queue: " + toString(load_item_queue.size()) + "\n";
		msg += "model_and_texture_loader_task_manager unfinished tasks: " + toString(model_and_texture_loader_task_manager.getNumUnfinishedTasks()) + "\n";
		msg += "model_loaded_messages_to_process: " + toString(model_loaded_messages_to_process.size()) + "\n";
		msg += "texture_loaded_messages_to_process: " + toString(texture_loaded_messages_to_process.size()) + "\n";

		if(texture_server)
			msg += "texture_server total mem usage:         " + getNiceByteSize(this->texture_server->getTotalMemUsage()) + "\n";

		msg += this->mesh_manager.getDiagnostics();


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

		if(selected_ob.nonNull())
		{
			msg += std::string("\nSelected object: \n");

			msg += "pos: " + selected_ob->pos.toStringMaxNDecimalPlaces(3) + "\n";
			msg += "centroid: " + selected_ob->getCentroidWS().toStringMaxNDecimalPlaces(3) + "\n";
			msg += "aabb os: " + selected_ob->getAABBOS().toStringMaxNDecimalPlaces(3) + "\n";
			msg += "aabb ws: " + selected_ob->getAABBWS().toStringMaxNDecimalPlaces(3) + "\n";
			msg += "aabb_ws_longest_len: " + doubleToStringMaxNDecimalPlaces(selected_ob->getAABBWSLongestLength(), 2) + "\n";
			msg += "biased aabb longest len: " + doubleToStringMaxNDecimalPlaces(selected_ob->getBiasedAABBLength(), 2) + "\n";

			msg += "max_model_lod_level: " + toString(selected_ob->max_model_lod_level) + "\n";
			msg += "current_lod_level: " + toString(selected_ob->current_lod_level) + "\n";
			msg += "loaded_model_lod_level: " + toString(selected_ob->loaded_model_lod_level) + "\n";

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
					OpenGLMaterial& mat0 = selected_ob->opengl_engine_ob->materials[0];
					if(mat0.albedo_texture.nonNull())
					{
						if(!selected_ob->materials.empty() && selected_ob->materials[0].nonNull())
							msg += "mat0 min lod level: " + toString(selected_ob->materials[0]->minLODLevel()) + "\n";
						msg += "mat0 tex: " + toString(mat0.albedo_texture->xRes()) + "x" + toString(mat0.albedo_texture->yRes()) + " (" + getNiceByteSize(mat0.albedo_texture->getByteSize()) + ")\n";
					}
					msg += "mat0 colourTexHasAlpha(): " + toString(selected_ob->materials[0]->colourTexHasAlpha()) + "\n";

					if(mat0.lightmap_texture.nonNull())
					{
						msg += "\n";
						msg += "lightmap: " + toString(mat0.lightmap_texture->xRes()) + "x" + toString(mat0.lightmap_texture->yRes()) + " (" + getNiceByteSize(mat0.lightmap_texture->getByteSize()) + ")\n";
					}
				}
				if(selected_ob->materials.size() >= 2)
				{
					msg += "mat1 colourTexHasAlpha(): " + toString(selected_ob->materials[1]->colourTexHasAlpha()) + "\n";
				}
			}
		}


		// Don't update diagnostics string when part of it is selected, so user can actually copy it.
		if(!ui->diagnosticsWidget->diagnosticsTextEdit->textCursor().hasSelection())
			ui->diagnosticsWidget->diagnosticsTextEdit->setPlainText(QtUtils::toQString(msg));
	}
}


void MainWindow::runScreenshotCode()
{
	if(run_as_screenshot_slave || test_screenshot_taking)
	{
		if(screenshot_output_path.empty()) // If we don't have a screenshot command we are currently executing:
		{
			try
			{
				if(test_screenshot_taking || screenshot_command_socket->readable(/*timeout (s)=*/0.01))
				{
					conPrint("Reading command from screenshot_command_socket etc...");
					const std::string command = test_screenshot_taking ? "takescreenshot" : screenshot_command_socket->readStringLengthFirst(1000);
					conPrint("Read screenshot command: " + command);
					if(command == "takescreenshot")
					{
						if(test_screenshot_taking)
						{
							screenshot_campos = Vec3d(0, -1, 100);
							screenshot_camangles = Vec3d(0, 2.5f, 0); // (heading, pitch, roll).
							screenshot_width_px = 1024;
							screenshot_highlight_parcel_id = 10;
							screenshot_output_path = "test_screenshot.jpg";

							screenshot_ortho_sensor_width_m = 100;
							taking_map_screenshot = false;
						}
						else
						{
							screenshot_campos.x = screenshot_command_socket->readDouble();
							screenshot_campos.y = screenshot_command_socket->readDouble();
							screenshot_campos.z = screenshot_command_socket->readDouble();
							screenshot_camangles.x = screenshot_command_socket->readDouble();
							screenshot_camangles.y = screenshot_command_socket->readDouble();
							screenshot_camangles.z = screenshot_command_socket->readDouble();
							screenshot_width_px = screenshot_command_socket->readInt32();
							screenshot_highlight_parcel_id = screenshot_command_socket->readInt32();
							screenshot_output_path = screenshot_command_socket->readStringLengthFirst(1000);
							taking_map_screenshot = false;
						}
					}
					else if(command == "takemapscreenshot")
					{
						int tile_x, tile_y, tile_z;
						if(test_screenshot_taking)
						{
							tile_x = 0;
							tile_y = 0;
							tile_z = 7;
							screenshot_output_path = "test_screenshot.jpg";
						}
						else
						{
							tile_x = screenshot_command_socket->readInt32();
							tile_y = screenshot_command_socket->readInt32();
							tile_z = screenshot_command_socket->readInt32();
							screenshot_output_path = screenshot_command_socket->readStringLengthFirst(1000);
						}

						const int TILE_WIDTH_PX = 256; // Works the easiest with leaflet.js
						const float TILE_WIDTH_M = 5120.f / (1 << tile_z);
						screenshot_campos = Vec3d(
							(tile_x + 0.5) * TILE_WIDTH_M,
							(tile_y + 0.5) * TILE_WIDTH_M,
							450.0
						);
						screenshot_camangles = Vec3d(
							0, // Heading
							3.14, // pitch
							0 // roll
						);
						screenshot_ortho_sensor_width_m = TILE_WIDTH_M;
						screenshot_width_px = TILE_WIDTH_PX;
						screenshot_highlight_parcel_id = -1;
						taking_map_screenshot = true;
					}
					else if(command == "quit")
					{
						conPrint("Received quit command, exiting...");
						exit(1);
					}
					else
						throw glare::Exception("received invalid screenshot command.");
				}
			}
			catch(glare::Exception& e)
			{
				conPrint("Excep while reading screenshot command from screenshot_command_socket: " + e.what() + ", exiting!");
				//QMessageBox msgBox;
				//msgBox.setWindowTitle("Error");
				//msgBox.setText(QtUtils::toQString("Excep while reading screenshot command from screenshot_command_socket: " + e.what()));
				//msgBox.exec();
				exit(1);
			}
		}
	}
	if(!screenshot_output_path.empty() && world_state.nonNull())
	{
		if(!screenshot_output_path.empty()) // If we are in screenshot-taking mode:
		{
			this->cam_controller.setPosition(screenshot_campos);
			this->cam_controller.setAngles(screenshot_camangles);
			this->player_physics.setPosition(screenshot_campos);

			// Enable fly mode so we don't just fall to the ground
			ui->actionFly_Mode->setChecked(true);
			this->player_physics.setFlyModeEnabled(true);
			this->cam_controller.setThirdPersonEnabled(false);
			ui->actionThird_Person_Camera->setChecked(false);
		}

		size_t num_obs;
		{
			Lock lock(this->world_state->mutex);
			num_obs = world_state->objects.size();
		}

		const bool map_screenshot = taking_map_screenshot;//parsed_args.isArgPresent("--takemapscreenshot");

		ui->glWidget->take_map_screenshot = map_screenshot;
		ui->glWidget->screenshot_ortho_sensor_width_m = screenshot_ortho_sensor_width_m;

		const size_t num_model_and_tex_tasks = load_item_queue.size() + model_and_texture_loader_task_manager.getNumUnfinishedTasks() + model_loaded_messages_to_process.size();

		if(time_since_last_waiting_msg.elapsed() > 2.0)
		{
			conPrint("---------------Waiting for loading to be done for screenshot ---------------");
			printVar(num_obs);
			printVar(num_model_and_tex_tasks);
			printVar(num_non_net_resources_downloading);
			printVar(num_net_resources_downloading);

			time_since_last_waiting_msg.reset();
		}

		const bool loaded_all =
			(time_since_last_screenshot.elapsed() > 4.0) && // Bit of a hack to allow time for the shadow mapping to render properly
			(num_obs > 0 || total_timer.elapsed() >= 15) && // Wait until we have downloaded some objects from the server, or (if the world is empty) X seconds have elapsed.
			(total_timer.elapsed() >= 8) && // Bit of a hack to allow time for the shadow mapping to render properly, also for the initial object query responses to arrive
			(num_model_and_tex_tasks == 0) &&
			(num_non_net_resources_downloading == 0) &&
			(num_net_resources_downloading == 0) &&
			(terrain_system.nonNull() && terrain_system->isTerrainFullyBuilt());

		if(loaded_all)
		{
			if(!done_screenshot_setup)
			{
				conPrint("Setting up for screenshot...");

				ui->editorDockWidget->hide();
				ui->chatDockWidget->hide();
				ui->diagnosticsDockWidget->hide();

				const int target_viewport_w = map_screenshot ? (screenshot_width_px * 2) : (650 * 2); // Existing screenshots are 650 px x 437 px.
				const int target_viewport_h = map_screenshot ? (screenshot_width_px * 2) : (437 * 2);

				conPrint("Setting geometry size...");
				// Make the gl widget a certain size so that the screenshot size / aspect ratio is consistent.
				ui->glWidget->setGeometry(0, 0, target_viewport_w, target_viewport_h);
				setUpForScreenshot();
			}
			else
			{
				try
				{
					saveScreenshot();

					// Reset screenshot state
					screenshot_output_path.clear();
					done_screenshot_setup = false;

					time_since_last_screenshot.reset();

					if(screenshot_command_socket.nonNull())
					{
						screenshot_command_socket->writeInt32(0); // Write success msg
						screenshot_command_socket->writeStringLengthFirst("Success!");
					}
				}
				catch(glare::Exception& e)
				{
					conPrint("Excep while saving screenshot: " + e.what());

					// Reset screenshot state
					screenshot_output_path.clear();
					done_screenshot_setup = false;

					time_since_last_screenshot.reset();

					if(screenshot_command_socket.nonNull())
					{
						screenshot_command_socket->writeInt32(1); // Write failure msg
						screenshot_command_socket->writeStringLengthFirst("Exception encountered: " + e.what());
					}
				}
			}
		}
	}
}


void MainWindow::physicsObjectEnteredWater(PhysicsObject& physics_ob)
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
void MainWindow::contactAdded(const JPH::Body &inBody1, const JPH::Body &inBody2, const JPH::ContactManifold& contact_manifold)
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
void MainWindow::contactPersisted(const JPH::Body &inBody1, const JPH::Body &inBody2, const JPH::ContactManifold& contact_manifold)
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


static const double PHYSICS_ONWERSHIP_PERIOD = 10.0;

bool MainWindow::isObjectPhysicsOwnedBySelf(WorldObject& ob, double global_time) const
{
	return (ob.physics_owner_id == (uint32)this->client_avatar_uid.value()) && 
		((global_time - ob.last_physics_ownership_change_global_time) < PHYSICS_ONWERSHIP_PERIOD);
}



bool MainWindow::isObjectPhysicsOwnedByOther(WorldObject& ob, double global_time) const
{
	return (ob.physics_owner_id != std::numeric_limits<uint32>::max()) && // If the owner is a valid UID,
		(ob.physics_owner_id != (uint32)this->client_avatar_uid.value()) && // and the owner is not us,
		((global_time - ob.last_physics_ownership_change_global_time) < PHYSICS_ONWERSHIP_PERIOD); // And the ownership is still valid
}


bool MainWindow::isObjectPhysicsOwned(WorldObject& ob, double global_time)
{
	return (ob.physics_owner_id != std::numeric_limits<uint32>::max()) && // If the owner is a valid UID,
		((global_time - ob.last_physics_ownership_change_global_time) < PHYSICS_ONWERSHIP_PERIOD); // And the ownership is still valid
}


bool MainWindow::isObjectVehicleBeingDrivenByOther(WorldObject& ob)
{
	return doesVehicleHaveAvatarInSeat(ob, /*seat_index=*/0);
}


bool MainWindow::doesVehicleHaveAvatarInSeat(WorldObject& ob, uint32 seat_index) const
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
void MainWindow::destroyVehiclePhysicsControllingObject(WorldObject* ob)
{
	vehicle_controllers.erase(ob);

	if(vehicle_controller_inside.nonNull() && vehicle_controller_inside->getControlledObject() == ob)
		vehicle_controller_inside = NULL;
}


void MainWindow::takePhysicsOwnershipOfObject(WorldObject& ob, double global_time)
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


void MainWindow::checkRenewalOfPhysicsOwnershipOfObject(WorldObject& ob, double global_time)
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
void MainWindow::updateVoxelEditMarkers()
{
	bool should_display_voxel_edit_marker = false;
	bool should_display_voxel_edit_face_marker = false;
	if(areEditingVoxels())
	{
		// NOTE: Stupid qt: QApplication::keyboardModifiers() doesn't update properly when just CTRL is pressed/released, without any other events.
		// So use GetAsyncKeyState on Windows, since it actually works.
#if defined(_WIN32)
		const bool ctrl_key_down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
		const bool alt_key_down = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0; // alt = VK_MENU
#else
		const Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
		const bool ctrl_key_down = (modifiers & Qt::ControlModifier) != 0;
		const bool alt_key_down  = (modifiers & Qt::AltModifier)     != 0;
#endif

		if(ctrl_key_down || alt_key_down)
		{
			const QPoint mouse_point = ui->glWidget->mapFromGlobal(QCursor::pos());

			const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
			const Vec4f dir = getDirForPixelTrace(mouse_point.x(), mouse_point.y());
			RayTraceResult results;
			this->physics_world->traceRay(origin, dir, /*max_t=*/1.0e5f, results);
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

						if(ctrl_key_down)
						{
							const Vec4f point_off_surface = hitpos_ws + results.hit_normal_ws * (current_voxel_w * 1.0e-3f);
							const Vec4f point_os = world_to_ob * point_off_surface;
							const Vec4f point_os_voxel_space = point_os / current_voxel_w;
							Vec3<int> voxel_indices((int)floor(point_os_voxel_space[0]), (int)floor(point_os_voxel_space[1]), (int)floor(point_os_voxel_space[2]));

							this->voxel_edit_marker->ob_to_world_matrix = ob_to_world * Matrix4f::translationMatrix(voxel_indices.x * current_voxel_w, voxel_indices.y * current_voxel_w, voxel_indices.z * current_voxel_w) *
								Matrix4f::uniformScaleMatrix(current_voxel_w);
							if(!voxel_edit_marker_in_engine)
							{
								this->ui->glWidget->opengl_engine->addObject(this->voxel_edit_marker);
								this->voxel_edit_marker_in_engine = true;
							}
							else
							{
								this->ui->glWidget->opengl_engine->updateObjectTransformData(*this->voxel_edit_marker);
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
								this->ui->glWidget->opengl_engine->addObject(this->voxel_edit_face_marker);
								voxel_edit_face_marker_in_engine = true;
							}
							else
							{
								this->ui->glWidget->opengl_engine->updateObjectTransformData(*this->voxel_edit_face_marker);
							}

							should_display_voxel_edit_marker = true;
							should_display_voxel_edit_face_marker = true;

							this->voxel_edit_marker->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(0.1, 0.9, 0.2));
							this->ui->glWidget->opengl_engine->objectMaterialsUpdated(*this->voxel_edit_marker);

						}
						else if(alt_key_down)
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
								this->ui->glWidget->opengl_engine->addObject(this->voxel_edit_marker);
								this->voxel_edit_marker_in_engine = true;
							}
							else
							{
								this->ui->glWidget->opengl_engine->updateObjectTransformData(*this->voxel_edit_marker);
							}

							should_display_voxel_edit_marker = true;
							
							this->voxel_edit_marker->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(0.9, 0.1, 0.1));
							this->ui->glWidget->opengl_engine->objectMaterialsUpdated(*this->voxel_edit_marker);
						}
					}
				}
			}
		}
	}

	// Remove edit markers from 3d engine if they shouldn't be displayed currently.
	if(voxel_edit_marker_in_engine && !should_display_voxel_edit_marker)
	{
		this->ui->glWidget->opengl_engine->removeObject(this->voxel_edit_marker);
		voxel_edit_marker_in_engine = false;
	}
	if(voxel_edit_face_marker_in_engine && !should_display_voxel_edit_face_marker)
	{
		this->ui->glWidget->opengl_engine->removeObject(this->voxel_edit_face_marker);
		voxel_edit_face_marker_in_engine = false;
	}
}


static std::string printableServerURL(const std::string& hostname, const std::string& userpath)
{
	if(userpath.empty())
		return hostname;
	else
		return hostname + "/" + userpath;
}


void MainWindow::updateStatusBar()
{
	ZoneScoped; // Tracy profiler

	std::string status;
	switch(this->connection_state)
	{
	case ServerConnectionState_NotConnected:
		status += "Not connected to server.";
		break;
	case ServerConnectionState_Connecting:
		status += "Connecting to " + printableServerURL(this->server_hostname, this->server_worldname) + "...";
		break;
	case ServerConnectionState_Connected:
		status += "Connected to " + printableServerURL(this->server_hostname, this->server_worldname);
		break;
	}

	const int total_num_non_net_resources_downloading = (int)num_non_net_resources_downloading + (int)this->download_queue.size();
	if(total_num_non_net_resources_downloading > 0)
		status += " | Downloading " + toString(total_num_non_net_resources_downloading) + ((total_num_non_net_resources_downloading == 1) ? " resource..." : " resources...");

	if(num_net_resources_downloading > 0)
		status += " | Downloading " + toString(num_net_resources_downloading) + ((num_net_resources_downloading == 1) ? " web resource..." : " web resources...");

	if(num_resources_uploading > 0)
		status += " | Uploading " + toString(num_resources_uploading) + ((num_resources_uploading == 1) ? " resource..." : " resources...");

	const size_t num_model_and_tex_tasks = load_item_queue.size() + model_and_texture_loader_task_manager.getNumUnfinishedTasks() + (model_loaded_messages_to_process.size() + texture_loaded_messages_to_process.size());
	if(num_model_and_tex_tasks > 0)
		status += " | Loading " + toString(num_model_and_tex_tasks) + ((num_model_and_tex_tasks == 1) ? " model or texture..." : " models and textures...");

	this->statusBar()->showMessage(QtUtils::toQString(status));
}


void MainWindow::on_actionAvatarSettings_triggered()
{
	AvatarSettingsDialog dialog(this->base_dir_path, this->settings, this->texture_server, this->resource_manager);
	const int res = dialog.exec();
	ui->glWidget->makeCurrent();// Change back from the dialog GL context to the mainwindow GL context.

	if((res == QDialog::Accepted) && dialog.loaded_mesh.nonNull()) //  loaded_object.nonNull()) // If the dialog was accepted, and we loaded something:
	{
		try
		{
			if(!this->logged_in_user_id.valid())
				throw glare::Exception("You must be logged in to set your avatar model");

			std::string mesh_URL;
			if(dialog.result_path != "")
			{
				// If the user selected a mesh that is not a bmesh, convert it to bmesh
				std::string bmesh_disk_path = dialog.result_path;
				if(!hasExtension(dialog.result_path, "bmesh"))
				{
					// Save as bmesh in temp location
					bmesh_disk_path = PlatformUtils::getTempDirPath() + "/temp.bmesh";

					BatchedMesh::WriteOptions write_options;
					write_options.compression_level = 9; // Use a somewhat high compression level, as this mesh is likely to be read many times, and only encoded here.
					// TODO: show 'processing...' dialog while it compresses and saves?
					dialog.loaded_mesh->writeToFile(bmesh_disk_path, write_options);
				}
				else
				{
					bmesh_disk_path = dialog.result_path;
				}

				// Compute hash over model
				const uint64 model_hash = FileChecksum::fileChecksum(bmesh_disk_path);

				const std::string original_filename = FileUtils::getFilename(dialog.result_path); // Use the original filename, not 'temp.igmesh'.
				mesh_URL = ResourceManager::URLForNameAndExtensionAndHash(original_filename, ::getExtension(bmesh_disk_path), model_hash); // ResourceManager::URLForPathAndHash(igmesh_disk_path, model_hash);

				// Copy model to local resources dir.  UploadResourceThread will read from here.
				this->resource_manager->copyLocalFileToResourceDir(bmesh_disk_path, mesh_URL);
			}

			const Vec3d cam_angles = this->cam_controller.getAngles();
			Avatar avatar;
			avatar.uid = this->client_avatar_uid;
			avatar.pos = Vec3d(this->cam_controller.getFirstPersonPosition());
			avatar.rotation = Vec3f(0, (float)cam_angles.y, (float)cam_angles.x);
			avatar.name = this->logged_in_user_name;
			avatar.avatar_settings.model_url = mesh_URL;
			avatar.avatar_settings.pre_ob_to_world_matrix = dialog.pre_ob_to_world_matrix;
			avatar.avatar_settings.materials = dialog.loaded_materials;


			// Copy all dependencies (textures etc..) to resources dir.  UploadResourceThread will read from here.
			std::set<DependencyURL> paths;
			avatar.getDependencyURLSet(/*ob_lod_level=*/0, paths);
			for(auto it = paths.begin(); it != paths.end(); ++it)
			{
				const std::string path = it->URL;
				if(FileUtils::fileExists(path))
				{
					const uint64 hash = FileChecksum::fileChecksum(path);
					const std::string resource_URL = ResourceManager::URLForPathAndHash(path, hash);
					this->resource_manager->copyLocalFileToResourceDir(path, resource_URL);
				}
			}

			// Convert texture paths on the object to URLs
			avatar.convertLocalPathsToURLS(*this->resource_manager);

			if(!task_manager)
				task_manager = new glare::TaskManager("mainwindow general task manager", myClamp<size_t>(PlatformUtils::getNumLogicalProcessors() / 2, 1, 8)), // Currently just used for LODGeneration::generateLODTexturesForMaterialsIfNotPresent().

			// Generate LOD textures for materials, if not already present on disk.
			LODGeneration::generateLODTexturesForMaterialsIfNotPresent(avatar.avatar_settings.materials, *resource_manager, *task_manager);

			// Send AvatarFullUpdate message to server
			MessageUtils::initPacket(scratch_packet, Protocol::AvatarFullUpdate);
			writeAvatarToNetworkStream(avatar, scratch_packet);

			enqueueMessageToSend(*this->client_thread, scratch_packet);

			showInfoNotification("Updated avatar.");
		}
		catch(glare::Exception& e)
		{
			// Show error
			print(e.what());
			QErrorMessage m;
			m.showMessage(QtUtils::toQString(e.what()));
			m.exec();
		}
	}
}


// Returns true if this user has permissions to create an object at new_ob_pos
bool MainWindow::haveParcelObjectCreatePermissions(const Vec3d& new_ob_pos, bool& ob_pos_in_parcel_out)
{
	ob_pos_in_parcel_out = false;

	if(isGodUser(this->logged_in_user_id))
	{
		ob_pos_in_parcel_out = true; // Just treat as in parcel.
		return true;
	}

	// If this is the personal world of the user:
	if(server_worldname != "" && server_worldname == this->logged_in_user_name)
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


bool MainWindow::haveObjectWritePermissions(const WorldObject& ob, const js::AABBox& new_aabb_ws, bool& ob_pos_in_parcel_out)
{
	ob_pos_in_parcel_out = false;

	if(isGodUser(this->logged_in_user_id))
	{
		ob_pos_in_parcel_out = true; // Just treat as in parcel.
		return true;
	}

	// If this is the personal world of the user:
	if(server_worldname != "" && server_worldname == this->logged_in_user_name)
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
bool MainWindow::clampObjectPositionToParcelForNewTransform(const WorldObject& ob, GLObjectRef& opengl_ob, const Vec3d& old_ob_pos,
	const Matrix4f& tentative_to_world_matrix,
	js::Vector<EdgeMarker, 16>& edge_markers_out, Vec3d& new_ob_pos_out)
{
	edge_markers_out.resize(0);
	bool have_creation_perms = false;
	Vec3d parcel_aabb_min;
	Vec3d parcel_aabb_max;

	if(isGodUser(this->logged_in_user_id) || (server_worldname != "" && server_worldname == this->logged_in_user_name) || // If god user, or if this is the personal world of the user:
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
		const js::AABBox ten_new_aabb_ws = ui->glWidget->opengl_engine->getAABBWSForObjectWithTransform(*opengl_ob, 
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
void MainWindow::setMaterialFlagsForObject(WorldObject* ob)
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
						const std::string local_tex_path = mat->colour_texture_url;
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
void MainWindow::createObject(const std::string& mesh_path, BatchedMeshRef loaded_mesh, bool loaded_mesh_is_image_cube,
	const js::Vector<Voxel, 16>& decompressed_voxels, const Vec3d& ob_pos, const Vec3f& scale, const Vec3f& axis, float angle, const std::vector<WorldMaterialRef>& materials)
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
		const std::string mesh_URL = ResourceManager::URLForNameAndExtensionAndHash(original_filename, ::getExtension(bmesh_disk_path), model_hash); // Make a URL like "projectdog_png_5624080605163579508.png"

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
	std::set<DependencyURL> paths;
	new_world_object->getDependencyURLSetBaseLevel(options, paths);
	for(auto it = paths.begin(); it != paths.end(); ++it)
	{
		const std::string path = it->URL;
		if(FileUtils::fileExists(path))
		{
			const uint64 hash = FileChecksum::fileChecksum(path);
			const std::string resource_URL = ResourceManager::URLForPathAndHash(path, hash);
			this->resource_manager->copyLocalFileToResourceDir(path, resource_URL);
		}
	}

	// Convert texture paths on the object to URLs
	new_world_object->convertLocalPathsToURLS(*this->resource_manager);

	if(!task_manager)
		task_manager = new glare::TaskManager("mainwindow general task manager", myClamp<size_t>(PlatformUtils::getNumLogicalProcessors() / 2, 1, 8)), // Currently just used for LODGeneration::generateLODTexturesForMaterialsIfNotPresent().

	// Generate LOD textures for materials, if not already present on disk.
	// Note that server will also generate LOD textures, however the client may want to display a particular LOD texture immediately, so generate on the client as well.
	LODGeneration::generateLODTexturesForMaterialsIfNotPresent(new_world_object->materials, *resource_manager, *task_manager);

	// Send CreateObject message to server
	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		new_world_object->writeToNetworkStream(scratch_packet);

		enqueueMessageToSend(*this->client_thread, scratch_packet);
	}
}


void MainWindow::on_actionAddObject_triggered()
{
	const Vec3d ob_pos = this->cam_controller.getFirstPersonPosition() + this->cam_controller.getForwardsVec() * 2.0f;

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

	AddObjectDialog dialog(this->base_dir_path, this->settings, this->texture_server, this->resource_manager, 
#ifdef _WIN32
		this->device_manager.ptr
#else
		NULL
#endif
	);
	const int res = dialog.exec();
	ui->glWidget->makeCurrent(); // Change back from the dialog GL context to the mainwindow GL context.

	if((res == QDialog::Accepted) && !dialog.loaded_materials.empty()) // If dialog was accepted, and we loaded an object successfully in it:
	{
		try
		{
			const Vec3d adjusted_ob_pos = ob_pos + cam_controller.getRightVec() * dialog.ob_cam_right_translation + cam_controller.getUpVec() * dialog.ob_cam_up_translation; // Centre object in front of camera

			// Some mesh types have a rotation to bring them to our z-up convention.  Don't change the rotation on those.
			Vec3f axis(0, 0, 1);
			float angle = 0;
			if(dialog.axis == Vec3f(0, 0, 1))
			{
				// If we don't have a rotation to z-up, make object face camera.
				angle = Maths::roundToMultipleFloating((float)this->cam_controller.getAngles().x - Maths::pi_2<float>(), Maths::pi_4<float>()); // Round to nearest 45 degree angle, facing camera.
			}
			else
			{
				axis = dialog.axis;
				angle = dialog.angle;
			}

			createObject(
				dialog.result_path,
				dialog.loaded_mesh,
				dialog.loaded_mesh_is_image_cube,
				dialog.loaded_voxels,
				adjusted_ob_pos,
				dialog.scale,
				axis,
				angle,
				dialog.loaded_materials
			);
		}
		catch(glare::Exception& e)
		{
			// Show error
			print(e.what());
			QErrorMessage m;
			m.showMessage(QtUtils::toQString(e.what()));
			m.exec();
		}
	}
}


void MainWindow::on_actionAddHypercard_triggered()
{
	const float quad_w = 0.4f;
	const Vec3d ob_pos = this->cam_controller.getFirstPersonPosition() + this->cam_controller.getForwardsVec() * 2.0f -
		this->cam_controller.getUpVec() * quad_w * 0.5f -
		this->cam_controller.getRightVec() * quad_w * 0.5f;

	// Check permissions
	bool ob_pos_in_parcel;
	const bool have_creation_perms = haveParcelObjectCreatePermissions(ob_pos, ob_pos_in_parcel);
	if(!have_creation_perms)
	{
		if(ob_pos_in_parcel)
			showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
		else
			showErrorNotification("You can only create hypercards in a parcel that you have write permissions for.");
		return;
	}

	WorldObjectRef new_world_object = new WorldObject();
	new_world_object->uid = UID(0); // Will be set by server
	new_world_object->object_type = WorldObject::ObjectType_Hypercard;
	new_world_object->pos = ob_pos;
	new_world_object->axis = Vec3f(0, 0, 1);
	new_world_object->angle = Maths::roundToMultipleFloating((float)this->cam_controller.getAngles().x - Maths::pi_2<float>(), Maths::pi_4<float>()); // Round to nearest 45 degree angle.
	new_world_object->scale = Vec3f(0.4f);
	new_world_object->content = "Select the object \nto edit this text";
	new_world_object->setAABBOS(js::AABBox(Vec4f(0,0,0,1), Vec4f(1,0,1,1)));

	// Send CreateObject message to server
	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		new_world_object->writeToNetworkStream(scratch_packet);

		enqueueMessageToSend(*this->client_thread, scratch_packet);
	}

	showInfoNotification("Added hypercard.");
}


void MainWindow::on_actionAdd_Spotlight_triggered()
{
	const float quad_w = 0.4f;
	const Vec3d ob_pos = this->cam_controller.getFirstPersonPosition() + this->cam_controller.getForwardsVec() * 2.0f -
		this->cam_controller.getUpVec() * quad_w * 0.5f -
		this->cam_controller.getRightVec() * quad_w * 0.5f;

	// Check permissions
	bool ob_pos_in_parcel;
	const bool have_creation_perms = haveParcelObjectCreatePermissions(ob_pos, ob_pos_in_parcel);
	if(!have_creation_perms)
	{
		if(ob_pos_in_parcel)
			showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
		else
			showErrorNotification("You can only create spotlights in a parcel that you have write permissions for.");
		return;
	}

	WorldObjectRef new_world_object = new WorldObject();
	new_world_object->uid = UID(0); // Will be set by server
	new_world_object->object_type = WorldObject::ObjectType_Spotlight;
	new_world_object->pos = ob_pos;
	new_world_object->axis = Vec3f(0, 0, 1);
	new_world_object->angle = 0;
	new_world_object->scale = Vec3f(1.f);

	// Emitting material
	new_world_object->materials.push_back(new WorldMaterial());
	new_world_object->materials.back()->emission_lum_flux_or_lum = 100000.f;

	// Spotlight housing material
	new_world_object->materials.push_back(new WorldMaterial());

	const float fixture_w = 0.1;
	const js::AABBox aabb_os = js::AABBox(Vec4f(-fixture_w/2, -fixture_w/2, 0,1), Vec4f(fixture_w/2,  fixture_w/2, 0,1));
	new_world_object->setAABBOS(aabb_os);


	// Send CreateObject message to server
	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		new_world_object->writeToNetworkStream(scratch_packet);

		enqueueMessageToSend(*this->client_thread, scratch_packet);
	}

	showInfoNotification("Added spotlight.");
}


void MainWindow::on_actionAdd_Web_View_triggered()
{
	const float quad_w = 0.4f;
	const Vec3d ob_pos = this->cam_controller.getFirstPersonPosition() + this->cam_controller.getForwardsVec() * 2.0f -
		this->cam_controller.getUpVec() * quad_w * 0.5f -
		this->cam_controller.getRightVec() * quad_w * 0.5f;

	// Check permissions
	bool ob_pos_in_parcel;
	const bool have_creation_perms = haveParcelObjectCreatePermissions(ob_pos, ob_pos_in_parcel);
	if(!have_creation_perms)
	{
		if(ob_pos_in_parcel)
			showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
		else
			showErrorNotification("You can only create web views in a parcel that you have write permissions for.");
		return;
	}

	WorldObjectRef new_world_object = new WorldObject();
	new_world_object->uid = UID(0); // Will be set by server
	new_world_object->object_type = WorldObject::ObjectType_WebView;
	new_world_object->pos = ob_pos;
	new_world_object->axis = Vec3f(0, 0, 1);
	new_world_object->angle = Maths::roundToMultipleFloating((float)this->cam_controller.getAngles().x - Maths::pi_2<float>(), Maths::pi_4<float>()); // Round to nearest 45 degree angle.
	new_world_object->scale = Vec3f(/*width=*/1.f, /*depth=*/0.02f, /*height=*/1080.f / 1920.f);
	new_world_object->max_model_lod_level = 0;

	new_world_object->target_url = "https://substrata.info/"; // Use a default URL - indicates to users how to set the URL.

	new_world_object->materials.resize(2);
	new_world_object->materials[0] = new WorldMaterial();
	new_world_object->materials[0]->colour_rgb = Colour3f(1.f);
	new_world_object->materials[1] = new WorldMaterial();

	const js::AABBox aabb_os = this->image_cube_shape.getAABBOS();
	new_world_object->setAABBOS(aabb_os);


	// Send CreateObject message to server
	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		new_world_object->writeToNetworkStream(scratch_packet);

		enqueueMessageToSend(*this->client_thread, scratch_packet);
	}

	showInfoNotification("Added web view.");
}


void MainWindow::on_actionAdd_Video_triggered()
{
	try
	{
		const float quad_w = 0.4f;
		const Vec3d ob_pos = this->cam_controller.getFirstPersonPosition() + this->cam_controller.getForwardsVec() * 2.0f -
			this->cam_controller.getUpVec() * quad_w * 0.5f -
			this->cam_controller.getRightVec() * quad_w * 0.5f;

		// Check permissions
		bool ob_pos_in_parcel;
		const bool have_creation_perms = haveParcelObjectCreatePermissions(ob_pos, ob_pos_in_parcel);
		if(!have_creation_perms)
		{
			if(ob_pos_in_parcel)
				showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
			else
				showErrorNotification("You can only create videos in a parcel that you have write permissions for.");
			return;
		}

		AddVideoDialog dialog(this->settings, this->resource_manager, 
#ifdef _WIN32
			this->device_manager.ptr
#else
			NULL
#endif
		);
		const int res = dialog.exec();

		if(res == QDialog::Accepted)
		{
			std::string use_URL;
			if(dialog.wasResultLocalPath())
			{
				if(dialog.getVideoLocalPath() == "")
					return;

				// Copy model to local resources dir if not already there.  UploadResourceThread will read from here.
				use_URL = this->resource_manager->copyLocalFileToResourceDirIfNotPresent(dialog.getVideoLocalPath());
			}
			else
			{
				if(dialog.getVideoURL() == "")
					return;

				use_URL = dialog.getVideoURL();
			}

			WorldObjectRef new_world_object = new WorldObject();
			new_world_object->uid = UID(0); // Will be set by server
			new_world_object->object_type = WorldObject::ObjectType_Video;
			new_world_object->pos = ob_pos;
			new_world_object->axis = Vec3f(0, 0, 1);
			new_world_object->angle = Maths::roundToMultipleFloating((float)this->cam_controller.getAngles().x - Maths::pi_2<float>(), Maths::pi_4<float>()); // Round to nearest 45 degree angle.
			new_world_object->scale = Vec3f(/*width=*/1.f, /*depth=*/0.02f, /*height=*/(float)dialog.video_height / dialog.video_width); // NOTE: matches Youtube aspect ratio of 16:9.
			new_world_object->max_model_lod_level = 0;

			BitUtils::setBit(new_world_object->flags, WorldObject::VIDEO_AUTOPLAY);

			new_world_object->materials.resize(2);
			new_world_object->materials[0] = new WorldMaterial();
			new_world_object->materials[0]->colour_rgb = Colour3f(0.f);
			new_world_object->materials[0]->emission_lum_flux_or_lum = 20000; // NOTE: this material data is generally not used in loadModelForObject() for ObjectType_Video.
			new_world_object->materials[0]->emission_rgb = Colour3f(1.f);
			new_world_object->materials[0]->emission_texture_url = use_URL; // Video URL is stored in emission_texture_url. (need to put here so it is a resource dependency)
			new_world_object->materials[1] = new WorldMaterial();

			const js::AABBox aabb_os = this->image_cube_shape.getAABBOS();
			new_world_object->setAABBOS(aabb_os);


			// Send CreateObject message to server
			{
				MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
				new_world_object->writeToNetworkStream(scratch_packet);

				enqueueMessageToSend(*this->client_thread, scratch_packet);
			}

			showInfoNotification("Added video.");
		}
	}
	catch(glare::Exception& e)
	{
		// Show error
		print(e.what());
		QErrorMessage m;
		m.showMessage(QtUtils::toQString(e.what()));
		m.exec();
	}
}


void MainWindow::on_actionAdd_Audio_Source_triggered()
{
	try
	{
		const float quad_w = 0.0f;
		const Vec3d ob_pos = this->cam_controller.getFirstPersonPosition() + this->cam_controller.getForwardsVec() * 2.0f -
			this->cam_controller.getUpVec() * quad_w * 0.5f -
			this->cam_controller.getRightVec() * quad_w * 0.5f;

		// Check permissions
		bool ob_pos_in_parcel;
		const bool have_creation_perms = haveParcelObjectCreatePermissions(ob_pos, ob_pos_in_parcel);
		if(!have_creation_perms)
		{
			if(ob_pos_in_parcel)
				showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
			else
				showErrorNotification("You can only create audio sources in a parcel that you have write permissions for.");
			return;
		}

		const QString last_audio_dir = settings->value("mainwindow/lastAudioFileDir").toString();

		QFileDialog::Options options;
		QString selected_filter;
		const QString selected_filename = QFileDialog::getOpenFileName(this,
			tr("Select audio file..."),
			last_audio_dir,
			tr("Audio file (*.mp3 *.wav)"), // tr("Audio file (*.mp3 *.m4a *.aac *.wav)"),
			&selected_filter,
			options
		);

		if(selected_filename != "")
		{
			settings->setValue("mainwindow/lastAudioFileDir", QtUtils::toQString(FileUtils::getDirectory(QtUtils::toIndString(selected_filename))));

			const std::string path = QtUtils::toStdString(selected_filename);

			// Compute hash over audio file
			const uint64 audio_file_hash = FileChecksum::fileChecksum(path);

			const std::string audio_file_URL = ResourceManager::URLForPathAndHash(path, audio_file_hash);

			// Copy audio file to local resources dir.  UploadResourceThread will read from here.
			this->resource_manager->copyLocalFileToResourceDir(path, audio_file_URL);

			const std::string model_obj_path = base_dir_path + "/resources/models/Capsule.obj";
			const std::string model_URL = "Capsule_obj_7611321750126528672.bmesh"; // NOTE: Assuming server already has capsule mesh as a resource

		
			WorldObjectRef new_world_object = new WorldObject();
			new_world_object->uid = UID(0); // Will be set by server
			new_world_object->object_type = WorldObject::ObjectType_Generic;
			new_world_object->pos = ob_pos;
			new_world_object->axis = Vec3f(0, 0, 1);
			new_world_object->angle = 0;
			new_world_object->scale = Vec3f(0.2f);
			new_world_object->model_url = model_URL;
			new_world_object->audio_source_url = audio_file_URL;
			new_world_object->materials.resize(1);
			new_world_object->materials[0] = new WorldMaterial();
			new_world_object->materials[0]->colour_rgb = Colour3f(1,0,0);

			// Set aabb_ws
			
			/*WorldObject loaded_object;
			BatchedMeshRef batched_mesh;
			ModelLoading::makeGLObjectForModelFile(task_manager, model_obj_path, batched_mesh, loaded_object);
			if(batched_mesh.nonNull())
			{
				const js::AABBox aabb_os = batched_mesh->aabb_os;
				new_world_object->aabb_ws = aabb_os.transformedAABB(obToWorldMatrix(*new_world_object));
			}*/

			const js::AABBox aabb_os(Vec4f(-0.25f, -0.25f, -0.5f, 1.0f), Vec4f(0.25f, 0.25f, 0.5f, 1.0f)); // AABB os of capsule.obj
			new_world_object->setAABBOS(aabb_os);


			// Send CreateObject message to server
			{
				MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
				new_world_object->writeToNetworkStream(scratch_packet);

				enqueueMessageToSend(*this->client_thread, scratch_packet);
			}

			showInfoNotification("Added audio source.");
		}
	}
	catch(glare::Exception& e)
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Error");
		msgBox.setText(QtUtils::toQString(e.what()));
		msgBox.exec();
	}
}


void MainWindow::on_actionAdd_Voxels_triggered()
{
	// Offset down by 0.25 to allow for centering with voxel width of 0.5.
	const Vec3d ob_pos = this->cam_controller.getFirstPersonPosition() + this->cam_controller.getForwardsVec() * 2.0f - Vec3d(0.25, 0.25, 0.25);

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

	
	WorldObjectRef new_world_object = new WorldObject();
	new_world_object->uid = UID(0); // Will be set by server
	new_world_object->object_type = WorldObject::ObjectType_VoxelGroup;
	new_world_object->materials.resize(1);
	new_world_object->materials[0] = new WorldMaterial();
	new_world_object->pos = ob_pos;
	new_world_object->axis = Vec3f(0, 0, 1);
	new_world_object->angle = 0;
	new_world_object->scale = Vec3f(0.5f); // This will be the initial width of the voxels
	new_world_object->getDecompressedVoxels().push_back(Voxel(Vec3<int>(0, 0, 0), 0)); // Start with a single voxel.
	new_world_object->compressVoxels();
	new_world_object->setAABBOS(new_world_object->getDecompressedVoxelGroup().getAABB());

	// Send CreateObject message to server
	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		new_world_object->writeToNetworkStream(scratch_packet);

		enqueueMessageToSend(*this->client_thread, scratch_packet);
	}

	showInfoNotification("Voxel Object created.");

	// Deselect any currently selected object
	deselectObject();
}


bool MainWindow::areEditingVoxels() const
{
	return this->selected_ob.nonNull() && this->selected_ob->object_type == WorldObject::ObjectType_VoxelGroup;
}


void MainWindow::on_actionCopy_Object_triggered()
{
	if(this->selected_ob.nonNull())
	{
		QClipboard* clipboard = QGuiApplication::clipboard();
		QMimeData* mime_data = new QMimeData();

		BufferOutStream temp_buf;
		this->selected_ob->writeToStream(temp_buf);

		mime_data->setData(/*mime-type:*/"x-substrata-object-binary", QByteArray((const char*)temp_buf.buf.data(), (int)temp_buf.buf.size()));
		clipboard->setMimeData(mime_data);
	}
}


void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
	if(event->mimeData()->hasImage() || event->mimeData()->hasUrls())
		event->acceptProposedAction();
}


void MainWindow::dropEvent(QDropEvent* event)
{
	handlePasteOrDropMimeData(event->mimeData());
}


void MainWindow::createImageObject(const std::string& local_image_path)
{
	Reference<Map2D> im = ImageDecoding::decodeImage(base_dir_path, local_image_path);

	createImageObjectForWidthAndHeight(local_image_path, (int)im->getMapWidth(), (int)im->getMapHeight(),
		LODGeneration::textureHasAlphaChannel(local_image_path, im) // has alpha
	);
}


// A model path has been drag-and-dropped or pasted.
void MainWindow::createModelObject(const std::string& local_model_path)
{
	const Vec3d ob_pos = this->cam_controller.getFirstPersonPosition() + this->cam_controller.getForwardsVec() * 2.0f;

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
	ModelLoading::makeGLObjectForModelFile(*ui->glWidget->opengl_engine, *ui->glWidget->opengl_engine->vert_buf_allocator, local_model_path,
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


void MainWindow::createImageObjectForWidthAndHeight(const std::string& local_image_path, int w, int h, bool has_alpha)
{
	// NOTE: adapted from AddObjectDialog::makeMeshForWidthAndHeight()

	BatchedMeshRef batched_mesh;
	std::vector<WorldMaterialRef> world_materials;
	Vec3f scale;
	GLObjectRef gl_ob = ModelLoading::makeImageCube(*ui->glWidget->opengl_engine, *ui->glWidget->opengl_engine->vert_buf_allocator, local_image_path, w, h, batched_mesh, world_materials, scale);

	WorldObjectRef new_world_object = new WorldObject();
	new_world_object->materials = world_materials;
	new_world_object->scale = scale;


	const float ob_cam_right_translation = -scale.x/2;
	const float ob_cam_up_translation    = -scale.z/2;

	const Vec3d ob_pos = this->cam_controller.getFirstPersonPosition() + this->cam_controller.getForwardsVec() * 2.0f + 
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
	new_world_object->angle = Maths::roundToMultipleFloating((float)this->cam_controller.getAngles().x - Maths::pi_2<float>(), Maths::pi_4<float>()); // Round to nearest 45 degree angle, facing camera.

	new_world_object->setAABBOS(batched_mesh->aabb_os);

	new_world_object->model_url = "image_cube_5438347426447337425.bmesh";

	BitUtils::setOrZeroBit(new_world_object->materials[0]->flags, WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG, has_alpha); // Set COLOUR_TEX_HAS_ALPHA_FLAG flag

	// Copy all dependencies (textures etc..) to resources dir.  UploadResourceThread will read from here.
	WorldObject::GetDependencyOptions options;
	std::set<DependencyURL> paths;
	new_world_object->getDependencyURLSet(/*ob_lod_level=*/0, options, paths);
	for(auto it = paths.begin(); it != paths.end(); ++it)
	{
		const std::string path = it->URL;
		if(FileUtils::fileExists(path))
		{
			const uint64 hash = FileChecksum::fileChecksum(path);
			const std::string resource_URL = ResourceManager::URLForPathAndHash(path, hash);
			this->resource_manager->copyLocalFileToResourceDir(path, resource_URL);
		}
	}

	// Convert texture paths on the object to URLs
	new_world_object->convertLocalPathsToURLS(*this->resource_manager);

	// Send CreateObject message to server
	MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
	new_world_object->writeToNetworkStream(scratch_packet);
	enqueueMessageToSend(*this->client_thread, scratch_packet);

	showInfoNotification("Object created.");
}


static bool isObjectWithPosition(const Vec3d& pos, WorldState* world_state)
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


void MainWindow::handlePasteOrDropMimeData(const QMimeData* mime_data)
{
	try
	{
		// const QStringList formats = mime_data->formats();
		// for(auto it = formats.begin(); it != formats.end(); ++it)
		// 	conPrint("Format: " + it->toStdString());

		if(mime_data)
		{
			if(mime_data->hasUrls())
			{
				const QList<QUrl> urls = mime_data->urls();

				std::string image_path_to_load;
				std::string model_path_to_load;
				for(auto it = urls.begin(); it != urls.end(); ++it)
				{
					const std::string url = it->toString().toStdString();
					if(hasPrefix(url, "file:///"))
					{
						const std::string path = eatPrefix(url, "file:///");

						if(FileUtils::fileExists(path))
						{
							if(ImageDecoding::hasSupportedImageExtension(path))
								image_path_to_load = path;

							if(ModelLoading::hasSupportedModelExtension(path))
								model_path_to_load = path;
						}
					}
				}

				if(!image_path_to_load.empty())
					createImageObject(image_path_to_load);

				if(!model_path_to_load.empty())
					createModelObject(model_path_to_load);

				if(image_path_to_load.empty() && model_path_to_load.empty())
					throw glare::Exception("Pasted files / URLs did not contain a supported image or model format.");
			}
			else if(mime_data->hasFormat("x-substrata-object-binary")) // Binary encoded substrata object, from a user copying a substrata object.
			{
				const QByteArray ob_data = mime_data->data("x-substrata-object-binary");

				// Copy QByteArray to BufferInStream
				BufferInStream in_stream_buf;
				in_stream_buf.buf.resize(ob_data.size());
				if(ob_data.size() > 0)
					std::memcpy(in_stream_buf.buf.data(), ob_data.data(), ob_data.size());

				try
				{
					// Deserialise object
					WorldObjectRef pasted_ob = new WorldObject();
					readWorldObjectFromStream(in_stream_buf, *pasted_ob);

					// Choose a position for the pasted object.
					Vec3d new_ob_pos;
					if(pasted_ob->pos.getDist(this->cam_controller.getFirstPersonPosition()) > 50.0) // If the source object is far from the camera:
					{
						// Position pasted object in front of the camera.
						const float ob_w = pasted_ob->getAABBWSLongestLength();
						new_ob_pos = this->cam_controller.getFirstPersonPosition() + this->cam_controller.getForwardsVec() * myMax(2.f, ob_w * 2.0f);
					}
					else
					{
						// If the camera is near the source object, position pasted object besides the source object.
						// Translate along an axis depending on the camera viewpoint.
						Vec3d use_offset_vec;
						if(std::fabs(cam_controller.getRightVec().x) > std::fabs(cam_controller.getRightVec().y))
							use_offset_vec = (cam_controller.getRightVec().x > 0) ? Vec3d(1,0,0) : Vec3d(-1,0,0);
						else
							use_offset_vec = (cam_controller.getRightVec().y > 0) ? Vec3d(0,1,0) : Vec3d(0,-1,0);

						// We don't want to paste directly in the same place as another object (for example a previously pasted object), otherwise users can create duplicate objects by mistake and lose them.
						// So check if there is already an object there, and choose another position if so.
						new_ob_pos = pasted_ob->pos;
						for(int i=0; i<100; ++i)
						{
							const Vec3d tentative_pos = pasted_ob->pos + use_offset_vec * (i + 1) * 0.5f;
							if(!isObjectWithPosition(tentative_pos, world_state.ptr()))
							{
								new_ob_pos = tentative_pos;
								break;
							}
						}
					}

					pasted_ob->pos = new_ob_pos;
					pasted_ob->transformChanged();

					// Check permissions
					bool ob_pos_in_parcel;
					const bool have_creation_perms = haveParcelObjectCreatePermissions(new_ob_pos, ob_pos_in_parcel);
					if(!have_creation_perms)
					{
						if(ob_pos_in_parcel)
							showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
						else
							showErrorNotification("You can only create objects in a parcel that you have write permissions for.");
						return;
					}

					// Create object, by sending CreateObject message to server
					// Note that the recreated object will have a different ID than in the clipboard.
					{
						MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
						pasted_ob->writeToNetworkStream(scratch_packet);

						enqueueMessageToSend(*this->client_thread, scratch_packet);

						showInfoNotification("Object pasted.");
					}
				}
				catch(glare::Exception& e)
				{
					conPrint("Error while reading object from clipboard: " + e.what());
				}
			}
			else if(mime_data->hasImage()) // Image data (for example from snip screen)
			{
				QImage image = qvariant_cast<QImage>(mime_data->imageData());

				const std::string temp_path = PlatformUtils::getTempDirPath() + "/temp.jpg";
				const bool res = image.save(QtUtils::toQString(temp_path), "JPG", 95);
				if(!res)
					throw glare::Exception("Failed to save image to disk.");

				createImageObjectForWidthAndHeight(temp_path, image.width(), image.height(), /*has alpha=*/false);
			}
		}
	}
	catch(glare::Exception& e)
	{
		// Show error
		print(e.what());
		QErrorMessage m;
		m.showMessage(QtUtils::toQString(e.what()));
		m.exec();
	}
}


void MainWindow::on_actionPaste_Object_triggered()
{
	QClipboard* clipboard = QGuiApplication::clipboard();
	const QMimeData* mime_data = clipboard->mimeData();

	handlePasteOrDropMimeData(mime_data);
}


void MainWindow::on_actionCloneObject_triggered()
{
	if(this->selected_ob.nonNull())
	{
		// Position cloned object besides the source object.
		// Translate along an axis depending on the camera viewpoint.
		Vec3d use_offset_vec;
		if(std::fabs(cam_controller.getRightVec().x) > std::fabs(cam_controller.getRightVec().y))
			use_offset_vec = (cam_controller.getRightVec().x > 0) ? Vec3d(1,0,0) : Vec3d(-1,0,0);
		else
			use_offset_vec = (cam_controller.getRightVec().y > 0) ? Vec3d(0,1,0) : Vec3d(0,-1,0);

		const Vec3d new_ob_pos = this->selected_ob->pos + use_offset_vec;

		bool ob_pos_in_parcel;
		const bool have_creation_perms = haveParcelObjectCreatePermissions(new_ob_pos, ob_pos_in_parcel);
		if(!have_creation_perms)
		{
			if(ob_pos_in_parcel)
				showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
			else
				showErrorNotification("You can only create objects in a parcel that you have write permissions for.");
			return;
		}

		WorldObjectRef new_world_object = new WorldObject();
		new_world_object->uid = UID(0); // Will be set by server
		new_world_object->object_type = this->selected_ob->object_type;
		new_world_object->model_url = this->selected_ob->model_url;
		new_world_object->script = this->selected_ob->script;
		new_world_object->materials = this->selected_ob->materials; // TODO: clone?
		new_world_object->content = this->selected_ob->content;
		new_world_object->target_url = this->selected_ob->target_url;
		new_world_object->pos = new_ob_pos;
		new_world_object->axis = selected_ob->axis;
		new_world_object->angle = selected_ob->angle;
		new_world_object->scale = selected_ob->scale;
		new_world_object->flags = selected_ob->flags;// | WorldObject::LIGHTMAP_NEEDS_COMPUTING_FLAG; // Lightmaps need to be built for it.
		new_world_object->getDecompressedVoxels() = selected_ob->getDecompressedVoxels();
		new_world_object->getCompressedVoxels() = selected_ob->getCompressedVoxels();
		new_world_object->audio_source_url = selected_ob->audio_source_url;
		new_world_object->audio_volume = selected_ob->audio_volume;
		new_world_object->setAABBOS(this->selected_ob->getAABBOS());

		new_world_object->max_model_lod_level = selected_ob->max_model_lod_level;
		new_world_object->mass = selected_ob->mass;
		new_world_object->friction = selected_ob->friction;
		new_world_object->restitution = selected_ob->restitution;


		// Send CreateObject message to server
		{
			MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
			new_world_object->writeToNetworkStream(scratch_packet);

			enqueueMessageToSend(*this->client_thread, scratch_packet);
		}

		// Deselect any currently selected object
		deselectObject();

		showInfoNotification("Object cloned.");
	}
	else
	{
		QMessageBox msgBox;
		msgBox.setText("Please select an object before cloning.");
		msgBox.exec();
	}
}


void MainWindow::on_actionDeleteObject_triggered()
{
	if(this->selected_ob.nonNull())
	{
		deleteSelectedObject();
	}
}


void MainWindow::on_actionReset_Layout_triggered()
{
	ui->editorDockWidget->setFloating(false);
	this->addDockWidget(Qt::LeftDockWidgetArea, ui->editorDockWidget);
	ui->editorDockWidget->show();

	ui->chatDockWidget->setFloating(false);
	this->addDockWidget(Qt::RightDockWidgetArea, ui->chatDockWidget, Qt::Vertical);
	ui->chatDockWidget->show();

	ui->materialBrowserDockWidget->setFloating(false);
	this->addDockWidget(Qt::TopDockWidgetArea, ui->materialBrowserDockWidget, Qt::Horizontal);
	ui->materialBrowserDockWidget->show();

#if INDIGO_SUPPORT
	ui->indigoViewDockWidget->setFloating(false);
	this->addDockWidget(Qt::RightDockWidgetArea, ui->indigoViewDockWidget, Qt::Vertical);
	ui->indigoViewDockWidget->show();
#endif

	ui->helpInfoDockWidget->setFloating(true);
	ui->helpInfoDockWidget->show();
	// Position near bottom right corner of glWidget.
	ui->helpInfoDockWidget->setGeometry(QRect(ui->glWidget->mapToGlobal(ui->glWidget->geometry().bottomRight() + QPoint(-320, -120)), QSize(300, 100)));

	//this->addDockWidget(Qt::RightDockWidgetArea, ui->chatDockWidget, Qt::Vertical);
	//ui->chatDockWidget->show();

	ui->diagnosticsDockWidget->setFloating(false);
	this->addDockWidget(Qt::TopDockWidgetArea, ui->diagnosticsDockWidget, Qt::Horizontal);
	ui->diagnosticsDockWidget->show();


	// Enable tool bar
	ui->toolBar->setVisible(true);
}


void MainWindow::on_actionLogIn_triggered()
{
	if(connection_state != ServerConnectionState_Connected)
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Can't log in");
		msgBox.setText("You must be connected to a server to log in.");
		msgBox.exec();
		return;
	}

	LoginDialog dialog(settings, this->server_hostname);
	const int res = dialog.exec();
	if(res == QDialog::Accepted)
	{
		const std::string username = QtUtils::toStdString(dialog.usernameLineEdit->text());
		const std::string password = QtUtils::toStdString(dialog.passwordLineEdit->text());

		//conPrint("username: " + username);
		//conPrint("password: " + password);
		//this->last_login_username = username;

		// Make LogInMessage packet and enqueue to send
		MessageUtils::initPacket(scratch_packet, Protocol::LogInMessage);
		scratch_packet.writeStringLengthFirst(username);
		scratch_packet.writeStringLengthFirst(password);

		enqueueMessageToSend(*this->client_thread, scratch_packet);
	}
}


void MainWindow::on_actionLogOut_triggered()
{
	// Make message packet and enqueue to send
	MessageUtils::initPacket(scratch_packet, Protocol::LogOutMessage);
	enqueueMessageToSend(*this->client_thread, scratch_packet);

	settings->setValue("LoginDialog/auto_login", false); // Don't log in automatically next start.
}


void MainWindow::on_actionSignUp_triggered()
{
	if(connection_state != ServerConnectionState_Connected)
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Can't sign up");
		msgBox.setText("You must be connected to a server to sign up.");
		msgBox.exec();
		return;
	}

	SignUpDialog dialog(settings, this->server_hostname);
	const int res = dialog.exec();
	if(res == QDialog::Accepted)
	{
		const std::string username = QtUtils::toStdString(dialog.usernameLineEdit->text());
		const std::string email    = QtUtils::toStdString(dialog.emailLineEdit->text());
		const std::string password = QtUtils::toStdString(dialog.passwordLineEdit->text());

		conPrint("username: " + username);
		conPrint("email:    " + email);
		conPrint("password: " + password);
		//this->last_login_username = username;

		// Make message packet and enqueue to send
		MessageUtils::initPacket(scratch_packet, Protocol::SignUpMessage);
		scratch_packet.writeStringLengthFirst(username);
		scratch_packet.writeStringLengthFirst(email);
		scratch_packet.writeStringLengthFirst(password);

		enqueueMessageToSend(*this->client_thread, scratch_packet);
	}
}


void MainWindow::addParcelObjects()
{
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
				if(!screenshot_output_path.empty()) // If we are in screenshot-taking mode, don't highlight writable parcels.
					use_write_perms = false;

				parcel->opengl_engine_ob = parcel->makeOpenGLObject(ui->glWidget->opengl_engine, use_write_perms);
				parcel->opengl_engine_ob->materials[0].shader_prog = this->parcel_shader_prog;
				parcel->opengl_engine_ob->materials[0].auto_assign_shader = false;
				ui->glWidget->opengl_engine->addObject(parcel->opengl_engine_ob); // Add to engine

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


void MainWindow::removeParcelObjects()
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
				ui->glWidget->opengl_engine->removeObject(parcel->opengl_engine_ob);
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


void MainWindow::recolourParcelsForLoggedInState()
{
	Lock lock(this->world_state->mutex);
	for(auto& it : this->world_state->parcels)
	{
		Parcel* parcel = it.second.getPointer();
		if(parcel->opengl_engine_ob.nonNull())
		{
			const bool write_perms = parcel->userHasWritePerms(this->logged_in_user_id);

			bool use_write_perms = write_perms;
			if(!screenshot_output_path.empty()) // If we are in screenshot-taking mode, don't highlight writable parcels.
				use_write_perms = false;

			parcel->setColourForPerms(use_write_perms);
		}
	}
}


void MainWindow::on_actionShow_Parcels_triggered()
{
	if(ui->actionShow_Parcels->isChecked())
	{
		addParcelObjects();
	}
	else // Else if show parcels is now unchecked:
	{
		removeParcelObjects();
	}

	settings->setValue("mainwindow/showParcels", QVariant(ui->actionShow_Parcels->isChecked()));
}


void MainWindow::on_actionFly_Mode_triggered()
{
	this->player_physics.setFlyModeEnabled(ui->actionFly_Mode->isChecked());

	settings->setValue("mainwindow/flyMode", QVariant(ui->actionFly_Mode->isChecked()));
}


void MainWindow::on_actionThird_Person_Camera_triggered()
{
	this->cam_controller.setThirdPersonEnabled(ui->actionThird_Person_Camera->isChecked());

	settings->setValue("mainwindow/thirdPersonCamera", QVariant(ui->actionThird_Person_Camera->isChecked()));

	// Add or remove our avatar opengl model.
	if(this->cam_controller.thirdPersonEnabled()) // If we just enabled third person camera:
	{
		// Add our avatar model. Do this by marking it as dirty.
		Lock lock(this->world_state->mutex);
		for(auto it = this->world_state->avatars.begin(); it != this->world_state->avatars.end(); ++it)
		{
			Avatar* avatar = it->second.getPointer();
			const bool our_avatar = avatar->uid == this->client_avatar_uid;
			if(our_avatar)
				avatar->other_dirty = true;
		}
	}
	else
	{
		// Remove our avatar model
		Lock lock(this->world_state->mutex);
		for(auto it = this->world_state->avatars.begin(); it != this->world_state->avatars.end(); ++it)
		{
			Avatar* avatar = it->second.getPointer();
			const bool our_avatar = avatar->uid == this->client_avatar_uid;
			if(our_avatar)
			{
				avatar->graphics.destroy(*ui->glWidget->opengl_engine);

				// Remove nametag OpenGL object
				if(avatar->nametag_gl_ob.nonNull())
					ui->glWidget->opengl_engine->removeObject(avatar->nametag_gl_ob);
				avatar->nametag_gl_ob = NULL;
				if(avatar->speaker_gl_ob.nonNull())
					ui->glWidget->opengl_engine->removeObject(avatar->speaker_gl_ob);
				avatar->speaker_gl_ob = NULL;
			}
		}

		// Turn off selfie mode if it was enabled.
		gesture_ui.turnOffSelfieMode(); // calls setSelfieModeEnabled(false);
	}
}


void MainWindow::on_actionGoToMainWorld_triggered()
{
	connectToServer("sub://" + this->server_hostname);// this->server_hostname, "");
}


void MainWindow::on_actionGoToPersonalWorld_triggered()
{
	if(this->logged_in_user_name != "")
	{
		connectToServer("sub://" + this->server_hostname + "/" + this->logged_in_user_name); //  this->server_hostname, this->logged_in_user_name);
	}
	else
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Not logged in");
		msgBox.setText("You are not logged in, so we don't know your personal world name.  Please log in first.");
		msgBox.exec();
	}
}


void MainWindow::on_actionGo_to_CryptoVoxels_World_triggered()
{
	connectToServer("sub://" + this->server_hostname + "/cryptovoxels");//  this->server_hostname, "cryptovoxels");
}


void MainWindow::on_actionGo_to_Parcel_triggered()
{
	GoToParcelDialog d(this->settings);
	const int code = d.exec();
	if(code == QDialog::Accepted)
	{
		try
		{
			const int parcel_num = stringToInt(QtUtils::toStdString(d.parcelNumberLineEdit->text()));

			bool found = true;
			{
				Lock lock(world_state->mutex);

				auto res = world_state->parcels.find(ParcelID(parcel_num));
				if(res != world_state->parcels.end())
				{
					const Parcel* parcel = res->second.ptr();

					cam_controller.setPosition(parcel->getVisitPosition());
					player_physics.setPosition(parcel->getVisitPosition());
				}
				else
					found = false;
			}

			if(!found)
			{
				QMessageBox msgBox;
				msgBox.setWindowTitle("Invalid parcel number");
				msgBox.setText("There is no parcel with that number.");
				msgBox.exec();
			}
		}
		catch(glare::Exception&)
		{
			QMessageBox msgBox;
			msgBox.setWindowTitle("Invalid parcel number");
			msgBox.setText("Please enter just a number.");
			msgBox.exec();
		}
	}
}


void MainWindow::on_actionGo_to_Position_triggered()
{
	GoToPositionDialog d(this->settings, cam_controller.getFirstPersonPosition());
	const int code = d.exec();
	if(code == QDialog::Accepted)
	{
		const Vec3d pos(
			d.XDoubleSpinBox->value(),
			d.YDoubleSpinBox->value(),
			d.ZDoubleSpinBox->value()
		);
			
		cam_controller.setPosition(pos);
		player_physics.setPosition(pos);
	}
}


void MainWindow::on_actionSet_Start_Location_triggered()
{
	settings->setValue(MainOptionsDialog::startLocationURLKey(), QtUtils::toQString(this->url_widget->getURL()));
}


void MainWindow::on_actionGo_To_Start_Location_triggered()
{
	const std::string start_URL = QtUtils::toStdString(settings->value(MainOptionsDialog::startLocationURLKey()).toString());

	if(start_URL.empty())
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Invalid start location URL");
		msgBox.setText("You need to set a start location first with the 'Go > Set current location as start location' menu command.");
		msgBox.exec();
	}
	else
		visitSubURL(start_URL);
}


void MainWindow::on_actionFind_Object_triggered()
{
	FindObjectDialog d(this->settings);
	const int code = d.exec();
	if(code == QDialog::Accepted)
	{
		try
		{
			const int ob_id = stringToInt(QtUtils::toStdString(d.objectIDLineEdit->text()));

			bool found = true;
			{
				Lock lock(world_state->mutex);

				auto res = world_state->objects.find(UID(ob_id));
				if(res != world_state->objects.end())
				{
					WorldObject* ob = res.getValue().ptr();

					deselectObject();
					selectObject(ob, /*selected_mat_index=*/0);
				}
				else
					found = false;
			}

			if(!found)
			{
				QMessageBox msgBox;
				msgBox.setWindowTitle("Invalid object id");
				msgBox.setText("There is no object with that id.");
				msgBox.exec();
			}
		}
		catch(glare::Exception&)
		{
			QMessageBox msgBox;
			msgBox.setWindowTitle("Invalid object id");
			msgBox.setText("Please enter just a number.");
			msgBox.exec();
		}
	}
}


void MainWindow::on_actionList_Objects_Nearby_triggered()
{
	ListObjectsNearbyDialog d(this->settings, this->world_state.ptr(), cam_controller.getPosition());
	const int code = d.exec();
	if(code == QDialog::Accepted)
	{
		const UID ob_id = d.getSelectedUID();
		if(ob_id.valid())
		{
			bool found = true;
			{
				Lock lock(world_state->mutex);
				auto res = world_state->objects.find(UID(ob_id));
				if(res != world_state->objects.end())
				{
					WorldObject* ob = res.getValue().ptr();

					deselectObject();
					selectObject(ob, /*selected_mat_index=*/0);
				}
				else
					found = false;
			}

			if(!found)
			{
				QMessageBox msgBox;
				msgBox.setWindowTitle("Invalid object id");
				msgBox.setText("There is no object with that id.");
				msgBox.exec();
			}
		}
	}
}


void MainWindow::on_actionExport_view_to_Indigo_triggered()
{
	ui->indigoView->saveSceneToDisk();
}


void MainWindow::on_actionTake_Screenshot_triggered()
{
	this->gesture_ui.setVisible(false); // Hide gesture UI

	ui->glWidget->updateGL(); // Draw again now that the gesture UI is hidden.

#if QT_VERSION_MAJOR >= 6
	QImage framebuffer = ui->glWidget->grabFramebuffer();
#else
	QImage framebuffer = ui->glWidget->grabFrameBuffer();
#endif
	
	// NOTE: Qt-saved images were doing weird things with parcel border alpha.  Just copy to an ImageMapUInt8 and do the image saving ourselves.

	ImageMapUInt8Ref map = convertQImageToImageMapUInt8(framebuffer);


	const std::string path = this->appdata_path + "/screenshots/screenshot_" + toString((uint64)Clock::getSecsSince1970()) + ".png";
	try
	{
		FileUtils::createDirIfDoesNotExist(FileUtils::getDirectory(path));

		PNGDecoder::write(*map, path);
	
		showInfoNotification("Saved screenshot to " + path);
	}
	catch(glare::Exception& e)
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Error");
		msgBox.setText(QtUtils::toQString("Saving screenshot to '" + path + "' failed: " + e.what()));
		msgBox.exec();
	}

	this->gesture_ui.setVisible(true); // Restore showing gesture UI
}


void MainWindow::on_actionShow_Screenshot_Folder_triggered()
{
	try
	{
		const std::string path = this->appdata_path + "/screenshots/";

		PlatformUtils::openFileBrowserWindowAtLocation(path);
	}
	catch(glare::Exception& e)
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Error");
		msgBox.setText(QtUtils::toQString(e.what()));
		msgBox.exec();
	}
}


void MainWindow::on_actionAbout_Substrata_triggered()
{
	AboutDialog d(this, appdata_path);
	d.exec();
}


void MainWindow::on_actionOptions_triggered()
{
	const std::string prev_audio_input_dev_name = QtUtils::toStdString(settings->value(MainOptionsDialog::inputDeviceNameKey(), "Default").toString());

	MainOptionsDialog d(this->settings);
	const int code = d.exec();
	if(code == QDialog::Accepted)
	{
		const float dist = (float)settings->value(MainOptionsDialog::objectLoadDistanceKey(), /*default val=*/500.0).toDouble();
		this->proximity_loader.setLoadDistance(dist);
		this->load_distance = dist;
		this->load_distance2 = dist*dist;

		//ui->glWidget->opengl_engine->setMSAAEnabled(settings->value(MainOptionsDialog::MSAAKey(), /*default val=*/true).toBool());

		startMainTimer(); // Restart main timer, as the timer interval depends on max FPS, whiich may have changed.
	}

	mic_read_thread_manager.enqueueMessage(new InputVolumeScaleChangedMessage(
		settings->value(MainOptionsDialog::inputScaleFactorNameKey(), /*default val=*/100).toInt() * 0.01f // input_vol_scale_factor (note: stored in percent in settings)
	));

	// Restart mic read thread if audio input device changed.
	if(QtUtils::toStdString(settings->value(MainOptionsDialog::inputDeviceNameKey(), "Default").toString()) != prev_audio_input_dev_name)
	{
		mic_read_thread_manager.killThreadsBlocking();

		Reference<glare::MicReadThread> mic_read_thread = new glare::MicReadThread(&this->msg_queue, this->udp_socket, this->client_avatar_uid, server_hostname, server_UDP_port,
			MainOptionsDialog::getInputDeviceName(settings),
			MainOptionsDialog::getInputScaleFactor(settings), // input_vol_scale_factor
			&mic_read_status
		);
		mic_read_thread_manager.addThread(mic_read_thread);
	}
}


void MainWindow::applyUndoOrRedoObject(const WorldObjectRef& restored_ob)
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

				voxels_different = in_world_ob->getCompressedVoxels() != restored_ob->getCompressedVoxels();

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
						ui->glWidget->opengl_engine->updateObjectTransformData(*opengl_ob);

						const int ob_lod_level = in_world_ob->getLODLevel(cam_controller.getPosition());

						// Update materials in opengl engine.
						for(size_t i=0; i<in_world_ob->materials.size(); ++i)
							if(i < opengl_ob->materials.size())
								ModelLoading::setGLMaterialFromWorldMaterial(*in_world_ob->materials[i], ob_lod_level, in_world_ob->lightmap_url, *this->resource_manager, opengl_ob->materials[i]);

						ui->glWidget->opengl_engine->objectMaterialsUpdated(*opengl_ob);
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
					ui->indigoView->objectTransformChanged(*in_world_ob);

					// Update object values in editor
					ui->objectEditor->setFromObject(*in_world_ob, ui->objectEditor->getSelectedMatIndex(), /*ob in editing user's world=*/connectedToUsersPersonalWorldOrGodUser());

					updateSelectedObjectPlacementBeam(); // Has to go after physics world update due to ray-trace needed.

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


void MainWindow::on_actionUndo_triggered()
{
	try
	{
		WorldObjectRef ob = undo_buffer.getUndoWorldObject();
		applyUndoOrRedoObject(ob);
	}
	catch(glare::Exception& e)
	{
		conPrint("ERROR: Exception while trying to undo change: " + e.what());
	}
}


void MainWindow::on_actionRedo_triggered()
{
	try
	{
		WorldObjectRef ob = undo_buffer.getRedoWorldObject();
		applyUndoOrRedoObject(ob);
	}
	catch(glare::Exception& e)
	{
		conPrint("ERROR: Exception while trying to redo change: " + e.what());
	}
}


void MainWindow::on_actionShow_Log_triggered()
{
	this->log_window->show();
	this->log_window->raise();
}


void MainWindow::bakeLightmapsForAllObjectsInParcel(uint32 lightmap_flag)
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
		lightmap_flag_timer->start(/*msec=*/20); // Trigger sending update-lightmap update flag message later.

		showInfoNotification("Baking lightmaps for " + toString(num_lightmaps_to_bake) + " objects in current parcel...");
	}
	else
		showErrorNotification("You must be in a parcel to trigger lightmapping on it.");
}


void MainWindow::on_actionBake_Lightmaps_fast_for_all_objects_in_parcel_triggered()
{
	bakeLightmapsForAllObjectsInParcel(WorldObject::LIGHTMAP_NEEDS_COMPUTING_FLAG);
}


void MainWindow::on_actionBake_lightmaps_high_quality_for_all_objects_in_parcel_triggered()
{
	bakeLightmapsForAllObjectsInParcel(WorldObject::HIGH_QUAL_LIGHTMAP_NEEDS_COMPUTING_FLAG);
}


void MainWindow::enableMaterialisationEffectOnOb(WorldObject& ob)
{
	// conPrint("MainWindow::enableMaterialisationEffectOnOb()");

	const float materialise_effect_start_time = (float)Clock::getTimeSinceInit();

	if(ob.opengl_engine_ob.nonNull())
	{
		for(size_t i=0; i<ob.opengl_engine_ob->materials.size(); ++i)
		{
			ob.opengl_engine_ob->materials[i].materialise_effect = true;
			ob.opengl_engine_ob->materials[i].materialise_start_time = materialise_effect_start_time;
		}

		ui->glWidget->opengl_engine->objectMaterialsUpdated(*ob.opengl_engine_ob);
	}

	ob.use_materialise_effect_on_load = true;
	ob.materialise_effect_start_time = materialise_effect_start_time;
}


void MainWindow::enableMaterialisationEffectOnAvatar(Avatar& avatar)
{
	// conPrint("MainWindow::enableMaterialisationEffectOnAvatar()");

	const float materialise_effect_start_time = (float)Clock::getTimeSinceInit();

	if(avatar.graphics.skinned_gl_ob.nonNull())
	{
		for(size_t i=0; i<avatar.graphics.skinned_gl_ob->materials.size(); ++i)
		{
			avatar.graphics.skinned_gl_ob->materials[i].materialise_effect = true;
			avatar.graphics.skinned_gl_ob->materials[i].materialise_start_time = materialise_effect_start_time;
		}

		ui->glWidget->opengl_engine->objectMaterialsUpdated(*avatar.graphics.skinned_gl_ob);
	}

	avatar.use_materialise_effect_on_load = true;
	avatar.materialise_effect_start_time = materialise_effect_start_time;
}


void MainWindow::on_actionSummon_Bike_triggered()
{
	try
	{
		if(!this->logged_in_user_id.valid())
		{
			showErrorNotification("You must be logged in to summon a bike.");
			return;
		}

		//TEMP: Save out bike mats
		if(false)
		{
			ModelLoading::MakeGLObjectResults results;
			ModelLoading::makeGLObjectForModelFile(*ui->glWidget->opengl_engine, *ui->glWidget->opengl_engine->vert_buf_allocator, 
				"D:\\models\\BMWCONCEPTBIKE\\optimized-dressed_fix7_offset4.glb", // This is exported from Blender from source_resources/bike-optimized-dressed_fix7_offset.blend
				results);

			std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<materials>\n";
			for(size_t i=0; i<results.materials.size(); ++i)
			{
				results.materials[i]->convertLocalPathsToURLS(*this->resource_manager);
				xml += results.materials[i]->serialiseToXML(/*tab_depth=*/1);
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
		const Quatf to_face_camera_rot = Quatf::fromAxisAndAngle(Vec3f(0,0,1), (float)this->cam_controller.getAngles().x);
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
		IndigoXMLDoc doc(this->base_dir_path + "/resources/bike_mats.xml");

		pugi::xml_node root = doc.getRootElement();

		std::vector<WorldMaterialRef> materials;
		for(pugi::xml_node n = root.child("material"); n; n = n.next_sibling("material"))
		{
			materials.push_back(WorldMaterial::loadFromXMLElem("bike_mats.xml", /*convert_rel_paths_to_abs_disk_paths=*/false, n));
		}
		


		WorldObjectRef new_world_object = new WorldObject();

		new_world_object->model_url = "optimized_dressed_fix7_offset4_glb_4474648345850208925.bmesh";
		new_world_object->max_model_lod_level = 2;

		new_world_object->flags = WorldObject::COLLIDABLE_FLAG | WorldObject::DYNAMIC_FLAG | WorldObject::SUMMONED_FLAG;

		new_world_object->uid = UID(0); // A new UID will be assigned by server
		new_world_object->materials = materials;
		new_world_object->pos = pos;
		new_world_object->axis = Vec3f(axis);
		new_world_object->angle = angle;
		new_world_object->scale = Vec3f(0.18f);
		new_world_object->setAABBOS(aabb_os);

		new_world_object->script = FileUtils::readEntireFileTextMode(this->base_dir_path + "/resources/summoned_bike_script.xml");

		new_world_object->mass = 200;

		setMaterialFlagsForObject(new_world_object.ptr());

		// Send CreateObject message to server
		{
			MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
			new_world_object->writeToNetworkStream(scratch_packet);

			enqueueMessageToSend(*this->client_thread, scratch_packet);
		}
	}
	catch(glare::Exception& e)
	{
		showErrorNotification(e.what());

		QMessageBox msgBox;
		msgBox.setText(QtUtils::toQString(e.what()));
		msgBox.exec();
	}
}


void MainWindow::on_actionSummon_Hovercar_triggered()
{
	try
	{
		if(!this->logged_in_user_id.valid())
		{
			showErrorNotification("You must be logged in to summon a hovercar.");
			return;
		}

		//TEMP: Save out hovercar mats
		if(false)
		{
			ModelLoading::MakeGLObjectResults results;
			ModelLoading::makeGLObjectForModelFile(*ui->glWidget->opengl_engine, *ui->glWidget->opengl_engine->vert_buf_allocator, 
				"D:\\models\\peugot_closed.glb",
				results);

			std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<materials>\n";
			for(size_t i=0; i<results.materials.size(); ++i)
			{
				results.materials[i]->convertLocalPathsToURLS(*this->resource_manager);
				xml += results.materials[i]->serialiseToXML(/*tab_depth=*/1);
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

		const Quatf to_face_camera_rot = Quatf::fromAxisAndAngle(Vec3f(0,0,1), (float)this->cam_controller.getAngles().x);

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
		IndigoXMLDoc doc(this->base_dir_path + "/resources/hovercar_mats.xml");

		pugi::xml_node root = doc.getRootElement();

		std::vector<WorldMaterialRef> materials;
		for(pugi::xml_node n = root.child("material"); n; n = n.next_sibling("material"))
		{
			materials.push_back(WorldMaterial::loadFromXMLElem("hovercar_mats.xml", /*convert_rel_paths_to_abs_disk_paths=*/false, n));
		}



		WorldObjectRef new_world_object = new WorldObject();

		new_world_object->model_url = "peugot_closed_glb_2887717763908023194.bmesh";
		new_world_object->max_model_lod_level = 2;

		new_world_object->flags = WorldObject::COLLIDABLE_FLAG | WorldObject::DYNAMIC_FLAG | WorldObject::SUMMONED_FLAG;

		new_world_object->uid = UID(0); // A new UID will be assigned by server
		new_world_object->materials = materials;
		new_world_object->pos = pos;
		new_world_object->axis = Vec3f(axis);
		new_world_object->angle = angle;
		new_world_object->scale = Vec3f(1.f);
		new_world_object->setAABBOS(aabb_os);

		new_world_object->script = FileUtils::readEntireFileTextMode(this->base_dir_path + "/resources/summoned_hovercar_script.xml");

		new_world_object->mass = 1000;

		setMaterialFlagsForObject(new_world_object.ptr());

		// Send CreateObject message to server
		{
			MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
			new_world_object->writeToNetworkStream(scratch_packet);

			enqueueMessageToSend(*this->client_thread, scratch_packet);
		}
	}
	catch(glare::Exception& e)
	{
		showErrorNotification(e.what());

		QMessageBox msgBox;
		msgBox.setText(QtUtils::toQString(e.what()));
		msgBox.exec();
	}
}


void MainWindow::on_actionMute_Audio_toggled(bool checked)
{
	if(checked)
	{
		audio_engine.setMasterVolume(0.f);
	}
	else
	{	
		audio_engine.setMasterVolume(1.f);
	}
}


void MainWindow::diagnosticsWidgetChanged()
{
	ui->glWidget->opengl_engine->setDrawWireFrames(ui->diagnosticsWidget->showWireframesCheckBox->isChecked());
}


void MainWindow::diagnosticsReloadTerrain()
{
	if(this->terrain_system.nonNull())
	{
		terrain_system->shutdown();
		terrain_system = NULL;
	}

	// Just leave terrain_system null, will be reinitialised in MainWindow::updateGroundPlane().
}


void MainWindow::sendChatMessageSlot()
{
	//conPrint("MainWindow::sendChatMessageSlot()");

	const std::string message = QtUtils::toIndString(ui->chatMessageLineEdit->text());

	// Make message packet and enqueue to send
	MessageUtils::initPacket(scratch_packet, Protocol::ChatMessageID);
	scratch_packet.writeStringLengthFirst(message);

	enqueueMessageToSend(*this->client_thread, scratch_packet);

	ui->chatMessageLineEdit->clear();
}


// Object transform has been edited, e.g. by the object editor.
void MainWindow::objectTransformEditedSlot()
{
	try
	{
		if(this->selected_ob.nonNull())
		{
			// Multiple edits using the object editor, in a short timespan, will be merged together,
			// unless force_new_undo_edit is true (is set when undo or redo is issued).
			const bool start_new_edit = force_new_undo_edit || (time_since_object_edited.elapsed() > 5.0);

			if(start_new_edit)
				undo_buffer.startWorldObjectEdit(*this->selected_ob);

			ui->objectEditor->writeTransformMembersToObject(*this->selected_ob);

			this->selected_ob->last_modified_time = TimeStamp::currentTime(); // Gets set on server as well, this is just for updating the local display.
			ui->objectEditor->objectLastModifiedUpdated(*this->selected_ob);

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
				ui->glWidget->makeCurrent();

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
						GLLightRef light = this->selected_ob->opengl_light;
						if(light.nonNull())
						{
							light->gpu_data.dir = normalise(new_ob_to_world_matrix * Vec4f(0, 0, -1, 0));
							ui->glWidget->opengl_engine->setLightPos(light, new_ob_pos.toVec4fPoint());
						}
					}

					// Update transform of OpenGL object
					opengl_ob->ob_to_world_matrix = new_ob_to_world_matrix;
					ui->glWidget->opengl_engine->updateObjectTransformData(*opengl_ob);

					// Update physics object transform
					physics_world->setNewObToWorldTransform(*selected_ob->physics_object, selected_ob->pos.toVec4fVector(), Quatf::fromAxisAndAngle(normalise(selected_ob->axis.toVec4fVector()), selected_ob->angle),
						useScaleForWorldOb(selected_ob->scale).toVec4fVector());

					updateSelectedObjectPlacementBeam(); // Has to go after physics world update due to ray-trace needed.

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
	catch(glare::Exception& e)
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Error");
		msgBox.setText(QtUtils::toQString(e.what()));
		msgBox.exec();
	}
}


// Object property (that is not a transform property) has been edited, e.g. by the object editor.
void MainWindow::objectEditedSlot()
{
	try
	{
		// Update object material(s) with values from editor.
		if(this->selected_ob.nonNull())
		{
			// Multiple edits using the object editor, in a short timespan, will be merged together,
			// unless force_new_undo_edit is true (is set when undo or redo is issued).
			const bool start_new_edit = force_new_undo_edit || (time_since_object_edited.elapsed() > 5.0);

			if(start_new_edit)
				undo_buffer.startWorldObjectEdit(*this->selected_ob);

			ui->objectEditor->toObject(*this->selected_ob); // Sets changed_flags on object as well.
			
			this->selected_ob->last_modified_time = TimeStamp::currentTime(); // Gets set on server as well, this is just for updating the local display.
			ui->objectEditor->objectLastModifiedUpdated(*this->selected_ob);

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
					Indigo::MeshRef indigo_mesh; // not used
					const int subsample_factor = 1;
					Reference<OpenGLMeshRenderData> gl_meshdata = ModelLoading::makeModelForVoxelGroup(selected_ob->getDecompressedVoxelGroup(), subsample_factor, ob_to_world,
						ui->glWidget->opengl_engine->vert_buf_allocator.ptr(), /*do_opengl_stuff=*/true, /*need_lightmap_uvs=*/false, mat_transparent, /*build_dynamic_physics_ob=*/selected_ob->isDynamic(),
						physics_shape, indigo_mesh);

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

		
			if(BitUtils::isBitSet(this->selected_ob->changed_flags, WorldObject::MODEL_URL_CHANGED) || 
				(BitUtils::isBitSet(this->selected_ob->changed_flags, WorldObject::DYNAMIC_CHANGED) || BitUtils::isBitSet(this->selected_ob->changed_flags, WorldObject::PHYSICS_VALUE_CHANGED)))
			{
				if(vehicle_controllers.find(selected_ob.ptr()) != vehicle_controllers.end()) // If there is a vehicle controller for selected_ob:
				{
					showErrorNotification("Can't edit vehicle physics properties while user is in vehicle");
				}
				else
				{
					removeAndDeleteGLAndPhysicsObjectsForOb(*this->selected_ob); // Remove old opengl and physics objects

					const std::string mesh_path = FileUtils::fileExists(this->selected_ob->model_url) ? this->selected_ob->model_url : resource_manager->pathForURL(this->selected_ob->model_url);

					ModelLoading::MakeGLObjectResults results;
					ModelLoading::makeGLObjectForModelFile(*ui->glWidget->opengl_engine, *ui->glWidget->opengl_engine->vert_buf_allocator, mesh_path,
						results
					);
			
					this->selected_ob->opengl_engine_ob = results.gl_ob;
					this->selected_ob->opengl_engine_ob->ob_to_world_matrix = obToWorldMatrix(*this->selected_ob);

					ui->glWidget->opengl_engine->addObject(this->selected_ob->opengl_engine_ob);

					ui->glWidget->opengl_engine->selectObject(this->selected_ob->opengl_engine_ob);

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
						const std::string mesh_URL = ResourceManager::URLForNameAndExtensionAndHash(original_filename, ::getExtension(bmesh_disk_path), model_hash); // Make a URL like "projectdog_png_5624080605163579508.png"

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

					PhysicsShape use_shape = PhysicsWorld::createJoltShapeForBatchedMesh(*results.batched_mesh, selected_ob->isDynamic());
					if(selected_ob->centre_of_mass_offset_os != Vec3f(0.f))
						use_shape = PhysicsWorld::createCOMOffsetShapeForShape(use_shape, selected_ob->centre_of_mass_offset_os.toVec4fVector());

					selected_ob->physics_object->shape = use_shape;
					selected_ob->physics_object->userdata = selected_ob.ptr();
					selected_ob->physics_object->userdata_type = 0;
					selected_ob->physics_object->ob_uid = selected_ob->uid;
					selected_ob->physics_object->pos = selected_ob->pos.toVec4fPoint();
					selected_ob->physics_object->rot = Quatf::fromAxisAndAngle(normalise(selected_ob->axis), selected_ob->angle);
					selected_ob->physics_object->scale = useScaleForWorldOb(selected_ob->scale);
			
					selected_ob->physics_object->kinematic = !selected_ob->script.empty();
					selected_ob->physics_object->dynamic = selected_ob->isDynamic();
					selected_ob->physics_object->is_sphere = FileUtils::getFilename(selected_ob->model_url) == "Icosahedron_obj_136334556484365507.bmesh";
					selected_ob->physics_object->is_cube = FileUtils::getFilename(selected_ob->model_url) == "Cube_obj_11907297875084081315.bmesh";

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
			std::vector<DependencyURL> URLs;
			this->selected_ob->appendDependencyURLsBaseLevel(options, URLs);

			for(size_t i=0; i<URLs.size(); ++i)
			{
				if(FileUtils::fileExists(URLs[i].URL)) // If this was a local path:
				{
					const std::string local_path = URLs[i].URL;
					const std::string URL = ResourceManager::URLForPathAndHash(local_path, FileChecksum::fileChecksum(local_path));

					// Copy model to local resources dir.
					resource_manager->copyLocalFileToResourceDir(local_path, URL);
				}
			}
		


			this->selected_ob->convertLocalPathsToURLS(*this->resource_manager);

			if(!task_manager)
				task_manager = new glare::TaskManager("mainwindow general task manager", myClamp<size_t>(PlatformUtils::getNumLogicalProcessors() / 2, 1, 8)), // Currently just used for LODGeneration::generateLODTexturesForMaterialsIfNotPresent().

			// Generate LOD textures for materials, if not already present on disk.
			// Note that server will also generate LOD textures, however the client may want to display a particular LOD texture immediately, so generate on the client as well.
			LODGeneration::generateLODTexturesForMaterialsIfNotPresent(selected_ob->materials, *resource_manager, *task_manager);

			const int ob_lod_level = this->selected_ob->getLODLevel(cam_controller.getPosition());
			const float max_dist_for_ob_lod_level = selected_ob->getMaxDistForLODLevel(ob_lod_level);

			startLoadingTexturesForObject(*this->selected_ob, ob_lod_level, max_dist_for_ob_lod_level);

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
										ModelLoading::setGLMaterialFromWorldMaterial(*this->selected_ob->materials[i], ob_lod_level, this->selected_ob->lightmap_url, *this->resource_manager,
											opengl_ob->materials[i]
										);

									assignedLoadedOpenGLTexturesToMats(selected_ob.ptr(), *ui->glWidget->opengl_engine, *resource_manager);
								}
							}

							ui->glWidget->opengl_engine->objectMaterialsUpdated(*opengl_ob);
						}
						else if(this->selected_ob->object_type == WorldObject::ObjectType_Hypercard)
						{
							if(selected_ob->content != selected_ob->loaded_content)
							{
								// Re-create opengl-ob
								ui->glWidget->makeCurrent();

								opengl_ob->materials.resize(1);
								opengl_ob->materials[0].tex_matrix = Matrix2f(1, 0, 0, -1); // OpenGL expects texture data to have bottom left pixel at offset 0, we have top left pixel, so flip

								const std::string tex_key = "hypercard_" + selected_ob->content;

								// If the hypercard texture is already loaded, use it
								opengl_ob->materials[0].albedo_texture = ui->glWidget->opengl_engine->getTextureIfLoaded(OpenGLTextureKey(tex_key));
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
										task->opengl_engine = ui->glWidget->opengl_engine;
										load_item_queue.enqueueItem(*this->selected_ob, task, /*max task dist=*/200.f);
									}
								}

								opengl_ob->ob_to_world_matrix = new_ob_to_world_matrix;
								ui->glWidget->opengl_engine->objectMaterialsUpdated(*opengl_ob);

								selected_ob->opengl_engine_ob = opengl_ob;

								selected_ob->loaded_content = selected_ob->content;
							}
						}
						else if(this->selected_ob->object_type == WorldObject::ObjectType_Spotlight)
						{
							GLLightRef light = this->selected_ob->opengl_light;
							if(light.nonNull())
							{
								light->gpu_data.dir = normalise(new_ob_to_world_matrix * Vec4f(0, 0, -1, 0));
								float scale;
								light->gpu_data.col = computeSpotlightColour(*this->selected_ob, light->gpu_data.cone_cos_angle_start, light->gpu_data.cone_cos_angle_end, scale);

								ui->glWidget->makeCurrent();
								ui->glWidget->opengl_engine->setLightPos(light, new_ob_pos.toVec4fPoint());


								// Use material[1] from the WorldObject as the light housing GL material.
								opengl_ob->materials.resize(2);
								if(this->selected_ob->materials.size() >= 2)
									ModelLoading::setGLMaterialFromWorldMaterial(*this->selected_ob->materials[1], /*lod level=*/ob_lod_level, /*lightmap URL=*/"", *resource_manager, /*open gl mat=*/opengl_ob->materials[0]);
								else
									opengl_ob->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(0.85f));

								// Apply a light emitting material to the light surface material in the spotlight model.
								if(this->selected_ob->materials.size() >= 1)
								{
									opengl_ob->materials[1].emission_linear_rgb = toLinearSRGB(this->selected_ob->materials[0]->colour_rgb);
									opengl_ob->materials[1].emission_scale = scale;
								}

								ui->glWidget->opengl_engine->objectMaterialsUpdated(*opengl_ob);
							}
						}

						// Update transform of OpenGL object
						opengl_ob->ob_to_world_matrix = new_ob_to_world_matrix;
						ui->glWidget->opengl_engine->updateObjectTransformData(*opengl_ob);

						// Update physics object transform
						selected_ob->physics_object->collidable = selected_ob->isCollidable();
						physics_world->setNewObToWorldTransform(*selected_ob->physics_object, selected_ob->pos.toVec4fVector(), Quatf::fromAxisAndAngle(normalise(selected_ob->axis.toVec4fVector()), selected_ob->angle),
							useScaleForWorldOb(selected_ob->scale).toVec4fVector());

						// Update in Indigo view
						ui->indigoView->objectTransformChanged(*selected_ob);

						updateSelectedObjectPlacementBeam(); // Has to go after physics world update due to ray-trace needed.

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
				loadAudioForObject(this->selected_ob.getPointer());
			}

			if(BitUtils::isBitSet(selected_ob->changed_flags, WorldObject::SCRIPT_CHANGED))
			{
				try
				{
					// Try loading script first
					loadScriptForObject(this->selected_ob.ptr());

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
				}
			}

			if(selected_ob->browser_vid_player.nonNull())
				selected_ob->browser_vid_player->videoURLMayHaveChanged(this, ui->glWidget->opengl_engine.ptr(), selected_ob.ptr());

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
	catch(glare::Exception& e)
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Error");
		msgBox.setText(QtUtils::toQString(e.what()));
		msgBox.exec();
	}
}


Reference<VehiclePhysics> MainWindow::createVehicleControllerForScript(WorldObject* ob)
{
	if(ob->vehicle_script.isType<Scripting::HoverCarScript>())
	{
		const Scripting::HoverCarScript* hover_car_script = ob->vehicle_script.downcastToPtr<Scripting::HoverCarScript>();

		HoverCarPhysicsSettings hover_car_physics_settings;
		hover_car_physics_settings.hovercar_mass = ob->mass;
		hover_car_physics_settings.script_settings = hover_car_script->settings.downcast<Scripting::HoverCarScriptSettings>();

		return new HoverCarPhysics(ob, ob->physics_object->jolt_body_id, hover_car_physics_settings, particle_manager.ptr());
	}
	else if(ob->vehicle_script.isType<Scripting::BoatScript>())
	{
		const Scripting::BoatScript* boat_script = ob->vehicle_script.downcastToPtr<Scripting::BoatScript>();

		BoatPhysicsSettings physics_settings;
		physics_settings.boat_mass = ob->mass;
		physics_settings.script_settings = boat_script->settings.downcast<Scripting::BoatScriptSettings>();

		return new BoatPhysics(ob, ob->physics_object->jolt_body_id, physics_settings, particle_manager.ptr());
	}
	else if(ob->vehicle_script.isType<Scripting::BikeScript>())
	{
		const Scripting::BikeScript* bike_script = ob->vehicle_script.downcastToPtr<Scripting::BikeScript>();

		BikePhysicsSettings bike_physics_settings;
		bike_physics_settings.bike_mass = ob->mass;
		bike_physics_settings.script_settings = bike_script->settings.downcast<Scripting::BikeScriptSettings>();

		return new BikePhysics(ob, bike_physics_settings, *physics_world, &audio_engine, base_dir_path, particle_manager.ptr());
	}
	else
		throw glare::Exception("invalid vehicle_script type");
}


// Parcel has been edited, e.g. by the parcel editor.
void MainWindow::parcelEditedSlot()
{
	if(this->selected_parcel.nonNull())
	{
		ui->parcelEditor->toParcel(*this->selected_parcel);

		Lock lock(this->world_state->mutex);
		//this->selected_parcel->from_local_other_dirty = true;
		this->world_state->dirty_from_local_parcels.insert(this->selected_parcel);
	}
}


void MainWindow::worldSettingsAppliedSlot()
{
	// NOTE: for now just wait for WorldSettingsUpdate to come from server, which will reload the terrain.
}


// An environment setting has been edited in the environment options dock widget
void MainWindow::environmentSettingChangedSlot()
{
	if(ui->glWidget->opengl_engine.nonNull())
	{
		const float theta = myClamp(::degreeToRad((float)ui->environmentOptionsWidget->sunThetaRealControl->value()), 0.01f, Maths::pi<float>() - 0.01f);
		const float phi   = ::degreeToRad((float)ui->environmentOptionsWidget->sunPhiRealControl->value());
		const Vec4f sundir = GeometrySampling::dirForSphericalCoords(phi, theta);

		ui->glWidget->opengl_engine->setSunDir(sundir);
	}
}


void MainWindow::bakeObjectLightmapSlot()
{
	if(this->selected_ob.nonNull())
	{
		// Don't bake lightmaps for objects with sketal animation for now (creating second UV set removes joints and weights).
		const bool has_skeletal_anim = this->selected_ob->opengl_engine_ob.nonNull() && this->selected_ob->opengl_engine_ob->mesh_data.nonNull() &&
			!this->selected_ob->opengl_engine_ob->mesh_data->animation_data.animations.empty();

		if(has_skeletal_anim)
		{
			showErrorNotification("You cannot currently bake lightmaps for objects with skeletal animation.");
		}
		else
		{
			this->selected_ob->lightmap_baking = true;

			BitUtils::setBit(this->selected_ob->flags, WorldObject::LIGHTMAP_NEEDS_COMPUTING_FLAG);
			objs_with_lightmap_rebuild_needed.insert(this->selected_ob);
			lightmap_flag_timer->start(/*msec=*/20); // Trigger sending update-lightmap update flag message later.
		}
	}
}


void MainWindow::bakeObjectLightmapHighQualSlot()
{
	if(this->selected_ob.nonNull())
	{
		// Don't bake lightmaps for objects with sketal animation for now (creating second UV set removes joints and weights).
		const bool has_skeletal_anim = this->selected_ob->opengl_engine_ob.nonNull() && this->selected_ob->opengl_engine_ob->mesh_data.nonNull() &&
			!this->selected_ob->opengl_engine_ob->mesh_data->animation_data.animations.empty();

		if(has_skeletal_anim)
		{
			showErrorNotification("You cannot currently bake lightmaps for objects with skeletal animation.");
		}
		else
		{
			this->selected_ob->lightmap_baking = true;

			BitUtils::setBit(this->selected_ob->flags, WorldObject::HIGH_QUAL_LIGHTMAP_NEEDS_COMPUTING_FLAG);
			objs_with_lightmap_rebuild_needed.insert(this->selected_ob);
			lightmap_flag_timer->start(/*msec=*/20); // Trigger sending update-lightmap update flag message later.
		}
	}
}


void MainWindow::removeLightmapSignalSlot()
{
	if(this->selected_ob.nonNull())
	{
		this->selected_ob->lightmap_url.clear();

		objectEditedSlot();
	}
}


void MainWindow::posAndRot3DControlsToggledSlot()
{
	if(ui->objectEditor->posAndRot3DControlsEnabled())
	{
		if(selected_ob.nonNull())
		{
			const bool have_edit_permissions = objectModificationAllowed(*this->selected_ob);

			// Add an object placement beam
			if(have_edit_permissions)
			{
				for(int i = 0; i < NUM_AXIS_ARROWS; ++i)
					ui->glWidget->opengl_engine->addObject(axis_arrow_objects[i]);

				for(int i = 0; i < 3; ++i)
					ui->glWidget->opengl_engine->addObject(rot_handle_arc_objects[i]);

				axis_and_rot_obs_enabled = true;

				updateSelectedObjectPlacementBeam();
			}
		}
	}
	else
	{
		for(int i = 0; i < NUM_AXIS_ARROWS; ++i)
			ui->glWidget->opengl_engine->removeObject(this->axis_arrow_objects[i]);

		for(int i = 0; i < 3; ++i)
			ui->glWidget->opengl_engine->removeObject(this->rot_handle_arc_objects[i]);

		axis_and_rot_obs_enabled = false;
	}
	
	settings->setValue("objectEditor/show3DControlsCheckBoxChecked", ui->objectEditor->posAndRot3DControlsEnabled());
}


void MainWindow::materialSelectedInBrowser(const std::string& path)
{
	if(selected_ob.nonNull())
	{
		const bool have_edit_permissions = objectModificationAllowedWithMsg(*this->selected_ob, "edit");
		if(have_edit_permissions)
			this->ui->objectEditor->materialSelectedInBrowser(path);
		else
			showErrorNotification("You do not have write permissions for this object, so you can't apply a material to it.");
	}
}


void MainWindow::sendLightmapNeededFlagsSlot()
{
	conPrint("MainWindow::sendLightmapNeededFlagsSlot");

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


void MainWindow::URLChangedSlot()
{
	const std::string URL = this->url_widget->getURL();
	visitSubURL(URL);
}


void MainWindow::visitSubURL(const std::string& URL) // Visit a substrata 'sub://' URL.  Checks hostname and only reconnects if the hostname is different from the current one.
{
	try
	{
		URLParseResults parse_res = URLParser::parseURL(URL);

		const std::string hostname = parse_res.hostname;
		const std::string worldname = parse_res.userpath;

		if(parse_res.parsed_parcel_uid)
			this->url_parcel_uid = parse_res.parcel_uid;
		else
			this->url_parcel_uid = -1;

		if(hostname != this->server_hostname || worldname != this->server_worldname)
		{
			// Connect to a different server!
			connectToServer(URL/*hostname, worldname*/);
		}

		// If we had a URL with a parcel UID, like sub://substrata.info/parcel/10, then look up the parcel to get its position, then go there.
		// Note that this could fail if the parcels are not loaded yet.
		if(parse_res.parsed_parcel_uid)
		{
			Lock lock(this->world_state->mutex);
			const auto res = this->world_state->parcels.find(ParcelID(parse_res.parcel_uid));
			if(res != this->world_state->parcels.end())
			{
				this->cam_controller.setPosition(res->second->getVisitPosition());
				this->player_physics.setPosition(res->second->getVisitPosition());
				showInfoNotification("Jumped to parcel " + toString(parse_res.parcel_uid));
			}
			else
				throw glare::Exception("Could not find parcel with id " + toString(parse_res.parcel_uid));
		}
		else
		{
			this->cam_controller.setPosition(Vec3d(parse_res.x, parse_res.y, parse_res.z));
			this->player_physics.setPosition(Vec3d(parse_res.x, parse_res.y, parse_res.z));
		}
	}
	catch(glare::Exception& e) // Handle URL parse failure
	{
		conPrint(e.what());
		QMessageBox msgBox;
		msgBox.setText(QtUtils::toQString(e.what()));
		msgBox.exec();
	}
}


void MainWindow::disconnectFromServerAndClearAllObjects() // Remove any WorldObjectRefs held by MainWindow.
{
	udp_socket = NULL;

	load_item_queue.clear();
	model_and_texture_loader_task_manager.cancelAndWaitForTasksToComplete(); 
	model_loaded_messages_to_process.clear();
	texture_loaded_messages_to_process.clear();

	// Kill any existing threads connected to the server
	net_resource_download_thread_manager.killThreadsBlocking();
	resource_upload_thread_manager.killThreadsBlocking();
	client_udp_handler_thread_manager.killThreadsBlocking();
	mic_read_thread_manager.killThreadsBlocking();

	this->client_thread_manager.killThreadsNonBlocking(); // Suggests to client_thread to quit, by calling ClientThread::kill(), which sets should_die = 1.
	resource_download_thread_manager.killThreadsNonBlocking(); // Suggests to DownloadResourcesThreads to quit, by calling DownloadResourcesThread::kill(), which sets should_die = 1.

	// Wait for some period of time to see if client_thread and resource download threads quit.  If not, hard-kill them by calling killConnection().
	Timer timer;
	while((this->client_thread_manager.getNumThreads() > 0) || (resource_download_thread_manager.getNumThreads() > 0)) // While client_thread or a resource download thread is still running:
	{
		if(timer.elapsed() > 1.0)
		{
			logAndConPrintMessage("Reached time limit waiting for client_thread or resource download threads to close.  Hard-killing connection(s)");

			if(this->client_thread_manager.getNumThreads() > 0)
			{
				if(client_thread.nonNull())
					this->client_thread->killConnection(); // Calls ungracefulShutdown on socket, which should interrupt and blocking socket calls.

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

			break;
		}

		PlatformUtils::Sleep(10);
	}
	this->client_thread = NULL; // Need to make sure client_thread is destroyed, since it hangs on to a bunch of references.
	resource_download_thread_manager.killThreadsBlocking();

	this->client_avatar_uid = UID::invalidUID();
	this->server_protocol_version = 0;


	this->logged_in_user_id = UserID::invalidUserID();
	this->logged_in_user_name = "";
	this->logged_in_user_flags = 0;

	user_details->setTextAsNotLoggedIn();

	ui->worldSettingsWidget->updateControlsEditable();

	ui->onlineUsersTextEdit->clear();
	ui->chatMessagesTextEdit->clear();

	gesture_ui.untoggleMicButton(); // Since mic_read_thread_manager has thread killed above.

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

			if(ob->opengl_engine_ob.nonNull())
				ui->glWidget->opengl_engine->removeObject(ob->opengl_engine_ob);

			if(ob->opengl_light.nonNull())
				ui->glWidget->opengl_engine->removeLight(ob->opengl_light);

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
				ui->glWidget->opengl_engine->removeObject(parcel->opengl_engine_ob);
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
				ui->glWidget->opengl_engine->removeObject(avatar->nametag_gl_ob);
			avatar->nametag_gl_ob = NULL;
			if(avatar->speaker_gl_ob.nonNull())
				ui->glWidget->opengl_engine->removeObject(avatar->speaker_gl_ob);
			avatar->speaker_gl_ob = NULL;

			avatar->graphics.destroy(*ui->glWidget->opengl_engine);

			hud_ui.removeMarkerForAvatar(avatar); // Remove any marker for the avatar from the HUD
		}
	}

	if(biome_manager && ui->glWidget->opengl_engine.nonNull() && physics_world.nonNull())
		biome_manager->clear(*ui->glWidget->opengl_engine, *physics_world);

	selected_ob = NULL;
	selected_parcel = NULL;


	active_objects.clear();
	obs_with_animated_tex.clear();
	web_view_obs.clear();
	browser_vid_player_obs.clear();
	audio_obs.clear();
	obs_with_scripts.clear();
	obs_with_diagnostic_vis.clear();

	path_controllers.clear();

	objs_with_lightmap_rebuild_needed.clear();

	proximity_loader.clearAllObjects();

	cur_loading_voxel_ob = NULL;
	cur_loading_mesh_data = NULL;


	if(terrain_decal_manager.nonNull())
	{
		terrain_decal_manager->clear();
		terrain_decal_manager = NULL;
	}

	if(particle_manager.nonNull())
	{
		particle_manager->clear();
		particle_manager = NULL;;
	}

	if(terrain_system.nonNull())
	{
		terrain_system->shutdown();
		terrain_system = NULL;
	}

	this->ui->indigoView->shutdown();

	// Clear textures_processing set etc.
	textures_processing.clear();
	models_processing.clear();
	audio_processing.clear();
	script_content_processing.clear();
	scatter_info_processing.clear();

	texture_server->clear();

	world_state = NULL;

	if(physics_world.nonNull())
	{
		assert(physics_world->getNumObjects() == 0);
		physics_world->clear();
	}
}


void MainWindow::connectToServer(const std::string& URL)
{
	// By default, randomly vary the position a bit so players don't spawn inside other players.
	const double spawn_r = 4.0;
	Vec3d spawn_pos = Vec3d(-spawn_r + 2 * spawn_r * rng.unitRandom(), -spawn_r + 2 * spawn_r * rng.unitRandom(), 2);

	try
	{
		URLParseResults parse_res = URLParser::parseURL(URL);

		this->server_hostname = parse_res.hostname;
		this->server_worldname = parse_res.userpath;

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
	}
	catch(glare::Exception& e) // Handle URL parse failure
	{
		conPrint(e.what());
		QMessageBox msgBox;
		msgBox.setText(QtUtils::toQString(e.what()));
		msgBox.exec();
		return;
	}

	//-------------------------------- Do disconnect process --------------------------------
	disconnectFromServerAndClearAllObjects();
	//-------------------------------- End disconnect process --------------------------------


	//-------------------------------- Do connect process --------------------------------

	// Move player position back to near origin.
	this->cam_controller.setPosition(spawn_pos);
	this->cam_controller.resetRotation();

	world_state = new WorldState();
	world_state->url_whitelist->loadDefaultWhitelist();

	const std::string avatar_path = QtUtils::toStdString(settings->value("avatarPath").toString());

	uint64 avatar_model_hash = 0;
	if(FileUtils::fileExists(avatar_path))
		avatar_model_hash = FileChecksum::fileChecksum(avatar_path);
	const std::string avatar_URL = resource_manager->URLForPathAndHash(avatar_path, avatar_model_hash);

	client_thread = new ClientThread(&msg_queue, server_hostname, server_port, avatar_URL, server_worldname, this->client_tls_config, this->world_ob_pool_allocator);
	client_thread->world_state = world_state;
	client_thread_manager.addThread(client_thread);

	for(int z=0; z<4; ++z)
		resource_download_thread_manager.addThread(new DownloadResourcesThread(&msg_queue, resource_manager, server_hostname, server_port, &this->num_non_net_resources_downloading, this->client_tls_config,
			&this->download_queue));

	for(int i=0; i<4; ++i)
		net_resource_download_thread_manager.addThread(new NetDownloadResourcesThread(&msg_queue, resource_manager, &num_net_resources_downloading));

	if(physics_world.isNull())
	{
		physics_world = new PhysicsWorld();
		physics_world->event_listener = this;
		player_physics.init(*physics_world, spawn_pos);

		//car_physics.init(*physics_world);
	}
	else
	{
		this->player_physics.setPosition(spawn_pos);
	}

	assert(terrain_decal_manager.isNull());
	terrain_decal_manager = new TerrainDecalManager(this->base_dir_path, opengl_engine.ptr());

	assert(particle_manager.isNull());
	particle_manager = new ParticleManager(this->base_dir_path, opengl_engine.ptr(), physics_world.ptr(), terrain_decal_manager.ptr());

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

	// Init indigoView
	this->ui->indigoView->initialise(this->base_dir_path);
	{
		Lock lock(this->world_state->mutex);
		this->ui->indigoView->addExistingObjects(*this->world_state, *this->resource_manager);
	}
}


Vec4f MainWindow::getDirForPixelTrace(int pixel_pos_x, int pixel_pos_y) const
{
	const Vec4f forwards = cam_controller.getForwardsVec().toVec4fVector();
	const Vec4f right = cam_controller.getRightVec().toVec4fVector();
	const Vec4f up = cam_controller.getUpVec().toVec4fVector();

	const float sensor_width = GlWidget::sensorWidth();
	const float sensor_height = sensor_width / ui->glWidget->viewport_aspect_ratio;
	const float lens_sensor_dist = GlWidget::lensSensorDist();

	const float gl_w = (float)ui->glWidget->geometry().width();
	const float gl_h = (float)ui->glWidget->geometry().height();

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

Vec4f MainWindow::pointOnLineWorldSpace(const Vec4f& p_a_ws, const Vec4f& p_b_ws, const Vec2f& pixel_coords) const
{
	const Vec4f cam_origin = cam_controller.getPosition().toVec4fPoint();
	const Vec4f cam_forw   = cam_controller.getForwardsVec().toVec4fVector();
	const Vec4f cam_right  = cam_controller.getRightVec().toVec4fVector();
	const Vec4f cam_up     = cam_controller.getUpVec().toVec4fVector();

	const float sensor_width  = GlWidget::sensorWidth();
	const float sensor_height = sensor_width / ui->glWidget->viewport_aspect_ratio;
	const float lens_sensor_dist = GlWidget::lensSensorDist();

	const float gl_w = (float)ui->glWidget->geometry().width();
	const float gl_h = (float)ui->glWidget->geometry().height();

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
bool MainWindow::getPixelForPoint(const Vec4f& point_ws, Vec2f& pixel_coords_out) const// Returns true if point is visible from camera.
{
	const Vec4f forwards = cam_controller.getForwardsVec().toVec4fVector();
	const Vec4f right = cam_controller.getRightVec().toVec4fVector();
	const Vec4f up = cam_controller.getUpVec().toVec4fVector();

	const float sensor_width = GlWidget::sensorWidth();
	const float sensor_height = sensor_width / ui->glWidget->viewport_aspect_ratio;
	const float lens_sensor_dist = GlWidget::lensSensorDist();

	const float gl_w = (float)ui->glWidget->geometry().width();
	const float gl_h = (float)ui->glWidget->geometry().height();

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
bool MainWindow::getGLUICoordsForPoint(const Vec4f& point_ws, Vec2f& coords_out) const// Returns true if point is visible from camera.
{
	const Vec4f forwards = cam_controller.getForwardsVec().toVec4fVector();
	const Vec4f right = cam_controller.getRightVec().toVec4fVector();
	const Vec4f up = cam_controller.getUpVec().toVec4fVector();

	const float sensor_width = GlWidget::sensorWidth();
	const float lens_sensor_dist = GlWidget::lensSensorDist();

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
int MainWindow::mouseOverAxisArrowOrRotArc(const Vec2f& pixel_coords, Vec4f& closest_seg_point_ws_out) 
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

			const float gl_w = (float)ui->glWidget->geometry().width();
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

				const float gl_w = (float)ui->glWidget->geometry().width();
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


void MainWindow::glWidgetMousePressed(QMouseEvent* e)
{
	// Trace through scene to see if we are clicking on a web-view
	if(this->physics_world.nonNull())
	{
		// Trace ray through scene
		const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
		const Vec4f dir = getDirForPixelTrace(e->pos().x(), e->pos().y());

		RayTraceResult results;
		this->physics_world->traceRay(origin, dir, /*max_t=*/1.0e5f, results);

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
				ob->web_view_data->mousePressed(e, uvs);
			}
			else if(ob->browser_vid_player.nonNull()) // If this is a video object:
			{
				ob->browser_vid_player->mousePressed(e, uvs);
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
			grabbed_axis = mouseOverAxisArrowOrRotArc(Vec2f((float)e->pos().x(), (float)e->pos().y()), /*closest_seg_point_ws_out=*/this->grabbed_point_ws);

			if(grabbed_axis >= 0) // If we grabbed an arrow or rotation arc:
			{
				this->ob_origin_at_grab = this->selected_ob->pos.toVec4fPoint();

				// Usually when the mouse button is held down, moving the mouse rotates the camera.
				// But when we have grabbed an arrow or rotation arc, it moves the object instead.  So don't rotate the camera.
				ui->glWidget->setCamRotationOnMouseMoveEnabled(false);

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
				const Vec4f dir = getDirForPixelTrace(e->pos().x(), e->pos().y());

				Planef plane(arc_centre, crossProduct(basis_a, basis_b));

				const float t = plane.rayIntersect(origin, dir);
				const Vec4f plane_p = origin + dir * t;

				const float angle = safeATan2(dot(plane_p - arc_centre, basis_b), dot(plane_p - arc_centre, basis_a));

				const Vec4f to_cam = cam_controller.getPosition().toVec4fPoint() - arc_centre;
				const float to_cam_angle = safeATan2(dot(basis_b, to_cam), dot(basis_a, to_cam)); // angle in basis_a-basis_b plane

				this->grabbed_angle = this->original_grabbed_angle = angle;
				this->grabbed_arc_angle_offset = to_cam_angle - this->original_grabbed_angle;

				//ui->glWidget->opengl_engine->addObject(ui->glWidget->opengl_engine->makeAABBObject(plane_p, plane_p + Vec4f(0.05f, 0.05f, 0.05f, 0), Colour4f(1, 0, 1, 1)));
			}
		}
	}

	// If we didn't grab any control, we will be in camera-rotate mode, so hide the mouse cursor.
	if(grabbed_axis < 0)
		ui->glWidget->hideCursor();
}


static Vec2f GLCoordsForGLWidgetPos(MainWindow* main_window, const Vec2f widget_pos)
{
	const int vp_width  = main_window->ui->glWidget->opengl_engine->getViewPortWidth();
	const int vp_height = main_window->ui->glWidget->opengl_engine->getViewPortHeight();

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	const double device_pixel_ratio = main_window->ui->glWidget->devicePixelRatio(); // For retina screens this is 2, meaning the gl viewport width is in physical pixels, of which have twice the density of qt pixel coordinates.
	const int use_vp_width  = (int)(vp_width  / device_pixel_ratio);
	const int use_vp_height = (int)(vp_height / device_pixel_ratio);
#else
	const int device_pixel_ratio = main_window->ui->glWidget->devicePixelRatio(); // For retina screens this is 2, meaning the gl viewport width is in physical pixels, of which have twice the density of qt pixel coordinates.
	const int use_vp_width  = vp_width  / device_pixel_ratio;
	const int use_vp_height = vp_height / device_pixel_ratio;
#endif

	return Vec2f(
		 (widget_pos.x - use_vp_width /2) / (use_vp_width /2),
		-(widget_pos.y - use_vp_height/2) / (use_vp_height/2)
	);
}


// This is emitted from GlWidget::mouseReleaseEvent().
void MainWindow::glWidgetMouseClicked(QMouseEvent* e)
{
	const Vec2f widget_pos((float)e->pos().x(), (float)e->pos().y());
	const Vec2f gl_coords = GLCoordsForGLWidgetPos(this, widget_pos);

	if(gl_ui.nonNull())
	{
		const bool accepted = gl_ui->handleMouseClick(gl_coords);
		if(accepted)
		{
			e->accept();
			return;
		}
	}

	if(grabbed_axis != -1 && selected_ob.nonNull())
	{
		undo_buffer.finishWorldObjectEdit(*selected_ob);
		grabbed_axis = -1;
	}
	ui->glWidget->setCamRotationOnMouseMoveEnabled(true);

	updateSelectedObjectPlacementBeam(); // Update so that rot arc handles snap back oriented to camera.

	if(selected_ob.nonNull())
	{
		selected_ob->decompressVoxels(); // Make sure voxels are decompressed for this object.
	}


	// Trace through scene to see if we are clicking on a web-view
	if(this->physics_world.nonNull())
	{
		// Trace ray through scene
		const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
		const Vec4f dir = getDirForPixelTrace(e->pos().x(), e->pos().y());

		RayTraceResult results;
		this->physics_world->traceRay(origin, dir, /*max_t=*/1.0e5f, results);

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
				ob->web_view_data->mouseReleased(e, uvs);
			}
			else if(ob->browser_vid_player.nonNull()) // If this is a video object:
			{
				ob->browser_vid_player->mouseReleased(e, uvs);
			}
		}
	}


	if(areEditingVoxels())
	{
		const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
		const Vec4f dir = getDirForPixelTrace(e->pos().x(), e->pos().y());
		RayTraceResult results;
		this->physics_world->traceRay(origin, dir, /*max_t=*/1.0e5f, results);
		if(results.hit_object)
		{
			const Vec4f hitpos_ws = origin + dir*results.hit_t;

			Vec2f pixel_coords;
			/*const bool visible = */getPixelForPoint(hitpos_ws, pixel_coords);

			if(selected_ob.nonNull())
			{
				selected_ob->decompressVoxels(); // Make sure voxels are decompressed for this object.

				if((e->modifiers() & Qt::ControlModifier) || e->modifiers() & Qt::AltModifier) // If user is trying to edit voxels:
				{
					const bool have_edit_permissions = objectModificationAllowedWithMsg(*selected_ob, "edit");
					if(have_edit_permissions)
					{
						const float current_voxel_w = 1;

						const Matrix4f world_to_ob = worldToObMatrix(*selected_ob);

						bool voxels_changed = false;

						if(e->modifiers() & Qt::ControlModifier)
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
								this->selected_ob->getDecompressedVoxels().back().mat_index = ui->objectEditor->getSelectedMatIndex();

								voxels_changed = true;

								undo_buffer.finishWorldObjectEdit(*selected_ob);
							}
							else
							{
								showErrorNotification("Can't create voxel that far away from rest of voxels.");
							}
						}
						else if(e->modifiers() & Qt::AltModifier)
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
}


void MainWindow::updateObjectModelForChangedDecompressedVoxels(WorldObjectRef& ob)
{
	Lock lock(this->world_state->mutex);

	ob->compressVoxels();

	ob->last_modified_time = TimeStamp::currentTime(); // Gets set on server as well, this is just for updating the local display.

	// Clear lightmap URL, since the lightmap will be invalid now the voxels (and hence the UV map) will have changed.
	ob->lightmap_url = "";

	// Remove any existing OpenGL and physics model
	if(ob->opengl_engine_ob.nonNull())
		ui->glWidget->opengl_engine->removeObject(ob->opengl_engine_ob);

	if(ob->opengl_light.nonNull())
		ui->glWidget->opengl_engine->removeLight(ob->opengl_light);

	destroyVehiclePhysicsControllingObject(ob.ptr()); // Destroy any vehicle controller controlling this object, as vehicle controllers have pointers to physics bodies.
	if(ob->physics_object.nonNull())
	{
		physics_world->removeObject(ob->physics_object);
		ob->physics_object = NULL;
	}

	// Update in Indigo view
	ui->indigoView->objectRemoved(*ob);

	if(!ob->getDecompressedVoxels().empty())
	{
		const Matrix4f ob_to_world = obToWorldMatrix(*ob);

		const int ob_lod_level = ob->getLODLevel(cam_controller.getPosition());

		js::Vector<bool, 16> mat_transparent(ob->materials.size());
		for(size_t i=0; i<ob->materials.size(); ++i)
			mat_transparent[i] = ob->materials[i]->opacity.val < 1.f;

		// Add updated model!
		PhysicsShape physics_shape;
		Indigo::MeshRef indigo_mesh;
		const int subsample_factor = 1;
		Reference<OpenGLMeshRenderData> gl_meshdata = ModelLoading::makeModelForVoxelGroup(ob->getDecompressedVoxelGroup(), subsample_factor, ob_to_world,
			ui->glWidget->opengl_engine->vert_buf_allocator.ptr(), /*do_opengl_stuff=*/true, /*need_lightmap_uvs=*/false, mat_transparent, /*build_dynamic_physics_ob=*/ob->isDynamic(),
			physics_shape, indigo_mesh);

		GLObjectRef gl_ob = ui->glWidget->opengl_engine->allocateObject();
		gl_ob->ob_to_world_matrix = ob_to_world;
		gl_ob->mesh_data = gl_meshdata;

		gl_ob->materials.resize(ob->materials.size());
		for(uint32 i=0; i<ob->materials.size(); ++i)
		{
			ModelLoading::setGLMaterialFromWorldMaterial(*ob->materials[i], ob_lod_level, ob->lightmap_url, *this->resource_manager, gl_ob->materials[i]);
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
		ui->glWidget->opengl_engine->addObjectAndLoadTexturesImmediately(gl_ob);

		// Update in Indigo view
		ui->indigoView->objectAdded(*ob, *this->resource_manager);

		ui->glWidget->opengl_engine->selectObject(gl_ob);

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


void MainWindow::pickUpSelectedObject()
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

			ui->glWidget->opengl_engine->setSelectionOutlineColour(PICKED_UP_OUTLINE_COLOUR);

			// Send UserSelectedObject message to server
			MessageUtils::initPacket(scratch_packet, Protocol::UserSelectedObject);
			writeToStream(selected_ob->uid, scratch_packet);
			enqueueMessageToSend(*this->client_thread, scratch_packet);

			showInfoNotification("Picked up object.");

			ui->objectEditor->objectPickedUp();

			selected_ob_picked_up = true;

			undo_buffer.startWorldObjectEdit(*selected_ob);

			// Play pick up sound, in the direction of the selection point
			const Vec4f to_pickup_point = normalise(selection_point_ws - origin);
			audio_engine.playOneShotSound(base_dir_path + "/resources/sounds/select_mono.wav", origin + to_pickup_point * 0.4f);
		}
	}
}


void MainWindow::dropSelectedObject()
{
	if(selected_ob.nonNull() && selected_ob_picked_up)
	{
		// Send UserDeselectedObject message to server
		MessageUtils::initPacket(scratch_packet, Protocol::UserDeselectedObject);
		writeToStream(selected_ob->uid, scratch_packet);

		enqueueMessageToSend(*this->client_thread, scratch_packet);

		ui->glWidget->opengl_engine->setSelectionOutlineColour(DEFAULT_OUTLINE_COLOUR);

		showInfoNotification("Dropped object.");

		ui->objectEditor->objectDropped();

		selected_ob_picked_up = false;

		undo_buffer.finishWorldObjectEdit(*selected_ob);

		// Play drop item sound, in the direction of the selection point.
		const Vec4f campos = this->cam_controller.getPosition().toVec4fPoint();
		const Vec4f selection_point_ws = obToWorldMatrix(*this->selected_ob) * this->selection_point_os;
		const Vec4f to_pickup_point = normalise(selection_point_ws - campos);

		audio_engine.playOneShotSound(base_dir_path + "/resources/sounds/deselect_mono.wav", campos + to_pickup_point * 0.4f);
	}
}


void MainWindow::setUIForSelectedObject() // Enable/disable delete object action etc..
{
	const bool have_selected_ob = this->selected_ob.nonNull();
	this->ui->actionCloneObject->setEnabled(have_selected_ob);
	this->ui->actionDeleteObject->setEnabled(have_selected_ob);
}


void MainWindow::doObjectSelectionTraceForMouseEvent(QMouseEvent* e)
{
	// Trace ray through scene, select object (if any) that is clicked on.
	const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
	const Vec4f dir = getDirForPixelTrace(e->pos().x(), e->pos().y());

	RayTraceResult results;
	this->physics_world->traceRay(origin, dir, /*max_t=*/1.0e5f, results);

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

			ui->glWidget->opengl_engine->selectObject(selected_parcel->opengl_engine_ob);
			ui->glWidget->opengl_engine->setSelectionOutlineColour(PARCEL_OUTLINE_COLOUR);

			// Show parcel editor, hide object editor.
			ui->parcelEditor->setFromParcel(*selected_parcel);
			ui->parcelEditor->setEnabled(true);
			ui->parcelEditor->show();
			ui->objectEditor->hide();
			ui->editorDockWidget->show(); // Show the object editor dock widget if it is hidden.
		}
		else if(results.hit_object->userdata && results.hit_object->userdata_type == 2) // If we hit an instance:
		{
			InstanceInfo* instance = static_cast<InstanceInfo*>(results.hit_object->userdata);
			selectObject(instance->prototype_object, results.hit_mat_index); // Select the original prototype object that the hit object is an instance of.
		}
		else // Else if the trace didn't hit anything:
		{
			ui->objectEditor->setEnabled(false);
		}

		const Vec4f selection_point_ws = origin + dir*results.hit_t;

		// Store the object-space selection point.  This will be used for moving the object.
		// Note: we set this after the selectObject() call above, which sets selection_point_os to (0,0,0).
		this->selection_point_os = results.hit_object->getWorldToObMatrix() * selection_point_ws;

		// Add gl object to show selection position:
		// ui->glWidget->opengl_engine->addObject(ui->glWidget->opengl_engine->makeAABBObject(selection_point_ws - Vec4f(0.05, 0.05, 0.05, 0), selection_point_ws + Vec4f(0.05, 0.05, 0.05, 0), Colour4f(0,0,1,1)));
	}
	else
	{
		// Deselect any currently selected object
		deselectObject();
		deselectParcel();
	}
}


void MainWindow::glWidgetMouseDoubleClicked(QMouseEvent* e)
{
	//conPrint("MainWindow::glWidgetMouseDoubleClicked()");

	doObjectSelectionTraceForMouseEvent(e);
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


// pos is in glWidget local coordinates.
// mouse_event is non-null if this is called from a mouse-move event
void MainWindow::updateInfoUIForMousePosition(const QPoint& pos, QMouseEvent* mouse_event)
{
	const Vec2f gl_coords = GLCoordsForGLWidgetPos(this, Vec2f((float)pos.x(), (float)pos.y()));

	// New for object mouseover hyperlink showing, and webview mouse-move events:
	if(this->physics_world.nonNull())
	{
		// Trace ray through scene
		const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
		const Vec4f dir = getDirForPixelTrace(pos.x(), pos.y());

		RayTraceResult results;
		this->physics_world->traceRay(origin, dir, /*max_t=*/1.0e5f, results);

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
							ob_info_ui.showHyperLink(ob->target_url, gl_coords);
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
							ob_info_ui.showMessage("Press [E] to enter vehicle", gl_coords);
						else
							ob_info_ui.showMessage("Press [E] to right vehicle", gl_coords);
						show_mouseover_info_ui = true;
					}

					if(show_mouseover_info_ui)
					{
						// Remove outline around any previously mouse-overed object (unless it is the main selected ob)
						if(this->mouseover_selected_gl_ob.nonNull())
						{
							if(ob != this->selected_ob.ptr()) 
								ui->glWidget->opengl_engine->deselectObject(this->mouseover_selected_gl_ob);
							this->mouseover_selected_gl_ob = NULL;
						}

						// Add outline around object
						if(ob->opengl_engine_ob.nonNull())
						{
							this->mouseover_selected_gl_ob = ob->opengl_engine_ob;
							ui->glWidget->opengl_engine->selectObject(ob->opengl_engine_ob);
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
					ui->glWidget->opengl_engine->deselectObject(this->mouseover_selected_gl_ob);
				this->mouseover_selected_gl_ob = NULL;
			}
			ob_info_ui.hideMessage();
		}
	}
}


void MainWindow::glWidgetMouseMoved(QMouseEvent* e)
{
	this->last_gl_widget_mouse_move_pos = e->pos();

	if(ui->glWidget->opengl_engine.isNull() || !ui->glWidget->opengl_engine->initSucceeded())
		return;

	const Vec2f widget_pos((float)e->pos().x(), (float)e->pos().y());
	const Vec2f gl_coords = GLCoordsForGLWidgetPos(this, widget_pos);

	if(gl_ui.nonNull())
	{
		const bool accepted = gl_ui->handleMouseMoved(gl_coords);
		if(accepted)
		{
			e->accept();
			return;
		}
	}

	if(!ui->glWidget->isCursorHidden())
		updateInfoUIForMousePosition(e->pos(), e);


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
			const Vec2f mousepos((float)e->pos().x(), (float)e->pos().y());

			const Vec2f closest_pixel = closestPointOnLineSegment(mousepos, start_pixelpos, end_pixelpos); // Closest pixel coords of point on 2d line to mouse pointer.

			// Project point on 2d line into 3d space along the line
			Vec4f new_p = pointOnLineWorldSpace(axis_arrow_segments[grabbed_axis].a, axis_arrow_segments[grabbed_axis].b, closest_pixel);

			// ui->glWidget->opengl_engine->addObject(ui->glWidget->opengl_engine->makeAABBObject(new_p, new_p + Vec4f(0.1f,0.1f,0.1f,0), Colour4f(0.9, 0.2, 0.5, 1.f)));

			Vec4f delta_p = new_p - grabbed_point_ws; // Desired change in position from when we grabbed the object

			assert(new_p.isFinite());

			Vec4f tentative_new_ob_p = ob_origin_at_grab + delta_p;

			if(tentative_new_ob_p.getDist(ob_origin_at_grab) > MAX_MOVE_DIST)
				tentative_new_ob_p = ob_origin_at_grab + (tentative_new_ob_p - ob_origin_at_grab) * MAX_MOVE_DIST / (tentative_new_ob_p - ob_origin_at_grab).length();

			assert(tentative_new_ob_p.isFinite());

			// Snap to grid
			if(ui->objectEditor->snapToGridCheckBox->isChecked())
			{
				const double grid_spacing = ui->objectEditor->gridSpacingDoubleSpinBox->value();
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
		const Vec4f dir = getDirForPixelTrace(e->pos().x(), e->pos().y());

		Planef plane(arc_centre, crossProduct(basis_a, basis_b));

		const float t = plane.rayIntersect(origin, dir);
		const Vec4f plane_p = origin + dir * t;

		//ui->glWidget->opengl_engine->addObject(ui->glWidget->opengl_engine->makeAABBObject(plane_p, plane_p + Vec4f(0.05f, 0.05f, 0.05f, 0), Colour4f(1, 0, 1, 1)));

		const float angle = safeATan2(dot(plane_p - arc_centre, basis_b), dot(plane_p - arc_centre, basis_a));

		const float delta = angle - grabbed_angle;

		//Matrix4f tentative_new_to_world = this->selected_ob->opengl_engine_ob->ob_to_world_matrix;
		//tentative_new_to_world = Matrix4f::rotationMatrix(crossProduct(basis_a, basis_b), delta) * tentative_new_to_world;
		//tryToMoveObject(tentative_new_to_world);

		rotateObject(this->selected_ob, crossProduct(basis_a, basis_b), delta);

		grabbed_angle = angle;

		updateSelectedObjectPlacementBeam(); // Update rotation arc handles etc..
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
				ui->glWidget->opengl_engine->objectMaterialsUpdated(*axis_arrow_objects[i]);
			}

			for(int i=0; i<3; ++i)
			{
				rot_handle_arc_objects[i]->materials[0].albedo_linear_rgb = toLinearSRGB(axis_arrows_default_cols[i]);
				ui->glWidget->opengl_engine->objectMaterialsUpdated(*rot_handle_arc_objects[i]);
			}

			//if(!mouse_trace_hit_selected_ob)
			{
				Vec4f dummy_grabbed_point_ws;
				const int axis = mouseOverAxisArrowOrRotArc(Vec2f((float)e->pos().x(), (float)e->pos().y()), dummy_grabbed_point_ws);
		
				if(axis >= 0 && axis < NUM_AXIS_ARROWS)
				{
					axis_arrow_objects[axis]->materials[0].albedo_linear_rgb = toLinearSRGB(axis_arrows_mouseover_cols[axis % 3]);
					ui->glWidget->opengl_engine->objectMaterialsUpdated(*axis_arrow_objects[axis]);
				}

				if(axis >= NUM_AXIS_ARROWS && axis < NUM_AXIS_ARROWS + 3)
				{
					const int grabbed_rot_axis = axis - NUM_AXIS_ARROWS;
					rot_handle_arc_objects[grabbed_rot_axis]->materials[0].albedo_linear_rgb = toLinearSRGB(axis_arrows_mouseover_cols[grabbed_rot_axis]);
					ui->glWidget->opengl_engine->objectMaterialsUpdated(*rot_handle_arc_objects[grabbed_rot_axis]);
				}
			}
		}
	}
}


// The user wants to rotate the object 'ob'.
void MainWindow::rotateObject(WorldObjectRef ob, const Vec4f& axis, float angle)
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
		opengl_ob->ob_to_world_matrix = new_ob_to_world;
		ui->glWidget->opengl_engine->updateObjectTransformData(*opengl_ob);

		// Update physics object
		physics_world->setNewObToWorldTransform(*ob->physics_object, ob->pos.toVec4fVector(), new_q, useScaleForWorldOb(ob->scale).toVec4fVector());

		// Update in Indigo view
		ui->indigoView->objectTransformChanged(*ob);

		// Set a timer to call updateObjectEditorObTransformSlot() later.  Not calling this every frame avoids stutters with webviews playing back videos interacting with Qt updating spinboxes.
		if(!update_ob_editor_transform_timer->isActive())
			update_ob_editor_transform_timer->start(/*msec=*/50);

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
				ui->glWidget->opengl_engine->lightUpdated(light);
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


void MainWindow::updateObjectEditorObTransformSlot()
{
	if(this->selected_ob.nonNull())
		ui->objectEditor->setTransformFromObject(*this->selected_ob);
}


void MainWindow::deleteSelectedObject()
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


ObjectPathController* MainWindow::getPathControllerForOb(const WorldObject& ob)
{
	for(size_t i=0; i<path_controllers.size(); ++i)
		if(path_controllers[i]->controlled_ob.ptr() == &ob)
			return path_controllers[i].ptr();
	return NULL;
}


void MainWindow::createPathControlledPathVisObjects(const WorldObject& ob)
{
	// Remove any existing ones
	for(size_t i=0; i<selected_ob_vis_gl_obs.size(); ++i)
		ui->glWidget->opengl_engine->removeObject(this->selected_ob_vis_gl_obs[i]);
	selected_ob_vis_gl_obs.clear();

	{
		ObjectPathController* path_controller = getPathControllerForOb(ob);
		if(path_controller)
		{
			// Draw path by making opengl objects
			for(size_t i=0; i<path_controller->waypoints.size(); ++i)
			{
				const Vec4f begin_pos = path_controller->waypoints[i].pos;
				const Vec4f end_pos = path_controller->waypoints[Maths::intMod((int)i + 1, (int)path_controller->waypoints.size())].pos;

				GLObjectRef edge_gl_ob = ui->glWidget->opengl_engine->allocateObject();

				const Matrix4f dir_matrix = Matrix4f::constructFromVectorStatic(normalise(end_pos - begin_pos));
				const Matrix4f scale_matrix = Matrix4f::scaleMatrix(/*radius=*/0.03f,/*radius=*/0.03f, begin_pos.getDist(end_pos));
				const Matrix4f ob_to_world = Matrix4f::translationMatrix(begin_pos) * dir_matrix * scale_matrix;

				edge_gl_ob->ob_to_world_matrix = ob_to_world;
				edge_gl_ob->mesh_data = ui->glWidget->opengl_engine->getCylinderMesh();

				OpenGLMaterial material;
				material.albedo_linear_rgb = toLinearSRGB(Colour3f(0.8f, 0.3f, 0.3f));
				material.transparent = true;
				material.alpha = 0.9f;
				edge_gl_ob->setSingleMaterial(material);

				ui->glWidget->opengl_engine->addObject(edge_gl_ob);

				// Add cube at vertex
				const float half_w = 0.2f;
				GLObjectRef vert_gl_ob = ui->glWidget->opengl_engine->makeAABBObject(begin_pos - Vec4f(half_w, half_w, half_w, 0), begin_pos + Vec4f(half_w, half_w, half_w, 0), Colour4f(0.3f, 0.8f, 0.3f, 0.9f));
				ui->glWidget->opengl_engine->addObject(vert_gl_ob);

				// Keep track of these objects we added.
				selected_ob_vis_gl_obs.push_back(edge_gl_ob);
				selected_ob_vis_gl_obs.push_back(vert_gl_ob);
			}
		}
	}
}


void MainWindow::selectObject(const WorldObjectRef& ob, int selected_mat_index)
{
	assert(ob.nonNull());

	// Deselect any existing object
	deselectObject();


	this->selected_ob = ob;
	assert(this->selected_ob->getRefCount() >= 0);

	this->selected_ob->is_selected = true;

	this->selection_point_os = Vec4f(0, 0, 0, 1); // Store a default value for this (kind of a pivot point).


	// If diagnostics widget is shown, show an AABB visualisation as well.
	if(ui->diagnosticsDockWidget->isVisible() && ui->diagnosticsWidget->showObAABBsCheckBox->isChecked())
	{
		// Add object-space AABB visualisation gl ob.
		this->aabb_os_vis_gl_ob = ui->glWidget->opengl_engine->makeAABBObject(this->selected_ob->getAABBOS().min_, this->selected_ob->getAABBOS().max_, Colour4f(0.3f, 0.7f, 0.3f, 0.5f));
		aabb_os_vis_gl_ob->ob_to_world_matrix = selected_ob->obToWorldMatrix() * OpenGLEngine::AABBObjectTransform(selected_ob->getAABBOS().min_, selected_ob->getAABBOS().max_);
		ui->glWidget->opengl_engine->addObject(this->aabb_os_vis_gl_ob);

		// Add world-space AABB visualisation gl ob.
		this->aabb_ws_vis_gl_ob = ui->glWidget->opengl_engine->makeAABBObject(this->selected_ob->getAABBWS().min_, this->selected_ob->getAABBWS().max_, Colour4f(0.7f, 0.3f, 0.3f, 0.5f));
		ui->glWidget->opengl_engine->addObject(this->aabb_ws_vis_gl_ob);
	}

	createPathControlledPathVisObjects(*this->selected_ob);

	// Mark the materials on the hit object as selected
	if(this->selected_ob->opengl_engine_ob.nonNull())
	{
		ui->glWidget->opengl_engine->selectObject(selected_ob->opengl_engine_ob);
		ui->glWidget->opengl_engine->setSelectionOutlineColour(DEFAULT_OUTLINE_COLOUR);
	}

	// Turn on voxel grid drawing if this is a voxel object
	if((this->selected_ob->object_type == WorldObject::ObjectType_VoxelGroup) && this->selected_ob->opengl_engine_ob.nonNull())
	{
		for(size_t z=0; z<this->selected_ob->opengl_engine_ob->materials.size(); ++z)
			this->selected_ob->opengl_engine_ob->materials[z].draw_planar_uv_grid = true;

		ui->glWidget->opengl_engine->objectMaterialsUpdated(*this->selected_ob->opengl_engine_ob);
	}

	const bool have_edit_permissions = objectModificationAllowed(*this->selected_ob);

	// Add an object placement beam
	if(have_edit_permissions)
	{
		ui->glWidget->opengl_engine->addObject(ob_placement_beam);
		ui->glWidget->opengl_engine->addObject(ob_placement_marker);

		if(ui->objectEditor->posAndRot3DControlsEnabled())
		{
			for(int i=0; i<NUM_AXIS_ARROWS; ++i)
				ui->glWidget->opengl_engine->addObject(axis_arrow_objects[i]);

			for(int i=0; i<3; ++i)
				ui->glWidget->opengl_engine->addObject(rot_handle_arc_objects[i]);

			axis_and_rot_obs_enabled = true;
		}

		updateSelectedObjectPlacementBeam();
	}

	// Show object editor, hide parcel editor.
	ui->objectEditor->setFromObject(*selected_ob, selected_mat_index, /*ob in editing user's world=*/connectedToUsersPersonalWorldOrGodUser()); // Update the editor widget with values from the selected object
	ui->objectEditor->setEnabled(true);
	ui->objectEditor->show();
	ui->parcelEditor->hide();

	setUIForSelectedObject();

	ui->objectEditor->setControlsEditable(have_edit_permissions);
	ui->editorDockWidget->show(); // Show the object editor dock widget if it is hidden.

	// Update help text
	if(have_edit_permissions)
	{
		QString text;
		if(selected_ob->object_type == WorldObject::ObjectType_VoxelGroup)
			text += "Ctrl + left-click: Add voxel.\n"
			"Alt + left-click: Delete voxel.\n"
			"\n";

		text += "'E' key: Pick up/drop object.\n"
			"Click and drag the mouse to move the object around when picked up.\n"
			"'[' and  ']' keys rotate the object.\n"
			"PgUp and  pgDown keys rotate the object.\n"
			"'-' and '+' keys or mouse wheel moves object near/far.\n"
			"Esc key: deselect object.";

		this->ui->helpInfoLabel->setText(text);
	}
}


void MainWindow::deselectObject()
{
	if(this->selected_ob.nonNull())
	{
		this->selected_ob->is_selected = false;
		dropSelectedObject();

		// Remove placement beam from 3d engine
		ui->glWidget->opengl_engine->removeObject(this->ob_placement_beam);
		ui->glWidget->opengl_engine->removeObject(this->ob_placement_marker);

		for(int i=0; i<NUM_AXIS_ARROWS; ++i)
			ui->glWidget->opengl_engine->removeObject(this->axis_arrow_objects[i]);

		for(int i=0; i<3; ++i)
			ui->glWidget->opengl_engine->removeObject(this->rot_handle_arc_objects[i]);

		axis_and_rot_obs_enabled = false;

		// Remove any edge markers
		while(ob_denied_move_markers.size() > 0)
		{
			ui->glWidget->opengl_engine->removeObject(ob_denied_move_markers.back());
			ob_denied_move_markers.pop_back();
		}

		// Remove visualisation objects
		if(this->aabb_os_vis_gl_ob.nonNull())
		{
			ui->glWidget->opengl_engine->removeObject(this->aabb_os_vis_gl_ob);
			this->aabb_os_vis_gl_ob = NULL;
		}
		if(this->aabb_ws_vis_gl_ob.nonNull())
		{
			ui->glWidget->opengl_engine->removeObject(this->aabb_ws_vis_gl_ob);
			this->aabb_ws_vis_gl_ob = NULL;
		}

		for(size_t i=0; i<selected_ob_vis_gl_obs.size(); ++i)
			ui->glWidget->opengl_engine->removeObject(this->selected_ob_vis_gl_obs[i]);
		selected_ob_vis_gl_obs.clear();



		// Deselect any currently selected object
		ui->glWidget->opengl_engine->deselectObject(this->selected_ob->opengl_engine_ob);

		// Turn off voxel grid drawing if this is a voxel object
		if((this->selected_ob->object_type == WorldObject::ObjectType_VoxelGroup) && this->selected_ob->opengl_engine_ob.nonNull())
		{
			for(size_t z=0; z<this->selected_ob->opengl_engine_ob->materials.size(); ++z)
				this->selected_ob->opengl_engine_ob->materials[z].draw_planar_uv_grid = false;

			ui->glWidget->opengl_engine->objectMaterialsUpdated(*this->selected_ob->opengl_engine_ob);
		}

		ui->objectEditor->setEnabled(false);

		this->selected_ob = NULL;

		grabbed_axis = -1;

		this->shown_object_modification_error_msg = false;

		this->ui->helpInfoLabel->setText(default_help_info_message);

		setUIForSelectedObject();
	}
}


void MainWindow::deselectParcel()
{
	if(this->selected_parcel.nonNull())
	{
		// Deselect any currently selected object
		ui->glWidget->opengl_engine->deselectObject(this->selected_parcel->opengl_engine_ob);

		ui->parcelEditor->setEnabled(false);

		this->selected_parcel = NULL;

		//this->shown_object_modification_error_msg = false;

		this->ui->helpInfoLabel->setText(default_help_info_message);
	}
}


// On Mac laptops, the delete key sends a Key_Backspace keycode for some reason.  So check for that as well.
static bool keyIsDeleteKey(int keycode)
{
#if defined(OSX)
	return 
		keycode == Qt::Key::Key_Backspace || 
		keycode == Qt::Key::Key_Delete;
#else
	return keycode == Qt::Key::Key_Delete;
#endif
}


void MainWindow::glWidgetKeyPressed(QKeyEvent* e)
{
	if(e->key() == Qt::Key::Key_Escape)
	{
		if(this->selected_ob.nonNull())
			deselectObject();

		if(this->selected_parcel.nonNull())
			deselectParcel();
	}

	if(selected_ob.nonNull() && selected_ob->web_view_data.nonNull()) // If we have a web-view object selected, send keyboard input to it:
	{
		ui->glWidget->setKeyboardCameraMoveEnabled(false); // We don't want WASD keys etc. to move the camera while we enter text into the webview, so disable camera moving from the keyboard.
		selected_ob->web_view_data->keyPressed(e);

		if(keyIsDeleteKey(e->key()))
			showInfoNotification("Use Edit > Delete Object menu command to delete object.");
		return;
	}
	else
		ui->glWidget->setKeyboardCameraMoveEnabled(true);

	
	if(keyIsDeleteKey(e->key()))
	{
		if(this->selected_ob.nonNull())
		{
			deleteSelectedObject();
		}
	}
	else if(e->key() == Qt::Key::Key_P)
	{
		// Spawn particle for testing
		for(int i=0; i<1; ++i)
		{
			Particle particle;
			particle.pos = (cam_controller.getFirstPersonPosition() + cam_controller.getForwardsVec()).toVec4fPoint(); // Vec4f(0,0,1.5,1);

			particle.vel = Vec4f(-1 + 2*rng.unitRandom(),-1 + 2*rng.unitRandom(),rng.unitRandom() * 2,0) * 1.f;
		
			particle.area = 0.01; // 0.000001f;

			particle.colour = Colour3f(0.75f);

			particle.particle_type = Particle::ParticleType_Smoke;

			particle.dwidth_dt = 0;
			particle.theta = rng.unitRandom() * Maths::get2Pi<float>();

			particle.dopacity_dt = 0;//-0.01f;

			particle_manager->addParticle(particle);
		}


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
	else if(e->key() == Qt::Key::Key_E)
	{
		// If we are controlling a vehicle, exit it
		if(vehicle_controller_inside.nonNull())
		{
			const Vec4f last_hover_car_pos = vehicle_controller_inside->getBodyTransform(*this->physics_world) * Vec4f(0,0,0,1);
			const Vec4f last_hover_car_linear_vel = vehicle_controller_inside->getLinearVel(*this->physics_world);

			const Vec4f last_hover_car_right_ws = vehicle_controller_inside->getSeatToWorldTransform(*this->physics_world, this->cur_seat_index, /*use_smoothed_network_transform=*/true) * Vec4f(-1,0,0,0);
			// TODO: make this programmatically the same side as the seat, or make the exit position scriptable?

			vehicle_controller_inside->userExitedVehicle(this->cur_seat_index);
			vehicle_controller_inside = NULL; // Null out vehicle_controller_inside reference.  Note that the controller will still exist and be referenced from vehicle_controllers.

			Vec4f new_player_pos = last_hover_car_pos + last_hover_car_right_ws * 2 + Vec4f(0,0,1.7f,0);
			new_player_pos[2] = myMax(new_player_pos[2], 1.67f); // Make sure above ground

			player_physics.setPosition(Vec3d(new_player_pos), /*linear vel=*/last_hover_car_linear_vel);


			// Send AvatarExitedVehicle message to server
			MessageUtils::initPacket(scratch_packet, Protocol::AvatarExitedVehicle);
			writeToStream(this->client_avatar_uid, scratch_packet);
			enqueueMessageToSend(*this->client_thread, scratch_packet);

			return;
		}
		else
		{
			Lock lock(world_state->mutex);

			const QPoint widget_pos = ui->glWidget->mapFromGlobal(QCursor::pos());

			// conPrint("glWidgetKeyPressed: widget_pos: " + toString(widget_pos.x()) + ", " + toString(widget_pos.y()));

			// Trace ray through scene
			const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
			const Vec4f dir = getDirForPixelTrace(widget_pos.x(), widget_pos.y());

			RayTraceResult results;
			this->physics_world->traceRay(origin, dir, /*max_t=*/1.0e5f, results);

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
									QDesktopServices::openUrl(QtUtils::toQString(url));
								}
								else if(protocol == "sub")
								{
									visitSubURL(url);
								}
								else
								{
									// Don't open this URL, might be something potentially unsafe like a file on disk
									QErrorMessage m;
									m.showMessage("This URL is potentially unsafe and will not be opened.");
									m.exec();
								}
							}
							else
							{
								QDesktopServices::openUrl(QtUtils::toQString("http://" + url));
							}

							return;
						}
					}
				}
			}
		}
	}
	else if(e->key() == Qt::Key::Key_F)
	{
		ui->actionFly_Mode->toggle();
		ui->actionFly_Mode->triggered(ui->actionFly_Mode->isChecked()); // Need to manually emit triggered signal, toggle doesn't do it.
	}
	else if(e->key() == Qt::Key::Key_V)
	{
		ui->actionThird_Person_Camera->toggle();
		ui->actionThird_Person_Camera->triggered(ui->actionThird_Person_Camera->isChecked()); // Need to manually emit triggered signal, toggle doesn't do it.
	}
	
	if(this->selected_ob.nonNull())
	{
		const float angle_step = Maths::pi<float>() / 32;
		if(e->key() == Qt::Key::Key_BracketLeft)
		{
			// Rotate object clockwise around z axis
			rotateObject(this->selected_ob, Vec4f(0,0,1,0), -angle_step);
		}
		else if(e->key() == Qt::Key::Key_BracketRight)
		{
			rotateObject(this->selected_ob, Vec4f(0,0,1,0), angle_step);
		}
		else if(e->key() == Qt::Key::Key_PageUp)
		{
			// Rotate object clockwise around camera right-vector
			rotateObject(this->selected_ob, this->cam_controller.getRightVec().toVec4fVector(), -angle_step);
		}
		else if(e->key() == Qt::Key::Key_PageDown)
		{
			// Rotate object counter-clockwise around camera right-vector
			rotateObject(this->selected_ob, this->cam_controller.getRightVec().toVec4fVector(), angle_step);
		}
		else if(e->key() == Qt::Key::Key_Equal || e->key() == Qt::Key::Key_Plus)
		{
			this->selection_vec_cs[1] *= 1.05f;
		}
		else if(e->key() == Qt::Key::Key_Minus)
		{
			this->selection_vec_cs[1] *= (1/1.05f);
		}
		else if(e->key() == Qt::Key::Key_E)
		{
			if(!this->selected_ob_picked_up)
				pickUpSelectedObject();
			else
				dropSelectedObject();
		}
	}

	if(e->key() == Qt::Key::Key_F5)
	{
		this->connectToServer("sub://" + this->server_hostname + "/" + this->server_worldname +
			"?x=" + doubleToStringMaxNDecimalPlaces(this->cam_controller.getPosition().x, 2) + 
			"&y=" + doubleToStringMaxNDecimalPlaces(this->cam_controller.getPosition().y, 2) + 
			"&z=" + doubleToStringMaxNDecimalPlaces(this->cam_controller.getPosition().z, 2));
	}
}


void MainWindow::glWidgetkeyReleased(QKeyEvent* e)
{
}


void MainWindow::glWidgetMouseWheelEvent(QWheelEvent* e)
{
	const Vec2f widget_pos((float)e->pos().x(), (float)e->pos().y());
	const Vec2f gl_coords = GLCoordsForGLWidgetPos(this, widget_pos);

	if(gl_ui.nonNull())
	{
		GLUIMouseWheelEvent wheel_event;
		wheel_event.angle_delta_y = e->angleDelta().y();

		const bool accepted = gl_ui->handleMouseWheelEvent(gl_coords, wheel_event);
		if(accepted)
		{
			e->accept();
			return;
		}
	}

	// Trace through scene to see if the mouse is over a web-view
	if(this->physics_world.nonNull())
	{
		const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();

#if QT_VERSION_MAJOR >= 6
		const Vec4f dir = getDirForPixelTrace((int)e->position().x(), (int)e->position().y());
#else
		const Vec4f dir = getDirForPixelTrace((int)e->pos().x(), (int)e->pos().y());
#endif
	
		RayTraceResult results;
		this->physics_world->traceRay(origin, dir, /*max_t=*/1.0e5f, results);
	
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
				ob->web_view_data->wheelEvent(e, uvs);
				e->accept();
				return;
			}
			else if(ob->browser_vid_player.nonNull()) // If this is a video object:
			{
				ob->browser_vid_player->wheelEvent(e, uvs);
				e->accept();
				return;
			}
		}
	}

	if(this->selected_ob.nonNull() && selected_ob_picked_up)
	{
		this->selection_vec_cs[1] *= (1.0f + e->angleDelta().y() * 0.0005f);
	}
	else
	{
		if(!cam_controller.thirdPersonEnabled() && (e->angleDelta().y() < 0)) // If we were in first person view, and scrolled down, change to third-person view.
		{
			ui->actionThird_Person_Camera->setChecked(true);
			ui->actionThird_Person_Camera->triggered(true); // Need to manually trigger the action.
		}
		else if(cam_controller.thirdPersonEnabled())
		{
			const bool change_to_first_person = cam_controller.handleScrollWheelEvent((float)e->angleDelta().y());
			if(change_to_first_person)
			{
				ui->actionThird_Person_Camera->setChecked(false);
				ui->actionThird_Person_Camera->triggered(true); // Need to manually trigger the action.
			}
		}
	}
}


void MainWindow::glWidgetViewportResized(int w, int h)
{
	if(gl_ui.nonNull())
	{
		gl_ui->viewportResized(w, h);
	}
	gesture_ui.viewportResized(w, h);
	ob_info_ui.viewportResized(w, h);
	misc_info_ui.viewportResized(w, h);
	hud_ui.viewportResized(w, h);
	minimap.viewportResized(w, h);
}


void MainWindow::cameraUpdated()
{
	ui->indigoView->cameraUpdated(this->cam_controller);
}


void MainWindow::playerMoveKeyPressed()
{
	stopGesture();

	gesture_ui.stopAnyGesturePlaying();
}


GLObjectRef MainWindow::makeNameTagGLObject(const std::string& nametag)
{
	QFont system_font = QApplication::font();
	system_font.setPointSize(48);

	const QFontMetrics font_metrics(system_font);
	const QRect rect = font_metrics.boundingRect(QtUtils::toQString(nametag));
	
	const int w_padding = (int)(font_metrics.height() * 0.5f);
	const int h_padding = (int)(font_metrics.height() * 0.5f);

	const int w_px = rect.width()          + w_padding * 2;
	const int h_px = font_metrics.height() + h_padding * 2;

	GLObjectRef gl_ob = ui->glWidget->opengl_engine->allocateObject();
	const float mesh_h = (float)h_px / w_px;
	gl_ob->mesh_data = MeshPrimitiveBuilding::makeRoundedCornerRect(*ui->glWidget->opengl_engine->vert_buf_allocator, Vec4f(1,0,0,0), Vec4f(0,0,1,0), /*w=*/1.f, /*h=*/mesh_h, /*corner radius=*/mesh_h / 4.f, /*tris_per_corner=*/8);
	gl_ob->materials.resize(1);

	// Make nametag texture
	ImageMapUInt8Ref map = new ImageMapUInt8(w_px, h_px, 3);
	
	QImage image(w_px, h_px, QImage::Format_RGB888);
	image.fill(QColor(255, 255, 255));
	QPainter painter(&image);
	painter.setPen(QPen(QColor(150, 150, 150)));
	painter.setFont(system_font);
	painter.drawText(image.rect(), Qt::AlignCenter, QtUtils::toQString(nametag));

	// Copy to the ImageMapUInt8 image
	for(int y=0; y<h_px; ++y)
		std::memcpy(map->getPixel(0, h_px - y - 1), image.scanLine(y), 3*w_px);

	gl_ob->materials[0].fresnel_scale = 0.1f;
	gl_ob->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(0.8f));
	TextureParams tex_params;
	tex_params.allow_compression = false;
	gl_ob->materials[0].albedo_texture = ui->glWidget->opengl_engine->getOrLoadOpenGLTextureForMap2D(OpenGLTextureKey("nametag_" + nametag), *map, tex_params);
	gl_ob->materials[0].cast_shadows = false;
	return gl_ob;
}


GLObjectRef MainWindow::makeSpeakerGLObject()
{
	GLObjectRef gl_ob = ui->glWidget->opengl_engine->allocateObject();
	gl_ob->mesh_data = MeshPrimitiveBuilding::makeRoundedCornerRect(*ui->glWidget->opengl_engine->vert_buf_allocator, Vec4f(1,0,0,0), Vec4f(0,0,1,0), /*w=*/1.f, /*h=*/1.f, /*corner radius=*/1.f / 4.f, /*tris_per_corner=*/8);
	gl_ob->materials.resize(1);
	gl_ob->materials[0].fresnel_scale = 0.1f;
	gl_ob->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(0.8f));
	gl_ob->materials[0].albedo_texture = ui->glWidget->opengl_engine->getTexture(base_dir_path + "/resources/buttons/vol_icon.png");
	gl_ob->materials[0].cast_shadows = false;
	return gl_ob;
}


OpenGLTextureRef MainWindow::makeToolTipTexture(const std::string& tooltip_text)
{
	const QString text = QtUtils::toQString(tooltip_text);

	QFont system_font = QApplication::font();
	system_font.setPointSize(24);

	QFontMetrics metrics(system_font);
	QRect text_rect = metrics.boundingRect(text);

	const int x_padding = 20; // in pixels
	const int y_padding = 12; // in pixels
	const int W = text_rect.width()  + x_padding*2;
	const int H = text_rect.height() + y_padding*2;
	ImageMapUInt8Ref map = new ImageMapUInt8(W, H, 3);

	QImage image(W, H, QImage::Format_RGB888);
	image.fill(QColor(255, 255, 255));
	QPainter painter(&image);
	painter.setPen(QPen(QColor(0, 0, 0)));
	painter.setFont(system_font);
	painter.drawText(x_padding, -text_rect.top() + y_padding, text);

	// Copy to map
	for(int y=0; y<H; ++y)
	{
		const uchar* line = image.scanLine(y);
		std::memcpy(map->getPixel(0, H - y - 1), line, 3*W);
	}

	TextureParams tex_params;
	tex_params.wrapping = OpenGLTexture::Wrapping_Clamp;
	tex_params.allow_compression = false;
	return ui->glWidget->opengl_engine->getOrLoadOpenGLTextureForMap2D(OpenGLTextureKey("tooltip_" + tooltip_text), *map, tex_params);
}


void MainWindow::setGLWidgetContextAsCurrent()
{
	this->ui->glWidget->makeCurrent();
}


//static bool done_terrain_test = false;

void MainWindow::updateGroundPlane()
{
	if(this->world_state.isNull())
		return;

	if(ui->glWidget->opengl_engine.isNull() || !ui->glWidget->opengl_engine->initSucceeded())
		return;

	if(terrain_system.isNull())
	{
		try
		{
			biome_manager->initTexturesAndModels(base_dir_path, *ui->glWidget->opengl_engine, *resource_manager); // TEMP NEW
		}
		catch(glare::Exception& e)
		{
			conPrint(e.what());
			QMessageBox msgBox;
			msgBox.setText(QtUtils::toQString(e.what()));
			msgBox.exec();
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
		}

		for(int i=0; i<4; ++i)
		{
			if(!spec.detail_col_map_URLs[i].empty())
				path_spec.detail_col_map_paths[i]    = resource_manager->pathForURL(spec.detail_col_map_URLs[i]);
			if(!spec.detail_height_map_URLs[i].empty())
				path_spec.detail_height_map_paths[i] = resource_manager->pathForURL(spec.detail_height_map_URLs[i]);
		}

		const float terrain_section_width_m = myClamp(spec.terrain_section_width_m, 8.f, 1000000.f);

		path_spec.terrain_section_width_m = terrain_section_width_m;
		path_spec.default_terrain_z = spec.default_terrain_z;
		path_spec.water_z = spec.water_z;
		path_spec.flags = spec.flags;


		const float aabb_ws_longest_len = terrain_section_width_m;

		//----------------------------- Start downloading textures, if not already present on disk in resource manager -----------------------------
		for(size_t i=0; i<spec.section_specs.size(); ++i)
		{
			const TerrainSpecSection& section_spec = spec.section_specs[i];
			const Vec4f centroid_ws(section_spec.x  * terrain_section_width_m, section_spec.y  * terrain_section_width_m, 0, 1);

			if(!section_spec.heightmap_URL.empty())
			{
				DownloadingResourceInfo info;
				info.use_sRGB = false;
				info.allow_compression = false;
				info.is_terrain_map = true;
				info.pos = Vec3d(centroid_ws);
				info.size_factor = LoadItemQueueItem::sizeFactorForAABBWS(aabb_ws_longest_len, /*importance_factor=*/1.f);
				startDownloadingResource(section_spec.heightmap_URL, centroid_ws, aabb_ws_longest_len, info);
			}
			if(!section_spec.mask_map_URL.empty())
			{
				DownloadingResourceInfo info;
				info.use_sRGB = false;
				info.allow_compression = false;
				info.is_terrain_map = true;
				info.pos = Vec3d(centroid_ws);
				info.size_factor = LoadItemQueueItem::sizeFactorForAABBWS(aabb_ws_longest_len, /*importance_factor=*/1.f);
				startDownloadingResource(section_spec.mask_map_URL, centroid_ws, aabb_ws_longest_len, info);
			}
		}

		for(int i=0; i<4; ++i)
		{
			if(!spec.detail_col_map_URLs[i].empty())
			{
				DownloadingResourceInfo info;
				info.use_sRGB = true;
				info.allow_compression = true;
				info.is_terrain_map = true;
				info.pos = Vec3d(0,0,0);
				info.size_factor = LoadItemQueueItem::sizeFactorForAABBWS(aabb_ws_longest_len, /*importance_factor=*/1.f);
				startDownloadingResource(spec.detail_col_map_URLs[i], /*centroid_ws=*/Vec4f(0,0,0,1), aabb_ws_longest_len, info);
			}
			if(!spec.detail_height_map_URLs[i].empty())
			{
				DownloadingResourceInfo info;
				info.use_sRGB = false;
				info.allow_compression = false;
				info.is_terrain_map = true;
				info.pos = Vec3d(0,0,0);
				info.size_factor = LoadItemQueueItem::sizeFactorForAABBWS(aabb_ws_longest_len, /*importance_factor=*/1.f);
				startDownloadingResource(spec.detail_height_map_URLs[i], /*centroid_ws=*/Vec4f(0,0,0,1), aabb_ws_longest_len, info);
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
				load_item_queue.enqueueItem(centroid_ws, aabb_ws_longest_len, 
					new LoadTextureTask(ui->glWidget->opengl_engine, this->texture_server, &this->msg_queue, path_section.heightmap_path, /*use_sRGB=*/false, /*allow compression=*/false, /*is terrain map=*/true, /*is_minimap_tile=*/false), 
					/*max_dist_for_ob_lod_level=*/std::numeric_limits<float>::max(), /*importance_factor=*/1.f);

			if(!section_spec.mask_map_URL.empty())
				load_item_queue.enqueueItem(centroid_ws, aabb_ws_longest_len, 
					new LoadTextureTask(ui->glWidget->opengl_engine, this->texture_server, &this->msg_queue, path_section.mask_map_path, /*use_sRGB=*/false, /*allow compression=*/false, /*is terrain map=*/true, /*is_minimap_tile=*/false), 
					/*max_dist_for_ob_lod_level=*/std::numeric_limits<float>::max(), /*importance_factor=*/1.f);
		}

		for(int i=0; i<4; ++i)
		{
			if(!spec.detail_col_map_URLs[i].empty() && this->resource_manager->isFileForURLPresent(spec.detail_col_map_URLs[i]))
				load_item_queue.enqueueItem(Vec4f(0,0,0,1), aabb_ws_longest_len, 
					new LoadTextureTask(ui->glWidget->opengl_engine, this->texture_server, &this->msg_queue, path_spec.detail_col_map_paths[i], /*use_sRGB=*/true, /*allow compression=*/true, /*is terrain map=*/true, /*is_minimap_tile=*/false), 
					/*max_dist_for_ob_lod_level=*/std::numeric_limits<float>::max(), /*importance_factor=*/1.f);

			if(!spec.detail_height_map_URLs[i].empty() && this->resource_manager->isFileForURLPresent(spec.detail_height_map_URLs[i]))
				load_item_queue.enqueueItem(Vec4f(0,0,0,1), aabb_ws_longest_len, 
					new LoadTextureTask(ui->glWidget->opengl_engine, this->texture_server, &this->msg_queue, path_spec.detail_height_map_paths[i], /*use_sRGB=*/false, /*allow compression=*/false, /*is terrain map=*/true, /*is_minimap_tile=*/false), 
					/*max_dist_for_ob_lod_level=*/std::numeric_limits<float>::max(), /*importance_factor=*/1.f);
		}
		//--------------------------------------------------------------------------------------------------------------------------------


		terrain_system = new TerrainSystem();
		terrain_system->init(path_spec, this->base_dir_path, ui->glWidget->opengl_engine.ptr(), this->physics_world.ptr(), biome_manager, this->cam_controller.getPosition(), &this->model_and_texture_loader_task_manager, bump_allocator, &this->msg_queue);
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

			GLObjectRef ob = ui->glWidget->opengl_engine->makeSphereObject(0.02f, Colour4f(1,0,0,1));
			ob->ob_to_world_matrix = Matrix4f::translationMatrix(Vec4f(px,py,500,1) + Vec4f(0,0,-1,0) * results.hitdist_ws) *  Matrix4f::uniformScaleMatrix(0.3f);
			ui->glWidget->opengl_engine->addObject(ob);
		}

		done_terrain_test = true;
	}
#endif
}


void MainWindow::performGestureClicked(const std::string& gesture_name, bool animate_head, bool loop_anim)
{
	const double cur_time = Clock::getTimeSinceInit(); // Used for animation, interpolation etc..

	// Change camera view to third person if it's not already, so we can see the gesture
	if(!ui->actionThird_Person_Camera->isChecked())
		ui->actionThird_Person_Camera->trigger();

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
}


void MainWindow::stopGesture()
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
	if(this->logged_in_user_id.valid())
	{
		MessageUtils::initPacket(scratch_packet, Protocol::AvatarStopGesture);
		writeToStream(this->client_avatar_uid, scratch_packet);

		enqueueMessageToSend(*this->client_thread, scratch_packet);
	}
}


void MainWindow::stopGestureClicked(const std::string& gesture_name)
{
	stopGesture();
}


void MainWindow::setSelfieModeEnabled(bool enabled)
{
	const double cur_time = Clock::getTimeSinceInit(); // Used for animation, interpolation etc..
	this->cam_controller.setSelfieModeEnabled(cur_time, enabled);

	if(enabled)
	{
		// Enable third-person camera view if not already enabled.
		if(!ui->actionThird_Person_Camera->isChecked())
			ui->actionThird_Person_Camera->trigger();
	}
}


void MainWindow::setMicForVoiceChatEnabled(bool enabled)
{
	if(enabled)
	{
		if(mic_read_thread_manager.getNumThreads() == 0)
		{
			Reference<glare::MicReadThread> mic_read_thread = new glare::MicReadThread(&this->msg_queue, this->udp_socket, this->client_avatar_uid, server_hostname, server_UDP_port,
				MainOptionsDialog::getInputDeviceName(settings),
				MainOptionsDialog::getInputScaleFactor(settings), // input_vol_scale_factor
				&mic_read_status
			);
			mic_read_thread_manager.addThread(mic_read_thread);
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


QPoint MainWindow::getGlWidgetPosInGlobalSpace() const
{
	return this->ui->glWidget->mapToGlobal(this->ui->glWidget->pos());
}


// See https://doc.qt.io/qt-5/qdesktopservices.html, this slot should be called when the user clicks on a sub:// link somewhere in the system.
void MainWindow::handleURL(const QUrl &url)
{
	this->connectToServer(QtUtils::toStdString(url.toString()));
}


void MainWindow::webViewDataLinkHovered(const QString& url)
{
	if(url.isEmpty())
	{
		ui->glWidget->setCursorIfNotHidden(Qt::ArrowCursor);
	}
	else
	{
		ui->glWidget->setCursorIfNotHidden(Qt::PointingHandCursor);
	}
}


// The mouse was double-clicked on a web-view object
void MainWindow::webViewMouseDoubleClicked(QMouseEvent* e)
{
	doObjectSelectionTraceForMouseEvent(e);
}


#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
typedef qintptr NativeEventArgType;
#else
typedef long NativeEventArgType;
#endif


// Override nativeEvent() so we can handle WM_COPYDATA messages from other Substrata processes.
// See https://www.programmersought.com/article/216036067/
bool MainWindow::nativeEvent(const QByteArray& event_type, void* message, NativeEventArgType* result)
{
#if defined(_WIN32)
	if(event_type == "windows_generic_MSG")
	{
		MSG* msg = reinterpret_cast<MSG*>(message);

		if(msg->message == WM_COPYDATA)
		{
			COPYDATASTRUCT* copy_data = reinterpret_cast<COPYDATASTRUCT*>(msg->lParam);

			const std::string text_body((const char*)copy_data->lpData, (const char*)copy_data->lpData + copy_data->cbData);

			if(hasPrefix(text_body, "openSubURL:"))
			{
				const std::string url = eatPrefix(text_body, "openSubURL:");

				conPrint("Opening URL '" + url + "'...");

				this->connectToServer(url);

				// Flash the taskbar icon, since the this window may not be visible.
				QApplication::alert(this);
			}
		}
	}
#endif

	return QWidget::nativeEvent(event_type, message, result); // Hand on to Qt processing
}


// If we receive a file-open event before the mainwindow has been created, (e.g. main_window is NULL), then store the url to use when the mainwindow is created.
// However if the mainwindow has already been created, call connectToServer on it.
// Note that this event will only be sent on Macs: "Note: This class is currently supported for macOS only."
// See https://www.programmersought.com/article/55521160114/ - "Use url scheme in mac os to evoke qt program and get startup parameters"
class OpenEventFilter : public QObject
{
public:
	OpenEventFilter() : main_window(NULL) {}

	virtual bool eventFilter(QObject* obj, QEvent* event)
	{
		if(event->type() == QEvent::FileOpen)
		{
			QFileOpenEvent* fileEvent = static_cast<QFileOpenEvent*>(event);
			if(!fileEvent->url().isEmpty())
			{
				const QString qurl = fileEvent->url().toString();

				if(main_window)
				{
					main_window->connectToServer(QtUtils::toStdString(qurl));

					QApplication::alert(main_window); // Flash the taskbar icon, since the this window may not be visible.
				}
				else
					url = QtUtils::toStdString(qurl);
			}
			return true; // Should the event be filtered out, e.g. have we handled it?
		}
		else
		{
			return QObject::eventFilter(obj, event);
		}
	}

	MainWindow* main_window;
	std::string url;
};


// Enable bugsplat unless the DISABLE_BUGSPLAT env var is set to a non-zero value.
#ifdef BUGSPLAT_SUPPORT
static bool shouldEnableBugSplat()
{
	try
	{
		const std::string val = PlatformUtils::getEnvironmentVariable("DISABLE_BUGSPLAT");
		return val == "0";
	}
	catch(glare::Exception&)
	{
		return true;
	}
}
#endif

#ifndef FUZZING


int main(int argc, char *argv[])
{
#ifdef BUGSPLAT_SUPPORT
	if(shouldEnableBugSplat())
	{
		// BugSplat initialization.
		new MiniDmpSender(
			L"Substrata", // database
			L"Substrata", // app
			StringUtils::UTF8ToPlatformUnicodeEncoding(cyberspace_version).c_str(), // version
			NULL, // app identifier
			MDSF_USEGUARDMEMORY | MDSF_LOGFILE | MDSF_PREVENTHIJACKING // flags
		);

		// The following calls add support for collecting crashes for abort(), vectored exceptions, out of memory,
		// pure virtual function calls, and for invalid parameters for OS functions.
		// These calls should be used for each module that links with a separate copy of the CRT.
		SetGlobalCRTExceptionBehavior();
		SetPerThreadCRTExceptionBehavior(); // This call needed in each thread of your app
	}
#endif


	QApplication::setAttribute(Qt::AA_UseDesktopOpenGL); // See https://forum.qt.io/topic/73255/qglwidget-blank-screen-on-different-computer/7

	//QtWebEngine::initialize();
//	QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
//	QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
//	QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
//	QtWebEngineQuick::initialize();

	// Note that this is deliberately constructed outside of the try..catch block below, because QErrorMessage crashes when displayed if
	// GuiClientApplication has been destroyed. (stupid qt).
	GuiClientApplication app(argc, argv);

	try
	{
		OpenEventFilter* open_even_filter = new OpenEventFilter();
		app.installEventFilter(open_even_filter);

#if defined(_WIN32)
		const bool com_init_success = WMFVideoReader::initialiseCOM();
#endif

		// Set the C standard lib locale back to c, so e.g. printf works as normal, and uses '.' as the decimal separator.
		std::setlocale(LC_ALL, "C");

		Clock::init();
		Networking::init();
		Winter::VirtualMachine::init();
		TLSSocket::initTLS();

		PlatformUtils::ignoreUnixSignals();

#if defined(_WIN32)
		// Initialize the Media Foundation platform.
		WMFVideoReader::initialiseWMF();
#endif

		std::string cyberspace_base_dir_path = PlatformUtils::getResourceDirectoryPath();
		std::string appdata_path = PlatformUtils::getOrCreateAppDataDirectory("Cyberspace");

		QDir::setCurrent(QtUtils::toQString(cyberspace_base_dir_path));
	
		conPrint("cyberspace_base_dir_path: " + cyberspace_base_dir_path);

		// Get a vector of the args.  Note that we will use app.arguments(), because it's the only way to get the args in Unicode in Qt.
		const QStringList arg_list = app.arguments();
		std::vector<std::string> args;
		for(int i = 0; i < arg_list.size(); ++i)
			args.push_back(QtUtils::toIndString(arg_list.at((int)i)));


		std::map<std::string, std::vector<ArgumentParser::ArgumentType> > syntax;
		syntax["--test"] = std::vector<ArgumentParser::ArgumentType>(); // Run unit tests
		syntax["-h"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // Specify hostname to connect to
		syntax["-u"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // Specify server URL to connect to
		syntax["-linku"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // Specify server URL to connect to, when a user has clicked on a substrata URL hyperlink.
		syntax["--extractanims"] = std::vector<ArgumentParser::ArgumentType>(2, ArgumentParser::ArgumentType_string); // Extract animation data
		syntax["--screenshotslave"] = std::vector<ArgumentParser::ArgumentType>(); // Run GUI as a screenshot-taking slave.
		syntax["--testscreenshot"] = std::vector<ArgumentParser::ArgumentType>(); // Test screenshot taking
		syntax["--no_MDI"] = std::vector<ArgumentParser::ArgumentType>(); // Disable MDI in graphics engine
		syntax["--no_bindless"] = std::vector<ArgumentParser::ArgumentType>(); // Disable bindless textures in graphics engine

		if(args.size() == 3 && args[1] == "-NSDocumentRevisionsDebugMode")
			args.resize(1); // This is some XCode debugging rubbish, remove it

		ArgumentParser parsed_args(args, syntax);

		if(parsed_args.isArgPresent("--test"))
		{
			TestSuite::test();
			return 0;
		}

		// Extract animation data from a GLTF file.
		if(parsed_args.isArgPresent("--extractanims"))
		{
			// E.g. --extractanims "D:\models\readyplayerme_avatar_animation_17.glb" "D:\models\extracted_avatar_anim.bin"
			const std::string input_path  = parsed_args.getArgStringValue("--extractanims", 0);
			const std::string output_path = parsed_args.getArgStringValue("--extractanims", 1);

			// Extract anims
			GLTFLoadedData data;
			Reference<BatchedMesh> mesh = FormatDecoderGLTF::loadGLBFile(input_path, data);

			FileOutStream file(output_path);
			mesh->animation_data.writeToStream(file);

			return 0;
		}


		//std::string server_hostname = "substrata.info";
		//std::string server_userpath = "";
		std::string server_URL = "sub://substrata.info";
		bool server_URL_explicitly_specified = false;

		if(parsed_args.isArgPresent("-h"))
		{
			server_URL = "sub://" + parsed_args.getArgStringValue("-h");
			server_URL_explicitly_specified = true;
		}
			//server_hostname = parsed_args.getArgStringValue("-h");
		if(parsed_args.isArgPresent("-u"))
		{
			server_URL = parsed_args.getArgStringValue("-u");
			server_URL_explicitly_specified = true;
			//const std::string URL = parsed_args.getArgStringValue("-u");
			//try
			//{
			//	URLParseResults parse_res = URLParser::parseURL(URL);

			//	server_hostname = parse_res.hostname;
			//	server_userpath = parse_res.userpath;
			//}
			//catch(glare::Exception& e) // Handle URL parse failure
			//{
			//	QMessageBox msgBox;
			//	msgBox.setText(QtUtils::toQString(e.what()));
			//	msgBox.exec();
			//	return 1;
			//}
		}
		else if(parsed_args.isArgPresent("-linku"))
		{
#if defined(_WIN32)
			// If we already have a Substrata application open on this computer, we want to tell that one to go to the URL, instead of opening another Substrata.
			// Search for an already existing Window called "Substrata vx.y"
			// If it exists, send a Windows message to that process, telling it to open the URL, and return from this process.
			// TODO: work out how to do this on Mac and Linux, if it's needed.
			const std::string target_window_title = computeWindowTitle();
			const HWND target_hwnd = ::FindWindowA(NULL, target_window_title.c_str());
			if(target_hwnd != 0)
			{
				const std::string msg_body = "openSubURL:" + parsed_args.getArgStringValue("-linku");

				COPYDATASTRUCT copy_data;
				copy_data.dwData = 0;
				copy_data.cbData = (DWORD)msg_body.size(); // The size, in bytes, of the data pointed to by the lpData member.
				copy_data.lpData = (void*)msg_body.data(); // The data to be passed to the receiving application

				SendMessage(target_hwnd,
					WM_COPYDATA,
					NULL,
					(LPARAM)&copy_data
				);
				return 0;
			}
			else
			{
				server_URL = parsed_args.getArgStringValue("-linku");
				server_URL_explicitly_specified = true;
			}
#else
			server_URL = parsed_args.getArgStringValue("-linku");
			server_URL_explicitly_specified = true;
#endif
		}

		if(!open_even_filter->url.empty())
		{
			// If we have received a url from a file-open event on Mac:
			server_URL = open_even_filter->url; // Use it
			server_URL_explicitly_specified = true;
		}


		int app_exec_res;
		{ // Scope of MainWindow mw and textureserver.

			// Since we will be inserted textures based on URLs, the textures should be different if they have different paths, so we don't need to do path canonicalisation.
			TextureServer texture_server(/*use_canonical_path_keys=*/false);

			MainWindow mw(cyberspace_base_dir_path, appdata_path, parsed_args); // Creates GLWidget

			open_even_filter->main_window = &mw;

			mw.texture_server = &texture_server;
			mw.ui->glWidget->texture_server_ptr = &texture_server; // Set texture server pointer before GlWidget::initializeGL() gets called, as it passes texture server pointer to the openglengine.

			if(parsed_args.isArgPresent("--screenshotslave"))
				mw.run_as_screenshot_slave = true;

			if(parsed_args.isArgPresent("--testscreenshot"))
				mw.test_screenshot_taking = true;

			mw.initialise();

			mw.show();

			mw.raise();

			if(!mw.ui->glWidget->opengl_engine->initSucceeded())
			{
				const std::string msg = "OpenGL engine initialisation failed: " + mw.ui->glWidget->opengl_engine->getInitialisationErrorMsg();
				
				mw.logMessage(msg);
				
				QtUtils::showErrorMessageDialog(msg, &mw);
				return 1;
			}
			mw.opengl_engine = mw.ui->glWidget->opengl_engine;

			mw.cam_controller.setPosition(Vec3d(0,0,4.7));
			mw.ui->glWidget->setCameraController(&mw.cam_controller);
			mw.ui->glWidget->setPlayerPhysics(&mw.player_physics);
			mw.cam_controller.setMoveScale(0.3f);

			PhysicsWorld::init(); // init Jolt stuff

			const float sun_phi = 1.f;
			const float sun_theta = Maths::pi<float>() / 4;
			mw.ui->glWidget->opengl_engine->setEnvMapTransform(Matrix3f::rotationAroundZAxis(sun_phi));

			/*
			Set env material
			*/
			if(mw.ui->glWidget->opengl_engine->initSucceeded())
			{
				OpenGLMaterial env_mat;
				env_mat.tex_matrix = Matrix2f(-1 / Maths::get2Pi<float>(), 0, 0, 1 / Maths::pi<float>());

				mw.ui->glWidget->opengl_engine->setEnvMat(env_mat);
			}

			mw.ui->glWidget->opengl_engine->setSunDir(normalise(Vec4f(std::cos(sun_phi) * sin(sun_theta), std::sin(sun_phi) * sin(sun_theta), cos(sun_theta), 0)));


			// Make an arrow marking the axes at the origin
#if BUILD_TESTS
			const Vec4f arrow_origin(0, 0, 0.05f, 1);
			{
				GLObjectRef arrow = mw.ui->glWidget->opengl_engine->makeArrowObject(arrow_origin, arrow_origin + Vec4f(1, 0, 0, 0), Colour4f(0.6, 0.2, 0.2, 1.f), 1.f);
				mw.ui->glWidget->opengl_engine->addObject(arrow);
			}
			{
				GLObjectRef arrow = mw.ui->glWidget->opengl_engine->makeArrowObject(arrow_origin, arrow_origin + Vec4f(0, 1, 0, 0), Colour4f(0.2, 0.6, 0.2, 1.f), 1.f);
				mw.ui->glWidget->opengl_engine->addObject(arrow);
			}
			{
				GLObjectRef arrow = mw.ui->glWidget->opengl_engine->makeArrowObject(arrow_origin, arrow_origin + Vec4f(0, 0, 1, 0), Colour4f(0.2, 0.2, 0.6, 1.f), 1.f);
				mw.ui->glWidget->opengl_engine->addObject(arrow);
			}
#endif

			// For ob placement:
			mw.axis_arrow_objects[0] = mw.ui->glWidget->opengl_engine->makeArrowObject(Vec4f(0,0,0,1), Vec4f(1, 0, 0, 1), Colour4f(0.6, 0.2, 0.2, 1.f), 1.f);
			mw.axis_arrow_objects[1] = mw.ui->glWidget->opengl_engine->makeArrowObject(Vec4f(0,0,0,1), Vec4f(0, 1, 0, 1), Colour4f(0.2, 0.6, 0.2, 1.f), 1.f);
			mw.axis_arrow_objects[2] = mw.ui->glWidget->opengl_engine->makeArrowObject(Vec4f(0,0,0,1), Vec4f(0, 0, 1, 1), Colour4f(0.2, 0.2, 0.6, 1.f), 1.f);

			//mw.axis_arrow_objects[3] = mw.ui->glWidget->opengl_engine->makeArrowObject(arrow_origin, arrow_origin - Vec4f(1, 0, 0, 0), Colour4f(0.6, 0.2, 0.2, 1.f), 1.f);
			//mw.axis_arrow_objects[4] = mw.ui->glWidget->opengl_engine->makeArrowObject(arrow_origin, arrow_origin - Vec4f(0, 1, 0, 0), Colour4f(0.2, 0.6, 0.2, 1.f), 1.f);
			//mw.axis_arrow_objects[5] = mw.ui->glWidget->opengl_engine->makeArrowObject(arrow_origin, arrow_origin - Vec4f(0, 0, 1, 0), Colour4f(0.2, 0.2, 0.6, 1.f), 1.f);

			for(int i=0; i<3; ++i)
			{
				mw.axis_arrow_objects[i]->materials[0].albedo_linear_rgb = toLinearSRGB(axis_arrows_default_cols[i]);
				mw.axis_arrow_objects[i]->always_visible = true;
			}


			for(int i=0; i<3; ++i)
			{
				GLObjectRef ob = mw.ui->glWidget->opengl_engine->allocateObject();
				ob->ob_to_world_matrix = Matrix4f::translationMatrix((float)i * 3, 0, 2);
				ob->mesh_data = MeshBuilding::makeRotationArcHandleMeshData(*mw.ui->glWidget->opengl_engine->vert_buf_allocator, arc_handle_half_angle * 2);
				ob->materials.resize(1);
				ob->materials[0].albedo_linear_rgb = toLinearSRGB(axis_arrows_default_cols[i]);
				ob->always_visible = true;
				mw.rot_handle_arc_objects[i] = ob;
			}


			// Build ground plane graphics and physics data
			mw.ground_quad_mesh_opengl_data = MeshPrimitiveBuilding::makeQuadMesh(*mw.ui->glWidget->opengl_engine->vert_buf_allocator, Vec4f(1,0,0,0), Vec4f(0,1,0,0), /*res=*/16);
			mw.ground_quad_shape = PhysicsWorld::createGroundQuadShape(ground_quad_w);


			// If the user didn't explictly specify a URL (e.g. on the command line), and there is a valid start location URL setting, use it.
			if(!server_URL_explicitly_specified)
			{
				const std::string start_loc_URL_setting = QtUtils::toStdString(mw.settings->value(MainOptionsDialog::startLocationURLKey()).toString());
				if(!start_loc_URL_setting.empty())
					server_URL = start_loc_URL_setting;
			}

			mw.connectToServer(server_URL/*server_hostname, server_userpath*/);


			// Make hypercard physics mesh
			{
				Indigo::MeshRef mesh = new Indigo::Mesh();

				unsigned int v_start = 0;
				{
					mesh->addVertex(Indigo::Vec3f(0,0,0));
					mesh->addVertex(Indigo::Vec3f(0,0,1));
					mesh->addVertex(Indigo::Vec3f(0,1,1));
					mesh->addVertex(Indigo::Vec3f(0,1,0));
					const unsigned int vertex_indices[]   = {v_start + 0, v_start + 1, v_start + 2};
					mesh->addTriangle(vertex_indices, vertex_indices, 0);
					const unsigned int vertex_indices_2[] = {v_start + 0, v_start + 2, v_start + 3};
					mesh->addTriangle(vertex_indices_2, vertex_indices_2, 0);
					v_start += 4;
				}
				mesh->endOfModel();

				mw.hypercard_quad_shape = PhysicsWorld::createJoltShapeForIndigoMesh(*mesh, /*build_dynamic_physics_ob=*/false);
			}

			mw.hypercard_quad_opengl_mesh = MeshPrimitiveBuilding::makeQuadMesh(*mw.ui->glWidget->opengl_engine->vert_buf_allocator, Vec4f(1, 0, 0, 0), Vec4f(0, 0, 1, 0), /*vert_res=*/2);

			// Make spotlight meshes
			{
				MeshBuilding::MeshBuildingResults results = MeshBuilding::makeSpotlightMeshes(cyberspace_base_dir_path, *mw.ui->glWidget->opengl_engine->vert_buf_allocator);
				mw.spotlight_opengl_mesh = results.opengl_mesh_data;
				mw.spotlight_shape = results.physics_shape;
			}

			// Make image cube meshes
			{
				MeshBuilding::MeshBuildingResults results = MeshBuilding::makeImageCube(*mw.ui->glWidget->opengl_engine->vert_buf_allocator);
				mw.image_cube_opengl_mesh = results.opengl_mesh_data;
				mw.image_cube_shape = results.physics_shape;
			}

			// Make unit-cube raymesh (used for placeholder model)
			mw.unit_cube_shape = mw.image_cube_shape;

			// Make object-placement beam model
			{
				mw.ob_placement_beam = mw.ui->glWidget->opengl_engine->allocateObject();
				mw.ob_placement_beam->ob_to_world_matrix = Matrix4f::identity();
				mw.ob_placement_beam->mesh_data = mw.ui->glWidget->opengl_engine->getCylinderMesh();

				OpenGLMaterial material;
				material.albedo_linear_rgb = toLinearSRGB(Colour3f(0.3f, 0.8f, 0.3f));
				material.transparent = true;
				material.alpha = 0.9f;

				mw.ob_placement_beam->setSingleMaterial(material);

				// Make object-placement beam hit marker out of a sphere.
				mw.ob_placement_marker = mw.ui->glWidget->opengl_engine->allocateObject();
				mw.ob_placement_marker->ob_to_world_matrix = Matrix4f::identity();
				mw.ob_placement_marker->mesh_data = mw.ui->glWidget->opengl_engine->getSphereMeshData();

				mw.ob_placement_marker->setSingleMaterial(material);
			}

			{
				// Make ob_denied_move_marker
				mw.ob_denied_move_marker = mw.ui->glWidget->opengl_engine->allocateObject();
				mw.ob_denied_move_marker->ob_to_world_matrix = Matrix4f::identity();
				mw.ob_denied_move_marker->mesh_data = mw.ui->glWidget->opengl_engine->getSphereMeshData();

				OpenGLMaterial material;
				material.albedo_linear_rgb = toLinearSRGB(Colour3f(0.8f, 0.2f, 0.2f));
				material.transparent = true;
				material.alpha = 0.9f;

				mw.ob_denied_move_marker->setSingleMaterial(material);
			}

			// Make voxel_edit_marker model
			{
				mw.voxel_edit_marker = mw.ui->glWidget->opengl_engine->allocateObject();
				mw.voxel_edit_marker->ob_to_world_matrix = Matrix4f::identity();
				mw.voxel_edit_marker->mesh_data = mw.ui->glWidget->opengl_engine->getCubeMeshData();

				OpenGLMaterial material;
				material.albedo_linear_rgb = toLinearSRGB(Colour3f(0.3f, 0.8f, 0.3f));
				material.transparent = true;
				material.alpha = 0.3f;

				mw.voxel_edit_marker->setSingleMaterial(material);
			}

			// Make voxel_edit_face_marker model
			{
				mw.voxel_edit_face_marker = mw.ui->glWidget->opengl_engine->allocateObject();
				mw.voxel_edit_face_marker->ob_to_world_matrix = Matrix4f::identity();
				mw.voxel_edit_face_marker->mesh_data = mw.ui->glWidget->opengl_engine->getUnitQuadMeshData();

				OpenGLMaterial material;
				material.albedo_linear_rgb = toLinearSRGB(Colour3f(0.3f, 0.8f, 0.3f));
				mw.voxel_edit_face_marker->setSingleMaterial(material);
			}

			// Make shader for parcels
			{
				const std::string use_shader_dir = cyberspace_base_dir_path + "/data/shaders";

				const std::string version_directive    = mw.ui->glWidget->opengl_engine->getVersionDirective();
				const std::string preprocessor_defines = mw.ui->glWidget->opengl_engine->getPreprocessorDefines();
				
				mw.parcel_shader_prog = new OpenGLProgram(
					"parcel hologram prog",
					new OpenGLShader(use_shader_dir + "/parcel_vert_shader.glsl", version_directive, preprocessor_defines, GL_VERTEX_SHADER),
					new OpenGLShader(use_shader_dir + "/parcel_frag_shader.glsl", version_directive, preprocessor_defines, GL_FRAGMENT_SHADER),
					mw.ui->glWidget->opengl_engine->getAndIncrNextProgramIndex()
				);
				mw.ui->glWidget->opengl_engine->addProgram(mw.parcel_shader_prog);
				// Let any glare::Exception thrown fall through to below.
			}


			// TEMP: make an avatar for testing of animation retargeting etc.
			if(false)
			{
				const int N = 10;
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

					//const std::string path = "C:\\Users\\nick\\Downloads\\jokerwithchainPOV.vrm";
					//const std::string path = "D:\\models\\readyplayerme_nick_tpose.glb";
					const std::string path = "D:\\models\\generic dude avatar.glb";
					const uint64 model_hash = FileChecksum::fileChecksum(path);
					const std::string original_filename = FileUtils::getFilename(path);
					const std::string mesh_URL = ResourceManager::URLForNameAndExtensionAndHash(original_filename, ::getExtension(original_filename), model_hash);
					mw.resource_manager->copyLocalFileToResourceDir(path, mesh_URL);

					PhysicsShape physics_shape;
					BatchedMeshRef batched_mesh;
					Reference<OpenGLMeshRenderData> mesh_data = ModelLoading::makeGLMeshDataAndBatchedMeshForModelURL(mesh_URL, *mw.resource_manager,
						mw.ui->glWidget->opengl_engine->vert_buf_allocator.ptr(), false, /*build_dynamic_physics_ob=*/false, physics_shape, batched_mesh);

					test_avatar->graphics.skinned_gl_ob = ModelLoading::makeGLObjectForMeshDataAndMaterials(*mw.ui->glWidget->opengl_engine, mesh_data, /*ob_lod_level=*/0, 
						test_avatar->avatar_settings.materials, /*lightmap_url=*/std::string(), *mw.resource_manager, ob_to_world_matrix);

				
					// Load animation data
					{
						FileInStream file(cyberspace_base_dir_path + "/resources/extracted_avatar_anim.bin");
						test_avatar->graphics.skinned_gl_ob->mesh_data->animation_data.loadAndRetargetAnim(file);
					}

					test_avatar->graphics.build();

					for(size_t z=0; z<test_avatar->graphics.skinned_gl_ob->materials.size(); ++z)
						test_avatar->graphics.skinned_gl_ob->materials[z].alpha = 0.5f;
					mw.ui->glWidget->opengl_engine->objectMaterialsUpdated(*test_avatar->graphics.skinned_gl_ob);

					for(size_t i=0; i<test_avatar->graphics.skinned_gl_ob->mesh_data->animation_data.nodes.size(); ++i)
					{
						conPrint("node " + toString(i) + ": " + test_avatar->graphics.skinned_gl_ob->mesh_data->animation_data.nodes[i].name);
					}

					assignedLoadedOpenGLTexturesToMats(test_avatar.ptr(), *mw.ui->glWidget->opengl_engine, *mw.resource_manager);

					mw.ui->glWidget->opengl_engine->addObject(test_avatar->graphics.skinned_gl_ob);
				}
			}

			// TEMP: Load a GLB object directly into the graphics engine for testing.
			if(false)
			{
				ModelLoading::MakeGLObjectResults results;
				ModelLoading::makeGLObjectForModelFile(*mw.ui->glWidget->opengl_engine, *mw.ui->glWidget->opengl_engine->vert_buf_allocator, 
					//"D:\\models\\readyplayerme_avatar_animation_18.glb", 
					"D:\\models\\BMWCONCEPTBIKE\\bike no armature.glb",
					//"N:\\glare-core\\trunk\\testfiles\\gltf\\BoxAnimated.glb", 
					results);

				results.gl_ob->ob_to_world_matrix = Matrix4f::rotationMatrix(Vec4f(1,0,0,0), Maths::pi_2<float>()) * Matrix4f::uniformScaleMatrix(1);
				mw.ui->glWidget->opengl_engine->addObject(results.gl_ob);
			}



			try
			{
				// Copy default avatar into resource dir
				{
					const std::string mesh_URL = "xbot_glb_10972822012543217816.glb";

					if(!mw.resource_manager->isFileForURLPresent(mesh_URL))
					{
						mw.resource_manager->copyLocalFileToResourceDir(cyberspace_base_dir_path + "/resources/xbot.glb", mesh_URL);
					}
				}
			}
			catch(glare::Exception& e)
			{
				conPrint(e.what());
				QMessageBox msgBox;
				msgBox.setText(QtUtils::toQString(e.what()));
				msgBox.exec();
				return 1;
			}

			mw.afterGLInitInitialise();

			app_exec_res = app.exec();

			open_even_filter->main_window = NULL;
		} // End scope of MainWindow mw and textureserver.

#if defined(_WIN32)
		WMFVideoReader::shutdownWMF();
#endif
		OpenSSL::shutdown();
		Winter::VirtualMachine::shutdown();
		Networking::shutdown();
#if defined(_WIN32)
		if(com_init_success) WMFVideoReader::shutdownCOM();
#endif

		return app_exec_res;
	}
	catch(Indigo::IndigoException& e)
	{
		// Show error
		conPrint(toStdString(e.what()));
		QErrorMessage m;
		m.showMessage(QtUtils::toQString(e.what()));
		m.exec();
		return 1;
	}
	catch(glare::Exception& e)
	{
		// Show error
		conPrint(e.what());
		QErrorMessage m;
		m.showMessage(QtUtils::toQString(e.what()));
		m.exec();
		return 1;
	}
}


#endif // End #ifndef FUZZING
