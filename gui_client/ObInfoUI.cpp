/*=====================================================================
ObInfoUI.cpp
------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "ObInfoUI.h"


#include "GUIClient.h"
#include <utils/UTF8Utils.h>


ObInfoUI::ObInfoUI()
:	gui_client(NULL)
{}


ObInfoUI::~ObInfoUI()
{}


void ObInfoUI::create(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_)
{
	opengl_engine = opengl_engine_;
	gui_client = gui_client_;
	gl_ui = gl_ui_;

	GLUITextView::CreateArgs create_args;
	info_text_view = new GLUITextView(*gl_ui, opengl_engine, "", Vec2f(0.f), create_args);
	info_text_view->setVisible(false);
	//info_text_view->handler = this;
	gl_ui->addWidget(info_text_view);

	updateWidgetPositions();
}


void ObInfoUI::destroy()
{
	if(info_text_view.nonNull())
	{
		gl_ui->removeWidget(info_text_view);
		info_text_view = NULL;
	}

	gl_ui = NULL;
	opengl_engine = NULL;
}


void ObInfoUI::think()
{
	if(gl_ui.nonNull())
	{
	}
}


void ObInfoUI::showHyperLink(const std::string& URL, const Vec2f& gl_coords)
{
	const int MAX_LEN = 60;
	std::string trimmed_URL = URL;
	if(trimmed_URL.size() > MAX_LEN)
	{
		trimmed_URL = trimmed_URL.substr(0, MAX_LEN) + "...";
	}

	showMessage("Press [E] to open " + trimmed_URL, gl_coords);
}


void ObInfoUI::showMessage(const std::string& message, const Vec2f& gl_coords)
{
	const Vec2f coords = gl_ui->UICoordsForOpenGLCoords(gl_coords);

	info_text_view->setText(*gl_ui, UTF8Utils::sanitiseUTF8String(message));

	info_text_view->setPos(*gl_ui, /*botleft=*/coords + Vec2f(-gl_ui->getUIWidthForDevIndepPixelWidth(100), -gl_ui->getUIWidthForDevIndepPixelWidth(50)));

	info_text_view->setVisible(true);
}


void ObInfoUI::hideMessage()
{
	info_text_view->setPos(*gl_ui, Vec2f(-100.f, -100.f));
	info_text_view->setVisible(false);
}


void ObInfoUI::updateWidgetPositions()
{
}


void ObInfoUI::viewportResized(int w, int h)
{
	if(gl_ui.nonNull())
	{
		updateWidgetPositions();
	}
}


void ObInfoUI::eventOccurred(GLUICallbackEvent& event)
{
	if(gui_client)
	{
		if(event.widget == info_text_view.ptr())
		{
			//GLUIButton* button = static_cast<GLUIButton*>(event.widget);

			event.accepted = true;

			conPrint("Clicked on text view!");
		}
	}
}
