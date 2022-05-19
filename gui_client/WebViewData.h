/*=====================================================================
WebViewData.h
-------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include <opengl/OpenGLEngine.h>
#include <opengl/WGL.h>
#include <utils/Timer.h>
#include <QtGui/QImage>
#include <QtCore/QObject>
#include <map>
class MainWindow;
class WorldObject;
class QMouseEvent;
class QEvent;
class QKeyEvent;
class QWheelEvent;
class WebViewDataCEFApp;
class WebViewCEFBrowser;


class WebViewData : public QObject, public RefCounted
{ 
	Q_OBJECT
public:
	WebViewData();
	~WebViewData();

	void process(MainWindow* main_window, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt);

	static const double maxBrowserDist() { return 20.0; }

	void mousePressed(QMouseEvent* e, const Vec2f& uv_coords);
	void mouseReleased(QMouseEvent* e, const Vec2f& uv_coords);
	void mouseDoubleClicked(QMouseEvent* e, const Vec2f& uv_coords);
	void mouseMoved(QMouseEvent* e, const Vec2f& uv_coords);

	void wheelEvent(QWheelEvent* e, const Vec2f& uv_coords);

	void keyPressed(QKeyEvent* e);
	void keyReleased(QKeyEvent* e);

signals:;
	void linkHoveredSignal(const QString &url);

	void mouseDoubleClickedSignal(QMouseEvent* e);

private slots:
	void loadStartedSlot();
	void loadProgress(int progress);
	void loadFinished(bool ok);

	void linkHovered(const QString &url);


private:
	std::string loaded_target_url;

	QString current_hovered_URL;

	int cur_load_progress;
	bool loading_in_progress;

	Reference<WebViewCEFBrowser> browser;

	bool showing_click_to_load_text;
	bool user_clicked_to_load;

	bool previous_is_visible;
};


typedef Reference<WebViewData> WebViewDataRef;
