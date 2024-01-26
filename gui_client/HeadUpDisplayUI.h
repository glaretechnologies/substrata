/*=====================================================================
HeadUpDisplayUI.h
-----------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUIButton.h>
#include <opengl/ui/GLUICallbackHandler.h>
#include <opengl/ui/GLUITextView.h>
#include <opengl/ui/GLUIImage.h>


class GUIClient;
class Avatar;


/*=====================================================================
HeadUpDisplayUI
---------------
Draws stuff like markers for other avatars
=====================================================================*/
class HeadUpDisplayUI : public GLUICallbackHandler
{
public:
	HeadUpDisplayUI();
	~HeadUpDisplayUI();

	void create(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_);
	void destroy();

	void think();

	void viewportResized(int w, int h);

	void updateMarkerForAvatar(Avatar* avatar, const Vec3d& avatar_pos);
	void removeMarkerForAvatar(Avatar* avatar);

	virtual void eventOccurred(GLUICallbackEvent& event);
private:
	void updateWidgetPositions();

	GUIClient* gui_client;
	GLUIRef gl_ui;
	Reference<OpenGLEngine> opengl_engine;
};
