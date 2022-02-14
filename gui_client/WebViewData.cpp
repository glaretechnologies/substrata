/*=====================================================================
WebViewData.cpp
---------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "WebViewData.h"


#include "MainWindow.h"
#include "../shared/WorldObject.h"
#include "../qt/QtUtils.h"
#include "Escaping.h"
#include "FileInStream.h"


//#define WEBENGINEVIEW_SUPPORT 1

#if WEBENGINEVIEW_SUPPORT
//#include <QtWebEngineQuick/qtwebenginequickglobal.h>
//#include <QtWebEngineWidgets/QWebEngineView>
//#include <QtWebEngineCore/QWebEngineSettings>
#endif
#include <QtGui/QPainter>


WebViewData::WebViewData()
:	webview(NULL),
	cur_load_progress(0),
	loading_in_progress(false)
{

}


WebViewData::~WebViewData()
{
#if WEBENGINEVIEW_SUPPORT
	delete webview;
#endif
}


void WebViewData::process(MainWindow* main_window, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt)
{
#if WEBENGINEVIEW_SUPPORT

	if(ob->opengl_engine_ob.nonNull())
	{
		if(!webview)
		{
			this->loaded_target_url = ob->target_url;


			webview = new QWebEngineView(/*ui->diagnosticsDockWidgetContents*/);

			connect(webview, SIGNAL(loadStarted()), this, SLOT(loadStartedSlot()));
			connect(webview, SIGNAL(loadProgress(int)), this, SLOT(loadProgress(int)));
			connect(webview, SIGNAL(loadFinished(bool)), this, SLOT(loadFinished(bool)));
			
			webview->setAttribute(Qt::WA_DontShowOnScreen);
			webview->setFixedSize(1920, 1080);
			webview->load(QUrl(QtUtils::toQString(ob->target_url)));

			webview->page()->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false); // Set settings so that auto-play works.  (see https://stackoverflow.com/a/58120230)
			webview->show();

			connect(webview->page(), SIGNAL(linkHovered(const QString&)), this, SLOT(linkHovered(const QString&)));
			connect(webview->page(), SIGNAL(linkHovered(const QString&)), this, SIGNAL(linkHoveredSignal(const QString&)));
		}


		// If target url has changed, tell webview to load it
		if(ob->target_url != this->loaded_target_url)
		{
			conPrint("Webview loading URL '" + ob->target_url + "'...");

			webview->load(QUrl(QtUtils::toQString(ob->target_url)));

			this->loaded_target_url = ob->target_url;
		}


		if(time_since_last_webview_display.elapsed() > 0.0333) // Max 30 fps
		{
			if(webview_qimage.size() != webview->size())
			{
				webview_qimage = QImage(webview->size(), QImage::Format_RGBA8888); // The 32 bit Qt formats seem faster than the 24 bit formats.
			}

			const int W = webview_qimage.width();
			const int H = webview_qimage.height();

			QPainter painter(&webview_qimage);

			// Draw the web-view to the QImage
			webview->render(&painter);

			// Draw hovered-over link URL at bottom left
			if(!this->current_hovered_URL.isEmpty())
			{
				QFont system_font = QApplication::font();
				system_font.setPointSize(16);

				QFontMetrics metrics(system_font);
				QRect text_rect = metrics.boundingRect(this->current_hovered_URL);

				//printVar(text_rect.x());
				//printVar(text_rect.y());
				//printVar(text_rect.top());
				//printVar(text_rect.bottom());
				//printVar(text_rect.left());
				//printVar(text_rect.right());
				/*
				For example:
				text_rect.x(): 0
				text_rect.y(): -23
				text_rect.top(): -23
				text_rect.bottom(): 4
				text_rect.left(): 0
				text_rect.right(): 464
				*/
				const int x_padding = 12; // in pixels
				const int y_padding = 12; // in pixels
				const int link_W = text_rect.width()  + x_padding*2;
				const int link_H = -text_rect.top() + y_padding*2;

				painter.setPen(QPen(QColor(200, 200, 200)));
				painter.fillRect(0, H - link_H, link_W, link_H, QBrush(QColor(200, 200, 200), Qt::SolidPattern));

				painter.setPen(QPen(QColor(0, 0, 0)));
				painter.setFont(system_font);
				painter.drawText(QPoint(x_padding, /*font baseline y=*/H - y_padding), this->current_hovered_URL);
			}

			// Draw loading indicator
			if(loading_in_progress)
			{
				const int loading_bar_w = (int)((float)W * cur_load_progress / 100.f);
				const int loading_bar_h = 8;
				painter.fillRect(0, H - loading_bar_h, loading_bar_w, loading_bar_h, QBrush(QColor(100, 100, 255), Qt::SolidPattern));
			}

			painter.end();

			if(ob->opengl_engine_ob->materials[0].albedo_texture.isNull())
			{
				ob->opengl_engine_ob->materials[0].albedo_texture = new OpenGLTexture(W, H, opengl_engine, OpenGLTexture::Format_SRGBA_Uint8, OpenGLTexture::Filtering_Bilinear);
			}

			// Update texture
			ob->opengl_engine_ob->materials[0].albedo_texture->load(W, H, /*row stride B=*/webview_qimage.bytesPerLine(), ArrayRef<uint8>(webview_qimage.constBits(), webview_qimage.sizeInBytes()));


			//webview_qimage.save("webview_qimage.png", "png");

			time_since_last_webview_display.reset();
		}


		//printVar(webview->hasFocus());
		//printVar(webview->hasEditFocus());

	} // end if(ob->opengl_engine_ob.nonNull())

#endif // WEBENGINEVIEW_SUPPORT
}


static double getMouseDoubleClickTime()
{
#if defined(_WIN32)
	return GetDoubleClickTime() * 1.0e-3;
#else
	return 0.5;
#endif
}


void WebViewData::mouseReleased(QMouseEvent* e, const Vec2f& uv_coords)
{
	conPrint("mouseReleased()");

	if(time_since_last_mouse_click.elapsed() < getMouseDoubleClickTime())
	{
		// This is a double-click
		conPrint("double-click detected");

		emit mouseDoubleClickedSignal(e);
	}
	else
	{
		postMouseEventToWebView(e, /*QEvent::MouseButtonRelease, */uv_coords);
	}

	time_since_last_mouse_click.reset();
}


void WebViewData::mousePressed(QMouseEvent* e, const Vec2f& uv_coords)
{
	conPrint("mousePressed()");

	postMouseEventToWebView(e, /*QEvent::MouseButtonPress, */uv_coords);
}


void WebViewData::mouseDoubleClicked(QMouseEvent* e, const Vec2f& uv_coords)
{
	conPrint("mouseDoubleClicked()");

	postMouseEventToWebView(e, /*QEvent::MouseButtonDblClick, */uv_coords);
}


void WebViewData::mouseMoved(QMouseEvent* e, const Vec2f& uv_coords)
{
	conPrint("mouseMoved(), uv_coords: " + uv_coords.toString());

	postMouseEventToWebView(e, /*QEvent::MouseMove, */uv_coords);
}


void WebViewData::wheelEvent(QWheelEvent* e, const Vec2f& uv_coords)
{
	conPrint("wheelEvent(), uv_coords: " + uv_coords.toString());
#if WEBENGINEVIEW_SUPPORT
	if(webview)
	{
		QWidget* events_receiver_widget = NULL;
		foreach(QObject* obj, webview->children())
		{
			QWidget* wgt = qobject_cast<QWidget*>(obj);
			if (wgt)
			{
				events_receiver_widget = wgt;
				break;
			}
		}

		if(events_receiver_widget)
		{
			const QPointF pos(uv_coords.x * webview->width(), (1 - uv_coords.y) * webview->height());

			// "The event must be allocated on the heap since the post event queue will take ownership of the event and delete it once it has been posted" ( https://doc.qt.io/qt-5/qcoreapplication.html#postEvent )
			QWheelEvent* new_event = new QWheelEvent(
				pos/*e->position()*/,
				pos/*e->globalPosition()*/,
				e->pixelDelta(),
				e->angleDelta(),
				e->buttons(),
				e->modifiers(),
				e->phase(),
				e->inverted(),
				e->source()
			);
			
			QCoreApplication::postEvent(events_receiver_widget, new_event);
		}
	}
#endif
}


void WebViewData::postMouseEventToWebView(QMouseEvent* e, /*QEvent::Type use_event_type, */const Vec2f& uv_coords)
{
	//conPrint("uv_coords: " + uv_coords.toString());
#if WEBENGINEVIEW_SUPPORT
	if(webview)
	{
		QWidget* events_receiver_widget = NULL;
		foreach(QObject* obj, webview->children())
		{
			QWidget* wgt = qobject_cast<QWidget*>(obj);
			if (wgt)
			{
				events_receiver_widget = wgt;
				break;
			}
		}

		if(events_receiver_widget)
		{
			QMouseEvent* new_event = new QMouseEvent(
				e->type(), // use_event_type,
				QPointF(uv_coords.x * webview->width(), (1 - uv_coords.y) * webview->height()),
				e->button(),
				e->buttons(),
				e->modifiers());
			QCoreApplication::postEvent(events_receiver_widget, new_event);
		}

		//webview->setFocus(Qt::MouseFocusReason);
	}
#endif
}


void WebViewData::postKeyEventToWebView(QKeyEvent* e)
{
#if WEBENGINEVIEW_SUPPORT
	if(webview)
	{
		QWidget* events_receiver_widget = NULL;
		foreach(QObject* obj, webview->children())
		{
			QWidget* wgt = qobject_cast<QWidget*>(obj);
			if (wgt)
			{
				events_receiver_widget = wgt;
				break;
			}
		}

		if(events_receiver_widget)
		{
			QKeyEvent* new_event = new QKeyEvent(
				e->type(), // use_event_type,
				e->key(),
				e->modifiers(),
				e->text()
			);
			QCoreApplication::postEvent(events_receiver_widget, new_event);
		}
	}
#endif
}


void WebViewData::keyPressed(QKeyEvent* e)
{
	postKeyEventToWebView(e);
}


void WebViewData::keyReleased(QKeyEvent* e)
{
	postKeyEventToWebView(e);
}


void WebViewData::loadStartedSlot()
{
	//conPrint("loadStartedSlot()");
	loading_in_progress = true;
}


void WebViewData::loadProgress(int progress)
{
	//conPrint("loadProgress(): " + toString(progress));
	cur_load_progress = progress;
}


void WebViewData::loadFinished(bool ok)
{
	//conPrint("loadFinished(): " + boolToString(ok));
	loading_in_progress = false;
}


void WebViewData::linkHovered(const QString &url)
{
	//conPrint("linkHovered(): " + QtUtils::toStdString(url));
	this->current_hovered_URL = url;
}
