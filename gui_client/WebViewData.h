/*=====================================================================
WebViewData.h
-------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include <utils/RefCounted.h>
#include <utils/Reference.h>
#include <string>
class GUIClient;
class WorldObject;
class EmbeddedBrowser;
class OpenGLEngine;
class MouseEvent;
class MouseWheelEvent;
class KeyEvent;
class TextInputEvent;
template <class T> class Vec2;


class WebViewData : public RefCounted
{ 
public:
	WebViewData();
	~WebViewData();

	void process(GUIClient* gui_client, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt);

	static double maxBrowserDist() { return 20.0; }

	void mousePressed(MouseEvent* e, const Vec2<float>& uv_coords);
	void mouseReleased(MouseEvent* e, const Vec2<float>& uv_coords);
	void mouseDoubleClicked(MouseEvent* e, const Vec2<float>& uv_coords);
	void mouseMoved(MouseEvent* e, const Vec2<float>& uv_coords);

	void wheelEvent(MouseWheelEvent* e, const Vec2<float>& uv_coords);

	void keyPressed(KeyEvent* e);
	void keyReleased(KeyEvent* e);
	void handleTextInputEvent(TextInputEvent& e);

private:
	std::string loaded_target_url;

	Reference<EmbeddedBrowser> browser;

	bool showing_click_to_load_text;
	bool user_clicked_to_load;

	bool previous_is_visible;
};


typedef Reference<WebViewData> WebViewDataRef;
