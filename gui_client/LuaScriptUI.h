/*=====================================================================
LuaScriptUI.h
-------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "../shared/URLString.h"
#include "../shared/UID.h"
#include "../shared/UserID.h"
#include <lua/LuaVM.h>
#include <opengl/ui/GLUICallbackHandler.h>
#include <maths/vec2.h>
#include <graphics/colour3.h>
#include <utils/RefCounted.h>
#include <utils/Reference.h>
#include <utils/TimeStamp.h>
#include <utils/Timer.h>
#include <map>
#include <vector>


class GUIClient;
class MouseEvent;
class ScriptUIContextMenu;
class HiddenScriptUI;
class SubstrataLuaScript;
class WorldObject;
class OpenGLTexture;
class GLUI;
class GLUIButton;
class GLUICheckBox;
class GLUIDropDownList;
class GLUIGridContainer;
class GLUIImage;
class GLUILineEdit;
class GLUISlider;
class GLUISpinBox;
class GLUITextButton;
class GLUITextView;
class GLUIWindow;


/*=====================================================================
LuaScriptUIWidget
-----------------
A GLUI widget created by a Lua script, with createUIImage(), createUIButton(), createUISlider(), createUISpinBox(), createUICheckBox(), createUIDropDown(),
createUILineEdit(), createUILabel(), createUIGrid() or createUIWindow().
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

	~LuaScriptUIWidget();

	static bool parseAnchor(const std::string& s, Anchor& anchor_out); // Returns false if s is not a valid anchor name.

	int id;
	SubstrataLuaScript* sub_lua_script; // The script that created the widget.  The widget is removed when the script evaluator is destroyed.

	Reference<GLUIImage> image;
	Reference<GLUITextButton> button;
	Reference<GLUIButton> image_button; // A button showing an image instead of text.
	Reference<GLUISlider> slider;
	Reference<GLUISpinBox> spinbox;
	Reference<GLUICheckBox> checkbox;
	Reference<GLUIDropDownList> dropdown;
	Reference<GLUILineEdit> line_edit; // For line edits.
	Reference<GLUITextView> label;
	Reference<GLUIGridContainer> grid;
	Reference<GLUIWindow> window;

	int parent_id; // Id of the grid or window widget this widget is in, or 0 if the widget is not in a grid or window.

	Anchor anchor; // anchor and offset_px are only used when the widget is not in a grid or window.
	Vec2f offset_px; // Offset of the widget from its anchor point, in device-independent pixels.  +x is right, +y is up.
	Vec2f size_px; // For images, image buttons and sliders.

	URLString image_URL; // For images and image buttons.
	bool image_texture_assigned;

	std::string script_tooltip; // Tooltip set by the script, shown above the line saying which object the widget is from.  Empty if none.

	bool visible; // Visibility requested by the script.  The widget is not drawn while LuaScriptUI is hidden, and an image is not drawn until its texture has loaded, regardless of this.

	LuaVM::LuaValueRef callback_ref; // Reference to the Lua onClick function for buttons, onClose function for windows, or onChange function for other widgets.  LUA_NOREF if none.
	LuaVM::LuaValueRef enter_callback_ref; // Reference to the Lua onEnter function for line edits.  LUA_NOREF if none.

	std::string last_reported_text; // For line edits: the text when onChange was last queued, or when the text was set by the script.  Used so that onChange is only called when the text changes.

	bool value_change_queued; // True if the value has changed since the last onChange call, and the widget id is in LuaScriptUI::changed_value_ids.

	GLUIWidgetRef getGLUIWidget() const;
};
typedef Reference<LuaScriptUIWidget> LuaScriptUIWidgetRef;


// An object or creator the user has hidden script UI from.
struct HiddenScriptUIEntry
{
	TimeStamp hidden_time;
	std::string creator_name; // Name of the object's creator, or of the hidden creator, at the time of hiding.  May be empty if not known.
};


/*=====================================================================
LuaScriptUI
-----------
Holds the widgets created by Lua scripts on the client.

Widgets are positioned relative to an anchor point on the viewport (a corner, edge centre or the centre),
plus an offset in pixels, and are repositioned when the viewport is resized.

A widget can be placed in a cell of a grid widget, or be the body of a window widget, in which case the grid or window lays it out and its own anchor and offset are unused.
Grids can be nested.  Windows can't be put in grids or windows.  A widget is drawn only if it and all the grids and windows containing it are visible.
A widget removed from a grid or window (by clearing or replacing its cell or the window body) goes back to being positioned by its own anchor and offset.
Destroying a grid or window destroys the widgets in it.
Clicking a window's close button hides the window, then calls its onClose function.

Button clicks, value changes and line edit enter presses are queued and the Lua callbacks are run from think(), not from inside GLUI event dispatch,
so that a callback can safely create or destroy widgets.  onChange is called at most once per think() call, with the latest value,
so dragging a slider doesn't run the callback for every mouse move event.
=====================================================================*/
class LuaScriptUI : public RefCounted, public GLUICallbackHandler
{
public:
	LuaScriptUI(GUIClient* gui_client, const Reference<GLUI>& gl_ui);
	~LuaScriptUI();

	static const size_t MAX_NUM_WIDGETS_PER_SCRIPT = 1000;
	static const size_t MAX_GRID_SIZE = 32; // Max number of rows and of columns in a grid.

	// Return the new widget id.  Throws glare::Exception if the script has too many widgets.
	int createImage(SubstrataLuaScript* sub_lua_script, const URLString& image_URL, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px, const Vec2f& size_px, bool visible);
	int createButton(SubstrataLuaScript* sub_lua_script, const std::string& text, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px, LuaVM::LuaValueRef onClick_ref, bool visible);
	int createImageButton(SubstrataLuaScript* sub_lua_script, const URLString& image_URL, const Vec2f& size_px, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px,
		LuaVM::LuaValueRef onClick_ref, bool visible);
	int createSlider(SubstrataLuaScript* sub_lua_script, double min_value, double max_value, double initial_value, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px, const Vec2f& size_px,
		LuaVM::LuaValueRef onChange_ref, bool visible);
	int createSpinBox(SubstrataLuaScript* sub_lua_script, double min_value, double max_value, double initial_value, double step, int num_decimal_places, float width_px,
		LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px, LuaVM::LuaValueRef onChange_ref, bool visible);
	int createCheckBox(SubstrataLuaScript* sub_lua_script, bool checked, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px, LuaVM::LuaValueRef onChange_ref, bool visible);
	int createDropDown(SubstrataLuaScript* sub_lua_script, const std::vector<std::string>& options, const std::string& initial_value, float width_px, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px,
		LuaVM::LuaValueRef onChange_ref, bool visible);
	int createLineEdit(SubstrataLuaScript* sub_lua_script, const std::string& text, float width_px, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px,
		LuaVM::LuaValueRef onChange_ref, LuaVM::LuaValueRef onEnter_ref, bool visible);
	int createLabel(SubstrataLuaScript* sub_lua_script, const std::string& text, const Colour3f& text_colour_linear, const Colour3f& background_colour_linear, float background_alpha, int font_size_px,
		const Vec2f& size_px, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px, bool visible); // size_px: minimum size, or zero to fit the text.
	int createGrid(SubstrataLuaScript* sub_lua_script, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px, const Colour3f& background_colour_linear, float background_alpha,
		float interior_padding_px, float edge_padding_px, bool visible);
	int createWindow(SubstrataLuaScript* sub_lua_script, const std::string& title, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px, const Colour3f& background_colour_linear, float background_alpha,
		float padding_px, LuaVM::LuaValueRef onClose_ref, bool visible);

	// Returns NULL if there is no widget with the given id belonging to the script.
	LuaScriptUIWidget* getWidget(SubstrataLuaScript* sub_lua_script, int id);

	void destroyWidget(SubstrataLuaScript* sub_lua_script, int id);

	void setWidgetVisible(LuaScriptUIWidget& widget, bool visible);
	void setWidgetOffset(LuaScriptUIWidget& widget, const Vec2f& offset_px); // Moves the widget.  Has no visible effect while the widget is in a grid or window.
	void setWidgetText(LuaScriptUIWidget& widget, const std::string& text); // For buttons, labels and line edits.  Doesn't call onChange.
	void setNumberValue(LuaScriptUIWidget& widget, double value); // For sliders and spin boxes.  Clamps to the range.  Doesn't call onChange.
	void setCheckBoxChecked(LuaScriptUIWidget& widget, bool checked); // Doesn't call onChange.
	void setWidgetTooltip(LuaScriptUIWidget& widget, const std::string& tooltip); // Newlines are replaced with spaces, so the tooltip is a single line above the line saying which object the widget is from.
	void setDropDownValue(LuaScriptUIWidget& widget, const std::string& value); // Doesn't call onChange.  Throws glare::Exception if value is not one of the options.

	// Puts child in the given cell of the grid (0-based, (0, 0) is the top-left cell), removing it from any grid it was in first.
	// If child is NULL, clears the cell.  A widget already in the cell is removed from the grid.
	// Throws glare::Exception if grid is not a grid, or if inserting child would put a grid inside itself.
	void setGridCell(LuaScriptUIWidget& grid, int cell_x, int cell_y, LuaScriptUIWidget* child);
	void addGridRow(LuaScriptUIWidget& grid, LuaScriptUIWidget& child); // Puts child in the first column of a new row at the bottom of the grid.

	// Makes body the body widget of the window, removing it from any grid or window it was in first.  If body is NULL, removes the current body.
	// A previous body widget goes back to being positioned by its own anchor and offset.
	// Throws glare::Exception if window is not a window, or if body is a window.
	void setWindowBody(LuaScriptUIWidget& window, LuaScriptUIWidget* body);

	void removeWidgetsForScript(SubstrataLuaScript* sub_lua_script);
	void removeAllWidgets();

	// Runs queued onClick and onChange callbacks.
	void think();

	// Called by GUIClient when a texture has been loaded and uploaded to the GPU.  Assigns it to any images and image buttons that are waiting for it.
	void handleUploadedTexture(const URLString& URL, const Reference<OpenGLTexture>& tex);

	void viewportResized();

	// Shows or hides all script widgets, e.g. for the Hide UI action.  Each widget's script-requested visibility is kept, and applies again when shown.
	void setVisible(bool visible);

	// True if the user has hidden UI from the script's object, or from all objects created by the script's object creator.
	// Widget creation functions return dummy widgets for such scripts.
	bool isUIHiddenForScript(const SubstrataLuaScript* sub_lua_script) const;

	// Hide UI from the object, or from all objects created by the user.  Removes existing widgets from the affected scripts.
	// Saves the entry to the settings store, for the current server.
	// creator_name is the name of the object creator, or of the hidden creator, for showing in the list of hidden UI.  May be empty if not known.
	void hideUIForObject(const UID& ob_uid, const std::string& creator_name);
	void hideUIForCreator(const UserID& creator_id, const std::string& creator_name);

	// Unhide UI from the object, or from the user's objects.  Removes the entry from the settings store,
	// and re-runs the scripts of the loaded objects that UI is no longer hidden for, so that they create their widgets again.
	void unhideUIForObject(const UID& ob_uid);
	void unhideUIForCreator(const UserID& creator_id);

	const std::map<UID, HiddenScriptUIEntry>& getHiddenObjects() const { return hidden_objects; }
	const std::map<UserID, HiddenScriptUIEntry>& getHiddenCreators() const { return hidden_creators; }

	// Opens the window listing hidden script UI, if not already open.
	void openHiddenScriptUI();

	// Loads the hidden objects and creators for the server from the settings store, replacing any loaded for a previous server.  Called when connecting to a server.
	void loadHiddenLists(const std::string& server_hostname);

	virtual void eventOccurred(GLUICallbackEvent& event) override; // From GLUICallbackHandler
	virtual void sliderValueChangedEventOccurred(GLUISliderValueChangedEvent& event) override; // From GLUICallbackHandler

	// Opens the context menu for the right-clicked widget, which shows which object and user the widget is from, and lets the user hide UI from the object or the user.
	virtual void contextMenuRequestedEventOccurred(GLUICallbackContextMenuEvent& event) override; // From GLUICallbackHandler

private:
	void addWidget(const LuaScriptUIWidgetRef& widget); // Sets the widget's tooltip and handler, and adds it to the widgets map.
	std::string makeWidgetTooltip(const LuaScriptUIWidget& widget) const; // The script's tooltip, if any, then a line saying which object the widget is from.
	size_t numWidgetsForScript(SubstrataLuaScript* sub_lua_script) const;
	LuaScriptUIWidget* getParent(const LuaScriptUIWidget& widget); // Returns the grid or window containing the widget, or NULL.
	LuaScriptUIWidget& getRootWidget(LuaScriptUIWidget& widget); // Returns the outermost grid or window containing the widget, or the widget itself if it is not in one.
	bool isEffectivelyVisible(const LuaScriptUIWidget& widget); // True if the UI is visible, and the widget and all grids and windows containing it are visible.
	void removeFromParent(LuaScriptUIWidget& widget); // Removes the widget from the grid or window containing it, if any.  Doesn't reposition the widget.
	void updateWidgetTransform(LuaScriptUIWidget& widget); // Updates the transform of the outermost grid or window containing the widget, or of the widget itself.
	void updateWidgetDrawn(LuaScriptUIWidget& widget); // Also updates widgets in the widget, if it is a grid or window.
	void checkAssignImageTexture(LuaScriptUIWidget& widget); // For images and image buttons.
	void assignImageTexture(LuaScriptUIWidget& widget, const Reference<OpenGLTexture>& tex);
	void startDownloadingImageIfNotPresent(const URLString& image_URL);
	void destroyWidgetAndContents(int id);
	void removeWidgetFromUI(LuaScriptUIWidget& widget);
	void queueValueChanged(int widget_id); // Queues an onChange call for the widget, if one isn't queued already.
	void lineEditTextChanged(int widget_id);
	void clearKeyboardFocusIfHeldBy(const LuaScriptUIWidget& widget); // So that a hidden or destroyed widget doesn't keep receiving key presses.
	void removeWidgetsForHiddenScripts();
	std::vector<Reference<WorldObject>> getObjectsWithHiddenUI(); // Returns the loaded objects with Lua scripts that UI is hidden for.
	void hiddenListsChanged(); // Updates the hidden script UI list, if open.
	void updateHiddenUIButton(); // Updates the hidden UI button position, size and visibility.
	void flashHiddenUIButton();
	void rerunScriptsWithUnhiddenUI(const std::vector<Reference<WorldObject>>& obs);
	std::vector<std::string> hiddenObjectsMapPath() const; // Settings store map paths for the current server.
	std::vector<std::string> hiddenCreatorsMapPath() const;
	void saveHiddenEntry(const std::vector<std::string>& map_path, const std::string& key, const HiddenScriptUIEntry& entry);
	HiddenScriptUIEntry loadHiddenEntry(const std::vector<std::string>& map_path, const std::string& key);

	GUIClient* gui_client;
	Reference<GLUI> gl_ui;

	std::map<int, LuaScriptUIWidgetRef> widgets; // Map from widget id to widget
	int next_widget_id;
	bool ui_visible; // False while all script widgets are hidden, see setVisible().

	std::vector<int> clicked_widget_ids; // Buttons clicked since the last think() call.
	std::vector<int> changed_value_ids; // Widgets whose value changed since the last think() call.
	std::vector<int> entered_line_edit_ids; // Line edits where enter was pressed since the last think() call.
	std::vector<int> closed_window_ids; // Windows whose close button was clicked since the last think() call.

	Reference<ScriptUIContextMenu> context_menu;

	// Objects and creators the user has hidden UI from.
	// Stored in the settings store per server, as object UIDs and user IDs are only unique within a server:
	//   hidden_list/worlds/<server_hostname>/hidden_objects/<uid>/hidden_time, creator_name
	//   hidden_list/worlds/<server_hostname>/hidden_creators/<user_id>/hidden_time, creator_name
	std::string server_hostname;
	std::map<UID, HiddenScriptUIEntry> hidden_objects;
	std::map<UserID, HiddenScriptUIEntry> hidden_creators;

	Reference<HiddenScriptUI> hidden_script_ui; // Window listing hidden script UI.  Null if not open.
	Reference<GLUIButton> hidden_ui_button; // Opens and closes the hidden script UI list.
	bool hidden_ui_button_flashing; // True while the button is flashing after the user blocks something, see flashHiddenUIButton().
	Timer hidden_ui_button_flash_timer; // Time since the flash started.
};
typedef Reference<LuaScriptUI> LuaScriptUIRef;
