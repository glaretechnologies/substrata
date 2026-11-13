/*=====================================================================
ScriptMessageUI.h
-----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUITextView.h>
#include <utils/RefCounted.h>
#include <utils/Reference.h>
#include <string>


/*=====================================================================
ScriptMessageUI
---------------
Shows messages from Lua scripts (showMessageToUser()) in the upper middle of the viewport.

One message is shown at a time: a new message replaces the current one.
Each message fades in while rising slightly, and fades out after a few seconds.
=====================================================================*/
class ScriptMessageUI : public RefCounted
{
public:
	ScriptMessageUI(const GLUIRef& gl_ui);
	~ScriptMessageUI();

	void showMessage(const std::string& message);

	// Updates the fade and position of the current message, and removes it when it has expired.  Call once per frame.
	void think();

private:
	void removeMessage();

	GLUIRef gl_ui;
	GLUITextViewRef text_view; // Null if no message is being shown.
	double creation_time;
};


typedef Reference<ScriptMessageUI> ScriptMessageUIRef;
