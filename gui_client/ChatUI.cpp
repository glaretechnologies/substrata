/*=====================================================================
ChatUI.cpp
----------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "ChatUI.h"


#include "GUIClient.h"
#include <settings/SettingsStore.h>
#include <graphics/SRGBUtils.h>
#include <opengl/MeshPrimitiveBuilding.h>
#include <utils/UTF8Utils.h>


ChatUI::ChatUI()
:	gui_client(NULL),
	expanded(true),
	visible(true)
{}


ChatUI::~ChatUI()
{}


static const float corner_radius_px = 8;
static const int font_size_px = 12;
static const int msgs_padding_w_px = 8;


void ChatUI::create(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_)
{
	opengl_engine = opengl_engine_;
	gui_client = gui_client_;
	gl_ui = gl_ui_;
#if EMSCRIPTEN
	const bool default_chat_expanded = false; // On mobile screens, chat can cover most of the viewport, so make collapsed by default.
#else
	const bool default_chat_expanded = true;
#endif
	expanded = gui_client_->getSettingsStore()->getBoolValue("setting/show_chat", /*default_value=*/default_chat_expanded);
	visible = true;

	draw_area_bottom_left_y = -1.f;

	try
	{
		Colour3f chat_background_col(0.1f);

		{
			GLUIGridContainer::CreateArgs container_args;
			container_args.background_colour = chat_background_col;
			container_args.background_alpha = 0.0f;
			container_args.cell_padding_px = 12;
			grid_container = new GLUIGridContainer(*gl_ui, opengl_engine, container_args);
			grid_container->setPosAndDims(Vec2f(0.0f, 0.0f), Vec2f(gl_ui->getUIWidthForDevIndepPixelWidth(300), gl_ui->getUIWidthForDevIndepPixelWidth(200)));
			gl_ui->addWidget(grid_container);
		}


		{
			GLUILineEdit::CreateArgs create_args;
			create_args.width = computeWidgetWidth();
			create_args.background_colour = chat_background_col;
			create_args.background_alpha = 0.8f;
			create_args.font_size_px = font_size_px;
			chat_line_edit = new GLUILineEdit(*gl_ui, opengl_engine, /*dummy botleft=*/Vec2f(0.f), create_args);

			GLUILineEdit* chat_line_edit_ptr = chat_line_edit.ptr();
			chat_line_edit->on_enter_pressed = [chat_line_edit_ptr, gui_client_]()
				{
					if(!chat_line_edit_ptr->getText().empty())
					{
						gui_client_->sendChatMessage(chat_line_edit_ptr->getText());

						chat_line_edit_ptr->clear(); 
					}
				};

			gl_ui->addWidget(chat_line_edit);
		}


		{
			GLUIButton::CreateArgs args;
			args.tooltip = "Hide chat";
			//args.button_colour = Colour3f(0.2f);
			//args.mouseover_button_colour = Colour3f(0.4f);
			//collapse_button = new GLUIButton(*gl_ui, opengl_engine, gui_client->resources_dir_path + "/buttons/left_tab.png", /*botleft=*/Vec2f(0), /*dims=*/Vec2f(0.1f), args);
			collapse_button = new GLUIButton(*gl_ui, opengl_engine, gui_client->resources_dir_path + "/buttons/down_tab.png", /*botleft=*/Vec2f(0), /*dims=*/Vec2f(0.1f), args);
			collapse_button->handler = this;
			gl_ui->addWidget(collapse_button);
		}

		{
			GLUIButton::CreateArgs args;
			args.tooltip = "Show chat";
			expand_button = new GLUIButton(*gl_ui, opengl_engine, gui_client->resources_dir_path + "/buttons/expand_chat_icon.png", /*botleft=*/Vec2f(0), /*dims=*/Vec2f(0.1f), args);
			expand_button->handler = this;
			expand_button->setVisible(false);
			gl_ui->addWidget(expand_button);
		}

		// TEMP: add some messages
		//for(int i=0; i<20; ++i)
		//	appendMessage("Nick: ", Colour3f(1.f, 0.8f, 0.2f), "Testing 123, new GLUIButton(*gl_ui, opengl_engine, gui_client->resources_dir_path");

		setWidgetVisibilityForExpanded();
		updateWidgetTransforms();
	}
	catch(glare::Exception& e)
	{
		conPrint("Warning: Excep while creating ChatUI: " + e.what());
	}
}


void ChatUI::destroy()
{
	// Remove + destroy message UI objects
	for(auto it = messages.begin(); it != messages.end(); ++it)
	{
		ChatMessage& msg = *it;
		//gl_ui->removeWidget(msg.name_text);
		gl_ui->removeWidget(msg.msg_text);
	}
	messages.clear();

	checkRemoveAndDeleteWidget(gl_ui, grid_container);
	checkRemoveAndDeleteWidget(gl_ui, chat_line_edit);
	checkRemoveAndDeleteWidget(gl_ui, collapse_button);
	checkRemoveAndDeleteWidget(gl_ui, expand_button);

	gl_ui = NULL;
	opengl_engine = NULL;
}


void ChatUI::setVisible(bool visible_)
{
	visible = visible_;

	if(!isInitialisedFully())
		return;

	for(auto it = messages.begin(); it != messages.end(); ++it)
	{
		//it->name_text->setVisible(visible);
		it->msg_text->setVisible(visible);
	}

	grid_container->setVisible(visible && expanded);
	chat_line_edit->setVisible(visible && expanded);
	collapse_button->setVisible(visible && expanded);
	expand_button->setVisible(visible && !expanded);
}


bool ChatUI::isInitialisedFully()
{
	return 
		gl_ui.nonNull() &&
		grid_container.nonNull() &&
		collapse_button.nonNull() &&
		expand_button.nonNull() &&
		chat_line_edit.nonNull();
}


float ChatUI::computeWidgetWidth()
{
	// Upper bound so that there is always enough room to show the minimise button to the right of the chat window.
	return myClamp(gl_ui->getUIWidthForDevIndepPixelWidth(500.f), /*lower bound=*/0.4f, /*upper bound=*/1.6f);
}


float ChatUI::computeWidgetHeight()
{
	return myMin(gl_ui->getUIWidthForDevIndepPixelWidth(700.f), /*upper bound=*/2 * gl_ui->getViewportMinMaxY() - gl_ui->getUIWidthForDevIndepPixelWidth(200.f));
}


void ChatUI::recreateTextViewsForMessage(ChatMessage& chatmessage, int row_index)
{
	// Remove existing text views
	//if(chatmessage.name_text)
	//	gl_ui->removeWidget(chatmessage.name_text);
	//chatmessage.name_text = NULL;

	if(chatmessage.msg_text)
		gl_ui->removeWidget(chatmessage.msg_text);
	chatmessage.msg_text = NULL;


	const float text_area_w = computeWidgetWidth() - gl_ui->getUIWidthForDevIndepPixelWidth(msgs_padding_w_px) * 2;
	//{
	//	GLUITextView::CreateArgs name_args;
	//	name_args.text_colour = chatmessage.avatar_colour;
	//	name_args.font_size_px = font_size_px;
	//	name_args.background_alpha = 0;
	//	name_args.max_width = text_area_w;
	//	chatmessage.name_text = new GLUITextView(*gl_ui, opengl_engine, UTF8Utils::sanitiseUTF8String(chatmessage.avatar_name), Vec2f(0.f), name_args);
	//	chatmessage.name_text->setVisible(this->expanded && visible);
	//	gl_ui->addWidget(chatmessage.name_text);
	//
	//	grid_container->setCellWidget(0, row_index, chatmessage.name_text);
	//}
	{
		GLUITextView::CreateArgs msg_args;
		msg_args.font_size_px = font_size_px;
		msg_args.padding_px = 9;
		msg_args.background_alpha = 0.3f;
		msg_args.background_corner_radius_px = corner_radius_px;
		//msg_args.line_0_x_offset = chatmessage.name_text->getRect().getWidths().x;// + gl_ui->getUIWidthForDevIndepPixelWidth(font_size_px / 3.f);
		msg_args.max_width = text_area_w;
		chatmessage.msg_text = new GLUITextView(*gl_ui, opengl_engine, UTF8Utils::sanitiseUTF8String(chatmessage.avatar_name + chatmessage.msg), Vec2f(0.f), msg_args);
		chatmessage.msg_text->setVisible(this->expanded && visible);
		gl_ui->addWidget(chatmessage.msg_text);

		grid_container->setCellWidget(0, row_index, chatmessage.msg_text);
	}
}


/*
msg index   row index
0           4
1           3
2           2
3           1
4           0

messages.size() == 5
*/


void ChatUI::recreateMessageTextViews()
{
	int i = 0;
	for(auto it = messages.begin(); it != messages.end(); ++it)
	{
		recreateTextViewsForMessage(*it, /*row index=*/(int)messages.size() - 1 - i);
		i++;
	}
}


void ChatUI::appendMessage(const std::string& avatar_name, const Colour3f& avatar_colour, const std::string& msg)
{
	if(!isInitialisedFully())
		return;

	const size_t MAX_NUM_MESSAGES = 30;

	// Add
	{
		{
			ChatMessage chatmessage;
			chatmessage.avatar_name = avatar_name;
			chatmessage.avatar_colour = avatar_colour;
			chatmessage.msg = msg;

			//recreateTextViewsForMessage(chatmessage);

			messages.push_back(chatmessage);
		}

		// Add a new row if we are not at MAX_NUM_MESSAGES rows yet.
		grid_container->cell_widgets.resize(1, myMin(MAX_NUM_MESSAGES, grid_container->cell_widgets.getHeight() + 1));

		// Move all messages up a row in the grid container
		for(int y = (int)grid_container->cell_widgets.getHeight() - 1; y >= 1; y--)
			grid_container->cell_widgets.elem(0, y) = grid_container->cell_widgets.elem(0, y-1);

		recreateTextViewsForMessage(messages.back(), /*row index=*/0);
	}


	// Remove old messages if we exceed max num messages
	if(messages.size() > MAX_NUM_MESSAGES)
	{
		ChatMessage removed_msg = messages.front();
		messages.pop_front();

		//gl_ui->removeWidget(removed_msg.name_text);
		gl_ui->removeWidget(removed_msg.msg_text);
	}


	updateWidgetTransforms();
}


void ChatUI::viewportResized(int w, int h)
{
	if(!isInitialisedFully())
		return;

	recreateMessageTextViews();
	updateWidgetTransforms();
}

void ChatUI::setDrawAreaBottomLeftY(float draw_area_bottom_left_y_)
{
	draw_area_bottom_left_y = draw_area_bottom_left_y_;
}


static const float button_spacing_px = 10;
static const float button_w_px = 20;


void ChatUI::handleMouseMoved(MouseEvent& mouse_event)
{
	if(!isInitialisedFully())
		return;

	const Vec2f coords = gl_ui->UICoordsForOpenGLCoords(mouse_event.gl_coords);

	// Show collapse button if mouse is over the (visible) chat widget
	if(expanded)
	{
		//const float msgs_background_ob_y = -gl_ui->getViewportMinMaxY() + gl_ui->getUIWidthForDevIndepPixelWidth(50);

		/*bool mouse_over = 
			coords.x <= -1.f + background_w + gl_ui->getUIWidthForDevIndepPixelWidth(60) &&
			coords.y <= -gl_ui->getViewportMinMaxY() + gl_ui->getUIWidthForDevIndepPixelWidth(50) + background_h;*/

		const Vec2f last_background_top_right_pos = grid_container->getRect().getMax();

		const float extra_mouse_over_px = 10;
		/*const float to_button_right_w = gl_ui->getUIWidthForDevIndepPixelWidth(button_spacing_px + button_w_px + extra_mouse_over_px);
		const bool mouse_over = 
			coords.x < (last_background_top_right_pos.x + to_button_right_w) && 
			coords.y < (last_background_top_right_pos.y + gl_ui->getUIWidthForDevIndepPixelWidth(extra_mouse_over_px));*/
		const float to_button_top_h = gl_ui->getUIWidthForDevIndepPixelWidth(button_spacing_px + button_w_px + extra_mouse_over_px);
		const bool mouse_over = 
			coords.x < (last_background_top_right_pos.x + gl_ui->getUIWidthForDevIndepPixelWidth(extra_mouse_over_px)) && 
			coords.y < (last_background_top_right_pos.y + to_button_top_h);

		collapse_button->setVisible(mouse_over && visible);
	}
}


void ChatUI::updateWidgetTransforms()
{
	if(!isInitialisedFully())
		return;

	const float widget_width  = computeWidgetWidth();
	const float widget_height = computeWidgetHeight();

	//---------------------------- Update chat_line_edit ----------------------------
	const float chat_widget_width = myMax(0.2f, 1.f - gl_ui->getUIWidthForDevIndepPixelWidth(140.f)); // leave room for icons   //myClamp(gl_ui->getUIWidthForDevIndepPixelWidth(600.f), /*lower bound=*/0.4f, /*upper bound=*/1.6f);
	chat_line_edit->setWidth(chat_widget_width);
	float chat_line_edit_y = /*-gl_ui->getViewportMinMaxY()*/draw_area_bottom_left_y + /*0.15f*/gl_ui->getUIWidthForDevIndepPixelWidth(20);

	//if(-1.f + widget_width >= -0.4f) // gl_ui->getUIWidthForDevIndepPixelWidth(800) > 2.f)
	//	chat_line_edit_y += myMax(gl_ui->getUIWidthForDevIndepPixelWidth(100), 0.15f) + gl_ui->getUIWidthForDevIndepPixelWidth(20); // Move above movement button in MiscInfoUI.

	chat_line_edit->setPos(/*botleft=*/Vec2f(-1.f + gl_ui->getUIWidthForDevIndepPixelWidth(20), chat_line_edit_y));


	//---------------------------- Update background_overlay_ob ----------------------------
	const float background_w = widget_width;
	const float background_h = widget_height;

	const float msgs_background_ob_y = chat_line_edit_y + gl_ui->getUIWidthForDevIndepPixelWidth(50);
	const Vec2f background_pos(-1.f + gl_ui->getUIWidthForDevIndepPixelWidth(20), msgs_background_ob_y);
	
	this->grid_container->setPosAndDims(background_pos, Vec2f(background_w, background_h));
	this->grid_container->updateGLTransform();
	
	//---------------------------- Update collapse_button ----------------------------
	const float button_w = gl_ui->getUIWidthForDevIndepPixelWidth(50);
	const float button_h = gl_ui->getUIWidthForDevIndepPixelWidth(button_w_px);
	const float text_top_y = grid_container->getClippedContentRect().getMax().y;
	collapse_button->setPosAndDims(Vec2f(background_pos.x, text_top_y + gl_ui->getUIWidthForDevIndepPixelWidth(button_spacing_px + 8)), Vec2f(button_w, button_h));

	//---------------------------- Update expand_button ----------------------------
	const float expand_button_w_px = 36;
	const float expand_button_w = gl_ui->getUIWidthForDevIndepPixelWidth(expand_button_w_px);
	expand_button->setPosAndDims(Vec2f(-1.f + gl_ui->getUIWidthForDevIndepPixelWidth(20), -gl_ui->getViewportMinMaxY() + gl_ui->getUIWidthForDevIndepPixelWidth(20 - 6) /*background_pos.y *//*+ background_h - button_h*/), 
		Vec2f(expand_button_w, expand_button_w));
}


void ChatUI::setWidgetVisibilityForExpanded()
{
	grid_container->setVisible(expanded && visible);
	collapse_button->setVisible(expanded && visible);
	expand_button->setVisible(!expanded && visible);
	chat_line_edit->setVisible(expanded && visible);

	for(auto it = messages.begin(); it != messages.end(); ++it)
	{
		ChatMessage& msg = *it;
		//msg.name_text->setVisible(expanded && visible);
		msg.msg_text->setVisible(expanded && visible);
	}
}


void ChatUI::eventOccurred(GLUICallbackEvent& event)
{
	if(!isInitialisedFully())
		return;

	if(event.widget == this->collapse_button.ptr())
	{
		assert(expanded);
		expanded = false;
	}
	else if(event.widget == this->expand_button.ptr())
	{
		assert(!expanded);
		expanded = true;
	}

	setWidgetVisibilityForExpanded();

	gui_client->getSettingsStore()->setBoolValue("setting/show_chat", expanded);
}
