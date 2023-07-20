/*=====================================================================
EmbeddedBrowser.h
-----------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include <utils/Timer.h>
#include <utils/RefCounted.h>
#include <utils/Reference.h>
#include <QtGui/QImage>
#include <QtCore/QObject>
#include <map>
class MainWindow;
class WorldObject;
class QMouseEvent;
class QEvent;
class QKeyEvent;
class QWheelEvent;
class EmbeddedBrowserCEFApp;
class EmbeddedBrowserCEFBrowser;
class OpenGLEngine;
class OpenGLTexture;
template <class T> class Vec2;


/*=====================================================================
EmbeddedBrowser
---------------
Hides CEF
=====================================================================*/
class EmbeddedBrowser : /*public QObject, */public RefCounted
{ 
	//Q_OBJECT
public:
	EmbeddedBrowser();
	~EmbeddedBrowser();

	void create(const std::string& URL, Reference<OpenGLTexture> opengl_tex, MainWindow* main_window, WorldObject* ob, OpenGLEngine* opengl_engine);

	//void requestExit();

	void navigate(const std::string& URL);

	void browserBecameVisible();

	//void startShuttingDown();

	//void process(MainWindow* main_window, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt);

	void mousePressed(QMouseEvent* e, const Vec2<float>& uv_coords);
	void mouseReleased(QMouseEvent* e, const Vec2<float>& uv_coords);
	void mouseDoubleClicked(QMouseEvent* e, const Vec2<float>& uv_coords);
	void mouseMoved(QMouseEvent* e, const Vec2<float>& uv_coords);

	void wheelEvent(QWheelEvent* e, const Vec2<float>& uv_coords);

	void keyPressed(QKeyEvent* e);
	void keyReleased(QKeyEvent* e);


private:
//	std::string loaded_target_url;
//
//	QString current_hovered_URL;

	//int cur_load_progress;
	//bool loading_in_progress;

	Reference<EmbeddedBrowserCEFBrowser> embedded_cef_browser;

	//bool showing_click_to_load_text;
	//bool user_clicked_to_load;

	//bool previous_is_visible;
};
