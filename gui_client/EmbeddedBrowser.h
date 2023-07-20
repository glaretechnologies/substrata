/*=====================================================================
EmbeddedBrowser.h
-----------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include <utils/Timer.h>
#include <utils/RefCounted.h>
#include <utils/Reference.h>
#include <map>
class MainWindow;
class WorldObject;
class QMouseEvent;
class QEvent;
class QKeyEvent;
class QWheelEvent;
class EmbeddedBrowserCEFBrowser;
class OpenGLEngine;
class OpenGLTexture;
template <class T> class Vec2;


/*=====================================================================
EmbeddedBrowser
---------------
Hides CEF
=====================================================================*/
class EmbeddedBrowser : public RefCounted
{ 
public:
	EmbeddedBrowser();
	~EmbeddedBrowser();

	void create(const std::string& URL, Reference<OpenGLTexture> opengl_tex, MainWindow* main_window, WorldObject* ob, OpenGLEngine* opengl_engine, const std::string& root_page = "");

	void updateRootPage(const std::string& root_page);
	void navigate(const std::string& URL);

	void browserBecameVisible();

	void mousePressed(QMouseEvent* e, const Vec2<float>& uv_coords);
	void mouseReleased(QMouseEvent* e, const Vec2<float>& uv_coords);
	void mouseDoubleClicked(QMouseEvent* e, const Vec2<float>& uv_coords);
	void mouseMoved(QMouseEvent* e, const Vec2<float>& uv_coords);

	void wheelEvent(QWheelEvent* e, const Vec2<float>& uv_coords);

	void keyPressed(QKeyEvent* e);
	void keyReleased(QKeyEvent* e);

private:
	Reference<EmbeddedBrowserCEFBrowser> embedded_cef_browser;
};
