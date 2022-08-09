/*=====================================================================
ObInfoUI.cpp
------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "ObInfoUI.h"


#include "MainWindow.h"
#include <QtCore/QSettings>


ObInfoUI::ObInfoUI()
:	main_window(NULL)
{}


ObInfoUI::~ObInfoUI()
{}


void ObInfoUI::create(Reference<OpenGLEngine>& opengl_engine_, MainWindow* main_window_, GLUIRef gl_ui_)
{
	opengl_engine = opengl_engine_;
	main_window = main_window_;
	gl_ui = gl_ui_;

	info_text_view = new GLUITextView();
	info_text_view->create(*gl_ui, opengl_engine, "YO", Vec2f(-100.f, 0.1f), Vec2f(0.1f, 0.1f), /*tooltip=*/""); // Create off-screen
	info_text_view->handler = this;
	gl_ui->addWidget(info_text_view);

	updateWidgetPositions();
}


void ObInfoUI::destroy()
{
	if(info_text_view.nonNull())
	{
		gl_ui->removeWidget(info_text_view);
		info_text_view->destroy();
		info_text_view = NULL;
	}
}


void ObInfoUI::think()
{
	if(gl_ui.nonNull())
	{
	}
}


void ObInfoUI::showHyperLink(const std::string& URL, const Vec2f& gl_coords)
{
	// conPrint("ObInfoUI::showHyperLink: URL: " + URL + ", gl_coords: " + gl_coords.toString());

	currently_showing_url = URL;

	// Convert from gl_coords to UI x/y coords
	const float y_scale = 1 / opengl_engine->getViewPortAspectRatio();
	const Vec2f coords(gl_coords.x, gl_coords.y * y_scale);

	const int MAX_LEN = 60;
	std::string trimmed_URL = URL;
	if(trimmed_URL.size() > MAX_LEN)
	{
		trimmed_URL = trimmed_URL.substr(0, MAX_LEN) + "...";
	}

	info_text_view->setText(*gl_ui, "Press [E] to open " + trimmed_URL);

	const Vec2f tex_dims = info_text_view->getTextureDimensions();


	const int max_w_pixels = 900;
	const float max_w = (float)max_w_pixels / opengl_engine->getViewPortWidth();

	const float use_w = myMin(max_w, 1.8f);
	const float use_h = use_w * (tex_dims.y / tex_dims.x);

	const float vert_offset = 0.1f; // ((float)100 / opengl_engine->getViewPortHeight()) / y_scale;

	info_text_view->setPosAndDims(/*botleft=*/coords + Vec2f(-0.3f, -vert_offset), /*dims=*/Vec2f(use_w, use_h));
}


void ObInfoUI::hideHyperLink()
{
	currently_showing_url = "";

	info_text_view->setPosAndDims(/*botleft=*/Vec2f(-100.f, -100.f), /*dims=*/Vec2f(0.6f, 0.2f)); // Position off-screen
}


std::string ObInfoUI::getCurrentlyShowingHyperlink() const // Returns empty string if not showing currently
{
	return currently_showing_url;
}

void ObInfoUI::updateWidgetPositions()
{
	if(gl_ui.nonNull())
	{
		//const float min_max_y = GLUI::getViewportMinMaxY(opengl_engine);
		//
		//const float SPACING = 0.02f;
		//const float BUTTON_W = 0.07f;
		//const float BUTTON_H = 0.07f;
		//
		//selfie_button->setPosAndDims(Vec2f(-1 + SPACING, -min_max_y + SPACING), Vec2f(BUTTON_W, BUTTON_H));

		
	}
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
	if(main_window)
	{
		if(event.widget == info_text_view.ptr())
		{
			//GLUIButton* button = static_cast<GLUIButton*>(event.widget);

			event.accepted = true;

			conPrint("Clicked on text view!");
			//main_window->setSelfieModeEnabled(selfie_button->toggled);
		}
	}
}
