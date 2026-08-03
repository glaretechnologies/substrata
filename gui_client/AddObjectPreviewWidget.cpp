#include "AddObjectPreviewWidget.h"


#include "MainOptionsDialog.h"
#include "../dll/include/IndigoMesh.h"
#include "../indigo/TextureServer.h"
#include "../indigo/globals.h"
#include "../graphics/Map2D.h"
#include "../graphics/ImageMap.h"
#include "../graphics/SRGBUtils.h"
#include "../graphics/GaussianSplatData.h"
#include "../opengl/GaussianSplatRenderer.h"
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


AddObjectPreviewWidget::AddObjectPreviewWidget(QWidget* parent)
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

	mem_allocator = new glare::MallocAllocator();
}


void AddObjectPreviewWidget::init(const std::string& base_dir_path_, QSettings* settings_, Reference<TextureServer> texture_server_, glare::TaskManager* main_task_manager_, glare::TaskManager* high_priority_task_manager_)
{
	this->makeCurrent();

	base_dir_path = base_dir_path_;
	settings = settings_;
	texture_server = texture_server_;
	main_task_manager = main_task_manager_;
	high_priority_task_manager = high_priority_task_manager_;

	OpenGLEngineSettings gl_settings;
	gl_settings.shadow_mapping = true;
	gl_settings.compress_textures = true;
	gl_settings.use_grouped_vbo_allocator = false; // Don't use best-fit allocator, as it uses a lot of GPU mem, and we don't need the perf from it.
	gl_settings.ssao = settings->value(MainOptionsDialog::SSAOKey(), /*default val=*/true).toBool();
	//gl_settings.use_final_image_buffer = settings_->value(MainOptionsDialog::BloomKey(), /*default val=*/true).toBool();
	opengl_engine = new OpenGLEngine(gl_settings);

	viewport_w = viewport_h = 100;
}


AddObjectPreviewWidget::~AddObjectPreviewWidget()
{
	// Assume that shutdown() has been called already.
}


void AddObjectPreviewWidget::shutdown()
{
	// Make context current as we destroy the opengl engine.
	this->makeCurrent();

	splat_renderer.set(nullptr); // Destroy splat_renderer.  Must happen while the GL context is still current.

	if(opengl_engine.nonNull())
		opengl_engine = NULL;
}


void AddObjectPreviewWidget::resizeGL(int width_, int height_)
{
	assert(QGLContext::currentContext() == this->context()); // "There is no need to call makeCurrent() because this has already been done when this function is called." (https://doc.qt.io/qt-5/qglwidget.html#resizeGL)

	viewport_w = width_;
	viewport_h = height_;

	this->opengl_engine->setViewportDims(viewport_w, viewport_h);


#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	// In Qt6, the GL widget uses a custom framebuffer (defaultFramebufferObject).  We want to make sure we draw to this.
	this->opengl_engine->setTargetFrameBuffer(new FrameBuffer(this->defaultFramebufferObject()));
#endif

	viewport_aspect_ratio = (double)width_ / (double)height_;
}


void AddObjectPreviewWidget::initializeGL()
{
	assert(QGLContext::currentContext() == this->context()); // "There is no need to call makeCurrent() because this has already been done when this function is called."  (https://doc.qt.io/qt-5/qglwidget.html#initializeGL)

	std::string data_dir = base_dir_path + "/data";
#if BUILD_TESTS
	try
	{
		// For development, allow loading opengl engine data, particularly shaders, straight from the repo dir.
		data_dir = PlatformUtils::getEnvironmentVariable("SUBSTRATA_USE_OPENGL_DATA_DIR"); // SUBSTRATA_USE_OPENGL_DATA_DIR can be set to e.g. n:/glare-core/opengl
		conPrint("Using OpenGL data dir from Env var: '" + data_dir + "'");
	}
	catch(glare::Exception&)
	{}
#endif

	opengl_engine->initialise(
		data_dir, // data dir (should contain 'shaders' and 'gl_data')
		texture_server, // texture_server
		NULL, // print output
		main_task_manager, high_priority_task_manager, mem_allocator
	);
	if(!opengl_engine->initSucceeded())
	{
		conPrint("AddObjectPreviewWidget opengl_engine init failed: " + opengl_engine->getInitialisationErrorMsg());
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

		// For previewing .sog Gaussian splat clouds.
		try
		{
			splat_renderer.set(new GaussianSplatRenderer());
			splat_renderer->makeShaders(*opengl_engine, data_dir + "/shaders");
		}
		catch(glare::Exception& e)
		{
			conPrint("Error building Gaussian splat shaders: " + e.what());
			splat_renderer.set(NULL); // Splat previews will just be unavailable.
		}
	}

	cam_phi = 0;
	cam_theta = 1.3f;
	cam_dist = 3;
	cam_target_pos = Vec4f(0,0,0.2f,1);

	// Add env mat
	OpenGLMaterial env_mat;
	opengl_engine->setEnvMat(env_mat);

	// target_marker_ob = opengl_engine->makeAABBObject(cam_target_pos + Vec4f(0,0,0,0), cam_target_pos + Vec4f(0.03f, 0.03f, 0.03f, 0.f), Colour4f(0.6f, 0.6f, 0.2f, 1.f));
	// opengl_engine->addObject(target_marker_ob);


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


void AddObjectPreviewWidget::paintGL()
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

	// NOTE: after the camera transform is set, which think() needs for the depth sort and the covariance projection.
	if(splat_renderer.ptr() && main_task_manager && (splat_renderer->numObjectsInWorld() > 0))
		splat_renderer->think(*opengl_engine, *main_task_manager);

	opengl_engine->draw();
}


void AddObjectPreviewWidget::setPreviewSplatCloud(const Reference<GaussianSplatData>& splat_data, const Vec3f& axis, float angle)
{
	if(!splat_renderer.ptr())
		return;

	this->makeCurrent();

	splat_renderer->removeAllObjects(); // Remove any previously previewed cloud.

	if(splat_data.isNull())
		return;

	// Offset the cloud vertically so it rests on the ground plane, like the mesh preview path does.
	const Matrix4f rot_matrix = Matrix4f::rotationMatrix(normalise(axis.toVec4fVector()), angle);
	const js::AABBox aabb_ws = splat_data->aabb_os.transformedAABBFast(rot_matrix);
	const float z_trans = -aabb_ws.min_[2];

	splat_renderer->addObject(splat_data, /*translation_ws=*/Vec4f(0, 0, z_trans, 1), Quatf::fromAxisAndAngle(normalise(axis.toVec4fVector()), angle),
		/*uniform_scale_ws=*/1.f, *opengl_engine);

	// Meshes are auto-scaled down to a sane size by getScaleForMesh(), but a splat cloud is placed in the world at scale 1,
	// so frame the camera on it instead of scaling it, to keep the preview honest about how big it will be.
	const Vec4f aabb_span = aabb_ws.max_ - aabb_ws.min_;
	const float longest_extent = myMax(aabb_span[0], myMax(aabb_span[1], aabb_span[2]));

	cam_target_pos = Vec4f(0, 0, aabb_span[2] * 0.5f, 1);
	cam_dist = myMax(longest_extent * 1.5f, 0.5f);
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
		
		/*conPrint("forwards: " + forwards.toStringNSigFigs(3));
		conPrint("right: " + right.toStringNSigFigs(3));
		conPrint("up: " + up.toStringNSigFigs(3));
		conPrint("cam_target_pos: " + cam_target_pos.toStringNSigFigs(3));*/

		//target_marker_ob->ob_to_world_matrix.setColumn(3, cam_target_pos);
		//opengl_engine->updateObjectTransformData(*target_marker_ob);
	}

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
