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
class GUIClient;
class WorldObject;
class MouseEvent;
class MouseWheelEvent;
class KeyEvent;
class TextInputEvent;
class EmbeddedBrowserCEFBrowser;
class OpenGLEngine;
class OpenGLTexture;
template <class T> class Vec2;


/*=====================================================================
EmbeddedBrowser
---------------
Wraps Chromium Embedded Framework (CEF).
Renders a browser to a specific texture on a WorldObject material.
=====================================================================*/
class EmbeddedBrowser : public RefCounted
{ 
public:
	EmbeddedBrowser();
	~EmbeddedBrowser();

	void create(const std::string& URL, int viewport_width, int viewport_height, GUIClient* gui_client, WorldObject* ob, size_t mat_index, bool apply_to_emission_texture, 
		OpenGLEngine* opengl_engine, const std::string& root_page = "");

	void updateRootPage(const std::string& root_page);
	void navigate(const std::string& URL);

	void browserBecameVisible();

	void mousePressed(MouseEvent* e, const Vec2<float>& uv_coords);
	void mouseReleased(MouseEvent* e, const Vec2<float>& uv_coords);
	void mouseDoubleClicked(MouseEvent* e, const Vec2<float>& uv_coords);
	void mouseMoved(MouseEvent* e, const Vec2<float>& uv_coords);

	void wheelEvent(MouseWheelEvent* e, const Vec2<float>& uv_coords);

	void keyPressed(KeyEvent* e);
	void keyReleased(KeyEvent* e);
	void handleTextInputEvent(TextInputEvent& e);

private:
	Reference<EmbeddedBrowserCEFBrowser> embedded_cef_browser;
};
