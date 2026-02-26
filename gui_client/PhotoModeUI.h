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
#include <opengl/ui/GLUILineEdit.h>
#include <opengl/ui/GLUIImage.h>
#include <opengl/ui/GLUISlider.h>
#include <opengl/ui/GLUIInertWidget.h>
#include <opengl/ui/GLUIGridContainer.h>
#include <utils/ThreadManager.h>


class GUIClient;
class SettingsStore;


struct PhotoModeSlider
{
	void setVisible(bool visible);
	void setValue(double value, GLUIRef gl_ui);
	void setValueNoEvent(double value, GLUIRef gl_ui);

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

	void create(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_, const Reference<SettingsStore>& settings);
	void destroy();

	void setVisible(bool visible);
	bool isVisible() const;

	void enablePhotoModeUI();
	void disablePhotoModeUI();
	bool isPhotoModeEnabled();

	void autofocusDistSet(double dist);

	//void standardCameraModeSelected();

	void think();

	void viewportResized(int w, int h);

	virtual void eventOccurred(GLUICallbackEvent& event) override; // From GLUICallbackHandler
	virtual void sliderValueChangedEventOccurred(GLUISliderValueChangedEvent& event) override; // From GLUICallbackHandler

private:
	void untoggleAllCamModeButtons();
	void updateWidgetPositions();
	void makePhotoModeSlider(PhotoModeSlider& slider, const std::string& label, const std::string& tooltip, double min_val, double max_val, double initial_value, double scroll_speed, int& cell_y);
	void updateFocusDistValueString();
	void updateFocalLengthValueString();
	void resetControlsToPhotoModeDefaults();
	void resetControlsToNonPhotoModeDefaults();
	void showUploadPhotoWidget();
	void hideUploadPhotoWidget();
	void uploadPhoto();

	GUIClient* gui_client;

	GLUIGridContainerRef grid_container;

	GLUITextButtonRef standard_cam_button;
	GLUITextButtonRef selfie_cam_button;
	GLUITextButtonRef fixed_angle_cam_button;
	GLUITextButtonRef free_cam_button;
	GLUITextButtonRef tracking_cam_button;
	GLUITextButtonRef reset_button;

	GLUITextViewRef autofocus_label;
	GLUITextButtonRef autofocus_off_button;
	GLUITextButtonRef autofocus_eye_button;

	GLUIButtonRef take_screenshot_button;
	GLUITextButtonRef show_screenshots_button;
	GLUITextButtonRef upload_photo_button;
	GLUITextButtonRef hide_ui_button;

	// Upload image dialog
	GLUIInertWidgetRef upload_background_ob;
	GLUIImageRef upload_image_widget;
	GLUITextViewRef caption_label;
	GLUILineEditRef caption_line_edit;
	GLUITextButtonRef ok_button;
	GLUITextButtonRef cancel_button;


	PhotoModeSlider dof_blur_slider;
	PhotoModeSlider dof_focus_distance_slider;
	PhotoModeSlider ev_adjust_slider;
	PhotoModeSlider saturation_slider;
	PhotoModeSlider focal_length_slider;
	PhotoModeSlider roll_slider;

	GLUIRef gl_ui;

	Reference<OpenGLEngine> opengl_engine;

	Reference<SettingsStore> settings;
	double im_to_upload_aspect_ratio;

	std::string upload_image_jpeg_path;
	ThreadManager upload_thread_manager;

	std::string last_caption;
};
