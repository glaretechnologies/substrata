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
#include <QtOpenGL/QGLWidget>


namespace Indigo { class Mesh; }
class CameraController;
class PlayerPhysics;
class TextureServer;
class EnvEmitter;


class GlWidget : public QGLWidget
{
	Q_OBJECT        // must include this if you use Qt signals/slots

public:
	GlWidget(QWidget *parent = 0);
	~GlWidget();

	void setBaseDir(const std::string& base_dir_path_) { base_dir_path = base_dir_path_; }

	void setCameraController(CameraController* cam_controller_);
	void setPlayerPhysics(PlayerPhysics* player_physics_);

	void addObject(const Reference<GLObject>& object, bool force_load_textures_immediately = false);
	void removeObject(const Reference<GLObject>& object);
	void addOverlayObject(const Reference<OverlayObject>& object);

	void setEnvMat(OpenGLMaterial& mat);

	void setCurrentTime(float time) { current_time = time; }
	void playerPhyicsThink(float dt); // Process keys held down.

	void setCamRotationOnMouseMoveEnabled(float enabled) { cam_rot_on_mouse_move_enabled = enabled; }
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
	void widgetShowSignal();
	void mousePressed(QMouseEvent* e);
	void mouseClicked(QMouseEvent* e);
	void mouseMoved(QMouseEvent* e);
	void keyPressed(QKeyEvent* e);
	void keyReleased(QKeyEvent* e);
	void mouseWheelSignal(QWheelEvent* e);
	void mouseDoubleClickedSignal(QMouseEvent* e);

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
public:
	float viewport_aspect_ratio;
	TextureServer* texture_server_ptr;
	Reference<OpenGLEngine> opengl_engine;
};
