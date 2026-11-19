/*=====================================================================
ScriptUIContextMenu.cpp
-----------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "ScriptUIContextMenu.h"


#include "GUIClient.h"
#include "LuaScriptUI.h"
#include "../shared/WorldStateLock.h"
#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUIContextMenu.h>
#include <utils/UTF8Utils.h>


ScriptUIContextMenu::ScriptUIContextMenu(GUIClient* gui_client_, LuaScriptUI* lua_script_ui_, const Reference<GLUI>& gl_ui_)
:	gui_client(gui_client_),
	lua_script_ui(lua_script_ui_),
	gl_ui(gl_ui_)
{
}


ScriptUIContextMenu::~ScriptUIContextMenu()
{
	close();
}


void ScriptUIContextMenu::open(const UID& ob_uid_, const UserID& creator_id_, const std::string& creator_name, const Vec2f& ui_coords)
{
	close();

	ob_uid = ob_uid_;
	creator_id = creator_id_;

	GLUIContextMenu::CreateArgs args;

	// Title, saying which object and user the UI is from.
	args.title = "UI from object " + ob_uid.toString();
	if(!creator_name.empty())
		args.title += " by " + creator_name;
	args.title = UTF8Utils::sanitiseUTF8String(args.title);

	item_actions.clear();

	args.items.push_back("Hide UI from this object");
	item_actions.push_back(Action_HideObject);

	if(creator_id.valid() && !creator_name.empty())
	{
		args.items.push_back(UTF8Utils::sanitiseUTF8String("Hide UI from all of " + creator_name + "'s objects"));
		item_actions.push_back(Action_HideCreator);
	}

	args.items.push_back("Select object");
	item_actions.push_back(Action_SelectObject);

	menu = new GLUIContextMenu(*gl_ui, args, ui_coords);
	menu->item_selected_func = [this](size_t item_index) { this->itemSelected(item_index); };
	gl_ui->addWidget(menu);
	gl_ui->setKeyboardFocusWidget(menu);
}


void ScriptUIContextMenu::close()
{
	checkRemoveAndDeleteWidget(gl_ui, menu);
}


// Called by the menu when an item is clicked, after the menu has closed.
void ScriptUIContextMenu::itemSelected(size_t item_index)
{
	if(item_index >= item_actions.size())
		return;

	const Action action = item_actions[item_index];
	if(action == Action_HideObject)
	{
		lua_script_ui->hideUIForObject(ob_uid);
	}
	else if(action == Action_HideCreator)
	{
		lua_script_ui->hideUIForCreator(creator_id);
	}
	else if(action == Action_SelectObject)
	{
		WorldObjectRef ob;
		if(gui_client->world_state)
		{
			WorldStateLock lock(gui_client->world_state->mutex);
			auto res = gui_client->world_state->objects.find(ob_uid);
			if(res != gui_client->world_state->objects.end())
				ob = res.getValue();
		}

		if(ob)
			gui_client->selectObject(ob, /*selected_mat_index=*/0);
	}
}
