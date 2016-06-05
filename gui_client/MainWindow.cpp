
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif


#ifdef _MSC_VER // Qt headers suppress some warnings on Windows, make sure the warning suppression doesn't propagate to our code. See https://bugreports.qt.io/browse/QTBUG-26877
#pragma warning(push, 0) // Disable warnings
#endif
#include "MainWindow.h"
#include "AvatarSettingsDialog.h"
//#include "IndigoApplication.h"
#include <QtCore/QTimer>
#include <QtCore/QProcess>
#include <QtCore/QMimeData>
#include <QtWidgets/QApplication>
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
#include "../networking/networking.h"
#include "../qt/QtUtils.h"
#include "../graphics/formatdecoderobj.h"
#include "../dll/include/IndigoMesh.h"
#include "../dll/IndigoStringUtils.h"
#include "../indigo/TextureServer.h"
#include <clocale>
#include "../indigo/StandardPrintOutput.h"

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
}


Reference<GLObject> globe;


static Reference<GLObject> loadAvatarModel(const std::string& model_url)
{
	// TEMP HACK: Just load a teapot for now :)

	Indigo::MeshRef mesh = new Indigo::Mesh();
	FormatDecoderObj::streamModel("teapot.obj", *mesh, 1.f);

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



	// Send an AvatarTransformUpdate packet to the server if needed.
	if(time_since_update_packet_sent.elapsed() > 0.1)
	{
		// Send AvatarTransformUpdate packet
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

		time_since_update_packet_sent.reset();
	}


	glWidget->updateGL();
}


void MainWindow::on_actionAvatarSettings_triggered()
{
	AvatarSettingsDialog d(this->settings, this->texture_server);
	d.exec();
}


Reference<PhysicsObject> makePhysicsObject(Indigo::MeshRef mesh, const Matrix4f& ob_to_world_matrix, StandardPrintOutput& print_output, Indigo::TaskManager& task_manager)
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
	CameraController::test();

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

		mw.client_thread = new ClientThread();
		mw.client_thread->world_state = mw.world_state;
		mw.client_thread->launch();

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
		{
			GLObjectRef ob = new GLObject();

			ob->ob_to_world_matrix.setToTranslationMatrix(0.0, 2.0, 1.0f);

			ob->mesh_data = mw.glWidget->opengl_engine->makeCapsuleMesh(
				Vec3f(0.2f, 0.1f, 0.2f), 
				Vec3f(0.4f, 0.2f, 0.4f)
			);

			ob->materials.resize(1);
			ob->materials[0].albedo_rgb = Colour3f(0.5f, 0.5f, 0.2f);
			
			mw.glWidget->opengl_engine->addObject(ob);
		}

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
		
			{
				Indigo::MeshRef mesh = new Indigo::Mesh();
				FormatDecoderObj::streamModel("teapot.obj", *mesh, 1.f);

				GLObjectRef ob = new GLObject();
				ob->materials.resize(1);
				ob->materials[0].albedo_rgb = Colour3f(0.6f, 0.2f, 0.2f);
				ob->materials[0].fresnel_scale = 1;

				ob->ob_to_world_matrix.setToTranslationMatrix(-3.0,1,0.5f);
				ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh);

				mw.glWidget->addObject(ob);

				mw.physics_world->addObject(makePhysicsObject(mesh, ob->ob_to_world_matrix, print_output, task_manager));
			}

			{
				Indigo::MeshRef mesh = new Indigo::Mesh();
				FormatDecoderObj::streamModel("teapot.obj", *mesh, 1.f);

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
			}

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
