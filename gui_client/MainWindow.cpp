
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif


#ifdef _MSC_VER // Qt headers suppress some warnings on Windows, make sure the warning suppression doesn't propagate to our code. See https://bugreports.qt.io/browse/QTBUG-26877
#pragma warning(push, 0) // Disable warnings
#endif
#include "MainWindow.h"
#include "AvatarSettingsDialog.h"
#include "AddObjectDialog.h"
#include "ModelLoading.h"
#include "UploadResourceThread.h"
#include "DownloadResourcesThread.h"
//#include "IndigoApplication.h"
#include <QtCore/QTimer>
#include <QtCore/QProcess>
#include <QtCore/QMimeData>
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


static const std::string server_hostname = "217.155.32.43";
const int server_port = 7654;


MainWindow::MainWindow(const std::string& base_dir_path_, const std::string& appdata_path_, const ArgumentParser& args, QWidget *parent)
:	base_dir_path(base_dir_path_),
	appdata_path(appdata_path_),
	parsed_args(args), 
	QMainWindow(parent)
{
	setupUi(this);

	settings = new QSettings("Glare Technologies", "Cyberspace");

	// Load main window geometry and state
	this->restoreGeometry(settings->value("mainwindow/geometry").toByteArray());
	this->restoreState(settings->value("mainwindow/windowState").toByteArray());

	connect(this->chatPushButton, SIGNAL(clicked()), this, SLOT(sendChatMessageSlot()));
	connect(this->chatMessageLineEdit, SIGNAL(returnPressed()), this, SLOT(sendChatMessageSlot()));
	connect(this->glWidget, SIGNAL(mouseClicked(QMouseEvent*)), this, SLOT(glWidgetMouseClicked(QMouseEvent*)));
	connect(this->glWidget, SIGNAL(mouseMoved(QMouseEvent*)), this, SLOT(glWidgetMouseMoved(QMouseEvent*)));
	connect(this->glWidget, SIGNAL(keyPressed(QKeyEvent*)), this, SLOT(glWidgetKeyPressed(QKeyEvent*)));
	connect(this->glWidget, SIGNAL(mouseWheelSignal(QWheelEvent*)), this, SLOT(glWidgetMouseWheelEvent(QWheelEvent*)));

	this->resources_dir = "resources"; // "./resources_" + toString(PlatformUtils::getProcessID());
	FileUtils::createDirIfDoesNotExist(this->resources_dir);

	conPrint("resources_dir: " + resources_dir);
	resource_manager = new ResourceManager(this->resources_dir);
}


void MainWindow::initialise()
{
	setWindowTitle("Cyberspace");

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

	this->glWidget->makeCurrent();
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
	options.use_embree = false;
	phy_ob->geometry->build(".", options, print_output, false, task_manager);

	phy_ob->geometry->buildJSTris();
				
	phy_ob->ob_to_world = ob_to_world_matrix;
	return phy_ob;
}


static Reference<GLObject> loadAvatarModel(const std::string& model_url)
{
	// TEMP HACK: Just load a teapot for now :)

	Indigo::MeshRef mesh = new Indigo::Mesh();
	MLTLibMaterials mats;
	FormatDecoderObj::streamModel("teapot.obj", *mesh, 1.f, true, mats);

	GLObjectRef ob = new GLObject();
	ob->materials.resize(1);
	ob->materials[0].albedo_rgb = Colour3f(0.3f, 0.5f, 0.4f);
	ob->materials[0].fresnel_scale = 1;

	ob->ob_to_world_matrix.setToTranslationMatrix(0, 0, 0);
	ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh);
	return ob;
}

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
				this->chatMessagesTextEdit->append(QtUtils::toQString(m->name + ": " + m->msg));
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

				// Since we have a new downloaded resource, iterate over objects and if they were using a placeholder model for this resource, load the proper model.
				try
				{
					Lock lock(this->world_state->mutex);

					for(auto it = this->world_state->objects.begin(); it != this->world_state->objects.end(); ++it)
					{
						WorldObject* ob = it->second.getPointer();
						if(ob->using_placeholder_model && (ob->model_url == m->URL))
						{
							// Remove placeholder GL object
							assert(ob->opengl_engine_ob.nonNull());
							glWidget->opengl_engine->removeObject(ob->opengl_engine_ob);

							conPrint("Adding Object to OpenGL Engine, UID " + toString(ob->uid.value()));
							//const std::string path = resources_dir + "/" + ob->model_url;
							const std::string path = this->resource_manager->pathForURL(ob->model_url);

							// Make GL object, add to OpenGL engine
							Indigo::MeshRef mesh;
							const Matrix4f ob_to_world_matrix = Matrix4f::translationMatrix((float)ob->pos.x, (float)ob->pos.y, (float)ob->pos.z) * Matrix4f::rotationMatrix(ob->axis.toVec4fVector(), ob->angle);
							GLObjectRef gl_ob = ModelLoading::makeGLObjectForModelFile(path, ob_to_world_matrix, mesh);
							ob->opengl_engine_ob = gl_ob;
							glWidget->addObject(gl_ob);

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
	
		glWidget->opengl_engine->updateObjectTransformData(*globe.getPointer());
	}

	glWidget->playerPhyicsThink();

	// Process player physics
	Vec4f campos = this->cam_controller.getPosition().toVec4fPoint();
	player_physics.update(*this->physics_world, dt, campos);
	this->cam_controller.setPosition(toVec3d(campos));

	// Update avatar graphics
	try
	{
		Lock lock(this->world_state->mutex);

		//for(size_t i=0; i<this->world_state->avatars.size(); ++i)
		for(auto it = this->world_state->avatars.begin(); it != this->world_state->avatars.end();)
		{
			const Avatar* avatar = it->second.getPointer();
			if(avatar->state == Avatar::State_Dead)
			{
				conPrint("Removing avatar.");
				// Remove any OpenGL object for it
				if(avatar_gl_objects.count(avatar) > 0)
				{
					this->glWidget->opengl_engine->removeObject(avatar_gl_objects[avatar]);
					avatar_gl_objects.erase(avatar);
				}

				// Remove avatar from avatar map
				auto old_avatar_iterator = it;
				it++;
				this->world_state->avatars.erase(old_avatar_iterator);
			}
			else
			{
				if(avatar->uid != this->client_thread->client_avatar_uid) // Don't render our own Avatar
				{
					bool new_object = false;

					if(avatar_gl_objects.count(avatar) == 0)
					{
						// No model for avatar yet
						GLObjectRef ob = loadAvatarModel(avatar->model_url);

						avatar_gl_objects[avatar] = ob;
						new_object = true;
					}

					assert(avatar_gl_objects.count(avatar) == 1);

					// Update transform for avatar
					GLObjectRef ob = avatar_gl_objects[avatar];
					ob->ob_to_world_matrix.setToRotationMatrix(avatar->axis.toVec4fVector(), avatar->angle);
					ob->ob_to_world_matrix.setColumn(3, Vec4f(avatar->pos.x, avatar->pos.y, avatar->pos.z, 1.f));

					if(new_object)
					{
						conPrint("Adding avatar to OpenGL Engine, UID " + toString(avatar->uid.value()));
						this->glWidget->addObject(ob);
					}
					else
					{
						this->glWidget->opengl_engine->updateObjectTransformData(*ob);
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
			if(ob->from_remote_dirty)
			{
				if(ob->state == WorldObject::State_Dead)
				{
					conPrint("Removing WorldObject.");
				
					// Remove any OpenGL object for it
					if(ob->opengl_engine_ob.nonNull())
						this->glWidget->opengl_engine->removeObject(ob->opengl_engine_ob);

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
						const Matrix4f ob_to_world_matrix = Matrix4f::translationMatrix((float)ob->pos.x, (float)ob->pos.y, (float)ob->pos.z) * Matrix4f::rotationMatrix(ob->axis.toVec4fVector(), ob->angle);

						// See if we have the file downloaded
						//const std::string path = resources_dir + "/" + ob->model_url;
						const std::string path = this->resource_manager->pathForURL(ob->model_url);
						if(!FileUtils::fileExists(path))
						{
							conPrint("Don't have model '" + ob->model_url + "' on disk, using placeholder.");

							// Use a temporary placeholder model.
							GLObjectRef cube_gl_ob = glWidget->opengl_engine->makeAABBObject(Vec4f(0,0,0,1), Vec4f(1,1,1,1), Colour4f(0.6f, 0.2f, 0.2, 0.5f));
							cube_gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(-0.5f, -0.5f, -0.5f) * ob_to_world_matrix;
							ob->opengl_engine_ob = cube_gl_ob;
							glWidget->addObject(cube_gl_ob);

							ob->using_placeholder_model = true;

							// Enqueue download of model
							this->resource_download_thread_manager.enqueueMessage(new DownloadResourceMessage(ob->model_url));
						}
						else
						{
							conPrint("Adding Object to OpenGL Engine, UID " + toString(ob->uid.value()));

							// Make GL object, add to OpenGL engine
							Indigo::MeshRef mesh;
							GLObjectRef gl_ob = ModelLoading::makeGLObjectForModelFile(path, ob_to_world_matrix, mesh);
							ob->opengl_engine_ob = gl_ob;
							glWidget->addObject(gl_ob);

							// Make physics object
							StandardPrintOutput print_output;
							Indigo::TaskManager task_manager;
							PhysicsObjectRef physics_ob = makePhysicsObject(mesh, ob_to_world_matrix, print_output, task_manager);
							ob->physics_object = physics_ob;
							physics_ob->userdata = ob;
							physics_world->addObject(physics_ob);
						}

						ob->from_remote_dirty = false;
					}
					else
					{
						// Update transform for object in OpenGL engine
						if(ob != selected_ob.getPointer()) // Don't update the selected object based on network messages, we will consider the local transform for it authoritative.
						{
							ob->opengl_engine_ob->ob_to_world_matrix.setToRotationMatrix(ob->axis.toVec4fVector(), ob->angle);
							ob->opengl_engine_ob->ob_to_world_matrix.setColumn(3, Vec4f(ob->pos.x, ob->pos.y, ob->pos.z, 1.f));
							this->glWidget->opengl_engine->updateObjectTransformData(*ob->opengl_engine_ob);

							// Update in physics engine
							ob->physics_object->ob_to_world = ob->opengl_engine_ob->ob_to_world_matrix;
							physics_world->updateObjectTransformData(*ob->physics_object);
						}

						ob->from_remote_dirty = false;
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
		this->selected_ob->pos = toVec3d(new_ob_pos);

		// Set graphics object pos and update in opengl engine.
		GLObjectRef opengl_ob(this->selected_ob->opengl_engine_ob);
		opengl_ob->ob_to_world_matrix.setColumn(3, new_ob_pos);
		this->glWidget->opengl_engine->updateObjectTransformData(*opengl_ob);

		// Update physics object
		this->selected_ob->physics_object->ob_to_world.setColumn(3, new_ob_pos);
		this->physics_world->updateObjectTransformData(*this->selected_ob->physics_object);

		// Mark as from-local-dirty to send an object transform updated message to the server
		this->selected_ob->from_local_dirty = true;
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

		//============ Send any ObjectTransformUpdates needed ===========
		{
			Lock lock(this->world_state->mutex);

			for(auto it = this->world_state->objects.begin(); it != this->world_state->objects.end(); ++it)
			{
				WorldObject* world_ob = it->second.getPointer();
				if(world_ob->from_local_dirty)
				{
					SocketBufferOutStream packet;
					packet.writeUInt32(ObjectTransformUpdate);
					writeToStream(world_ob->uid, packet);
					writeToStream(Vec3d(world_ob->pos), packet);
					writeToStream(Vec3f(world_ob->axis), packet); // TODO: rotation
					packet.writeFloat(world_ob->angle);

					std::string packet_string(packet.buf.size(), '\0');
					std::memcpy(&packet_string[0], packet.buf.data(), packet.buf.size());

					this->client_thread->enqueueDataToSend(packet_string);

					world_ob->from_local_dirty = false;
				}
			}
		}


		time_since_update_packet_sent.reset();
	}


	glWidget->updateGL();
}


void MainWindow::on_actionAvatarSettings_triggered()
{
	AvatarSettingsDialog d(this->settings, this->texture_server);
	d.exec();
}


void MainWindow::on_actionAddObject_triggered()
{
	AddObjectDialog d(this->settings, this->texture_server);
	if(d.exec() == QDialog::Accepted)
	{
		// Try and load model
		try
		{
			this->glWidget->makeCurrent();

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
				packet.writeUInt32(ObjectCreated);
				packet.writeStringLengthFirst(URL);
				packet.writeUInt64(d.model_hash);
				writeToStream(ob_pos, packet); // pos
				writeToStream(Vec3f(0,0,1), packet); // axis
				packet.writeFloat(0.f); // angle

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


void MainWindow::sendChatMessageSlot()
{
	//conPrint("MainWindow::sendChatMessageSlot()");

	const std::string name = QtUtils::toIndString(settings->value("username").toString());
	const std::string message = QtUtils::toIndString(this->chatMessageLineEdit->text());

	// Make message packet and enqueue to send
	SocketBufferOutStream packet;
	packet.writeUInt32(ChatMessageID);
	packet.writeStringLengthFirst(name);
	packet.writeStringLengthFirst(message);

	std::string packet_string(packet.buf.size(), '\0');
	std::memcpy(&packet_string[0], packet.buf.data(), packet.buf.size());

	this->client_thread->enqueueDataToSend(packet_string);

	this->chatMessageLineEdit->clear();
}


void MainWindow::glWidgetMouseClicked(QMouseEvent* e)
{
	//conPrint("MainWindow::glWidgetMouseClicked()");

	// Trace ray through scene
	ThreadContext thread_context;
	const Vec4f origin = this->cam_controller.getPosition().toVec4fPoint();
	const Vec4f forwards = cam_controller.getForwardsVec().toVec4fVector();
	const Vec4f right = cam_controller.getRightVec().toVec4fVector();
	const Vec4f up = cam_controller.getUpVec().toVec4fVector();

	const float sensor_width = 0.035f;
	const float sensor_height = sensor_width / glWidget->viewport_aspect_ratio;
	const float lens_sensor_dist = 0.03f;

	const float s_x = sensor_width *  (float)(e->pos().x() - glWidget->geometry().width() /2) / glWidget->geometry().width(); // dist right on sensor from centre of sensor
	const float s_y = sensor_height * (float)(e->pos().y() - glWidget->geometry().height()/2) / glWidget->geometry().height(); // dist down on sensor from centre of sensor

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
		{
			for(size_t z=0; z<selected_ob->opengl_engine_ob->materials.size(); ++z)
				selected_ob->opengl_engine_ob->materials[z].selected = false;
		}

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
			for(size_t z=0; z<selected_ob->opengl_engine_ob->materials.size(); ++z)
				selected_ob->opengl_engine_ob->materials[z].selected = true;
		}
	}
	else
	{
		// Deselect any currently selected object
		if(this->selected_ob.nonNull())
		{
			for(size_t z=0; z<selected_ob->opengl_engine_ob->materials.size(); ++z)
				selected_ob->opengl_engine_ob->materials[z].selected = false;
		}

		// deslect object
		this->selected_ob = NULL;
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

	const Matrix4f new_ob_to_world = Matrix4f::translationMatrix(ob->pos.toVec4fPoint()) * Matrix4f::rotationMatrix(ob->axis.toVec4fVector(), ob->angle);

	// Update in opengl engine.
	GLObjectRef opengl_ob(ob->opengl_engine_ob);
	opengl_ob->ob_to_world_matrix = new_ob_to_world;
	this->glWidget->opengl_engine->updateObjectTransformData(*opengl_ob);

	// Update physics object
	ob->physics_object->ob_to_world = new_ob_to_world;
	this->physics_world->updateObjectTransformData(*this->selected_ob->physics_object);
}


void MainWindow::glWidgetKeyPressed(QKeyEvent* e)
{
	if(e->key() == Qt::Key::Key_Escape)
	{
		if(this->selected_ob.nonNull())
		{
			// Mark the materials on the hit object as unselected
			for(size_t z=0; z<selected_ob->opengl_engine_ob->materials.size(); ++z)
				selected_ob->opengl_engine_ob->materials[z].selected = false;

			// Deselect object
			this->selected_ob = NULL;
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

		mw.client_thread = new ClientThread(&mw.msg_queue, server_hostname, server_port, &mw);
		mw.client_thread->world_state = mw.world_state;
		//mw.client_thread->launch();
		mw.client_thread_manager.addThread(mw.client_thread);

		

		mw.physics_world = new PhysicsWorld();


		//CameraController cam_controller;
		mw.cam_controller.setPosition(Vec3d(0,0,4.7));
		mw.glWidget->setCameraController(&mw.cam_controller);
		mw.glWidget->setPlayerPhysics(&mw.player_physics);
		mw.cam_controller.setMoveScale(0.3f);

		TextureServer texture_server;
		mw.texture_server = &texture_server;
		mw.glWidget->texture_server_ptr = &texture_server;

		const float sun_phi = 1.f;
		const float sun_theta = 0.9510680884f;
		mw.glWidget->opengl_engine->setSunDir(normalise(Vec4f(std::cos(sun_phi) * sin(sun_theta), std::sin(sun_phi) * sun_theta, cos(sun_theta), 0)));

		mw.glWidget->opengl_engine->setEnvMapTransform(Matrix3f::rotationMatrix(Vec3f(0,0,1), sun_phi));

		/*
		Set env material
		*/
		{
			OpenGLMaterial env_mat;
			env_mat.albedo_tex_path = "sky.png";
			env_mat.tex_matrix = Matrix2f(-1 / Maths::get2Pi<float>(), 0, 0, 1 / Maths::pi<float>());

			mw.glWidget->setEnvMat(env_mat);
		}


		//TEMP: make an arrow
		{
			GLObjectRef arrow = mw.glWidget->opengl_engine->makeArrowObject(Vec4f(1,1,1,1), Vec4f(2,1,1,1), Colour4f(0.6, 0.2, 0.2, 1.f), 1.f);
			mw.glWidget->opengl_engine->addObject(arrow);
		}
		{
			GLObjectRef arrow = mw.glWidget->opengl_engine->makeArrowObject(Vec4f(1,1,1,1), Vec4f(1,2,1,1), Colour4f(0.2, 0.6, 0.2, 1.f), 1.f);
			mw.glWidget->opengl_engine->addObject(arrow);
		}
		{
			GLObjectRef arrow = mw.glWidget->opengl_engine->makeArrowObject(Vec4f(1,1,1,1), Vec4f(1,1,2,1), Colour4f(0.2, 0.2, 0.6, 1.f), 1.f);
			mw.glWidget->opengl_engine->addObject(arrow);
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

			mw.glWidget->addOverlayObject(ob);
		}
		


		/*
		Load a ground plane into the GL engine
		*/
		if(true)
		{
			Indigo::MeshRef mesh = new Indigo::Mesh();
			mesh->addVertex(Indigo::Vec3f(-100, -100, 0), Indigo::Vec3f(0,0,1));
			mesh->addVertex(Indigo::Vec3f( 100, -100, 0), Indigo::Vec3f(0,0,1));
			mesh->addVertex(Indigo::Vec3f( 100,  100, 0), Indigo::Vec3f(0,0,1));
			mesh->addVertex(Indigo::Vec3f(-100,  100, 0), Indigo::Vec3f(0,0,1));
			
			mesh->num_uv_mappings = 1;
			mesh->addUVs(Indigo::Vector<Indigo::Vec2f>(1, Indigo::Vec2f(0,0)));
			mesh->addUVs(Indigo::Vector<Indigo::Vec2f>(1, Indigo::Vec2f(100,0)));
			mesh->addUVs(Indigo::Vector<Indigo::Vec2f>(1, Indigo::Vec2f(100,100)));
			mesh->addUVs(Indigo::Vector<Indigo::Vec2f>(1, Indigo::Vec2f(0,100)));
			
			uint32 indices[] = {0, 1, 2, 3};
			mesh->addQuad(indices, indices, 0);

			mesh->endOfModel();

			GLObjectRef ob = new GLObject();
			ob->materials.resize(1);
			ob->materials[0].albedo_rgb = Colour3f(0.7f, 0.7f, 0.7f);
			ob->materials[0].albedo_tex_path = "obstacle.png";
			ob->materials[0].phong_exponent = 10.f;

			ob->ob_to_world_matrix.setToTranslationMatrix(0,0,0);
			ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh);

			mw.glWidget->addObject(ob);

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
				ob->materials[0].phong_exponent = 1000.f;

				Matrix4f scale;
				scale.setToUniformScaleMatrix(100.f);
				Matrix4f trans;
				trans.setToTranslationMatrix(10,10,0.f);
				mul(trans, scale, ob->ob_to_world_matrix);
				ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh);

				mw.glWidget->addObject(ob);

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
				ob->materials[0].phong_exponent = 1000.f;

				Matrix4f scale;
				scale.setToUniformScaleMatrix(1.f);
				Matrix4f trans;
				trans.setToTranslationMatrix(-10,10,0.f);
				mul(trans, scale, ob->ob_to_world_matrix);
				ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh);

				mw.glWidget->addObject(ob);

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
				gl_ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh);

				mw.glWidget->addObject(gl_ob);

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
