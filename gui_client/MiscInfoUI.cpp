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
	{
		gl_ui->removeWidget(admin_msg_text_view);
		admin_msg_text_view->destroy();
		admin_msg_text_view = NULL;
	}
	
	if(speed_text_view.nonNull())
	{
		gl_ui->removeWidget(speed_text_view);
		speed_text_view->destroy();
		speed_text_view = NULL;
	}

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
			admin_msg_text_view->destroy();
			admin_msg_text_view = NULL;
		}
	}
	else
	{
		if(admin_msg_text_view.isNull())
		{
			admin_msg_text_view = new GLUITextView();
			admin_msg_text_view->create(*gl_ui, opengl_engine, msg, /*botleft=*/Vec2f(0.1f, 0.9f), /*dims=*/Vec2f(0.1f, 0.1f), /*tooltip=*/""); // Create off-screen
			admin_msg_text_view->setColour(Colour3f(1.0f, 0.6f, 0.3f));
			admin_msg_text_view->handler = this;
			gl_ui->addWidget(admin_msg_text_view);
		}

		admin_msg_text_view->setText(*gl_ui, msg);

		updateWidgetPositions();
	}
}


void MiscInfoUI::showVehicleSpeed(float speed_km_per_h)
{
	const std::string msg = doubleToStringMaxNDecimalPlaces(speed_km_per_h, 0) + " km/h";
	if(speed_text_view.isNull())
	{
		speed_text_view = new GLUITextView();
		speed_text_view->create(*gl_ui, opengl_engine, msg, /*botleft=*/Vec2f(0.1f, 0.9f), /*dims=*/Vec2f(0.1f, 0.1f), /*tooltip=*/""); // Create off-screen
		speed_text_view->setColour(Colour3f(1.0f, 1.0f, 1.0f));
		speed_text_view->handler = this;
		gl_ui->addWidget(speed_text_view);
	}

	speed_text_view->setText(*gl_ui, msg);

	updateWidgetPositions();
}


void MiscInfoUI::showVehicleInfo(const std::string& msg)
{
	if(speed_text_view.isNull())
	{
		speed_text_view = new GLUITextView();
		speed_text_view->create(*gl_ui, opengl_engine, msg, /*botleft=*/Vec2f(0.1f, 0.9f), /*dims=*/Vec2f(0.1f, 0.1f), /*tooltip=*/""); // Create off-screen
		speed_text_view->setColour(Colour3f(1.0f, 1.0f, 1.0f));
		speed_text_view->handler = this;
		gl_ui->addWidget(speed_text_view);
	}

	speed_text_view->setText(*gl_ui, msg);

	updateWidgetPositions();
}



void MiscInfoUI::hideVehicleSpeed()
{
	// Destroy/remove speed_text_view
	if(speed_text_view.nonNull())
	{
		gl_ui->removeWidget(speed_text_view);
		speed_text_view->destroy();
		speed_text_view = NULL;
	}
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
			const float min_max_y = GLUI::getViewportMinMaxY(opengl_engine);

			const Vec2f tex_dims = admin_msg_text_view->getTextureDimensions();

			const float use_w = tex_dims.x / opengl_engine->getViewPortWidth();
			const float use_h = use_w * (tex_dims.y / tex_dims.x);

			const float vert_offset = 50.f / opengl_engine->getViewPortWidth(); // 50 pixels

			admin_msg_text_view->setPosAndDims(/*botleft=*/Vec2f(-0.4f, min_max_y - use_h - vert_offset), /*dims=*/Vec2f(use_w, use_h));
		}
		
		if(speed_text_view.nonNull())
		{
			const float min_max_y = GLUI::getViewportMinMaxY(opengl_engine);

			const Vec2f tex_dims = speed_text_view->getTextureDimensions();

			const float use_w = tex_dims.x / opengl_engine->getViewPortWidth();
			const float use_h = use_w * (tex_dims.y / tex_dims.x);

			const float vert_offset = 50.f / opengl_engine->getViewPortWidth(); // 50 pixels

			speed_text_view->setPosAndDims(/*botleft=*/Vec2f(-use_w/2, min_max_y - vert_offset * 2), /*dims=*/Vec2f(use_w, use_h)); // Position horizontally centred at top of screen.
		}
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
