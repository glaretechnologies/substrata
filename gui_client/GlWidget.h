/*=====================================================================
GlWidget.h
----------
Copyright Glare Technologies Limited 2023 -
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
#include <QtCore/QTimer>


namespace Indigo { class Mesh; }
namespace glare { class TaskManager; }
class CameraController;
class PlayerPhysics;
class TextureServer;
class EnvEmitter;
class QSettings;
class QGamepad;


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

	void shutdown();

	// Non-empty if error occurred.
	std::string getInitialisationErrorMsg() const { return initialisation_error_msg; }

	void setBaseDir(const std::string& base_dir_path_, PrintOutput* print_output_, QSettings* settings_) { base_dir_path = base_dir_path_; print_output = print_output_; settings = settings_; }

	void setCameraController(CameraController* cam_controller_);

	void setCamRotationOnMouseDragEnabled(bool enabled) { cam_rot_on_mouse_move_enabled = enabled; }
	void setKeyboardCameraMoveEnabled(bool enabled) { cam_move_on_key_input_enabled = enabled; }
	bool isKeyboardCameraMoveEnabled() { return cam_move_on_key_input_enabled; }
	void hideCursor();
	bool isCursorHidden();

	void setCursorIfNotHidden(Qt::CursorShape new_shape);

	static float defaultSensorWidth() { return 0.035f; }
	static float defaultLensSensorDist() { return 0.025f; }

	void* makeNewSharedGLContext();

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
	virtual void focusOutEvent(QFocusEvent* e);

	void showEvent(QShowEvent* e);
	
signals:;
	void widgetShowSignal();
	void mousePressed(QMouseEvent* e);
	void mouseReleased(QMouseEvent* e);
	void mouseMoved(QMouseEvent* e);
	void mouseWheelSignal(QWheelEvent* e);
	void mouseDoubleClickedSignal(QMouseEvent* e);
	void keyPressed(QKeyEvent* e);
	void keyReleased(QKeyEvent* e);
	void viewportResizedSignal(int w, int h);
	void cutShortcutActivated();
	void copyShortcutActivated();
	void pasteShortcutActivated();
	void focusOutSignal();
	void gamepadButtonXChangedSignal(bool pressed);
	void gamepadButtonAChangedSignal(bool pressed);

private slots:
	void gamepadInputSlot();
	void initGamepadsSlot();
	void buttonXChangedSlot(bool pressed);

private:
	
	QPoint mouse_move_origin;
	QPoint last_mouse_press_pos;
	CameraController* cam_controller;

	std::string base_dir_path;

	int viewport_w, viewport_h;

	//Timer fps_timer;
	bool cam_rot_on_mouse_move_enabled;
	bool cam_move_on_key_input_enabled;

	std::string initialisation_error_msg;
public:
	QGamepad* gamepad;
	Reference<OpenGLEngine> opengl_engine;
	float near_draw_dist;
	float max_draw_dist;

	QTimer* gamepad_init_timer;
	PrintOutput* print_output;
	QSettings* settings;

	bool take_map_screenshot;
	float screenshot_ortho_sensor_width_m;

	bool allow_bindless_textures;
	bool allow_multi_draw_indirect;

	glare::TaskManager* main_task_manager;
	glare::TaskManager* high_priority_task_manager;
	Reference<glare::Allocator> main_mem_allocator;
};
