/*=====================================================================
ObInfoUI.h
-----------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUIButton.h>
#include <opengl/ui/GLUICallbackHandler.h>
#include <opengl/ui/GLUITextView.h>


class GUIClient;


/*=====================================================================
ObInfoUI
---------
For object info and hyperlinks etc.
=====================================================================*/
class ObInfoUI : public GLUICallbackHandler
{
public:
	ObInfoUI();
	~ObInfoUI();

	void create(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_);
	void destroy();

	void think();

	void showMessage(const std::string& message, const Vec2f& gl_coords);

	void hideMessage();

	void viewportResized(int w, int h);

	virtual void eventOccurred(GLUICallbackEvent& event) override; // From GLUICallbackHandler

private:
	void updateWidgetPositions();
//public:
	GUIClient* gui_client;

	GLUITextViewRef info_text_view;

	GLUIRef gl_ui;

	Reference<OpenGLEngine> opengl_engine;
};
