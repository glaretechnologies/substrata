/*=====================================================================
ChatUI.h
--------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUIButton.h>
#include <opengl/ui/GLUICallbackHandler.h>
#include <opengl/ui/GLUITextView.h>
#include <opengl/ui/GLUIImage.h>
#include <opengl/ui/GLUILineEdit.h>
#include "ClientThread.h"


class GUIClient;
class Avatar;


/*=====================================================================
ChatUI
------

=====================================================================*/
class ChatUI : public GLUICallbackHandler
{
public:
	ChatUI();
	~ChatUI();

	void create(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_);
	void destroy();

	void appendMessage(const std::string& avatar_name, const Colour3f& avatar_colour, const std::string& msg);

	void viewportResized(int w, int h);

	void handleMouseMoved(MouseEvent& mouse_event);

	virtual void eventOccurred(GLUICallbackEvent& event);
private:
	bool isInitialisedFully();

	struct ChatMessage
	{
		Colour3f avatar_colour;
		std::string avatar_name, msg;

		GLUITextViewRef name_text;
		GLUITextViewRef msg_text;
	};

	void setWidgetVisibilityForExpanded();
	void updateWidgetTransforms();
	void recreateMessageTextViews();
	void recreateTextViewsForMessage(ChatMessage& msg);


	std::list<ChatMessage> messages;

	bool expanded;
	OverlayObjectRef background_overlay_ob;
	GLUIButtonRef collapse_button;
	GLUIButtonRef expand_button;
	Vec2i last_viewport_dims;
	GUIClient* gui_client;
	GLUIRef gl_ui;
	Reference<OpenGLEngine> opengl_engine;
	
	Vec2f last_background_top_right_pos;

	GLUILineEditRef chat_line_edit;
};
