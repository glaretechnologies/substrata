/*=====================================================================
GestureUI.cpp
-------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "GestureUI.h"


#include "GUIClient.h"
#include <settings/SettingsStore.h>
#include <graphics/SRGBUtils.h>
#include <tracy/Tracy.hpp>


GestureUI::GestureUI()
:	gui_client(NULL),
	gestures_visible(false),
	vehicle_buttons_visible(false),
	untoggle_button_time(-1)
{}


GestureUI::~GestureUI()
{}


// Column 0: Animation name
// Column 1: Should the animation data control the head (e.g. override the procedural lookat anim)?
// Column 2: Should the animation automatically loop.
// Column 3: Animation duration (from debug output in OpenGLEngine.cpp, conPrint("anim_datum_a..  etc..")
static const char* gestures[] = {
	"Clapping",						"",				"Loop",		"",
	"Dancing",						"AnimHead",		"Loop",		"",
	"Dancing 2",					"AnimHead",		"Loop",		"",
	"Excited",						"AnimHead",		"Loop",		"6.5666666",
	"Looking",						"AnimHead",		"",			"8.016666",
	"Quick Informal Bow",			"AnimHead",		"",			"2.75",
	"Rejected",						"AnimHead",		"",			"4.8166666",
	"Sit",							"",				"Loop",		"",
	"Sitting On Ground",			"",				"Loop",		"",
	"Sleeping Idle",				"AnimHead",		"Loop",		"",
	"Standing React Death Forward",	"AnimHead",		"",			"3.6833334",
	"Waving 1",						"",				"Loop",		"",
	"Waving 2",						"",				"",			"3.1833334",
	"Yawn",							"AnimHead",		"",			"8.35"
};

static const int NUM_GESTURE_FIELDS = 4;

static_assert((staticArrayNumElems(gestures) % NUM_GESTURE_FIELDS) == 0, "(staticArrayNumElems(gestures) % NUM_GESTURE_FIELDS) == 0");

bool GestureUI::animateHead(const std::string& gesture)
{
	for(size_t i=0; i<staticArrayNumElems(gestures); i += NUM_GESTURE_FIELDS)
		if(gestures[i] == gesture)
			return std::string(gestures[i+1]) == "AnimHead";
	assert(0);
	return false;
}


bool GestureUI::loopAnim(const std::string& gesture)
{
	for(size_t i=0; i<staticArrayNumElems(gestures); i += NUM_GESTURE_FIELDS)
		if(gestures[i] == gesture)
			return std::string(gestures[i+2]) == "Loop";
	assert(0);
	return false;
}


void GestureUI::create(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_)
{
	ZoneScoped; // Tracy profiler

	opengl_engine = opengl_engine_;
	gui_client = gui_client_;
	gl_ui = gl_ui_;

	gestures_visible        = gui_client->getSettingsStore()->getBoolValue("GestureUI/gestures_visible",        /*default val=*/false);
	vehicle_buttons_visible = false;

	const float min_max_y = gl_ui->getViewportMinMaxY();

	for(size_t i=0; i<staticArrayNumElems(gestures); i += NUM_GESTURE_FIELDS)
	{
		const std::string gesture_name = gestures[i];

		GLUIButton::CreateArgs args;
		args.tooltip = gesture_name;
		GLUIButtonRef button = new GLUIButton(*gl_ui, opengl_engine,  gui_client->resources_dir_path + "/buttons/" + gesture_name + ".png", Vec2f(0.1f + i * 0.15f, -min_max_y + 0.06f), Vec2f(0.1f, 0.1f), args);
		button->toggleable = true;
		button->client_data = gesture_name;
		button->handler = this;
		gl_ui->addWidget(button);

		gesture_buttons.push_back(button);
	}

	// Create left and right tab buttons
	{
		GLUIButton::CreateArgs args;
		args.tooltip = "View gestures";
		expand_button = new GLUIButton(*gl_ui, opengl_engine, gui_client->resources_dir_path + "/buttons/Waving 1.png", Vec2f(0), Vec2f(0.1f, 0.1f), args);
		expand_button->handler = this;
		gl_ui->addWidget(expand_button);
	}
	
	{
		GLUIButton::CreateArgs args;
		args.tooltip = "Hide gestures";
		collapse_button = new GLUIButton(*gl_ui, opengl_engine, gui_client->resources_dir_path + "/buttons/right_tab.png", Vec2f(0), Vec2f(0.1f, 0.1f), args);
		collapse_button->handler = this;
		gl_ui->addWidget(collapse_button);
	}
	
	{
		{
			GLUIButton::CreateArgs args;
			args.tooltip = "Summon vehicle";
			vehicle_button = new GLUIButton(*gl_ui, opengl_engine, gui_client->resources_dir_path + "/buttons/bike.png", Vec2f(0), Vec2f(0.1f, 0.1f),args);
			vehicle_button->handler = this;
			gl_ui->addWidget(vehicle_button);
		}

		{
			GLUITextButton::CreateArgs args;
			args.tooltip = "Summon bike";
			summon_bike_button = new GLUITextButton(*gl_ui, opengl_engine_, "Summon bike", Vec2f(0), args);
			summon_bike_button->setVisible(vehicle_buttons_visible);
			summon_bike_button->handler = this;
			gl_ui->addWidget(summon_bike_button);
		}
		{
			GLUITextButton::CreateArgs args;
			args.tooltip = "Summon car";
			summon_car_button = new GLUITextButton(*gl_ui, opengl_engine_, "Summon car", Vec2f(0), args);
			summon_car_button->setVisible(vehicle_buttons_visible);
			summon_car_button->handler = this;
			gl_ui->addWidget(summon_car_button);
		}
		{
			GLUITextButton::CreateArgs args;
			args.tooltip = "Summon boat";
			summon_boat_button = new GLUITextButton(*gl_ui, opengl_engine_, "Summon boat", Vec2f(0), args);
			summon_boat_button->setVisible(vehicle_buttons_visible);
			summon_boat_button->handler = this;
			gl_ui->addWidget(summon_boat_button);
		}
		{
			GLUITextButton::CreateArgs args;
			args.tooltip = "Summon jet ski";
			summon_jetski_button = new GLUITextButton(*gl_ui, opengl_engine_, "Summon jet ski", Vec2f(0), args);
			summon_jetski_button->setVisible(vehicle_buttons_visible);
			summon_jetski_button->handler = this;
			gl_ui->addWidget(summon_jetski_button);
		}
		{
			GLUITextButton::CreateArgs args;
			args.tooltip = "Summon hovercar";
			summon_hovercar_button = new GLUITextButton(*gl_ui, opengl_engine_, "Summon hovercar", Vec2f(0), args);
			summon_hovercar_button->setVisible(vehicle_buttons_visible);
			summon_hovercar_button->handler = this;
			gl_ui->addWidget(summon_hovercar_button);
		}

		{
			GLUIButton::CreateArgs args;
			args.tooltip = "Hide vehicles";
			collapse_vehicle_button = new GLUIButton(*gl_ui, opengl_engine, gui_client->resources_dir_path + "/buttons/down_tab.png", Vec2f(0), Vec2f(0.1f, 0.1f), args);
			collapse_vehicle_button->handler = this;
			collapse_vehicle_button->setVisible(vehicle_buttons_visible);
			gl_ui->addWidget(collapse_vehicle_button);
		}

	}
	
	{
		GLUIButton::CreateArgs args;
		args.tooltip = "Photo mode";
		photo_mode_button = new GLUIButton(*gl_ui, opengl_engine, gui_client->resources_dir_path + "/buttons/Selfie.png", Vec2f(0), Vec2f(0.1f, 0.1f),args);
		photo_mode_button->toggleable = true;
		photo_mode_button->handler = this;
		gl_ui->addWidget(photo_mode_button);
	}
	
	{	
		GLUIButton::CreateArgs args;
		args.tooltip = "Enable microphone for voice chat";
		microphone_button = new GLUIButton(*gl_ui, opengl_engine, gui_client->resources_dir_path + "/buttons/microphone.png", Vec2f(0), Vec2f(0.1f, 0.1f), args);
		microphone_button->toggleable = true;
		microphone_button->handler = this;
		gl_ui->addWidget(microphone_button);
	}

	{	
		mic_level_image = new GLUIImage(*gl_ui, opengl_engine, ""/*gui_client->base_dir_path + "/resources/buttons/mic_level.png"*/, Vec2f(0), Vec2f(0.1f, 0.1f), "Microphone input indicator");
		gl_ui->addWidget(mic_level_image);
	}

	updateWidgetPositions();
}


void GestureUI::destroy()
{
	checkRemoveAndDeleteWidget(gl_ui, expand_button);
	checkRemoveAndDeleteWidget(gl_ui, collapse_button);
	checkRemoveAndDeleteWidget(gl_ui, vehicle_button);
	checkRemoveAndDeleteWidget(gl_ui, photo_mode_button);
	checkRemoveAndDeleteWidget(gl_ui, microphone_button);
	checkRemoveAndDeleteWidget(gl_ui, mic_level_image);
	checkRemoveAndDeleteWidget(gl_ui, summon_bike_button);
	checkRemoveAndDeleteWidget(gl_ui, summon_car_button);
	checkRemoveAndDeleteWidget(gl_ui, summon_boat_button);
	checkRemoveAndDeleteWidget(gl_ui, summon_jetski_button);
	checkRemoveAndDeleteWidget(gl_ui, summon_hovercar_button);
	checkRemoveAndDeleteWidget(gl_ui, collapse_vehicle_button);

	for(size_t i=0; i<gesture_buttons.size(); ++i)
		gl_ui->removeWidget(gesture_buttons[i]);
	gesture_buttons.resize(0);

	gl_ui = NULL;
	opengl_engine = NULL;
}


void GestureUI::think()
{
	if(gl_ui.nonNull())
	{
		// Untoggle gesture buttons if we have reached untoggle_button_time.
		if((untoggle_button_time > 0) && (timer.elapsed() >= untoggle_button_time))
		{
			for(size_t i=0; i<gesture_buttons.size(); ++i)
				gesture_buttons[i]->setToggled(false);

			untoggle_button_time = -1;
		}
	}
}


//bool GestureUI::handleMouseClick(const Vec2f& gl_coords)
//{
//	if(gl_ui.nonNull())
//		return gl_ui->handleMouseClick(gl_coords);
//	else
//		return false;
//}
//
//
//bool GestureUI::handleMouseMoved(const Vec2f& gl_coords)
//{
//	if(gl_ui.nonNull())
//		return gl_ui->handleMouseMoved(gl_coords);
//	else
//		return false;
//}


static const float BUTTON_W_PIXELS = 50;

void GestureUI::updateWidgetPositions()
{
	if(gl_ui.nonNull())
	{
		const float min_max_y = gl_ui->getViewportMinMaxY();

		const float BUTTON_W = gl_ui->getUIWidthForDevIndepPixelWidth(BUTTON_W_PIXELS);

		const float BUTTON_H = BUTTON_W;
		const float SPACING = BUTTON_W * 0.28f;

		// On narrow screens (e.g. phones), the gesture buttons may intefere with other buttons or even go offscreen to the left.  So arrange more vertically, with less per line.
		const int NUM_BUTTONS_PER_ROW = ((BUTTON_W * 7 + SPACING * 7) < 0.8f) ? 7 : 3;
		const float GESTURES_LEFT_X = gestures_visible ? (1 - (BUTTON_W * NUM_BUTTONS_PER_ROW + SPACING * NUM_BUTTONS_PER_ROW)) : 1000;

		const float gestures_bottom_margin = (GESTURES_LEFT_X < 0.3f) ? gl_ui->getUIWidthForDevIndepPixelWidth(120) : SPACING; // If gesture buttons would overlap with other buttons (e.g. on narrow screens), move up.
		for(size_t i=0; i<gesture_buttons.size(); ++i)
		{
			const float x = (i % NUM_BUTTONS_PER_ROW) * (BUTTON_W + SPACING) + GESTURES_LEFT_X;
			const float y = (i / NUM_BUTTONS_PER_ROW) * (BUTTON_H + SPACING);
			gesture_buttons[i]->setPosAndDims(Vec2f(x, -min_max_y + y + gestures_bottom_margin), Vec2f(BUTTON_W, BUTTON_H));
		}

		if(collapse_button)
		{
			const float TAB_BUTTON_W = gl_ui->getUIWidthForDevIndepPixelWidth(40/*35*/);

			static const float collapse_button_w_px = 20;
			static const float collapse_button_h_px = 50;
			const float collapse_button_w = gl_ui->getUIWidthForDevIndepPixelWidth(collapse_button_w_px); // for vertical collapse button (left / right)
			const float collapse_button_h = gl_ui->getUIWidthForDevIndepPixelWidth(collapse_button_h_px);

			const float num_gesture_button_rows = (float)Maths::roundedUpDivide((int)gesture_buttons.size(), NUM_BUTTONS_PER_ROW);
			collapse_button->setPosAndDims(Vec2f(GESTURES_LEFT_X - collapse_button_w - SPACING, -min_max_y + num_gesture_button_rows * (BUTTON_H + SPACING) - SPACING - collapse_button_h + gestures_bottom_margin), Vec2f(collapse_button_w, collapse_button_h));

			expand_button->setPosAndDims(Vec2f(1 - TAB_BUTTON_W - SPACING, -min_max_y + SPACING), Vec2f(TAB_BUTTON_W, TAB_BUTTON_W/*BUTTON_H * 2 + SPACING*/));
			expand_button->setVisible(!gestures_visible);

			const float vehicle_button_x = -BUTTON_W * 1.5f - SPACING;
			vehicle_button->setPosAndDims(Vec2f(vehicle_button_x, -min_max_y + SPACING), Vec2f(BUTTON_W, BUTTON_H));

			const float selfie_button_x = vehicle_button_x + BUTTON_W + SPACING;
			photo_mode_button->setPosAndDims(Vec2f(selfie_button_x, -min_max_y + SPACING), Vec2f(BUTTON_W, BUTTON_H));

			const float mic_button_x = selfie_button_x + BUTTON_W + SPACING;
			microphone_button->setPosAndDims(Vec2f(mic_button_x, -min_max_y + SPACING), Vec2f(BUTTON_W, BUTTON_H));

			mic_level_image->setPosAndDims(Vec2f(mic_button_x + BUTTON_W * 0.8f, -min_max_y + SPACING + BUTTON_H * 0.2f), Vec2f(BUTTON_H * 0.2f, 0));


			if(vehicle_buttons_visible)
			{
				summon_jetski_button  ->setPos(Vec2f(vehicle_button_x, -min_max_y + BUTTON_W + 2*SPACING));
				summon_boat_button    ->setPos(Vec2f(vehicle_button_x, summon_jetski_button->rect.getMax().y + SPACING));
				summon_hovercar_button->setPos(Vec2f(vehicle_button_x, summon_boat_button->rect.getMax().y + SPACING));
				summon_car_button     ->setPos(Vec2f(vehicle_button_x, summon_hovercar_button->rect.getMax().y + SPACING));
				summon_bike_button    ->setPos(Vec2f(vehicle_button_x, summon_car_button->rect.getMax().y + SPACING));

				collapse_vehicle_button->setPosAndDims(Vec2f(vehicle_button_x, summon_bike_button->rect.getMax().y + SPACING), Vec2f(collapse_button_h, collapse_button_w));
			}
		}
	}
}


void GestureUI::viewportResized(int w, int h)
{
	if(gl_ui.nonNull())
	{
		updateWidgetPositions();
	}
}


void GestureUI::setVisible(bool visible)
{
	if(gl_ui.nonNull())
	{
		for(size_t i=0; i<gesture_buttons.size(); ++i)
			gesture_buttons[i]->setVisible(visible);

		collapse_button->setVisible(visible);
		expand_button->setVisible(visible);
		vehicle_button->setVisible(visible);
		photo_mode_button->setVisible(visible);
		microphone_button->setVisible(visible);
		mic_level_image->setVisible(visible);
	}
}


void GestureUI::eventOccurred(GLUICallbackEvent& event)
{
	if(gui_client)
	{
		GLUIButton* button = dynamic_cast<GLUIButton*>(event.widget);
		if(button)
		{
			for(size_t i=0; i<staticArrayNumElems(gestures); i += NUM_GESTURE_FIELDS)
			{
				const std::string gesture_name = gestures[i];
				if(gesture_name == event.widget->client_data)
				{
					event.accepted = true;
					const bool animate_head = std::string(gestures[i+1]) == "AnimHead";
					const bool loop			= std::string(gestures[i+2]) == "Loop";

					if(button->toggleable)
					{
						if(button->toggled)
						{
							gui_client->performGestureClicked(/*gesture name=*/event.widget->client_data, animate_head, /*loop anim=*/loop);

							if(!loop)
								untoggle_button_time = timer.elapsed() + ::stringToDouble(gestures[i+3]); // Make button untoggle when gesture has finished.
							else
								untoggle_button_time = -1;
						}
						else
							gui_client->stopGestureClicked(/*gesture name=*/event.widget->client_data);
					}
					else
						gui_client->performGestureClicked(/*gesture name=*/event.widget->client_data, animate_head, /*loop anim=*/false);

					// Untoggle any other toggled buttons.
					for(size_t z=0; z<gesture_buttons.size(); ++z)
						if(gesture_buttons[z] != button && gesture_buttons[z]->toggleable)
							gesture_buttons[z]->setToggled(false);
				}
			}

			if(button == expand_button.ptr())
			{
				event.accepted = true;
				gestures_visible = true;
				updateWidgetPositions();
				gui_client->getSettingsStore()->setBoolValue("GestureUI/gestures_visible", gestures_visible);
			}
			else if(button == collapse_button.ptr())
			{
				event.accepted = true;
				gestures_visible = false;
				updateWidgetPositions();
				gui_client->getSettingsStore()->setBoolValue("GestureUI/gestures_visible", gestures_visible);
			}
			else if(button == vehicle_button.ptr())
			{
				event.accepted = true;
				vehicle_buttons_visible = !vehicle_buttons_visible;
				summon_bike_button->setVisible(vehicle_buttons_visible);
				summon_car_button->setVisible(vehicle_buttons_visible);
				summon_boat_button->setVisible(vehicle_buttons_visible);
				summon_jetski_button->setVisible(vehicle_buttons_visible);
				summon_hovercar_button->setVisible(vehicle_buttons_visible);
				collapse_vehicle_button->setVisible(vehicle_buttons_visible);

				updateWidgetPositions();
			}
			else if(button == collapse_vehicle_button.ptr())
			{
				event.accepted = true;
				vehicle_buttons_visible = false;
				summon_bike_button->setVisible(vehicle_buttons_visible);
				summon_car_button->setVisible(vehicle_buttons_visible);
				summon_boat_button->setVisible(vehicle_buttons_visible);
				summon_jetski_button->setVisible(vehicle_buttons_visible);
				summon_hovercar_button->setVisible(vehicle_buttons_visible);
				collapse_vehicle_button->setVisible(vehicle_buttons_visible);

				updateWidgetPositions();
			}
			else if(button == photo_mode_button.ptr())
			{
				event.accepted = true;
				//gui_client->setSelfieModeEnabled(photo_mode_button->toggled);
				gui_client->setPhotoModeEnabled(photo_mode_button->toggled);
			}
			else if(button == microphone_button.ptr())
			{
				event.accepted = true;
				gui_client->setMicForVoiceChatEnabled(microphone_button->toggled);

				if(microphone_button->toggled)
					microphone_button->tooltip = "Disable microphone for voice chat";
				else
					microphone_button->tooltip = "Enable microphone for voice chat";
			}
		}

		bool hide_vehicle_buttons = false;
		if(event.widget == summon_bike_button.ptr())
		{
			event.accepted = true;
			try
			{
				gui_client->summonBike();
			}
			catch(glare::Exception& e)
			{
				gui_client->showErrorNotification(e.what());
			}
			hide_vehicle_buttons = true;
		}
		else if(event.widget == summon_car_button.ptr())
		{
			event.accepted = true;
			try
			{
				gui_client->summonCar();
			}
			catch(glare::Exception& e)
			{
				gui_client->showErrorNotification(e.what());
			}
			hide_vehicle_buttons = true;
		}
		else if(event.widget == summon_boat_button.ptr())
		{
			event.accepted = true;
			try
			{
				gui_client->summonBoat();
			}
			catch(glare::Exception& e)
			{
				gui_client->showErrorNotification(e.what());
			}
			hide_vehicle_buttons = true;
		}
		else if(event.widget == summon_jetski_button.ptr())
		{
			event.accepted = true;
			try
			{
				gui_client->summonJetSki();
			}
			catch(glare::Exception& e)
			{
				gui_client->showErrorNotification(e.what());
			}
			hide_vehicle_buttons = true;
		}
		else if(event.widget == summon_hovercar_button.ptr())
		{
			event.accepted = true;
			try
			{
				gui_client->summonHovercar();
			}
			catch(glare::Exception& e)
			{
				gui_client->showErrorNotification(e.what());
			}
			hide_vehicle_buttons = true;
		}

		if(hide_vehicle_buttons)
		{
			vehicle_buttons_visible = false;
			summon_bike_button->setVisible(vehicle_buttons_visible);
			summon_car_button->setVisible(vehicle_buttons_visible);
			summon_boat_button->setVisible(vehicle_buttons_visible);
			summon_jetski_button->setVisible(vehicle_buttons_visible);
			summon_hovercar_button->setVisible(vehicle_buttons_visible);
			collapse_vehicle_button->setVisible(vehicle_buttons_visible);
			updateWidgetPositions();
		}
	}
}


bool GestureUI::getCurrentGesturePlaying(std::string& gesture_name_out, bool& animate_head_out, bool& loop_out)
{
	for(size_t z=0; z<gesture_buttons.size(); ++z)
	{
		if(gesture_buttons[z]->toggled)
		{
			const std::string button_gesture_name = gesture_buttons[z]->client_data;

			// Find matching gesture
			for(size_t i=0; i<staticArrayNumElems(gestures); i += NUM_GESTURE_FIELDS)
			{
				const std::string gesture_name = gestures[i];
				if(button_gesture_name == gesture_name)
				{
					const bool animate_head = std::string(gestures[i+1]) == "AnimHead";
					const bool loop			= std::string(gestures[i+2]) == "Loop";

					gesture_name_out = gesture_name;
					animate_head_out = animate_head;
					loop_out = loop;
					return true;
				}
			}
		}
	}

	return false;
}


void GestureUI::stopAnyGesturePlaying()
{
	// Untoggle any toggled buttons.
	for(size_t z=0; z<gesture_buttons.size(); ++z)
		gesture_buttons[z]->setToggled(false);

	untoggle_button_time = -1;
}


//void GestureUI::turnOffSelfieMode()
//{
//	selfie_button->setToggled(false);
//	gui_client->setSelfieModeEnabled(selfie_button->toggled);
//}


void GestureUI::untoggleMicButton()
{
	if(microphone_button.nonNull())
		microphone_button->setToggled(false);
}


void GestureUI::setCurrentMicLevel(float linear_level, float display_level)
{
	if(mic_level_image.nonNull())
	{
		const float BUTTON_W = gl_ui->getUIWidthForDevIndepPixelWidth(BUTTON_W_PIXELS);
		const float BUTTON_H = BUTTON_W;

		mic_level_image->setDims(Vec2f(BUTTON_W * 0.14f, BUTTON_H * display_level * 0.6f));

		// Show a green bar that changes to red if the amplitude gets too close to 1.
		const Colour3f green = toLinearSRGB(Colour3f(0, 54.5f/100, 8.6f/100));
		const Colour3f red   = toLinearSRGB(Colour3f(78.7f / 100, 0, 0));

		mic_level_image->setColour(Maths::lerp(green, red, Maths::smoothStep(0.9f, 0.95f, linear_level)));
	}
}
