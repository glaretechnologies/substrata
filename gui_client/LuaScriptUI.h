/*=====================================================================
LuaScriptUI.h
-------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "../shared/URLString.h"
#include <lua/LuaVM.h>
#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUICallbackHandler.h>
#include <opengl/ui/GLUIImage.h>
#include <opengl/ui/GLUISlider.h>
#include <opengl/ui/GLUITextButton.h>
#include <utils/RefCounted.h>
#include <utils/Reference.h>
#include <map>
#include <vector>


class GUIClient;
class SubstrataLuaScript;


/*=====================================================================
LuaScriptUIWidget
-----------------
A GLUI widget created by a Lua script, with createUIImage(), createUIButton() or createUISlider().
=====================================================================*/
class LuaScriptUIWidget : public RefCounted
{
public:
	enum Anchor
	{
		Anchor_TopLeft,
		Anchor_Top,
		Anchor_TopRight,
		Anchor_Left,
		Anchor_Centre,
		Anchor_Right,
		Anchor_BottomLeft,
		Anchor_Bottom,
		Anchor_BottomRight
	};

	static bool parseAnchor(const std::string& s, Anchor& anchor_out); // Returns false if s is not a valid anchor name.

	int id;
	SubstrataLuaScript* sub_lua_script; // The script that created the widget.  The widget is removed when the script evaluator is destroyed.

	GLUIImageRef image;
	GLUITextButtonRef button;
	GLUISliderRef slider;

	Anchor anchor;
	Vec2f offset_px; // Offset of the widget from its anchor point, in device-independent pixels.  +x is right, +y is up.
	Vec2f size_px; // For images and sliders.

	URLString image_URL;
	bool image_texture_assigned;

	bool visible; // Visibility requested by the script.  The widget is not drawn while LuaScriptUI is hidden, and an image is not drawn until its texture has loaded, regardless of this.

	LuaVM::LuaValueRef callback_ref; // Reference to the Lua onClick function for buttons, or onChange function for sliders.  LUA_NOREF if none.

	bool slider_change_queued; // True if the slider value has changed since the last onChange call, and the widget id is in LuaScriptUI::changed_slider_ids.
};
typedef Reference<LuaScriptUIWidget> LuaScriptUIWidgetRef;


/*=====================================================================
LuaScriptUI
-----------
Holds the widgets created by Lua scripts on the client.

Widgets are positioned relative to an anchor point on the viewport (a corner, edge centre or the centre),
plus an offset in pixels, and are repositioned when the viewport is resized.

Button clicks and slider value changes are queued and the Lua onClick and onChange callbacks are run from think(), not from inside GLUI event dispatch,
so that a callback can safely create or destroy widgets.  A slider's onChange is called at most once per think() call, with the latest value,
so dragging a slider doesn't run the callback for every mouse move event.
=====================================================================*/
class LuaScriptUI : public RefCounted, public GLUICallbackHandler
{
public:
	LuaScriptUI(GUIClient* gui_client, const GLUIRef& gl_ui);
	~LuaScriptUI();

	static const size_t MAX_NUM_WIDGETS_PER_SCRIPT = 32;

	// Return the new widget id.  Throws glare::Exception if the script has too many widgets.
	int createImage(SubstrataLuaScript* sub_lua_script, const URLString& image_URL, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px, const Vec2f& size_px, bool visible);
	int createButton(SubstrataLuaScript* sub_lua_script, const std::string& text, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px, LuaVM::LuaValueRef onClick_ref, bool visible);
	int createSlider(SubstrataLuaScript* sub_lua_script, double min_value, double max_value, double initial_value, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px, const Vec2f& size_px,
		LuaVM::LuaValueRef onChange_ref, bool visible);

	// Returns NULL if there is no widget with the given id belonging to the script.
	LuaScriptUIWidget* getWidget(SubstrataLuaScript* sub_lua_script, int id);

	void destroyWidget(SubstrataLuaScript* sub_lua_script, int id);

	void setWidgetVisible(LuaScriptUIWidget& widget, bool visible);
	void setButtonText(LuaScriptUIWidget& widget, const std::string& text);
	void setSliderValue(LuaScriptUIWidget& widget, double value); // Doesn't call onChange.

	void removeWidgetsForScript(SubstrataLuaScript* sub_lua_script);
	void removeAllWidgets();

	// Assigns any newly loaded image textures, and runs queued onClick and onChange callbacks.
	void think();

	void viewportResized();

	// Shows or hides all script widgets, e.g. for the Hide UI action.  Each widget's script-requested visibility is kept, and applies again when shown.
	void setVisible(bool visible);

	virtual void eventOccurred(GLUICallbackEvent& event) override; // From GLUICallbackHandler
	virtual void sliderValueChangedEventOccurred(GLUISliderValueChangedEvent& event) override; // From GLUICallbackHandler

private:
	size_t numWidgetsForScript(SubstrataLuaScript* sub_lua_script) const;
	void updateWidgetTransform(LuaScriptUIWidget& widget);
	void updateWidgetDrawn(LuaScriptUIWidget& widget);
	void checkAssignImageTexture(LuaScriptUIWidget& widget);
	void removeWidgetFromUI(LuaScriptUIWidget& widget);

	GUIClient* gui_client;
	GLUIRef gl_ui;

	std::map<int, LuaScriptUIWidgetRef> widgets; // Map from widget id to widget
	int next_widget_id;
	bool ui_visible; // False while all script widgets are hidden, see setVisible().

	std::vector<int> clicked_widget_ids; // Buttons clicked since the last think() call.
	std::vector<int> changed_slider_ids; // Sliders whose value changed since the last think() call.
};
typedef Reference<LuaScriptUI> LuaScriptUIRef;
