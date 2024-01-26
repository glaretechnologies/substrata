/*=====================================================================
UIEvents.h
----------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include <maths/vec2.h>
#include <utils/RefCounted.h>
#include <utils/Reference.h>
#include <string>


struct MouseCursorState
{
	Vec2i cursor_pos; // Relative to widget
	Vec2f gl_coords;
	bool ctrl_key_down;
	bool alt_key_down;
};


enum MouseButton
{
	None		= 0,
	Left		= 1,
	Middle		= 2,
	Right		= 4,
	Back		= 8,
	Forward		= 16
};


enum Key
{
	Key_None,

	Key_Escape,
	Key_Backspace,
	Key_Delete,

	Key_Space,

	Key_LeftBracket,
	Key_RightBracket,

	Key_PageUp,
	Key_PageDown,

	Key_Equals,
	Key_Plus,
	Key_Minus,

	Key_Left,
	Key_Right,
	Key_Up,
	Key_Down,

	Key_A,
	Key_B,
	Key_C,
	Key_D,
	Key_E,
	Key_F,
	Key_G,
	Key_H,
	Key_I,
	Key_J,
	Key_K,
	Key_L,
	Key_M,
	Key_N,
	Key_O,
	Key_P,
	Key_Q,
	Key_R,
	Key_S,
	Key_T,
	Key_U,
	Key_V,
	Key_W,
	Key_X,
	Key_Y,
	Key_Z,

	Key_0,
	Key_1,
	Key_2,
	Key_3,
	Key_4,
	Key_5,
	Key_6,
	Key_7,
	Key_8,
	Key_9,

	Key_F1,
	Key_F2,
	Key_F3,
	Key_F4,
	Key_F5,
	Key_F6,
	Key_F7,
	Key_F8,
	Key_F9,
	Key_F10,
	Key_F11,
	Key_F12
};


enum Modifiers
{
	Alt		= 1,
	Ctrl	= 2,
	Shift	= 4
};


class MouseEvent
{
public:
	MouseEvent() : cursor_pos(0, 0), gl_coords(0, 0), accepted(false), modifiers(0), button(MouseButton::None) {}

	Vec2i cursor_pos;
	Vec2f gl_coords;
	uint32 modifiers;
	MouseButton button;

	bool accepted;
};


class MouseWheelEvent
{
public:
	MouseWheelEvent() : modifiers(0), accepted(false) {}

	Vec2i cursor_pos;
	Vec2f gl_coords;
	Vec2i angle_delta;
	uint32 modifiers;

	bool accepted;
};


class KeyEvent
{
public:
	KeyEvent() : modifiers(0), accepted(false) {}

	Key key;
	uint32 native_virtual_key;
	std::string text; // Unicode text in UTF-8
	uint32 modifiers;

	bool accepted;
};
