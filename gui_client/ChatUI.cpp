/*=====================================================================
ChatUI.cpp
----------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "ChatUI.h"


#include "GUIClient.h"
#include "SettingsStore.h"
#include <graphics/SRGBUtils.h>
#include <opengl/MeshPrimitiveBuilding.h>
#include <utils/UTF8Utils.h>


ChatUI::ChatUI()
:	gui_client(NULL),
	expanded(true)
{}


ChatUI::~ChatUI()
{}


static const float widget_w = 0.6f;
static const float corner_radius_px = 8;
static const int font_size_px = 12;
static const int msgs_padding_w_px = 8;


void ChatUI::create(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_)
{
	opengl_engine = opengl_engine_;
	gui_client = gui_client_;
	gl_ui = gl_ui_;
	expanded = gui_client_->getSettingsStore()->getBoolValue("setting/show_chat", /*default_value=*/true);
	last_background_top_right_pos = Vec2f(0.f);

	try
	{
		// Create background quad to go behind text
		background_overlay_ob = new OverlayObject();
		background_overlay_ob->material.albedo_linear_rgb = Colour3f(0);
		background_overlay_ob->material.alpha = 0.8f;
		// Init with dummy data, will be updated in updateWidgetTransforms()
		background_overlay_ob->ob_to_world_matrix = Matrix4f::identity();
		background_overlay_ob->mesh_data = opengl_engine->getUnitQuadMeshData(); // Dummy
		this->last_viewport_dims = Vec2i(0); 
		opengl_engine->addOverlayObject(background_overlay_ob);


		GLUILineEdit::CreateArgs create_args;
		create_args.width = widget_w;
		create_args.background_colour = Colour3f(0.0f);
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


		{
			GLUIButton::CreateArgs args;
			args.tooltip = "Hide chat";
			//args.button_colour = Colour3f(0.2f);
			//args.mouseover_button_colour = Colour3f(0.4f);
			collapse_button = new GLUIButton(*gl_ui, opengl_engine, gui_client->resources_dir_path + "/buttons/left_tab.png", /*botleft=*/Vec2f(0), /*dims=*/Vec2f(0.1f), args);
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
		gl_ui->removeWidget(msg.name_text);
		gl_ui->removeWidget(msg.msg_text);
	}
	messages.clear();

	if(background_overlay_ob.nonNull())
		opengl_engine->removeOverlayObject(background_overlay_ob);
	background_overlay_ob = NULL;

	if(chat_line_edit.nonNull())
		gl_ui->removeWidget(chat_line_edit);
	chat_line_edit = NULL;

	if(collapse_button.nonNull())
		gl_ui->removeWidget(collapse_button);
	collapse_button = NULL;

	if(expand_button.nonNull())
		gl_ui->removeWidget(expand_button);
	expand_button = NULL;

	gl_ui = NULL;
	opengl_engine = NULL;
}


bool ChatUI::isInitialisedFully()
{
	return 
		gl_ui.nonNull() &&
		background_overlay_ob.nonNull() &&
		collapse_button.nonNull() &&
		expand_button.nonNull() &&
		chat_line_edit.nonNull();
}


void ChatUI::recreateTextViewsForMessage(ChatMessage& chatmessage)
{
	// Remove existing text views
	if(chatmessage.name_text.nonNull())
		gl_ui->removeWidget(chatmessage.name_text);
	chatmessage.name_text = NULL;

	if(chatmessage.msg_text.nonNull())
		gl_ui->removeWidget(chatmessage.msg_text);
	chatmessage.msg_text = NULL;


	const float text_area_w = widget_w - gl_ui->getUIWidthForDevIndepPixelWidth(msgs_padding_w_px) * 2;
	{
		GLUITextView::CreateArgs name_args;
		name_args.text_colour = chatmessage.avatar_colour;
		name_args.font_size_px = font_size_px;
		name_args.background_alpha = 0;
		name_args.max_width = text_area_w;
		chatmessage.name_text = new GLUITextView(*gl_ui, opengl_engine, UTF8Utils::sanitiseUTF8String(chatmessage.avatar_name), Vec2f(0.f), name_args);
		chatmessage.name_text->setVisible(this->expanded);
		gl_ui->addWidget(chatmessage.name_text);
	}
	{
		GLUITextView::CreateArgs msg_args;
		msg_args.font_size_px = font_size_px;
		msg_args.background_alpha = 0;
		msg_args.line_0_x_offset = chatmessage.name_text->getRect().getWidths().x;// + gl_ui->getUIWidthForDevIndepPixelWidth(font_size_px / 3.f);
		msg_args.max_width = text_area_w;
		chatmessage.msg_text = new GLUITextView(*gl_ui, opengl_engine, UTF8Utils::sanitiseUTF8String(chatmessage.msg), Vec2f(0.f), msg_args);
		chatmessage.msg_text->setVisible(this->expanded);
		gl_ui->addWidget(chatmessage.msg_text);
	}
}


void ChatUI::recreateMessageTextViews()
{
	for(auto it = messages.begin(); it != messages.end(); ++it)
		recreateTextViewsForMessage(*it);
}


void ChatUI::appendMessage(const std::string& avatar_name, const Colour3f& avatar_colour, const std::string& msg)
{
	if(!isInitialisedFully())
		return;

	// Add
	{
		ChatMessage chatmessage;
		chatmessage.avatar_name = avatar_name;
		chatmessage.avatar_colour = avatar_colour;
		chatmessage.msg = msg;

		recreateTextViewsForMessage(chatmessage);

		messages.push_back(chatmessage);
	}


	// Remove old messages if we exceed max num messages
	const size_t MAX_NUM_MESSAGES = 30;
	if(messages.size() > MAX_NUM_MESSAGES)
	{
		ChatMessage removed_msg = messages.front();
		messages.pop_front();

		gl_ui->removeWidget(removed_msg.name_text);
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


static const float button_spacing_px = 10;
static const float button_w_px = 20;


void ChatUI::handleMouseMoved(MouseEvent& mouse_event)
{
	if(!isInitialisedFully())
		return;

	const Vec2f coords = gl_ui->UICoordsForOpenGLCoords(mouse_event.gl_coords);

	if(expanded)
	{
		//const float msgs_background_ob_y = -gl_ui->getViewportMinMaxY() + gl_ui->getUIWidthForDevIndepPixelWidth(50);

		/*bool mouse_over = 
			coords.x <= -1.f + background_w + gl_ui->getUIWidthForDevIndepPixelWidth(60) &&
			coords.y <= -gl_ui->getViewportMinMaxY() + gl_ui->getUIWidthForDevIndepPixelWidth(50) + background_h;*/

		const float extra_mouse_over_px = 10;
		const float to_button_right_w = gl_ui->getUIWidthForDevIndepPixelWidth(button_spacing_px + button_w_px + extra_mouse_over_px);
		const bool mouse_over = 
			coords.x < (last_background_top_right_pos.x + to_button_right_w) && 
			coords.y < (last_background_top_right_pos.y + gl_ui->getUIWidthForDevIndepPixelWidth(extra_mouse_over_px));

		collapse_button->setVisible(mouse_over);
	}
}


void ChatUI::updateWidgetTransforms()
{
	if(!isInitialisedFully())
		return;

	//---------------------------- Update chat_line_edit ----------------------------

	const float chat_line_edit_y = -gl_ui->getViewportMinMaxY() + /*0.15f*/gl_ui->getUIWidthForDevIndepPixelWidth(20);
	chat_line_edit->setPos(/*botleft=*/Vec2f(-1.f + gl_ui->getUIWidthForDevIndepPixelWidth(20), chat_line_edit_y));


	//---------------------------- Update background_overlay_ob ----------------------------
	const float background_w = widget_w;
	const float background_h = 0.5f;

	const float y_scale = opengl_engine->getViewPortAspectRatio(); // scale from GL UI to opengl coords
	const float z = 0.1f;
	const float msgs_background_ob_y = chat_line_edit_y + gl_ui->getUIWidthForDevIndepPixelWidth(50);
	Vec2f background_pos(-1.f + gl_ui->getUIWidthForDevIndepPixelWidth(20), msgs_background_ob_y);
	//background_overlay_ob->ob_to_world_matrix = Matrix4f::translationMatrix(background_pos.x, background_pos.y * y_scale, z) * Matrix4f::scaleMatrix(background_w, background_h * y_scale, 1);
	background_overlay_ob->ob_to_world_matrix = Matrix4f::translationMatrix(background_pos.x, background_pos.y * y_scale, z) * Matrix4f::scaleMatrix(1, y_scale, 1);

	this->last_background_top_right_pos = background_pos + Vec2f(background_w, background_h);
	

	if(this->last_viewport_dims != opengl_engine->getViewportDims())
	{
		// Viewport has changed, recreate rounded-corner rect.
		conPrint("ChatUI: Viewport has changed, recreate rounded-corner rect.");
		background_overlay_ob->mesh_data = MeshPrimitiveBuilding::makeRoundedCornerRect(*opengl_engine->vert_buf_allocator, /*i=*/Vec4f(1,0,0,0), /*j=*/Vec4f(0,1,0,0), /*w=*/background_w, /*h=*/background_h, 
			/*corner radius=*/gl_ui->getUIWidthForDevIndepPixelWidth(corner_radius_px), /*tris_per_corner=*/8);

		//rect = Rect2f(botleft, botleft + Vec2f(args.width, background_h));
		this->last_viewport_dims = opengl_engine->getViewportDims();
	}


	//---------------------------- Update chat message text ----------------------------

	const float vert_spacing = gl_ui->getUIWidthForDevIndepPixelWidth(10);

	const float clip_padding_w = gl_ui->getUIWidthForDevIndepPixelWidth(msgs_padding_w_px);
	
	// Just use zero for bottom clip padding so as not to clip of descenders.
	const Rect2f clip_region(gl_ui->OpenGLCoordsForUICoords(background_pos) + Vec2f(clip_padding_w, 0.f), gl_ui->OpenGLCoordsForUICoords(background_pos + Vec2f(background_w, background_h) - Vec2f(clip_padding_w)));

	float y = msgs_background_ob_y + gl_ui->getUIWidthForDevIndepPixelWidth(msgs_padding_w_px);
	for(auto it = messages.rbegin(); it != messages.rend(); ++it)
	{
		ChatMessage& msg = *it;

		y += msg.msg_text->getRect().getWidths().y - gl_ui->getUIWidthForDevIndepPixelWidth(font_size_px); // Move start position up to leave room for multi-line text views

		const Vec2f line_0_botleft = Vec2f(-1.f + gl_ui->getUIWidthForDevIndepPixelWidth(20 + msgs_padding_w_px), y);

		msg.name_text->setPos(*gl_ui, /*botleft=*/line_0_botleft);
		msg.name_text->setClipRegion(clip_region);

		msg.msg_text->setPos(*gl_ui, /*botleft=*/line_0_botleft); // Vec2f(msg.name_text->getRect().getMax().x/* + gl_ui->getUIWidthForDevIndepPixelWidth(20)*/, y));
		msg.msg_text->setClipRegion(clip_region);

		const float max_msg_y = myMax(msg.name_text->getRect().getMax().y, msg.msg_text->getRect().getMax().y);

		y = max_msg_y + vert_spacing;
	}

	//---------------------------- Update collapse_button ----------------------------
	const float button_w = gl_ui->getUIWidthForDevIndepPixelWidth(button_w_px);
	const float button_h = gl_ui->getUIWidthForDevIndepPixelWidth(50);
	collapse_button->setPosAndDims(Vec2f(background_pos.x + background_w + gl_ui->getUIWidthForDevIndepPixelWidth(button_spacing_px), background_pos.y + background_h - button_h), Vec2f(button_w, button_h));

	//---------------------------- Update expand_button ----------------------------
	const float expand_button_w_px = 36;
	const float expand_button_w = gl_ui->getUIWidthForDevIndepPixelWidth(expand_button_w_px);
	expand_button->setPosAndDims(Vec2f(-1.f + gl_ui->getUIWidthForDevIndepPixelWidth(20), -gl_ui->getViewportMinMaxY() + gl_ui->getUIWidthForDevIndepPixelWidth(20 - 6) /*background_pos.y *//*+ background_h - button_h*/), 
		Vec2f(expand_button_w, expand_button_w));
}


void ChatUI::setWidgetVisibilityForExpanded()
{
	background_overlay_ob->draw = expanded;
	collapse_button->setVisible(expanded);
	expand_button->setVisible(!expanded);
	chat_line_edit->setVisible(expanded);

	for(auto it = messages.begin(); it != messages.end(); ++it)
	{
		ChatMessage& msg = *it;
		msg.name_text->setVisible(expanded);
		msg.msg_text->setVisible(expanded);
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
