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
Lua bindings for script UI widgets: createUIImage(), createUIButton(), createUISlider(), createUILabel(), createUIGrid(),
and the metatable for the widget handles they return.

Widgets only exist on the client (see gui_client/LuaScriptUI).  On the server the functions return a handle with id 0, and operations
on it do nothing, so that a script shared between client and server doesn't need to check IS_CLIENT around UI code.
=====================================================================*/
namespace LuaScriptUIFunctions
{
	// Sets createUIImage() etc. as globals.  Only valid for per-object script VMs, as the functions call getSubstrataLuaScript().
	void registerGlobalFunctions(lua_State* state);

	// Creates the read-only metatable for widget handles.  Returns a reference to it, and leaves the stack unchanged.
	LuaVM::LuaValueRef createWidgetMetaTable(LuaVM& lua_vm);
}
