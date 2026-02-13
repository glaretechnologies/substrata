/*=====================================================================
GestureManagerUI.cpp
--------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "GestureManagerUI.h"


#include "GUIClient.h"
#include "../shared/ResourceManager.h"
#include <utils/FileUtils.h>
#include <utils/FileInStream.h>
#include <utils/PlatformUtils.h>


GestureManagerUI::GestureManagerUI(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_, const GestureSettings& gesture_settings_)
{
	opengl_engine = opengl_engine_;
	gui_client = gui_client_;
	gl_ui = gl_ui_;
	gesture_settings = gesture_settings_;
	need_rebuild_grid = false;

	// Create grid
	{
		GLUIGridContainer::CreateArgs container_args;
		container_args.background_colour = Colour3f(0.1f);
		container_args.background_alpha = 0.8f;
		container_args.cell_padding_px = 4;
		grid_container = new GLUIGridContainer(*gl_ui, opengl_engine, container_args);
		grid_container->setPosAndDims(Vec2f(0.0f, 0.0f), Vec2f(gl_ui->getUIWidthForDevIndepPixelWidth(300), gl_ui->getUIWidthForDevIndepPixelWidth(200)));
		gl_ui->addWidget(grid_container);
	}

	rebuildGridForGestureSettings();
}


GestureManagerUI::~GestureManagerUI()
{
	checkRemoveAndDeleteWidget(gl_ui, grid_container);

	for(size_t i=0; i<gestures.size(); ++i)
	{
		checkRemoveAndDeleteWidget(gl_ui, gestures[i].gesture_image);
		checkRemoveAndDeleteWidget(gl_ui, gestures[i].name_text);
		checkRemoveAndDeleteWidget(gl_ui, gestures[i].animate_head_checkbox);
		checkRemoveAndDeleteWidget(gl_ui, gestures[i].loop_checkbox);
		checkRemoveAndDeleteWidget(gl_ui, gestures[i].remove_gesture_button);
	}

	checkRemoveAndDeleteWidget(gl_ui, add_gesture_button);
}


void GestureManagerUI::rebuildGridForGestureSettings()
{
	grid_container->clear();

	// Remove all grid interior widgets
	for(size_t i=0; i<gestures.size(); ++i)
	{
		checkRemoveAndDeleteWidget(gl_ui, gestures[i].gesture_image);
		checkRemoveAndDeleteWidget(gl_ui, gestures[i].name_text);
		checkRemoveAndDeleteWidget(gl_ui, gestures[i].animate_head_checkbox);
		checkRemoveAndDeleteWidget(gl_ui, gestures[i].loop_checkbox);
		checkRemoveAndDeleteWidget(gl_ui, gestures[i].remove_gesture_button);
	}
	gestures.clear();

	checkRemoveAndDeleteWidget(gl_ui, add_gesture_button);



	// Recreate grid interior widgets
	for(size_t i=0; i<gesture_settings.gesture_settings.size(); ++i)
	{
		const SingleGestureSettings& setting = gesture_settings.gesture_settings[i];

		int cell_x = 0;

		// If this isn't one of the default gestures, fall back to to using "Waving 1" button texture for now.
		const std::string tex_name = GestureSettings::isDefaultGestureName(setting.friendly_name) ? setting.friendly_name : "Waving 1";

		GLUIImageRef gesture_image = new GLUIImage(*gl_ui, opengl_engine, /*tex path=*/gui_client->resources_dir_path + "/buttons/" + tex_name + ".png", Vec2f(0.f), Vec2f(0.05f), "");
		gesture_image->immutable_dims = true;

		grid_container->setCellWidget(/*x=*/cell_x++, /*y=*/(int)i, gesture_image);

		
		GLUITextViewRef gesture_name_text;
		{
			GLUITextView::CreateArgs text_args;
			text_args.background_alpha = 0;
			text_args.tooltip = toStdString(setting.anim_URL);
			gesture_name_text = new GLUITextView(*gl_ui, opengl_engine, setting.friendly_name, Vec2f(0.f), text_args);
			grid_container->setCellWidget(/*x=*/cell_x++, /*y=*/(int)i, gesture_name_text);
		}

		const Colour3f checkbox_box_col(0.2f);
		const Colour3f checkbox_mouseover_col(0.3f);

		GLUICheckBoxRef animate_head_checkbox;
		{
			GLUICheckBox::CreateArgs args;
			args.tooltip = "Gesture animation controls head";
			args.box_colour = checkbox_box_col;
			args.mouseover_box_colour = checkbox_mouseover_col;
			args.checked = BitUtils::isBitSet(setting.flags, SingleGestureSettings::FLAG_ANIMATE_HEAD);
			animate_head_checkbox = new GLUICheckBox(*gl_ui, opengl_engine, gui_client->base_dir_path + "/data/gl_data/ui/tick.png", Vec2f(0), Vec2f(0.1f), args);
			animate_head_checkbox->immutable_dims = true;
			animate_head_checkbox->handler = this;
			grid_container->setCellWidget(/*x=*/cell_x++, /*y=*/(int)i, animate_head_checkbox);
		}
		
		GLUICheckBoxRef loop_checkbox;
		{
			GLUICheckBox::CreateArgs args;
			args.tooltip = "Loop gesture animation";
			args.box_colour = checkbox_box_col;
			args.mouseover_box_colour = checkbox_mouseover_col;
			args.checked = BitUtils::isBitSet(setting.flags, SingleGestureSettings::FLAG_LOOP);
			loop_checkbox = new GLUICheckBox(*gl_ui, opengl_engine, gui_client->base_dir_path + "/data/gl_data/ui/tick.png", Vec2f(0), Vec2f(0.1f), args);
			loop_checkbox->immutable_dims = true;
			loop_checkbox->handler = this;
			grid_container->setCellWidget(/*x=*/cell_x++, /*y=*/(int)i, loop_checkbox);
		}

		GLUIButtonRef remove_gesture_button;
		{
			GLUIButton::CreateArgs args;
			args.tooltip = "Remove gesture";
			remove_gesture_button = new GLUIButton(*gl_ui, opengl_engine, gui_client->resources_dir_path + "/buttons/white_x.png", Vec2f(0), Vec2f(0.1f, 0.1f), args);
			remove_gesture_button->immutable_dims = true;
			remove_gesture_button->handler = this;
			grid_container->setCellWidget(/*x=*/cell_x++, /*y=*/(int)i, remove_gesture_button);
		}

		PerGestureUI ui;
		ui.gesture_image = gesture_image;
		ui.name_text = gesture_name_text;
		ui.remove_gesture_button = remove_gesture_button;
		ui.animate_head_checkbox = animate_head_checkbox;
		ui.loop_checkbox = loop_checkbox;
		gestures.push_back(ui);
	}

	{
		GLUIButton::CreateArgs args;
		args.tooltip = "Add a new gesture";
		add_gesture_button = new GLUIButton(*gl_ui, opengl_engine, gui_client->resources_dir_path + "/buttons/plus.png", Vec2f(0), Vec2f(0.1f, 0.1f), args);
		add_gesture_button->immutable_dims = true;
		add_gesture_button->handler = this;
		gl_ui->addWidget(add_gesture_button);

		grid_container->setCellWidget(/*x=*/0, /*y=*/(int)gesture_settings.gesture_settings.size(), add_gesture_button);
	}

	updateWidgetPositions();
}


void GestureManagerUI::think()
{
	if(need_rebuild_grid)
	{
		rebuildGridForGestureSettings();

		need_rebuild_grid = false;
	}
}


static const float BUTTON_W_PIXELS = 30;

void GestureManagerUI::updateWidgetPositions()
{
	if(gl_ui)
	{
		const float margin = gl_ui->getUIWidthForDevIndepPixelWidth(100);

		for(size_t i=0; i<gestures.size(); ++i)
		{
			gestures[i].gesture_image        ->setDims(Vec2f(gl_ui->getUIWidthForDevIndepPixelWidth(BUTTON_W_PIXELS)));
			gestures[i].remove_gesture_button->setDims(Vec2f(gl_ui->getUIWidthForDevIndepPixelWidth(22)));
			gestures[i].animate_head_checkbox->setDims(Vec2f(gl_ui->getUIWidthForDevIndepPixelWidth(22)));
			gestures[i].loop_checkbox        ->setDims(Vec2f(gl_ui->getUIWidthForDevIndepPixelWidth(22)));
		}

		add_gesture_button->setDims(Vec2f(gl_ui->getUIWidthForDevIndepPixelWidth(BUTTON_W_PIXELS)));


		grid_container->setPosAndDims(Vec2f(-1.f + margin, -gl_ui->getViewportMinMaxY() + margin), Vec2f(myMax(0.01f, 2.f - 2*margin), myMax(0.01f, 2*gl_ui->getViewportMinMaxY() - 2*margin)));


		grid_container->updateGLTransform();
	}
}


void GestureManagerUI::viewportResized(int w, int h)
{
	updateWidgetPositions();
}


void GestureManagerUI::eventOccurred(GLUICallbackEvent& event)
{
	if(gui_client)
	{
		GLUIButton* button = dynamic_cast<GLUIButton*>(event.widget);
		if(button)
		{
			if(button == add_gesture_button.ptr())
			{
				conPrint("Add gesture button");
				event.accepted = true;

				std::vector<UIInterface::FileTypeFilter> filters(1);
				filters[0].description = "Animation";
				filters[0].file_types.push_back("subanim");
				filters[0].file_types.push_back("glb");

				const std::string selected_path = gui_client->ui_interface->showOpenFileDialog("Select an animation", filters, "GestureManager/add_anim");
				if(!selected_path.empty())
				{
					try
					{
						std::string anim_name;
						URLString anim_URL;
						float anim_len = 1.f;

						if(hasExtension(selected_path, "subanim"))
						{
							// Load the animation file, get the animation name from it.
							{
								FileInStream file(selected_path);

								// Read magic number
								char buf[4];
								file.readData(buf, 4); 
								if(buf[0] != 'S' || buf[1] != 'U' || buf[2] != 'B' || buf[3] != 'A')
									throw glare::Exception("Invalid magic number/string loading '" + toString(selected_path) + "'");

								Reference<AnimationData> anim = new AnimationData();
								anim->readFromStream(file);

								// Checks num anims = 1 (for prepareForMultipleUse())
								if(anim->animations.size() != 1)
									throw glare::Exception(".subanim file must have exactly one animation in it.");
						
								anim_name = anim->animations[0]->name;
								anim_len  = anim->animations[0]->anim_len;
							}


							// Copy into resources, get URL (which will have the file hash in it)
							anim_URL = gui_client->resource_manager->copyLocalFileToResourceDirAndReturnURL(selected_path);
						}
						else if(hasExtension(selected_path, "glb"))
						{
							// Take the animation name from the GLB filename.  Don't use the actual name of the animation as read from the GLB contents, because it is "mixamo.com" for mixamo anims.
							anim_name = ::removeDotAndExtension(FileUtils::getFilename(selected_path));

							const std::string temp_subanim_path = PlatformUtils::getTempDirPath() + "/" + anim_name + ".subanim";
							anim_len = AvatarGraphics::processAndConvertGLBAnimToSubanim(/*glb_path=*/selected_path, anim_name, /*output_subanim_path=*/temp_subanim_path);

							// Copy into resources, get URL (which will have the file hash in it)
							anim_URL = gui_client->resource_manager->copyLocalFileToResourceDirAndReturnURL(temp_subanim_path);
						}
						else
							throw glare::Exception("invalid extension");

						assert(!anim_name.empty());
						assert(!anim_URL.empty());

						if(anim_name.size() > SingleGestureSettings::MAX_NAME_SIZE)
							throw glare::Exception("name too long.");

						if(anim_URL.size() > SingleGestureSettings::MAX_NAME_SIZE)
							throw glare::Exception("anim_URL too long.");

						if(gesture_settings.gesture_settings.size() == GestureSettings::MAX_GESTURE_SETTINGS_SIZE)
							throw glare::Exception("gesture_settings too large.");

						// Append new gesture to gesture_settings.
						SingleGestureSettings setting;
						setting.friendly_name = anim_name;
						setting.anim_URL = anim_URL;
						setting.anim_duration = anim_len;

						gesture_settings.gesture_settings.push_back(setting);


						gui_client->gestureSettingsChanged(gesture_settings);

						need_rebuild_grid = true; // Rebuild grid UI later when not handling clicks for it.
					}
					catch(glare::Exception& e)
					{
						gui_client->showErrorNotification(e.what());
					}
				}
			}

			// See if it's a remove gesture button
			for(size_t i=0; i<gestures.size(); ++i)
			{
				if(gestures[i].remove_gesture_button == button)
				{
					conPrint("Removing gesture " + toString(i));

					gesture_settings.gesture_settings.erase(gesture_settings.gesture_settings.begin() + i);

					gui_client->gestureSettingsChanged(gesture_settings);

					need_rebuild_grid = true; // Rebuild grid UI later when not handling clicks for it.

					event.accepted = true;
					break;
				}
			}
		}


		for(size_t i=0; i<gestures.size(); ++i)
		{
			if(gestures[i].animate_head_checkbox.ptr() == event.widget)
			{
				BitUtils::setOrZeroBit(gesture_settings.gesture_settings[i].flags, SingleGestureSettings::FLAG_ANIMATE_HEAD, gestures[i].animate_head_checkbox->isChecked());

				gui_client->gestureSettingsChanged(gesture_settings);

				event.accepted = true;
				break;
			}

			if(gestures[i].loop_checkbox.ptr() == event.widget)
			{
				BitUtils::setOrZeroBit(gesture_settings.gesture_settings[i].flags, SingleGestureSettings::FLAG_LOOP, gestures[i].loop_checkbox->isChecked());

				gui_client->gestureSettingsChanged(gesture_settings);

				event.accepted = true;
				break;
			}
		}
	}
}
