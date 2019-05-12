/*=====================================================================
MainWindow.cpp
--------------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif


#ifdef _MSC_VER // Qt headers suppress some warnings on Windows, make sure the warning suppression doesn't propagate to our code. See https://bugreports.qt.io/browse/QTBUG-26877
#pragma warning(push, 0) // Disable warnings
#endif
#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "UserDetailsWidget.h"
#include "AvatarSettingsDialog.h"
#include "AddObjectDialog.h"
#include "ModelLoading.h"
#include "UploadResourceThread.h"
#include "DownloadResourcesThread.h"
#include "NetDownloadResourcesThread.h"
#include "AvatarGraphics.h"
#include "GuiClientApplication.h"
#include "WinterShaderEvaluator.h"
#include "LoginDialog.h"
#include "SignUpDialog.h"
#include "ResetPasswordDialog.h"
#include "ChangePasswordDialog.h"
#include "URLWidget.h"
#include "../shared/Protocol.h"
#include "../shared/Version.h"
#include <QtCore/QProcess>
#include <QtCore/QMimeData>
#include <QtCore/QSettings>
#include <QtWidgets/QApplication>
#include <QtGui/QMouseEvent>
#include <QtGui/QClipboard>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QGraphicsTextItem>
#include <QtWidgets/QMessageBox>
#include <QtGui/QImageWriter>
#include <QtWidgets/QErrorMessage>
#include <QtWidgets/QSplashScreen>
#include <QtWidgets/QShortcut>
#include "../qt/QtUtils.h"
#ifdef _MSC_VER
#pragma warning(pop) // Re-enable warnings
#endif
#include "../utils/Clock.h"
#include "../utils/Timer.h"
#include "../utils/PlatformUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/Exception.h"
#include "../utils/CameraController.h"
#include "../utils/TaskManager.h"
#include "../utils/SocketBufferOutStream.h"
#include "../utils/StringUtils.h"
#include "../utils/FileUtils.h"
#include "../utils/FileChecksum.h"
#include "../utils/Parser.h"
#include "../utils/ContainerUtils.h"
#include "../utils/JSONParser.h"
#include "../utils/Base64.h"
#include "../networking/networking.h"
#include "../networking/SMTPClient.h" // Just for testing
#include "../networking/TLSSocket.h" // Just for testing
#include "../networking/HTTPClient.h" // Just for testing
#include "../networking/url.h" // Just for testing

#include "../simpleraytracer/ray.h"
#include "../graphics/formatdecoderobj.h"
#include "../graphics/ImageMap.h"
//#include "../opengl/EnvMapProcessing.h"
#include "../dll/include/IndigoMesh.h"
#include "../dll/IndigoStringUtils.h"
#include "../indigo/TextureServer.h"
#include "../indigo/ThreadContext.h"
#include "../opengl/OpenGLShader.h"
#include <clocale>
#include <zlib.h>

#include "../physics/TreeTest.h" // Just for testing
#include "../utils/VectorUnitTests.h" // Just for testing
#include "../utils/ReferenceTest.h" // Just for testing
#include "../utils/JSONParser.h" // Just for testing
#include "../opengl/OpenGLEngineTests.h" // Just for testing
#include "../graphics/FormatDecoderGLTF.h" // Just for testing


#if defined(_WIN32) || defined(_WIN64)
#else
#include <signal.h>
#endif


// If we are building on Windows, and we are not in Release mode (e.g. BUILD_TESTS is enabled), then make sure the console window is shown.
#if defined(_WIN32) && defined(BUILD_TESTS)
#pragma comment(linker, "/SUBSYSTEM:CONSOLE")
#endif


const int server_port = 7600;


static const double ground_quad_w = 1000.f;

AvatarGraphicsRef test_avatar;


const Colour4f DEFAULT_OUTLINE_COLOUR   = Colour4f::fromHTMLHexString("0ff7fb"); // light blue
const Colour4f PICKED_UP_OUTLINE_COLOUR = Colour4f::fromHTMLHexString("69fa2d"); // light green


MainWindow::MainWindow(const std::string& base_dir_path_, const std::string& appdata_path_, const ArgumentParser& args, QWidget *parent)
:	base_dir_path(base_dir_path_),
	appdata_path(appdata_path_),
	parsed_args(args), 
	QMainWindow(parent),
	connection_state(ServerConnectionState_NotConnected),
	logged_in_user_id(UserID::invalidUserID()),
	shown_object_modification_error_msg(false),
	need_help_info_dock_widget_position(false),
	total_num_res_to_download(0),
	num_frames(0),
	voxel_edit_marker_in_engine(false),
	voxel_edit_face_marker_in_engine(false),
	selected_ob_picked_up(false)
{
	ui = new Ui::MainWindow();
	ui->setupUi(this);

	ui->glWidget->setBaseDir(base_dir_path);

	// Add a space to right-align the UserDetailsWidget (see http://www.setnode.com/blog/right-aligning-a-button-in-a-qtoolbar/)
	QWidget* spacer = new QWidget();
	spacer->setMinimumWidth(200);
	spacer->setMaximumWidth(200);
	//spacer->setGeometry(QRect()
	//spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	ui->toolBar->addWidget(spacer);

	url_widget = new URLWidget(this);
	ui->toolBar->addWidget(url_widget);

	user_details = new UserDetailsWidget(this);
	ui->toolBar->addWidget(user_details);

	
	
	// Open Log File
	const std::string logfile_path = FileUtils::join(this->appdata_path, "log.txt");
	this->logfile.open(StringUtils::UTF8ToPlatformUnicodeEncoding(logfile_path).c_str(), std::ios_base::out);
	if(!logfile.good())
		conPrint("WARNING: Failed to open log file at '" + logfile_path + "' for writing.");
	logfile << "============================= Cyberspace Started =============================" << std::endl;
	logfile << Clock::getAsciiTime() << std::endl;



	settings = new QSettings("Glare Technologies", "Cyberspace");

	// Restore main window geometry and state
	this->restoreGeometry(settings->value("mainwindow/geometry").toByteArray());
	this->restoreState(settings->value("mainwindow/windowState").toByteArray());

	connect(ui->chatPushButton, SIGNAL(clicked()), this, SLOT(sendChatMessageSlot()));
	connect(ui->chatMessageLineEdit, SIGNAL(returnPressed()), this, SLOT(sendChatMessageSlot()));
	connect(ui->glWidget, SIGNAL(mouseClicked(QMouseEvent*)), this, SLOT(glWidgetMouseClicked(QMouseEvent*)));
	connect(ui->glWidget, SIGNAL(mouseDoubleClickedSignal(QMouseEvent*)), this, SLOT(glWidgetMouseDoubleClicked(QMouseEvent*)));
	connect(ui->glWidget, SIGNAL(mouseMoved(QMouseEvent*)), this, SLOT(glWidgetMouseMoved(QMouseEvent*)));
	connect(ui->glWidget, SIGNAL(keyPressed(QKeyEvent*)), this, SLOT(glWidgetKeyPressed(QKeyEvent*)));
	connect(ui->glWidget, SIGNAL(keyReleased(QKeyEvent*)), this, SLOT(glWidgetkeyReleased(QKeyEvent*)));
	connect(ui->glWidget, SIGNAL(mouseWheelSignal(QWheelEvent*)), this, SLOT(glWidgetMouseWheelEvent(QWheelEvent*)));
	connect(ui->objectEditor, SIGNAL(objectChanged()), this, SLOT(objectEditedSlot()));
	connect(user_details, SIGNAL(logInClicked()), this, SLOT(on_actionLogIn_triggered()));
	connect(user_details, SIGNAL(logOutClicked()), this, SLOT(on_actionLogOut_triggered()));
	connect(user_details, SIGNAL(signUpClicked()), this, SLOT(on_actionSignUp_triggered()));
	connect(url_widget, SIGNAL(URLChanged()), this, SLOT(URLChangedSlot()));

	this->resources_dir = appdata_path + "/resources";
	//this->resources_dir = appdata_path + "/resources_" + toString(PlatformUtils::getProcessID());
	FileUtils::createDirIfDoesNotExist(this->resources_dir);

	print("resources_dir: " + resources_dir);
	resource_manager = new ResourceManager(this->resources_dir);

	cam_controller.setMouseSensitivity(-1.0);
}


void MainWindow::initialise()
{
	setWindowTitle(QtUtils::toQString("Substrata v" + ::cyberspace_version));

	ui->materialBrowserDockWidgetContents->init(this, this->base_dir_path, this->appdata_path, this->texture_server);
	connect(ui->materialBrowserDockWidgetContents, SIGNAL(materialSelected(const std::string&)), this, SLOT(materialSelectedInBrowser(const std::string&)));

	ui->objectEditor->setControlsEnabled(false);
	ui->parcelEditor->hide();

	// Set to 17ms due to this issue on Mac OS: https://bugreports.qt.io/browse/QTBUG-60346
	startTimer(17);

	ui->infoDockWidget->setTitleBarWidget(new QWidget());
	ui->infoDockWidget->hide();

	// Update help text
	this->ui->helpInfoLabel->setText("Use the W/A/S/D keys and arrow keys to move and look around.\n"
		"Click and drag the mouse on the 3D view to look around.\n"
		"Space key: jump\n"
		"Double-click an object to select it."
	);
	this->ui->helpInfoDockWidget->show();

	if(!settings->contains("mainwindow/geometry"))
		need_help_info_dock_widget_position = true;
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
}


MainWindow::~MainWindow()
{
	// Kill ClientThread
	print("killing ClientThread");
	this->client_thread->killConnection();
	this->client_thread = NULL;
	this->client_thread_manager.killThreadsBlocking();
	print("killed ClientThread");

	ui->glWidget->makeCurrent();

	// Kill various threads before we start destroying members of MainWindow they may depend on.
	resource_upload_thread_manager.killThreadsBlocking();
	resource_download_thread_manager.killThreadsBlocking();
	net_resource_download_thread_manager.killThreadsBlocking();

	delete ui;
}


void MainWindow::closeEvent(QCloseEvent* event)
{
	// Save main window geometry and state.  See http://doc.qt.io/archives/qt-4.8/qmainwindow.html#saveState
	settings->setValue("mainwindow/geometry", saveGeometry());
	settings->setValue("mainwindow/windowState", saveState());

	QMainWindow::closeEvent(event);
}


static Reference<PhysicsObject> makePhysicsObject(Indigo::MeshRef mesh, const Matrix4f& ob_to_world_matrix, StandardPrintOutput& print_output, Indigo::TaskManager& task_manager)
{
	Reference<PhysicsObject> phy_ob = new PhysicsObject(/*collidable=*/true);
	phy_ob->geometry = new RayMesh("mesh", false);
	phy_ob->geometry->fromIndigoMesh(*mesh);
				
	phy_ob->geometry->buildTrisFromQuads();
	Geometry::BuildOptions options;
	phy_ob->geometry->build(options, print_output, false, task_manager);

	phy_ob->ob_to_world = ob_to_world_matrix;
	return phy_ob;
}


//static Reference<GLObject> loadAvatarModel(const std::string& model_url)
//{
//	// TEMP HACK: Just load a teapot for now :)
//
//	Indigo::MeshRef mesh = new Indigo::Mesh();
//	MLTLibMaterials mats;
//	FormatDecoderObj::streamModel("teapot.obj", *mesh, 1.f, true, mats);
//
//	GLObjectRef ob = new GLObject();
//	ob->materials.resize(1);
//	ob->materials[0].albedo_rgb = Colour3f(0.3f, 0.5f, 0.4f);
//	ob->materials[0].fresnel_scale = 1;
//
//	ob->ob_to_world_matrix.setToTranslationMatrix(0, 0, 0);
//	ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh, false);
//	return ob;
//}


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


static const Matrix4f obToWorldMatrix(const WorldObjectRef& ob)
{
	const Vec4f pos((float)ob->pos.x, (float)ob->pos.y, (float)ob->pos.z, 1.f);

	return Matrix4f::translationMatrix(pos + ob->translation) *
		Matrix4f::rotationMatrix(normalise(ob->axis.toVec4fVector()), ob->angle) *
		Matrix4f::scaleMatrix(ob->scale.x, ob->scale.y, ob->scale.z);
}


// TODO: check/test
static const Matrix4f worldToObMatrix(const WorldObjectRef& ob)
{
	const Vec4f pos((float)ob->pos.x, (float)ob->pos.y, (float)ob->pos.z, 1.f);

	return Matrix4f::scaleMatrix(1/ob->scale.x, 1/ob->scale.y, 1/ob->scale.z) *
		Matrix4f::rotationMatrix(normalise(ob->axis.toVec4fVector()), -ob->angle) *
		Matrix4f::translationMatrix(-pos - ob->translation);
}


void MainWindow::startDownloadingResource(const std::string& url)
{
	conPrint("-------------------MainWindow::startDownloadingResource()-------------------\nURL: " + url);

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
			this->net_resource_download_thread_manager.enqueueMessage(new DownloadResourceMessage(url));
		else
			this->resource_download_thread_manager.enqueueMessage(new DownloadResourceMessage(url));
	}
	catch(Indigo::Exception& e)
	{
		conPrint("Failed to parse URL '" + url + "': " + e.what());
	}
}


// Check if the model file and any material dependencies (textures etc..) are downloaded.
// If so load the model into the OpenGL and physics engines.
// If not, set a placeholder model and queue up the downloads.
void MainWindow::loadModelForObject(WorldObject* ob, bool start_downloading_missing_files)
{
	print("Loading model for ob: UID: " + ob->uid.toString() + ", model URL: " + ob->model_url);
	Timer timer;
	ob->loaded_model_url = ob->model_url;

	ui->glWidget->makeCurrent();

	try
	{
		// Sanity check position, axis, angle
		if(!::isFinite(ob->pos.x) || !::isFinite(ob->pos.y) || !::isFinite(ob->pos.z))
			throw Indigo::Exception("Position had non-finite component.");
		if(!::isFinite(ob->axis.x) || !::isFinite(ob->axis.y) || !::isFinite(ob->axis.z))
			throw Indigo::Exception("axis had non-finite component.");
		if(!::isFinite(ob->angle))
			throw Indigo::Exception("angle was non-finite.");


		// See if we have the files downloaded
		std::set<std::string> dependency_URLs;
		ob->getDependencyURLSet(dependency_URLs);

		// Do we have all the objects downloaded?
		bool all_downloaded = true;
		for(auto it = dependency_URLs.begin(); it != dependency_URLs.end(); ++it)
		{
			const std::string& url = *it;
			if(resource_manager->isValidURL(url))
			{
				if(!resource_manager->isFileForURLPresent(url))
				{
					all_downloaded = false;
					if(start_downloading_missing_files)
						startDownloadingResource(url);
				}
			}
			else
				all_downloaded = false;
		}

		const Matrix4f ob_to_world_matrix = obToWorldMatrix(ob);

		// Sanity check ob_to_world_matrix matrix
		for(int i=0; i<16; ++i)
			if(!::isFinite(ob_to_world_matrix.e[i]))
				throw Indigo::Exception("ob_to_world_matrix had non-finite component.");

		Matrix4f world_to_ob;
		const bool ob_to_world_invertible = ob_to_world_matrix.getInverseForAffine3Matrix(world_to_ob);
		if(!ob_to_world_invertible)
			throw Indigo::Exception("ob_to_world_matrix was not invertible."); // TEMP: do we actually need this restriction?

		// Check world_to_ob matrix
		for(int i=0; i<16; ++i)
			if(!::isFinite(world_to_ob.e[i]))
				throw Indigo::Exception("world_to_ob had non-finite component.");


		if(!all_downloaded)
		{
			if(!ob->using_placeholder_model)
			{
				// Remove any existing OpenGL and physics model
				if(ob->opengl_engine_ob.nonNull())
					ui->glWidget->removeObject(ob->opengl_engine_ob);

				if(ob->physics_object.nonNull())
					physics_world->removeObject(ob->physics_object);

				// Use a temporary placeholder cube model.
				const Matrix4f cube_ob_to_world_matrix = Matrix4f::translationMatrix(-0.5f, -0.5f, -0.5f) * ob_to_world_matrix;

				GLObjectRef cube_gl_ob = ui->glWidget->opengl_engine->makeAABBObject(Vec4f(0, 0, 0, 1), Vec4f(1, 1, 1, 1), Colour4f(0.6f, 0.2f, 0.2, 0.5f));
				cube_gl_ob->ob_to_world_matrix = cube_ob_to_world_matrix;
				ob->opengl_engine_ob = cube_gl_ob;
				ui->glWidget->addObject(cube_gl_ob);

				// Make physics object
				PhysicsObjectRef physics_ob = new PhysicsObject(/*collidable=*/true);
				physics_ob->geometry = this->unit_cube_raymesh;
				physics_ob->ob_to_world = cube_ob_to_world_matrix;

				ob->physics_object = physics_ob;
				physics_ob->userdata = ob;
				physics_ob->userdata_type = 0;
				physics_world->addObject(physics_ob);

				physics_world->rebuild(task_manager, print_output);

				ob->using_placeholder_model = true;
			}
		}
		else
		{
			print("\tAll resources present for object.  Adding Object to OpenGL Engine etc..");
			
			// Remove any existing OpenGL and physics model
			if(ob->opengl_engine_ob.nonNull())
				ui->glWidget->removeObject(ob->opengl_engine_ob);

			if(ob->physics_object.nonNull())
				physics_world->removeObject(ob->physics_object);

			ob->using_placeholder_model = false;

			
			GLObjectRef gl_ob;
			Reference<PhysicsObject> physics_ob;

			if(ob->object_type == WorldObject::ObjectType_Hypercard)
			{
				physics_ob = new PhysicsObject(/*collidable=*/true);
				physics_ob->geometry = this->hypercard_quad_raymesh;
				physics_ob->ob_to_world = ob_to_world_matrix;

				gl_ob = new GLObject();
				gl_ob->mesh_data = this->hypercard_quad_opengl_mesh;
				gl_ob->materials.resize(1);
				gl_ob->materials[0].albedo_rgb = Colour3f(0.85f);
				gl_ob->materials[0].albedo_texture = makeHypercardTexMap(ob->content);
				gl_ob->ob_to_world_matrix = ob_to_world_matrix;

				ob->loaded_content = ob->content;
			}
			else if(ob->object_type == WorldObject::ObjectType_VoxelGroup)
			{
				if(ob->voxel_group.voxels.size() == 0)
				{
					// Add dummy cube marker for zero-voxel case.
					physics_ob = new PhysicsObject(/*collidable=*/false);
					physics_ob->geometry = this->unit_cube_raymesh;
					physics_ob->ob_to_world = ob_to_world_matrix * Matrix4f::translationMatrix(-0.5f, -0.5f, -0.5f);

					gl_ob = new GLObject();
					gl_ob->mesh_data = ui->glWidget->opengl_engine->getCubeMeshData();
					gl_ob->materials.resize(1);
					gl_ob->materials[0].albedo_rgb = Colour3f(0.8);
					gl_ob->materials[0].albedo_tex_path = "resources/voxel_dummy_texture.png";
					gl_ob->materials[0].tex_matrix = Matrix2f(1, 0, 0, -1); // OpenGL expects texture data to have bottom left pixel at offset 0, we have top left pixel, so flip
					gl_ob->ob_to_world_matrix = ob_to_world_matrix * Matrix4f::translationMatrix(-0.5f, -0.5f, -0.5f);
				}
				else
				{
					Reference<RayMesh> raymesh;
					Reference<OpenGLMeshRenderData> gl_meshdata = ModelLoading::makeModelForVoxelGroup(ob->voxel_group, task_manager, raymesh);

					physics_ob = new PhysicsObject(/*collidable=*/true);
					physics_ob->geometry = raymesh;
					physics_ob->ob_to_world = ob_to_world_matrix;

					gl_ob = new GLObject();
					gl_ob->mesh_data = gl_meshdata;
					gl_ob->materials.resize(ob->materials.size());
					for(uint32 i=0; i<ob->materials.size(); ++i)
						ModelLoading::setGLMaterialFromWorldMaterial(*ob->materials[i], *this->resource_manager, gl_ob->materials[i]);
					gl_ob->ob_to_world_matrix = ob_to_world_matrix;
				}
			}
			else
			{
				// Make GL object, add to OpenGL engine
				Indigo::MeshRef mesh;
				Reference<RayMesh> raymesh;
				gl_ob = ModelLoading::makeGLObjectForModelURLAndMaterials(ob->model_url, ob->materials, *this->resource_manager, this->mesh_manager, this->task_manager, ob_to_world_matrix, mesh, raymesh);
				
				// Make physics object
				physics_ob = new PhysicsObject(/*collidable=*/true);
				physics_ob->geometry = raymesh;
				physics_ob->ob_to_world = ob_to_world_matrix;
			}

			ob->opengl_engine_ob = gl_ob;
			ui->glWidget->addObject(gl_ob);

			ob->physics_object = physics_ob;
			physics_ob->userdata = ob;
			physics_ob->userdata_type = 0;
			physics_world->addObject(physics_ob);
			physics_world->rebuild(task_manager, print_output);

			loadScriptForObject(ob); // Load any script for the object.
		}

		print("\tModel loaded. (Elapsed: " + timer.elapsedStringNSigFigs(4) + ")");
	}
	catch(Indigo::Exception& e)
	{
		print("Error while loading object with UID " + ob->uid.toString() + ", model_url='" + ob->model_url + "': " + e.what());
	}
	catch(FileUtils::FileUtilsExcep& e)
	{
		print("Error while loading object with UID " + ob->uid.toString() + ", model_url='" + ob->model_url + "': " + e.what());
	}
}


void MainWindow::loadScriptForObject(WorldObject* ob)
{
	try
	{
		if(!ob->script_url.empty())
		{
			if(ob->loaded_script_url == ob->script_url) // If we have already loaded this script, return.
				return;

			ob->loaded_script_url = ob->script_url;

			try
			{
				const std::string script_path = resource_manager->pathForURL(ob->script_url);
				const std::string script_content = FileUtils::readEntireFileTextMode(script_path);

				// Handle instancing command if present
				int count = 0;
				const std::vector<std::string> lines = StringUtils::splitIntoLines(script_content);
				for(size_t z=0; z<lines.size(); ++z)
				{
					if(::hasPrefix(lines[z], "#instancing"))
					{
						Parser parser(lines[z].data(), (unsigned int)lines[z].size());
						parser.parseString("#instancing");
						parser.parseWhiteSpace();
						if(!parser.parseInt(count))
							throw Indigo::Exception("Failed to parse count after #instancing.");
					}
				}

				ob->script_evaluator = new WinterShaderEvaluator(this->base_dir_path, script_content);

				if(count > 0) // If instancing was requested:
				{
					conPrint("Doing instancing with count " + toString(count));

					// Create a bunch of copies of this object
					for(size_t z=0; z<(size_t)count; ++z)
					{
						WorldObjectRef instance = new WorldObject();
						instance->instance_index = (int)z;
						instance->script_evaluator = ob->script_evaluator;
						instance->prototype_object = ob;

						instance->uid = UID::invalidUID();
						instance->object_type = ob->object_type;
						instance->model_url = ob->model_url;
						instance->materials = ob->materials;
						instance->script_url = ob->script_url;
						instance->content = ob->content;
						instance->target_url = ob->target_url;
						instance->pos = ob->pos;// +Vec3d((double)z, 0, 0); // TEMP HACK
						instance->axis = ob->axis;
						instance->angle = ob->angle;
						instance->scale = ob->scale;

						instance->state = WorldObject::State_Alive;

						// Make GL object
						instance->opengl_engine_ob = new GLObject();
						instance->opengl_engine_ob->ob_to_world_matrix = ob->opengl_engine_ob->ob_to_world_matrix;
						instance->opengl_engine_ob->mesh_data = ob->opengl_engine_ob->mesh_data;
						instance->opengl_engine_ob->materials = ob->opengl_engine_ob->materials;

						// Add to 3d engine
						ui->glWidget->addObject(instance->opengl_engine_ob);

						// Add to instances list
						this->world_state->instances.insert(instance);
					}
				}
			}
			catch(Indigo::Exception& e)
			{
				// If this user created this model, show the error message.
				if(ob->creator_id == this->logged_in_user_id)
				{
					showErrorNotification("Error while loading script '" + ob->script_url + "': " + e.what());
				}

				throw Indigo::Exception("Error while loading script '" + ob->script_url + "': " + e.what());
			}
		}
	}
	catch(Indigo::Exception& e)
	{
		print("Error while loading object with UID " + ob->uid.toString() + ", model_url='" + ob->model_url + "': " + e.what());
	}
	catch(FileUtils::FileUtilsExcep& e)
	{
		print("Error while loading object with UID " + ob->uid.toString() + ", model_url='" + ob->model_url + "': " + e.what());
	}
}


void MainWindow::print(const std::string& message) // Print to log and console
{
	conPrint(message);
	this->logfile << message << std::endl;
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


void MainWindow::evalObjectScript(WorldObject* ob, double cur_time)
{
	CybWinterEnv winter_env;
	winter_env.instance_index = ob->instance_index;

	if(ob->script_evaluator->jitted_evalRotation)
	{
		const Vec4f rot = ob->script_evaluator->evalRotation((float)cur_time, winter_env);
		ob->angle = rot.length();
		if(ob->angle > 0)
			ob->axis = Vec3f(normalise(rot));
		else
			ob->axis = Vec3f(1, 0, 0);
	}

	if(ob->script_evaluator->jitted_evalTranslation)
	{
		if(ob->prototype_object.nonNull())
			ob->pos = ob->prototype_object->pos;
		ob->translation = ob->script_evaluator->evalTranslation((float)cur_time, winter_env);
	}

	// Update transform in 3d engine
	ob->opengl_engine_ob->ob_to_world_matrix = obToWorldMatrix(ob);

	ui->glWidget->opengl_engine->updateObjectTransformData(*ob->opengl_engine_ob);

	// Update in physics engine
	if(ob->physics_object.nonNull())
	{
		ob->physics_object->ob_to_world = ob->opengl_engine_ob->ob_to_world_matrix;
		physics_world->updateObjectTransformData(*ob->physics_object);

		// TODO: Update physics world accel structure?
	}
}


void MainWindow::updateOnlineUsersList() // Works off world state avatars.
{
	std::vector<std::string> names;
	{
		Lock lock(world_state->mutex);
		for(auto it = world_state->avatars.begin(); it != world_state->avatars.end(); ++it)
			names.push_back(it->second->name);
	}

	std::sort(names.begin(), names.end());

	// Combine names into a single string, while escaping any HTML chars.
	QString s;
	for(size_t i=0; i<names.size(); ++i)
		s += QtUtils::toQString(names[i]).toHtmlEscaped() + ((i + 1 < names.size()) ? "<br/>" : "");

	ui->onlineUsersTextEdit->setHtml(s);
}


bool MainWindow::objectModificationAllowed(const WorldObject& ob)
{
	if(!this->logged_in_user_id.valid())
	{
		return false;
	}
	else
	{
		return (this->logged_in_user_id == ob.creator_id) || isGodUser(this->logged_in_user_id);
	}
}


// Update object placement beam - a beam that goes from the object to what's below it.
void MainWindow::updateSelectedObjectPlacementBeam()
{
	// Update object placement beam - a beam that goes from the object to what's below it.
	if(selected_ob.nonNull())
	{
		GLObjectRef opengl_ob = this->selected_ob->opengl_engine_ob;
		const Matrix4f& to_world = opengl_ob->ob_to_world_matrix;

		const js::AABBox new_aabb_ws = ui->glWidget->opengl_engine->getAABBWSForObjectWithTransform(*opengl_ob, to_world);

		// We need to determine where to trace down from.  
		// To find this point, first trace up *just* against the selected object.
		RayTraceResult trace_results;
		Vec4f start_trace_pos = new_aabb_ws.centroid();
		start_trace_pos[2] = new_aabb_ws.min_[2] - 0.001f;
		this->selected_ob->physics_object->traceRay(Ray(start_trace_pos, Vec4f(0, 0, 1, 0), 0.f, 1.0e30f), 1.0e30f, thread_context, trace_results);
		const float up_beam_len = trace_results.hit_object ? trace_results.hitdist_ws : new_aabb_ws.axisLength(2) * 0.5f;

		// Now Trace ray downwards.  Start from just below where we got to in upwards trace.
		const Vec4f down_beam_startpos = start_trace_pos + Vec4f(0, 0, 1, 0) * (up_beam_len - 0.001f);
		this->physics_world->traceRay(down_beam_startpos, Vec4f(0, 0, -1, 0), thread_context, trace_results);
		const float down_beam_len = trace_results.hit_object ? trace_results.hitdist_ws : 1000.0f;
		const Vec4f lower_hit_normal = trace_results.hit_object ? normalise(trace_results.hit_normal_ws) : Vec4f(0, 0, 1, 0);

		const Vec4f down_beam_hitpos = down_beam_startpos + Vec4f(0, 0, -1, 0) * down_beam_len;

		Matrix4f scale_matrix = Matrix4f::scaleMatrix(/*radius=*/0.05f, /*radius=*/0.05f, down_beam_len);
		Matrix4f ob_to_world = Matrix4f::translationMatrix(down_beam_hitpos) * scale_matrix;
		ob_placement_beam->ob_to_world_matrix = ob_to_world;
		ui->glWidget->opengl_engine->updateObjectTransformData(*ob_placement_beam);

		// Place hit marker
		Matrix4f marker_scale_matrix = Matrix4f::scaleMatrix(0.2f, 0.2f, 0.01f);
		Matrix4f orientation; orientation.constructFromVector(lower_hit_normal);
		ob_placement_marker->ob_to_world_matrix = Matrix4f::translationMatrix(down_beam_hitpos) *
			orientation * marker_scale_matrix;
		ui->glWidget->opengl_engine->updateObjectTransformData(*ob_placement_marker);
	}
}


// Also shows error notifications if modification is not allowed.
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
		const bool logged_in_user_can_modify = (this->logged_in_user_id == ob.creator_id) || isGodUser(this->logged_in_user_id);
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


void MainWindow::timerEvent(QTimerEvent* event)
{
	const double cur_time = Clock::getTimeSinceInit();

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
	if(!this->url_widget->hasFocus())
		this->url_widget->setURL("cyb://" + server_hostname + 
			"?x=" + doubleToStringNDecimalPlaces(this->cam_controller.getPosition().x, 1) + 
			"&y=" + doubleToStringNDecimalPlaces(this->cam_controller.getPosition().y, 1) +
			"&z=" + doubleToStringNDecimalPlaces(this->cam_controller.getPosition().z, 1));

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



	const float dt = (float)time_since_last_timer_ev.elapsed();
	time_since_last_timer_ev.reset();

	num_frames++;
	if(fps_display_timer.elapsed() > 1.0)
	{
		//const float fps = num_frames / (float)fps_display_timer.elapsed();
		//conPrint("FPS: " + doubleToStringNSigFigs(fps, 4));
		num_frames = 0;
		fps_display_timer.reset();
	}
		

	
	// Handle any messages (chat messages etc..)
	{
		Lock msg_queue_lock(this->msg_queue.getMutex());
		while(!msg_queue.unlockedEmpty())
		{
			Reference<ThreadMessage> msg;
			this->msg_queue.unlockedDequeue(msg);

			if(dynamic_cast<const ClientConnectedToServerMessage*>(msg.getPointer()))
			{
				this->connection_state = ServerConnectionState_Connected;
				updateStatusBar();

				// Try and log in automatically if we have saved credentials.
				if(!settings->value("LoginDialog/username").toString().isEmpty() && !settings->value("LoginDialog/password").toString().isEmpty())
				{
					// Make LogInMessage packet and enqueue to send
					SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
					packet.writeUInt32(Protocol::LogInMessage);
					packet.writeStringLengthFirst(QtUtils::toStdString(settings->value("LoginDialog/username").toString()));
					packet.writeStringLengthFirst(LoginDialog::decryptPassword(QtUtils::toStdString(settings->value("LoginDialog/password").toString())));

					this->client_thread->enqueueDataToSend(packet);
				}
			}
			else if(dynamic_cast<const ClientConnectingToServerMessage*>(msg.getPointer()))
			{
				this->connection_state = ServerConnectionState_Connecting;
				updateStatusBar();
			}
			else if(dynamic_cast<const ClientDisconnectedFromServerMessage*>(msg.getPointer()))
			{
				const ClientDisconnectedFromServerMessage* m = static_cast<const ClientDisconnectedFromServerMessage*>(msg.getPointer());
				if(!m->error_message.empty())
				{
					showErrorNotification(m->error_message);
				}
				this->connection_state = ServerConnectionState_NotConnected;
				updateStatusBar();
			}
			else if(dynamic_cast<const ChatMessage*>(msg.getPointer()))
			{
				const ChatMessage* m = static_cast<const ChatMessage*>(msg.getPointer());
				ui->chatMessagesTextEdit->append(QtUtils::toQString(m->name + ": " + m->msg).toHtmlEscaped());
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
			else if(dynamic_cast<const LoggedInMessage*>(msg.getPointer()))
			{
				const LoggedInMessage* m = static_cast<const LoggedInMessage*>(msg.getPointer());
				//QMessageBox msgBox;
				//msgBox.setWindowTitle("Logged in");
				//msgBox.setText("Successfully logged in.");
				//msgBox.exec();

				user_details->setTextAsLoggedIn(m->username);
				this->logged_in_user_id = m->user_id;

				conPrint("Logged in as user with id " + toString(this->logged_in_user_id.value()));

				recolourParcelsForLoggedInState();


				// Send AvatarFullUpdate message, to change the nametag on our avatar.
				const Vec3d cam_angles = this->cam_controller.getAngles();
				Avatar avatar;
				avatar.uid = this->client_thread->client_avatar_uid;
				avatar.pos = Vec3d(this->cam_controller.getPosition());
				avatar.rotation = Vec3f(0, 0, (float)cam_angles.x);
				avatar.model_url = "";
				avatar.name = m->username;

				SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
				packet.writeUInt32(Protocol::AvatarFullUpdate);
				writeToNetworkStream(avatar, packet);

				this->client_thread->enqueueDataToSend(packet);
			}
			else if(dynamic_cast<const LoggedOutMessage*>(msg.getPointer()))
			{
				//QMessageBox msgBox;
				//msgBox.setWindowTitle("Logged out");
				//msgBox.setText("Successfully logged out.");
				//msgBox.exec();

				user_details->setTextAsNotLoggedIn();
				this->logged_in_user_id = UserID::invalidUserID();

				recolourParcelsForLoggedInState();

				// Send AvatarFullUpdate message, to change the nametag on our avatar.
				const Vec3d cam_angles = this->cam_controller.getAngles();
				Avatar avatar;
				avatar.uid = this->client_thread->client_avatar_uid;
				avatar.pos = Vec3d(this->cam_controller.getPosition());
				avatar.rotation = Vec3f(0, 0, (float)cam_angles.x);
				avatar.model_url = "";
				avatar.name = "Anonymous";

				SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
				packet.writeUInt32(Protocol::AvatarFullUpdate);
				writeToNetworkStream(avatar, packet);

				this->client_thread->enqueueDataToSend(packet);
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

				// Send AvatarFullUpdate message, to change the nametag on our avatar.
				const Vec3d cam_angles = this->cam_controller.getAngles();
				Avatar avatar;
				avatar.uid = this->client_thread->client_avatar_uid;
				avatar.pos = Vec3d(this->cam_controller.getPosition());
				avatar.rotation = Vec3f(0, 0, (float)cam_angles.x);
				avatar.model_url = "";
				avatar.name = m->username;

				SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
				packet.writeUInt32(Protocol::AvatarFullUpdate);
				writeToNetworkStream(avatar, packet);

				this->client_thread->enqueueDataToSend(packet);
			}
			else if(dynamic_cast<const UserSelectedObjectMessage*>(msg.getPointer()))
			{
				//print("MainWIndow: Received UserSelectedObjectMessage");
				const UserSelectedObjectMessage* m = static_cast<const UserSelectedObjectMessage*>(msg.getPointer());
				Lock lock(this->world_state->mutex);
				if(this->world_state->avatars.count(m->avatar_uid) != 0 && this->world_state->objects.count(m->object_uid) != 0)
				{
					this->world_state->avatars[m->avatar_uid]->selected_object_uid = m->object_uid;
				}
			}
			else if(dynamic_cast<const UserDeselectedObjectMessage*>(msg.getPointer()))
			{
				//print("MainWIndow: Received UserDeselectedObjectMessage");
				const UserDeselectedObjectMessage* m = static_cast<const UserDeselectedObjectMessage*>(msg.getPointer());
				Lock lock(this->world_state->mutex);
				if(this->world_state->avatars.count(m->avatar_uid) != 0)
				{
					this->world_state->avatars[m->avatar_uid]->selected_object_uid = UID::invalidUID();
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

						const std::string username = QtUtils::toStdString(settings->value("LoginDialog/username").toString());
						const std::string password = LoginDialog::decryptPassword(QtUtils::toStdString(settings->value("LoginDialog/password").toString()));

						resource_upload_thread_manager.addThread(new UploadResourceThread(&this->msg_queue, path, m->URL, server_hostname, server_port, username, password));
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

				conPrint("Got NewResourceOnServerMessage, URL: " + m->URL);

				if(ResourceManager::isValidURL(m->URL))
				{
					if(!resource_manager->isFileForURLPresent(m->URL)) // If we don't have this file yet:
					{
						conPrint("Do not have resource.");

						// Iterate over objects and see if they were using a placeholder model for this resource.
						Lock lock(this->world_state->mutex);
						bool need_resource = false;
						for(auto it = this->world_state->objects.begin(); it != this->world_state->objects.end(); ++it)
						{
							WorldObject* ob = it->second.getPointer();
							//if(ob->using_placeholder_model)
							{
								std::set<std::string> URL_set;
								ob->getDependencyURLSet(URL_set);
								need_resource = need_resource || (URL_set.count(m->URL) != 0);
							}
						}

						conPrint("need_resource: " + boolToString(need_resource));

						if(need_resource)
						{
							conPrint("Need resource, downloading: " + m->URL);
							this->resource_download_thread_manager.enqueueMessage(new DownloadResourceMessage(m->URL));
						}
					}
				}
			}
			else if(dynamic_cast<const ResourceDownloadingStatus*>(msg.getPointer()))
			{
				const ResourceDownloadingStatus* m = msg.downcastToPtr<const ResourceDownloadingStatus>();
				this->total_num_res_to_download = m->total_to_download;
				updateStatusBar();
			}
			else if(dynamic_cast<const ResourceDownloadedMessage*>(msg.getPointer()))
			{
				const ResourceDownloadedMessage* m = static_cast<const ResourceDownloadedMessage*>(msg.getPointer());

				conPrint("ResourceDownloadedMessage, URL: " + m->URL);

				// Since we have a new downloaded resource, iterate over objects and avatars and if they were using a placeholder model for this resource, load the proper model.
				try
				{
					Lock lock(this->world_state->mutex);

					for(auto it = this->world_state->objects.begin(); it != this->world_state->objects.end(); ++it)
					{
						WorldObject* ob = it->second.getPointer();

						//if(ob->using_placeholder_model)
						{
							std::set<std::string> URL_set;
							ob->getDependencyURLSet(URL_set);
							if(URL_set.count(m->URL)) // If the downloaded resource was used by this model:
							{
								loadModelForObject(ob, /*start_downloading_missing_files=*/false);
							}
						}
					}

					//for(auto it = this->world_state->avatars.begin(); it != this->world_state->avatars.end(); ++it)
					//{
					//	Avatar* avatar = it->second.getPointer();
					//	if(avatar->using_placeholder_model && (avatar->model_url == m->URL))
					//	{
					//		// Remove placeholder GL object
					//		assert(avatar->opengl_engine_ob.nonNull());
					//		ui->glWidget->opengl_engine->removeObject(avatar->opengl_engine_ob);

					//		conPrint("Adding Object to OpenGL Engine, UID " + toString(avatar->uid.value()));
					//		//const std::string path = resources_dir + "/" + ob->model_url;
					//		const std::string path = this->resource_manager->pathForURL(avatar->model_url);

					//		// Make GL object, add to OpenGL engine
					//		Indigo::MeshRef mesh;
					//		const Matrix4f ob_to_world_matrix = Matrix4f::translationMatrix((float)avatar->pos.x, (float)avatar->pos.y, (float)avatar->pos.z) * 
					//			Matrix4f::rotationMatrix(normalise(avatar->axis.toVec4fVector()), avatar->angle);
					//		GLObjectRef gl_ob = ModelLoading::makeGLObjectForModelFile(path, ob_to_world_matrix, mesh);
					//		avatar->opengl_engine_ob = gl_ob;
					//		ui->glWidget->addObject(gl_ob);

					//		avatar->using_placeholder_model = false;
					//	}
					//}
				}
				catch(Indigo::Exception& e)
				{
					print("Error while loading object: " + e.what());
				}
			}
		}
	}

	// Evaluate scripts on objects
	{
		Lock lock(this->world_state->mutex);

		for(auto it = this->world_state->objects.begin(); it != this->world_state->objects.end(); ++it)
		{
			WorldObject* ob = it->second.getPointer();
			if(ob->script_evaluator.nonNull())
				evalObjectScript(ob, cur_time);
		}

		// Evaluate scripts on instances
		for(auto it = this->world_state->instances.begin(); it != this->world_state->instances.end(); ++it)
		{
			WorldObject* ob = it->getPointer();
			if(ob->script_evaluator.nonNull())
				evalObjectScript(ob, cur_time);
		}
	}




	ui->glWidget->setCurrentTime((float)cur_time);
	ui->glWidget->playerPhyicsThink(dt);

	// Process player physics
	Vec4f campos = this->cam_controller.getPosition().toVec4fPoint();
	player_physics.update(*this->physics_world, dt, this->thread_context, campos);
	this->cam_controller.setPosition(toVec3d(campos));

	// Update avatar graphics
	try
	{
		Lock lock(this->world_state->mutex);

		for(auto it = this->world_state->avatars.begin(); it != this->world_state->avatars.end();)
		{
			Avatar* avatar = it->second.getPointer();
			if(avatar->uid == this->client_thread->client_avatar_uid) // Don't render our own Avatar
			{
				if(avatar->other_dirty) // If just created
				{
					updateOnlineUsersList(); // Update name list
					avatar->other_dirty = false;
				}

				it++;
				continue;
			}


			//if(avatar->other_dirty || avatar->transform_dirty)
			{
				if(avatar->state == Avatar::State_Dead)
				{
					print("Removing avatar.");
					// Remove any OpenGL object for it
					//if(avatar->opengl_engine_ob.nonNull())
					//	ui->glWidget->opengl_engine->removeObject(avatar->opengl_engine_ob);
					if(avatar->graphics.nonNull())
					{
						avatar->graphics->destroy(*ui->glWidget->opengl_engine);
						avatar->graphics = NULL;
					}

					// Remove nametag OpenGL object
					if(avatar->opengl_engine_nametag_ob.nonNull())
						ui->glWidget->opengl_engine->removeObject(avatar->opengl_engine_nametag_ob);

					// Remove avatar from avatar map
					auto old_avatar_iterator = it;
					it++;
					this->world_state->avatars.erase(old_avatar_iterator);

					updateOnlineUsersList();
				}
				else
				{
					bool reload_opengl_model = false; // load or reload model?
					if(avatar->graphics.isNull())
						reload_opengl_model = true;

					if(avatar->other_dirty)
						reload_opengl_model = true;

					if(reload_opengl_model) // If this is a new avatar that doesn't have an OpenGL model yet:
					{
						print("(Re)Loading avatar model. model URL: " + avatar->model_url + ", Avatar name: " + avatar->name);

						updateOnlineUsersList();

						// Remove any existing model and nametag
						//if(avatar->opengl_engine_ob.nonNull())
						//	ui->glWidget->removeObject(avatar->opengl_engine_ob);
						if(avatar->graphics.nonNull())
						{
							avatar->graphics->destroy(*ui->glWidget->opengl_engine);
							avatar->graphics = NULL;
						}
						if(avatar->opengl_engine_nametag_ob.nonNull()) // Remove nametag ob
							ui->glWidget->removeObject(avatar->opengl_engine_nametag_ob);

						//Vec3d pos;
						//Vec3f rotation;
						//avatar->getInterpolatedTransform(cur_time, pos, rotation);

						//const Matrix4f ob_to_world_matrix = rotateThenTranslateMatrix(pos, rotation);

						//std::vector<std::string> dependency_URLs;
						//avatar->appendDependencyURLs(dependency_URLs);

						//std::map<std::string, std::string> paths_for_URLs;

						// Do we have all the objects downloaded?
						//bool all_downloaded = true;
						//for(size_t i=0; i<dependency_URLs.size(); ++i)
						//{
						//	const std::string path = this->resource_manager->pathForURL(dependency_URLs[i]);
						//	if(!FileUtils::fileExists(path))
						//	{
						//		all_downloaded = false;
						//		// Enqueue download of resource.  TODO: check for dups
						//		this->resource_download_thread_manager.enqueueMessage(new DownloadResourceMessage(dependency_URLs[i]));
						//	}
						//
						//	paths_for_URLs[dependency_URLs[i]] = path;
						//}

						// See if we have the file downloaded
						//if(!all_downloaded)
						//{
						//	conPrint("Don't have avatar model '" + avatar->model_url + "' on disk, using placeholder.");
						//
						//	// Use a temporary placeholder model.
						//	GLObjectRef cube_gl_ob = ui->glWidget->opengl_engine->makeAABBObject(Vec4f(0, 0, 0, 1), Vec4f(1, 1, 1, 1), Colour4f(0.6f, 0.2f, 0.2, 0.5f));
						//	cube_gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(-0.5f, -0.5f, -0.5f) * ob_to_world_matrix;
						//	avatar->opengl_engine_ob = cube_gl_ob;
						//	ui->glWidget->addObject(cube_gl_ob);
						//
						//	avatar->using_placeholder_model = true;
						//}
						//else
						{
							print("Adding Avatar to OpenGL Engine, UID " + toString(avatar->uid.value()));

							// Make GL object, add to OpenGL engine
							//Indigo::MeshRef mesh;
							//GLObjectRef gl_ob = ModelLoading::makeGLObjectForModelFile(paths_for_URLs[avatar->model_url], ob_to_world_matrix, mesh);
							//avatar->opengl_engine_ob = gl_ob;
							//ui->glWidget->addObject(gl_ob);

							//avatar->using_placeholder_model = false;

							avatar->graphics = new AvatarGraphics();
							avatar->graphics->create(*ui->glWidget->opengl_engine);

							// No physics object for avatars.
						}

						// Add nametag object for avatar
						{
							avatar->opengl_engine_nametag_ob = makeNameTagGLObject(avatar->name);

							// Set transform to be above avatar.  This transform will be updated later.
							avatar->opengl_engine_nametag_ob->ob_to_world_matrix = Matrix4f::translationMatrix(avatar->pos.toVec4fVector());

							ui->glWidget->addObject(avatar->opengl_engine_nametag_ob); // Add to 3d engine
						}
					} // End if reload_opengl_model


					// Update transform if we have an avatar or placeholder OpenGL model.
					Vec3d pos;
					Vec3f rotation;
					avatar->getInterpolatedTransform(cur_time, pos, rotation);

					/*if(avatar->opengl_engine_ob.nonNull())
					{
						avatar->opengl_engine_ob->ob_to_world_matrix = rotateThenTranslateMatrix(pos, axis, angle);
						ui->glWidget->opengl_engine->updateObjectTransformData(*avatar->opengl_engine_ob);
					}*/
					if(avatar->graphics.nonNull())
					{
						avatar->graphics->setOverallTransform(*ui->glWidget->opengl_engine, pos, rotation, cur_time);
					}

					// Update nametag transform also
					if(avatar->opengl_engine_nametag_ob.nonNull())
					{
						// We want to rotate the nametag towards the camera.
						const Vec4f to_cam = normalise(pos.toVec4fPoint() - this->cam_controller.getPosition().toVec4fPoint());

						const Vec4f axis_k = Vec4f(0, 0, 1, 0);
						const Vec4f axis_j = normalise(removeComponentInDir(to_cam, axis_k));
						const Vec4f axis_i = crossProduct(axis_j, axis_k);
						const Matrix4f rot_matrix(axis_i, axis_j, axis_k, Vec4f(0, 0, 0, 1));

						// Tex width and height from makeNameTagGLObject():
						const int W = 256;
						const int H = 80;
						const float ws_width = 0.4f;
						const float ws_height = ws_width * H / W;

						// Rotate around z-axis, then translate to just above the avatar's head.
						avatar->opengl_engine_nametag_ob->ob_to_world_matrix = Matrix4f::translationMatrix(pos.toVec4fVector() + Vec4f(0, 0, 0.3f, 0)) *
							rot_matrix * Matrix4f::scaleMatrix(ws_width, 1, ws_height) * Matrix4f::translationMatrix(-0.5f, 0.f, 0.f);

						ui->glWidget->opengl_engine->updateObjectTransformData(*avatar->opengl_engine_nametag_ob); // Update transform in 3d engine
					}

					// Update selected object beam for the avatar, if it has an object selected
					if(avatar->selected_object_uid.valid())
					{
						if(avatar->graphics.nonNull())
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

								avatar->graphics->setSelectedObBeam(*ui->glWidget->opengl_engine, selected_pos);
							}
						}
					}
					else
					{
						if(avatar->graphics.nonNull())
							avatar->graphics->hideSelectedObBeam(*ui->glWidget->opengl_engine);
					}

					avatar->other_dirty = false;
					avatar->transform_dirty = false;

					++it;
				}
			} // end if avatar is dirty
			//else
			//	++it;
		} // end for each avatar
	}
	catch(Indigo::Exception& e)
	{
		print("Error while Updating avatar graphics: " + e.what());
	}


	//TEMP
	if(test_avatar.nonNull())
		test_avatar->setOverallTransform(*ui->glWidget->opengl_engine, Vec3d(0, 3, 1.67), Vec3f(0, 0, 0), cur_time);


	bool need_physics_world_rebuild = false;

	// Update world object graphics and physics models that have been marked as from-server-dirty based on incoming network messages from server.
	try
	{
		Lock lock(this->world_state->mutex);

		for(auto it = this->world_state->objects.begin(); it != this->world_state->objects.end();)
		{
			WorldObject* ob = it->second.getPointer();
			if(ob->from_remote_other_dirty || ob->from_remote_transform_dirty)
			{
				if(ob->state == WorldObject::State_Dead)
				{
					print("Removing WorldObject.");
				
					// Remove any OpenGL object for it
					if(ob->opengl_engine_ob.nonNull())
						ui->glWidget->opengl_engine->removeObject(ob->opengl_engine_ob);

					// Remove physics object
					if(ob->physics_object.nonNull())
					{
						physics_world->removeObject(ob->physics_object);
						need_physics_world_rebuild = true;
					}

					//// Remove object from object map
					auto old_object_iterator = it;
					it++;
					this->world_state->objects.erase(old_object_iterator);
				}
				else
				{
					bool reload_opengl_model = false; // load or reload model?
					if(ob->opengl_engine_ob.isNull())
						reload_opengl_model = true;

					if(ob->object_type == WorldObject::ObjectType_Generic)
					{
						if(ob->loaded_model_url != ob->model_url)
							reload_opengl_model = true;
					}
					else if(ob->object_type == WorldObject::ObjectType_VoxelGroup)
					{
						reload_opengl_model = ob->from_remote_other_dirty;
					}
					else if(ob->object_type == WorldObject::ObjectType_Hypercard)
					{
						reload_opengl_model = ob->loaded_content != ob->content;
					}

					if(reload_opengl_model)
					{
						loadModelForObject(ob, /*start_downloading_missing_files=*/true);

						ob->from_remote_other_dirty     = false;
						ob->from_remote_transform_dirty = false;
					}
					else // else if opengl ob is not null:
					{
						// Update transform for object in OpenGL engine
						if(ob != selected_ob.getPointer()) // Don't update the selected object based on network messages, we will consider the local transform for it authoritative.
						{
							if(ob->from_remote_other_dirty)
							{
								// TODO: handle model path change etc..

								// Update materials in opengl engine.
								GLObjectRef opengl_ob = ob->opengl_engine_ob;

								for(size_t i=0; i<ob->materials.size(); ++i)
									if(i < opengl_ob->materials.size())
									{
										ModelLoading::setGLMaterialFromWorldMaterial(*ob->materials[i], *this->resource_manager, opengl_ob->materials[i]);
									}

								ui->glWidget->opengl_engine->objectMaterialsUpdated(opengl_ob);
							}
							else
							{
								assert(ob->from_remote_transform_dirty);

								// Compute interpolated transformation
								Vec3d pos;
								Vec3f axis;
								float angle;
								ob->getInterpolatedTransform(cur_time, pos, axis, angle);

								ob->opengl_engine_ob->ob_to_world_matrix = Matrix4f::translationMatrix((float)pos.x, (float)pos.y, (float)pos.z) * 
									Matrix4f::rotationMatrix(normalise(axis.toVec4fVector()), angle) * 
									Matrix4f::scaleMatrix(ob->scale.x, ob->scale.y, ob->scale.z);

								ui->glWidget->opengl_engine->updateObjectTransformData(*ob->opengl_engine_ob);

								// Update in physics engine
								ob->physics_object->ob_to_world = ob->opengl_engine_ob->ob_to_world_matrix;
								physics_world->updateObjectTransformData(*ob->physics_object);
							}

							active_objects.insert(ob);
						}
						
						ob->from_remote_other_dirty     = false;
						ob->from_remote_transform_dirty = false;
					}

					++it;
				}
			}// end if(ob->from_server_dirty)
			/*else if(ob->from_local_dirty)
			{
				if(ob->state == WorldObject::State_JustCreated)
				{
					// Send object created message to server


					// Clear dirty flags
					ob->from_local_dirty = false;
				}

				++it;
			}*/
			else
				++it;
			
		}
	}
	catch(Indigo::Exception& e)
	{
		print("Error while Updating object graphics: " + e.what());
	}



	// Update parcel graphics and physics models that have been marked as from-server-dirty based on incoming network messages from server.
	try
	{
		Lock lock(this->world_state->mutex);

		for(auto it = this->world_state->parcels.begin(); it != this->world_state->parcels.end();)
		{
			Parcel* parcel = it->second.getPointer();
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
						need_physics_world_rebuild = true;
					}

					//// Remove object from object map
					auto old_parcel_iterator = it;
					it++;
					this->world_state->parcels.erase(old_parcel_iterator);
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
							parcel->opengl_engine_ob = parcel->makeOpenGLObject(ui->glWidget->opengl_engine, write_perms);
							parcel->opengl_engine_ob->materials[0].shader_prog = this->parcel_shader_prog;
							ui->glWidget->opengl_engine->addObject(parcel->opengl_engine_ob);

							// Make physics object for parcel:
							parcel->physics_object = parcel->makePhysicsObject(this->unit_cube_raymesh, task_manager);
							physics_world->addObject(parcel->physics_object);
							need_physics_world_rebuild = true;
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

					parcel->from_remote_dirty     = false;
					++it;
				}
			}// end if(parcel->from_remote_dirty)
			else
				++it;
		}
	}
	catch(Indigo::Exception& e)
	{
		print("Error while updating parcel graphics: " + e.what());
	}


	// Interpolate any active objects (Objects that have moved recently and so need interpolation done on them.)
	{
		Lock lock(this->world_state->mutex);
		for(auto it = active_objects.begin(); it != active_objects.end();)
		{
			WorldObjectRef ob = *it;

			if(cur_time - ob->snapshot_times[0]/*last_snapshot_time*/ > 1.0)
			{
				// Object is not active any more, remove from active_objects set.
				auto to_erase = it;
				it++;
				active_objects.erase(to_erase);
			}
			else
			{
				Vec3d pos;
				Vec3f axis;
				float angle;
				ob->getInterpolatedTransform(cur_time, pos, axis, angle);

				ob->opengl_engine_ob->ob_to_world_matrix = Matrix4f::translationMatrix((float)pos.x, (float)pos.y, (float)pos.z) * 
					Matrix4f::rotationMatrix(normalise(axis.toVec4fVector()), angle) * 
					Matrix4f::scaleMatrix(ob->scale.x, ob->scale.y, ob->scale.z);

				ui->glWidget->opengl_engine->updateObjectTransformData(*ob->opengl_engine_ob);

				// Update in physics engine
				ob->physics_object->ob_to_world = ob->opengl_engine_ob->ob_to_world_matrix;
				physics_world->updateObjectTransformData(*ob->physics_object);
				need_physics_world_rebuild = true;

				it++;
			}
		}
	}



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
			const Vec4f selection_vec_ws(right*selection_vec_cs[0] + forwards*selection_vec_cs[1] + up*selection_vec_cs[2]);

			// Get the target position for the new selection point in world space.
			const Vec4f new_sel_point_ws = origin + selection_vec_ws;

			// Get the current position for the selection point in world-space.
			const Vec4f selection_point_ws = obToWorldMatrix(this->selected_ob) * this->selection_point_os;

			const Vec4f desired_new_ob_pos = this->selected_ob->pos.toVec4fPoint() + (new_sel_point_ws - selection_point_ws);

			GLObjectRef opengl_ob = this->selected_ob->opengl_engine_ob;

			Matrix4f tentative_new_to_world = opengl_ob->ob_to_world_matrix;
			tentative_new_to_world.setColumn(3, desired_new_ob_pos);

			const js::AABBox tentative_new_aabb_ws = ui->glWidget->opengl_engine->getAABBWSForObjectWithTransform(*opengl_ob, tentative_new_to_world);

			// Check parcel permissions for this object
			bool ob_pos_in_parcel;
			const bool have_creation_perms = haveObjectWritePermissions(tentative_new_aabb_ws, ob_pos_in_parcel);
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
			const bool new_transform_valid = clampObjectPositionToParcelForNewTransform(opengl_ob, 
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
					GLObjectRef new_marker = new GLObject();
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
					Matrix4f marker_scale_matrix = Matrix4f::scaleMatrix(use_scale, use_scale, 0.01f);
					Matrix4f orientation; orientation.constructFromVector(edge_markers[i].normal);

					ob_denied_move_markers[i]->ob_to_world_matrix = Matrix4f::translationMatrix(edge_markers[i].pos) * 
							orientation * marker_scale_matrix;
				
					ui->glWidget->opengl_engine->updateObjectTransformData(*ob_denied_move_markers[i]);
				}
				//----------- End display edge markers -----------


				// Set world object pos
				this->selected_ob->setPosAndHistory(new_ob_pos);

				// Set graphics object pos and update in opengl engine.
				Matrix4f new_to_world = opengl_ob->ob_to_world_matrix;
				new_to_world.setColumn(3, new_ob_pos.toVec4fPoint());

				opengl_ob->ob_to_world_matrix = new_to_world;
				ui->glWidget->opengl_engine->updateObjectTransformData(*opengl_ob);

				// Update physics object
				this->selected_ob->physics_object->ob_to_world.setColumn(3, new_ob_pos.toVec4fPoint());
				this->physics_world->updateObjectTransformData(*this->selected_ob->physics_object);
				need_physics_world_rebuild = true;

				this->ui->objectEditor->updateObjectPos(*selected_ob);

				// Mark as from-local-dirty to send an object transform updated message to the server
				this->selected_ob->from_local_transform_dirty = true;

				updateSelectedObjectPlacementBeam();
			} 
			else // else if new transfrom not valid
			{
				showErrorNotification("New object position is not valid - You can only move objects in a parcel that you have write permissions for.");
			}
		}
	}

	updateVoxelEditMarkers();

	// Send an AvatarTransformUpdate packet to the server if needed.
	if(time_since_update_packet_sent.elapsed() > 0.1)
	{
		// Send AvatarTransformUpdate packet
		{
			/*Vec3d axis;
			double angle;
			this->cam_controller.getAxisAngleForAngles(this->cam_controller.getAngles(), axis, angle);

			conPrint("cam_controller.getForwardsVec()" + this->cam_controller.getForwardsVec().toString());
			conPrint("cam_controller.getAngles()" + this->cam_controller.getAngles().toString());
			conPrint("axis: " + axis.toString());
			conPrint("angle: " + toString(angle));*/

			const Vec3d cam_angles = this->cam_controller.getAngles();
			const double angle = cam_angles.x;

			SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
			packet.writeUInt32(Protocol::AvatarTransformUpdate);
			writeToStream(this->client_thread->client_avatar_uid, packet);
			writeToStream(Vec3d(this->cam_controller.getPosition()), packet);
			writeToStream(Vec3f(0, 0, (float)angle), packet);

			this->client_thread->enqueueDataToSend(packet);
		}

		//============ Send any object updates needed ===========
		{
			Lock lock(this->world_state->mutex);

			for(auto it = this->world_state->objects.begin(); it != this->world_state->objects.end(); ++it)
			{
				WorldObject* world_ob = it->second.getPointer();
				if(world_ob->from_local_other_dirty)
				{
					// Enqueue ObjectFullUpdate
					SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
					packet.writeUInt32(Protocol::ObjectFullUpdate);
					writeToNetworkStream(*world_ob, packet);
					
					this->client_thread->enqueueDataToSend(packet);

					world_ob->from_local_other_dirty = false;
					world_ob->from_local_transform_dirty = false;
				}
				else if(world_ob->from_local_transform_dirty)
				{
					// Enqueue ObjectTransformUpdate
					SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
					packet.writeUInt32(Protocol::ObjectTransformUpdate);
					writeToStream(world_ob->uid, packet);
					writeToStream(Vec3d(world_ob->pos), packet);
					writeToStream(Vec3f(world_ob->axis), packet);
					packet.writeFloat(world_ob->angle);

					this->client_thread->enqueueDataToSend(packet);

					world_ob->from_local_transform_dirty = false;
				}

			}
		}


		time_since_update_packet_sent.reset();
	}

	ui->glWidget->makeCurrent();
	ui->glWidget->updateGL();

	if(need_physics_world_rebuild)
	{
		//Timer timer;
		physics_world->rebuild(task_manager, print_output);
		//conPrint("Physics world rebuild took " + timer.elapsedStringNSigFigs(5));
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
			this->physics_world->traceRay(origin, dir, thread_context, results);
			if(results.hit_object)
			{
				const Vec4f hitpos_ws = origin + dir*results.hitdist_ws;

				if(selected_ob.nonNull())
				{
					const bool have_edit_permissions = objectModificationAllowedWithMsg(*this->selected_ob, "edit");
					if(have_edit_permissions)
					{
						const float current_voxel_w = 1;

						Matrix4f ob_to_world = obToWorldMatrix(selected_ob);
						Matrix4f world_to_ob = worldToObMatrix(selected_ob);

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

							this->voxel_edit_marker->materials[0].albedo_rgb = Colour3f(0.1, 0.9, 0.2);
							this->ui->glWidget->opengl_engine->objectMaterialsUpdated(this->voxel_edit_marker);

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
							
							this->voxel_edit_marker->materials[0].albedo_rgb = Colour3f(0.9, 0.1, 0.1);
							this->ui->glWidget->opengl_engine->objectMaterialsUpdated(this->voxel_edit_marker);
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


void MainWindow::updateStatusBar()
{
	std::string status;
	switch(connection_state)
	{
	case ServerConnectionState_NotConnected:
		status += "Not connected to server.";
		break;
	case ServerConnectionState_Connecting:
		status += "Connecting to " + this->server_hostname + "...";
		break;
	case ServerConnectionState_Connected:
		status += "Connected to " + this->server_hostname;
		break;
	}

	if(total_num_res_to_download > 0)
		status += " | Downloading " + toString(total_num_res_to_download) + ((total_num_res_to_download == 1) ? " resource..." : " resources...");

	this->statusBar()->showMessage(QtUtils::toQString(status));
}


void MainWindow::on_actionAvatarSettings_triggered()
{
	AvatarSettingsDialog d(this->settings, this->texture_server);
	if(d.exec() == QDialog::Accepted)
	{
		// Send AvatarFullUpdate message to server
		try
		{
			const std::string URL = ResourceManager::URLForPathAndHash(d.result_path, d.model_hash);

			// Copy model to local resources dir.  UploadResourceThread will read from here.
			FileUtils::copyFile(d.result_path, this->resource_manager->pathForURL(URL));

			const Vec3d cam_angles = this->cam_controller.getAngles();
			Avatar avatar;
			avatar.uid = this->client_thread->client_avatar_uid;
			avatar.pos = Vec3d(this->cam_controller.getPosition());
			avatar.rotation = Vec3f(0, 0, (float)cam_angles.x);
			avatar.model_url = URL;
			avatar.name = d.getAvatarName();

			SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
			packet.writeUInt32(Protocol::AvatarFullUpdate);
			writeToNetworkStream(avatar, packet);

			this->client_thread->enqueueDataToSend(packet);
		}
		catch(Indigo::IndigoException& e)
		{
			// Show error
			print(toStdString(e.what()));
			QErrorMessage m;
			m.showMessage(QtUtils::toQString(e.what()));
			m.exec();
		}
		catch(FileUtils::FileUtilsExcep& e)
		{
			// Show error
			print(e.what());
			QErrorMessage m;
			m.showMessage(QtUtils::toQString(e.what()));
			m.exec();
		}
		catch(Indigo::Exception& e)
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


bool MainWindow::haveObjectWritePermissions(const js::AABBox& new_aabb_ws, bool& ob_pos_in_parcel_out)
{
	ob_pos_in_parcel_out = false;

	if(isGodUser(this->logged_in_user_id))
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

			if(parcel->AABBInParcel(new_aabb_ws))
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


// If the object was not in a parcel with write permissions at all, returns false.
// If the object can not be made to fit in the current parcel, returns false.
// new_ob_pos_out is set to new, clamped position.
bool MainWindow::clampObjectPositionToParcelForNewTransform(GLObjectRef& opengl_ob, const Vec3d& old_ob_pos,
	const Matrix4f& tentative_to_world_matrix,
	js::Vector<EdgeMarker, 16>& edge_markers_out, Vec3d& new_ob_pos_out)
{
	edge_markers_out.resize(0);
	bool have_creation_perms = false;
	Vec3d parcel_aabb_min;
	Vec3d parcel_aabb_max;

	if(isGodUser(this->logged_in_user_id))
	{
		const Vec4f newpos = tentative_to_world_matrix.getColumn(3);
		new_ob_pos_out = Vec3d(newpos[0], newpos[1], newpos[2]); // New object position
		return true;
	}

	// Work out what parcel the object is in currently (e.g. what parcel old_ob_pos is in)
	{
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
					parcel_aabb_min = parcel->aabb_min;
					parcel_aabb_max = parcel->aabb_max;
					break;
				}
			}
		}
	}

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


void MainWindow::on_actionAddObject_triggered()
{
	const Vec3d ob_pos = this->cam_controller.getPosition() + this->cam_controller.getForwardsVec() * 2.0f;

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

	AddObjectDialog d(this->base_dir_path, this->settings, this->texture_server, this->resource_manager);
	if(d.exec() == QDialog::Accepted)
	{
		// Try and load model
		try
		{
			ui->glWidget->makeCurrent();

			

			// Make GL object
			/*const Matrix4f ob_to_world_matrix = Matrix4f::translationMatrix((float)ob_pos.x, (float)ob_pos.y, (float)ob_pos.z);
			Indigo::MeshRef mesh;
			GLObjectRef gl_ob = ModelLoading::makeGLObjectForModelFile(d.result_path, ob_to_world_matrix, mesh);
			glWidget->addObject(gl_ob);

			// Make physics object
			StandardPrintOutput print_output;
			Indigo::TaskManager task_manager;
			PhysicsObjectRef physics_ob = makePhysicsObject(mesh, ob_to_world_matrix, print_output, task_manager);
			physics_world->addObject(physics_ob);*/

			// Add world object to local world state.
			/*{
				Lock lock(world_state->mutex);

				const UID uid(next_uid++);//TEMP
				WorldObjectRef world_ob = new WorldObject();
				world_ob->state = WorldObject::State_JustCreated;
				world_ob->from_local_dirty = true;
				world_ob->uid = uid;
				world_ob->angle = 0;
				world_ob->axis = Vec3f(1,0,0);
				world_ob->model_url = "teapot.obj";
				world_ob->opengl_engine_ob = gl_ob.getPointer();
				world_ob->physics_object = physics_ob.getPointer();
				world_state->objects[uid] = world_ob;

				physics_ob->userdata = world_ob.getPointer();
			}*/

			// If the user selected an obj, convert it to an indigo mesh file
			std::string igmesh_disk_path = d.result_path;
			if(!hasExtension(d.result_path, "igmesh"))
			{
				// Save as IGMESH in temp location
				igmesh_disk_path = PlatformUtils::getTempDirPath() + "/temp.igmesh";
				Indigo::Mesh::writeToFile(toIndigoString(igmesh_disk_path), *d.loaded_mesh, /*use compression=*/true);
			}
			else
			{
				igmesh_disk_path = d.result_path;
			}

			// Compute hash over model
			const uint64 model_hash = FileChecksum::fileChecksum(igmesh_disk_path);

			const std::string original_filename = FileUtils::getFilename(d.result_path); // Use the original filename, not 'temp.igmesh'.
			const std::string URL = ResourceManager::URLForNameAndExtensionAndHash(original_filename, ::getExtension(igmesh_disk_path), model_hash); // ResourceManager::URLForPathAndHash(igmesh_disk_path, model_hash);

			// Copy model to local resources dir.  UploadResourceThread will read from here.
			this->resource_manager->copyLocalFileToResourceDir(igmesh_disk_path, URL);

			


			WorldObjectRef new_world_object = new WorldObject();
			new_world_object->uid = UID(0); // Will be set by server
			new_world_object->model_url = URL;
			new_world_object->materials = d.loaded_materials;
			new_world_object->pos = ob_pos;
			new_world_object->axis = Vec3f(0, 0, 1);
			new_world_object->angle = 0;
			new_world_object->scale = Vec3f(1.f);

			// Copy all dependencies (textures etc..) to resources dir.  UploadResourceThread will read from here.
			std::set<std::string> paths;
			new_world_object->getDependencyURLSet(paths);
			for(auto it = paths.begin(); it != paths.end(); ++it)
			{
				const std::string path = *it;
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
			{
				SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);

				packet.writeUInt32(Protocol::CreateObject);
				writeToNetworkStream(*new_world_object, packet);

				this->client_thread->enqueueDataToSend(packet);
			}

			showInfoNotification("Object created.");
		}
		catch(Indigo::IndigoException& e)
		{
			// Show error
			print(toStdString(e.what()));
			QErrorMessage m;
			m.showMessage(QtUtils::toQString(e.what()));
			m.exec();
		}
		catch(FileUtils::FileUtilsExcep& e)
		{
			// Show error
			print(e.what());
			QErrorMessage m;
			m.showMessage(QtUtils::toQString(e.what()));
			m.exec();
		}
		catch(Indigo::Exception& e)
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
	const Vec3d ob_pos = this->cam_controller.getPosition() + this->cam_controller.getForwardsVec() * 2.0f -
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
	new_world_object->angle = (float)this->cam_controller.getAngles().x - Maths::pi_2<float>();
	new_world_object->scale = Vec3f(0.4f);
	new_world_object->content = "Select the object \nto edit this text";


	// Send CreateObject message to server
	{
		SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);

		packet.writeUInt32(Protocol::CreateObject);
		writeToNetworkStream(*new_world_object, packet);

		this->client_thread->enqueueDataToSend(packet);
	}

	showInfoNotification("Added hypercard.");
}


void MainWindow::on_actionAdd_Voxels_triggered()
{
	const Vec3d ob_pos = this->cam_controller.getPosition() + this->cam_controller.getForwardsVec() * 2.0f;

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

	// Send CreateObject message to server
	{
		SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);

		packet.writeUInt32(Protocol::CreateObject);
		writeToNetworkStream(*new_world_object, packet);

		this->client_thread->enqueueDataToSend(packet);
	}

	showInfoNotification("Voxel Object created.");

	// Deselect any currently selected object
	deselectObject();
}


bool MainWindow::areEditingVoxels()
{
	return this->selected_ob.nonNull() && this->selected_ob->object_type == WorldObject::ObjectType_VoxelGroup;
}


void MainWindow::on_actionCloneObject_triggered()
{
	if(this->selected_ob.nonNull())
	{
		const double dist_to_ob = this->selected_ob->pos.getDist(this->cam_controller.getPosition());

		const Vec3d new_ob_pos = this->selected_ob->pos + this->cam_controller.getRightVec() * dist_to_ob * 0.2;

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
		new_world_object->model_url = this->selected_ob->model_url;
		new_world_object->script_url = this->selected_ob->script_url;
		new_world_object->materials = this->selected_ob->materials; // TODO: clone?
		new_world_object->pos = new_ob_pos;
		new_world_object->axis = selected_ob->axis;
		new_world_object->angle = selected_ob->angle;
		new_world_object->scale = selected_ob->scale;

		// Send CreateObject message to server
		{
			SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);

			packet.writeUInt32(Protocol::CreateObject);
			writeToNetworkStream(*new_world_object, packet);

			this->client_thread->enqueueDataToSend(packet);
		}

		// Deselect any currently selected object
		deselectObject();
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

	ui->helpInfoDockWidget->setFloating(true);
	ui->helpInfoDockWidget->show();
	// Position near bottom right corner of glWidget.
	ui->helpInfoDockWidget->setGeometry(QRect(ui->glWidget->mapToGlobal(ui->glWidget->geometry().bottomRight() + QPoint(-320, -120)), QSize(300, 100)));

	//this->addDockWidget(Qt::RightDockWidgetArea, ui->chatDockWidget, Qt::Vertical);
	//ui->chatDockWidget->show();

	// Enable tool bar
	ui->toolBar->setVisible(true);
}


void MainWindow::passwordResetRequested()
{
	conPrint("passwordResetRequested()");

	ResetPasswordDialog dialog(settings);
	const int res = dialog.exec();
	if(res == QDialog::Accepted)
	{
		// Make RequestPasswordReset packet and enqueue to send
		const std::string email_addr = QtUtils::toIndString(dialog.emailLineEdit->text());
		SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
		packet.writeUInt32(Protocol::RequestPasswordReset);
		packet.writeStringLengthFirst(email_addr);
		this->client_thread->enqueueDataToSend(packet);

		QMessageBox msgBox;
		msgBox.setWindowTitle("Password Reset Requested");
		msgBox.setText("A reset-password email has been sent to the email address you entered.");
		msgBox.exec();

		ChangePasswordDialog change_password_dialog(settings);
		change_password_dialog.setResetCodeLineEditVisible(true);
		const int res2 = change_password_dialog.exec();
		if(res2 == QDialog::Accepted)
		{
			// Send a ChangePasswordWithResetToken packet
			const std::string reset_token = QtUtils::toIndString(change_password_dialog.resetCodeLineEdit->text());
			const std::string new_password = QtUtils::toIndString(change_password_dialog.passwordLineEdit->text());
			SocketBufferOutStream packet2(SocketBufferOutStream::DontUseNetworkByteOrder);
			packet2.writeUInt32(Protocol::ChangePasswordWithResetToken);
			packet2.writeStringLengthFirst(email_addr);
			packet2.writeStringLengthFirst(reset_token);
			packet2.writeStringLengthFirst(new_password);
			this->client_thread->enqueueDataToSend(packet2);
		}
	}
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

	LoginDialog dialog(settings);
	connect(&dialog, SIGNAL(passWordResetRequested()), this, SLOT(passwordResetRequested()));
	const int res = dialog.exec();
	if(res == QDialog::Accepted)
	{
		const std::string username = QtUtils::toStdString(dialog.usernameLineEdit->text());
		const std::string password = QtUtils::toStdString(dialog.passwordLineEdit->text());

		conPrint("username: " + username);
		conPrint("password: " + password);
		//this->last_login_username = username;

		// Make LogInMessage packet and enqueue to send
		SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
		packet.writeUInt32(Protocol::LogInMessage);
		packet.writeStringLengthFirst(username);
		packet.writeStringLengthFirst(password);
		this->client_thread->enqueueDataToSend(packet);
	}
}


void MainWindow::on_actionLogOut_triggered()
{
	// Make message packet and enqueue to send
	SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
	packet.writeUInt32(Protocol::LogOutMessage);
	this->client_thread->enqueueDataToSend(packet);
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

	SignUpDialog dialog(settings);
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
		SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
		packet.writeUInt32(Protocol::SignUpMessage);
		packet.writeStringLengthFirst(username);
		packet.writeStringLengthFirst(email);
		packet.writeStringLengthFirst(password);

		this->client_thread->enqueueDataToSend(packet);
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
				parcel->opengl_engine_ob = parcel->makeOpenGLObject(ui->glWidget->opengl_engine, write_perms);
				parcel->opengl_engine_ob->materials[0].shader_prog = this->parcel_shader_prog;
				ui->glWidget->opengl_engine->addObject(parcel->opengl_engine_ob); // Add to engine

				// Make physics object for parcel:
				parcel->physics_object = parcel->makePhysicsObject(this->unit_cube_raymesh, task_manager);
				physics_world->addObject(parcel->physics_object);
			}
		}

		physics_world->rebuild(task_manager, print_output);
	}
	catch(Indigo::Exception& e)
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

		physics_world->rebuild(task_manager, print_output);
	}
	catch(Indigo::Exception& e)
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
			parcel->setColourForPerms(write_perms);
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


void MainWindow::sendChatMessageSlot()
{
	//conPrint("MainWindow::sendChatMessageSlot()");

	const std::string message = QtUtils::toIndString(ui->chatMessageLineEdit->text());

	// Make message packet and enqueue to send
	SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
	packet.writeUInt32(Protocol::ChatMessageID);
	packet.writeStringLengthFirst(message);

	this->client_thread->enqueueDataToSend(packet);

	ui->chatMessageLineEdit->clear();
}


// Object has been edited, e.g. by the object editor.
void MainWindow::objectEditedSlot()
{
	// Update object material(s) with values from editor.
	if(this->selected_ob.nonNull())
	{
		//if(selected_ob->materials.empty())
		//	selected_ob->materials.push_back(new WorldMaterial());

		//this->matEditor->toMaterial(*this->selected_ob->materials[0]);
		ui->objectEditor->toObject(*this->selected_ob);

		// Copy all dependencies into resource directory if they are not there already.
		// URLs will actually be paths from editing for now.
		std::vector<std::string> URLs;
		this->selected_ob->appendDependencyURLs(URLs);

		for(size_t i=0; i<URLs.size(); ++i)
		{
			if(FileUtils::fileExists(URLs[i])) // If this was a local path:
			{
				const std::string local_path = URLs[i];
				const std::string URL = ResourceManager::URLForPathAndHash(local_path, FileChecksum::fileChecksum(local_path));

				// Copy model to local resources dir.
				resource_manager->copyLocalFileToResourceDir(local_path, URL);
			}
		}

		this->selected_ob->convertLocalPathsToURLS(*this->resource_manager);

		// All paths should be URLs now
		URLs.clear();
		this->selected_ob->appendDependencyURLs(URLs);

		// See if we have all required resources, we may have changed a texture URL to something we don't have
		bool all_downloaded = true;
		for(auto it = URLs.begin(); it != URLs.end(); ++it)
		{
			const std::string& url = *it;
			if(resource_manager->isValidURL(url))
			{
				if(!resource_manager->isFileForURLPresent(url))
				{
					all_downloaded = false;
					startDownloadingResource(url);
				}
			}
			else
				all_downloaded = false;
		}

		if(all_downloaded)
		{
			Matrix4f new_ob_to_world_matrix = Matrix4f::translationMatrix((float)this->selected_ob->pos.x, (float)this->selected_ob->pos.y, (float)this->selected_ob->pos.z) *
				Matrix4f::rotationMatrix(normalise(this->selected_ob->axis.toVec4fVector()), this->selected_ob->angle) *
				Matrix4f::scaleMatrix(this->selected_ob->scale.x, this->selected_ob->scale.y, this->selected_ob->scale.z);

			GLObjectRef opengl_ob = selected_ob->opengl_engine_ob;

			js::Vector<EdgeMarker, 16> edge_markers;
			Vec3d new_ob_pos;
			const bool valid = clampObjectPositionToParcelForNewTransform(
				opengl_ob,
				this->selected_ob->pos, 
				new_ob_to_world_matrix,
				edge_markers, 
				new_ob_pos);
			if(valid)
			{
				new_ob_to_world_matrix.setColumn(3, new_ob_pos.toVec4fPoint());
				selected_ob->setPosAndHistory(new_ob_pos);

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
								ModelLoading::setGLMaterialFromWorldMaterial(*this->selected_ob->materials[i], *this->resource_manager,
									opengl_ob->materials[i]
								);
						}
					}

					ui->glWidget->opengl_engine->objectMaterialsUpdated(opengl_ob);
				}
				else if(this->selected_ob->object_type == WorldObject::ObjectType_Hypercard)
				{
					if(selected_ob->content != selected_ob->loaded_content)
					{
						// Re-create opengl-ob
						ui->glWidget->makeCurrent();

						opengl_ob->materials.resize(1);
						opengl_ob->materials[0].albedo_texture = makeHypercardTexMap(selected_ob->content);

						opengl_ob->ob_to_world_matrix = new_ob_to_world_matrix;
						selected_ob->opengl_engine_ob = opengl_ob;

						selected_ob->loaded_content = selected_ob->content;
					}
				}

				// Update transform of OpenGL object
				opengl_ob->ob_to_world_matrix = new_ob_to_world_matrix;
				ui->glWidget->opengl_engine->updateObjectTransformData(*opengl_ob);

				// Update physics object transform
				selected_ob->physics_object->ob_to_world = new_ob_to_world_matrix;
				this->physics_world->updateObjectTransformData(*selected_ob->physics_object);

				updateSelectedObjectPlacementBeam(); // Has to go after physics world update due to ray-trace needed.

				// Mark as from-local-dirty to send an object updated message to the server
				this->selected_ob->from_local_other_dirty = true;

				if(this->selected_ob->model_url != this->selected_ob->loaded_model_url) // These will be different if model path was changed.
				{
					loadModelForObject(this->selected_ob.getPointer(), /*start_downloading_missing_files=*/false);
					this->ui->glWidget->opengl_engine->selectObject(this->selected_ob->opengl_engine_ob);
				}

				loadScriptForObject(this->selected_ob.getPointer());
			}
			else // Else if new transform is not valid
			{
				showErrorNotification("New object transform is not valid - Object must be entirely in a parcel that you have write permissions for.");
			}
		}
	}
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


void MainWindow::URLChangedSlot()
{
	// Set the play position to the coordinates in the URL
	try
	{
		const std::string URL = this->url_widget->getURL();
		Parser parser(URL.c_str(), (unsigned int)URL.size());
		// Parse protocol
		string_view protocol;
		parser.parseAlphaToken(protocol);
		if(protocol != "cyb")
			throw Indigo::Exception("Unhandled protocol scheme '" + protocol + "'.");
		if(!parser.parseString("://"))
			throw Indigo::Exception("Expected '://' after protocol scheme.");

		string_view host;
		//parser.parseAlphaToken(host);

		parser.parseToCharOrEOF('?', host);
		
		// TEMP: ignore host for now.

		double x = 0;
		double y = 0;
		double z = 2;
		if(parser.currentIsChar('?'))
		{
			if(!parser.parseChar('?'))
				throw Indigo::Exception("Expected '?' after host.");

			if(!parser.parseChar('x'))
				throw Indigo::Exception("Expected 'x' after '?'.");

			if(!parser.parseChar('='))
				throw Indigo::Exception("Expected '=' after 'x'.");

			if(!parser.parseDouble(x))
				throw Indigo::Exception("Failed to parse x coord.");

			if(!parser.parseChar('&'))
				throw Indigo::Exception("Expected '&' after x coodinate.");

			if(!parser.parseChar('y'))
				throw Indigo::Exception("Expected 'y' after '?'.");

			if(!parser.parseChar('='))
				throw Indigo::Exception("Expected '=' after 'y'.");

			if(!parser.parseDouble(y))
				throw Indigo::Exception("Failed to parse y coord.");

			if(parser.currentIsChar('&'))
			{
				parser.advance();

				string_view URL_arg_name;
				if(!parser.parseToChar('=', URL_arg_name))
					throw Indigo::Exception("Failed to parse URL argument after &");
				if(URL_arg_name == "z")
				{
					if(!parser.parseChar('='))
						throw Indigo::Exception("Expected '=' after 'z'.");

					if(!parser.parseDouble(z))
						throw Indigo::Exception("Failed to parse z coord.");
				}
				else
					throw Indigo::Exception("Unknown URL arg '" + URL_arg_name.to_string() + "'");
			}

			conPrint("x: " + toString(x) + ", y: " + toString(y) + ", z: " + toString(z));
		}

		this->cam_controller.setPosition(Vec3d(x, y, z));
	}
	catch(Indigo::Exception& e)
	{
		conPrint(e.what());
		QMessageBox msgBox;
		msgBox.setText(QtUtils::toQString(e.what()));
		msgBox.exec();
	}
}


Vec4f MainWindow::getDirForPixelTrace(int pixel_pos_x, int pixel_pos_y)
{
	const Vec4f forwards = cam_controller.getForwardsVec().toVec4fVector();
	const Vec4f right = cam_controller.getRightVec().toVec4fVector();
	const Vec4f up = cam_controller.getUpVec().toVec4fVector();

	const float sensor_width = GlWidget::sensorWidth();
	const float sensor_height = sensor_width / ui->glWidget->viewport_aspect_ratio;
	const float lens_sensor_dist = GlWidget::lensSensorDist();

	const float s_x = sensor_width *  (float)(pixel_pos_x - ui->glWidget->geometry().width() /2) / ui->glWidget->geometry().width(); // dist right on sensor from centre of sensor
	const float s_y = sensor_height * (float)(pixel_pos_y - ui->glWidget->geometry().height()/2) / ui->glWidget->geometry().height(); // dist down on sensor from centre of sensor

	const float r_x = s_x / lens_sensor_dist;
	const float r_y = s_y / lens_sensor_dist;

	const Vec4f dir = normalise(forwards + right * r_x - up * r_y);
	return dir;
}


void MainWindow::glWidgetMouseClicked(QMouseEvent* e)
{
	if(areEditingVoxels())
	{
		const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
		const Vec4f dir = getDirForPixelTrace(e->pos().x(), e->pos().y());
		RayTraceResult results;
		this->physics_world->traceRay(origin, dir, thread_context, results);
		if(results.hit_object)
		{
			const Vec4f hitpos_ws = origin + dir*results.hitdist_ws;

			if(selected_ob.nonNull())
			{
				const bool have_edit_permissions = objectModificationAllowedWithMsg(*selected_ob, "edit");
				if(have_edit_permissions)
				{
					const float current_voxel_w = 1;

					Matrix4f ob_to_world = obToWorldMatrix(selected_ob);
					Matrix4f world_to_ob = worldToObMatrix(selected_ob);

					bool voxels_changed = false;

					if(e->modifiers() & Qt::ControlModifier)
					{
						const Vec4f point_off_surface = hitpos_ws + results.hit_normal_ws * (current_voxel_w * 1.0e-3f);

						const Vec4f point_os = world_to_ob * point_off_surface;
						const Vec4f point_os_voxel_space = point_os / current_voxel_w;
						Vec3<int> voxel_indices((int)floor(point_os_voxel_space[0]), (int)floor(point_os_voxel_space[1]), (int)floor(point_os_voxel_space[2]));

						// Add the voxel!
						this->selected_ob->voxel_group.voxels.push_back(Voxel());
						this->selected_ob->voxel_group.voxels.back().pos = voxel_indices;
						this->selected_ob->voxel_group.voxels.back().mat_index = ui->objectEditor->getSelectedMatIndex();

						voxels_changed = true;
					}
					else if(e->modifiers() & Qt::AltModifier)
					{
						const Vec4f point_under_surface = hitpos_ws - results.hit_normal_ws * (current_voxel_w * 1.0e-3f);

						const Vec4f point_os = world_to_ob * point_under_surface;
						const Vec4f point_os_voxel_space = point_os / current_voxel_w;
						Vec3<int> voxel_indices((int)floor(point_os_voxel_space[0]), (int)floor(point_os_voxel_space[1]), (int)floor(point_os_voxel_space[2]));

						// Remove the voxel, if present
						for(size_t z=0; z<this->selected_ob->voxel_group.voxels.size(); ++z)
						{
							if(this->selected_ob->voxel_group.voxels[z].pos == voxel_indices)
								this->selected_ob->voxel_group.voxels.erase(this->selected_ob->voxel_group.voxels.begin() + z);
						}

						voxels_changed = true;
					}

					if(voxels_changed)
					{
						// Remove any existing OpenGL and physics model
						if(this->selected_ob->opengl_engine_ob.nonNull())
							ui->glWidget->removeObject(this->selected_ob->opengl_engine_ob);

						if(this->selected_ob->physics_object.nonNull())
							physics_world->removeObject(this->selected_ob->physics_object);

						if(this->selected_ob->voxel_group.voxels.size() > 0)
						{
							// Add updated model!
							Reference<RayMesh> raymesh;
							Reference<OpenGLMeshRenderData> gl_meshdata = ModelLoading::makeModelForVoxelGroup(this->selected_ob->voxel_group, task_manager, raymesh);

							GLObjectRef gl_ob = new GLObject();
							gl_ob->ob_to_world_matrix = ob_to_world;
							gl_ob->mesh_data = gl_meshdata;

							gl_ob->materials.resize(this->selected_ob->materials.size());
							for(uint32 i=0; i<this->selected_ob->materials.size(); ++i)
								ModelLoading::setGLMaterialFromWorldMaterial(*this->selected_ob->materials[i], *this->resource_manager, gl_ob->materials[i]);

							Reference<PhysicsObject> physics_ob = new PhysicsObject(/*collidable=*/true);
							physics_ob->geometry = raymesh;
							physics_ob->ob_to_world = ob_to_world;


							this->selected_ob->opengl_engine_ob = gl_ob;
							ui->glWidget->addObject(gl_ob);

							ui->glWidget->opengl_engine->selectObject(gl_ob);

							this->selected_ob->physics_object = physics_ob;
							physics_ob->userdata = (void*)(this->selected_ob.ptr());
							physics_ob->userdata_type = 0;
							physics_world->addObject(physics_ob);
							physics_world->rebuild(task_manager, print_output);
						}

						// Mark as from-local-dirty to send an object updated message to the server
						this->selected_ob->from_local_other_dirty = true;
					}
				}
			}
		}
		
	}
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

			const Vec4f selection_point_ws = obToWorldMatrix(this->selected_ob) * this->selection_point_os;

			const Vec4f selection_vec_ws = selection_point_ws - origin;
			this->selection_vec_cs = Vec4f(dot(selection_vec_ws, right), dot(selection_vec_ws, forwards), dot(selection_vec_ws, up), 0.f);

			ui->glWidget->opengl_engine->setSelectionOutlineColour(PICKED_UP_OUTLINE_COLOUR);

			// Send UserSelectedObject message to server
			SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
			packet.writeUInt32(Protocol::UserSelectedObject);
			writeToStream(selected_ob->uid, packet);
			this->client_thread->enqueueDataToSend(packet);

			showInfoNotification("Picked up object.");

			selected_ob_picked_up = true;
		}
	}
}


void MainWindow::dropSelectedObject()
{
	if(selected_ob.nonNull() && selected_ob_picked_up)
	{
		// Send UserDeselectedObject message to server
		SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
		packet.writeUInt32(Protocol::UserDeselectedObject);
		writeToStream(selected_ob->uid, packet);
		this->client_thread->enqueueDataToSend(packet);

		ui->glWidget->opengl_engine->setSelectionOutlineColour(DEFAULT_OUTLINE_COLOUR);

		showInfoNotification("Dropped object.");

		selected_ob_picked_up = false;
	}
}



void MainWindow::glWidgetMouseDoubleClicked(QMouseEvent* e)
{
	//conPrint("MainWindow::glWidgetMouseDoubleClicked()");

	// Trace ray through scene
	const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
	const Vec4f forwards = cam_controller.getForwardsVec().toVec4fVector();
	const Vec4f right = cam_controller.getRightVec().toVec4fVector();
	const Vec4f up = cam_controller.getUpVec().toVec4fVector();

	const Vec4f dir = getDirForPixelTrace(e->pos().x(), e->pos().y());

	RayTraceResult results;
	this->physics_world->traceRay(origin, dir, thread_context, results);

	if(results.hit_object)
	{
		const Vec4f selection_point_ws = origin + dir*results.hitdist_ws;

		// Store the object-space selection point.  This will be used for moving the object.
		this->selection_point_os = results.hit_object->world_to_ob * selection_point_ws;

		// Add an object at the hit point
		//this->glWidget->addObject(glWidget->opengl_engine->makeAABBObject(this->selection_point_ws - Vec4f(0.03f, 0.03f, 0.03f, 0.f), this->selection_point_ws + Vec4f(0.03f, 0.03f, 0.03f, 0.f), Colour4f(0.6f, 0.6f, 0.2f, 1.f)));

		// Deselect any currently selected object
		if(this->selected_ob.nonNull())
			deselectObject();

		if(this->selected_parcel.nonNull())
			deselectParcel();

		if(results.hit_object->userdata && results.hit_object->userdata_type == 0) // If we hit an object:
		{
			// Select the object

			this->selected_ob = static_cast<WorldObject*>(results.hit_object->userdata);
			assert(this->selected_ob->getRefCount() >= 0);

			// Mark the materials on the hit object as selected
			ui->glWidget->opengl_engine->selectObject(selected_ob->opengl_engine_ob);
			ui->glWidget->opengl_engine->setSelectionOutlineColour(DEFAULT_OUTLINE_COLOUR);


			const bool have_edit_permissions = objectModificationAllowed(*this->selected_ob);

			// Add an object placement beam
			if(have_edit_permissions)
			{
				ui->glWidget->opengl_engine->addObject(ob_placement_beam);
				ui->glWidget->opengl_engine->addObject(ob_placement_marker);

				updateSelectedObjectPlacementBeam();
			}

			const int selected_mat = selected_ob->physics_object->geometry->getMaterialIndexForTri(results.hit_tri_index);

			// Show object editor, hide parcel editor.
			ui->objectEditor->setFromObject(*selected_ob, selected_mat); // Update the editor widget with values from the selected object
			ui->objectEditor->setEnabled(true);
			ui->objectEditor->show();
			ui->parcelEditor->hide();

			
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
					"'-' and '+' keys wheel moves object near/far.\n"
					"Esc key: deselect object.";

				this->ui->helpInfoLabel->setText(text);
				this->ui->helpInfoDockWidget->show();
			}
		}
		else if(results.hit_object->userdata && results.hit_object->userdata_type == 1) // Else if we hit a parcel:
		{
			this->selected_parcel = static_cast<Parcel*>(results.hit_object->userdata);

			ui->glWidget->opengl_engine->selectObject(selected_parcel->opengl_engine_ob);


			// Show parcel editor, hide object editor.
			ui->parcelEditor->setFromParcel(*selected_parcel);
			ui->parcelEditor->setEnabled(true);
			ui->parcelEditor->show();
			ui->objectEditor->hide();
			ui->editorDockWidget->show(); // Show the object editor dock widget if it is hidden.
		}
		else
		{
			ui->objectEditor->setEnabled(false);
		}
	}
	else
	{
		// Deselect any currently selected object
		deselectObject();
		deselectParcel();
	}
}


void MainWindow::glWidgetMouseMoved(QMouseEvent* e)
{
//	Qt::MouseButtons mb = e->buttons();
//
//	if(this->selected_ob.nonNull() && (mb & Qt::LeftButton))
//	{
//		// Move selected object 
//
//		// Get direction for current mouse cursor position
//		const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
//		const Vec4f forwards = cam_controller.getForwardsVec().toVec4fVector();
//		const Vec4f right = cam_controller.getRightVec().toVec4fVector();
//		const Vec4f up = cam_controller.getUpVec().toVec4fVector();
//
//		const float sensor_width = 0.035f;
//		const float sensor_height = sensor_width / glWidget->viewport_aspect_ratio;
//		const float lens_sensor_dist = 0.03f;
//
//		const float s_x = sensor_width *  (float)(e->pos().x() - glWidget->geometry().width() /2) / glWidget->geometry().width(); // dist right on sensor from centre of sensor
//		const float s_y = sensor_height * (float)(e->pos().y() - glWidget->geometry().height()/2) / glWidget->geometry().height(); // dist down on sensor from centre of sensor
//
//		const float r_x = s_x / lens_sensor_dist;
//		const float r_y = s_y / lens_sensor_dist;
//
//		const Vec4f dir = normalise(forwards + right * r_x - up * r_y);
//
//		// Convert selection vector to world space
//		const Vec4f selection_vec_ws(right*selection_vec_cs[0] + forwards*selection_vec_cs[1] + up*selection_vec_cs[2]);
//
//		const Vec4f new_sel_point_ws = origin + selection_vec_ws;
//		//const Vec4f new_sel_point_ws = origin + dir*this->selection_point_dist;
//
//		
//		const Vec4f new_ob_pos = this->selected_ob_pos_upon_selection + (new_sel_point_ws - this->selection_point_ws);
//
//		// Set world object pos
//		this->selected_ob->pos = toVec3d(new_ob_pos);
//
//		// Set graphics object pos and update in opengl engine.
//		GLObjectRef opengl_ob(this->selected_ob->opengl_engine_ob);
//		opengl_ob->ob_to_world_matrix.setColumn(3, new_ob_pos);
//		this->glWidget->opengl_engine->updateObjectTransformData(*opengl_ob);
//	}
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

		const Matrix4f new_ob_to_world = Matrix4f::translationMatrix(ob->pos.toVec4fPoint()) * Matrix4f::rotationMatrix(normalise(ob->axis.toVec4fVector()), ob->angle) *
			Matrix4f::scaleMatrix(ob->scale.x, ob->scale.y, ob->scale.z);

		// Update in opengl engine.
		GLObjectRef opengl_ob = ob->opengl_engine_ob;
		opengl_ob->ob_to_world_matrix = new_ob_to_world;
		ui->glWidget->opengl_engine->updateObjectTransformData(*opengl_ob);

		// Update physics object
		ob->physics_object->ob_to_world = new_ob_to_world;
		this->physics_world->updateObjectTransformData(*ob->physics_object);

		// Update object values in editor
		ui->objectEditor->setFromObject(*ob, ui->objectEditor->getSelectedMatIndex());
	}
}


void MainWindow::deleteSelectedObject()
{
	if(this->selected_ob.nonNull())
	{
		if(objectModificationAllowedWithMsg(*this->selected_ob, "delete"))
		{
			QMessageBox::StandardButton reply = QMessageBox::question(this, "Delete object", "Are you sure you want to delete this object?", QMessageBox::Yes|QMessageBox::No);
			if(reply == QMessageBox::Yes)
			{
				// Send DestroyObject packet
				SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
				packet.writeUInt32(Protocol::DestroyObject);
				writeToStream(selected_ob->uid, packet);

				this->client_thread->enqueueDataToSend(packet);


				deselectObject();
			}
		}
	}
}


void MainWindow::deselectObject()
{
	if(this->selected_ob.nonNull())
	{
		dropSelectedObject();

		// Remove placement beam from 3d engine
		ui->glWidget->opengl_engine->removeObject(this->ob_placement_beam);
		ui->glWidget->opengl_engine->removeObject(this->ob_placement_marker);

		// Remove any edge markers
		while(ob_denied_move_markers.size() > 0)
		{
			ui->glWidget->opengl_engine->removeObject(ob_denied_move_markers.back());
			ob_denied_move_markers.pop_back();
		}

		// Deselect any currently selected object
		ui->glWidget->opengl_engine->deselectObject(this->selected_ob->opengl_engine_ob);

		ui->objectEditor->setEnabled(false);

		this->selected_ob = NULL;

		this->shown_object_modification_error_msg = false;

		this->ui->helpInfoDockWidget->hide();
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

		this->ui->helpInfoDockWidget->hide();
	}
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
	else if(e->key() == Qt::Key::Key_Delete)
	{
		if(this->selected_ob.nonNull())
		{
			deleteSelectedObject();
		}
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
}


void MainWindow::glWidgetkeyReleased(QKeyEvent* e)
{
}


void MainWindow::glWidgetMouseWheelEvent(QWheelEvent* e)
{
	if(this->selected_ob.nonNull())
	{
		this->selection_vec_cs[1] *= (1.0f + e->delta() * 0.0005f);
	}
}


GLObjectRef MainWindow::makeNameTagGLObject(const std::string& nametag)
{
	const int W = 256;
	const int H = 80;

	GLObjectRef gl_ob = new GLObject();
	gl_ob->mesh_data = this->hypercard_quad_opengl_mesh;
	gl_ob->materials.resize(1);

	// Make nametag texture

	ImageMapUInt8Ref map = new ImageMapUInt8(W, H, 3);

	QImage image(W, H, QImage::Format_RGB888);
	image.fill(QColor(230, 230, 230));
	QPainter painter(&image);
	painter.setPen(QPen(QColor(0, 0, 0)));
	painter.setFont(QFont("Times", 24, QFont::Bold));
	painter.drawText(image.rect(), Qt::AlignCenter, QtUtils::toQString(nametag));

	// Copy to map
	for(int y=0; y<H; ++y)
	{
		const QRgb* line = (const QRgb*)image.scanLine(y);
		std::memcpy(map->getPixel(0, H - y - 1), line, 3*W);
	}

	gl_ob->materials[0].fresnel_scale = 0.1f;
	gl_ob->materials[0].albedo_rgb = Colour3f(0.8f);
	gl_ob->materials[0].albedo_texture = ui->glWidget->opengl_engine->getOrLoadOpenGLTexture(*map);
	return gl_ob;
}


Reference<OpenGLTexture> MainWindow::makeHypercardTexMap(const std::string& content)
{
	// conPrint("makeHypercardGLObject(), content: " + content);
	const int W = 512;
	const int H = 512;

	// Make hypercard texture

	ImageMapUInt8Ref map = new ImageMapUInt8(W, H, 3);

	QImage image(W, H, QImage::Format_RGB888);
	image.fill(QColor(220, 220, 220));
	QPainter painter(&image);
	painter.setPen(QPen(QColor(30, 30, 30)));
	painter.setFont(QFont("helvetica", 30, QFont::Normal));
	const int padding = 20;
	painter.drawText(QRect(padding, padding, W - padding*2, H - padding*2), Qt::AlignLeft/* | Qt::AlignVCenter*/, QtUtils::toQString(content));

	// Copy to map
	for(int y=0; y<H; ++y)
	{
		const QRgb* line = (const QRgb*)image.scanLine(y);
		std::memcpy(map->getPixel(0, H - y - 1), line, 3*W);
	}

	return ui->glWidget->opengl_engine->getOrLoadOpenGLTexture(*map);
}


static bool contains(const SmallVector<Vec2i, 4>& v, const Vec2i& p)
{
	for(size_t i=0; i<v.size(); ++i)
		if(v[i] == p)
			return true;
	return false;
}


void MainWindow::updateGroundPlane()
{
	// The basic idea is that we want to have a ground-plane quad under the player's feet at all times.
	// However the quad can't get too large, or you start getting shuddering and other graphical glitches.
	// So we'll load in 4 quads around the player position, and add new quads or remove old ones as required as the player moves.
	
	const Vec3d pos = cam_controller.getPosition();

	// Get integer indices of nearest 4 quads to player position.
	const int cur_x = Maths::floorToInt(pos.x / ground_quad_w);
	const int cur_y = Maths::floorToInt(pos.y / ground_quad_w);

	const int adj_x = (Maths::fract(pos.x / ground_quad_w) < 0.5) ? (cur_x - 1) : (cur_x + 1);
	const int adj_y = (Maths::fract(pos.y / ground_quad_w) < 0.5) ? (cur_y - 1) : (cur_y + 1);

	SmallVector<Vec2i, 4> new_quads(4);
	new_quads[0] = Vec2i(cur_x, cur_y);
	new_quads[1] = Vec2i(adj_x, cur_y);
	new_quads[2] = Vec2i(cur_x, adj_y);
	new_quads[3] = Vec2i(adj_x, adj_y);

	// Add any new quad not in ground_quads.
	for(auto it = new_quads.begin(); it != new_quads.end(); ++it)
		if(ground_quads.count(*it) == 0)
		{
			// Make new quad
			//conPrint("Added ground quad (" + toString(it->x) + ", " + toString(it->y) + ")");

			GLObjectRef gl_ob = new GLObject();
			gl_ob->materials.resize(1);
			gl_ob->materials[0].albedo_rgb = Colour3f(0.9f);
			//gl_ob->materials[0].albedo_rgb = Colour3f(Maths::fract(it->x * 0.1234), Maths::fract(it->y * 0.436435f), 0.7f);
			gl_ob->materials[0].albedo_tex_path = "resources/obstacle.png";
			gl_ob->materials[0].roughness = 0.8f;
			gl_ob->materials[0].fresnel_scale = 0.5f;

			gl_ob->ob_to_world_matrix.setToTranslationMatrix(it->x * (float)ground_quad_w, it->y * (float)ground_quad_w, 0);
			gl_ob->mesh_data = ground_quad_mesh_opengl_data;

			ui->glWidget->addObject(gl_ob);

			Reference<PhysicsObject> phy_ob = new PhysicsObject(/*collidable=*/true);
			phy_ob->geometry = ground_quad_raymesh;
			phy_ob->ob_to_world = gl_ob->ob_to_world_matrix;

			physics_world->addObject(phy_ob);

			GroundQuad ground_quad;
			ground_quad.gl_ob = gl_ob;
			ground_quad.phy_ob = phy_ob;

			ground_quads.insert(std::make_pair(*it, ground_quad));
		}

	// Remove any stale ground quads.
	for(auto it = ground_quads.begin(); it != ground_quads.end();)
	{
		if(!contains(new_quads, it->first))
		{
			//conPrint("Removed ground quad (" + toString(it->first.x) + ", " + toString(it->first.y) + ")");

			// Remove this ground quad as it is not needed any more.
			ui->glWidget->removeObject(it->second.gl_ob);
			physics_world->removeObject(it->second.phy_ob);

			it = ground_quads.erase(it);
		}
		else
			++it;
	}
}


int main(int argc, char *argv[])
{
	GuiClientApplication app(argc, argv);

	// Set the C standard lib locale back to c, so e.g. printf works as normal, and uses '.' as the decimal separator.
	std::setlocale(LC_ALL, "C");

	Clock::init();
	Networking::createInstance();
	Winter::VirtualMachine::init();
	TLSSocket::initTLS();

	PlatformUtils::ignoreUnixSignals();

	std::string cyberspace_base_dir_path;
	std::string appdata_path;
	try
	{
		cyberspace_base_dir_path = PlatformUtils::getResourceDirectoryPath();

		appdata_path = PlatformUtils::getOrCreateAppDataDirectory("Cyberspace");
	}
	catch(PlatformUtils::PlatformUtilsExcep& e)
	{
		conPrint(e.what());
		QErrorMessage m;
		m.showMessage(QtUtils::toQString(e.what()));
		m.exec();
		return 1;
	}

	QDir::setCurrent(QtUtils::toQString(cyberspace_base_dir_path));
	
	conPrint("cyberspace_base_dir_path: " + cyberspace_base_dir_path);


	// Get a vector of the args.  Note that we will use app.arguments(), because it's the only way to get the args in Unicode in Qt.
	const QStringList arg_list = app.arguments();
	std::vector<std::string> args;
	for(int i = 0; i < arg_list.size(); ++i)
		args.push_back(QtUtils::toIndString(arg_list.at((int)i)));

	try
	{
		std::map<std::string, std::vector<ArgumentParser::ArgumentType> > syntax;
		syntax["--test"] = std::vector<ArgumentParser::ArgumentType>();
		syntax["-h"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string);
		

		if(args.size() == 3 && args[1] == "-NSDocumentRevisionsDebugMode")
			args.resize(1); // This is some XCode debugging rubbish, remove it

		ArgumentParser parsed_args(args, syntax);

#if BUILD_TESTS
		if(parsed_args.isArgPresent("--test"))
		{
			js::Triangle::test();
			//Timer::test();
			//IPAddress::test();
			//FormatDecoderGLTF::test();
			//JSONParser::test();
			//OpenGLEngineTests::test(cyberspace_base_dir_path);
			//StringUtils::test();
			//URL::test();
			//HTTPClient::test();
			//EnvMapProcessing::run(cyberspace_base_dir_path);
			//SMTPClient::test();
			//js::VectorUnitTests::test();
			js::TreeTest::doTests(appdata_path);
			//Vec4f::test();
			//js::AABBox::test();
			//Matrix4f::test();
			//ReferenceTest::run();
			//Matrix4f::test();
			//CameraController::test();
			return 0;
		}
#endif

		std::string server_hostname = "substrata.info";
		if(parsed_args.isArgPresent("-h"))
			server_hostname = parsed_args.getArgStringValue("-h");


		MainWindow mw(cyberspace_base_dir_path, appdata_path, parsed_args);

		TextureServer texture_server;
		mw.texture_server = &texture_server;

		mw.initialise();

		mw.ui->glWidget->texture_server_ptr = &texture_server;

		mw.show();

		mw.raise();

		if(mw.ui->glWidget->opengl_engine.nonNull() && !mw.ui->glWidget->opengl_engine->initSucceeded())
		{
			mw.print("opengl_engine init failed: " + mw.ui->glWidget->opengl_engine->getInitialisationErrorMsg());
		}

		mw.server_hostname = server_hostname;
		mw.world_state = new WorldState();

		mw.resource_download_thread_manager.addThread(new DownloadResourcesThread(&mw.msg_queue, mw.resource_manager, mw.server_hostname, server_port));

		for(int i=0; i<4; ++i)
			mw.net_resource_download_thread_manager.addThread(new NetDownloadResourcesThread(&mw.msg_queue, mw.resource_manager));

		const std::string avatar_path = QtUtils::toStdString(mw.settings->value("avatarPath").toString());
		//const std::string username    = QtUtils::toStdString(mw.settings->value("username").toString());

		uint64 avatar_model_hash = 0;
		if(FileUtils::fileExists(avatar_path))
			avatar_model_hash = FileChecksum::fileChecksum(avatar_path);
		const std::string avatar_URL = mw.resource_manager->URLForPathAndHash(avatar_path, avatar_model_hash);

		mw.client_thread = new ClientThread(&mw.msg_queue, server_hostname, server_port, &mw, avatar_URL);
		mw.client_thread->world_state = mw.world_state;
		mw.client_thread_manager.addThread(mw.client_thread);


		mw.physics_world = new PhysicsWorld();


		mw.cam_controller.setPosition(Vec3d(0,0,4.7));
		mw.ui->glWidget->setCameraController(&mw.cam_controller);
		mw.ui->glWidget->setPlayerPhysics(&mw.player_physics);
		mw.cam_controller.setMoveScale(0.3f);

		const float sun_phi = 1.f;
		const float sun_theta = Maths::pi<float>() / 4;
		mw.ui->glWidget->opengl_engine->setSunDir(normalise(Vec4f(std::cos(sun_phi) * sin(sun_theta), std::sin(sun_phi) * sun_theta, cos(sun_theta), 0)));

		mw.ui->glWidget->opengl_engine->setEnvMapTransform(Matrix3f::rotationMatrix(Vec3f(0,0,1), sun_phi));

		/*
		Set env material
		*/
		{
			OpenGLMaterial env_mat;
			env_mat.albedo_tex_path = cyberspace_base_dir_path + "/resources/sky_no_sun.exr";
			env_mat.tex_matrix = Matrix2f(-1 / Maths::get2Pi<float>(), 0, 0, 1 / Maths::pi<float>());

			mw.ui->glWidget->setEnvMat(env_mat);
		}


		// Make an arrow marking the axes at the origin
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


		// Load a test voxel
		/*{
			VoxelGroup voxel_group;
			//voxel_group.voxel_width = 0.5;
			voxel_group.voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
			voxel_group.voxels.push_back(Voxel(Vec3<int>(1, 0, 0), 1));
			voxel_group.voxels.push_back(Voxel(Vec3<int>(1, 0, 1), 0));
			//voxel_group.voxels.push_back(Voxel(Vec3<int>(0, 0, 1), 1));
			//voxel_group.voxels.push_back(Voxel(Vec3<int>(1, 1, 1), 0));
			//voxel_group.voxels.push_back(Voxel(Vec3<int>(1, 1, 2), 2));
			//
			//
			//const int N = 10;
			//for(int i=0; i<N; ++i)
			//	voxel_group.voxels.push_back(Voxel(Vec3<int>(1, 1, 2 + i), 2));

			Timer timer;

			Reference<RayMesh> raymesh;
			Reference<OpenGLMeshRenderData> gl_meshdata;
			//for(int z=0; z<10000; ++z)
				gl_meshdata = ModelLoading::makeModelForVoxelGroup(voxel_group, mw.task_manager, raymesh);

			conPrint("Voxel meshing took " + timer.elapsedString());

			GLObjectRef gl_ob = new GLObject();
			gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(3, 3, 1);
			gl_ob->mesh_data = gl_meshdata;

			gl_ob->materials.resize(3);
			gl_ob->materials[0].albedo_rgb = Colour3f(0.9f, 0.1f, 0.1f);
			gl_ob->materials[0].albedo_tex_path = "resources/obstacle.png";

			gl_ob->materials[1].albedo_rgb = Colour3f(0.1f, 0.9f, 0.1f);

			gl_ob->materials[2].albedo_rgb = Colour3f(0.1f, 0.1f, 0.9f);

			mw.ui->glWidget->addObject(gl_ob);
		}*/
		//mw.ui->glWidget->opengl_engine->setDrawWireFrames(true);





		// Test loading ben's world data (CryptoVoxels), data is from https://www.cryptovoxels.com/grid/parcels
		if(false)
		{
			const std::string scene_dir = "D:\\files\\CV_world";
			const bool save_indigo_scene = true;

			std::string indigo_xml =
				"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
				"<scene>\n"
				"	<renderer_settings>	 \n"
				"		<width>800</width>		 \n"
				"		<height>600</height>	 \n"
				"	</renderer_settings>	 \n";

			indigo_xml +=
				"<skylight>									\n"
				"	<sundir>1 1 1</sundir>					\n"
				"	<turbidity>2</turbidity>					\n"
				"												\n"
				"	<model>captured-simulation</model>			\n"
				"</skylight>								\n";

			indigo_xml +=
				"<camera>											   \n"
				"	<pos>2 0 3</pos>								   \n"
				"	<up>0 0 1</up>									   \n"
				"	<forwards>1 0 0</forwards>						   \n"
				"	<!--<forwards>1 1 0</forwards>-->				   \n"
				"													   \n"
				"													   \n"
				"	<aperture_radius>0.001</aperture_radius>			   \n"
				"	<focus_distance>10000.0</focus_distance>		   \n"
				"	<aspect_ratio>1.33</aspect_ratio>				   \n"
				"	<sensor_width>0.036</sensor_width>				   \n"
				"	<lens_sensor_dist>0.03</lens_sensor_dist>		   \n"
				"	<white_balance>D65</white_balance>				   \n"
				"													   \n"
				"	</camera>										   \n";
			indigo_xml +=
				"<tonemapping>											  \n"
				"	<linear>											  \n"
				"		<scale>0.05</scale>								  \n"
				"	</linear>											  \n"
				"	</tonemapping>										  \n";

			// Add ground mesh
			indigo_xml +=
				"	<material>																  \n"
				"		<name>white</name>													  \n"
				"		<diffuse>															  \n"
				"			<colour>0.85 0.85 0.85</colour>									  \n"
				"		</diffuse>															  \n"
				"	</material>																  \n"
				"																			  \n"
				"	<mesh>																	  \n"
				"		<name>groundmesh</name>												  \n"
				"		<normal_smoothing>false</normal_smoothing>							  \n"
				"		<embedded>															  \n"
				"			<expose_uv_set>													  \n"
				"				<index>0</index>											  \n"
				"				<name>albedo</name>											  \n"
				"			</expose_uv_set>												  \n"
				"																			  \n"
				"			<vertex pos=\"-1000 -1000 1\" normal=\"0 0 1\" uv0=\"0 0\" />				  \n"
				"			<vertex pos=\"-1000 1000 1\" normal=\"0 0 1\" uv0=\"0 10000\" />			  \n"
				"			<vertex pos=\"1000 1000 1\" normal=\"0 0 1\" uv0=\"10000 10000\" />		  \n"
				"			<vertex pos=\"1000 -1000 1\" normal=\"0 0 1\" uv0=\"10000 0\" />			  \n"
				"																			  \n"
				"			<triangle_set>													  \n"
				"				<material_name>white</material_name>						  \n"
				"				<tri>0 1 2</tri>											  \n"
				"				<tri>0 2 3</tri>											  \n"
				"			</triangle_set>													  \n"
				"		</embedded>															  \n"
				"	</mesh>																	  \n"
				"																			  \n"
				"	<model>																	  \n"
				"		<pos>0 0 0</pos>													  \n"
				"		<scale>1.0</scale>													  \n"
				"		<mesh_name>groundmesh</mesh_name>									  \n"
				"	</model>																  \n";

			// Make texture mats
			std::vector<OpenGLMaterial> cv_mats(16 + 8);
			const char* paths[] ={
				"00-grid.png",
				"01-grid.png",
				"02-window.png",
				"03-white-square.png",
				"04-line.png",
				"05-bricks.png",
				"06-the-xx.png",
				"07-lined.png",
				"08-nick-batt.png",
				"09-scots.png",
				"10-subgrid.png",
				"11-microblob.png",
				"12-smallblob.png",
				"13-smallblob.png",
				"14-blob.png",
				"03-white-square.png"
			};
			const std::string base_path = "D:\\files\\cryptovoxels_textures";
			for(int i=0; i<16; ++i)
				cv_mats[i].albedo_tex_path = base_path + "/" + paths[i];

			cv_mats[2].transparent = true;

			// Make constant colour mats
			const char* colors[] ={
				"#ffffff",
				"#888888",
				"#000000",
				"#ff71ce",
				"#01cdfe",
				"#05ffa1",
				"#b967ff",
				"#fffb96"
			};
			for(int i=0; i<8; ++i)
				cv_mats[16 + i].albedo_rgb = Colour3f(
					hexStringToUInt32(std::string(colors[i]).substr(1, 2)) / 255.0f,
					hexStringToUInt32(std::string(colors[i]).substr(3, 2)) / 255.0f,
					hexStringToUInt32(std::string(colors[i]).substr(5, 2)) / 255.0f
				);

			// Write mats to Indigo materials
			if(save_indigo_scene)
			{
				/*for(int i=0; i<24; ++i)
					indigo_xml +=
					"<material>									  \n"
					"	<name>mat " + toString(i) + "</name>		\n"
					"	<uid>" + toString(100 + i) + "</uid>		\n"
					"											  \n"
					"	<phong>									  \n"
					"		<diffuse>" + toString(cv_mats[i].albedo_rgb.r) + " " + toString(cv_mats[i].albedo_rgb.g) + " " + toString(cv_mats[i].albedo_rgb.b) + "</diffuse>\n"
					"		<texture>								   \n"
					"			<path>" + cv_mats[i].albedo_tex_path + "</path>				   \n"
					"		</texture>								   \n"
					"	<ior>1.5</ior>								  \n"
					"	<exponent>10000</exponent>					  \n"
					"</phong>									  \n"
					"</material>								  \n";*/
				for(int i=0; i<16; ++i)
					if(i != 2)
						indigo_xml +=
						"<material>									  \n"
						"	<name>mat " + toString(i) + "</name>		\n"
						"	<uid>" + toString(100 + i) + "</uid>		\n"
						"	<diffuse>										  \n"
						"		<albedo>									   \n"
						"			<texture>						  \n"
						"				<path>" + cv_mats[i].albedo_tex_path + "</path>				   \n"
						"			</texture>							  \n"
						"		</albedo>									   \n"
						"	</diffuse>										\n"
						"</material>									  \n";

				for(int i=16; i<24; ++i)
					indigo_xml +=
					"<material>									  \n"
					"	<name>mat " + toString(i) + "</name>		\n"
					"	<uid>" + toString(100 + i) + "</uid>		\n"
					"	<diffuse>										  \n"
					"		<albedo>									   \n"
					"			<constant>								   \n"
					"				<rgb>								   \n"
					"					<rgb>" + toString(cv_mats[i].albedo_rgb.r) + " " + toString(cv_mats[i].albedo_rgb.g) + " " + toString(cv_mats[i].albedo_rgb.b) + "</rgb>			   \n"
					"				</rgb>								   \n"
					"			</constant>								   \n"
					"		</albedo>									   \n"
					"	</diffuse>										\n"
					"</material>									  \n";

				// Make mat 2 specular
				indigo_xml +=
					"<medium>																\n"
					"	<name>medium1</name>												\n"
					"																		\n"
					"	<basic>																\n"
					"		<ior>1.5</ior>													\n"
					"		<cauchy_b_coeff>0.0</cauchy_b_coeff>							\n"
					"		<absorption_coefficient_spectrum>								\n"
					"			<rgb>														   \n"
					"				<rgb>1 0.3 0.1</rgb>									   \n"
					"			</rgb>														   \n"
					"		</absorption_coefficient_spectrum>								\n"
					"	</basic>															\n"
					"</medium>																\n"
					"																		\n"
					"<material>																\n"
					"	<name>mat " + toString(2) + "</name>		\n"
					"	<uid>" + toString(100 + 2) + "</uid>		\n"
					"																		\n"
					"	<specular>															\n"
					"		<transparent>true</transparent>									\n"
					"		<internal_medium_name>medium1</internal_medium_name>			\n"
					"	</specular>															\n"
					"</material>															\n";

				/*" 
				<absorption_layer_transmittance>										\n"
					"	<constant>															\n"
					"	  <rgb>																\n"
					"		<rgb>1 0 0</rgb>												\n"
					"		<gamma>2.2</gamma>												\n"
					"	  </rgb>															\n"
					"	</constant>															\n"
					"  </absorption_layer_transmittance>									\n"
					*/

			}																		

			Timer timer;
			JSONParser parser;
			parser.parseFile("D:\\downloads\\parcels.json");

			std::vector<uint16> voxel_data;
			voxel_data.resize(1000000);

			std::vector<unsigned char> data;

			assert(parser.nodes[0].type == JSONNode::Type_Object);

			int total_num_voxels = 0;
			int custom_uid = 500000;

			const JSONNode& parcels_array = parser.nodes[0].getChildArray(parser, "parcels");

			conPrint("Num parcels: " + toString(parcels_array.child_indices.size()));

			for(size_t q=0; q<parcels_array.child_indices.size(); ++q)
			{
				const JSONNode& parcel_node = parser.nodes[parcels_array.child_indices[q]];

				int x1, y1, z1, x2, y2, z2, id;
				x1 = y1 = z1 = x2 = y2 = z2 = id = 0;

				std::vector<OpenGLMaterial> parcel_mats = cv_mats; // Copy mats as may be updated with custom mats for this parcel.
				bool custom_palette_used = false;

				for(size_t w=0; w<parcel_node.name_val_pairs.size(); ++w)
				{
					if(parcel_node.name_val_pairs[w].name == "voxels")
					{
						const JSONNode& voxel_node = parser.nodes[parcel_node.name_val_pairs[w].value_node_index];

						assert(voxel_node.type == JSONNode::Type_String);
						
						Base64::decode(voxel_node.string_v, data);

						// Allocate deflate state
						z_stream stream;
						stream.zalloc = Z_NULL;
						stream.zfree = Z_NULL;
						stream.opaque = Z_NULL;
						stream.next_in = (Bytef*)data.data();
						stream.avail_in = (unsigned int)data.size();

						int ret = inflateInit(&stream);
						if(ret != Z_OK)
							throw Indigo::Exception("inflateInit failed.");

						stream.next_out = (Bytef*)voxel_data.data();
						stream.avail_out = (unsigned int)(voxel_data.size() * sizeof(uint16));

						int result = inflate(&stream, Z_FINISH);
						if(result != Z_STREAM_END)
							throw Indigo::Exception("inflate failed.");

						inflateEnd(&stream);
					}
					else if(parcel_node.name_val_pairs[w].name == "x1")
						x1 = (int)parser.nodes[parcel_node.name_val_pairs[w].value_node_index].getDoubleValue();
					else if(parcel_node.name_val_pairs[w].name == "y1")
						y1 = (int)parser.nodes[parcel_node.name_val_pairs[w].value_node_index].getDoubleValue();
					else if(parcel_node.name_val_pairs[w].name == "z1")
						z1 = (int)parser.nodes[parcel_node.name_val_pairs[w].value_node_index].getDoubleValue();
					else if(parcel_node.name_val_pairs[w].name == "x2")
						x2 = (int)parser.nodes[parcel_node.name_val_pairs[w].value_node_index].getDoubleValue();
					else if(parcel_node.name_val_pairs[w].name == "y2")
						y2 = (int)parser.nodes[parcel_node.name_val_pairs[w].value_node_index].getDoubleValue();
					else if(parcel_node.name_val_pairs[w].name == "z2")
						z2 = (int)parser.nodes[parcel_node.name_val_pairs[w].value_node_index].getDoubleValue();
					else if(parcel_node.name_val_pairs[w].name == "id")
						id = (int)parser.nodes[parcel_node.name_val_pairs[w].value_node_index].getDoubleValue();
					else if(parcel_node.name_val_pairs[w].name == "palette")
					{
						// Load custom palette, e.g. "palette":["#ffffff","#888888","#000000","#80ffff","#01cdfe","#0080ff","#008080","#004080"]
						const JSONNode& palette_array = parser.nodes[parcel_node.name_val_pairs[w].value_node_index];

						if(palette_array.type != JSONNode::Type_Null)
						{
							if(palette_array.child_indices.size() != 8)
								throw Indigo::Exception("Invalid number of colours in palette.");

							for(size_t p=0; p<8; ++p) // For each palette entry
							{
								const JSONNode& col_node = parser.nodes[palette_array.child_indices[p]];
								assert(col_node.type == JSONNode::Type_String);

								parcel_mats[16 + p].albedo_rgb = Colour3f(
										hexStringToUInt32(std::string(col_node.string_v).substr(1, 2)) / 255.0f,
										hexStringToUInt32(std::string(col_node.string_v).substr(3, 2)) / 255.0f,
										hexStringToUInt32(std::string(col_node.string_v).substr(5, 2)) / 255.0f
									);
							}

							custom_palette_used = true;
						}
					}
				}

				//if(id != 1045) continue;

				// At this point hopefully we have parsed voxel data and coords
				const int xspan = x2 - x1;
				const int yspan = y2 - y1;
				const int zspan = z2 - z1;

				const int voxels_x = xspan * 2;
				const int voxels_y = yspan * 2;
				const int voxels_z = zspan * 2;

				const int expected_num_voxels = voxels_x * voxels_y * voxels_z;

				//assert(expected_num_voxels == voxel_data.size());

				// Do a pass over voxels to get list of used mats
				std::vector<bool> mat_used(16 + 8);

				for(int x=0; x<expected_num_voxels; ++x)
				{
					const uint16 v = voxel_data[x];
					if(v != 0)
					{
						int mat_index;
						if((v >> 5) & 0x7)
							mat_index = 16 + ((v >> 5) & 0x7); // flat colour mat
						else
							mat_index = v & 0xF; // texture mat

						mat_used[mat_index] = true;
					}
				}

				// Make material array and material indices in material array
				std::vector<OpenGLMaterial> used_mats;
				std::vector<int> used_mat_index(24);
				for(int i=0; i<24; ++i)
					if(mat_used[i])
					{
						used_mat_index[i] = (int)used_mats.size();
						used_mats.push_back(parcel_mats[i]);
					}

				VoxelGroup voxel_group;
				int read_i = 0;
				for(int x=x1; x<x1+voxels_x; ++x)
					for(int y=y1; y<y1+voxels_y; ++y)
						for(int z=z1; z<z1+voxels_z; ++z)
						{
							const uint16 v = voxel_data[read_i++];
							if(v != 0)
							{
								int mat_index;
								if((v >> 5) & 0x7)
									mat_index = 16 + ((v >> 5) & 0x7); // flat colour mat
								else
									mat_index = v & 0xF; // texture mat

								const int final_mat_index = used_mat_index[mat_index];

								// Get relative xyz in CV coords (y-up, left-handed)
								const int rx = x - x1;
								const int ry = y - y1;
								const int rz = z - z1;

								// Convert to substrata coords (z-up)
								const int use_x = -rx;
								const int use_y = -rz;
								const int use_z = ry;
								voxel_group.voxels.push_back(Voxel(Vec3<int>(use_x, use_y, use_z), final_mat_index));
							}
						}

				assert(read_i == expected_num_voxels);

				if(voxel_group.voxels.size() > 0)
				{
					Reference<RayMesh> raymesh;
					Reference<OpenGLMeshRenderData> gl_meshdata;
					gl_meshdata = ModelLoading::makeModelForVoxelGroup(voxel_group, mw.task_manager, raymesh);

					// Convert to substrata coords (z-up)
					const int use_x = -x1;
					const int use_y = -z1;
					const int use_z = y1;

					// Scale matrix is 0.5 as voxels are 0.5 m wide in CV.
					GLObjectRef gl_ob = new GLObject();
					gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix((float)use_x, (float)use_y, (float)use_z) * Matrix4f::uniformScaleMatrix(0.5f);
					gl_ob->mesh_data = gl_meshdata;
					
					gl_ob->materials = used_mats;

					mw.ui->glWidget->addObject(gl_ob);

					
					if(save_indigo_scene)
					{
						// Write out any custom materials
						int ob_custom_uid_start = custom_uid;
						if(custom_palette_used)
						{
							// Save materials for custom palette.
							for(int i=16; i<24; ++i)
							{
								indigo_xml +=
									"<material>									  \n"
									"	<name>custom palette mat " + toString(custom_uid) + "</name>		\n"
									"	<uid>" + toString(custom_uid) + "</uid>		\n"
									"	<diffuse>										  \n"
									"		<albedo>									   \n"
									"			<constant>								   \n"
									"				<rgb>								   \n"
									"					<rgb>" + toString(parcel_mats[i].albedo_rgb.r) + " " + toString(parcel_mats[i].albedo_rgb.g) + " " + toString(parcel_mats[i].albedo_rgb.b) + "</rgb>\n"
									"				</rgb>								   \n"
									"			</constant>								   \n"
									"		</albedo>									   \n"
									"	</diffuse>										\n"
									"</material>									  \n";
								custom_uid++;
							}
						}

						const std::string mesh_path = scene_dir + "/mesh_" + toString(id) + ".igmesh";
						raymesh->saveToIndigoMeshOnDisk(mesh_path, /*use_compression=*/true);

						indigo_xml +=
							"	<mesh>											\n"
							"	<name>mesh_" + toString(id) + "</name>			\n"
							"	<uid>" + toString(1000000 + id) + "</uid>	\n"
							"	<scale>1</scale>								\n"
							"	<normal_smoothing>false</normal_smoothing>		\n"
							"	<external>										\n"
							"		<path>" + mesh_path + "</path>							\n"
							"	</external>										\n"
							"	</mesh>											\n";

						indigo_xml +=
							"<model2>											  \n"
							"	<uid>" + toString(2000000 + id) + "</uid>	\n"
							"	<name>parcel #" + toString(id) + "</name>			\n"
							"	<geometry_uid>" + toString(1000000 + id) + "</geometry_uid>					  \n"
							"	<scale>0.5</scale>								  \n"
							"	<rotation>										  \n"
							"		<matrix>									  \n"
							"			1 0 0 0 1 0 0 0 1 						  \n"
							"		</matrix>									  \n"
							"	</rotation>										  \n"
							"	<keyframe>										  \n"
							"		<time>0</time>								  \n"
							"		<pos>" + toString(use_x) + " " + toString(use_y) + " " + toString(use_z) + "</pos>							  \n"
							"		<rotation_quaternion>						  \n"
							"			<axis>1 0 0</axis>						  \n"
							"			<angle>0</angle>						  \n"
							"		</rotation_quaternion>						  \n"
							"	</keyframe>										  \n"
							"	<materials>										  \n";

						for(int i=0; i<24; ++i)
							if(mat_used[i])
							{
								//const int mat_index = used_mat_index[i];
								int mat_uid = 100 + i;// mat_index;

								if(custom_palette_used && i >= 16)
									mat_uid = ob_custom_uid_start + (i - 16);

								indigo_xml += "		<material_uid>" + toString(mat_uid) + "</material_uid>				  \n";
							}
						indigo_xml +=
							"	</materials>									  \n"
							"</model2>											  \n";
					}														


					total_num_voxels += (int)voxel_group.voxels.size();
				}
			}

			if(save_indigo_scene)
			{
				indigo_xml += "</scene>\n";
				FileUtils::writeEntireFileTextMode(scene_dir + "/scene.igs", indigo_xml);
			}

			conPrint("Loaded all voxel data in " + timer.elapsedString());
			conPrint("total_num_voxels " + toString(total_num_voxels));
		}



		
		// Load a test overlay quad
		if(false)
		{
			OverlayObjectRef ob = new OverlayObject();

			ob->ob_to_world_matrix.setToUniformScaleMatrix(0.95f);

			ob->material.albedo_rgb = Colour3f(0.7f, 0.2f, 0.2f);
			ob->material.alpha = 1.f;
			ob->material.albedo_tex_path = "N:\\indigo\\trunk\\testscenes\\ColorChecker_sRGB_from_Ref.jpg";
			ob->material.tex_matrix = Matrix2f(1, 0, 0, -1); // OpenGL expects texture data to have bottom left pixel at offset 0, we have top left pixel, so flip

			ob->mesh_data = OpenGLEngine::makeOverlayQuadMesh();

			mw.ui->glWidget->addOverlayObject(ob);
		}


		/*
		Load a ground plane into the GL engine
		*/
		{
			// Build Indigo::Mesh
			mw.ground_quad_mesh = new Indigo::Mesh();
			mw.ground_quad_mesh->num_uv_mappings = 1;

			// Tessalate ground mesh, to avoid texture shimmer due to large quads.
			const int N = 10;
			mw.ground_quad_mesh->vert_positions.reserve(N * N);
			mw.ground_quad_mesh->vert_normals.reserve(N * N);
			mw.ground_quad_mesh->uv_pairs.reserve(N * N);
			mw.ground_quad_mesh->quads.reserve(N * N);

			for(int y=0; y<N; ++y)
			{
				const float v = (float)y/((float)N - 1);
				for(int x=0; x<N; ++x)
				{
					const float u = (float)x/((float)N - 1);
					mw.ground_quad_mesh->vert_positions.push_back(Indigo::Vec3f(u * (float)ground_quad_w, v * (float)ground_quad_w, 0.f));
					mw.ground_quad_mesh->vert_normals.push_back(Indigo::Vec3f(0, 0, 1));
					mw.ground_quad_mesh->uv_pairs.push_back(Indigo::Vec2f(u * (float)ground_quad_w, v * (float)ground_quad_w));

					if(x < N-1 && y < N-1)
					{
						mw.ground_quad_mesh->quads.push_back(Indigo::Quad());
						mw.ground_quad_mesh->quads.back().mat_index = 0;
						mw.ground_quad_mesh->quads.back().vertex_indices[0] = mw.ground_quad_mesh->quads.back().uv_indices[0] = y    *N + x;
						mw.ground_quad_mesh->quads.back().vertex_indices[1] = mw.ground_quad_mesh->quads.back().uv_indices[1] = y    *N + x+1;
						mw.ground_quad_mesh->quads.back().vertex_indices[2] = mw.ground_quad_mesh->quads.back().uv_indices[2] = (y+1)*N + x+1;
						mw.ground_quad_mesh->quads.back().vertex_indices[3] = mw.ground_quad_mesh->quads.back().uv_indices[3] = (y+1)*N + x;
					}
				}
			}

			mw.ground_quad_mesh->endOfModel();

			// Build OpenGLMeshRenderData
			mw.ground_quad_mesh_opengl_data = OpenGLEngine::buildIndigoMesh(mw.ground_quad_mesh, false);

			// Build RayMesh (for physics)
			mw.ground_quad_raymesh = new RayMesh("mesh", false);
			mw.ground_quad_raymesh->fromIndigoMesh(*mw.ground_quad_mesh);

			mw.ground_quad_raymesh->buildTrisFromQuads();
			Geometry::BuildOptions options;
			mw.ground_quad_raymesh->build(options, mw.print_output, false, mw.task_manager);
		}

		mw.updateGroundPlane();


		// Make hypercard physics mesh
		{
			mw.hypercard_quad_raymesh = new RayMesh("quad", false);
			mw.hypercard_quad_raymesh->addVertex(Vec3f(0, 0, 0));
			mw.hypercard_quad_raymesh->addVertex(Vec3f(1, 0, 0));
			mw.hypercard_quad_raymesh->addVertex(Vec3f(1, 0, 1));
			mw.hypercard_quad_raymesh->addVertex(Vec3f(0, 0, 1));

			unsigned int uv_i[] ={ 0, 0, 0, 0 };
			unsigned int v_i[]  ={ 0, 3, 2, 1 };
			mw.hypercard_quad_raymesh->addQuad(v_i, uv_i, 0);

			mw.hypercard_quad_raymesh->buildTrisFromQuads();
			Geometry::BuildOptions options;
			mw.hypercard_quad_raymesh->build(options, mw.print_output, false, mw.task_manager);
		}

		mw.hypercard_quad_opengl_mesh = OpenGLEngine::makeQuadMesh(Vec4f(1, 0, 0, 0), Vec4f(0, 0, 1, 0));

		// Make unit-cube raymesh (used for placeholder model)
		{
			mw.unit_cube_raymesh = new RayMesh("mesh", false);
			mw.unit_cube_raymesh->addVertex(Vec3f(0, 0, 0));
			mw.unit_cube_raymesh->addVertex(Vec3f(1, 0, 0));
			mw.unit_cube_raymesh->addVertex(Vec3f(1, 1, 0));
			mw.unit_cube_raymesh->addVertex(Vec3f(0, 1, 0));
			mw.unit_cube_raymesh->addVertex(Vec3f(0, 0, 1));
			mw.unit_cube_raymesh->addVertex(Vec3f(1, 0, 1));
			mw.unit_cube_raymesh->addVertex(Vec3f(1, 1, 1));
			mw.unit_cube_raymesh->addVertex(Vec3f(0, 1, 1));

			unsigned int uv_i[] ={ 0, 0, 0, 0 };
			{
				unsigned int v_i[] ={ 0, 3, 2, 1 };
				mw.unit_cube_raymesh->addQuad(v_i, uv_i, 0); // z = 0 quad
			}
			{
				unsigned int v_i[] ={ 4, 5, 6, 7 };
				mw.unit_cube_raymesh->addQuad(v_i, uv_i, 0); // z = 1 quad
			}
			{
				unsigned int v_i[] ={ 0, 1, 5, 4 };
				mw.unit_cube_raymesh->addQuad(v_i, uv_i, 0); // y = 0 quad
			}
			{
				unsigned int v_i[] ={ 2, 3, 7, 6 };
				mw.unit_cube_raymesh->addQuad(v_i, uv_i, 0); // y = 1 quad
			}
			{
				unsigned int v_i[] ={ 0, 4, 7, 3 };
				mw.unit_cube_raymesh->addQuad(v_i, uv_i, 0); // x = 0 quad
			}
			{
				unsigned int v_i[] ={ 1, 2, 6, 5 };
				mw.unit_cube_raymesh->addQuad(v_i, uv_i, 0); // x = 1 quad
			}

			mw.unit_cube_raymesh->buildTrisFromQuads();
			Geometry::BuildOptions options;
			mw.unit_cube_raymesh->build(options, mw.print_output, /*verbose=*/false, mw.task_manager);
		}

		// Make object-placement beam model
		{
			mw.ob_placement_beam = new GLObject();
			mw.ob_placement_beam->ob_to_world_matrix = Matrix4f::identity();
			mw.ob_placement_beam->mesh_data = mw.ui->glWidget->opengl_engine->getCylinderMesh();

			OpenGLMaterial material;
			material.albedo_rgb = Colour3f(0.3f, 0.8f, 0.3f);
			material.transparent = true;
			material.alpha = 0.9f;

			mw.ob_placement_beam->materials = std::vector<OpenGLMaterial>(1, material);

			// Make object-placement beam hit marker out of a sphere.
			mw.ob_placement_marker = new GLObject();
			mw.ob_placement_marker->ob_to_world_matrix = Matrix4f::identity();
			mw.ob_placement_marker->mesh_data = mw.ui->glWidget->opengl_engine->getSphereMeshData();

			mw.ob_placement_marker->materials = std::vector<OpenGLMaterial>(1, material);
		}

		{
			// Make ob_denied_move_marker
			mw.ob_denied_move_marker = new GLObject();
			mw.ob_denied_move_marker->ob_to_world_matrix = Matrix4f::identity();
			mw.ob_denied_move_marker->mesh_data = mw.ui->glWidget->opengl_engine->getSphereMeshData();

			OpenGLMaterial material;
			material.albedo_rgb = Colour3f(0.8f, 0.2f, 0.2f);
			material.transparent = true;
			material.alpha = 0.9f;

			mw.ob_denied_move_marker->materials = std::vector<OpenGLMaterial>(1, material);
		}

		// Make voxel_edit_marker model
		{
			mw.voxel_edit_marker = new GLObject();
			mw.voxel_edit_marker->ob_to_world_matrix = Matrix4f::identity();
			mw.voxel_edit_marker->mesh_data = mw.ui->glWidget->opengl_engine->getCubeMeshData();

			OpenGLMaterial material;
			material.albedo_rgb = Colour3f(0.3f, 0.8f, 0.3f);
			material.transparent = true;
			material.alpha = 0.3f;

			mw.voxel_edit_marker->materials = std::vector<OpenGLMaterial>(1, material);
		}

		// Make voxel_edit_face_marker model
		{
			mw.voxel_edit_face_marker = new GLObject();
			mw.voxel_edit_face_marker->ob_to_world_matrix = Matrix4f::identity();
			mw.voxel_edit_face_marker->mesh_data = mw.ui->glWidget->opengl_engine->makeUnitQuadMesh();

			OpenGLMaterial material;
			material.albedo_rgb = Colour3f(0.3f, 0.8f, 0.3f);
			mw.voxel_edit_face_marker->materials = std::vector<OpenGLMaterial>(1, material);
		}

		// Make shader for parcels
		{
			const std::string use_shader_dir = cyberspace_base_dir_path + "/data/shaders";
			mw.parcel_shader_prog = new OpenGLProgram(
				"parcel hologram prog",
				new OpenGLShader(use_shader_dir + "/parcel_vert_shader.glsl", "", GL_VERTEX_SHADER),
				new OpenGLShader(use_shader_dir + "/parcel_frag_shader.glsl", "", GL_FRAGMENT_SHADER)
			);
			// Let any Indigo::Exception thrown fall through to below.
		}

		try
		{
			// TEMP: make a parcel
			if(false)
			{
				ParcelRef parcel = new Parcel();
				parcel->id = ParcelID(0);
				parcel->owner_id = UserID(0);
				parcel->admin_ids.push_back(UserID(0));
				parcel->created_time = TimeStamp::currentTime();
				parcel->description = " a parcel";
				parcel->owner_name = "the owner";
				parcel->verts[0] = Vec2d(10, 10);
				parcel->verts[1] = Vec2d(20, 12);
				parcel->verts[2] = Vec2d(18, 20);
				parcel->verts[3] = Vec2d(11, 18);
				parcel->zbounds = Vec2d(-0.1, 30);
				parcel->build();

				mw.world_state->parcels[parcel->id] = parcel;
			}

			// TEMP: make an avatar
			if(false)
			{
				test_avatar = new AvatarGraphics();
				test_avatar->create(*mw.ui->glWidget->opengl_engine);
				test_avatar->setOverallTransform(*mw.ui->glWidget->opengl_engine, Vec3d(0, 3, 2.67), Vec3f(0, 0, 1), 0.0);
			}

			// Load a wedge
			if(false)
			{
				Indigo::MeshRef mesh = new Indigo::Mesh();
				Indigo::Mesh::readFromFile("resources/wedge.igmesh", *mesh);

				GLObjectRef ob = new GLObject();
				ob->materials.resize(1);
				ob->materials[0].albedo_rgb = Colour3f(0.6f, 0.2f, 0.2f);
				ob->materials[0].fresnel_scale = 1;
				ob->materials[0].roughness = 0.3f;

				ob->ob_to_world_matrix = Matrix4f::translationMatrix(10, 10, 0) * Matrix4f::uniformScaleMatrix(100.f);
				ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh, false);

				mw.ui->glWidget->addObject(ob);

				mw.physics_world->addObject(makePhysicsObject(mesh, ob->ob_to_world_matrix, mw.print_output, mw.task_manager));
			}

			if(false)
			{
				Indigo::MeshRef mesh;
				std::vector<WorldMaterialRef> loaded_materials;

				const std::string path = "C:\\Users\\nick\\Downloads\\cemetery_angel_-_miller\\scene.gltf";

				GLObjectRef ob = ModelLoading::makeGLObjectForModelFile(path,
					Matrix4f::translationMatrix(12, 3, 0) * Matrix4f::uniformScaleMatrix(1.f),
					mesh,
					loaded_materials
				);

				mw.ui->glWidget->addObject(ob);

				//mw.physics_world->addObject(makePhysicsObject(mesh, ob->ob_to_world_matrix, mw.print_output, mw.task_manager));
			}

			if(false)
			{
				Indigo::MeshRef mesh;
				std::vector<WorldMaterialRef> loaded_materials;

				const std::string path = "C:\\Users\\nick\\Downloads\\scifi_girl_v.01\\scene.gltf";

				GLObjectRef ob = ModelLoading::makeGLObjectForModelFile(path,
					Matrix4f::translationMatrix(9, 3, 0) * Matrix4f::uniformScaleMatrix(0.1f),
					mesh,
					loaded_materials
				);

				mw.ui->glWidget->addObject(ob);

				//mw.physics_world->addObject(makePhysicsObject(mesh, ob->ob_to_world_matrix, mw.print_output, mw.task_manager));
			}
		}
		catch(Indigo::Exception& e)
		{
			conPrint(e.what());
			QMessageBox msgBox;
			msgBox.setText(QtUtils::toQString(e.what()));
			msgBox.exec();
			return 1;
		}

		mw.afterGLInitInitialise();


		mw.physics_world->rebuild(mw.task_manager, mw.print_output);

		return app.exec();
	}
	catch(ArgumentParserExcep& e)
	{
		// Show error
		conPrint(e.what());
		QErrorMessage m;
		m.showMessage(QtUtils::toQString(e.what()));
		m.exec();
		return 1;
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
	catch(Indigo::Exception& e)
	{
		// Show error
		conPrint(e.what());
		QErrorMessage m;
		m.showMessage(QtUtils::toQString(e.what()));
		m.exec();
		return 1;
	}
}
