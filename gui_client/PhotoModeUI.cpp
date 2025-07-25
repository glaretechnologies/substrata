/*=====================================================================
PhotoModeUI.cpp
---------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "PhotoModeUI.h"


#include "GUIClient.h"


PhotoModeUI::PhotoModeUI()
:	gui_client(NULL)
{}


PhotoModeUI::~PhotoModeUI()
{}


void PhotoModeUI::create(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_)
{
	opengl_engine = opengl_engine_;
	gui_client = gui_client_;
	gl_ui = gl_ui_;

	{
		GLUITextButton::CreateArgs args;
		standard_cam_button = new GLUITextButton(*gl_ui_, opengl_engine_, "standard camera", Vec2f(0), args);
		standard_cam_button->handler = this;
		gl_ui->addWidget(standard_cam_button);

		standard_cam_button->setToggled(true);
	}
	{
		GLUITextButton::CreateArgs args;
		fixed_angle_cam_button = new GLUITextButton(*gl_ui_, opengl_engine_, "fixed angle camera", Vec2f(0), args);
		fixed_angle_cam_button->handler = this;
		gl_ui->addWidget(fixed_angle_cam_button);
	}
	{
		GLUITextButton::CreateArgs args;
		free_cam_button = new GLUITextButton(*gl_ui_, opengl_engine_, "free camera", Vec2f(0), args);
		free_cam_button->handler = this;
		gl_ui->addWidget(free_cam_button);
	}
	{
		GLUITextButton::CreateArgs args;
		tracking_cam_button = new GLUITextButton(*gl_ui_, opengl_engine_, "tracking camera", Vec2f(0), args);
		tracking_cam_button->handler = this;
		gl_ui->addWidget(tracking_cam_button);
	}

	updateWidgetPositions();
}


void PhotoModeUI::destroy()
{
	checkRemoveAndDeleteWidget(gl_ui, standard_cam_button);
	checkRemoveAndDeleteWidget(gl_ui, fixed_angle_cam_button);
	checkRemoveAndDeleteWidget(gl_ui, free_cam_button);
	checkRemoveAndDeleteWidget(gl_ui, tracking_cam_button);
	
	gl_ui = NULL;
	opengl_engine = NULL;
}


void PhotoModeUI::setVisible(bool visible)
{
	if(standard_cam_button) 
		standard_cam_button->setVisible(visible);

	if(fixed_angle_cam_button) 
		fixed_angle_cam_button->setVisible(visible);

	if(free_cam_button) 
		free_cam_button->setVisible(visible);
	
	if(tracking_cam_button) 
		tracking_cam_button->setVisible(visible);
}


bool PhotoModeUI::isVisible() const
{
	if(standard_cam_button) 
		return standard_cam_button->isVisible();
	else
		return false;
}


void PhotoModeUI::standardCameraModeSelected()
{
	untoggleAllCamModeButtons();
	standard_cam_button->setToggled(true);
}


void PhotoModeUI::think()
{
}


void PhotoModeUI::updateWidgetPositions()
{
	if(gl_ui)
	{
		//const float min_max_y = gl_ui->getViewportMinMaxY();
		const float margin = gl_ui->getUIWidthForDevIndepPixelWidth(18);

		float cur_y = 0.2f;
		if(standard_cam_button)
		{
			standard_cam_button ->setPos(/*botleft=*/Vec2f(1 - 0.3f, cur_y));
			cur_y -= standard_cam_button->rect.getWidths().y + margin;
		}

		if(fixed_angle_cam_button)
		{
			fixed_angle_cam_button ->setPos(/*botleft=*/Vec2f(1 - 0.3f, cur_y));
			cur_y -= fixed_angle_cam_button->rect.getWidths().y + margin;
		}
		
		if(tracking_cam_button)
		{
			tracking_cam_button ->setPos(/*botleft=*/Vec2f(1 - 0.3f, cur_y));
			cur_y -= tracking_cam_button->rect.getWidths().y + margin;
		}

		if(free_cam_button)
		{
			free_cam_button ->setPos(/*botleft=*/Vec2f(1 - 0.3f, cur_y));
			cur_y -= free_cam_button->rect.getWidths().y + margin;
		}
	}
}


void PhotoModeUI::viewportResized(int w, int h)
{
	if(gl_ui.nonNull())
	{
		if(standard_cam_button) standard_cam_button->rebuild();
		if(fixed_angle_cam_button) fixed_angle_cam_button->rebuild();
		if(free_cam_button) free_cam_button->rebuild();
		if(tracking_cam_button) tracking_cam_button->rebuild();

		updateWidgetPositions();
	}
}


void PhotoModeUI::untoggleAllCamModeButtons()
{
	if(standard_cam_button)			standard_cam_button->setToggled(false);
	if(fixed_angle_cam_button)		fixed_angle_cam_button->setToggled(false);
	if(free_cam_button)				free_cam_button->setToggled(false);
	if(tracking_cam_button)			tracking_cam_button->setToggled(false);
}


void PhotoModeUI::eventOccurred(GLUICallbackEvent& event)
{
	if(gui_client)
	{
		if(event.widget == standard_cam_button.ptr())
		{
			gui_client->cam_controller.standardCameraModeSelected();

			untoggleAllCamModeButtons();
			standard_cam_button->setToggled(true);

			event.accepted = true;
		}
		else if(event.widget == fixed_angle_cam_button.ptr())
		{
			gui_client->cam_controller.fixedAngleCameraModeSelected();

			untoggleAllCamModeButtons();
			fixed_angle_cam_button->setToggled(true);

			event.accepted = true;
		}
		else if(event.widget == free_cam_button.ptr())
		{
			gui_client->cam_controller.freeCameraModeSelected();

			untoggleAllCamModeButtons();
			free_cam_button->setToggled(true);

			event.accepted = true;
		}
		else if(event.widget == tracking_cam_button.ptr())
		{
			gui_client->cam_controller.trackingCameraModeSelected();

			untoggleAllCamModeButtons();
			tracking_cam_button->setToggled(true);

			event.accepted = true;
		}
	}
}
