/*=====================================================================
MiscInfoUI.h
------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUIButton.h>
#include <opengl/ui/GLUICallbackHandler.h>
#include <opengl/ui/GLUITextView.h>


class GUIClient;


/*=====================================================================
MiscInfoUI
----------
For showing admin messages from server etc.
=====================================================================*/
class MiscInfoUI : public GLUICallbackHandler
{
public:
	MiscInfoUI();
	~MiscInfoUI();

	void create(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_);
	void destroy();

	void think();

	void showServerAdminMessage(const std::string& msg);

	void showVehicleSpeed(float speed_km_per_h);
	void showVehicleInfo(const std::string& info);
	void hideVehicleSpeed();

	void viewportResized(int w, int h);

	virtual void eventOccurred(GLUICallbackEvent& event) override; // From GLUICallbackHandler

private:
	void updateWidgetPositions();

	GUIClient* gui_client;

	GLUITextViewRef admin_msg_text_view;

	GLUITextViewRef speed_text_view;

	GLUIRef gl_ui;

	Reference<OpenGLEngine> opengl_engine;
};
