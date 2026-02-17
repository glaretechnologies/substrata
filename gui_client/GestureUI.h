/*=====================================================================
GestureUI.h
-----------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "../shared/GestureSettings.h"
#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUIButton.h>
#include <opengl/ui/GLUITextButton.h>
#include <opengl/ui/GLUIImage.h>
#include <opengl/ui/GLUICallbackHandler.h>


class GUIClient;
class GestureManagerUI;


/*=====================================================================
GestureUI
---------
Buttons for playing gestures.
Also some misc. other buttons like microphone and photo mode buttons.
=====================================================================*/
class GestureUI : public GLUICallbackHandler
{
public:
	GestureUI();
	~GestureUI();

	void create(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_);
	void destroy();

	void think();

	void setCurrentGestureSettings(const GestureSettings& gesture_settings);

	//bool handleMouseClick(const Vec2f& gl_coords);
	//bool handleMouseMoved(const Vec2f& gl_coords);
	void viewportResized(int w, int h);

	void setVisible(bool visible);

	virtual void eventOccurred(GLUICallbackEvent& event) override; // From GLUICallbackHandler

	void stopAnyGesturePlaying();

	//void turnOffSelfieMode();

	void untoggleMicButton();

	void setCurrentMicLevel(float linear_level, float display_level);

private:
	void rebuildGestureWidgets();
	void updateWidgetPositions();
//public:
	GUIClient* gui_client;

	std::vector<GLUIButtonRef> gesture_buttons;

	GLUIButtonRef edit_gestures_button;
	Reference<GestureManagerUI> gesture_manager;
	GLUIButtonRef close_gesture_manager_button;

	GestureSettings gesture_settings;

	GLUIButtonRef expand_button;
	GLUIButtonRef collapse_button;

	GLUIButtonRef vehicle_button;
	GLUIButtonRef collapse_vehicle_button;


	GLUIButtonRef photo_mode_button;

	GLUIButtonRef microphone_button; // TODO: move out of GestureUI or rename GestureUI.

	GLUIImageRef mic_level_image;

	bool gestures_visible;
	bool vehicle_buttons_visible;

	GLUITextButtonRef summon_bike_button;
	GLUITextButtonRef summon_car_button;
	GLUITextButtonRef summon_boat_button;
	GLUITextButtonRef summon_jetski_button;
	GLUITextButtonRef summon_hovercar_button;

	GLUIRef gl_ui;

	Reference<OpenGLEngine> opengl_engine;

	Timer timer;
	double untoggle_button_time;
};
