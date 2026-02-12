/*=====================================================================
GestureManagerUI.h
------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "../shared/GestureSettings.h"
#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUIButton.h>
#include <opengl/ui/GLUIImage.h>
#include <opengl/ui/GLUITextView.h>
#include <opengl/ui/GLUICallbackHandler.h>
#include <opengl/ui/GLUIGridContainer.h>
#include <opengl/ui/GLUICheckBox.h>


class GUIClient;


/*=====================================================================
GestureManagerUI
----------------
For adding new gestures and editing or removing existing gestures.
=====================================================================*/
class GestureManagerUI : public GLUICallbackHandler, public ThreadSafeRefCounted
{
public:
	GestureManagerUI(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_, const GestureSettings& gesture_settings);
	~GestureManagerUI();

	void think();

	void viewportResized(int w, int h);

	virtual void eventOccurred(GLUICallbackEvent& event) override; // From GLUICallbackHandler


	void updateWidgetPositions();
private:
	void rebuildGridForGestureSettings();


	GUIClient* gui_client;
	GLUIRef gl_ui;
	Reference<OpenGLEngine> opengl_engine;


	struct PerGestureUI
	{
		GLUIImageRef gesture_image;
		GLUITextViewRef name_text;
		GLUICheckBoxRef animate_head_checkbox;
		GLUICheckBoxRef loop_checkbox;
		GLUIButtonRef remove_gesture_button;
	};
	std::vector<PerGestureUI> gestures;
	
public:
	GLUIGridContainerRef grid_container;
private:
	GLUIButtonRef add_gesture_button;

	GestureSettings gesture_settings;

	bool need_rebuild_grid;
};
