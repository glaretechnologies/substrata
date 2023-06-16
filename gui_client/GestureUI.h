/*=====================================================================
GestureUI.h
-----------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUIButton.h>
#include <opengl/ui/GLUICallbackHandler.h>


class MainWindow;


/*=====================================================================
GestureUI
---------

=====================================================================*/
class GestureUI : public GLUICallbackHandler
{
public:
	GestureUI();
	~GestureUI();

	void create(Reference<OpenGLEngine>& opengl_engine_, MainWindow* main_window_, GLUIRef gl_ui_);
	void destroy();

	void think();

	//bool handleMouseClick(const Vec2f& gl_coords);
	//bool handleMouseMoved(const Vec2f& gl_coords);
	void viewportResized(int w, int h);

	virtual void eventOccurred(GLUICallbackEvent& event) override; // From GLUICallbackHandler

	// Get the current gesture being performed, according to the UI state (i.e. which button is toggled).
	// Returns true if a gesture is being performed, false otherwise.
	bool getCurrentGesturePlaying(std::string& gesture_name_out, bool& animate_head_out, bool& loop_out);

	void stopAnyGesturePlaying();

	void turnOffSelfieMode();

	static bool animateHead(const std::string& gesture);
	static bool loopAnim(const std::string& gesture);

private:
	void updateWidgetPositions();
//public:
	MainWindow* main_window;

	std::vector<GLUIButtonRef> gesture_buttons;

	GLUIButtonRef left_tab_button;
	GLUIButtonRef right_tab_button;

	GLUIButtonRef selfie_button;

	GLUIButtonRef microphone_button; // TODO: move out of GestureUI or rename GestureUI.

	bool gestures_visible;

	GLUIRef gl_ui;

	Reference<OpenGLEngine> opengl_engine;

	Timer timer;
	double untoggle_button_time;
};
