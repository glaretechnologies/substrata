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


class MainWindow;
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

	void create(Reference<OpenGLEngine>& opengl_engine_, MainWindow* main_window_, GLUIRef gl_ui_);
	void destroy();

	void updateMarkerForAvatar(const Avatar* avatar, const Vec3d& avatar_pos);
	void removeMarkerForAvatar(const Avatar* avatar);

	virtual void eventOccurred(GLUICallbackEvent& event);
private:
	MainWindow* main_window;
	GLUIRef gl_ui;
	Reference<OpenGLEngine> opengl_engine;

	std::map<const Avatar*, GLUIImageRef> avatar_markers;
};
