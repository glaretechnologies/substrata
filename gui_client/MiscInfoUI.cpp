/*=====================================================================
MiscInfoUI.cpp
--------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "MiscInfoUI.h"


#include "GUIClient.h"


MiscInfoUI::MiscInfoUI()
:	gui_client(NULL)
{}


MiscInfoUI::~MiscInfoUI()
{}


void MiscInfoUI::create(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_)
{
	opengl_engine = opengl_engine_;
	gui_client = gui_client_;
	gl_ui = gl_ui_;

	updateWidgetPositions();
}


void MiscInfoUI::destroy()
{
	if(admin_msg_text_view.nonNull())
		gl_ui->removeWidget(admin_msg_text_view);
	admin_msg_text_view = NULL;

	for(size_t i=0; i<prebuilt_digits.size(); ++i)
		gl_ui->removeWidget(prebuilt_digits[i]);
	prebuilt_digits.clear();

	if(unit_string_view.nonNull())
		gl_ui->removeWidget(unit_string_view);
	unit_string_view = NULL;
	
	gl_ui = NULL;
	opengl_engine = NULL;
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
			admin_msg_text_view->setColour(Colour3f(1.0f, 0.6f, 0.3f));
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
			prebuilt_digits[z*3 + i]->setVisible(should_draw);
		}
	}

	if(unit_string_view.isNull())
	{
		const std::string msg = " km/h";
		GLUITextView::CreateArgs create_args;
		create_args.font_size_px = speed_font_size_px;
		create_args.background_alpha = 0;
		unit_string_view = new GLUITextView(*gl_ui, opengl_engine, msg, /*botleft=*/Vec2f(gl_ui->getUIWidthForDevIndepPixelWidth(speed_font_x_advance) * 0, text_y), create_args); // Create off-screen
		unit_string_view->setColour(Colour3f(1.0f, 1.0f, 1.0f));
		gl_ui->addWidget(unit_string_view);
	}
	else
		unit_string_view->setVisible(true);

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
		if(admin_msg_text_view.nonNull())
		{
			const float min_max_y = gl_ui->getViewportMinMaxY();

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
	}
}


void MiscInfoUI::viewportResized(int w, int h)
{
	if(gl_ui.nonNull())
	{
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
	}
}
