#include "AddObjectPreviewWidget.h"


#include "../dll/include/IndigoMesh.h"
#include "../indigo/TextureServer.h"
#include "../indigo/globals.h"
#include "../graphics/Map2D.h"
#include "../graphics/imformatdecoder.h"
#include "../graphics/ImageMap.h"
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
#include <QtGui/QMouseEvent>
#include <set>
#include <stack>
#include <algorithm>


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


AddObjectPreviewWidget::AddObjectPreviewWidget(QWidget *parent)
:	
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	QOpenGLWidget(parent)
#else
	QGLWidget(makeFormat(), parent)
#endif
{
	viewport_aspect_ratio = 1;

	OpenGLEngineSettings settings;
	settings.shadow_mapping = true;
	settings.compress_textures = true;
	opengl_engine = new OpenGLEngine(settings);

	viewport_w = viewport_h = 100;

	// Needed to get keyboard events.
	setFocusPolicy(Qt::StrongFocus);
}


AddObjectPreviewWidget::~AddObjectPreviewWidget()
{
	shutdown();
}


void AddObjectPreviewWidget::shutdown()
{
	if(opengl_engine.nonNull())
	{
		// Make context current as we destroy the opengl enegine.
		this->makeCurrent();
		opengl_engine = NULL;
	}
}


void AddObjectPreviewWidget::resizeGL(int width_, int height_)
{
	viewport_w = width_;
	viewport_h = height_;

	glViewport(0, 0, width_, height_);

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	// In Qt6, the GL widget uses a custom framebuffer (defaultFramebufferObject).  We want to make sure we draw to this.
	this->opengl_engine->setTargetFrameBuffer(new FrameBuffer(this->defaultFramebufferObject()));
#endif

	viewport_aspect_ratio = (double)width_ / (double)height_;
}


void AddObjectPreviewWidget::initializeGL()
{
	opengl_engine->initialise(
		//"n:/indigo/trunk/opengl", // data dir
		base_dir_path + "/data", // data dir (should contain 'shaders' and 'gl_data')
		texture_server_ptr,
		NULL // print output
	);
	if(!opengl_engine->initSucceeded())
	{
		conPrint("AddObjectPreviewWidget opengl_engine init failed: " + opengl_engine->getInitialisationErrorMsg());
	}

	if(opengl_engine->initSucceeded())
	{
		try
		{
			opengl_engine->setCirrusTexture(opengl_engine->getTexture(base_dir_path + "/resources/cirrus.exr"));
		}
		catch(glare::Exception& e)
		{
			conPrint("Error: " + e.what());
		}
	}

	cam_phi = 0;
	cam_theta = 1.3f;
	cam_dist = 3;
	cam_target_pos = Vec4f(0,0,0.2f,1);

	// Add env mat
	{
		OpenGLMaterial env_mat;
		try
		{
			env_mat.albedo_texture = opengl_engine->getTexture(base_dir_path + "/resources/sky_no_sun.exr");
		}
		catch(glare::Exception& e)
		{
			assert(0);
			conPrint("ERROR: " + e.what());
		}
		env_mat.tex_matrix = Matrix2f(-1 / Maths::get2Pi<float>(), 0, 0, 1 / Maths::pi<float>());

		opengl_engine->setEnvMat(env_mat);
	}

	// target_marker_ob = opengl_engine->makeAABBObject(cam_target_pos + Vec4f(0,0,0,0), cam_target_pos + Vec4f(0.03f, 0.03f, 0.03f, 0.f), Colour4f(0.6f, 0.6f, 0.2f, 1.f));
	// opengl_engine->addObject(target_marker_ob);


	// Load a ground plane into the GL engine
	{
		const float W = 200;

		GLObjectRef ob = new GLObject();
		ob->materials.resize(1);
		ob->materials[0].albedo_rgb = Colour3f(0.9f);
		try
		{
			ob->materials[0].albedo_texture = opengl_engine->getTexture("resources/obstacle.png");
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


void AddObjectPreviewWidget::paintGL()
{
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

	opengl_engine->setViewport(viewport_w, viewport_h);
	opengl_engine->setMaxDrawDistance(100.f);
	opengl_engine->setPerspectiveCameraTransform(world_to_camera_space_matrix, sensor_width, lens_sensor_dist, render_aspect_ratio, /*lens shift up=*/0.f, /*lens shift right=*/0.f);
	opengl_engine->setCurrentTime((float)timer.elapsed());
	opengl_engine->draw();
}


void AddObjectPreviewWidget::addObject(const Reference<GLObject>& object)
{
	this->makeCurrent();

	opengl_engine->addObject(object);
}


void AddObjectPreviewWidget::addOverlayObject(const Reference<OverlayObject>& object)
{
	this->makeCurrent();

	opengl_engine->addOverlayObject(object);
}


void AddObjectPreviewWidget::keyPressEvent(QKeyEvent* e)
{
}


void AddObjectPreviewWidget::keyReleaseEvent(QKeyEvent* e)
{
}


void AddObjectPreviewWidget::mousePressEvent(QMouseEvent* e)
{
	mouse_move_origin = QCursor::pos();
}


void AddObjectPreviewWidget::showEvent(QShowEvent* e)
{
	emit widgetShowSignal();
}


void AddObjectPreviewWidget::mouseMoveEvent(QMouseEvent* e)
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
		//const Vec4f right(1,0,0,0);
		//const Vec4f up(0,0,1,0);

		cam_target_pos += right * -(float)delta.x() * move_scale + up * (float)delta.y() * move_scale;
		
		conPrint("forwards: " + forwards.toStringNSigFigs(3));
		conPrint("right: " + right.toStringNSigFigs(3));
		conPrint("up: " + up.toStringNSigFigs(3));
		conPrint("cam_target_pos: " + cam_target_pos.toStringNSigFigs(3));

		//target_marker_ob->ob_to_world_matrix.setColumn(3, cam_target_pos);
		//opengl_engine->updateObjectTransformData(*target_marker_ob);
	}

	//if(mb & Qt::RightButton || mb & Qt::LeftButton || mb & Qt::MidButton)
	//	emit cameraUpdated();

	mouse_move_origin = new_pos;
}


void AddObjectPreviewWidget::wheelEvent(QWheelEvent* ev)
{
	// Make change proportional to distance value.
	// Mouse wheel scroll up reduces distance.
	cam_dist = myClamp<float>(cam_dist - (cam_dist * ev->angleDelta().y() * 0.002f), 0.01f, 10000.f);

	ev->accept(); // We want to kill the event now.
	this->setFocus(); // otherwise this loses focus for some reason.
}
