/*=====================================================================
HiddenScriptUI.h
----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "../shared/UID.h"
#include "../shared/UserID.h"
#include <opengl/ui/GLUICallbackHandler.h>
#include <opengl/ui/GLUIGridContainer.h>
#include <opengl/ui/GLUIWindow.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/Reference.h>
#include <vector>


class LuaScriptUI;
class GLUI;


/*=====================================================================
HiddenScriptUI
--------------
A window listing the objects and users the user has hidden script UI from, on the current server, most recently hidden first,
with an Unhide button for each.

Unhide button presses are queued and done in think(), as unhiding re-runs scripts and rebuilds the list,
which shouldn't be done inside GLUI event dispatch.
=====================================================================*/
class HiddenScriptUI : public GLUICallbackHandler, public ThreadSafeRefCounted
{
public:
	HiddenScriptUI(LuaScriptUI* lua_script_ui, const Reference<GLUI>& gl_ui);
	~HiddenScriptUI();

	// Does any queued unhides, and rebuilds the list if needed.
	void think();

	void viewportResized();

	void listChanged(); // Called by LuaScriptUI when the hidden lists change.  The list is rebuilt in the next think() call.

	virtual void eventOccurred(GLUICallbackEvent& event) override; // From GLUICallbackHandler
	virtual void closeWindowEventOccurred(GLUICallbackEvent& event) override; // From GLUICallbackHandler

	bool close_soon; // Set when the close button is clicked.  LuaScriptUI should destroy this asap.

private:
	void rebuild();
	void updateWindowPos(); // Centres the window in the viewport.

	struct QueuedUnhide
	{
		bool is_creator; // If true, unhide creator_id, otherwise unhide ob_uid.
		UID ob_uid;
		UserID creator_id;
	};

	LuaScriptUI* lua_script_ui;
	Reference<GLUI> gl_ui;

	GLUIWindowRef window;
	GLUIGridContainerRef grid;

	std::vector<QueuedUnhide> queued_unhides;
	bool need_rebuild;
};
