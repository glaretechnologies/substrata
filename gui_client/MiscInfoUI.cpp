/*=====================================================================
MiscInfoUI.cpp
--------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "MiscInfoUI.h"


#include "GUIClient.h"
#include <tracy/Tracy.hpp>


MiscInfoUI::MiscInfoUI()
:	gui_client(NULL),
	visible(true)
{}


MiscInfoUI::~MiscInfoUI()
{}


void MiscInfoUI::create(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_)
{
	ZoneScoped; // Tracy profiler

	opengl_engine = opengl_engine_;
	gui_client = gui_client_;
	gl_ui = gl_ui_;

#if EMSCRIPTEN // Only show login, signup and movement buttons on web.
	GLUITextButton::CreateArgs login_args;
	login_args.tooltip = "Log in to an existing user account";
	login_args.font_size_px = 12;
	login_button = new GLUITextButton(*gl_ui_, opengl_engine_, "Log in", Vec2f(0.f), login_args);
	login_button->handler = this;
	gl_ui->addWidget(login_button);

	GLUITextButton::CreateArgs signup_args;
	signup_args.tooltip = "Create a new user account";
	signup_args.font_size_px = 12;
	signup_button = new GLUITextButton(*gl_ui_, opengl_engine_, "Sign up", Vec2f(0.f), signup_args);
	signup_button->handler = this;
	gl_ui->addWidget(signup_button);


	movement_button = new GLUIButton(*gl_ui_, opengl_engine_, /*tex path=*/gui_client->resources_dir_path + "/buttons/dir_pad.png", Vec2f(0.f), /*dims=*/Vec2f(0.4f, 0.1f), GLUIButton::CreateArgs());
	movement_button->handler = this;
	gl_ui->addWidget(movement_button);


	{
		GLUIButton::CreateArgs args;
		args.tooltip = "Avatar settings";
		avatar_button = new GLUIButton(*gl_ui_, opengl_engine_, gui_client->resources_dir_path + "/buttons/avatar.png", Vec2f(0.f), /*dims=*/Vec2f(0.4f, 0.1f), args);
		avatar_button->handler = this;
		gl_ui->addWidget(avatar_button);
	}
#endif

	updateWidgetPositions();
}


void MiscInfoUI::destroy()
{
	checkRemoveAndDeleteWidget(gl_ui, movement_button);
	checkRemoveAndDeleteWidget(gl_ui, login_button);
	checkRemoveAndDeleteWidget(gl_ui, signup_button);
	checkRemoveAndDeleteWidget(gl_ui, logged_in_button);
	checkRemoveAndDeleteWidget(gl_ui, avatar_button);
	checkRemoveAndDeleteWidget(gl_ui, admin_msg_text_view);
	checkRemoveAndDeleteWidget(gl_ui, unit_string_view);

	for(size_t i=0; i<prebuilt_digits.size(); ++i)
		gl_ui->removeWidget(prebuilt_digits[i]);
	prebuilt_digits.clear();

	gl_ui = NULL;
	opengl_engine = NULL;
}


void MiscInfoUI::setVisible(bool visible_)
{
	visible = visible_;

	if(movement_button) 
		movement_button->setVisible(visible);

	if(login_button) 
		login_button->setVisible(visible);

	if(signup_button) 
		signup_button->setVisible(visible);

	if(logged_in_button) 
		logged_in_button->setVisible(visible);
	
	if(avatar_button) 
		avatar_button->setVisible(visible);

	if(admin_msg_text_view) 
		admin_msg_text_view->setVisible(visible);

	for(size_t i=0; i<prebuilt_digits.size(); ++i)
		prebuilt_digits[i]->setVisible(visible);

	if(unit_string_view) 
		unit_string_view->setVisible(visible);
}


void MiscInfoUI::showLogInAndSignUpButtons()
{
	if(login_button)
		login_button->setVisible(true);

	if(signup_button)
		signup_button->setVisible(true);
	
	if(logged_in_button)
		logged_in_button->setVisible(false);

	updateWidgetPositions();
}


void MiscInfoUI::showLoggedInButton(const std::string& username)
{
	// Hide login and signup buttons
	if(login_button)
		login_button->setVisible(false);

	if(signup_button)
		signup_button->setVisible(false);
	

	// Remove any existing logged_in_button (as text may change)
	if(logged_in_button)
		gl_ui->removeWidget(logged_in_button);
	logged_in_button = NULL;

#if EMSCRIPTEN // Only show on web
	GLUITextButton::CreateArgs logged_in_button_args;
	logged_in_button_args.tooltip = "View user account";
	logged_in_button_args.font_size_px = 12;
	logged_in_button = new GLUITextButton(*gl_ui, opengl_engine, "Logged in as " + username, Vec2f(0.f), logged_in_button_args);
	logged_in_button->handler = this;
	gl_ui->addWidget(logged_in_button);
#endif

	updateWidgetPositions();
}


void MiscInfoUI::showServerAdminMessage(const std::string& msg)
{
	if(msg.empty())
	{
		// Destroy/remove admin_msg_text_view
		if(admin_msg_text_view.nonNull())
		{
			gl_ui->removeWidget(admin_msg_text_view);
			admin_msg_text_view = NULL;
		}
	}
	else
	{
		if(admin_msg_text_view.isNull())
		{
			GLUITextView::CreateArgs create_args;
			admin_msg_text_view = new GLUITextView(*gl_ui, opengl_engine, msg, /*botleft=*/Vec2f(0.1f, 0.9f), create_args); // Create off-screen
			admin_msg_text_view->setTextColour(Colour3f(1.0f, 0.6f, 0.3f));
			gl_ui->addWidget(admin_msg_text_view);
		}

		admin_msg_text_view->setText(*gl_ui, msg);

		updateWidgetPositions();
	}
}

static const int speed_margin_px = 70; // pixels between bottom of viewport and text baseline.
static const int speed_font_size_px = 40;
static const int speed_font_x_advance = 50; // between digits

void MiscInfoUI::showVehicleSpeed(float speed_km_per_h)
{
	const float text_y = -gl_ui->getViewportMinMaxY() + gl_ui->getUIWidthForDevIndepPixelWidth(speed_margin_px);

	// The approach we will take here is to pre-create the digits 0-9 in the ones, tens and hundreds places, and then only make the digits corresponding to the current speed visible.
	// This will avoid any runtime allocs.
	// prebuilt_digits will be laid out like:
	// 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, ..
	// |   \   \
	// |   tens  ones
	// hundreds

	if(prebuilt_digits.empty())
	{
		prebuilt_digits.resize(30);
		for(int i=0; i<30; ++i)
		{
			const int digit_val = i / 3;
			const int digit_place = i % 3;
			GLUITextView::CreateArgs create_args;
			create_args.font_size_px = speed_font_size_px;
			create_args.background_alpha = 0;
			create_args.text_selectable = false;
			prebuilt_digits[i] = new GLUITextView(*gl_ui, opengl_engine, toString(digit_val), Vec2f(0.f + (-3 + digit_place) * gl_ui->getUIWidthForDevIndepPixelWidth(speed_font_x_advance), text_y), create_args);
		}
	}

	const int speed_int = (int)speed_km_per_h;

	for(int i=0; i<3; ++i) // For each digit place:
	{
		const int place_1_val = (i == 0) ? 100 : ((i == 1) ? 10 : 1);
		const int digit_val = (speed_int / place_1_val) % 10;

		for(int z=0; z<10; ++z) // For digit val z at digit place i:
		{
			const bool should_draw = (digit_val == z) && ((speed_int >= place_1_val) || (i == 2 && speed_int == 0)); // Don't show leading zeroes.  But if speed = 0, we do need to show one zero.
			prebuilt_digits[z*3 + i]->setVisible(should_draw && visible);
		}
	}

	if(unit_string_view.isNull())
	{
		const std::string msg = " km/h";
		GLUITextView::CreateArgs create_args;
		create_args.font_size_px = speed_font_size_px;
		create_args.background_alpha = 0;
		create_args.text_selectable = false;
		unit_string_view = new GLUITextView(*gl_ui, opengl_engine, msg, /*botleft=*/Vec2f(gl_ui->getUIWidthForDevIndepPixelWidth(speed_font_x_advance) * 0, text_y), create_args); // Create off-screen
		unit_string_view->setTextColour(Colour3f(1.0f, 1.0f, 1.0f));
		gl_ui->addWidget(unit_string_view);
	}
	else
		unit_string_view->setVisible(true && visible);

	updateWidgetPositions();
}


void MiscInfoUI::showVehicleInfo(const std::string& msg)
{
	updateWidgetPositions();
}



void MiscInfoUI::hideVehicleSpeed()
{
	// Destroy/remove speed_text_view

	for(size_t i=0; i<prebuilt_digits.size(); ++i)
		prebuilt_digits[i]->setVisible(false);

	if(unit_string_view.nonNull())
		unit_string_view->setVisible(false);
}


void MiscInfoUI::think()
{
	if(gl_ui.nonNull())
	{
	}
}


void MiscInfoUI::updateWidgetPositions()
{
	if(gl_ui.nonNull())
	{
		const float min_max_y = gl_ui->getViewportMinMaxY();
		const float margin = gl_ui->getUIWidthForDevIndepPixelWidth(18);

		float cur_x = -1 + margin;
		if(login_button && signup_button)
		{
			const Vec2f login_button_dims  = login_button->rect.getWidths();
			const Vec2f signup_button_dims = signup_button->rect.getWidths();
			
			const float x_spacing = margin;//gl_ui->getUIWidthForDevIndepPixelWidth(10);
			//const float total_w = login_button_dims.x + x_spacing + signup_button_dims.x;

			if(login_button->isVisible())
			{
				login_button ->setPos(/*botleft=*/Vec2f(cur_x                                  , min_max_y - login_button_dims.y  - margin));
				signup_button->setPos(/*botleft=*/Vec2f(cur_x + login_button_dims.x + x_spacing, min_max_y - signup_button_dims.y - margin));

				cur_x = cur_x + login_button_dims.x + x_spacing + signup_button_dims.x + x_spacing;
			}
		}

		if(logged_in_button)
		{
			const Vec2f button_dims = logged_in_button->rect.getWidths();

			if(logged_in_button->isVisible())
			{
				logged_in_button->setPos(/*botleft=*/Vec2f(/*-1 + margin*/cur_x, min_max_y - button_dims.y + - margin));
				cur_x = cur_x + button_dims.x + margin;
			}
		}

		if(avatar_button)
		{
			const float avatar_button_w = gl_ui->getUIWidthForDevIndepPixelWidth(40);

			// Adjust position slightly differently depending on if we have login/logged-in buttons or not.
			if(login_button || logged_in_button)
				avatar_button->setPosAndDims(/*botleft=*/Vec2f(cur_x - margin * 0.4f, min_max_y - avatar_button_w * 0.82f - margin), Vec2f(avatar_button_w, avatar_button_w));
			else
				avatar_button->setPosAndDims(/*botleft=*/Vec2f(cur_x - margin * 0.1f, min_max_y - avatar_button_w * 0.82f - margin), Vec2f(avatar_button_w, avatar_button_w));
		}


		if(admin_msg_text_view.nonNull())
		{
			const Vec2f text_dims = admin_msg_text_view->getRect().getWidths();

			const float vert_margin = 50.f / opengl_engine->getViewPortWidth(); // 50 pixels

			admin_msg_text_view->setPos(*gl_ui, /*botleft=*/Vec2f(-0.4f, min_max_y - text_dims.y - vert_margin));
		}
		

		const float text_y = -gl_ui->getViewportMinMaxY() + gl_ui->getUIWidthForDevIndepPixelWidth(speed_margin_px);

		// 0, 0, 0, 1, 1, 1, 2, 2, 2
		for(int i=0; i<(int)prebuilt_digits.size(); ++i)
		{
			const int digit_place = i % 3;
			prebuilt_digits[i]->setPos(*gl_ui, Vec2f(0.f + (-3 + digit_place) * gl_ui->getUIWidthForDevIndepPixelWidth(speed_font_x_advance), text_y));
		}
		if(unit_string_view.nonNull())
			unit_string_view->setPos(*gl_ui, /*botleft=*/Vec2f(gl_ui->getUIWidthForDevIndepPixelWidth(speed_font_x_advance) * 0, text_y));


		if(movement_button)
		{
			const float spacing = gl_ui->getUIWidthForDevIndepPixelWidth(25);

			const float button_w = myMax(gl_ui->getUIWidthForDevIndepPixelWidth(100), 0.15f);
			const float button_h = button_w;
			const Vec2f pos = Vec2f(-1.f + 0.4f /*+ spacing*/, -min_max_y + spacing);

			movement_button->setPosAndDims(pos, Vec2f(button_w, button_h));
		}
	}
}


void MiscInfoUI::viewportResized(int w, int h)
{
	if(gl_ui.nonNull())
	{
		if(login_button) login_button->rebuild();
		if(signup_button) signup_button->rebuild();
		if(logged_in_button) logged_in_button->rebuild();

		updateWidgetPositions();
	}
}


void MiscInfoUI::eventOccurred(GLUICallbackEvent& event)
{
	if(gui_client)
	{
		if(event.widget == admin_msg_text_view.ptr())
		{
			//GLUIButton* button = static_cast<GLUIButton*>(event.widget);

			event.accepted = true;

			//conPrint("Clicked on text view!");
			//gui_client->setSelfieModeEnabled(selfie_button->toggled);
		}
		else if(event.widget == login_button.ptr())
		{
			gui_client->loginButtonClicked();

			event.accepted = true;
		}
		else if(event.widget == signup_button.ptr())
		{
			gui_client->signupButtonClicked();

			event.accepted = true;
		}
		else if(event.widget == logged_in_button.ptr())
		{
			gui_client->loggedInButtonClicked();

			event.accepted = true;
		}
		else if(event.widget == movement_button.ptr())
		{
			event.accepted = true;
		}
		else if(event.widget == avatar_button.ptr())
		{
			gui_client->ui_interface->showAvatarSettings();
		}
	}
}
