/*=====================================================================
PhotoModeUI.h
-------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#pragma once


#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUIButton.h>
#include <opengl/ui/GLUITextButton.h>
#include <opengl/ui/GLUICallbackHandler.h>
#include <opengl/ui/GLUITextView.h>


class GUIClient;


/*=====================================================================
PhotoModeUI
-----------

=====================================================================*/
class PhotoModeUI : public GLUICallbackHandler
{
public:
	PhotoModeUI();
	~PhotoModeUI();

	void create(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_);
	void destroy();

	void setVisible(bool visible);

	void standardCameraModeSelected();

	void think();

	void viewportResized(int w, int h);

	virtual void eventOccurred(GLUICallbackEvent& event) override; // From GLUICallbackHandler

private:
	void untoggleAllCamModeButtons();
	void updateWidgetPositions();

	GUIClient* gui_client;

	GLUITextButtonRef standard_cam_button;
	GLUITextButtonRef fixed_angle_cam_button;
	GLUITextButtonRef free_cam_button;
	GLUITextButtonRef tracking_cam_button;

	GLUIRef gl_ui;

	Reference<OpenGLEngine> opengl_engine;
};
