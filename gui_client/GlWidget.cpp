/*=====================================================================
GlWidget.cpp
------------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#include "GlWidget.h"


#include "PlayerPhysics.h"
#include "CameraController.h"
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
	cam_controller(NULL),
	current_time(0.f),
	cam_rot_on_mouse_move_enabled(true),
	max_draw_dist(1000.f),
	gamepad(NULL)
{
	viewport_aspect_ratio = 1;

	OpenGLEngineSettings settings;
	settings.enable_debug_output = true;
	settings.shadow_mapping = true;
	settings.compress_textures = true;
	settings.depth_fog = true;
	opengl_engine = new OpenGLEngine(settings);

	SHIFT_down = false;
	W_down = false;
	A_down = false;
	S_down = false;
	D_down = false;
	space_down = false;
	C_down = false;
	left_down = false;
	right_down = false;

	viewport_w = viewport_h = 100;

	// Needed to get keyboard events.
	setFocusPolicy(Qt::StrongFocus);

	setMouseTracking(true); // Set this so we get mouse move events even when a mouse button is not down.


	gamepad_init_timer = new QTimer(this);
	gamepad_init_timer->setSingleShot(true);
	connect(gamepad_init_timer, SIGNAL(timeout()), this, SLOT(initGamepadsSlot()));
	gamepad_init_timer->start(/*msec=*/500); 

	//// See if we have any attached gamepads
	//QGamepadManager* manager = QGamepadManager::instance();

	//const QList<int> list = manager->connectedGamepads();

	//if(!list.isEmpty())
	//{
	//	gamepad = new QGamepad(list.at(0));

	//	connect(gamepad, SIGNAL(axisLeftXChanged(double)), this, SLOT(gamepadInputSlot()));
	//	connect(gamepad, SIGNAL(axisLeftYChanged(double)), this, SLOT(gamepadInputSlot()));
	//}
}


void GlWidget::initGamepadsSlot()
{
	// See if we have any attached gamepads
	QGamepadManager* manager = QGamepadManager::instance();

	const QList<int> list = manager->connectedGamepads();

	if(!list.isEmpty())
	{
		gamepad = new QGamepad(list.at(0));

		connect(gamepad, SIGNAL(axisLeftXChanged(double)), this, SLOT(gamepadInputSlot()));
		connect(gamepad, SIGNAL(axisLeftYChanged(double)), this, SLOT(gamepadInputSlot()));
	}
}


void GlWidget::init()
{
	
}


void GlWidget::gamepadInputSlot()
{

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

	//glViewport(0, 0, width_, height_);

	viewport_aspect_ratio = (double)width_ / (double)height_;

	this->opengl_engine->setViewport(viewport_w, viewport_h);

	this->opengl_engine->setMainViewport(viewport_w, viewport_h);
}


void GlWidget::initializeGL()
{
	assert(this->texture_server_ptr);

	opengl_engine->initialise(
		//"n:/indigo/trunk/opengl", // data dir
		base_dir_path + "/data", // data dir (should contain 'shaders' and 'gl_data')
		this->texture_server_ptr
	);
	if(!opengl_engine->initSucceeded())
	{
		conPrint("opengl_engine init failed: " + opengl_engine->getInitialisationErrorMsg());
		initialisation_error_msg = opengl_engine->getInitialisationErrorMsg();
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

		const Matrix4f rot = Matrix4f(right.toVec4fVector(), forwards.toVec4fVector(), up.toVec4fVector(), Vec4f(0,0,0,1)).getTranspose();

		Matrix4f world_to_camera_space_matrix;
		rot.rightMultiplyWithTranslationMatrix(-cam_pos.toVec4fVector(), /*result=*/world_to_camera_space_matrix);

		const float sensor_width = sensorWidth();
		const float lens_sensor_dist = lensSensorDist();
		const float render_aspect_ratio = viewport_aspect_ratio;
		opengl_engine->setViewport(viewport_w, viewport_h);
		opengl_engine->setNearDrawDistance(0.22f);
		opengl_engine->setMaxDrawDistance(max_draw_dist);
		opengl_engine->setPerspectiveCameraTransform(world_to_camera_space_matrix, sensor_width, lens_sensor_dist, render_aspect_ratio, /*lens shift up=*/0.f, /*lens shift right=*/0.f);
		opengl_engine->setCurrentTime(current_time);
		opengl_engine->draw();
	}

	//conPrint("FPS: " + doubleToStringNSigFigs(1 / timer.elapsed(), 1));
	//timer.reset();
}


void GlWidget::addObject(const Reference<GLObject>& object, bool force_load_textures_immediately)
{
	this->makeCurrent();
	if(!opengl_engine->initSucceeded())
		return;

	if(force_load_textures_immediately)
	{
		try
		{
			for(size_t i=0; i<object->materials.size(); ++i)
				if(!object->materials[i].tex_path.empty())
					object->materials[i].albedo_texture = opengl_engine->getTexture(object->materials[i].tex_path);
		}
		catch(glare::Exception& e)
		{
			conPrint("ERROR: " + e.what());
		}
	}

	opengl_engine->addObject(object);
}


void GlWidget::removeObject(const Reference<GLObject>& object)
{
	this->makeCurrent();

	opengl_engine->removeObject(object);
}


void GlWidget::addOverlayObject(const Reference<OverlayObject>& object)
{
	this->makeCurrent();

	opengl_engine->addOverlayObject(object);
}


void GlWidget::setEnvMat(OpenGLMaterial& mat)
{
	this->makeCurrent();

	opengl_engine->setEnvMat(mat);
}


void GlWidget::keyPressEvent(QKeyEvent* e)
{
	if(this->player_physics)
	{
		SHIFT_down = (e->modifiers() & Qt::ShiftModifier);

		if(e->key() == Qt::Key::Key_Space)
		{
			this->player_physics->processJump(*this->cam_controller);
			space_down = true;
		}
		else if(e->key() == Qt::Key::Key_W)
		{
			W_down = true;
		}
		else if(e->key() == Qt::Key::Key_S)
		{
			S_down = true;
		}
		else if(e->key() == Qt::Key::Key_A)
		{
			A_down = true;
		}
		if(e->key() == Qt::Key::Key_D)
		{
			D_down = true;
		}
		else if(e->key() == Qt::Key::Key_C)
		{
			C_down = true;
		}
		else if(e->key() == Qt::Key::Key_Left)
		{
			left_down = true;
		}
		else if(e->key() == Qt::Key::Key_Right)
		{
			right_down = true;
		}
	}

	emit keyPressed(e);
}


void GlWidget::keyReleaseEvent(QKeyEvent* e)
{
	if(this->player_physics)
	{
		SHIFT_down = (e->modifiers() & Qt::ShiftModifier);

		if(e->key() == Qt::Key::Key_Space)
		{
			space_down = false;
		}
		else if(e->key() == Qt::Key::Key_W)
		{
			W_down = false;
		}
		else if(e->key() == Qt::Key::Key_S)
		{
			S_down = false;
		}
		else if(e->key() == Qt::Key::Key_A)
		{
			A_down = false;
		}
		else if(e->key() == Qt::Key::Key_D)
		{
			D_down = false;
		}
		else if(e->key() == Qt::Key::Key_C)
		{
			C_down = false;
		}
		else if(e->key() == Qt::Key::Key_Left)
		{
			left_down = false;
		}
		else if(e->key() == Qt::Key::Key_Right)
		{
			right_down = false;
		}
	}

	emit keyReleased(e);
}


void GlWidget::playerPhyicsThink(float dt)
{
	// On Windows we will use GetAsyncKeyState() to test if a key is down.
	// On Mac OS / Linux we will use our W_down etc.. state.
	// This isn't as good because if we miss the keyReleaseEvent due to not having focus when the key is released, the key will act as if it's stuck down.
	// TODO: Find an equivalent solution to GetAsyncKeyState on Mac/Linux.

	bool cam_changed = false;

	// Handle gamepad input
	double gamepad_strafe_leftright = 0;
	double gamepad_strafe_forwardsback = 0;
	double gamepad_turn_leftright = 0;
	double gamepad_turn_updown = 0;
	if(gamepad)
	{
		gamepad_strafe_leftright = gamepad->axisLeftX();
		gamepad_strafe_forwardsback = gamepad->axisLeftY();

		gamepad_turn_leftright = gamepad->axisRightX();
		gamepad_turn_updown = gamepad->axisRightY();

		//if(gamepad->axisLeftY() != 0)
		//{ this->cam_controller->update(/*pos delta=*/Vec3d(0.0), Vec2d(0, dt *  gamepad->axisLeftY())); cam_changed = true; }

		//if(gamepad->axisRightX() != 0)
		//{ this->cam_controller->update(/*pos delta=*/Vec3d(0.0), Vec2d(0, dt * -gamepad->axisRightX())); cam_changed = true; }

		//if(gamepad->axisRightY() != 0)
		//{ this->cam_controller->update(/*pos delta=*/Vec3d(0.0), Vec2d(0, dt *  gamepad->axisRightY())); cam_changed = true; }
	}

	if(gamepad_strafe_leftright != 0 || gamepad_strafe_forwardsback != 0 || gamepad_turn_leftright != 0 || gamepad_turn_updown != 0)
		cam_changed = true;


#ifdef _WIN32
	if(hasFocus())
	{
		SHIFT_down = GetAsyncKeyState(VK_SHIFT);

		if(GetAsyncKeyState('W') || GetAsyncKeyState(VK_UP))
		{	this->player_physics->processMoveForwards(1.f, SHIFT_down, *this->cam_controller); cam_changed = true; }
		if(GetAsyncKeyState('S') || GetAsyncKeyState(VK_DOWN))
		{	this->player_physics->processMoveForwards(-1.f, SHIFT_down, *this->cam_controller); cam_changed = true; }
		if(GetAsyncKeyState('A'))
		{	this->player_physics->processStrafeRight(-1.f, SHIFT_down, *this->cam_controller); cam_changed = true; }
		if(GetAsyncKeyState('D'))
		{	this->player_physics->processStrafeRight(1.f, SHIFT_down, *this->cam_controller); cam_changed = true; }

		// Move vertically up or down in flymode.
		if(GetAsyncKeyState(' '))
		{	this->player_physics->processMoveUp(1.f, SHIFT_down, *this->cam_controller); cam_changed = true; }
		if(GetAsyncKeyState('C'))
		{	this->player_physics->processMoveUp(-1.f, SHIFT_down, *this->cam_controller); cam_changed = true; }

		// Turn left or right
		const float base_rotate_speed = 200;
		if(GetAsyncKeyState(VK_LEFT))
		{	this->cam_controller->update(/*pos delta=*/Vec3d(0.0), Vec2d(0, dt * -base_rotate_speed * (SHIFT_down ? 3.0 : 1.0))); cam_changed = true; }
		if(GetAsyncKeyState(VK_RIGHT))
		{	this->cam_controller->update(/*pos delta=*/Vec3d(0.0), Vec2d(0, dt *  base_rotate_speed * (SHIFT_down ? 3.0 : 1.0))); cam_changed = true; }

		if(cam_changed)
			emit cameraUpdated();
	}
#else
	if(W_down)
	{	this->player_physics->processMoveForwards(1.f, SHIFT_down, *this->cam_controller); cam_changed = true; }
	if(S_down)
	{	this->player_physics->processMoveForwards(-1.f, SHIFT_down, *this->cam_controller); cam_changed = true; }
	if(A_down)
	{	this->player_physics->processStrafeRight(-1.f, SHIFT_down, *this->cam_controller); cam_changed = true; }
	if(D_down)
	{	this->player_physics->processStrafeRight(1.f, SHIFT_down, *this->cam_controller); cam_changed = true; }

	// Move vertically up or down in flymode.
	if(space_down)
	{	this->player_physics->processMoveUp(1.f, SHIFT_down, *this->cam_controller); cam_changed = true; }
	if(C_down)
	{	this->player_physics->processMoveUp(-1.f, SHIFT_down, *this->cam_controller); cam_changed = true; }

	// Turn left or right
	const float base_rotate_speed = 200;
	if(left_down)
	{	this->cam_controller->update(/*pos delta=*/Vec3d(0.0), Vec2d(0, dt * -base_rotate_speed * (SHIFT_down ? 3.0 : 1.0))); cam_changed = true; }
	if(right_down)
	{	this->cam_controller->update(/*pos delta=*/Vec3d(0.0), Vec2d(0, dt *  base_rotate_speed * (SHIFT_down ? 3.0 : 1.0))); cam_changed = true; }
#endif

	
	if(gamepad)
	{
		const float gamepad_move_speed_factor = 10;
		const float gamepad_rotate_speed = 400;

		// Move vertically up or down in flymode.
		if(gamepad->buttonR2() != 0)
		{	this->player_physics->processMoveUp(gamepad_move_speed_factor * pow(gamepad->buttonR2(), 3.f), SHIFT_down, *this->cam_controller); cam_changed = true; }

		if(gamepad->buttonL2() != 0)
		{	this->player_physics->processMoveUp(gamepad_move_speed_factor * -pow(gamepad->buttonL2(), 3.f), SHIFT_down, *this->cam_controller); cam_changed = true; }

		if(gamepad->axisLeftX() != 0)
		{	this->player_physics->processStrafeRight(gamepad_move_speed_factor * pow(gamepad->axisLeftX(), 3.f), SHIFT_down, *this->cam_controller); cam_changed = true; }

		if(gamepad->axisLeftY() != 0)
		{	this->player_physics->processMoveForwards(gamepad_move_speed_factor * -pow(gamepad->axisLeftY(), 3.f), SHIFT_down, *this->cam_controller); cam_changed = true; }

		if(gamepad->axisRightX() != 0)
		{ this->cam_controller->update(/*pos delta=*/Vec3d(0.0), Vec2d(0, dt * gamepad_rotate_speed * pow(gamepad->axisRightX(), 3.0f))); cam_changed = true; }

		if(gamepad->axisRightY() != 0)
		{ this->cam_controller->update(/*pos delta=*/Vec3d(0.0), Vec2d(dt *  gamepad_rotate_speed * -pow(gamepad->axisRightY(), 3.f), 0)); cam_changed = true; }
	}


	if(cam_changed)
		emit cameraUpdated();
}


void GlWidget::hideCursor()
{
	// Hide cursor when moving view.
	this->setCursor(QCursor(Qt::BlankCursor));
}


void GlWidget::mousePressEvent(QMouseEvent* e)
{
	//conPrint("mousePressEvent at " + toString(QCursor::pos().x()) + ", " + toString(QCursor::pos().y()));
	mouse_move_origin = QCursor::pos();
	last_mouse_press_pos = QCursor::pos();

	// Hide cursor when moving view.
	//this->setCursor(QCursor(Qt::BlankCursor));

	emit mousePressed(e);
}


void GlWidget::mouseReleaseEvent(QMouseEvent* e)
{
	// Unhide cursor.
	this->unsetCursor();

	//conPrint("mouseReleaseEvent at " + toString(QCursor::pos().x()) + ", " + toString(QCursor::pos().y()));

	//if((QCursor::pos() - last_mouse_press_pos).manhattanLength() < 4)
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
	Qt::MouseButtons mb = e->buttons();
	if(cam_rot_on_mouse_move_enabled && (cam_controller != NULL) && (mb & Qt::LeftButton))// && (e->modifiers() & Qt::AltModifier))
	{
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


		// On Windows/linux, reset the cursor position to where we started, so we never run out of space to move.
		// QCursor::setPos() does not work on mac, and also gives a message about Substrata trying to control the computer, which we want to avoid.
		// So don't use setPos() on Mac.
#if defined(OSX)
		mouse_move_origin = QCursor::pos();
#else
		QCursor::setPos(mouse_move_origin);
#endif

		//conPrint("mouseMoveEvent FPS: " + doubleToStringNSigFigs(1 / timer.elapsed(), 1));
		//timer.reset();
	}

	QGLWidget::mouseMoveEvent(e);

	emit mouseMoved(e);

	//conPrint("mouseMoveEvent time since last event: " + doubleToStringNSigFigs(timer.elapsed(), 5));
	//timer.reset();
}


void GlWidget::wheelEvent(QWheelEvent* e)
{
	emit mouseWheelSignal(e);
}


void GlWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
	emit mouseDoubleClickedSignal(e);
}
