/*=====================================================================
ScriptUIContextMenu.h
---------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "../shared/UID.h"
#include "../shared/UserID.h"
#include <maths/vec2.h>
#include <utils/RefCounted.h>
#include <utils/Reference.h>
#include <string>
#include <vector>


class GUIClient;
class LuaScriptUI;
class GLUI;
class GLUIContextMenu;


/*=====================================================================
ScriptUIContextMenu
-------------------
The menu shown when the user right-clicks on a widget created by a Lua script.
Shows which object and user the widget is from, and lets the user hide UI from the object, or from all objects created by the user,
select the object, or open the list of hidden UI.

Drawn by the client, so scripts can't create, remove or imitate it.
=====================================================================*/
class ScriptUIContextMenu : public RefCounted
{
public:
	ScriptUIContextMenu(GUIClient* gui_client, LuaScriptUI* lua_script_ui, const Reference<GLUI>& gl_ui);
	~ScriptUIContextMenu();

	// Opens the menu for a widget from the given object, with the menu's top-left corner at ui_coords.  Closes any menu that is already open.
	// creator_id may be invalid and creator_name empty if the object creator is not known, in which case the hide-creator item is not shown.
	void open(const UID& ob_uid, const UserID& creator_id, const std::string& creator_name, const Vec2f& ui_coords);
	void close();

private:
	enum Action
	{
		Action_HideObject,
		Action_HideCreator,
		Action_SelectObject,
		Action_ListHiddenUI
	};

	void itemSelected(size_t item_index);

	GUIClient* gui_client;
	LuaScriptUI* lua_script_ui;
	Reference<GLUI> gl_ui;

	Reference<GLUIContextMenu> menu;
	std::vector<Action> item_actions; // Action for each menu item.

	UID ob_uid; // The object the menu was opened for.
	UserID creator_id;
	std::string creator_name;
};
