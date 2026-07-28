/*=====================================================================
BuilderAIUI.h
-------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUIButton.h>
#include <opengl/ui/GLUICallbackHandler.h>
#include <opengl/ui/GLUITextView.h>
#include <opengl/ui/GLUIImage.h>
#include <opengl/ui/GLUILineEdit.h>
#include <opengl/ui/GLUIGridContainer.h>
#include <opengl/ui/GLUIWindow.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/Reference.h>
#include <functional>
#include <list>
#include <string>


class GUIClient;


/*=====================================================================
BuilderAIUI
-----------
The "Builder AI" panel: a chat interface for asking the AI to build things in
the world, e.g. "build me a house here".

This is a separate conversation from the main world chat, and is shown in its
own panel on the right hand side of the screen.

The conversation is held on the server, over a dedicated connection that is
opened when this panel is shown, so this class just displays messages and
sends the user's text (see on_send_message).
=====================================================================*/
class BuilderAIUI : public GLUICallbackHandler, public ThreadSafeRefCounted
{
public:
	BuilderAIUI(GUIClient* gui_client_, GLUIRef gl_ui_);
	~BuilderAIUI();

	void setVisible(bool visible);
	bool isVisible() const { return visible; }

	// Called when the user submits some text.  Set by GUIClient, which sends it to the server.
	std::function<void(const std::string& text)> on_send_message;

	// Called when the user presses the stop button.
	std::function<void()> on_cancel;

	//---------------- Called as messages arrive from the server ----------------

	// Show a message the user just sent.
	void appendUserMessage(const std::string& text);

	// Append some streamed assistant text.  Continues the current assistant message, or starts a new one.
	void appendAssistantTextDelta(const std::string& text);

	// Show what the AI is currently doing, e.g. "create_cube".  Replaces the previous activity line.
	void setToolActivity(const std::string& tool_name);

	// The AI has finished responding.  Re-enables input.
	void turnComplete();

	void showError(const std::string& msg);

	//--------------------------------------------------------------------------

	void viewportResized(int w, int h);

	void handleMouseMoved(MouseEvent& mouse_event);

	virtual void eventOccurred(GLUICallbackEvent& event) override;

private:
	struct BuilderAIMessage
	{
		BuilderAIMessage() : from_user(false), is_error(false) {}
		bool from_user;
		bool is_error;
		std::string text;

		GLUITextViewRef msg_text;
	};

	bool isInitialisedFully();
	float computeWidgetWidth();

	void setWidgetVisibilityForExpanded();
	void updateWidgetTransforms();
	void recreateMessageTextViews();
	void recreateTextViewsForMessage(BuilderAIMessage& msg, int row_index);
	void appendMessage(bool from_user, bool is_error, const std::string& text);
	void setTurnInProgress(bool in_progress);

	std::list<BuilderAIMessage> messages;

	// The assistant's text is streamed to us a fragment at a time, so we append to the last message rather than
	// adding a new one for each fragment.
	bool last_message_is_streaming_assistant;

	bool turn_in_progress; // Is the AI currently responding?  While true the user can't send another message.

	bool expanded;
	bool visible;

	GLUIWindowRef window;
	GLUIGridContainerRef main_grid_container;
	GLUIGridContainerRef chat_grid_container;
	GLUITextViewRef status_text;   // Shows what the AI is currently doing.

	GLUIGridContainerRef line_edit_row_container;
	GLUILineEditRef msg_line_edit;
	GLUIButtonRef stop_button;
	//GLUIButtonRef collapse_button;
	GLUIButtonRef expand_button;

	GUIClient* gui_client;
	GLUIRef gl_ui;
};


typedef Reference<BuilderAIUI> BuilderAIUIRef;
