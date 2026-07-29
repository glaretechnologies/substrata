/*=====================================================================
BuilderAIUI.cpp
---------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "BuilderAIUI.h"


#include "GUIClient.h"
#include <graphics/SRGBUtils.h>
#include <utils/UTF8Utils.h>
#include <utils/ConPrint.h>


static const float corner_radius_px = 8;
static const int font_size_px = 12;
static const int msgs_padding_w_px = 8;

static const size_t MAX_NUM_MESSAGES = 16;

static const Colour3f panel_background_col(0.08f);
static const Colour3f user_text_col      = toLinearSRGB(Colour3f(0.9f, 0.9f, 0.9f));
static const Colour3f assistant_text_col = toLinearSRGB(Colour3f(0.7f, 0.85f, 1.0f));
static const Colour3f error_text_col     = toLinearSRGB(Colour3f(1.0f, 0.5f, 0.4f));


BuilderAIUI::BuilderAIUI(GUIClient* gui_client_, GLUIRef gl_ui_)
:	gui_client(gui_client_),
	gl_ui(gl_ui_),
	last_message_is_streaming_assistant(false),
	turn_in_progress(false),
	expanded(false),
	visible(true) // Hidden until the user opens the panel.
{
	try
	{
		/*
		---------------------------------------|  window
		|             Builder AI              X|
		|--------------------------------------|
		||------------------------------------||  main_grid_container
		||------------------------------------||  
		||| -------------                    |||
		||||Build a cube |                   |||
		||| -------------                    |||
		||| -----------------                |||  chat_grid_container
		||||Ok I built a cube |              |||
		||| __________________               |||
		|||----------------------------------|||
		|| Status text                        ||
		||------------------------------------||
		|| Line edit                     |Stop|| line_edit_row_container
		||------------------------------------||
		---------------------------------------
		*/

		// Create window
		{
			GLUIWindow::CreateArgs args;
			args.title = "Builder AI";
			args.background_colour = Colour3f(0.1f);
			args.background_alpha = 0.9f;
			args.z = -0.2f;
			window = new GLUIWindow(*gl_ui_, args);
			window->debug_name = "builder AI window";
			//window->setFixedDimsUICoords(Vec2f(0.5f, gl_ui->getViewportMinMaxY() * 1.6f));
		
			window->handler = this;
			//window->on_contained_widget_changed_size = [this](){ this->windowChangedSize(); };
			window->on_close_window = [this](){ 
				this->expanded = false;
				this->setWidgetVisibilityForExpanded(); 
			};
			gl_ui->addWidget(window);
		}

		{
			GLUIGridContainer::CreateArgs args;
			args.interior_cell_x_padding_px = 5;
			args.interior_cell_y_padding_px = 5;
			args.background_alpha = 0;
			//args.background_colour = Colour3f(0.1f, 0.5f, 0.1f);
			//args.background_alpha = 0.9f;
			main_grid_container = new GLUIGridContainer(*gl_ui_, args);
			main_grid_container->debug_name = "builder AI main_grid_container";

			window->setBodyWidget(main_grid_container);
		}

		{
			GLUIGridContainer::CreateArgs container_args;
			//container_args.background_colour = panel_background_col;
			//container_args.background_alpha = 0.4f;
			container_args.background_alpha = 0.0f;
			container_args.interior_cell_x_padding_px = 4;
			container_args.interior_cell_y_padding_px = 4;
			container_args.exterior_cell_x_padding_px = 4;
			container_args.exterior_cell_y_padding_px = 4;
			//container_args.background_colour = Colour3f(0.4f, 0.1f, 0.1f);
			//container_args.background_alpha = 0.9f;
			chat_grid_container = new GLUIGridContainer(*gl_ui, container_args);
			chat_grid_container->debug_name = "builder AI chat_grid_container";

			main_grid_container->addWidgetOnNewRow(chat_grid_container);
		}

		

		{
			GLUITextView::CreateArgs args;
			args.font_size_px = font_size_px;
			args.text_colour = Colour3f(0.6f);
			args.background_alpha = 0.0f;
			status_text = new GLUITextView(*gl_ui, "", Vec2f(0.f), args);
			status_text->debug_name = "status_text";
			
			main_grid_container->addWidgetOnNewRow(status_text);
		}

		{
			GLUIGridContainer::CreateArgs args;
			args.interior_cell_x_padding_px = 5;
			args.interior_cell_y_padding_px = 5;
			args.background_alpha = 0;
			line_edit_row_container = new GLUIGridContainer(*gl_ui_, args);
			line_edit_row_container->debug_name = "line_edit_row_container";
			line_edit_row_container->setColumnMinXPx(/*column index=*/1, 200);

			main_grid_container->addWidgetOnNewRow(line_edit_row_container);
		}

		{
			GLUILineEdit::CreateArgs create_args;
			//create_args.sizing_type_x = GLUILineEdit::SizingType_Expanding;
			create_args.sizing_type_x = GLUILineEdit::SizingType_FixedSizePx;
			create_args.fixed_size.x = 500;
			create_args.background_colour = panel_background_col;
			create_args.background_alpha = 0.8f;
			create_args.font_size_px = font_size_px;
			msg_line_edit = new GLUILineEdit(*gl_ui, /*dummy botleft=*/Vec2f(0.f), create_args);
			msg_line_edit->debug_name = "msg_line_edit";

			GLUILineEdit* line_edit_ptr = msg_line_edit.ptr();
			msg_line_edit->on_enter_pressed = [this, line_edit_ptr]()
				{
					if(this->turn_in_progress) // Don't allow sending another message while the AI is still working.
						return;

					const std::string text = line_edit_ptr->getText();
					if(!text.empty())
					{
						line_edit_ptr->clear();
						this->appendUserMessage(text);
						this->setTurnInProgress(true);
						if(this->on_send_message)
							this->on_send_message(text);
					}
				};

			line_edit_row_container->setCellWidget(0, 0, msg_line_edit);
		}

		{
			GLUIButton::CreateArgs args;
			args.sizing_type_x = GLUIWidget::SizingType_FixedSizePx;
			args.sizing_type_y = GLUIWidget::SizingType_FixedSizePx;
			args.fixed_size = Vec2f(30.f);
			args.tooltip = "Stop";
			stop_button = new GLUIButton(*gl_ui, gui_client->resources_dir_path + /*"/buttons/delete.png"*/"/buttons/white_x.png", args);
			stop_button->debug_name = "stop_button";
			stop_button->handler = this;

			line_edit_row_container->setCellWidget(1, 0, stop_button); // Position to right of line edit
		}

		{
			GLUIButton::CreateArgs args;
			args.tooltip = "Show Builder AI";
			expand_button = new GLUIButton(*gl_ui, gui_client->resources_dir_path + "/buttons/builder_ai.png", args);
			expand_button->handler = this;
			gl_ui->addWidget(expand_button);
		}


		setWidgetVisibilityForExpanded();
		updateWidgetTransforms();
	}
	catch(glare::Exception& e)
	{
		assert(0);
		conPrint("Warning: Excep while creating BuilderAIUI: " + e.what());
	}
}


BuilderAIUI::~BuilderAIUI()
{
	if(gl_ui.isNull())
		return; // Never fully created, or already torn down.

	for(auto it = messages.begin(); it != messages.end(); ++it)
		gl_ui->removeWidget(it->msg_text);
	messages.clear();

	checkRemoveAndDeleteWidget(gl_ui, window);
	checkRemoveAndDeleteWidget(gl_ui, expand_button);

	gl_ui = NULL;
}


void BuilderAIUI::setVisible(bool visible_)
{
	visible = visible_;

	if(!isInitialisedFully())
		return;

	setWidgetVisibilityForExpanded();
}


bool BuilderAIUI::isInitialisedFully()
{
	return
		gl_ui.nonNull() &&
		window.nonNull() &&
		main_grid_container.nonNull() &&
		chat_grid_container.nonNull() &&
		status_text.nonNull() &&
		line_edit_row_container.nonNull() &&
		msg_line_edit.nonNull() &&
		stop_button.nonNull() &&
		expand_button.nonNull();
}


//===================== Messages =====================


void BuilderAIUI::recreateTextViewsForMessage(BuilderAIMessage& msg, int row_index)
{
	if(msg.msg_text)
		gl_ui->removeWidget(msg.msg_text);
	msg.msg_text = NULL;

	const float text_area_max_width = gl_ui->getUIWidthForDevIndepPixelWidth(500.f);

	GLUITextView::CreateArgs msg_args;
	msg_args.font_size_px = font_size_px;
	msg_args.padding_px = 9;
	msg_args.background_alpha = 0.3f;
	msg_args.background_corner_radius_px = corner_radius_px;
	msg_args.max_width = text_area_max_width;
	msg_args.text_colour = msg.is_error ? error_text_col : (msg.from_user ? user_text_col : assistant_text_col);

	msg.msg_text = new GLUITextView(*gl_ui, UTF8Utils::sanitiseUTF8String(msg.text), Vec2f(0.f), msg_args);
	msg.msg_text->setVisible(this->expanded && visible);
	gl_ui->addWidget(msg.msg_text);

	chat_grid_container->setCellWidget(0, row_index, msg.msg_text);
}


void BuilderAIUI::recreateMessageTextViews()
{
	int i = 0;
	for(auto it = messages.begin(); it != messages.end(); ++it)
		recreateTextViewsForMessage(*it, /*row index=*/i++);
}


void BuilderAIUI::appendMessage(bool from_user, bool is_error, const std::string& text)
{
	if(!isInitialisedFully())
		return;

	{
		BuilderAIMessage msg;
		msg.from_user = from_user;
		msg.is_error = is_error;
		msg.text = text;
		messages.push_back(msg);
	}

	// Add a new row if we are not at MAX_NUM_MESSAGES rows yet, else scroll the existing rows up.
	if(chat_grid_container->cell_widgets.getHeight() < MAX_NUM_MESSAGES)
		chat_grid_container->cell_widgets.resize(1, chat_grid_container->cell_widgets.getHeight() + 1);
	else
		for(int y = 0; y + 1 < (int)chat_grid_container->cell_widgets.getHeight(); ++y)
			chat_grid_container->cell_widgets.elem(0, y) = chat_grid_container->cell_widgets.elem(0, y+1);

	recreateTextViewsForMessage(messages.back(), /*row index=*/(int)chat_grid_container->cell_widgets.getHeight() - 1);

	if(messages.size() > MAX_NUM_MESSAGES)
	{
		BuilderAIMessage removed_msg = messages.front();
		messages.pop_front();
		gl_ui->removeWidget(removed_msg.msg_text);
	}

	updateWidgetTransforms();
}


void BuilderAIUI::appendUserMessage(const std::string& text)
{
	last_message_is_streaming_assistant = false;
	appendMessage(/*from_user=*/true, /*is_error=*/false, text);
}


void BuilderAIUI::appendAssistantTextDelta(const std::string& text)
{
	if(!isInitialisedFully())
		return;

	if(last_message_is_streaming_assistant && !messages.empty())
	{
		// Continue the current assistant message.
		BuilderAIMessage& msg = messages.back();
		msg.text += text;
		msg.msg_text->setText(UTF8Utils::sanitiseUTF8String(msg.text));
		updateWidgetTransforms();
	}
	else
	{
		appendMessage(/*from_user=*/false, /*is_error=*/false, text);
		last_message_is_streaming_assistant = true;
	}
}


void BuilderAIUI::setToolActivity(const std::string& tool_name)
{
	if(!isInitialisedFully())
		return;

	// A new tool call means any streamed text before it is now a finished message.
	last_message_is_streaming_assistant = false;

	status_text->setText("Building: " + tool_name + "…");
	status_text->setVisible(this->expanded && visible);
	updateWidgetTransforms();
}


void BuilderAIUI::turnComplete()
{
	last_message_is_streaming_assistant = false;
	setTurnInProgress(false);
}


void BuilderAIUI::showError(const std::string& msg)
{
	last_message_is_streaming_assistant = false;
	appendMessage(/*from_user=*/false, /*is_error=*/true, msg);
	setTurnInProgress(false);
}


void BuilderAIUI::setTurnInProgress(bool in_progress)
{
	turn_in_progress = in_progress;

	if(!isInitialisedFully())
		return;

	stop_button->setVisible(in_progress && this->expanded && visible);
	if(!in_progress)
	{
		status_text->setText("");
		status_text->setVisible(false);
	}
	updateWidgetTransforms();
}


//===================== Layout =====================


void BuilderAIUI::viewportResized(int w, int h)
{
	if(!isInitialisedFully())
		return;

	recreateMessageTextViews();
	updateWidgetTransforms();
}


void BuilderAIUI::handleMouseMoved(MouseEvent& mouse_event)
{
}


void BuilderAIUI::updateWidgetTransforms()
{
	if(!isInitialisedFully())
		return;

	// Anchor the panel to the right hand side of the screen.
	const float side_margin = gl_ui->getUIWidthForDevIndepPixelWidth(30);
	const float top_margin  = gl_ui->getUIWidthForDevIndepPixelWidth(30);

	window->recomputeLayout();

	const float width = window->getDims().x;
	const float height = window->getDims().y;

	window->setPos(/*botleft=*/Vec2f(1 - side_margin - width, gl_ui->getViewportMinMaxY() - top_margin - height));

	//---------------------------- expand_button ----------------------------
	const float expand_button_w = gl_ui->getUIWidthForDevIndepPixelWidth(40);
	expand_button->setPosAndDims(Vec2f(1.f - expand_button_w - gl_ui->getUIWidthForDevIndepPixelWidth(10), -expand_button_w/2),
		Vec2f(expand_button_w));
}


void BuilderAIUI::setWidgetVisibilityForExpanded()
{
	const bool show = expanded && visible;

	window->setVisible(show);
	stop_button->setVisible(show && turn_in_progress);
	status_text->setVisible(show && turn_in_progress);

	expand_button->setVisible(!expanded && visible);
}


void BuilderAIUI::eventOccurred(GLUICallbackEvent& event)
{
	if(!isInitialisedFully())
		return;

	if(event.widget == this->expand_button.ptr())
	{
		expanded = true;
		setWidgetVisibilityForExpanded();
	}
	else if(event.widget == this->stop_button.ptr())
	{
		if(on_cancel)
			on_cancel();
	}
}
