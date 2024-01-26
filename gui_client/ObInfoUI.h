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

	void showHyperLink(const std::string& URL, const Vec2f& gl_coords);

	void showMessage(const std::string& message, const Vec2f& gl_coords);

	void hideMessage();

	//bool handleMouseClick(const Vec2f& gl_coords);
	//bool handleMouseMoved(const Vec2f& gl_coords);
	void viewportResized(int w, int h);

	virtual void eventOccurred(GLUICallbackEvent& event) override; // From GLUICallbackHandler

private:
	void updateWidgetPositions();
//public:
	GUIClient* gui_client;

	//GLUIButtonRef selfie_button;

	GLUITextViewRef info_text_view;

	GLUIRef gl_ui;

	Reference<OpenGLEngine> opengl_engine;
};
