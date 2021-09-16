/*=====================================================================
GestureUI.cpp
-------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "GestureUI.h"


#include "MainWindow.h"
#include <QtCore/QSettings>


GestureUI::GestureUI()
:	main_window(NULL),
	gestures_visible(false)
{}


GestureUI::~GestureUI()
{}


// Column 0: Animation name
// Column 1: Should the animation data control the head (e.g. override the procedural lookat anim)?
// Column 2: Should the animation automatically loop.
static const char* gestures[] = {
	"Clapping",						"",				"Loop",
	"Dancing",						"AnimHead",		"Loop",
	"Excited",						"AnimHead",		"",
	"Looking",						"AnimHead",		"",
	"Quick Informal Bow",			"AnimHead",		"",
	"Rejected",						"AnimHead",		"",
	"Sit",							"",				"Loop",
	"Sitting On Ground",			"",				"Loop",
	"Sitting Talking",				"",				"Loop",
	"Sleeping Idle",				"AnimHead",		"Loop",
	"Standing React Death Forward",	"AnimHead",		"",
	"Waving 1",						"",				"",
	"Waving 2",						"",				"",
	"Yawn",							"AnimHead",		""
};


bool GestureUI::animateHead(const std::string& gesture)
{
	for(size_t i=0; i<staticArrayNumElems(gestures); i += 3)
		if(gestures[i] == gesture)
			return std::string(gestures[i+1]) == "AnimHead";
	assert(0);
	return false;
}


bool GestureUI::loopAnim(const std::string& gesture)
{
	for(size_t i=0; i<staticArrayNumElems(gestures); i += 3)
		if(gestures[i] == gesture)
			return std::string(gestures[i+2]) == "Loop";
	assert(0);
	return false;
}


void GestureUI::create(Reference<OpenGLEngine>& opengl_engine_, MainWindow* main_window_)
{
	opengl_engine = opengl_engine_;
	main_window = main_window_;

	gestures_visible = main_window->settings->value("GestureUI/gestures_visible", /*default val=*/false).toBool();

	gl_ui = new GLUI();
	gl_ui->create(opengl_engine, this);

	const float min_max_y = GLUI::getViewportMinMaxY(opengl_engine);

	for(size_t i=0; i<staticArrayNumElems(gestures); i += 3)
	{
		const std::string gesture_name = gestures[i];
		//const bool animate_head = std::string(gestures[i+1]) == "AnimHead";
		const bool looping = std::string(gestures[i+2]) == "Loop";

		GLUIButtonRef button = new GLUIButton();
		button->create(*gl_ui, opengl_engine, "N:\\new_cyberspace\\trunk\\source_resources\\buttons\\" + gesture_name + ".png", Vec2f(0.1f + i * 0.15f, -min_max_y + 0.06f), Vec2f(0.1f, 0.1f), /*tooltip=*/gesture_name);
		button->toggleable = looping;
		button->client_data = gesture_name;
		button->handler = this;
		gl_ui->addWidget(button);

		gesture_buttons.push_back(button);
	}

	// Create left and right tab buttons
	left_tab_button = new GLUIButton();
	left_tab_button->create(*gl_ui, opengl_engine, "N:\\new_cyberspace\\trunk\\source_resources\\buttons\\left_tab.png", Vec2f(0.1f, 0.1f), Vec2f(0.1f, 0.1f), /*tooltip=*/"View gestures");
	left_tab_button->handler = this;
	gl_ui->addWidget(left_tab_button);
	
	right_tab_button = new GLUIButton();
	right_tab_button->create(*gl_ui, opengl_engine, "N:\\new_cyberspace\\trunk\\source_resources\\buttons\\right_tab.png", Vec2f(0.1f, 0.1f), Vec2f(0.1f, 0.1f), /*tooltip=*/"Hide gestures");
	right_tab_button->handler = this;
	gl_ui->addWidget(right_tab_button);

	updateWidgetPositions();
}


void GestureUI::destroy()
{
	if(left_tab_button.nonNull())
	{
		gl_ui->removeWidget(left_tab_button);
		left_tab_button->destroy();
	}
	if(right_tab_button.nonNull())
	{
		gl_ui->removeWidget(right_tab_button);
		right_tab_button->destroy();
	}


	for(size_t i=0; i<gesture_buttons.size(); ++i)
	{
		gl_ui->removeWidget(gesture_buttons[i]);
		gesture_buttons[i]->destroy();
	}
	gesture_buttons.resize(0);

	gl_ui->destroy();
	gl_ui = NULL;
}


bool GestureUI::handleMouseClick(const Vec2f& gl_coords)
{
	if(gl_ui.nonNull())
		return gl_ui->handleMouseClick(gl_coords);
	else
		return false;
}


bool GestureUI::handleMouseMoved(const Vec2f& gl_coords)
{
	if(gl_ui.nonNull())
		return gl_ui->handleMouseMoved(gl_coords);
	else
		return false;
}


void GestureUI::updateWidgetPositions()
{
	if(gl_ui.nonNull())
	{
		const float min_max_y = GLUI::getViewportMinMaxY(opengl_engine);

		const float SPACING = 0.02f;
		const float BUTTON_W = 0.07f;
		const float BUTTON_H = 0.07f;

		const int NUM_BUTTONS_PER_ROW = 7;
		const float GESTURES_LEFT_X = gestures_visible ? (1 - (BUTTON_W * NUM_BUTTONS_PER_ROW + SPACING * NUM_BUTTONS_PER_ROW)) : 1000;

		for(size_t i=0; i<gesture_buttons.size(); ++i)
		{
			const float x = (i % NUM_BUTTONS_PER_ROW) * (BUTTON_W + SPACING) + GESTURES_LEFT_X;
			const float y = (i / NUM_BUTTONS_PER_ROW) * (BUTTON_H + SPACING);
			gesture_buttons[i]->setPosAndDims(Vec2f(x, -min_max_y + y + SPACING), Vec2f(BUTTON_W, BUTTON_H));
		}

		const float TAB_BUTTON_W = 0.05f;
		right_tab_button->setPosAndDims(Vec2f(GESTURES_LEFT_X - TAB_BUTTON_W - SPACING, -min_max_y + SPACING), Vec2f(TAB_BUTTON_W, BUTTON_H * 2 + SPACING));

		if(!gestures_visible)
			left_tab_button->setPosAndDims(Vec2f(1 - TAB_BUTTON_W - SPACING, -min_max_y + SPACING), Vec2f(TAB_BUTTON_W, BUTTON_H * 2 + SPACING));
		else
			left_tab_button->setPosAndDims(Vec2f(1000, -min_max_y + SPACING), Vec2f(TAB_BUTTON_W, BUTTON_H * 2 + SPACING)); // hide
	}
}


void GestureUI::viewportResized(int w, int h)
{
	if(gl_ui.nonNull())
	{
		gl_ui->viewportResized(w, h);

		updateWidgetPositions();
	}
}


void GestureUI::eventOccurred(GLUICallbackEvent& event)
{
	if(main_window)
	{
		GLUIButton* button = static_cast<GLUIButton*>(event.widget);

		for(size_t i=0; i<staticArrayNumElems(gestures); i += 3)
		{
			const std::string gesture_name = gestures[i];
			if(gesture_name == event.widget->client_data)
			{
				event.accepted = true;
				const bool animate_head = std::string(gestures[i+1]) == "AnimHead";

				if(button->toggleable)
				{
					if(button->toggled)
						main_window->performGestureClicked(event.widget->client_data, animate_head, /*loop anim=*/true);
					else
						main_window->stopGestureClicked(event.widget->client_data);
				}
				else
					main_window->performGestureClicked(event.widget->client_data, animate_head, /*loop anim=*/false);

				// Untoggle any other toggled buttons.
				for(size_t z=0; z<gesture_buttons.size(); ++z)
					if(gesture_buttons[z] != button && gesture_buttons[z]->toggleable)
						gesture_buttons[z]->setToggled(false);
			}
		}

		if(button == left_tab_button.ptr())
		{
			event.accepted = true;
			gestures_visible = true;
			updateWidgetPositions();
			main_window->settings->setValue("GestureUI/gestures_visible", gestures_visible);
		}
		else if(button == right_tab_button.ptr())
		{
			event.accepted = true;
			gestures_visible = false;
			updateWidgetPositions();
			main_window->settings->setValue("GestureUI/gestures_visible", gestures_visible);
		}
	}
}


OpenGLTextureRef GestureUI::makeToolTipTexture(const std::string& text)
{
	if(main_window)
	{
		return main_window->makeToolTipTexture(text);
	}
	else
		return NULL;
}
