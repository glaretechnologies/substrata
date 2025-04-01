/*=====================================================================
BrowserVidPlayer.h
------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include <utils/RefCounted.h>
#include <utils/Reference.h>
#include <string>
class GUIClient;
class WorldObject;
class MouseEvent;
class MouseWheelEvent;
class KeyEvent;
class EmbeddedBrowser;
class OpenGLEngine;
class QString;
template <class T> class Vec2;


class BrowserVidPlayer : public RefCounted
{ 
public:
	BrowserVidPlayer();
	~BrowserVidPlayer();

	void process(GUIClient* gui_client, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt);

	void videoURLMayHaveChanged(GUIClient* gui_client, OpenGLEngine* opengl_engine, WorldObject* ob);

	static double maxBrowserDist() { return 20.0; }

	void mousePressed(MouseEvent* e, const Vec2<float>& uv_coords);
	void mouseReleased(MouseEvent* e, const Vec2<float>& uv_coords);
	void mouseDoubleClicked(MouseEvent* e, const Vec2<float>& uv_coords);
	void mouseMoved(MouseEvent* e, const Vec2<float>& uv_coords);

	void wheelEvent(MouseWheelEvent* e, const Vec2<float>& uv_coords);

	void keyPressed(KeyEvent* e);
	void keyReleased(KeyEvent* e);

private:
	void createNewBrowserPlayer(GUIClient* gui_client, OpenGLEngine* opengl_engine, WorldObject* ob);

	GUIClient* m_gui_client;

	enum State
	{
		State_Unloaded,
		State_ErrorOccurred,
		State_BrowserCreated
	};
	State state;

	std::string loaded_video_url;

	Reference<EmbeddedBrowser> browser;
	int html_view_handle;

	bool using_iframe;

	bool previous_is_visible;
};


typedef Reference<BrowserVidPlayer> BrowserVidPlayerRef;
