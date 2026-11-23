/*=====================================================================
LuaScriptUIFunctions.h
----------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <lua/LuaVM.h>
struct lua_State;


/*=====================================================================
LuaScriptUIFunctions
--------------------
Lua bindings for script UI widgets: createUIImage(), createUIButton(), createUISlider(), createUISpinBox(), createUICheckBox(), createUIDropDown(), createUILineEdit(), createUILabel(), createUIGrid(), createUIWindow(),
and the metatable for the widget handles they return.

Widgets only exist on the client (see gui_client/LuaScriptUI).  On the server the functions return a handle with id 0, and operations
on it do nothing, so that a script shared between client and server doesn't need to check IS_CLIENT around UI code.

On the client, widgets can only be created once the user has interacted with the script, for example by using or touching the script's object
(see SubstrataLuaScript::isUIActivated()).  Before that, the functions raise a Lua error, which can be caught with pcall.  So scripts create their widgets in a handler like onUserUsedObject.
Scripts in their creator's personal world don't need activating.

Example script, with a window containing a grid with one of each widget type, shown when the user uses the object.  Replace the image URL with the URL of an image resource.

--lua
-- @resource https://example.com/logo.png

local window = nil -- Created the first time the user uses the object.

local function createSettingsWindow()
	window = createUIWindow({ title = "Settings", anchor = "top-left", offset = Vec2(20, -20),
		onClose = function() print("Window closed") end }) -- The close button hides the window.  Show it again with window.visible = true.

	local grid = createUIGrid({ interior_padding = 5, edge_padding = 0 })
	window:setBody(grid)

	local title = createUILabel({ text = "Options", text_colour = Vec3f(1, 0.9, 0.2), font_size = 18 })
	local logo = createUIImage({ image_url = "https://example.com/logo.png", size = Vec2(64, 64) })
	grid:setCell(1, 1, title)
	grid:setCell(2, 1, logo)

	local volume_label = createUILabel({ text = "Volume: 0.5" })
	local volume_slider = createUISlider({ min = 0, max = 1, value = 0.5, width = 150,
		onChange = function(value) volume_label.text = string.format("Volume: %.2f", value) end })
	grid:setCell(1, 2, volume_label)
	grid:setCell(2, 2, volume_slider)

	local lights_label = createUILabel({ text = "Lights on" })
	local lights_checkbox = createUICheckBox({ checked = true,
		onChange = function(checked) lights_label.text = checked and "Lights on" or "Lights off" end })
	grid:setCell(1, 3, lights_label)
	grid:setCell(2, 3, lights_checkbox)

	local quality_label = createUILabel({ text = "Quality: Medium" })
	local quality_dropdown = createUIDropDown({ options = {"Low", "Medium", "High"}, value = "Medium", width = 120,
		onChange = function(value) quality_label.text = "Quality: " .. value end })
	grid:setCell(1, 4, quality_label)
	grid:setCell(2, 4, quality_dropdown)

	local name_label = createUILabel({ text = "Name:" })
	local name_edit = createUILineEdit({ text = "", width = 150,
		onEnter = function(text) print("Hello, " .. text) end })
	grid:setCell(1, 5, name_label)
	grid:setCell(2, 5, name_edit)

	local count_label = createUILabel({ text = "Count:" })
	local count_spinbox = createUISpinBox({ min = 1, max = 10, value = 3, step = 1, decimals = 0, width = 80,
		onChange = function(value) print("Count is now " .. value) end })
	grid:setCell(1, 6, count_label)
	grid:setCell(2, 6, count_spinbox)

	local reset_button = createUIButton({ text = "Reset",
		onClick = function()
			volume_slider.value = 0.5 -- Setting value or checked doesn't call onChange, so update the labels as well.
			volume_label.text = "Volume: 0.50"
			lights_checkbox.checked = true
			lights_label.text = "Lights on"
			quality_dropdown.value = "Medium"
			quality_label.text = "Quality: Medium"
			name_edit.text = ""
			count_spinbox.value = 3
		end })
	grid:addRow(reset_button) -- Puts the button in column 1 of a new row at the bottom.

	local logo_button = createUIButton({ image_url = "https://example.com/logo.png", size = Vec2(24, 24),
		onClick = function() logo.visible = not logo.visible end })
	grid:setCell(2, 7, logo_button) -- Next to the reset button.
end

function onUserUsedObject(av : Avatar, ob : Object)
	if window then
		window.visible = true -- Show the window again if it was closed.
	else
		createSettingsWindow()
	end
end
=====================================================================*/
namespace LuaScriptUIFunctions
{
	// Sets createUIImage() etc. as globals.  Only valid for per-object script VMs, as the functions call getSubstrataLuaScript().
	void registerGlobalFunctions(lua_State* state);

	// Creates the read-only metatable for widget handles.  Returns a reference to it, and leaves the stack unchanged.
	LuaVM::LuaValueRef createWidgetMetaTable(LuaVM& lua_vm);
}
