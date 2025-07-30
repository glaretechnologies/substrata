/*=====================================================================
PhotoModeUI.h
-------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#pragma once


#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUIButton.h>
#include <opengl/ui/GLUITextButton.h>
#include <opengl/ui/GLUICallbackHandler.h>
#include <opengl/ui/GLUITextView.h>
#include <opengl/ui/GLUISlider.h>


class GUIClient;


struct PhotoModeSlider
{
	void setVisible(bool visible);
	void setValue(double value, GLUIRef gl_ui);

	GLUITextViewRef label;
	GLUISliderRef slider;
	GLUITextViewRef value_view;
};


/*=====================================================================
PhotoModeUI
-----------

=====================================================================*/
class PhotoModeUI : public GLUICallbackHandler
{
public:
	PhotoModeUI();
	~PhotoModeUI();

	void create(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_);
	void destroy();

	void setVisible(bool visible);
	bool isVisible() const;

	void enablePhotoModeUI();
	void disablePhotoModeUI();

	void standardCameraModeSelected();

	void think();

	void viewportResized(int w, int h);

	virtual void eventOccurred(GLUICallbackEvent& event) override; // From GLUICallbackHandler
	virtual void sliderValueChangedEventOccurred(GLUISliderValueChangedEvent& event) override; // From GLUICallbackHandler

private:
	void untoggleAllCamModeButtons();
	void updateWidgetPositions();
	void updateSliderPosition(PhotoModeSlider& slider, float margin, float& cur_y);
	void makePhotoModeSlider(PhotoModeSlider& slider, const std::string& label, const std::string& tooltip, double min_val, double max_val, double initial_value, double scroll_speed);
	void updateFocusDistValueString();
	void updateFocalLengthValueString();

	GUIClient* gui_client;

	GLUITextButtonRef standard_cam_button;
	GLUITextButtonRef fixed_angle_cam_button;
	GLUITextButtonRef free_cam_button;
	GLUITextButtonRef tracking_cam_button;

	OverlayObjectRef background_overlay_ob;

	PhotoModeSlider dof_blur_slider;
	PhotoModeSlider dof_focus_distance_slider;
	PhotoModeSlider ev_adjust_slider;
	PhotoModeSlider focal_length_slider;
	PhotoModeSlider roll_slider;

	GLUIRef gl_ui;

	Reference<OpenGLEngine> opengl_engine;
};
