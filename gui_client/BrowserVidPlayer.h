/*=====================================================================
BrowserVidPlayer.h
------------------
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
class QString;
template <class T> class Vec2;


class BrowserVidPlayer : public RefCounted
{ 
public:
	BrowserVidPlayer();
	~BrowserVidPlayer();

	void process(MainWindow* main_window, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt);

	void videoURLMayHaveChanged(MainWindow* main_window, OpenGLEngine* opengl_engine, WorldObject* ob);

	static double maxBrowserDist() { return 20.0; }

	void mousePressed(QMouseEvent* e, const Vec2<float>& uv_coords);
	void mouseReleased(QMouseEvent* e, const Vec2<float>& uv_coords);
	void mouseDoubleClicked(QMouseEvent* e, const Vec2<float>& uv_coords);
	void mouseMoved(QMouseEvent* e, const Vec2<float>& uv_coords);

	void wheelEvent(QWheelEvent* e, const Vec2<float>& uv_coords);

	void keyPressed(QKeyEvent* e);
	void keyReleased(QKeyEvent* e);

private:
	void createNewBrowserPlayer(MainWindow* main_window, OpenGLEngine* opengl_engine, WorldObject* ob);

	enum State
	{
		State_Unloaded,
		State_ErrorOccurred,
		State_BrowserCreated
	};
	State state;

	std::string loaded_video_url;

	Reference<EmbeddedBrowser> browser;

	bool previous_is_visible;
};


typedef Reference<BrowserVidPlayer> BrowserVidPlayerRef;
