
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif


#ifdef _MSC_VER // Qt headers suppress some warnings on Windows, make sure the warning suppression doesn't propagate to our code. See https://bugreports.qt.io/browse/QTBUG-26877
#pragma warning(push, 0) // Disable warnings
#endif
#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "AvatarSettingsDialog.h"
#include "AddObjectDialog.h"
#include "ModelLoading.h"
#include "UploadResourceThread.h"
#include "DownloadResourcesThread.h"
//#include "IndigoApplication.h"
#include <QtCore/QTimer>
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
#ifdef _MSC_VER
#pragma warning(pop) // Re-enable warnings
#endif
#include "GuiClientApplication.h"
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
#include "../networking/networking.h"
#include "../qt/QtUtils.h"
#include "../graphics/formatdecoderobj.h"
#include "../dll/include/IndigoMesh.h"
#include "../dll/IndigoStringUtils.h"
#include "../indigo/TextureServer.h"
#include "../indigo/ThreadContext.h"
#include <clocale>
#include "../utils/StandardPrintOutput.h"

//TEMP:
#include "../utils/ReferenceTest.h"


#if defined(_WIN32) || defined(_WIN64)
#else
#include <signal.h>
#endif


// If we are building on Windows, and we are not in Release mode (e.g. BUILD_TESTS is enabled), then make sure the console window is shown.
#if defined(_WIN32) && defined(BUILD_TESTS)
#pragma comment(linker, "/SUBSYSTEM:CONSOLE")
#endif


static int64 next_uid = 10000;//TEMP HACK


static const std::string server_hostname = "127.0.0.1"; // "217.155.32.43";
const int server_port = 7654;


MainWindow::MainWindow(const std::string& base_dir_path_, const std::string& appdata_path_, const ArgumentParser& args, QWidget *parent)
:	base_dir_path(base_dir_path_),
	appdata_path(appdata_path_),
	parsed_args(args), 
	QMainWindow(parent)
{
	ui = new Ui::MainWindow();
	ui->setupUi(this);

	settings = new QSettings("Glare Technologies", "Cyberspace");

	// Load main window geometry and state
	this->restoreGeometry(settings->value("mainwindow/geometry").toByteArray());
	this->restoreState(settings->value("mainwindow/windowState").toByteArray());

	connect(ui->chatPushButton, SIGNAL(clicked()), this, SLOT(sendChatMessageSlot()));
	connect(ui->chatMessageLineEdit, SIGNAL(returnPressed()), this, SLOT(sendChatMessageSlot()));
	connect(ui->glWidget, SIGNAL(mouseClickedSignal(QMouseEvent*)), this, SLOT(glWidgetMouseClicked(QMouseEvent*)));
	connect(ui->glWidget, SIGNAL(mouseDoubleClickedSignal(QMouseEvent*)), this, SLOT(glWidgetMouseDoubleClicked(QMouseEvent*)));
	connect(ui->glWidget, SIGNAL(mouseMoved(QMouseEvent*)), this, SLOT(glWidgetMouseMoved(QMouseEvent*)));
	connect(ui->glWidget, SIGNAL(keyPressed(QKeyEvent*)), this, SLOT(glWidgetKeyPressed(QKeyEvent*)));
	connect(ui->glWidget, SIGNAL(mouseWheelSignal(QWheelEvent*)), this, SLOT(glWidgetMouseWheelEvent(QWheelEvent*)));
	connect(ui->objectEditor, SIGNAL(objectChanged()), this, SLOT(objectEditedSlot()));

	this->resources_dir = "resources"; // "./resources_" + toString(PlatformUtils::getProcessID());
	FileUtils::createDirIfDoesNotExist(this->resources_dir);

	conPrint("resources_dir: " + resources_dir);
	resource_manager = new ResourceManager(this->resources_dir);
}


void MainWindow::initialise()
{
	setWindowTitle("Cyberspace");

	ui->objectEditor->setControlsEnabled(false);

	timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(timerEvent()));
    timer->start(10);
}


MainWindow::~MainWindow()
{
	// Save main window geometry and state
	settings->setValue("mainwindow/geometry", saveGeometry());
	settings->setValue("mainwindow/windowState", saveState());

	// Kill ClientThread
	conPrint("killing ClientThread");
	this->client_thread->killConnection();
	this->client_thread = NULL;
	this->client_thread_manager.killThreadsBlocking();
	conPrint("killed ClientThread");

	ui->glWidget->makeCurrent();

	delete ui;
}


Reference<GLObject> globe;


static Reference<PhysicsObject> makePhysicsObject(Indigo::MeshRef mesh, const Matrix4f& ob_to_world_matrix, StandardPrintOutput& print_output, Indigo::TaskManager& task_manager)
{
	Reference<PhysicsObject> phy_ob = new PhysicsObject();
	phy_ob->geometry = new RayMesh("mesh", false);
	phy_ob->geometry->fromIndigoMesh(*mesh);
				
	phy_ob->geometry->buildTrisFromQuads();
	Geometry::BuildOptions options;
	options.bih_tri_threshold = 1;
	options.cache_trees = false;
	phy_ob->geometry->build(".", options, print_output, false, task_manager);

	phy_ob->geometry->buildJSTris();
				
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


void MainWindow::timerEvent()
{
	const float dt = time_since_last_timer_ev.elapsed();
	time_since_last_timer_ev.reset();

	// Handle any messages (chat messages etc..)
	{
		Lock lock(this->msg_queue.getMutex());
		while(!msg_queue.unlockedEmpty())
		{
			Reference<ThreadMessage> msg;
			this->msg_queue.unlockedDequeue(msg);

			if(dynamic_cast<const ChatMessage*>(msg.getPointer()))
			{
				const ChatMessage* m = static_cast<const ChatMessage*>(msg.getPointer());
				ui->chatMessagesTextEdit->append(QtUtils::toQString(m->name + ": " + m->msg));
			}
			else if(dynamic_cast<const GetFileMessage*>(msg.getPointer()))
			{
				const GetFileMessage* m = static_cast<const GetFileMessage*>(msg.getPointer());

				if(ResourceManager::isValidURL(m->URL))
				{
					const std::string path = resource_manager->pathForURL(m->URL);
					if(FileUtils::fileExists(path))
						resource_upload_thread_manager.addThread(new UploadResourceThread(&this->msg_queue, path, m->URL, server_hostname, server_port));
				}
			}
			else if(dynamic_cast<const ResourceDownloadedMessage*>(msg.getPointer()))
			{
				const ResourceDownloadedMessage* m = static_cast<const ResourceDownloadedMessage*>(msg.getPointer());

				// Since we have a new downloaded resource, iterate over objects and avatars and if they were using a placeholder model for this resource, load the proper model.
				try
				{
					Lock lock(this->world_state->mutex);

					for(auto it = this->world_state->objects.begin(); it != this->world_state->objects.end(); ++it)
					{
						WorldObject* ob = it->second.getPointer();

						if(ob->using_placeholder_model)
						{
							std::vector<std::string> dependency_URLs;
							ob->appendDependencyURLs(dependency_URLs);
							std::set<std::string> URL_set(dependency_URLs.begin(), dependency_URLs.end());

							if(URL_set.count(m->URL)) // If the downloaded resource was used by this model:
							{
								std::map<std::string, std::string> paths_for_URLs;

								// Do we have all the dependencies for this model downloaded?
								bool all_downloaded = true;
								for(size_t i=0; i<dependency_URLs.size(); ++i)
								{
									const std::string path = this->resource_manager->pathForURL(dependency_URLs[i]);
									paths_for_URLs[dependency_URLs[i]] = path;

									if(!FileUtils::fileExists(path))
										all_downloaded = false;
								}

								if(all_downloaded)
								{
									// Remove placeholder GL object
									assert(ob->opengl_engine_ob.nonNull());
									ui->glWidget->opengl_engine->removeObject(ob->opengl_engine_ob);

									conPrint("Adding Object to OpenGL Engine, UID " + toString(ob->uid.value()));
									//const std::string path = resources_dir + "/" + ob->model_url;
									const std::string path = this->resource_manager->pathForURL(ob->model_url);

									// Make GL object, add to OpenGL engine
									Indigo::MeshRef mesh;
									const Matrix4f ob_to_world_matrix = Matrix4f::translationMatrix((float)ob->pos.x, (float)ob->pos.y, (float)ob->pos.z) * 
										Matrix4f::rotationMatrix(normalise(ob->axis.toVec4fVector()), ob->angle);
									GLObjectRef gl_ob = ModelLoading::makeGLObjectForModelFile(path, ob->materials, /*paths_for_URLs*/*this->resource_manager, ob_to_world_matrix, mesh);
									ob->opengl_engine_ob = gl_ob;
									ui->glWidget->addObject(gl_ob);

									// Make physics object
									StandardPrintOutput print_output;
									Indigo::TaskManager task_manager;
									PhysicsObjectRef physics_ob = makePhysicsObject(mesh, ob_to_world_matrix, print_output, task_manager);
									ob->physics_object = physics_ob;
									physics_ob->userdata = ob;
									physics_world->addObject(physics_ob);

									ob->using_placeholder_model = false;
								}
							}
						}
					}

					for(auto it = this->world_state->avatars.begin(); it != this->world_state->avatars.end(); ++it)
					{
						Avatar* avatar = it->second.getPointer();
						if(avatar->using_placeholder_model && (avatar->model_url == m->URL))
						{
							// Remove placeholder GL object
							assert(avatar->opengl_engine_ob.nonNull());
							ui->glWidget->opengl_engine->removeObject(avatar->opengl_engine_ob);

							conPrint("Adding Object to OpenGL Engine, UID " + toString(avatar->uid.value()));
							//const std::string path = resources_dir + "/" + ob->model_url;
							const std::string path = this->resource_manager->pathForURL(avatar->model_url);

							// Make GL object, add to OpenGL engine
							Indigo::MeshRef mesh;
							const Matrix4f ob_to_world_matrix = Matrix4f::translationMatrix((float)avatar->pos.x, (float)avatar->pos.y, (float)avatar->pos.z) * 
								Matrix4f::rotationMatrix(normalise(avatar->axis.toVec4fVector()), avatar->angle);
							GLObjectRef gl_ob = ModelLoading::makeGLObjectForModelFile(path, ob_to_world_matrix, mesh);
							avatar->opengl_engine_ob = gl_ob;
							ui->glWidget->addObject(gl_ob);

							avatar->using_placeholder_model = false;
						}
					}
				}
				catch(Indigo::Exception& e)
				{
					conPrint(e.what());

				}
			}
		}
	}


	// Update spinning globe
	if(globe.nonNull())
	{
		Matrix4f scale;
		scale.setToUniformScaleMatrix(1.0f);
		Matrix4f rot;
		rot.setToRotationMatrix(Vec4f(0,0,1,0), total_timer.elapsed() * 0.3f);
		mul(scale, rot, globe->ob_to_world_matrix);

		globe->ob_to_world_matrix.setColumn(3, Vec4f(-1.f, 0.f, 1.5f, 1));
	
		ui->glWidget->opengl_engine->updateObjectTransformData(*globe.getPointer());
	}

	ui->glWidget->playerPhyicsThink();

	// Process player physics
	Vec4f campos = this->cam_controller.getPosition().toVec4fPoint();
	player_physics.update(*this->physics_world, dt, campos);
	this->cam_controller.setPosition(toVec3d(campos));

	// Update avatar graphics
	try
	{
		Lock lock(this->world_state->mutex);

		for(auto it = this->world_state->avatars.begin(); it != this->world_state->avatars.end();)
		{
			Avatar* avatar = it->second.getPointer();
			if(avatar->state == Avatar::State_Dead)
			{
				conPrint("Removing avatar.");
				// Remove any OpenGL object for it
				if(avatar->opengl_engine_ob.nonNull())
					ui->glWidget->opengl_engine->removeObject(avatar->opengl_engine_ob);

				// Remove avatar from avatar map
				auto old_avatar_iterator = it;
				it++;
				this->world_state->avatars.erase(old_avatar_iterator);
			}
			else
			{
				if(avatar->uid != this->client_thread->client_avatar_uid) // Don't render our own Avatar
				{
					if(avatar->opengl_engine_ob.isNull()) // If this is a new avatar that doesn't have an OpenGL model yet:
					{
						const double cur_time = Clock::getCurTimeRealSec();
						Vec3d pos;
						Vec3f axis;
						float angle;
						avatar->getInterpolatedTransform(cur_time, pos, axis, angle);

						const Matrix4f ob_to_world_matrix = Matrix4f::translationMatrix((float)pos.x, (float)pos.y, (float)pos.z) * 
							Matrix4f::rotationMatrix(normalise(axis.toVec4fVector()), angle);

						// See if we have the file downloaded
						const std::string path = this->resource_manager->pathForURL(avatar->model_url);
						if(!FileUtils::fileExists(path))
						{
							conPrint("Don't have avatar model '" + avatar->model_url + "' on disk, using placeholder.");

							// Use a temporary placeholder model.
							GLObjectRef cube_gl_ob = ui->glWidget->opengl_engine->makeAABBObject(Vec4f(0,0,0,1), Vec4f(1,1,1,1), Colour4f(0.6f, 0.2f, 0.2, 0.5f));
							cube_gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(-0.5f, -0.5f, -0.5f) * ob_to_world_matrix;
							avatar->opengl_engine_ob = cube_gl_ob;
							ui->glWidget->addObject(cube_gl_ob);

							avatar->using_placeholder_model = true;

							// Enqueue download of model
							this->resource_download_thread_manager.enqueueMessage(new DownloadResourceMessage(avatar->model_url));
						}
						else
						{
							conPrint("Adding Avatar to OpenGL Engine, UID " + toString(avatar->uid.value()));

							// Make GL object, add to OpenGL engine
							Indigo::MeshRef mesh;
							GLObjectRef gl_ob = ModelLoading::makeGLObjectForModelFile(path, ob_to_world_matrix, mesh);
							avatar->opengl_engine_ob = gl_ob;
							ui->glWidget->addObject(gl_ob);

							// No physics object for avatars.
						}
					}
					else
					{
						const double cur_time = Clock::getCurTimeRealSec();
						Vec3d pos;
						Vec3f axis;
						float angle;
						avatar->getInterpolatedTransform(cur_time, pos, axis, angle);

						avatar->opengl_engine_ob->ob_to_world_matrix.setToRotationMatrix(normalise(axis.toVec4fVector()), angle);
						avatar->opengl_engine_ob->ob_to_world_matrix.setColumn(3, Vec4f(pos.x, pos.y, pos.z, 1.f));
						ui->glWidget->opengl_engine->updateObjectTransformData(*avatar->opengl_engine_ob);

						//avatar->from_remote_dirty = false;
					}
				}

				 ++it;
			}
		}
	}
	catch(Indigo::Exception& e)
	{
		conPrint(e.what());
	}

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
					conPrint("Removing WorldObject.");
				
					// Remove any OpenGL object for it
					if(ob->opengl_engine_ob.nonNull())
						ui->glWidget->opengl_engine->removeObject(ob->opengl_engine_ob);

					// Remove physics object  TODO
					//if(ob->physics_object.nonNull())
					//	physics_world->removeObject(ob->physics_object);

					//// Remove object from object map
					auto old_object_iterator = it;
					it++;
					this->world_state->objects.erase(old_object_iterator);
				}
				else
				{
					if(ob->opengl_engine_ob.isNull())
					{
						const Matrix4f ob_to_world_matrix = Matrix4f::translationMatrix((float)ob->pos.x, (float)ob->pos.y, (float)ob->pos.z) * 
							Matrix4f::rotationMatrix(normalise(ob->axis.toVec4fVector()), ob->angle) * 
							Matrix4f::scaleMatrix(ob->scale.x, ob->scale.y, ob->scale.z);

						// See if we have the file downloaded
						//const std::string model_path    = this->resource_manager->pathForURL(ob->model_url);
						//const std::string material_path = this->resource_manager->pathForURL(ob->material_url);

						std::vector<std::string> dependency_URLs;
						ob->appendDependencyURLs(dependency_URLs);

						std::map<std::string, std::string> paths_for_URLs;

						// Do we have all the objects downloaded?
						bool all_downloaded = true;
						for(size_t i=0; i<dependency_URLs.size(); ++i)
						{
							const std::string path = this->resource_manager->pathForURL(dependency_URLs[i]);
							if(!FileUtils::fileExists(path))
							{
								all_downloaded = false;
								// Enqueue download of resource.  TODO: check for dups
								this->resource_download_thread_manager.enqueueMessage(new DownloadResourceMessage(dependency_URLs[i]));
							}

							paths_for_URLs[dependency_URLs[i]] = path;
						}



						if(!all_downloaded) // !(FileUtils::fileExists(model_path)/* && FileUtils::fileExists(material_path)*/))
						{
							//if(!(FileUtils::fileExists(model_path)))
							//	conPrint("Don't have model '" + ob->model_url + "' on disk, using placeholder.");
							//if(!(FileUtils::fileExists(material_path)))
							//	conPrint("Don't have material '" + ob->material_url + "' on disk, using placeholder.");

							// Use a temporary placeholder model.
							GLObjectRef cube_gl_ob = ui->glWidget->opengl_engine->makeAABBObject(Vec4f(0,0,0,1), Vec4f(1,1,1,1), Colour4f(0.6f, 0.2f, 0.2, 0.5f));
							cube_gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(-0.5f, -0.5f, -0.5f) * ob_to_world_matrix;
							ob->opengl_engine_ob = cube_gl_ob;
							ui->glWidget->addObject(cube_gl_ob);

							ob->using_placeholder_model = true;

							// Enqueue download of model
							//this->resource_download_thread_manager.enqueueMessage(new DownloadResourceMessage(ob->model_url));
						}
						else
						{
							conPrint("Adding Object to OpenGL Engine, UID " + toString(ob->uid.value()));

							// Make GL object, add to OpenGL engine
							Indigo::MeshRef mesh;
							GLObjectRef gl_ob = ModelLoading::makeGLObjectForModelFile(paths_for_URLs[ob->model_url], ob->materials, *this->resource_manager/*paths_for_URLs*/, ob_to_world_matrix, mesh);
							ob->opengl_engine_ob = gl_ob;
							ui->glWidget->addObject(gl_ob);

							// Make physics object
							StandardPrintOutput print_output;
							Indigo::TaskManager task_manager;
							PhysicsObjectRef physics_ob = makePhysicsObject(mesh, ob_to_world_matrix, print_output, task_manager);
							ob->physics_object = physics_ob;
							physics_ob->userdata = ob;
							physics_world->addObject(physics_ob);
						}

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

								ui->glWidget->opengl_engine->objectMaterialsUpdated(opengl_ob, *this->texture_server);
							}
							else
							{
								assert(ob->from_remote_transform_dirty);

								const double cur_time = Clock::getCurTimeRealSec();
								Vec3d pos;
								Vec3f axis;
								float angle;
								ob->getInterpolatedTransform(cur_time, pos, axis, angle);

								ob->opengl_engine_ob->ob_to_world_matrix = Matrix4f::translationMatrix((float)pos.x, (float)pos.y, (float)pos.z) * 
									Matrix4f::rotationMatrix(normalise(axis.toVec4fVector()), angle) * 
									Matrix4f::scaleMatrix(ob->scale.x, ob->scale.y, ob->scale.z);

								//ob->opengl_engine_ob->ob_to_world_matrix.setToRotationMatrix(ob->axis.toVec4fVector(), ob->angle);
								//ob->opengl_engine_ob->ob_to_world_matrix.setColumn(3, Vec4f(ob->pos.x, ob->pos.y, ob->pos.z, 1.f));
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
		conPrint(e.what());
	}


	// Interpolate any active objects (Objects that have moved recently and so need interpolation done on them.)
	{
		const double cur_time = Clock::getCurTimeRealSec();

		Lock lock(this->world_state->mutex);
		for(auto it = active_objects.begin(); it != active_objects.end();)
		{
			WorldObjectRef ob = *it;

			if(cur_time - ob->last_snapshot_time > 1.0)
			{
				// Object is not active any more, remove from active_objects set.
				auto to_erase = it;
				it++;
				active_objects.erase(to_erase);
			}
			else
			{
				const double cur_time = Clock::getCurTimeRealSec();
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

				it++;
			}
		}
	}



	// Move selected object if there is one
	if(this->selected_ob.nonNull())
	{
		// Get direction for current mouse cursor position
		const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
		const Vec4f forwards = cam_controller.getForwardsVec().toVec4fVector();
		const Vec4f right = cam_controller.getRightVec().toVec4fVector();
		const Vec4f up = cam_controller.getUpVec().toVec4fVector();

		// Convert selection vector to world space
		const Vec4f selection_vec_ws(right*selection_vec_cs[0] + forwards*selection_vec_cs[1] + up*selection_vec_cs[2]);

		const Vec4f new_sel_point_ws = origin + selection_vec_ws;
		
		const Vec4f new_ob_pos = this->selected_ob_pos_upon_selection + (new_sel_point_ws - this->selection_point_ws);

		// Set world object pos
		this->selected_ob->setPosAndHistory(toVec3d(new_ob_pos));

		// Set graphics object pos and update in opengl engine.
		GLObjectRef opengl_ob(this->selected_ob->opengl_engine_ob);
		opengl_ob->ob_to_world_matrix.setColumn(3, new_ob_pos);
		ui->glWidget->opengl_engine->updateObjectTransformData(*opengl_ob);

		// Update physics object
		this->selected_ob->physics_object->ob_to_world.setColumn(3, new_ob_pos);
		this->physics_world->updateObjectTransformData(*this->selected_ob->physics_object);

		// Mark as from-local-dirty to send an object transform updated message to the server
		this->selected_ob->from_local_transform_dirty = true;
	}

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
			const Vec3d axis(0,0,1);
			const double angle = cam_angles.x;

			SocketBufferOutStream packet;
			packet.writeUInt32(AvatarTransformUpdate);
			writeToStream(this->client_thread->client_avatar_uid, packet);
			writeToStream(Vec3d(this->cam_controller.getPosition()), packet);
			writeToStream(Vec3f(axis.x,axis.y,axis.z), packet); // TODO: rotation
			packet.writeFloat(angle);

			std::string packet_string(packet.buf.size(), '\0');
			std::memcpy(&packet_string[0], packet.buf.data(), packet.buf.size());

			this->client_thread->enqueueDataToSend(packet_string);
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
					SocketBufferOutStream packet;
					packet.writeUInt32(ObjectFullUpdate);
					writeToNetworkStream(*world_ob, packet);
					
					std::string packet_string(packet.buf.size(), '\0');
					std::memcpy(&packet_string[0], packet.buf.data(), packet.buf.size());

					this->client_thread->enqueueDataToSend(packet_string);

					world_ob->from_local_other_dirty = false;
					world_ob->from_local_transform_dirty = false;
				}
				else if(world_ob->from_local_transform_dirty)
				{
					// Enqueue ObjectTransformUpdate
					SocketBufferOutStream packet;
					packet.writeUInt32(ObjectTransformUpdate);
					writeToStream(world_ob->uid, packet);
					writeToStream(Vec3d(world_ob->pos), packet);
					writeToStream(Vec3f(world_ob->axis), packet);
					packet.writeFloat(world_ob->angle);

					std::string packet_string(packet.buf.size(), '\0');
					std::memcpy(&packet_string[0], packet.buf.data(), packet.buf.size());

					this->client_thread->enqueueDataToSend(packet_string);

					world_ob->from_local_transform_dirty = false;
				}

			}
		}


		time_since_update_packet_sent.reset();
	}


	ui->glWidget->updateGL();
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
			avatar.axis = Vec3f(0,0,1);
			avatar.angle = (float)cam_angles.x;
			avatar.model_url = URL;

			SocketBufferOutStream packet;
			packet.writeUInt32(AvatarFullUpdate);
			writeToNetworkStream(avatar, packet);

			std::string packet_string(packet.buf.size(), '\0');
			std::memcpy(&packet_string[0], packet.buf.data(), packet.buf.size());

			this->client_thread->enqueueDataToSend(packet_string);
		}
		catch(Indigo::IndigoException& e)
		{
			// Show error
			conPrint(toStdString(e.what()));
			QErrorMessage m;
			m.showMessage(QtUtils::toQString(e.what()));
			m.exec();
		}
		catch(FileUtils::FileUtilsExcep& e)
		{
			// Show error
			conPrint(e.what());
			QErrorMessage m;
			m.showMessage(QtUtils::toQString(e.what()));
			m.exec();
		}
		catch(Indigo::Exception& e)
		{
			// Show error
			conPrint(e.what());
			QErrorMessage m;
			m.showMessage(QtUtils::toQString(e.what()));
			m.exec();
		}
	}
}


void MainWindow::on_actionAddObject_triggered()
{
	AddObjectDialog d(this->settings, this->texture_server);
	if(d.exec() == QDialog::Accepted)
	{
		// Try and load model
		try
		{
			ui->glWidget->makeCurrent();

			const Vec3d ob_pos = this->cam_controller.getPosition() + this->cam_controller.getForwardsVec() * 1.0f;

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

			const std::string URL = ResourceManager::URLForPathAndHash(d.result_path, d.model_hash);

			// Copy model to local resources dir.  UploadResourceThread will read from here.
			FileUtils::copyFile(d.result_path, this->resource_manager->pathForURL(URL));

			// Send ObjectCreated message to server
			{
				SocketBufferOutStream packet;

				//WorldObject new_ob;
				//new_ob.uid = UID::invalidUID(); // Server will set UID.
				//new_ob.model_url = URL;

				packet.writeUInt32(ObjectCreated);
				packet.writeStringLengthFirst(URL);
				packet.writeUInt64(d.model_hash);
				writeToStream(ob_pos, packet); // pos
				writeToStream(Vec3f(0,0,1), packet); // axis
				packet.writeFloat(0.f); // angle
				writeToStream(Vec3f(1.f), packet); // scale

				std::string packet_string(packet.buf.size(), '\0');
				std::memcpy(&packet_string[0], packet.buf.data(), packet.buf.size());

				this->client_thread->enqueueDataToSend(packet_string);
			}
		}
		catch(Indigo::IndigoException& e)
		{
			// Show error
			conPrint(toStdString(e.what()));
			QErrorMessage m;
			m.showMessage(QtUtils::toQString(e.what()));
			m.exec();
		}
		catch(FileUtils::FileUtilsExcep& e)
		{
			// Show error
			conPrint(e.what());
			QErrorMessage m;
			m.showMessage(QtUtils::toQString(e.what()));
			m.exec();
		}
		catch(Indigo::Exception& e)
		{
			// Show error
			conPrint(e.what());
			QErrorMessage m;
			m.showMessage(QtUtils::toQString(e.what()));
			m.exec();
		}
	}
}


void MainWindow::on_actionDeleteObject_triggered()
{
	if(this->selected_ob.nonNull())
	{
		deleteSelectedObject();

		// Deselect object
		this->selected_ob = NULL;
		ui->objectEditor->setEnabled(false);
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

	// Enable tool bar
	ui->toolBar->setVisible(true);
}


void MainWindow::sendChatMessageSlot()
{
	//conPrint("MainWindow::sendChatMessageSlot()");

	const std::string name = QtUtils::toIndString(settings->value("username").toString());
	const std::string message = QtUtils::toIndString(ui->chatMessageLineEdit->text());

	// Make message packet and enqueue to send
	SocketBufferOutStream packet;
	packet.writeUInt32(ChatMessageID);
	packet.writeStringLengthFirst(name);
	packet.writeStringLengthFirst(message);

	std::string packet_string(packet.buf.size(), '\0');
	std::memcpy(&packet_string[0], packet.buf.data(), packet.buf.size());

	this->client_thread->enqueueDataToSend(packet_string);

	ui->chatMessageLineEdit->clear();
}


void MainWindow::objectEditedSlot()
{
	conPrint("MainWindow::objectEditedSlot()");

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
				const std::string URL = ResourceManager::URLForPathAndHash(URLs[i], FileChecksum::fileChecksum(URLs[i]));

				// Copy model to local resources dir.
				FileUtils::copyFile(URLs[i], this->resource_manager->pathForURL(URL));
			}
		}

		this->selected_ob->materials[0]->convertLocalPathsToURLS(*this->resource_manager);

		// Update in opengl engine.
		GLObjectRef opengl_ob = selected_ob->opengl_engine_ob;
		if(opengl_ob.nonNull())
		{
			if(!opengl_ob->materials.empty())
			{
				ModelLoading::setGLMaterialFromWorldMaterial(*this->selected_ob->materials[0], *this->resource_manager, 
					opengl_ob->materials[0]
				);
			}
		}

		ui->glWidget->opengl_engine->objectMaterialsUpdated(opengl_ob, *this->texture_server);

		// Update transform of OpenGL object
		opengl_ob->ob_to_world_matrix = Matrix4f::translationMatrix((float)this->selected_ob->pos.x, (float)this->selected_ob->pos.y, (float)this->selected_ob->pos.z) * 
				Matrix4f::rotationMatrix(normalise(this->selected_ob->axis.toVec4fVector()), this->selected_ob->angle) * 
				Matrix4f::scaleMatrix(this->selected_ob->scale.x, this->selected_ob->scale.y, this->selected_ob->scale.z);

		ui->glWidget->opengl_engine->updateObjectTransformData(*opengl_ob);

		// Mark as from-local-dirty to send an object updated message to the server
		this->selected_ob->from_local_other_dirty = true;
	}
}


void MainWindow::glWidgetMouseClicked(QMouseEvent* e)
{
	//conPrint("MainWindow::glWidgetMouseClicked()");
}


void MainWindow::glWidgetMouseDoubleClicked(QMouseEvent* e)
{
	//conPrint("MainWindow::glWidgetMouseDoubleClicked()");

	// Trace ray through scene
	ThreadContext thread_context;
	const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
	const Vec4f forwards = cam_controller.getForwardsVec().toVec4fVector();
	const Vec4f right = cam_controller.getRightVec().toVec4fVector();
	const Vec4f up = cam_controller.getUpVec().toVec4fVector();

	const float sensor_width = 0.035f;
	const float sensor_height = sensor_width / ui->glWidget->viewport_aspect_ratio;
	const float lens_sensor_dist = 0.03f;

	const float s_x = sensor_width *  (float)(e->pos().x() - ui->glWidget->geometry().width() /2) / ui->glWidget->geometry().width(); // dist right on sensor from centre of sensor
	const float s_y = sensor_height * (float)(e->pos().y() - ui->glWidget->geometry().height()/2) / ui->glWidget->geometry().height(); // dist down on sensor from centre of sensor

	const float r_x = s_x / lens_sensor_dist;
	const float r_y = s_y / lens_sensor_dist;

	const Vec4f dir = normalise(forwards + right * r_x - up * r_y);

	RayTraceResult results;
	this->physics_world->traceRay(origin, dir, thread_context, results);

	if(results.hit_object)
	{
		this->selection_point_ws = origin + dir*results.hitdist;
		// Add an object at the hit point
		//this->glWidget->addObject(glWidget->opengl_engine->makeAABBObject(this->selection_point_ws - Vec4f(0.03f, 0.03f, 0.03f, 0.f), this->selection_point_ws + Vec4f(0.03f, 0.03f, 0.03f, 0.f), Colour4f(0.6f, 0.6f, 0.2f, 1.f)));

		// Deselect any currently selected object
		if(this->selected_ob.nonNull())
			ui->glWidget->opengl_engine->deselectObject(selected_ob->opengl_engine_ob);

		this->selected_ob = static_cast<WorldObject*>(results.hit_object->userdata);
		if(this->selected_ob.nonNull())
		{
			assert(this->selected_ob->getRefCount() >= 0 && this->selected_ob->getRefCount() < 10);
			const Vec4f selection_vec_ws = this->selection_point_ws - origin;
			// Get selection_vec_cs
			this->selection_vec_cs = Vec4f(dot(selection_vec_ws, right), dot(selection_vec_ws, forwards), dot(selection_vec_ws, up), 0.f);

			//this->selection_point_dist = results.hitdist;
			this->selected_ob_pos_upon_selection = results.hit_object->ob_to_world.getColumn(3);

			// Mark the materials on the hit object as selected
			ui->glWidget->opengl_engine->selectObject(selected_ob->opengl_engine_ob);


			// Update the editor widget with values from the selected object
			//if(selected_ob->materials.empty())
			//	selected_ob->materials.push_back(new WorldMaterial());

			//this->matEditor->setFromMaterial(*selected_ob->materials[0]);
			ui->objectEditor->setFromObject(*selected_ob);


			ui->objectEditor->setEnabled(true);
		}
		else
		{
			ui->objectEditor->setEnabled(false);
		}
	}
	else
	{
		// Deselect any currently selected object
		if(this->selected_ob.nonNull())
			ui->glWidget->opengl_engine->deselectObject(this->selected_ob->opengl_engine_ob);

		// deslect object
		this->selected_ob = NULL;
		ui->objectEditor->setEnabled(false);
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


void MainWindow::rotateObject(WorldObjectRef ob, const Vec4f& axis, float angle)
{
	// Rotate object clockwise around z axis
	const Matrix3f rot_matrix = Matrix3f::rotationMatrix(ob->axis, selected_ob->angle);
	const Matrix3f new_rot = Matrix3f::rotationMatrix(toVec3f(axis), angle) * rot_matrix;
	new_rot.rotationMatrixToAxisAngle(ob->axis, this->selected_ob->angle);

	const Matrix4f new_ob_to_world = Matrix4f::translationMatrix(ob->pos.toVec4fPoint()) * Matrix4f::rotationMatrix(normalise(ob->axis.toVec4fVector()), ob->angle) * 
		Matrix4f::scaleMatrix(ob->scale.x, ob->scale.y, ob->scale.z);

	// Update in opengl engine.
	GLObjectRef opengl_ob = ob->opengl_engine_ob;
	opengl_ob->ob_to_world_matrix = new_ob_to_world;
	ui->glWidget->opengl_engine->updateObjectTransformData(*opengl_ob);

	// Update physics object
	ob->physics_object->ob_to_world = new_ob_to_world;
	this->physics_world->updateObjectTransformData(*this->selected_ob->physics_object);
}


void MainWindow::deleteSelectedObject()
{
	if(this->selected_ob.nonNull())
	{
		// Send ObjectDestroyed packet
		SocketBufferOutStream packet;
		packet.writeUInt32(ObjectDestroyed);
		writeToStream(selected_ob->uid, packet);

		std::string packet_string(packet.buf.size(), '\0');
		std::memcpy(&packet_string[0], packet.buf.data(), packet.buf.size());

		this->client_thread->enqueueDataToSend(packet_string);
	}
}


void MainWindow::glWidgetKeyPressed(QKeyEvent* e)
{
	if(e->key() == Qt::Key::Key_Escape)
	{
		if(this->selected_ob.nonNull())
		{
			ui->glWidget->opengl_engine->deselectObject(this->selected_ob->opengl_engine_ob);

			// Deselect object
			this->selected_ob = NULL;
			ui->objectEditor->setEnabled(false);
		}
	}
	else if(e->key() == Qt::Key::Key_Delete)
	{
		if(this->selected_ob.nonNull())
		{
			deleteSelectedObject();

			// Deselect object
			this->selected_ob = NULL;
			ui->objectEditor->setEnabled(false);
		}
	}
	
	if(this->selected_ob.nonNull())
	{
		const float angle_step = Maths::pi<float>() / 32;
		if(e->key() == Qt::Key::Key_Left)
		{
			// Rotate object clockwise around z axis
			rotateObject(this->selected_ob, Vec4f(0,0,1,0), -angle_step);
		}
		else if(e->key() == Qt::Key::Key_Right)
		{
			rotateObject(this->selected_ob, Vec4f(0,0,1,0), angle_step);
		}
		else if(e->key() == Qt::Key::Key_Up)
		{
			// Rotate object clockwise around camera right-vector
			rotateObject(this->selected_ob, this->cam_controller.getRightVec().toVec4fVector(), -angle_step);
		}
		else if(e->key() == Qt::Key::Key_Down)
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
	}
}


void MainWindow::glWidgetMouseWheelEvent(QWheelEvent* e)
{
	if(this->selected_ob.nonNull())
	{
		this->selection_vec_cs[1] *= (1.0f + e->delta() * 0.0005f);
	}
}


int main(int argc, char *argv[])
{
	GuiClientApplication app(argc, argv);

	// Set the C standard lib locale back to c, so e.g. printf works as normal, and uses '.' as the decimal separator.
	std::setlocale(LC_ALL, "C");

	Clock::init();
	Networking::createInstance();

	PlatformUtils::ignoreUnixSignals();

	std::string indigo_base_dir_path;
	try
	{
		indigo_base_dir_path = PlatformUtils::getResourceDirectoryPath();
	}
	catch(PlatformUtils::PlatformUtilsExcep& e)
	{
		conPrint(e.what());
		QErrorMessage m;
		m.showMessage(QtUtils::toQString(e.what()));
		m.exec();
		return 1;
	}

	// Get the 'appdata_path', which will be indigo_base_dir_path on Linux/OS-X, but something like
	// 'C:\Users\Nicolas Chapman\AppData\Roaming\Indigo Renderer' on Windows.
	const std::string appdata_path = PlatformUtils::getOrCreateAppDataDirectoryWithDummyFallback();

	QDir::setCurrent(QtUtils::toQString(indigo_base_dir_path));


	// Get a vector of the args.  Note that we will use app.arguments(), because it's the only way to get the args in Unicode in Qt.
	const QStringList arg_list = app.arguments();
	std::vector<std::string> args;
	for(int i = 0; i < arg_list.size(); ++i)
		args.push_back(QtUtils::toIndString(arg_list.at((int)i)));


	//TEMP:
	//ReferenceTest::run();
	//Matrix4f::test();
	//CameraController::test();

	StandardPrintOutput print_output;
	Indigo::TaskManager task_manager;

	try
	{
		std::map<std::string, std::vector<ArgumentParser::ArgumentType> > syntax;

		ArgumentParser parsed_args(args, syntax);

		MainWindow mw(indigo_base_dir_path, appdata_path, parsed_args);

		mw.initialise();

		mw.show();

		mw.raise();



		Mutex temp_mutex;

		mw.world_state = new WorldState();


		mw.resource_download_thread_manager.addThread(new DownloadResourcesThread(&mw.msg_queue, mw.resource_manager, server_hostname, server_port));

		const std::string avatar_path = QtUtils::toStdString(mw.settings->value("avatarPath").toString());
		const std::string username    = QtUtils::toStdString(mw.settings->value("username").toString());

		uint64 avatar_model_hash = 0;
		if(FileUtils::fileExists(avatar_path))
			avatar_model_hash = FileChecksum::fileChecksum(avatar_path);
		const std::string avatar_URL = mw.resource_manager->URLForPathAndHash(avatar_path, avatar_model_hash);

		mw.client_thread = new ClientThread(&mw.msg_queue, server_hostname, server_port, &mw, username, avatar_URL);
		mw.client_thread->world_state = mw.world_state;
		//mw.client_thread->launch();
		mw.client_thread_manager.addThread(mw.client_thread);

		

		mw.physics_world = new PhysicsWorld();


		//CameraController cam_controller;
		mw.cam_controller.setPosition(Vec3d(0,0,4.7));
		mw.ui->glWidget->setCameraController(&mw.cam_controller);
		mw.ui->glWidget->setPlayerPhysics(&mw.player_physics);
		mw.cam_controller.setMoveScale(0.3f);

		TextureServer texture_server;
		mw.texture_server = &texture_server;
		mw.ui->glWidget->texture_server_ptr = &texture_server;

		const float sun_phi = 1.f;
		const float sun_theta = 0.9510680884f;
		mw.ui->glWidget->opengl_engine->setSunDir(normalise(Vec4f(std::cos(sun_phi) * sin(sun_theta), std::sin(sun_phi) * sun_theta, cos(sun_theta), 0)));

		mw.ui->glWidget->opengl_engine->setEnvMapTransform(Matrix3f::rotationMatrix(Vec3f(0,0,1), sun_phi));

		/*
		Set env material
		*/
		{
			OpenGLMaterial env_mat;
			env_mat.albedo_tex_path = "sky.png";
			env_mat.tex_matrix = Matrix2f(-1 / Maths::get2Pi<float>(), 0, 0, 1 / Maths::pi<float>());

			mw.ui->glWidget->setEnvMat(env_mat);
		}


		//TEMP: make an arrow
		{
			GLObjectRef arrow = mw.ui->glWidget->opengl_engine->makeArrowObject(Vec4f(1,1,1,1), Vec4f(2,1,1,1), Colour4f(0.6, 0.2, 0.2, 1.f), 1.f);
			mw.ui->glWidget->opengl_engine->addObject(arrow);
		}
		{
			GLObjectRef arrow = mw.ui->glWidget->opengl_engine->makeArrowObject(Vec4f(1,1,1,1), Vec4f(1,2,1,1), Colour4f(0.2, 0.6, 0.2, 1.f), 1.f);
			mw.ui->glWidget->opengl_engine->addObject(arrow);
		}
		{
			GLObjectRef arrow = mw.ui->glWidget->opengl_engine->makeArrowObject(Vec4f(1,1,1,1), Vec4f(1,1,2,1), Colour4f(0.2, 0.2, 0.6, 1.f), 1.f);
			mw.ui->glWidget->opengl_engine->addObject(arrow);
		}

		// TEMP: make capsule
		/*{
			GLObjectRef ob = new GLObject();

			ob->ob_to_world_matrix.setToTranslationMatrix(0.0, 2.0, 1.0f);

			ob->mesh_data = mw.glWidget->opengl_engine->makeCapsuleMesh(
				Vec3f(0.2f, 0.1f, 0.2f), 
				Vec3f(0.4f, 0.2f, 0.4f)
			);

			ob->materials.resize(1);
			ob->materials[0].albedo_rgb = Colour3f(0.5f, 0.5f, 0.2f);
			
			mw.glWidget->opengl_engine->addObject(ob);
		}*/

		//TEMP: make an AABB
		/*{
			GLObjectRef aabb = mw.glWidget->opengl_engine->makeAABBObject(Vec4f(1,1,1,1), Vec4f(2,2,2,1), Colour4f(0.6, 0.5, 0.5, 0.6));
			mw.glWidget->opengl_engine->addObject(aabb);
		}*/

		/*{
			GLObjectRef aabb = mw.glWidget->opengl_engine->makeAABBObject(Vec4f(1,1,1,1), Vec4f(2,1.5,1.5,1), Colour4f(0.6, 0.3, 0.3, 0.6));
			mw.glWidget->opengl_engine->addObject(aabb);
		}

		{
			GLObjectRef aabb = mw.glWidget->opengl_engine->makeAABBObject(Vec4f(3,1,1,1), Vec4f(4,3,2,1), Colour4f(0.3, 0.6, 0.3, 0.6));
			mw.glWidget->opengl_engine->addObject(aabb);
		}

		{
			GLObjectRef aabb = mw.glWidget->opengl_engine->makeAABBObject(Vec4f(5,1,1,1), Vec4f(6,2,3,1), Colour4f(0.3, 0.3, 0.6, 0.2));
			mw.glWidget->opengl_engine->addObject(aabb);
		}*/

	
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
		if(true)
		{
			Indigo::MeshRef mesh = new Indigo::Mesh();
			const float r = 10000.f;
			mesh->addVertex(Indigo::Vec3f(-r, -r, 0), Indigo::Vec3f(0,0,1));
			mesh->addVertex(Indigo::Vec3f( r, -r, 0), Indigo::Vec3f(0,0,1));
			mesh->addVertex(Indigo::Vec3f( r,  r, 0), Indigo::Vec3f(0,0,1));
			mesh->addVertex(Indigo::Vec3f(-r,  r, 0), Indigo::Vec3f(0,0,1));
			
			mesh->num_uv_mappings = 1;
			mesh->addUVs(Indigo::Vector<Indigo::Vec2f>(1, Indigo::Vec2f(0,0)));
			mesh->addUVs(Indigo::Vector<Indigo::Vec2f>(1, Indigo::Vec2f(r,0)));
			mesh->addUVs(Indigo::Vector<Indigo::Vec2f>(1, Indigo::Vec2f(r,r)));
			mesh->addUVs(Indigo::Vector<Indigo::Vec2f>(1, Indigo::Vec2f(0,r)));
			
			uint32 indices[] = {0, 1, 2, 3};
			mesh->addQuad(indices, indices, 0);

			mesh->endOfModel();

			GLObjectRef ob = new GLObject();
			ob->materials.resize(1);
			ob->materials[0].albedo_rgb = Colour3f(0.7f, 0.7f, 0.7f);
			ob->materials[0].albedo_tex_path = "obstacle.png";
			ob->materials[0].roughness = 0.8f;

			ob->ob_to_world_matrix.setToTranslationMatrix(0,0,0);
			ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh, false);

			mw.ui->glWidget->addObject(ob);

			// Load physics object
			/*{
				Reference<PhysicsObject> phy_ob = new PhysicsObject();
				phy_ob->geometry = new RayMesh("ground plane", false);
				phy_ob->geometry->fromIndigoMesh(*mesh);
				
				phy_ob->geometry->buildTrisFromQuads();
				Geometry::BuildOptions options;
				options.bih_tri_threshold = 1;
				options.cache_trees = false;
				options.use_embree = false;
				phy_ob->geometry->build(".", options, print_output, false, task_manager);

				phy_ob->geometry->buildJSTris();
				
				phy_ob->ob_to_world = ob->ob_to_world_matrix;
				mw.physics_world->addObject(phy_ob);
			}*/
			mw.physics_world->addObject(makePhysicsObject(mesh, ob->ob_to_world_matrix, print_output, task_manager));
		}


		// Add a spinning globe
		/*{
			globe = new GLObject();
			globe->mesh_data = mw.glWidget->opengl_engine->getSphereMeshData();
			globe->materials.resize(1);
			OpenGLMaterial env_mat;
			globe->materials[0].albedo_tex_path = "N:\\indigo\\trunk\\testscenes\\world.200401.3x5400x2700.jpg";
			globe->materials[0].tex_matrix = Matrix2f(1 / Maths::get2Pi<float>(), 0, 0, 1 / Maths::pi<float>());

			globe->ob_to_world_matrix = Matrix4f::identity();
			globe->ob_to_world_matrix.setColumn(3, Vec4f(-2.f,1,0.8f,3.3f)); // Set pos

			mw.glWidget->addObject(globe);

			//mw.physics_world->addObject(makePhysicsObject(mesh, ob->ob_to_world_matrix, print_output, task_manager));
		}*/



		try
		{
			// Eames Elephant
			
			/*{
				Indigo::MeshRef mesh = new Indigo::Mesh();
				FormatDecoderObj::streamModel("N:\\indigo\\trunk\\testscenes\\eames_elephant.obj", *mesh, 1.f);

				GLObjectRef ob = new GLObject();
				ob->materials.resize(2);
				ob->materials[0].albedo_rgb = Colour3f(0.6f, 0.2f, 0.2f);
				ob->materials[0].fresnel_scale = 1;

				ob->materials[1].albedo_rgb = Colour3f(0.6f, 0.6f, 0.2f);
				ob->materials[1].fresnel_scale = 1;

				ob->ob_to_world_matrix.setToRotationMatrix(Vec4f(1,0,0,0), Maths::pi_2<float>());
				ob->ob_to_world_matrix.setColumn(3, Vec4f(0.3,1,0.8f,1.f)); // Set pos
				//ob->ob_to_world_matrix.setToTranslationMatrix(0.3,1,0.8f);
				ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh);

				mw.glWidget->addObject(ob);

				mw.physics_world->addObject(makePhysicsObject(mesh, ob->ob_to_world_matrix, print_output, task_manager));
			}*/

			//TEMP:
			// Load a GIANT teapot
		/*	{
				Indigo::MeshRef mesh = new Indigo::Mesh();
				FormatDecoderObj::streamModel("N:\\indigo\\trunk\\testfiles\\teapot.obj", *mesh, 10.f);

				GLObjectRef ob = new GLObject();
				ob->materials.resize(1);
				ob->materials[0].albedo_rgb = Colour3f(0.6f, 0.2f, 6.2f);
				ob->materials[0].fresnel_scale = 1;

				ob->ob_to_world_matrix.setToTranslationMatrix(-10,10,-1.4f);
				ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh);

				mw.glWidget->addObject(ob);

				mw.physics_world->addObject(makePhysicsObject(mesh, ob->ob_to_world_matrix, print_output, task_manager));
			}*/


			// Load a wedge
			{
				Indigo::MeshRef mesh = new Indigo::Mesh();
				Indigo::Mesh::readFromFile("wedge.igmesh", *mesh);
				//"N:\\indigo\\trunk\\dist\\shared\\data\\preview_scenes\\other\\wedge.igmesh"

				GLObjectRef ob = new GLObject();
				ob->materials.resize(1);
				ob->materials[0].albedo_rgb = Colour3f(0.6f, 0.2f, 0.2f);
				ob->materials[0].fresnel_scale = 1;
				ob->materials[0].roughness = 0.3f;

				Matrix4f scale;
				scale.setToUniformScaleMatrix(100.f);
				Matrix4f trans;
				trans.setToTranslationMatrix(10,10,0.f);
				mul(trans, scale, ob->ob_to_world_matrix);
				ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh, false);

				mw.ui->glWidget->addObject(ob);

				mw.physics_world->addObject(makePhysicsObject(mesh, ob->ob_to_world_matrix, print_output, task_manager));
			}

			// Load steps
			if(false)
			{
				Indigo::MeshRef mesh = new Indigo::Mesh();
				Indigo::Mesh::readFromFile("steps.igmesh", *mesh);

				GLObjectRef ob = new GLObject();
				ob->materials.resize(1);
				ob->materials[0].albedo_rgb = Colour3f(0.4f, 0.4f, 0.2f);
				ob->materials[0].fresnel_scale = 1;
				ob->materials[0].roughness = 0.3f;

				Matrix4f scale;
				scale.setToUniformScaleMatrix(1.f);
				Matrix4f trans;
				trans.setToTranslationMatrix(-10,10,0.f);
				mul(trans, scale, ob->ob_to_world_matrix);
				ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh, false);

				mw.ui->glWidget->addObject(ob);

				mw.physics_world->addObject(makePhysicsObject(mesh, ob->ob_to_world_matrix, print_output, task_manager));
			}


			//TEMP:
			// Load a teapot
		
			if(false)
			{
				Indigo::MeshRef mesh = new Indigo::Mesh();
				MLTLibMaterials mats;
				FormatDecoderObj::streamModel("teapot.obj", *mesh, 1.f, false, mats);

				// Create graphics object
				GLObjectRef gl_ob = new GLObject();
				gl_ob->materials.resize(1);
				gl_ob->materials[0].albedo_rgb = Colour3f(0.6f, 0.2f, 0.2f);
				gl_ob->materials[0].fresnel_scale = 1;

				gl_ob->ob_to_world_matrix.setToTranslationMatrix(-3.0,1,0.5f);
				gl_ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh, false);

				mw.ui->glWidget->addObject(gl_ob);

				// Create physics object
				PhysicsObjectRef physics_ob = makePhysicsObject(mesh, gl_ob->ob_to_world_matrix, print_output, task_manager);
				mw.physics_world->addObject(physics_ob);

				// Add world object
				{
					Lock lock(mw.world_state->mutex);
					const UID uid(5000);
					WorldObjectRef world_ob = new WorldObject();
					world_ob->uid = uid;
					world_ob->angle = 0;
					world_ob->axis = Vec3f(1,0,0);
					world_ob->model_url = "teapot.obj";
					world_ob->opengl_engine_ob = gl_ob.getPointer();
					world_ob->physics_object = physics_ob.getPointer();
					mw.world_state->objects[uid] = world_ob;

					physics_ob->userdata = world_ob.getPointer();
				}
			}

		/*	{
				Indigo::MeshRef mesh = new Indigo::Mesh();
				MLTLibMaterials mats;
				FormatDecoderObj::streamModel("teapot.obj", *mesh, 1.f, false, mats);

				GLObjectRef ob = new GLObject();
				ob->materials.resize(1);
				ob->materials[0].albedo_rgb = Colour3f(0.6f, 0.2f, 0.2f);
				ob->materials[0].fresnel_scale = 1;

				//ob->ob_to_world_matrix.setToTranslationMatrix(-3.0,1,0.5f);
				ob->ob_to_world_matrix.setToUniformScaleMatrix(5.0f);
				ob->ob_to_world_matrix.setColumn(3, Vec4f(-3.0, -3.0, 0.0, 1.0f));
				ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh);

				mw.glWidget->addObject(ob);

				mw.physics_world->addObject(makePhysicsObject(mesh, ob->ob_to_world_matrix, print_output, task_manager));
			}*/

			// Transparent teapot
			/*{
				Indigo::MeshRef mesh = new Indigo::Mesh();
				FormatDecoderObj::streamModel("N:\\indigo\\trunk\\testfiles\\teapot.obj", *mesh, 1.f);

				GLObjectRef ob = new GLObject();
				ob->materials.resize(1);
				ob->materials[0].albedo_rgb = Colour3f(0.2f, 0.2f, 0.6f);
				ob->materials[0].alpha = 0.4f;
				ob->materials[0].transparent = true;

				ob->ob_to_world_matrix.setToTranslationMatrix(-0.3,1, 0.8f);
				ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh);

				mw.glWidget->addObject(ob);

				mw.physics_world->addObject(makePhysicsObject(mesh, ob->ob_to_world_matrix, print_output, task_manager));
			}

			{
				Indigo::MeshRef mesh = new Indigo::Mesh();
				FormatDecoderObj::streamModel("N:\\indigo\\trunk\\testfiles\\teapot.obj", *mesh, 1.f);

				// Remove shading normals on mesh
				mesh->vert_normals.resize(0);

				GLObjectRef ob = new GLObject();
				ob->materials.resize(1);
				ob->materials[0].albedo_rgb = Colour3f(0.2f, 0.6f, 0.2f);

				ob->ob_to_world_matrix.setToTranslationMatrix(0.3,1,0.5f);
				ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh);

				mw.glWidget->addObject(ob);

				mw.physics_world->addObject(makePhysicsObject(mesh, ob->ob_to_world_matrix, print_output, task_manager));
			}*/
		}
		catch(Indigo::Exception& e)
		{
			conPrint(e.what());
			QMessageBox msgBox;
			msgBox.setText(QtUtils::toQString(e.what()));
			msgBox.exec();
			return 1;
		}


		mw.physics_world->build(task_manager, print_output);



		// Connect IndigoApplication openFileOSX signal to MainWindow openFileOSX slot.
		// It needs to be connected right after handling the possibly early QFileOpenEvent because otherwise it might tigger the slot twice
		// if the event comes in late, between connecting and handling the early signal.
		//QObject::connect(&app, SIGNAL(openFileOSX(const QString)), &mw, SLOT(openFileOSX(const QString)));

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
