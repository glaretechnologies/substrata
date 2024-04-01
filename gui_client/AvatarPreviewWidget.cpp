#include "AvatarPreviewWidget.h"


#include "MainOptionsDialog.h"
#include "../dll/include/IndigoMesh.h"
#include "../indigo/TextureServer.h"
#include "../indigo/globals.h"
#include "../graphics/Map2D.h"
#include "../graphics/ImageMap.h"
#include "../graphics/SRGBUtils.h"
#include "../maths/vec3.h"
#include "../maths/GeometrySampling.h"
#include "../utils/Lock.h"
#include "../utils/Mutex.h"
#include "../utils/Clock.h"
#include "../utils/Timer.h"
#include "../utils/Platform.h"
#include "../utils/FileUtils.h"
#include "../utils/Reference.h"
#include "../utils/StringUtils.h"
#include "../utils/TaskManager.h"
#include "../utils/PlatformUtils.h"
#include <QtGui/QMouseEvent>
#include <QtCore/QSettings>


// https://wiki.qt.io/How_to_use_OpenGL_Core_Profile_with_Qt
// https://developer.apple.com/opengl/capabilities/GLInfo_1085_Core.html
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
static QGLFormat makeFormat()
{
	// We need to request a 'core' profile.  Otherwise on OS X, we get an OpenGL 2.1 interface, whereas we require a v3+ interface.
	QGLFormat format;
	// We need to request version 3.2 (or above?) on OS X, otherwise we get legacy version 2.
#ifdef OSX
	format.setVersion(3, 2);
#endif
	format.setProfile(QGLFormat::CoreProfile);
	format.setSampleBuffers(true);
	return format;
}
#endif


AvatarPreviewWidget::AvatarPreviewWidget(QWidget *parent)
:	
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	QOpenGLWidget(parent)
#else
	QGLWidget(makeFormat(), parent)
#endif
{
	viewport_aspect_ratio = 1;

	// Needed to get keyboard events.
	setFocusPolicy(Qt::StrongFocus);

	// Create main task manager.
	// This is for doing work like texture compression and EXR loading, that will be created by LoadTextureTasks etc.
	// Alloc these on the heap as Emscripten may have issues with stack-allocated objects before the emscripten_set_main_loop() call.
	const size_t main_task_manager_num_threads = myClamp<size_t>(PlatformUtils::getNumLogicalProcessors(), 1, 4);
	main_task_manager = new glare::TaskManager("main task manager", main_task_manager_num_threads);
	main_task_manager->setThreadPriorities(MyThread::Priority_Lowest);


	// Create high-priority task manager.
	// For short, processor intensive tasks that the main thread depends on, such as computing animation data for the current frame, or executing Jolt physics tasks.
	const size_t high_priority_task_manager_num_threads = myClamp<size_t>(PlatformUtils::getNumLogicalProcessors(), 1, 4);
	high_priority_task_manager = new glare::TaskManager("high_priority_task_manager", high_priority_task_manager_num_threads);

	mem_allocator = new glare::MallocAllocator();
}


AvatarPreviewWidget::~AvatarPreviewWidget()
{
	// Assume that shutdown() has been called already.
	delete main_task_manager;
	delete high_priority_task_manager;
}


void AvatarPreviewWidget::init(const std::string& base_dir_path_, QSettings* settings_, Reference<TextureServer> texture_server_)
{
	this->makeCurrent();

	base_dir_path = base_dir_path_;
	settings = settings_;
	texture_server = texture_server_;

	OpenGLEngineSettings gl_settings;
	gl_settings.shadow_mapping = true;
	gl_settings.compress_textures = true;
	gl_settings.use_grouped_vbo_allocator = false; // Don't use best-fit allocator, as it uses a lot of GPU mem, and we don't need the perf from it.
	opengl_engine = new OpenGLEngine(gl_settings);

	viewport_w = viewport_h = 100;
}


void AvatarPreviewWidget::shutdown()
{
	// Make context current as we destroy the opengl enegine.
	this->makeCurrent();

	if(opengl_engine.nonNull())
		opengl_engine = NULL;
}


void AvatarPreviewWidget::resizeGL(int width_, int height_)
{
	assert(QGLContext::currentContext() == this->context()); // "There is no need to call makeCurrent() because this has already been done when this function is called." (https://doc.qt.io/qt-5/qglwidget.html#resizeGL)

	viewport_w = width_;
	viewport_h = height_;

	this->opengl_engine->setViewportDims(viewport_w, viewport_h);

	this->opengl_engine->setMainViewportDims(viewport_w, viewport_h);

	viewport_aspect_ratio = (double)width_ / (double)height_;

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	// In Qt6, the GL widget uses a custom framebuffer (defaultFramebufferObject).  We want to make sure we draw to this.
	this->opengl_engine->setTargetFrameBuffer(new FrameBuffer(this->defaultFramebufferObject()));
#endif
}


void AvatarPreviewWidget::initializeGL()
{
	assert(QGLContext::currentContext() == this->context()); // "There is no need to call makeCurrent() because this has already been done when this function is called."  (https://doc.qt.io/qt-5/qglwidget.html#initializeGL)

	opengl_engine->initialise(
		base_dir_path + "/data", // data dir (should contain 'shaders' and 'gl_data')
		texture_server,
		NULL, // print output
		main_task_manager, high_priority_task_manager, mem_allocator
	);
	if(!opengl_engine->initSucceeded())
	{
		conPrint("AvatarPreviewWidget opengl_engine init failed: " + opengl_engine->getInitialisationErrorMsg());
	}

	if(opengl_engine->initSucceeded())
	{
		try
		{
			opengl_engine->setCirrusTexture(opengl_engine->getTexture(base_dir_path + "/data/resources/cirrus.exr"));
		}
		catch(glare::Exception& e)
		{
			conPrint("Error: " + e.what());
		}

		if(settings->value(MainOptionsDialog::BloomKey(), /*default val=*/true).toBool())
			opengl_engine->getCurrentScene()->bloom_strength = 0.3f;
	}


	cam_phi = 0;
	cam_theta = 1.3f;
	cam_dist = 3;
	cam_target_pos = Vec4f(0,0,1,1);

	// Add env mat
	OpenGLMaterial env_mat;
	opengl_engine->setEnvMat(env_mat);


	// Load a ground plane into the GL engine
	{
		const float W = 200;

		GLObjectRef ob = opengl_engine->allocateObject();
		ob->materials.resize(1);
		ob->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(0.8f));
		try
		{
			ob->materials[0].albedo_texture = opengl_engine->getTexture(base_dir_path + "/data/resources/obstacle.png");
		}
		catch(glare::Exception& e)
		{
			assert(0);
			conPrint("ERROR: " + e.what());
		}
		ob->materials[0].roughness = 0.8f;
		ob->materials[0].fresnel_scale = 0.5f;
		ob->materials[0].tex_matrix = Matrix2f(W, 0, 0, W);

		ob->ob_to_world_matrix = Matrix4f::scaleMatrix(W, W, 1) * Matrix4f::translationMatrix(-0.5f, -0.5f, 0);
		ob->mesh_data = opengl_engine->getUnitQuadMeshData();

		opengl_engine->addObject(ob);
	}
}


void AvatarPreviewWidget::paintGL()
{
	assert(QGLContext::currentContext() == this->context()); // "There is no need to call makeCurrent() because this has already been done when this function is called."  (https://doc.qt.io/qt-5/qglwidget.html#initializeGL)

	if(opengl_engine.isNull())
		return;

	const Matrix4f T = Matrix4f::translationMatrix(0.f, cam_dist, 0.f);
	const Matrix4f z_rot = Matrix4f::rotationMatrix(Vec4f(0,0,1,0), cam_phi);
	const Matrix4f x_rot = Matrix4f::rotationMatrix(Vec4f(1,0,0,0), -(cam_theta - Maths::pi_2<float>()));
	const Matrix4f rot = x_rot * z_rot;
	const Matrix4f world_to_camera_space_matrix = T * rot * Matrix4f::translationMatrix(-cam_target_pos);

	const float sensor_width = 0.035f;
	const float lens_sensor_dist = 0.03f;
	const float render_aspect_ratio = viewport_aspect_ratio;

	opengl_engine->setViewportDims(viewport_w, viewport_h);
	opengl_engine->setMaxDrawDistance(100.f);
	opengl_engine->setPerspectiveCameraTransform(world_to_camera_space_matrix, sensor_width, lens_sensor_dist, render_aspect_ratio, /*lens shift up=*/0.f, /*lens shift right=*/0.f);
	opengl_engine->setCurrentTime((float)timer.elapsed());
	opengl_engine->draw();
}


void AvatarPreviewWidget::keyPressEvent(QKeyEvent* e)
{
}


void AvatarPreviewWidget::keyReleaseEvent(QKeyEvent* e)
{
}


void AvatarPreviewWidget::mousePressEvent(QMouseEvent* e)
{
	mouse_move_origin = QCursor::pos();
}


void AvatarPreviewWidget::showEvent(QShowEvent* e)
{
	emit widgetShowSignal();
}


void AvatarPreviewWidget::mouseMoveEvent(QMouseEvent* e)
{
	Qt::MouseButtons mb = e->buttons();

	// Get new mouse position, movement vector and set previous mouse position to new.
	QPoint new_pos = QCursor::pos();
	QPoint delta = new_pos - mouse_move_origin; 

	if(mb & Qt::LeftButton)
	{
		const float move_scale = 0.005f;
		cam_phi += delta.x() * move_scale;
		cam_theta = myClamp<float>(cam_theta - (float)delta.y() * move_scale, 0.01f, Maths::pi<float>() - 0.01f);
	}

	if((mb & Qt::MiddleButton) || (mb & Qt::RightButton))
	{
		const float move_scale = 0.005f;

		const Vec4f forwards = GeometrySampling::dirForSphericalCoords(-cam_phi + Maths::pi_2<float>(), Maths::pi<float>() - cam_theta);
		const Vec4f right = normalise(crossProduct(forwards, Vec4f(0,0,1,0)));
		const Vec4f up = crossProduct(right, forwards);

		cam_target_pos += right * -(float)delta.x() * move_scale + up * (float)delta.y() * move_scale;
	}

	mouse_move_origin = new_pos;
}


void AvatarPreviewWidget::wheelEvent(QWheelEvent* ev)
{
	// Make change proportional to distance value.
	// Mouse wheel scroll up reduces distance.
	cam_dist = myClamp<float>(cam_dist - (cam_dist * ev->angleDelta().y() * 0.002f), 0.01f, 20.f);

	ev->accept(); // We want to kill the event now.
	this->setFocus(); // otherwise this loses focus for some reason.
}
