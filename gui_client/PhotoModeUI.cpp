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


void PhotoModeUI::makePhotoModeSlider(PhotoModeSlider& slider, const std::string& label, const std::string& tooltip, double min_val, double max_val, double initial_value, double scroll_speed)
{
	GLUITextView::CreateArgs text_view_args;
	text_view_args.background_alpha = 0;
	text_view_args.text_colour = Colour3f(0.9f);
	slider.label = new GLUITextView(*gl_ui, opengl_engine, label, Vec2f(0), text_view_args);

	GLUISlider::CreateArgs args;
	args.tooltip = tooltip;
	args.min_value = min_val;
	args.max_value = max_val;
	args.initial_value = initial_value;
	args.scroll_speed = scroll_speed;
	slider.slider = new GLUISlider(*gl_ui, opengl_engine, Vec2f(0), Vec2f(0.1f), args);
	slider.slider->handler = this;
	gl_ui->addWidget(slider.slider);

	slider.value_view = new GLUITextView(*gl_ui, opengl_engine, doubleToStringMaxNDecimalPlaces(args.initial_value, 2), Vec2f(0), text_view_args);
}


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
		background_overlay_ob = new OverlayObject();
		background_overlay_ob->mesh_data = opengl_engine->getUnitQuadMeshData();
		background_overlay_ob->material.albedo_linear_rgb = Colour3f(0.f);
		background_overlay_ob->material.alpha = 0.2f;

		background_overlay_ob->ob_to_world_matrix = Matrix4f::identity();

		opengl_engine->addOverlayObject(background_overlay_ob);
	}

	makePhotoModeSlider(dof_blur_slider, /*label=*/"Depth of field blur", /*tooltip=*/"Depth of field blur strength", 
		/*min val=*/0.0, /*max val=*/1.0, /*initial val=*/opengl_engine->getCurrentScene()->dof_blur_strength, /*scroll speed=*/1.0);

	makePhotoModeSlider(dof_focus_distance_slider, /*label=*/"Focus Distance", /*tooltip=*/"Focus Distance", 
		/*min val=*/0.001, /*max val=*/1.0, /*initial val=*/0.7, /*scroll speed=*/1.0);
	
	makePhotoModeSlider(ev_adjust_slider, /*label=*/"EV adjust", /*tooltip=*/"EV adjust", 
		/*min val=*/-8, /*max val=*/8, /*initial val=*/0, /*scroll speed=*/1.0);

	makePhotoModeSlider(focal_length_slider, /*label=*/"Focal length", /*tooltip=*/"Camera focal length", 
		/*min val=*/0.010, /*max val=*/1.0, /*initial val=*/0.025, /*scroll speed=*/0.05);

	makePhotoModeSlider(roll_slider, /*label=*/"Roll", /*tooltip=*/"Camera roll angle", 
		/*min val=*/-90, /*max val=*/90, /*initial val=*/0, /*scroll speed=*/1.0);

	updateWidgetPositions();
}

static void checkRemove(GLUIRef gl_ui, PhotoModeSlider& slider)
{
	checkRemoveAndDeleteWidget(gl_ui, slider.label);
	checkRemoveAndDeleteWidget(gl_ui, slider.slider);
	checkRemoveAndDeleteWidget(gl_ui, slider.value_view);
}

void PhotoModeUI::destroy()
{
	if(background_overlay_ob)
	{
		opengl_engine->removeOverlayObject(background_overlay_ob);
		background_overlay_ob = NULL;
	}

	checkRemoveAndDeleteWidget(gl_ui, standard_cam_button);
	checkRemoveAndDeleteWidget(gl_ui, fixed_angle_cam_button);
	checkRemoveAndDeleteWidget(gl_ui, free_cam_button);
	checkRemoveAndDeleteWidget(gl_ui, tracking_cam_button);
	checkRemove(gl_ui, dof_blur_slider);
	checkRemove(gl_ui, dof_focus_distance_slider);
	checkRemove(gl_ui, ev_adjust_slider);
	checkRemove(gl_ui, focal_length_slider);
	checkRemove(gl_ui, roll_slider);
	
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

		background_overlay_ob->draw = visible;

		dof_blur_slider.setVisible(visible);
		dof_focus_distance_slider.setVisible(visible);
		ev_adjust_slider.setVisible(visible);
		focal_length_slider.setVisible(visible);
		roll_slider.setVisible(visible);
	}
}


bool PhotoModeUI::isVisible() const
{
	if(standard_cam_button) 
		return standard_cam_button->isVisible();
	else
		return false;
}


void PhotoModeUI::enablePhotoModeUI()
{
	setVisible(true);
}


void PhotoModeUI::disablePhotoModeUI()
{
	setVisible(false);

	// Set camera params to the standard ones
	opengl_engine->getCurrentScene()->dof_blur_strength = 0.0f;
	opengl_engine->getCurrentScene()->dof_blur_focus_distance = 1.f;
	opengl_engine->getCurrentScene()->exposure_factor = 1.f;
	gui_client->cam_controller.lens_sensor_dist = 0.025f;

	gui_client->cam_controller.standardCameraModeSelected();

	// Remove roll
	Vec3d angles = gui_client->cam_controller.getAngles();
	angles.z = 0;
	gui_client->cam_controller.setAngles(angles);


	// Reset sliders
	dof_blur_slider.setValue(opengl_engine->getCurrentScene()->dof_blur_strength, gl_ui);
	dof_focus_distance_slider.setValue(0.7, gl_ui);
	ev_adjust_slider.setValue(0.0, gl_ui);
	focal_length_slider.setValue(0.025, gl_ui);
	roll_slider.setValue(0.0, gl_ui);
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


		updateSliderPosition(dof_blur_slider, margin, cur_y);
		updateSliderPosition(dof_focus_distance_slider, margin, cur_y);
		updateSliderPosition(ev_adjust_slider, margin, cur_y);
		updateSliderPosition(focal_length_slider, margin, cur_y);
		updateSliderPosition(roll_slider, margin, cur_y);

		// Set the transform of the transparent background quad behind sliders.
		const float z = 0.1f;
		const float y_scale = opengl_engine->getViewPortAspectRatio(); // scale from GL UI to opengl coords
		const float back_margin = gl_ui->getUIWidthForDevIndepPixelWidth(10);
		const float background_w = 1.f - roll_slider.label->rect.getMin().x + back_margin;
		const float background_h = dof_blur_slider.label->rect.getMax().y - roll_slider.label->rect.getMin().y + back_margin*2;
		background_overlay_ob->ob_to_world_matrix = Matrix4f::translationMatrix(roll_slider.label->rect.getMin().x -back_margin, (roll_slider.label->rect.getMin().y - back_margin) * y_scale, z) * 
			Matrix4f::scaleMatrix(background_w, background_h * y_scale, 1);
	}
}


void PhotoModeUI::updateSliderPosition(PhotoModeSlider& slider, float margin, float& cur_y)
{
	const float label_w  = gl_ui->getUIWidthForDevIndepPixelWidth(160);
	const float slider_w = gl_ui->getUIWidthForDevIndepPixelWidth(180);
	const float value_w  = gl_ui->getUIWidthForDevIndepPixelWidth(60);
	float x_margin = margin;

	float cur_x = 1.f - value_w - slider_w - label_w - x_margin * 3;

	float text_y_offset = gl_ui->getUIWidthForDevIndepPixelWidth(4); // To align with the slider
	slider.label->setPos(*gl_ui, Vec2f(cur_x, cur_y + text_y_offset));
	cur_x += label_w + x_margin;

	slider.slider->setPosAndDims(/*botleft=*/Vec2f(cur_x, cur_y), /*dims=*/Vec2f(slider_w, gl_ui->getUIWidthForDevIndepPixelWidth(21)));
	cur_x += slider_w + x_margin;

	slider.value_view->setPos(*gl_ui, Vec2f(cur_x, cur_y + text_y_offset));

	cur_y -= slider.slider->rect.getWidths().y + margin;
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
	if(standard_cam_button)
	{
		standard_cam_button->setToggled(false);
		fixed_angle_cam_button->setToggled(false);
		free_cam_button->setToggled(false);
		tracking_cam_button->setToggled(false);
	}
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
	if(event.widget == dof_blur_slider.slider.ptr())
	{
		dof_blur_slider.value_view->setText(*gl_ui, doubleToStringMaxNDecimalPlaces(event.value, 2)); // Update value text view

		opengl_engine->getCurrentScene()->dof_blur_strength = (float)event.value;
	}
	else if(event.widget == dof_focus_distance_slider.slider.ptr())
	{
		const float focus_dist = 1.f / (1.f - (float)event.value) - 1.f;

		dof_focus_distance_slider.value_view->setText(*gl_ui, doubleToStringMaxNDecimalPlaces(focus_dist, 2) + " m"); // Update value text view

		opengl_engine->getCurrentScene()->dof_blur_focus_distance = focus_dist;
	}
	else if(event.widget == ev_adjust_slider.slider.ptr())
	{
		ev_adjust_slider.value_view->setText(*gl_ui, doubleToStringMaxNDecimalPlaces(event.value, 2)); // Update value text view

		opengl_engine->getCurrentScene()->exposure_factor = (float)std::exp2(event.value);
	}
	else if(event.widget == focal_length_slider.slider.ptr())
	{
		focal_length_slider.value_view->setText(*gl_ui, doubleToStringMaxNDecimalPlaces(event.value * 1000, 0) + " mm"); // Update value text view

		gui_client->cam_controller.lens_sensor_dist = (float)event.value;
	}
	else if(event.widget == roll_slider.slider.ptr())
	{
		roll_slider.value_view->setText(*gl_ui, doubleToStringMaxNDecimalPlaces(event.value, 2)); // Update value text view

		Vec3d angles = gui_client->cam_controller.getAngles();
		angles.z = ::degreeToRad(event.value);
		gui_client->cam_controller.setAngles(angles);
	}
}


void PhotoModeSlider::setVisible(bool visible)
{
	label->setVisible(visible);
	slider->setVisible(visible);
	value_view->setVisible(visible);
}


void PhotoModeSlider::setValue(double value, GLUIRef gl_ui)
{
	slider->setValue(value);

	value_view->setText(*gl_ui, doubleToStringMaxNDecimalPlaces(value, 2)); // Update value text view
}
