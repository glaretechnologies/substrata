/*=====================================================================
LuaScriptUIFunctions.cpp
------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "LuaScriptUIFunctions.h"


#include "SubstrataLuaScript.h"
#include "SubstrataLuaVM.h"
#include "URLString.h"
#if GUI_CLIENT
#include "../gui_client/GUIClient.h"
#include "../gui_client/LuaScriptUI.h"
#include <graphics/SRGBUtils.h>
#endif
#include <lua/LuaUtils.h>
#include <lua/LuaScript.h>
#include <utils/StringUtils.h>
#include <utils/Exception.h>
#include <maths/mathstypes.h>
#include <maths/vec2.h>
#include <graphics/colour3.h>
#include <lualib.h>
#include <cmath>
#include <initializer_list>
#include <limits>


// If false, createUIImage(), createUIButton(), createUISlider(), createUILabel() and createUIGrid() are not registered, so scripts can't create UI widgets.
static const bool ENABLE_SCRIPT_UI = true;

static const size_t MAX_UI_TEXT_SIZE = 1000;

static const int MAX_UI_GRID_SIZE = 32; // Max number of rows and of columns in a grid.  Same as LuaScriptUI::MAX_GRID_SIZE.


static std::string errorContextString(lua_State* state)
{
	return "\n" + LuaUtils::getCallStackAsString(state);
}


static void checkNumArgs(lua_State* state, int num_args_required)
{
	const int num_args = lua_gettop(state);
	if(num_args < num_args_required)
		throw glare::Exception("Expected " + toString(num_args_required) + " arg(s) to function, got " + toString(num_args) + "." + errorContextString(state));
}


static SubstrataLuaScript* getSubstrataLuaScript(lua_State* state)
{
	return static_cast<SubstrataLuaScript*>(static_cast<LuaScript*>(lua_getthreaddata(state)));
}


// Prints a warning to the script output for each key in the table that isn't one of valid_keys, so that misspelt parameter names are reported.
// These are warnings rather than errors, so that scripts using parameters added in later versions still run on older clients and servers.
static void warnAboutUnknownTableKeys(lua_State* state, int table_index, const char* func_name, std::initializer_list<const char*> valid_keys)
{
	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	lua_pushnil(state); // Push nil as the first key
	while(lua_next(state, table_index) != 0) // Pops key, pushes next key and its value
	{
		if(lua_type(state, -2) == LUA_TSTRING) // Check the type rather than calling lua_tostring() on any key, as that would convert a number key in place and break lua_next.
		{
			const char* key = lua_tostring(state, -2);
			bool valid = false;
			for(const char* valid_key : valid_keys)
				if(stringEqual(key, valid_key))
					valid = true;

			if(!valid)
			{
				std::string valid_keys_str;
				for(const char* valid_key : valid_keys)
					valid_keys_str += (valid_keys_str.empty() ? "" : ", ") + std::string(valid_key);

				sub_lua_script->printWarningOnce(std::string(func_name) + "(): unknown parameter '" + std::string(key).substr(0, 100) + "' was ignored (valid parameters are " + valid_keys_str + ")");
			}
		}
		else
			sub_lua_script->printWarningOnce(std::string(func_name) + "(): parameter table has a key that is not a string, which was ignored");

		lua_pop(state, 1); // Pop value, keep key for the next lua_next call
	}
}


// Reads a field like {x=1, y=2} from the table, and checks the values are finite with magnitude at most max_abs_val.
static Vec2f getUIVec2fTableField(lua_State* state, int table_index, const char* key, const Vec2f& default_val, float max_abs_val)
{
	const Vec2f v = LuaUtils::getTableVec2fFieldWithDefault(state, table_index, key, default_val);

	if(!isFinite(v.x) || !isFinite(v.y) || (std::fabs(v.x) > max_abs_val) || (std::fabs(v.y) > max_abs_val))
		throw glare::Exception("Field '" + std::string(key) + "' values must be finite and have magnitude at most " + toString(max_abs_val) + errorContextString(state));

	return v;
}


// Reads a Vec3f sRGB colour field from the table, and checks the components are in [0, 1].
static Colour3f getUIColourTableField(lua_State* state, int table_index, const char* key, const Colour3f& default_val)
{
	const Vec3f c = LuaUtils::getTableVec3fFieldWithDefault(state, table_index, key, Vec3f(default_val.r, default_val.g, default_val.b));
	for(int i=0; i<3; ++i)
		if(!isFinite(c[i]) || !(c[i] >= 0.f && c[i] <= 1.f)) // Check isFinite explicitly, as with /fp:fast NaN comparisons may not be false.
			throw glare::Exception("Field '" + std::string(key) + "' components must be between 0 and 1" + errorContextString(state));
	return Colour3f(c.x, c.y, c.z);
}


static float getUIAlphaTableField(lua_State* state, int table_index, const char* key, float default_val)
{
	const double alpha = LuaUtils::getTableNumberFieldWithDefault(state, table_index, key, default_val);
	if(!isFinite(alpha) || !(alpha >= 0.0 && alpha <= 1.0)) // Check isFinite explicitly, as with /fp:fast NaN comparisons may not be false.
		throw glare::Exception("Field '" + std::string(key) + "' must be between 0 and 1" + errorContextString(state));
	return (float)alpha;
}


static std::string getUITextTableField(lua_State* state, int table_index, const char* key)
{
	const std::string text = LuaUtils::getTableStringField(state, table_index, key);
	if(text.size() > MAX_UI_TEXT_SIZE)
		throw glare::Exception("Field '" + std::string(key) + "' is too long (max " + toString(MAX_UI_TEXT_SIZE) + " bytes)" + errorContextString(state));
	return text;
}


// Pushes a handle table { id = widget_id } with the widget handle metatable.
static void pushUIWidgetHandle(SubstrataLuaScript* sub_lua_script, lua_State* state, int widget_id)
{
	lua_createtable(state, /*num array elems=*/0, /*num non-array elems=*/1); // Push handle table
	LuaUtils::setNumberAsTableField(state, "id", widget_id);

	lua_getref(state, sub_lua_script->substrata_lua_vm->scriptUIWidgetMetaTable_ref.ref); // Push metatable
	lua_setmetatable(state, -2); // Pops the metatable and sets it as the metatable of the handle table.
}


#if GUI_CLIENT
static LuaScriptUIWidget::Anchor getAnchorTableField(lua_State* state, int table_index)
{
	std::string anchor_str = LuaUtils::getTableStringFieldWithEmptyDefault(state, table_index, "anchor");
	if(anchor_str.empty())
		anchor_str = "centre";

	LuaScriptUIWidget::Anchor anchor;
	if(!LuaScriptUIWidget::parseAnchor(anchor_str, anchor))
		throw glare::Exception("Invalid anchor (valid anchors are top-left, top, top-right, left, centre, right, bottom-left, bottom, bottom-right)" + errorContextString(state));
	return anchor;
}


// Reads the id field of a widget handle.  Scripts can assign any number to it, so check it is finite and in int range before converting it.
static int getUIWidgetIDFromHandle(lua_State* state, int handle_index)
{
	const double id = LuaUtils::getTableNumberField(state, handle_index, "id");
	if(!isFinite(id) || !(id >= (double)std::numeric_limits<int>::min() && id <= (double)std::numeric_limits<int>::max()))
		throw glare::Exception("Invalid widget handle id" + errorContextString(state));
	return (int)id;
}


// Returns NULL if the handle is a dummy handle, or the widget has been destroyed.
static LuaScriptUIWidget* getUIWidgetForHandle(lua_State* state, int handle_index)
{
	const int id = getUIWidgetIDFromHandle(state, handle_index);

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	if(!sub_lua_script->gui_client->lua_script_ui)
		return NULL;
	return sub_lua_script->gui_client->lua_script_ui->getWidget(sub_lua_script, id);
}
#endif


static int createUIImage(lua_State* state)
{
	// Arg 1: params : Table, with fields
	//   image_url : string    Must be declared in the script header with @resource.
	//   size : {x=w, y=h}     Size in pixels.  Default {x=128, y=128}.
	//   anchor : string       Viewport anchor point.  Default "centre".
	//   offset : {x=, y=}     Offset from the anchor point in pixels, +x is right, +y is up.  Default {x=0, y=0}.
	//   visible : bool        Default true.
	// Returns a widget handle.

	checkNumArgs(state, /*num_args_required*/1);
	LuaUtils::checkValueIsTable(state, /*index=*/1);
	warnAboutUnknownTableKeys(state, /*table index=*/1, "createUIImage", { "image_url", "size", "anchor", "offset", "visible" });

	const std::string image_URL = LuaUtils::getTableStringField(state, /*table index=*/1, "image_url");
	const Vec2f size_px   = getUIVec2fTableField(state, /*table index=*/1, "size", /*default=*/Vec2f(128.f), /*max abs val=*/4096.f);
	[[maybe_unused]] const Vec2f offset_px = getUIVec2fTableField(state, /*table index=*/1, "offset", /*default=*/Vec2f(0.f), /*max abs val=*/10000.f);
	[[maybe_unused]] const bool visible = LuaUtils::getTableBoolFieldWithDefault(state, /*table index=*/1, "visible", /*default=*/true);

	if(!(size_px.x >= 1.f && size_px.y >= 1.f))
		throw glare::Exception("createUIImage(): size must be at least 1 pixel" + errorContextString(state));

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	if(!sub_lua_script->isResourceURLDeclared(image_URL))
		throw glare::Exception("createUIImage(): image_url '" + image_URL.substr(0, 256) + "' is not declared in the script header.  Declare it with a line like:\n-- @resource " + image_URL.substr(0, 256) + errorContextString(state));

	int widget_id = 0;
#if GUI_CLIENT
	const LuaScriptUIWidget::Anchor anchor = getAnchorTableField(state, /*table index=*/1);

	if(sub_lua_script->gui_client->lua_script_ui)
		widget_id = sub_lua_script->gui_client->lua_script_ui->createImage(sub_lua_script, toURLString(image_URL), anchor, offset_px, size_px, visible);
#endif

	pushUIWidgetHandle(sub_lua_script, state, widget_id); // Push handle table
	return 1; // Count of returned values
}


static int createUIButton(lua_State* state)
{
	// Arg 1: params : Table, with fields
	//   text : string
	//   onClick : function    Called with no arguments when the button is clicked.  Optional.
	//   anchor : string       Viewport anchor point.  Default "centre".
	//   offset : {x=, y=}     Offset from the anchor point in pixels, +x is right, +y is up.  Default {x=0, y=0}.
	//   visible : bool        Default true.
	// Returns a widget handle.

	checkNumArgs(state, /*num_args_required*/1);
	LuaUtils::checkValueIsTable(state, /*index=*/1);
	warnAboutUnknownTableKeys(state, /*table index=*/1, "createUIButton", { "text", "onClick", "anchor", "offset", "visible" });

	[[maybe_unused]] const std::string text = getUITextTableField(state, /*table index=*/1, "text");
	[[maybe_unused]] const Vec2f offset_px = getUIVec2fTableField(state, /*table index=*/1, "offset", /*default=*/Vec2f(0.f), /*max abs val=*/10000.f);
	[[maybe_unused]] const bool visible = LuaUtils::getTableBoolFieldWithDefault(state, /*table index=*/1, "visible", /*default=*/true);

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	int widget_id = 0;
#if GUI_CLIENT
	const LuaScriptUIWidget::Anchor anchor = getAnchorTableField(state, /*table index=*/1);

	if(sub_lua_script->gui_client->lua_script_ui)
	{
		const LuaVM::LuaValueRef onClick_ref = sub_lua_script->getOptionalRefToFunctionInTable(state, /*table index=*/1, "onClick");
		try
		{
			widget_id = sub_lua_script->gui_client->lua_script_ui->createButton(sub_lua_script, text, anchor, offset_px, onClick_ref, visible); // Takes ownership of onClick_ref
		}
		catch(glare::Exception&)
		{
			sub_lua_script->freeRefToFunction(onClick_ref); // Does nothing for LUA_NOREF.
			throw;
		}
	}
#endif

	pushUIWidgetHandle(sub_lua_script, state, widget_id); // Push handle table
	return 1; // Count of returned values
}


static int createUISlider(lua_State* state)
{
	// Arg 1: params : Table, with fields
	//   min : number          Default 0.
	//   max : number          Default 1.
	//   value : number        Initial value, clamped to [min, max].  Default min.
	//   onChange : function   Called with the new value when the user changes the value.  Optional.
	//   width : number        Width in pixels.  Default 200.
	//   anchor : string       Viewport anchor point.  Default "centre".
	//   offset : {x=, y=}     Offset from the anchor point in pixels, +x is right, +y is up.  Default {x=0, y=0}.
	//   visible : bool        Default true.
	// Returns a widget handle.

	checkNumArgs(state, /*num_args_required*/1);
	LuaUtils::checkValueIsTable(state, /*index=*/1);
	warnAboutUnknownTableKeys(state, /*table index=*/1, "createUISlider", { "min", "max", "value", "onChange", "width", "anchor", "offset", "visible" });

	const double min_value = LuaUtils::getTableNumberFieldWithDefault(state, /*table index=*/1, "min", /*default=*/0.0);
	const double max_value = LuaUtils::getTableNumberFieldWithDefault(state, /*table index=*/1, "max", /*default=*/1.0);
	[[maybe_unused]] const double value = LuaUtils::getTableNumberFieldWithDefault(state, /*table index=*/1, "value", /*default=*/min_value);
	const double width = LuaUtils::getTableNumberFieldWithDefault(state, /*table index=*/1, "width", /*default=*/200.0);
	[[maybe_unused]] const Vec2f offset_px = getUIVec2fTableField(state, /*table index=*/1, "offset", /*default=*/Vec2f(0.f), /*max abs val=*/10000.f);
	[[maybe_unused]] const bool visible = LuaUtils::getTableBoolFieldWithDefault(state, /*table index=*/1, "visible", /*default=*/true);

	if(!isFinite(min_value) || !isFinite(max_value) || !isFinite(value) || (std::fabs(min_value) > 1.0e15) || (std::fabs(max_value) > 1.0e15))
		throw glare::Exception("createUISlider(): min, max and value must be finite, and min and max must have magnitude at most 1e15" + errorContextString(state));
	if(!(min_value < max_value))
		throw glare::Exception("createUISlider(): min must be less than max" + errorContextString(state));
	if(!isFinite(width) || !(width >= 20.0 && width <= 4096.0))
		throw glare::Exception("createUISlider(): width must be between 20 and 4096 pixels" + errorContextString(state));

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	int widget_id = 0;
#if GUI_CLIENT
	const LuaScriptUIWidget::Anchor anchor = getAnchorTableField(state, /*table index=*/1);

	if(sub_lua_script->gui_client->lua_script_ui)
	{
		const LuaVM::LuaValueRef onChange_ref = sub_lua_script->getOptionalRefToFunctionInTable(state, /*table index=*/1, "onChange");
		try
		{
			widget_id = sub_lua_script->gui_client->lua_script_ui->createSlider(sub_lua_script, min_value, max_value, value, anchor, offset_px, /*size_px=*/Vec2f((float)width, 21.f), onChange_ref, visible); // Takes ownership of onChange_ref
		}
		catch(glare::Exception&)
		{
			sub_lua_script->freeRefToFunction(onChange_ref); // Does nothing for LUA_NOREF.
			throw;
		}
	}
#endif

	pushUIWidgetHandle(sub_lua_script, state, widget_id); // Push handle table
	return 1; // Count of returned values
}


static int createUILabel(lua_State* state)
{
	// Arg 1: params : Table, with fields
	//   text : string              Can contain newlines.
	//   text_colour : Vec3f        sRGB colour, components in [0, 1].  Default Vec3f(1, 1, 1).
	//   font_size : number         In pixels, between 6 and 100.  Default is the default UI font size.
	//   background_colour : Vec3f  sRGB colour, components in [0, 1].  Default Vec3f(0, 0, 0).
	//   background_alpha : number  In [0, 1].  Default 0 (no background).
	//   anchor : string            Viewport anchor point.  Default "centre".
	//   offset : {x=, y=}          Offset from the anchor point in pixels, +x is right, +y is up.  Default {x=0, y=0}.
	//   visible : bool             Default true.
	// Returns a widget handle.

	checkNumArgs(state, /*num_args_required*/1);
	LuaUtils::checkValueIsTable(state, /*index=*/1);
	warnAboutUnknownTableKeys(state, /*table index=*/1, "createUILabel", { "text", "text_colour", "font_size", "background_colour", "background_alpha", "anchor", "offset", "visible" });

	[[maybe_unused]] const std::string text = getUITextTableField(state, /*table index=*/1, "text");
	[[maybe_unused]] const Colour3f text_colour = getUIColourTableField(state, /*table index=*/1, "text_colour", /*default=*/Colour3f(1.f));
	const double font_size = LuaUtils::getTableNumberFieldWithDefault(state, /*table index=*/1, "font_size", /*default=*/-1.0); // -1 = use default
	[[maybe_unused]] const Colour3f background_colour = getUIColourTableField(state, /*table index=*/1, "background_colour", /*default=*/Colour3f(0.f));
	[[maybe_unused]] const float background_alpha = getUIAlphaTableField(state, /*table index=*/1, "background_alpha", /*default=*/0.f);
	[[maybe_unused]] const Vec2f offset_px = getUIVec2fTableField(state, /*table index=*/1, "offset", /*default=*/Vec2f(0.f), /*max abs val=*/10000.f);
	[[maybe_unused]] const bool visible = LuaUtils::getTableBoolFieldWithDefault(state, /*table index=*/1, "visible", /*default=*/true);

	if(!isFinite(font_size) || !(font_size == -1.0 || (font_size >= 6.0 && font_size <= 100.0)))
		throw glare::Exception("createUILabel(): font_size must be between 6 and 100 pixels" + errorContextString(state));

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	int widget_id = 0;
#if GUI_CLIENT
	const LuaScriptUIWidget::Anchor anchor = getAnchorTableField(state, /*table index=*/1);
	const int font_size_px = (font_size == -1.0) ? GLUI::getDefaultFontSizePx() : (int)font_size;

	if(sub_lua_script->gui_client->lua_script_ui)
		widget_id = sub_lua_script->gui_client->lua_script_ui->createLabel(sub_lua_script, text, toLinearSRGB(text_colour), toLinearSRGB(background_colour), background_alpha, font_size_px,
			anchor, offset_px, visible);
#endif

	pushUIWidgetHandle(sub_lua_script, state, widget_id); // Push handle table
	return 1; // Count of returned values
}


static int createUIGrid(lua_State* state)
{
	// Arg 1: params : Table, with fields
	//   background_colour : Vec3f  sRGB colour, components in [0, 1].  Default Vec3f(0.1, 0.1, 0.1).
	//   background_alpha : number  In [0, 1].  Default 0 (no background).  If > 0, clicks on the background don't go through to the world.
	//   interior_padding : number  Padding on each cell side that faces another cell, in pixels, so the gap between adjacent cells is twice this.  Default 5.
	//   edge_padding : number      Padding on each cell side that faces the grid edge, in pixels.  Default 10.
	//   anchor : string            Viewport anchor point.  Default "centre".
	//   offset : {x=, y=}          Offset from the anchor point in pixels, +x is right, +y is up.  Default {x=0, y=0}.
	//   visible : bool             Default true.
	// Returns a widget handle.  Add widgets with grid:setCell(column, row, widget) or grid:addRow(widget).

	checkNumArgs(state, /*num_args_required*/1);
	LuaUtils::checkValueIsTable(state, /*index=*/1);
	warnAboutUnknownTableKeys(state, /*table index=*/1, "createUIGrid", { "background_colour", "background_alpha", "interior_padding", "edge_padding", "anchor", "offset", "visible" });

	[[maybe_unused]] const Colour3f background_colour = getUIColourTableField(state, /*table index=*/1, "background_colour", /*default=*/Colour3f(0.1f));
	[[maybe_unused]] const float background_alpha = getUIAlphaTableField(state, /*table index=*/1, "background_alpha", /*default=*/0.f);
	const double interior_padding = LuaUtils::getTableNumberFieldWithDefault(state, /*table index=*/1, "interior_padding", /*default=*/5.0);
	const double edge_padding = LuaUtils::getTableNumberFieldWithDefault(state, /*table index=*/1, "edge_padding", /*default=*/10.0);
	[[maybe_unused]] const Vec2f offset_px = getUIVec2fTableField(state, /*table index=*/1, "offset", /*default=*/Vec2f(0.f), /*max abs val=*/10000.f);
	[[maybe_unused]] const bool visible = LuaUtils::getTableBoolFieldWithDefault(state, /*table index=*/1, "visible", /*default=*/true);

	if(!isFinite(interior_padding) || !isFinite(edge_padding) || !(interior_padding >= 0.0 && interior_padding <= 100.0) || !(edge_padding >= 0.0 && edge_padding <= 100.0))
		throw glare::Exception("createUIGrid(): interior_padding and edge_padding must be between 0 and 100 pixels" + errorContextString(state));

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	int widget_id = 0;
#if GUI_CLIENT
	const LuaScriptUIWidget::Anchor anchor = getAnchorTableField(state, /*table index=*/1);

	if(sub_lua_script->gui_client->lua_script_ui)
		widget_id = sub_lua_script->gui_client->lua_script_ui->createGrid(sub_lua_script, anchor, offset_px, toLinearSRGB(background_colour), background_alpha,
			(float)interior_padding, (float)edge_padding, visible);
#endif

	pushUIWidgetHandle(sub_lua_script, state, widget_id); // Push handle table
	return 1; // Count of returned values
}


static int getGridIndexArg(lua_State* state, int index)
{
	const double v = LuaUtils::getDoubleArg(state, index);
	if(!isFinite(v) || !(v >= 1.0 && v <= (double)MAX_UI_GRID_SIZE) || (v != std::floor(v)))
		throw glare::Exception("UI grid: column and row must be integers between 1 and " + toString(MAX_UI_GRID_SIZE) + errorContextString(state));
	return (int)v;
}


// grid:setCell(column, row, widget).  column and row start at 1, (1, 1) is the top-left cell.  If widget is nil, clears the cell.
static int gridSetCell(lua_State* state)
{
	// Arg 1: grid handle
	// Arg 2: column : number
	// Arg 3: row : number
	// Arg 4: widget handle, or nil

	checkNumArgs(state, /*num_args_required*/3);
	LuaUtils::checkValueIsTable(state, /*index=*/1);

	[[maybe_unused]] const int column = getGridIndexArg(state, /*index=*/2);
	[[maybe_unused]] const int row    = getGridIndexArg(state, /*index=*/3);

	const bool clear_cell = lua_isnoneornil(state, /*index=*/4);
	if(!clear_cell)
		LuaUtils::checkValueIsTable(state, /*index=*/4);

#if GUI_CLIENT
	LuaScriptUIWidget* grid = getUIWidgetForHandle(state, /*handle index=*/1);
	if(grid)
	{
		LuaScriptUIWidget* child = clear_cell ? NULL : getUIWidgetForHandle(state, /*handle index=*/4);
		if(clear_cell || child) // If the widget has been destroyed, do nothing.
			getSubstrataLuaScript(state)->gui_client->lua_script_ui->setGridCell(*grid, column - 1, row - 1, child);
	}
#endif
	return 0;
}


// grid:addRow(widget).  Puts the widget in the first column of a new row at the bottom of the grid.
static int gridAddRow(lua_State* state)
{
	// Arg 1: grid handle
	// Arg 2: widget handle

	checkNumArgs(state, /*num_args_required*/2);
	LuaUtils::checkValueIsTable(state, /*index=*/1);
	LuaUtils::checkValueIsTable(state, /*index=*/2);

#if GUI_CLIENT
	LuaScriptUIWidget* grid  = getUIWidgetForHandle(state, /*handle index=*/1);
	LuaScriptUIWidget* child = getUIWidgetForHandle(state, /*handle index=*/2);
	if(grid && child)
		getSubstrataLuaScript(state)->gui_client->lua_script_ui->addGridRow(*grid, *child);
#endif
	return 0;
}


static int destroyUIWidget(lua_State* state)
{
	// Arg 1: widget handle

	checkNumArgs(state, /*num_args_required*/1);
	LuaUtils::checkValueIsTable(state, /*index=*/1);

#if GUI_CLIENT
	const int id = getUIWidgetIDFromHandle(state, /*handle index=*/1);

	SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	if(sub_lua_script->gui_client->lua_script_ui)
		sub_lua_script->gui_client->lua_script_ui->destroyWidget(sub_lua_script, id);
#endif
	return 0;
}


// __index metamethod for widget handles.  Called when a field that isn't in the handle table is read.
static int scriptUIWidgetIndexMetaMethod(lua_State* state)
{
	// Arg 1: handle table
	// Arg 2: key
	checkNumArgs(state, 2);

	const char* key_str = lua_tolstring(state, /*index=*/2, /*stringlen=*/NULL); // May return NULL if not a string
	if(!key_str)
		throw glare::Exception("UI widget: key must be a string" + errorContextString(state));

	if(stringEqual(key_str, "destroy"))
	{
		lua_pushcfunction(state, destroyUIWidget, /*debugname=*/"destroy"); // Push destroy function
		return 1;
	}
	else if(stringEqual(key_str, "setCell"))
	{
		lua_pushcfunction(state, gridSetCell, /*debugname=*/"setCell"); // Push setCell function
		return 1;
	}
	else if(stringEqual(key_str, "addRow"))
	{
		lua_pushcfunction(state, gridAddRow, /*debugname=*/"addRow"); // Push addRow function
		return 1;
	}
	else if(stringEqual(key_str, "visible"))
	{
		bool visible = false;
#if GUI_CLIENT
		LuaScriptUIWidget* widget = getUIWidgetForHandle(state, /*handle index=*/1);
		visible = widget && widget->visible;
#endif
		lua_pushboolean(state, visible ? 1 : 0); // Push visible
		return 1;
	}
	else if(stringEqual(key_str, "value"))
	{
		double value = 0;
#if GUI_CLIENT
		LuaScriptUIWidget* widget = getUIWidgetForHandle(state, /*handle index=*/1);
		if(widget)
		{
			if(!widget->slider)
				throw glare::Exception("UI widget: only sliders have a value field" + errorContextString(state));
			value = widget->slider->getValue();
		}
#endif
		lua_pushnumber(state, value); // Push value
		return 1;
	}
	else
		throw glare::Exception("UI widget: Unknown field '" + std::string(key_str) + "'" + errorContextString(state));
}


// __newindex metamethod for widget handles.  Called when a field that isn't in the handle table is assigned to.
static int scriptUIWidgetNewIndexMetaMethod(lua_State* state)
{
	// Arg 1: handle table
	// Arg 2: key
	// Arg 3: value
	checkNumArgs(state, 3);

	const char* key_str = lua_tolstring(state, /*index=*/2, /*stringlen=*/NULL); // May return NULL if not a string
	if(!key_str)
		throw glare::Exception("UI widget: key must be a string" + errorContextString(state));

	[[maybe_unused]] SubstrataLuaScript* sub_lua_script = getSubstrataLuaScript(state);

	if(stringEqual(key_str, "visible"))
	{
		[[maybe_unused]] const bool visible = LuaUtils::getBool(state, /*index=*/3);
#if GUI_CLIENT
		LuaScriptUIWidget* widget = getUIWidgetForHandle(state, /*handle index=*/1);
		if(widget)
		{
			sub_lua_script->gui_client->lua_script_ui->setWidgetVisible(*widget, visible);
		}
#endif
	}
	else if(stringEqual(key_str, "text"))
	{
		const std::string text = LuaUtils::getString(state, /*index=*/3);
		if(text.size() > MAX_UI_TEXT_SIZE)
			throw glare::Exception("UI widget: text is too long (max " + toString(MAX_UI_TEXT_SIZE) + " bytes)" + errorContextString(state));
#if GUI_CLIENT
		LuaScriptUIWidget* widget = getUIWidgetForHandle(state, /*handle index=*/1);
		if(widget)
		{
			if(!widget->button && !widget->label)
				throw glare::Exception("UI widget: only buttons and labels have a text field" + errorContextString(state));

			sub_lua_script->gui_client->lua_script_ui->setWidgetText(*widget, text);
		}
#endif
	}
	else if(stringEqual(key_str, "value"))
	{
		[[maybe_unused]] const double value = LuaUtils::getDoubleArg(state, /*index=*/3);
		if(!isFinite(value))
			throw glare::Exception("UI widget: value must be finite" + errorContextString(state));
#if GUI_CLIENT
		LuaScriptUIWidget* widget = getUIWidgetForHandle(state, /*handle index=*/1);
		if(widget)
		{
			if(!widget->slider)
				throw glare::Exception("UI widget: only sliders have a value field" + errorContextString(state));

			sub_lua_script->gui_client->lua_script_ui->setSliderValue(*widget, value);
		}
#endif
	}
	else
		throw glare::Exception("UI widget: Unknown or read-only field '" + std::string(key_str) + "'" + errorContextString(state));
	return 0;
}


void LuaScriptUIFunctions::registerGlobalFunctions(lua_State* state)
{
	if(!ENABLE_SCRIPT_UI)
		return;

	lua_pushcfunction(state, createUIImage, /*debugname=*/"createUIImage"); // Push function
	lua_setglobal(state, "createUIImage"); // Pops function

	lua_pushcfunction(state, createUIButton, /*debugname=*/"createUIButton"); // Push function
	lua_setglobal(state, "createUIButton"); // Pops function

	lua_pushcfunction(state, createUISlider, /*debugname=*/"createUISlider"); // Push function
	lua_setglobal(state, "createUISlider"); // Pops function

	lua_pushcfunction(state, createUILabel, /*debugname=*/"createUILabel"); // Push function
	lua_setglobal(state, "createUILabel"); // Pops function

	lua_pushcfunction(state, createUIGrid, /*debugname=*/"createUIGrid"); // Push function
	lua_setglobal(state, "createUIGrid"); // Pops function
}


LuaVM::LuaValueRef LuaScriptUIFunctions::createWidgetMetaTable(LuaVM& lua_vm)
{
	// No "uid" field and no entry in metatable_uid_to_ref_map: widget handles can't be serialised (e.g. into object storage).
	lua_createtable(lua_vm.state, /*num array elems=*/0, /*num non-array elems=*/2); // Push metatable

	lua_vm.setCFunctionAsTableField(scriptUIWidgetIndexMetaMethod,    /*debugname=*/"scriptUIWidgetIndexMetaMethod",    /*key=*/"__index");
	lua_vm.setCFunctionAsTableField(scriptUIWidgetNewIndexMetaMethod, /*debugname=*/"scriptUIWidgetNewIndexMetaMethod", /*key=*/"__newindex");

	lua_setreadonly(lua_vm.state, /*index=*/-1, /*enabled=*/1); // Set metatable as read-only.

	const LuaVM::LuaValueRef ref = lua_vm.createReferenceToLuaValue(lua_vm.state, /*index=*/-1); // Does not pop.
	lua_pop(lua_vm.state, 1); // Pop metatable
	return ref;
}
