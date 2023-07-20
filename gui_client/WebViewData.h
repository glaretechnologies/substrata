/*=====================================================================
WebViewData.h
-------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include <utils/RefCounted.h>
#include <utils/Reference.h>
#include <string>
class MainWindow;
class WorldObject;
class QMouseEvent;
class QEvent;
class QKeyEvent;
class QWheelEvent;
class EmbeddedBrowser;
class OpenGLEngine;
template <class T> class Vec2;


class WebViewData : public RefCounted
{ 
public:
	WebViewData();
	~WebViewData();

	void process(MainWindow* main_window, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt);

	static double maxBrowserDist() { return 20.0; }

	void mousePressed(QMouseEvent* e, const Vec2<float>& uv_coords);
	void mouseReleased(QMouseEvent* e, const Vec2<float>& uv_coords);
	void mouseDoubleClicked(QMouseEvent* e, const Vec2<float>& uv_coords);
	void mouseMoved(QMouseEvent* e, const Vec2<float>& uv_coords);

	void wheelEvent(QWheelEvent* e, const Vec2<float>& uv_coords);

	void keyPressed(QKeyEvent* e);
	void keyReleased(QKeyEvent* e);

private:
	std::string loaded_target_url;

	Reference<EmbeddedBrowser> browser;

	bool showing_click_to_load_text;
	bool user_clicked_to_load;

	bool previous_is_visible;
};


typedef Reference<WebViewData> WebViewDataRef;
