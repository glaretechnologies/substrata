/*=====================================================================
GlWidget.h
----------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#pragma once


#include "../utils/IncludeWindows.h" // This needs to go first for NOMINMAX.
#include "../opengl/OpenGLEngine.h"
#include "../utils/Timer.h"
#include "../utils/Reference.h"
#include "../utils/RefCounted.h"
#include <QtCore/QEvent>
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#include <QtOpenGLWidgets/QOpenGLWidget>
#else
#include <QtOpenGL/QGLWidget>
#endif
//#include <QtGamepad/QGamepad>
#include <QtCore/QTimer>


namespace Indigo { class Mesh; }
class CameraController;
class PlayerPhysics;
class TextureServer;
class EnvEmitter;
class QSettings;


class GlWidget : public
#if 0 // (QT_VERSION_MAJOR >= 6)
	QOpenGLWidget
#else
	QGLWidget
#endif
{
	Q_OBJECT        // must include this if you use Qt signals/slots

public:
	GlWidget(QWidget *parent = 0);
	~GlWidget();

	// Non-empty if error occurred.
	std::string getInitialisationErrorMsg() const { return initialisation_error_msg; }

	void setBaseDir(const std::string& base_dir_path_, PrintOutput* print_output_, QSettings* settings_) { base_dir_path = base_dir_path_; print_output = print_output_; settings = settings_; }

	void setCameraController(CameraController* cam_controller_);
	void setPlayerPhysics(PlayerPhysics* player_physics_);

	void addObject(const Reference<GLObject>& object, bool force_load_textures_immediately = false);
	void removeObject(const Reference<GLObject>& object);
	void addOverlayObject(const Reference<OverlayObject>& object);

	void setEnvMat(OpenGLMaterial& mat);

	void setCurrentTime(float time) { current_time = time; }
	void playerPhyicsThink(float dt); // Process keys held down.

	void setCamRotationOnMouseMoveEnabled(bool enabled) { cam_rot_on_mouse_move_enabled = enabled; }
	void setKeyboardCameraMoveEnabled(bool enabled) { cam_move_on_key_input_enabled = enabled; }
	void hideCursor();

	static float sensorWidth() { return 0.035f; }
	static float lensSensorDist() { return 0.025f; }
protected:

	virtual void initializeGL();
	virtual void resizeGL(int w, int h); // Gets called whenever the widget has been resized (and also when it is shown for the first time because all newly created widgets get a resize event automatically).
	virtual void paintGL(); // Gets called whenever the widget needs to be updated.

	virtual void keyPressEvent(QKeyEvent* e);
	virtual void keyReleaseEvent(QKeyEvent* e);
	virtual void mousePressEvent(QMouseEvent* e);
	virtual void mouseReleaseEvent(QMouseEvent* e);
	virtual void mouseMoveEvent(QMouseEvent* e);
	virtual void wheelEvent(QWheelEvent* e);
	virtual void mouseDoubleClickEvent(QMouseEvent* e);

	void showEvent(QShowEvent* e);
	
signals:;
	void cameraUpdated();
	void playerMoveKeyPressed(); // Player pressed a key such as W/A/S/D to move the player position
	void widgetShowSignal();
	void mousePressed(QMouseEvent* e);
	void mouseClicked(QMouseEvent* e);
	void mouseMoved(QMouseEvent* e);
	void keyPressed(QKeyEvent* e);
	void keyReleased(QKeyEvent* e);
	void mouseWheelSignal(QWheelEvent* e);
	void mouseDoubleClickedSignal(QMouseEvent* e);
	void viewportResizedSignal(int w, int h);
	
private slots:
	//void gamepadInputSlot();
	void initGamepadsSlot();

private:
	
	QPoint mouse_move_origin;
	QPoint last_mouse_press_pos;
	CameraController* cam_controller;
	PlayerPhysics* player_physics;

	std::string base_dir_path;

	int viewport_w, viewport_h;

	bool SHIFT_down, A_down, W_down, S_down, D_down, space_down, C_down, left_down, right_down;
	Timer timer;
	float current_time;
	bool cam_rot_on_mouse_move_enabled;
	bool cam_move_on_key_input_enabled;

	//QGamepad* gamepad;

	std::string initialisation_error_msg;
public:
	float viewport_aspect_ratio;
	TextureServer* texture_server_ptr;
	Reference<OpenGLEngine> opengl_engine;
	float near_draw_dist;
	float max_draw_dist;

	QTimer* gamepad_init_timer;
	PrintOutput* print_output;
	QSettings* settings;

	bool take_map_screenshot;
	float screenshot_ortho_sensor_width_m;
};
