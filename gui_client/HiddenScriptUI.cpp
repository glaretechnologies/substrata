/*=====================================================================
HiddenScriptUI.cpp
------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "HiddenScriptUI.h"


#include "LuaScriptUI.h"
#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUITextButton.h>
#include <opengl/ui/GLUITextView.h>
#include <utils/TimeStamp.h>
#include <utils/UTF8Utils.h>
#include <algorithm>


HiddenScriptUI::HiddenScriptUI(LuaScriptUI* lua_script_ui_, const Reference<GLUI>& gl_ui_)
:	close_soon(false),
	lua_script_ui(lua_script_ui_),
	gl_ui(gl_ui_),
	need_rebuild(false)
{
	// Grid with a row per hidden object or user: [description | time hidden | Unhide button]
	{
		GLUIGridContainer::CreateArgs args;
		args.background_alpha = 0.f;
		args.interior_cell_x_padding_px = 8;
		args.interior_cell_y_padding_px = 4;
		args.exterior_cell_x_padding_px = 12;
		args.exterior_cell_y_padding_px = 12;
		grid = new GLUIGridContainer(*gl_ui, args);
		grid->debug_name = "HiddenScriptUI grid";
	}

	// Window containing the grid
	{
		GLUIWindow::CreateArgs args;
		args.title = "Blocked On-screen Items";
		args.background_alpha = 0.9f;
		args.background_colour = Colour3f(0.1f);
		args.padding_px = 0;
		args.z = -0.2f;
		window = new GLUIWindow(*gl_ui, args);
		window->setBodyWidget(grid);
		window->handler = this;
		gl_ui->addWidget(window);
	}

	rebuild();
}


HiddenScriptUI::~HiddenScriptUI()
{
	checkRemoveAndDeleteWidget(gl_ui, window); // Also removes the grid and the widgets in it.
	grid = nullptr;
}


void HiddenScriptUI::think()
{
	for(size_t i=0; i<queued_unhides.size(); ++i)
	{
		if(queued_unhides[i].is_creator)
			lua_script_ui->unhideUIForCreator(queued_unhides[i].creator_id);
		else
			lua_script_ui->unhideUIForObject(queued_unhides[i].ob_uid);
	}
	queued_unhides.clear();

	if(need_rebuild)
		rebuild();
}


void HiddenScriptUI::viewportResized()
{
	updateWindowPos();
}


void HiddenScriptUI::listChanged()
{
	need_rebuild = true;
}


void HiddenScriptUI::eventOccurred(GLUICallbackEvent& /*event*/)
{
}


void HiddenScriptUI::closeWindowEventOccurred(GLUICallbackEvent& /*event*/)
{
	close_soon = true; // LuaScriptUI should destroy this asap.  (Can't destroy this now, as we are in an event handler from the window)
}


void HiddenScriptUI::rebuild()
{
	need_rebuild = false;

	grid->clear(); // Removes the widgets in the grid from the GL UI.

	// Make a row for each hidden object and user
	struct Row
	{
		TimeStamp hidden_time;
		std::string description;
		QueuedUnhide unhide;
	};
	std::vector<Row> rows;

	const std::map<UID, HiddenScriptUIEntry>& hidden_objects = lua_script_ui->getHiddenObjects();
	for(auto it = hidden_objects.begin(); it != hidden_objects.end(); ++it)
	{
		Row row;
		row.hidden_time = it->second.hidden_time;
		row.description = "Object " + it->first.toString();
		if(!it->second.creator_name.empty())
			row.description += " by " + it->second.creator_name;
		row.unhide.is_creator = false;
		row.unhide.ob_uid = it->first;
		rows.push_back(row);
	}

	const std::map<UserID, HiddenScriptUIEntry>& hidden_creators = lua_script_ui->getHiddenCreators();
	for(auto it = hidden_creators.begin(); it != hidden_creators.end(); ++it)
	{
		Row row;
		row.hidden_time = it->second.hidden_time;
		if(it->second.creator_name.empty())
			row.description = "All objects by user " + it->first.toString();
		else
			row.description = "All of " + it->second.creator_name + "'s objects";
		row.unhide.is_creator = true;
		row.unhide.creator_id = it->first;
		rows.push_back(row);
	}

	// Most recently hidden first, so that something hidden by mistake is at the top.
	std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.hidden_time.time > b.hidden_time.time; });

	GLUITextView::CreateArgs text_args;
	text_args.background_alpha = 0;

	for(size_t i=0; i<rows.size(); ++i)
	{
		const int y = (int)i;

		grid->setCellWidget(/*x=*/0, y, new GLUITextView(*gl_ui, UTF8Utils::sanitiseUTF8String(rows[i].description), text_args));

		grid->setCellWidget(/*x=*/1, y, new GLUITextView(*gl_ui, "blocked " + rows[i].hidden_time.timeAgoDescription(), text_args));

		GLUITextButton::CreateArgs button_args;
		GLUITextButtonRef unhide_button = new GLUITextButton(*gl_ui, "Unblock", button_args);
		const QueuedUnhide unhide = rows[i].unhide;
		unhide_button->on_pressed = [this, unhide](GLUICallbackEvent& event)
		{
			queued_unhides.push_back(unhide);
			event.accepted = true;
		};
		grid->setCellWidget(/*x=*/2, y, unhide_button);
	}

	if(rows.empty())
		grid->setCellWidget(/*x=*/0, /*y=*/0, new GLUITextView(*gl_ui, "No on-screen items are blocked.", text_args));

	updateWindowPos();
}


void HiddenScriptUI::updateWindowPos()
{
	window->recomputeLayout();

	// Centre the window in the viewport
	const float left_margin = myMax(0.f, (2.f - window->getDims().x) / 2);
	const float bot_margin  = myMax(0.f, (gl_ui->getViewportMinMaxY()*2 - window->getDims().y) / 2);

	window->setPos(Vec2f(-1.f + left_margin, -gl_ui->getViewportMinMaxY() + bot_margin));
}
