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
	{
		GLUISlider::CreateArgs args;
		args.tooltip = "Depth of field blur strength";
		args.initial_value = opengl_engine->getCurrentScene()->dof_blur_strength;
		dof_blur_slider = new GLUISlider(*gl_ui_, opengl_engine_, Vec2f(0), Vec2f(0.1f), args);
		dof_blur_slider->handler = this;
		gl_ui->addWidget(dof_blur_slider);
	}
	{
		GLUISlider::CreateArgs args;
		args.tooltip = "focus distance";
		args.initial_value = opengl_engine->getCurrentScene()->dof_blur_focus_distance;
		dof_focus_distance_slider = new GLUISlider(*gl_ui_, opengl_engine_, Vec2f(0), Vec2f(0.1f), args);
		dof_focus_distance_slider->handler = this;
		gl_ui->addWidget(dof_focus_distance_slider);
	}
	{
		GLUISlider::CreateArgs args;
		args.tooltip = "EV adjust";
		args.min_value = -8;
		args.max_value = 8;
		args.initial_value = std::log2(opengl_engine->getCurrentScene()->exposure_factor);
		ev_adjust_slider = new GLUISlider(*gl_ui_, opengl_engine_, Vec2f(0), Vec2f(0.1f), args);
		ev_adjust_slider->handler = this;
		gl_ui->addWidget(ev_adjust_slider);
	}
	{
		GLUISlider::CreateArgs args;
		args.tooltip = "Camera focal length";
		args.min_value = 0.010; // 10 mm
		args.max_value = 1.0; // 1000 mm
		args.initial_value = opengl_engine_->getCurrentScene()->lens_sensor_dist;
		args.scroll_speed = 0.1;
		zoom_slider = new GLUISlider(*gl_ui_, opengl_engine_, Vec2f(0), Vec2f(0.1f), args);
		zoom_slider->handler = this;
		gl_ui->addWidget(zoom_slider);
	}
	{
		GLUISlider::CreateArgs args;
		args.tooltip = "Camera roll";
		args.min_value = -90;
		args.max_value = 90;
		args.initial_value = 0;
		roll_slider = new GLUISlider(*gl_ui_, opengl_engine_, Vec2f(0), Vec2f(0.1f), args);
		roll_slider->handler = this;
		gl_ui->addWidget(roll_slider);
	}

	updateWidgetPositions();
}


void PhotoModeUI::destroy()
{
	checkRemoveAndDeleteWidget(gl_ui, standard_cam_button);
	checkRemoveAndDeleteWidget(gl_ui, fixed_angle_cam_button);
	checkRemoveAndDeleteWidget(gl_ui, free_cam_button);
	checkRemoveAndDeleteWidget(gl_ui, tracking_cam_button);
	checkRemoveAndDeleteWidget(gl_ui, dof_blur_slider);
	checkRemoveAndDeleteWidget(gl_ui, dof_focus_distance_slider);
	checkRemoveAndDeleteWidget(gl_ui, ev_adjust_slider);
	checkRemoveAndDeleteWidget(gl_ui, zoom_slider);
	checkRemoveAndDeleteWidget(gl_ui, roll_slider);
	
	gl_ui = NULL;
	opengl_engine = NULL;
}


void PhotoModeUI::setVisible(bool visible)
{
	if(standard_cam_button)
	{
		standard_cam_button->setVisible(visible);
		fixed_angle_cam_button->setVisible(visible);
		free_cam_button->setVisible(visible);
		tracking_cam_button->setVisible(visible);
		dof_blur_slider->setVisible(visible);
		dof_focus_distance_slider->setVisible(visible);
		ev_adjust_slider->setVisible(visible);
		zoom_slider->setVisible(visible);
		roll_slider->setVisible(visible);
	}
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
	if(gl_ui && standard_cam_button)
	{
		//const float min_max_y = gl_ui->getViewportMinMaxY();
		const float margin = gl_ui->getUIWidthForDevIndepPixelWidth(18);

		float cur_y = 0.2f;
		standard_cam_button->setPos(/*botleft=*/Vec2f(1 - 0.3f, cur_y));
		cur_y -= standard_cam_button->rect.getWidths().y + margin;

		fixed_angle_cam_button->setPos(/*botleft=*/Vec2f(1 - 0.3f, cur_y));
		cur_y -= fixed_angle_cam_button->rect.getWidths().y + margin;
		
		tracking_cam_button->setPos(/*botleft=*/Vec2f(1 - 0.3f, cur_y));
		cur_y -= tracking_cam_button->rect.getWidths().y + margin;
		
		free_cam_button->setPos(/*botleft=*/Vec2f(1 - 0.3f, cur_y));
		cur_y -= free_cam_button->rect.getWidths().y + margin;
		
		dof_blur_slider->setPosAndDims(/*botleft=*/Vec2f(1 - 0.3f, cur_y), /*dims=*/Vec2f(gl_ui->getUIWidthForDevIndepPixelWidth(180), gl_ui->getUIWidthForDevIndepPixelWidth(21)));
		cur_y -= dof_blur_slider->rect.getWidths().y + margin;
		
		dof_focus_distance_slider->setPosAndDims(/*botleft=*/Vec2f(1 - 0.3f, cur_y), /*dims=*/Vec2f(gl_ui->getUIWidthForDevIndepPixelWidth(180), gl_ui->getUIWidthForDevIndepPixelWidth(21)));
		cur_y -= dof_focus_distance_slider->rect.getWidths().y + margin;
		
		ev_adjust_slider->setPosAndDims(/*botleft=*/Vec2f(1 - 0.3f, cur_y), /*dims=*/Vec2f(gl_ui->getUIWidthForDevIndepPixelWidth(180), gl_ui->getUIWidthForDevIndepPixelWidth(21)));
		cur_y -= ev_adjust_slider->rect.getWidths().y + margin;
		
		zoom_slider->setPosAndDims(/*botleft=*/Vec2f(1 - 0.3f, cur_y), /*dims=*/Vec2f(gl_ui->getUIWidthForDevIndepPixelWidth(180), gl_ui->getUIWidthForDevIndepPixelWidth(21)));
		cur_y -= zoom_slider->rect.getWidths().y + margin;
		
		roll_slider->setPosAndDims(/*botleft=*/Vec2f(1 - 0.3f, cur_y), /*dims=*/Vec2f(gl_ui->getUIWidthForDevIndepPixelWidth(180), gl_ui->getUIWidthForDevIndepPixelWidth(21)));
		cur_y -= roll_slider->rect.getWidths().y + margin;
	}
}


void PhotoModeUI::viewportResized(int w, int h)
{
	if(gl_ui && standard_cam_button)
	{
		standard_cam_button->rebuild();
		fixed_angle_cam_button->rebuild();
		free_cam_button->rebuild();
		tracking_cam_button->rebuild();

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


void PhotoModeUI::sliderValueChangedEventOccurred(GLUISliderValueChangedEvent& event)
{
	if(event.widget == dof_blur_slider.ptr())
	{
		opengl_engine->getCurrentScene()->dof_blur_strength = (float)event.value;
	}
	else if(event.widget == dof_focus_distance_slider.ptr())
	{
		opengl_engine->getCurrentScene()->dof_blur_focus_distance = 1.f / (1.f - (float)event.value) - 1.f;
	}
	else if(event.widget == ev_adjust_slider.ptr())
	{
		opengl_engine->getCurrentScene()->exposure_factor = (float)std::exp2(event.value);
	}
	else if(event.widget == zoom_slider.ptr())
	{
		printVar(event.value);
		//opengl_engine->getCurrentScene()->lens_sensor_dist = (float)(0.025 * event.value);
		gui_client->cam_controller.lens_sensor_dist = (float)event.value;
	}
	else if(event.widget == roll_slider.ptr())
	{
		printVar(event.value);
		Vec3d angles = gui_client->cam_controller.getAngles();
		angles.z = ::degreeToRad(event.value);
		printVar(angles);
		gui_client->cam_controller.setAngles(angles);

		Vec3d angles2 = gui_client->cam_controller.getAngles();
		printVar(angles2);
	}
}
