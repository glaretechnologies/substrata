//#include "IncludeWindows.h"
//#include <GL/gl3w.h>


#include "GlWidget.h"


#include "PlayerPhysics.h"
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
#include "../utils/CameraController.h"
#include "../utils/TaskManager.h"
#include <QtGui/QMouseEvent>
#include <set>
#include <stack>
#include <algorithm>


// https://wiki.qt.io/How_to_use_OpenGL_Core_Profile_with_Qt
// https://developer.apple.com/opengl/capabilities/GLInfo_1085_Core.html
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


GlWidget::GlWidget(QWidget *parent)
:	QGLWidget(makeFormat(), parent),
	cam_controller(NULL)
{
	viewport_aspect_ratio = 1;

	OpenGLEngineSettings settings;
	settings.shadow_mapping = true;
	opengl_engine = new OpenGLEngine(settings);

	SHIFT_down = false;
	W_down = false;
	A_down = false;
	S_down = false;
	D_down = false;

	viewport_w = viewport_h = 100;

	// Needed to get keyboard events.
	setFocusPolicy(Qt::StrongFocus);
}


GlWidget::~GlWidget()
{
	opengl_engine = NULL;
}


void GlWidget::setCameraController(CameraController* cam_controller_)
{
	cam_controller = cam_controller_;
}


void GlWidget::setPlayerPhysics(PlayerPhysics* player_physics_)
{
	player_physics = player_physics_;
}


void GlWidget::resizeGL(int width_, int height_)
{
	viewport_w = width_;
	viewport_h = height_;

	glViewport(0, 0, width_, height_);

	viewport_aspect_ratio = (double)width_ / (double)height_;
}


void GlWidget::initializeGL()
{
	opengl_engine->initialise(
		//"n:/indigo/trunk/opengl/shaders" // shader dir
		"./shaders" // shader dir
	);
	if(!opengl_engine->initSucceeded())
	{
		conPrint("opengl_engine init failed: " + opengl_engine->getInitialisationErrorMsg());
	}
}


void GlWidget::paintGL()
{
	if(cam_controller)
	{
		// Work out current camera transform
		Vec3d cam_pos, up, forwards, right;
		cam_pos = cam_controller->getPosition();
		cam_controller->getBasis(right, up, forwards);

		Matrix4f world_to_camera_space_matrix;
		Matrix4f T;
		T.setToTranslationMatrix(-cam_pos.x, -cam_pos.y, -cam_pos.z);

		Matrix4f rot;
		rot.e[ 0] = (float)right.x;
		rot.e[ 4] = (float)right.y;
		rot.e[ 8] = (float)right.z;
		rot.e[12] = 0;

		rot.e[ 1] = (float)forwards.x;
		rot.e[ 5] = (float)forwards.y;
		rot.e[ 9] = (float)forwards.z;
		rot.e[13] = 0;

		rot.e[ 2] = (float)up.x;
		rot.e[ 6] = (float)up.y;
		rot.e[10] = (float)up.z;
		rot.e[14] = 0;

		rot.e[ 3] = 0;
		rot.e[ 7] = 0;
		rot.e[11] = 0;
		rot.e[15] = 1;

		mul(rot, T, world_to_camera_space_matrix);

		const float sensor_width = 0.035f;
		const float lens_sensor_dist = 0.03f;
		const float render_aspect_ratio = viewport_aspect_ratio;
		opengl_engine->setViewportAspectRatio(viewport_aspect_ratio, viewport_w, viewport_h);
		opengl_engine->setMaxDrawDistance(1000.f);
		opengl_engine->setCameraTransform(world_to_camera_space_matrix, sensor_width, lens_sensor_dist, render_aspect_ratio);
		opengl_engine->draw();
	}
}


void GlWidget::addObject(const Reference<GLObject>& object)
{
	this->makeCurrent();

	// Build materials
	for(size_t i=0; i<object->materials.size(); ++i)
		buildMaterial(object->materials[i]);

	opengl_engine->addObject(object);
}


void GlWidget::addOverlayObject(const Reference<OverlayObject>& object)
{
	this->makeCurrent();

	buildMaterial(object->material);

	opengl_engine->addOverlayObject(object);
}


void GlWidget::setEnvMat(OpenGLMaterial& mat)
{
	this->makeCurrent();

	buildMaterial(mat);
	opengl_engine->setEnvMat(mat);
}


void GlWidget::buildMaterial(OpenGLMaterial& opengl_mat)
{
	try
	{
		if(!opengl_mat.albedo_tex_path.empty() && opengl_mat.albedo_texture.isNull()) // If texture not already loaded:
		{
			std::string use_path;
			try
			{
				use_path = FileUtils::getActualOSPath(opengl_mat.albedo_tex_path);
			}
			catch(FileUtils::FileUtilsExcep&)
			{
				use_path = opengl_mat.albedo_tex_path;
			}

			Reference<Map2D> tex = this->texture_server_ptr->getTexForPath(indigo_base_dir, use_path);
			unsigned int tex_xres = tex->getMapWidth();
			unsigned int tex_yres = tex->getMapHeight();

			if(tex.isType<ImageMapUInt8>())
			{
				ImageMapUInt8* imagemap = static_cast<ImageMapUInt8*>(tex.getPointer());

				if(imagemap->getN() == 3)
				{
					Reference<OpenGLTexture> opengl_tex = new OpenGLTexture();
					opengl_tex->load(tex_xres, tex_yres, imagemap->getData(), opengl_engine, GL_RGB, GL_RGB, GL_UNSIGNED_BYTE, 
						false // nearest filtering
					);
					opengl_mat.albedo_texture = opengl_tex;
				}
			}
		}
		//std::cout << "successfully loaded " << use_path << ", xres = " << tex_xres << ", yres = " << tex_yres << std::endl << std::endl;
	}
	catch(TextureServerExcep& e)
	{
		conPrint("Failed to load texture '" + opengl_mat.albedo_tex_path + "': " + e.what());
	}
	catch(ImFormatExcep& e)
	{
		conPrint("Failed to load texture '" + opengl_mat.albedo_tex_path + "': " + e.what());
	}
}


void GlWidget::keyPressEvent(QKeyEvent* e)
{
	if(this->player_physics)
	{
		SHIFT_down = (e->modifiers() & Qt::ShiftModifier);

		if(e->key() == Qt::Key::Key_Space)
		{
			this->player_physics->processJump(*this->cam_controller);
		}
		if(e->key() == Qt::Key::Key_W)
		{
			W_down = true;
		}
		if(e->key() == Qt::Key::Key_S)
		{
			S_down = true;
		}
		if(e->key() == Qt::Key::Key_A)
		{
			A_down = true;
		}
		if(e->key() == Qt::Key::Key_D)
		{
			D_down = true;
		}
	}

	emit keyPressed(e);
}


void GlWidget::keyReleaseEvent(QKeyEvent* e)
{
	if(this->player_physics)
	{
		SHIFT_down = (e->modifiers() & Qt::ShiftModifier);

		if(e->key() == Qt::Key::Key_W)
		{
			W_down = false;
		}
		if(e->key() == Qt::Key::Key_S)
		{
			S_down = false;
		}
		if(e->key() == Qt::Key::Key_A)
		{
			A_down = false;
		}
		if(e->key() == Qt::Key::Key_D)
		{
			D_down = false;
		}
	}
}


void GlWidget::playerPhyicsThink()
{
	if(W_down)
		this->player_physics->processMoveForwards(1.f, SHIFT_down, *this->cam_controller);
	if(S_down)
		this->player_physics->processMoveForwards(-1.f, SHIFT_down, *this->cam_controller);
	if(A_down)
		this->player_physics->processStrafeRight(-1.f, SHIFT_down, *this->cam_controller);
	if(D_down)
		this->player_physics->processStrafeRight(1.f, SHIFT_down, *this->cam_controller);
}


void GlWidget::mousePressEvent(QMouseEvent* e)
{
	//conPrint("mousePressEvent at " + toString(QCursor::pos().x()) + ", " + toString(QCursor::pos().y()));
	mouse_move_origin = QCursor::pos();
	last_mouse_press_pos = QCursor::pos();
}


void GlWidget::mouseReleaseEvent(QMouseEvent* e)
{
	//conPrint("mouseReleaseEvent at " + toString(QCursor::pos().x()) + ", " + toString(QCursor::pos().y()));

	if((QCursor::pos() - last_mouse_press_pos).manhattanLength() < 4)
	{
		//conPrint("Click at " + toString(QCursor::pos().x()) + ", " + toString(QCursor::pos().y()));
		//conPrint("Click at " + toString(e->pos().x()) + ", " + toString(e->pos().y()));

		emit mouseClicked(e);
	}
}


void GlWidget::showEvent(QShowEvent* e)
{
	emit widgetShowSignal();
}


void GlWidget::mouseMoveEvent(QMouseEvent* e)
{
	if(cam_controller != NULL)// && (e->modifiers() & Qt::AltModifier))
	{
		Qt::MouseButtons mb = e->buttons();
		//double shift_scale = ((e->modifiers() & Qt::ShiftModifier) == 0) ? 1.0 : 0.35; // If shift is held, movement speed is roughly 1/3

		// Get new mouse position, movement vector and set previous mouse position to new.
		QPoint new_pos = QCursor::pos();
		QPoint delta(new_pos.x() - mouse_move_origin.x(),
					 mouse_move_origin.y() - new_pos.y()); // Y+ is down in screenspace, not up as desired.

		if(mb & Qt::LeftButton)  cam_controller->update(Vec3d(0, 0, 0), Vec2d(delta.y(), delta.x())/* * shift_scale*/);
		//if(mb & Qt::MidButton)   cam_controller->update(Vec3d(delta.x(), 0, delta.y()) * shift_scale, Vec2d(0, 0));
		//if(mb & Qt::RightButton) cam_controller->update(Vec3d(0, delta.y(), 0) * shift_scale, Vec2d(0, 0));

		if(mb & Qt::RightButton || mb & Qt::LeftButton || mb & Qt::MidButton)
			emit cameraUpdated();

		mouse_move_origin = new_pos;

		emit mouseMoved(e);
	}
}


void GlWidget::wheelEvent(QWheelEvent* e)
{
	emit mouseWheelSignal(e);
}


void GlWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
	emit mouseDoubleClickedSignal(e);
}
