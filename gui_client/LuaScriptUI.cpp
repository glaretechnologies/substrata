/*=====================================================================
LuaScriptUI.cpp
---------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "LuaScriptUI.h"


#include "GUIClient.h"
#include "ScriptUIContextMenu.h"
#include "HiddenScriptUI.h"
#include "../shared/SubstrataLuaScript.h"
#include "../shared/ResourceManager.h"
#include "../shared/WorldStateLock.h"
#include <lua/LuaScript.h>
#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUIButton.h>
#include <opengl/ui/GLUICheckBox.h>
#include <opengl/ui/GLUIDropDownList.h>
#include <opengl/ui/GLUIGridContainer.h>
#include <opengl/ui/GLUIImage.h>
#include <opengl/ui/GLUILineEdit.h>
#include <opengl/ui/GLUISlider.h>
#include <opengl/ui/GLUISpinBox.h>
#include <opengl/ui/GLUITextButton.h>
#include <opengl/ui/GLUITextView.h>
#include <opengl/ui/GLUIWindow.h>
#include <graphics/SRGBUtils.h>
#include <maths/mathstypes.h>
#include <settings/SettingsStore.h>
#include <utils/ConPrint.h>
#include <utils/Exception.h>
#include <utils/StringUtils.h>
#include <utils/UTF8Utils.h>
#include <lua.h>
#include <algorithm>
#include <cmath>


LuaScriptUIWidget::~LuaScriptUIWidget()
{}


bool LuaScriptUIWidget::parseAnchor(const std::string& s, Anchor& anchor_out)
{
	if(s == "top-left")			{ anchor_out = Anchor_TopLeft;		return true; }
	if(s == "top")				{ anchor_out = Anchor_Top;			return true; }
	if(s == "top-right")		{ anchor_out = Anchor_TopRight;		return true; }
	if(s == "left")				{ anchor_out = Anchor_Left;			return true; }
	if(s == "centre")			{ anchor_out = Anchor_Centre;		return true; }
	if(s == "right")			{ anchor_out = Anchor_Right;		return true; }
	if(s == "bottom-left")		{ anchor_out = Anchor_BottomLeft;	return true; }
	if(s == "bottom")			{ anchor_out = Anchor_Bottom;		return true; }
	if(s == "bottom-right")		{ anchor_out = Anchor_BottomRight;	return true; }
	return false;
}


GLUIWidgetRef LuaScriptUIWidget::getGLUIWidget() const
{
	if(image)	return image;
	if(button)	return button;
	if(image_button)	return image_button;
	if(slider)	return slider;
	if(spinbox)		return spinbox;
	if(checkbox)	return checkbox;
	if(dropdown)	return dropdown;
	if(line_edit)	return line_edit;
	if(label)	return label;
	if(grid)	return grid;
	if(window)	return window;
	return GLUIWidgetRef();
}


// Returns the anchor position as fractions of the viewport width and height, from the bottom left.
static Vec2f anchorFractions(LuaScriptUIWidget::Anchor anchor)
{
	switch(anchor)
	{
	case LuaScriptUIWidget::Anchor_TopLeft:		return Vec2f(0.f,  1.f);
	case LuaScriptUIWidget::Anchor_Top:			return Vec2f(0.5f, 1.f);
	case LuaScriptUIWidget::Anchor_TopRight:	return Vec2f(1.f,  1.f);
	case LuaScriptUIWidget::Anchor_Left:		return Vec2f(0.f,  0.5f);
	case LuaScriptUIWidget::Anchor_Centre:		return Vec2f(0.5f, 0.5f);
	case LuaScriptUIWidget::Anchor_Right:		return Vec2f(1.f,  0.5f);
	case LuaScriptUIWidget::Anchor_BottomLeft:	return Vec2f(0.f,  0.f);
	case LuaScriptUIWidget::Anchor_Bottom:		return Vec2f(0.5f, 0.f);
	case LuaScriptUIWidget::Anchor_BottomRight:	return Vec2f(1.f,  0.f);
	}
	return Vec2f(0.5f, 0.5f);
}


static const double HIDDEN_UI_BUTTON_FLASH_DURATION_S = 1.5;
static const double HIDDEN_UI_BUTTON_FLASH_NUM_PULSES = 3;
static const float  HIDDEN_UI_BUTTON_FLASH_MAX_EXTRA_SCALE = 0.4f; // The button grows by up to this fraction of its size while pulsing.
static const Colour3f HIDDEN_UI_BUTTON_FLASH_COLOUR = toLinearSRGB(Colour3f(1.f, 0.6f, 0.1f)); // Orange.  The button is this colour at the peak of each pulse.


LuaScriptUI::LuaScriptUI(GUIClient* gui_client_, const GLUIRef& gl_ui_)
:	gui_client(gui_client_),
	gl_ui(gl_ui_),
	next_widget_id(1),
	ui_visible(true),
	hidden_ui_button_flashing(false)
{
	context_menu = new ScriptUIContextMenu(gui_client_, this, gl_ui_);

	// Button for opening the hidden script UI list.
	{
		GLUIButton::CreateArgs args;
		args.tooltip = "Blocked on-screen items";
		hidden_ui_button = new GLUIButton(*gl_ui, gui_client->resources_dir_path + "/buttons/hidden_ui.png", args);
		hidden_ui_button->handler = this;
		gl_ui->addWidget(hidden_ui_button);
	}
	updateHiddenUIButton();
}


LuaScriptUI::~LuaScriptUI()
{
	hidden_script_ui = nullptr;
	checkRemoveAndDeleteWidget(gl_ui, hidden_ui_button);
	removeAllWidgets();
	context_menu = nullptr; // Removes the menu widgets, if open.
}


// Sets the tooltip on the widget, saying which object and user it is from, makes right-clicking on the widget open the script UI context menu, and adds the widget to the widgets map.
void LuaScriptUI::addWidget(const LuaScriptUIWidgetRef& widget)
{
	std::string tooltip = "Script";
	const WorldObject* ob = widget->sub_lua_script->world_object;
	if(ob)
	{
		tooltip += " from object " + ob->uid.toString();
		if(!ob->creator_name.empty())
			tooltip += " by " + ob->creator_name;
		tooltip += " - right-click for options";
	}

	widget->getGLUIWidget()->setTooltip(UTF8Utils::sanitiseUTF8String(tooltip));

	widget->getGLUIWidget()->handler = this; // For click and value changed events, and contextMenuRequestedEventOccurred() on right-clicks.

	widgets[widget->id] = widget;
}


size_t LuaScriptUI::numWidgetsForScript(SubstrataLuaScript* sub_lua_script) const
{
	size_t num = 0;
	for(auto it = widgets.begin(); it != widgets.end(); ++it)
		if(it->second->sub_lua_script == sub_lua_script)
			num++;
	return num;
}


int LuaScriptUI::createImage(SubstrataLuaScript* sub_lua_script, const URLString& image_URL, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px, const Vec2f& size_px, bool visible)
{
	if(numWidgetsForScript(sub_lua_script) >= MAX_NUM_WIDGETS_PER_SCRIPT)
		throw glare::Exception("Too many UI widgets created by script (max " + toString(MAX_NUM_WIDGETS_PER_SCRIPT) + ")");

	LuaScriptUIWidgetRef widget = new LuaScriptUIWidget();
	widget->id = next_widget_id++;
	widget->sub_lua_script = sub_lua_script;
	widget->parent_id = 0;
	widget->anchor = anchor;
	widget->offset_px = offset_px;
	widget->size_px = size_px;
	widget->image_URL = image_URL;
	widget->image_texture_assigned = false;
	widget->visible = visible;
	widget->value_change_queued = false;

	widget->image = new GLUIImage(*gl_ui, /*tex_path=*/"", /*tooltip=*/"");
	widget->image->setMouseOverColour(Colour3f(1.f)); // No mouse-over highlight
	widget->image->setFixedDimsPx(size_px); // So that a grid containing the image knows its size.
	gl_ui->addWidget(widget->image);

	updateWidgetTransform(*widget);
	updateWidgetDrawn(*widget); // Not drawn until the texture is assigned.

	startDownloadingImageIfNotPresent(image_URL);
	checkAssignImageTexture(*widget);

	addWidget(widget);
	return widget->id;
}


// Start downloading the image if it isn't present already.  Images declared with @resource will usually have been downloaded along with the object already.
void LuaScriptUI::startDownloadingImageIfNotPresent(const URLString& image_URL)
{
	ResourceRef resource = gui_client->resource_manager->getOrCreateResourceForURL(image_URL);
	if(resource->getState() == Resource::State_NotPresent)
	{
		DownloadingResourceInfo info;
		info.texture_params.wrapping = OpenGLTexture::Wrapping_Clamp;
		info.texture_params.allow_compression = false;
		info.pos = gui_client->cam_controller.getFirstPersonPosition();
		info.size_factor = 1.f;
		info.used_by_other = true;
		gui_client->startDownloadingResource(image_URL, gui_client->cam_controller.getFirstPersonPosition().toVec4fPoint(), /*aabb_ws_longest_len=*/1.f, info);
	}
}


int LuaScriptUI::createButton(SubstrataLuaScript* sub_lua_script, const std::string& text, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px, LuaVM::LuaValueRef onClick_ref, bool visible)
{
	if(numWidgetsForScript(sub_lua_script) >= MAX_NUM_WIDGETS_PER_SCRIPT)
		throw glare::Exception("Too many UI widgets created by script (max " + toString(MAX_NUM_WIDGETS_PER_SCRIPT) + ")");

	LuaScriptUIWidgetRef widget = new LuaScriptUIWidget();
	widget->id = next_widget_id++;
	widget->sub_lua_script = sub_lua_script;
	widget->parent_id = 0;
	widget->anchor = anchor;
	widget->offset_px = offset_px;
	widget->size_px = Vec2f(0.f);
	widget->image_texture_assigned = false;
	widget->visible = visible;
	widget->callback_ref = onClick_ref;
	widget->value_change_queued = false;

	GLUITextButton::CreateArgs args;
	widget->button = new GLUITextButton(*gl_ui, UTF8Utils::sanitiseUTF8String(text), args);
	gl_ui->addWidget(widget->button);

	updateWidgetTransform(*widget);
	updateWidgetDrawn(*widget);

	addWidget(widget);
	return widget->id;
}


int LuaScriptUI::createImageButton(SubstrataLuaScript* sub_lua_script, const URLString& image_URL, const Vec2f& size_px, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px,
	LuaVM::LuaValueRef onClick_ref, bool visible)
{
	if(numWidgetsForScript(sub_lua_script) >= MAX_NUM_WIDGETS_PER_SCRIPT)
		throw glare::Exception("Too many UI widgets created by script (max " + toString(MAX_NUM_WIDGETS_PER_SCRIPT) + ")");

	LuaScriptUIWidgetRef widget = new LuaScriptUIWidget();
	widget->id = next_widget_id++;
	widget->sub_lua_script = sub_lua_script;
	widget->parent_id = 0;
	widget->anchor = anchor;
	widget->offset_px = offset_px;
	widget->size_px = size_px;
	widget->image_URL = image_URL;
	widget->image_texture_assigned = false;
	widget->visible = visible;
	widget->callback_ref = onClick_ref;
	widget->value_change_queued = false;

	GLUIButton::CreateArgs args;
	args.sizing_type_x = GLUIWidget::SizingType_FixedSizePx;
	args.sizing_type_y = GLUIWidget::SizingType_FixedSizePx;
	args.fixed_size = size_px;
	widget->image_button = new GLUIButton(*gl_ui, /*tex_path=*/"", args);
	gl_ui->addWidget(widget->image_button);

	updateWidgetTransform(*widget);
	updateWidgetDrawn(*widget); // Not drawn until the texture is assigned.

	startDownloadingImageIfNotPresent(image_URL);
	checkAssignImageTexture(*widget);

	addWidget(widget);
	return widget->id;
}


int LuaScriptUI::createSlider(SubstrataLuaScript* sub_lua_script, double min_value, double max_value, double initial_value, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px, const Vec2f& size_px,
	LuaVM::LuaValueRef onChange_ref, bool visible)
{
	if(numWidgetsForScript(sub_lua_script) >= MAX_NUM_WIDGETS_PER_SCRIPT)
		throw glare::Exception("Too many UI widgets created by script (max " + toString(MAX_NUM_WIDGETS_PER_SCRIPT) + ")");

	LuaScriptUIWidgetRef widget = new LuaScriptUIWidget();
	widget->id = next_widget_id++;
	widget->sub_lua_script = sub_lua_script;
	widget->parent_id = 0;
	widget->anchor = anchor;
	widget->offset_px = offset_px;
	widget->size_px = size_px;
	widget->image_texture_assigned = false;
	widget->visible = visible;
	widget->callback_ref = onChange_ref;
	widget->value_change_queued = false;

	GLUISlider::CreateArgs args;
	args.fixed_size = size_px;
	args.min_value = min_value;
	args.max_value = max_value;
	args.initial_value = myClamp(initial_value, min_value, max_value);
	widget->slider = new GLUISlider(*gl_ui, args);
	gl_ui->addWidget(widget->slider);

	updateWidgetTransform(*widget);
	updateWidgetDrawn(*widget);

	addWidget(widget);
	return widget->id;
}


int LuaScriptUI::createSpinBox(SubstrataLuaScript* sub_lua_script, double min_value, double max_value, double initial_value, double step, int num_decimal_places, float width_px,
	LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px, LuaVM::LuaValueRef onChange_ref, bool visible)
{
	if(numWidgetsForScript(sub_lua_script) >= MAX_NUM_WIDGETS_PER_SCRIPT)
		throw glare::Exception("Too many UI widgets created by script (max " + toString(MAX_NUM_WIDGETS_PER_SCRIPT) + ")");

	LuaScriptUIWidgetRef widget = new LuaScriptUIWidget();
	widget->id = next_widget_id++;
	widget->sub_lua_script = sub_lua_script;
	widget->parent_id = 0;
	widget->anchor = anchor;
	widget->offset_px = offset_px;
	widget->size_px = Vec2f(0.f);
	widget->image_texture_assigned = false;
	widget->visible = visible;
	widget->callback_ref = onChange_ref;
	widget->value_change_queued = false;

	GLUISpinBox::CreateArgs args;
	args.min_value = min_value;
	args.max_value = max_value;
	args.initial_value = initial_value; // Clamped by GLUISpinBox.
	args.step = step;
	args.num_decimal_places = num_decimal_places;
	args.fixed_size.x = width_px;
	widget->spinbox = new GLUISpinBox(*gl_ui, args);
	const int widget_id = widget->id;
	widget->spinbox->handler_func = [this, widget_id](GLUISpinBoxValueChangedEvent& /*event*/) { queueValueChanged(widget_id); };
	gl_ui->addWidget(widget->spinbox);

	updateWidgetTransform(*widget);
	updateWidgetDrawn(*widget);

	addWidget(widget);
	return widget->id;
}


int LuaScriptUI::createCheckBox(SubstrataLuaScript* sub_lua_script, bool checked, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px, LuaVM::LuaValueRef onChange_ref, bool visible)
{
	if(numWidgetsForScript(sub_lua_script) >= MAX_NUM_WIDGETS_PER_SCRIPT)
		throw glare::Exception("Too many UI widgets created by script (max " + toString(MAX_NUM_WIDGETS_PER_SCRIPT) + ")");

	LuaScriptUIWidgetRef widget = new LuaScriptUIWidget();
	widget->id = next_widget_id++;
	widget->sub_lua_script = sub_lua_script;
	widget->parent_id = 0;
	widget->anchor = anchor;
	widget->offset_px = offset_px;
	widget->size_px = Vec2f(0.f);
	widget->image_texture_assigned = false;
	widget->visible = visible;
	widget->callback_ref = onChange_ref;
	widget->value_change_queued = false;

	GLUICheckBox::CreateArgs args;
	args.checked = checked;
	widget->checkbox = new GLUICheckBox(*gl_ui, gui_client->base_dir_path + "/data/gl_data/ui/tick.png", args);
	gl_ui->addWidget(widget->checkbox);

	updateWidgetTransform(*widget);
	updateWidgetDrawn(*widget);

	addWidget(widget);
	return widget->id;
}


int LuaScriptUI::createDropDown(SubstrataLuaScript* sub_lua_script, const std::vector<std::string>& options, const std::string& initial_value, float width_px, LuaScriptUIWidget::Anchor anchor,
	const Vec2f& offset_px, LuaVM::LuaValueRef onChange_ref, bool visible)
{
	if(numWidgetsForScript(sub_lua_script) >= MAX_NUM_WIDGETS_PER_SCRIPT)
		throw glare::Exception("Too many UI widgets created by script (max " + toString(MAX_NUM_WIDGETS_PER_SCRIPT) + ")");

	LuaScriptUIWidgetRef widget = new LuaScriptUIWidget();
	widget->id = next_widget_id++;
	widget->sub_lua_script = sub_lua_script;
	widget->parent_id = 0;
	widget->anchor = anchor;
	widget->offset_px = offset_px;
	widget->size_px = Vec2f(0.f);
	widget->image_texture_assigned = false;
	widget->visible = visible;
	widget->callback_ref = onChange_ref;
	widget->value_change_queued = false;

	GLUIDropDownList::CreateArgs args;
	args.options.resize(options.size());
	for(size_t i=0; i<options.size(); ++i)
		args.options[i] = UTF8Utils::sanitiseUTF8String(options[i]);
	args.initial_value = UTF8Utils::sanitiseUTF8String(initial_value);
	args.fixed_size.x = width_px;
	widget->dropdown = new GLUIDropDownList(*gl_ui, args);
	const int widget_id = widget->id;
	widget->dropdown->handler_func = [this, widget_id](GLUIDropDownListValueChangedEvent& /*event*/) { queueValueChanged(widget_id); };
	gl_ui->addWidget(widget->dropdown);

	updateWidgetTransform(*widget);
	updateWidgetDrawn(*widget);

	addWidget(widget);
	return widget->id;
}


int LuaScriptUI::createLineEdit(SubstrataLuaScript* sub_lua_script, const std::string& text, float width_px, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px,
	LuaVM::LuaValueRef onChange_ref, LuaVM::LuaValueRef onEnter_ref, bool visible)
{
	if(numWidgetsForScript(sub_lua_script) >= MAX_NUM_WIDGETS_PER_SCRIPT)
		throw glare::Exception("Too many UI widgets created by script (max " + toString(MAX_NUM_WIDGETS_PER_SCRIPT) + ")");

	LuaScriptUIWidgetRef widget = new LuaScriptUIWidget();
	widget->id = next_widget_id++;
	widget->sub_lua_script = sub_lua_script;
	widget->parent_id = 0;
	widget->anchor = anchor;
	widget->offset_px = offset_px;
	widget->size_px = Vec2f(0.f);
	widget->image_texture_assigned = false;
	widget->visible = visible;
	widget->callback_ref = onChange_ref;
	widget->enter_callback_ref = onEnter_ref;
	widget->value_change_queued = false;
	widget->last_reported_text = UTF8Utils::sanitiseUTF8String(text);

	GLUILineEdit::CreateArgs args;
	args.fixed_size.x = width_px;
	widget->line_edit = new GLUILineEdit(*gl_ui, args);
	widget->line_edit->setText(widget->last_reported_text);
	const int widget_id = widget->id;
	widget->line_edit->on_text_changed = [this, widget_id]() { lineEditTextChanged(widget_id); };
	widget->line_edit->on_enter_pressed = [this, widget_id]() { entered_line_edit_ids.push_back(widget_id); };
	gl_ui->addWidget(widget->line_edit);

	updateWidgetTransform(*widget);
	updateWidgetDrawn(*widget);

	addWidget(widget);
	return widget->id;
}


int LuaScriptUI::createLabel(SubstrataLuaScript* sub_lua_script, const std::string& text, const Colour3f& text_colour_linear, const Colour3f& background_colour_linear, float background_alpha, int font_size_px,
	const Vec2f& size_px, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px, bool visible)
{
	if(numWidgetsForScript(sub_lua_script) >= MAX_NUM_WIDGETS_PER_SCRIPT)
		throw glare::Exception("Too many UI widgets created by script (max " + toString(MAX_NUM_WIDGETS_PER_SCRIPT) + ")");

	LuaScriptUIWidgetRef widget = new LuaScriptUIWidget();
	widget->id = next_widget_id++;
	widget->sub_lua_script = sub_lua_script;
	widget->parent_id = 0;
	widget->anchor = anchor;
	widget->offset_px = offset_px;
	widget->size_px = Vec2f(0.f);
	widget->image_texture_assigned = false;
	widget->visible = visible;
	widget->value_change_queued = false;

	GLUITextView::CreateArgs args;
	args.text_colour = text_colour_linear;
	args.background_colour = background_colour_linear;
	args.background_alpha = background_alpha;
	args.font_size_px = font_size_px;
	// An empty label with a size is a plain rectangle of exactly that size, so has no padding.  Otherwise use the same padding to font size ratio as the GLUITextView default.
	args.padding_px = (text.empty() && (size_px.x > 0 || size_px.y > 0)) ? 0 : (int)std::ceil(font_size_px * 0.6f);
	args.fixed_size = size_px; // The label is at least this size, and big enough to fit the text.  Zero means size the label to fit the text.
	args.text_selectable = false; // So that clicking on a label doesn't take keyboard focus.
	widget->label = new GLUITextView(*gl_ui, UTF8Utils::sanitiseUTF8String(text), args);
	gl_ui->addWidget(widget->label);

	updateWidgetTransform(*widget);
	updateWidgetDrawn(*widget);

	addWidget(widget);
	return widget->id;
}


int LuaScriptUI::createGrid(SubstrataLuaScript* sub_lua_script, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px, const Colour3f& background_colour_linear, float background_alpha,
	float interior_padding_px, float edge_padding_px, bool visible)
{
	if(numWidgetsForScript(sub_lua_script) >= MAX_NUM_WIDGETS_PER_SCRIPT)
		throw glare::Exception("Too many UI widgets created by script (max " + toString(MAX_NUM_WIDGETS_PER_SCRIPT) + ")");

	LuaScriptUIWidgetRef widget = new LuaScriptUIWidget();
	widget->id = next_widget_id++;
	widget->sub_lua_script = sub_lua_script;
	widget->parent_id = 0;
	widget->anchor = anchor;
	widget->offset_px = offset_px;
	widget->size_px = Vec2f(0.f);
	widget->image_texture_assigned = false;
	widget->visible = visible;
	widget->value_change_queued = false;

	GLUIGridContainer::CreateArgs args;
	args.background_colour = background_colour_linear;
	args.background_alpha = background_alpha;
	args.interior_cell_x_padding_px = interior_padding_px;
	args.interior_cell_y_padding_px = interior_padding_px;
	args.exterior_cell_x_padding_px = edge_padding_px;
	args.exterior_cell_y_padding_px = edge_padding_px;
	args.background_consumes_events = background_alpha > 0; // Clicks on a visible background shouldn't go through to the world.
	widget->grid = new GLUIGridContainer(*gl_ui, args);
	gl_ui->addWidget(widget->grid);

	updateWidgetTransform(*widget);
	updateWidgetDrawn(*widget);

	addWidget(widget);
	return widget->id;
}


int LuaScriptUI::createWindow(SubstrataLuaScript* sub_lua_script, const std::string& title, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px, const Colour3f& background_colour_linear,
	float background_alpha, float padding_px, LuaVM::LuaValueRef onClose_ref, bool visible)
{
	if(numWidgetsForScript(sub_lua_script) >= MAX_NUM_WIDGETS_PER_SCRIPT)
		throw glare::Exception("Too many UI widgets created by script (max " + toString(MAX_NUM_WIDGETS_PER_SCRIPT) + ")");

	LuaScriptUIWidgetRef widget = new LuaScriptUIWidget();
	widget->id = next_widget_id++;
	widget->sub_lua_script = sub_lua_script;
	widget->parent_id = 0;
	widget->anchor = anchor;
	widget->offset_px = offset_px;
	widget->size_px = Vec2f(0.f);
	widget->image_texture_assigned = false;
	widget->visible = visible;
	widget->callback_ref = onClose_ref;
	widget->value_change_queued = false;

	GLUIWindow::CreateArgs args;
	args.title = UTF8Utils::sanitiseUTF8String(title);
	args.background_colour = background_colour_linear;
	args.background_alpha = background_alpha;
	args.padding_px = padding_px;
	widget->window = new GLUIWindow(*gl_ui, args);
	const int widget_id = widget->id;
	widget->window->on_close_window = [this, widget_id]() { closed_window_ids.push_back(widget_id); };
	gl_ui->addWidget(widget->window);

	updateWidgetTransform(*widget);
	updateWidgetDrawn(*widget);

	addWidget(widget);
	return widget->id;
}


LuaScriptUIWidget* LuaScriptUI::getWidget(SubstrataLuaScript* sub_lua_script, int id)
{
	auto res = widgets.find(id);
	if(res == widgets.end() || (res->second->sub_lua_script != sub_lua_script)) // Scripts can only access their own widgets.
		return NULL;
	return res->second.ptr();
}


void LuaScriptUI::destroyWidget(SubstrataLuaScript* sub_lua_script, int id)
{
	auto res = widgets.find(id);
	if(res == widgets.end() || (res->second->sub_lua_script != sub_lua_script))
		return;

	removeFromParent(*res->second);
	destroyWidgetAndContents(id);
}


void LuaScriptUI::destroyWidgetAndContents(int id)
{
	std::vector<int> child_ids;
	for(auto it = widgets.begin(); it != widgets.end(); ++it)
		if(it->second->parent_id == id)
			child_ids.push_back(it->first);

	for(size_t i=0; i<child_ids.size(); ++i)
		destroyWidgetAndContents(child_ids[i]);

	auto res = widgets.find(id);
	if(res != widgets.end())
	{
		removeWidgetFromUI(*res->second);
		widgets.erase(res);
	}
}


void LuaScriptUI::setWidgetVisible(LuaScriptUIWidget& widget, bool visible)
{
	widget.visible = visible;
	updateWidgetDrawn(widget);
}


void LuaScriptUI::setWidgetOffset(LuaScriptUIWidget& widget, const Vec2f& offset_px)
{
	widget.offset_px = offset_px;
	if(widget.parent_id == 0) // Widgets in a grid or window are positioned by it.
		updateWidgetTransform(widget);
}


void LuaScriptUI::setWidgetText(LuaScriptUIWidget& widget, const std::string& text)
{
	if(widget.button)
	{
		widget.button->setText(UTF8Utils::sanitiseUTF8String(text));
		updateWidgetTransform(widget); // The button size may have changed.
	}
	if(widget.label)
	{
		widget.label->setText(UTF8Utils::sanitiseUTF8String(text));
		updateWidgetTransform(widget); // The label size may have changed.
	}
	if(widget.line_edit)
	{
		widget.last_reported_text = UTF8Utils::sanitiseUTF8String(text); // So that onChange isn't called for this change.
		widget.line_edit->setText(widget.last_reported_text);
	}
}


void LuaScriptUI::setNumberValue(LuaScriptUIWidget& widget, double value)
{
	if(widget.slider)
		widget.slider->setValueNoEvent(value); // Clamps to the slider range.
	if(widget.spinbox)
		widget.spinbox->setValueNoEvent(value); // Clamps to the spin box range.
}


void LuaScriptUI::setCheckBoxChecked(LuaScriptUIWidget& widget, bool checked)
{
	if(widget.checkbox)
		widget.checkbox->setChecked(checked); // Doesn't send an event.
}


void LuaScriptUI::setDropDownValue(LuaScriptUIWidget& widget, const std::string& value)
{
	if(widget.dropdown)
	{
		const std::string sanitised_value = UTF8Utils::sanitiseUTF8String(value); // The options were sanitised when the drop-down list was created.
		const std::vector<std::string>& options = widget.dropdown->getOptions();
		if(std::find(options.begin(), options.end(), sanitised_value) == options.end())
			throw glare::Exception("UI drop-down list: value '" + value.substr(0, 100) + "' is not one of the options");

		widget.dropdown->setValueNoEvent(sanitised_value);
	}
}


void LuaScriptUI::setGridCell(LuaScriptUIWidget& grid, int cell_x, int cell_y, LuaScriptUIWidget* child)
{
	if(!grid.grid)
		throw glare::Exception("UI widget: only grids have cells");
	if(cell_x < 0 || cell_y < 0 || cell_x >= (int)MAX_GRID_SIZE || cell_y >= (int)MAX_GRID_SIZE)
		throw glare::Exception("UI grid: too many rows or columns (max " + toString(MAX_GRID_SIZE) + ")");

	if(child)
	{
		if(child->window)
			throw glare::Exception("UI grid: can't put a window in a grid");

		for(LuaScriptUIWidget* w = &grid; w; w = getParent(*w))
			if(w == child)
				throw glare::Exception("UI grid: can't put a grid inside itself");

		removeFromParent(*child);
	}

	// Remove any widget already in the cell from the grid.  It goes back to being positioned by its own anchor and offset.
	if(((size_t)cell_x < grid.grid->cell_widgets.getWidth()) && ((size_t)cell_y < grid.grid->cell_widgets.getHeight()))
	{
		const GLUIWidgetRef occupant = grid.grid->cell_widgets.elem(cell_x, cell_y);
		if(occupant)
		{
			for(auto it = widgets.begin(); it != widgets.end(); ++it)
			{
				LuaScriptUIWidget* w = it->second.ptr();
				if(w->getGLUIWidget() == occupant)
				{
					removeFromParent(*w);
					updateWidgetTransform(*w);
					updateWidgetDrawn(*w);
					break;
				}
			}
		}
	}

	if(child)
	{
		grid.grid->setCellWidget(cell_x, cell_y, child->getGLUIWidget());
		child->parent_id = grid.id;
		updateWidgetDrawn(*child);
	}

	updateWidgetTransform(grid);
}


void LuaScriptUI::addGridRow(LuaScriptUIWidget& grid, LuaScriptUIWidget& child)
{
	if(!grid.grid)
		throw glare::Exception("UI widget: only grids have cells");

	setGridCell(grid, /*cell_x=*/0, /*cell_y=*/(int)grid.grid->cell_widgets.getHeight(), &child);
}


void LuaScriptUI::setWindowBody(LuaScriptUIWidget& window, LuaScriptUIWidget* body)
{
	if(!window.window)
		throw glare::Exception("UI widget: only windows have a body");

	if(body)
	{
		if(body->window)
			throw glare::Exception("UI window: can't put a window in a window");

		removeFromParent(*body);
	}

	// Remove the current body widget, if any.  It goes back to being positioned by its own anchor and offset.
	for(auto it = widgets.begin(); it != widgets.end(); ++it)
	{
		LuaScriptUIWidget* w = it->second.ptr();
		if((w->parent_id == window.id) && (w != body))
		{
			removeFromParent(*w);
			updateWidgetTransform(*w);
			updateWidgetDrawn(*w);
			break;
		}
	}

	if(body)
	{
		window.window->setBodyWidget(body->getGLUIWidget());
		body->parent_id = window.id;
		updateWidgetDrawn(*body);
	}

	updateWidgetTransform(window);
}


LuaScriptUIWidget* LuaScriptUI::getParent(const LuaScriptUIWidget& widget)
{
	if(widget.parent_id == 0)
		return NULL;
	auto res = widgets.find(widget.parent_id);
	return (res != widgets.end()) ? res->second.ptr() : NULL;
}


LuaScriptUIWidget& LuaScriptUI::getRootWidget(LuaScriptUIWidget& widget)
{
	LuaScriptUIWidget* w = &widget;
	while(LuaScriptUIWidget* parent = getParent(*w))
		w = parent;
	return *w;
}


bool LuaScriptUI::isEffectivelyVisible(const LuaScriptUIWidget& widget)
{
	if(!ui_visible)
		return false;
	for(const LuaScriptUIWidget* w = &widget; w; w = getParent(*w))
		if(!w->visible)
			return false;
	return true;
}


void LuaScriptUI::removeFromParent(LuaScriptUIWidget& widget)
{
	LuaScriptUIWidget* parent = getParent(widget);
	widget.parent_id = 0;
	if(parent)
	{
		if(parent->grid)
			parent->grid->removeWidget(widget.getGLUIWidget());
		else if(parent->window)
			parent->window->setBodyWidget(NULL);
		updateWidgetTransform(*parent); // The grid or window size may have changed.
	}
}


void LuaScriptUI::removeWidgetsForScript(SubstrataLuaScript* sub_lua_script)
{
	for(auto it = widgets.begin(); it != widgets.end();)
	{
		if(it->second->sub_lua_script == sub_lua_script)
		{
			removeWidgetFromUI(*it->second);
			it = widgets.erase(it);
		}
		else
			++it;
	}
}


void LuaScriptUI::removeAllWidgets()
{
	context_menu->close();

	for(auto it = widgets.begin(); it != widgets.end(); ++it)
		removeWidgetFromUI(*it->second);
	widgets.clear();
	clicked_widget_ids.clear();
	changed_value_ids.clear();
	entered_line_edit_ids.clear();
	closed_window_ids.clear();
}


void LuaScriptUI::removeWidgetFromUI(LuaScriptUIWidget& widget)
{
	checkRemoveAndDeleteWidget(gl_ui, widget.image);
	checkRemoveAndDeleteWidget(gl_ui, widget.button);
	checkRemoveAndDeleteWidget(gl_ui, widget.image_button);
	checkRemoveAndDeleteWidget(gl_ui, widget.slider);
	checkRemoveAndDeleteWidget(gl_ui, widget.spinbox);
	checkRemoveAndDeleteWidget(gl_ui, widget.checkbox);
	checkRemoveAndDeleteWidget(gl_ui, widget.dropdown);
	checkRemoveAndDeleteWidget(gl_ui, widget.line_edit);
	checkRemoveAndDeleteWidget(gl_ui, widget.label);
	checkRemoveAndDeleteWidget(gl_ui, widget.grid);
	checkRemoveAndDeleteWidget(gl_ui, widget.window);

	if(widget.callback_ref.ref != LUA_NOREF)
	{
		widget.sub_lua_script->freeRefToFunction(widget.callback_ref);
		widget.callback_ref = LuaVM::LuaValueRef();
	}
	if(widget.enter_callback_ref.ref != LUA_NOREF)
	{
		widget.sub_lua_script->freeRefToFunction(widget.enter_callback_ref);
		widget.enter_callback_ref = LuaVM::LuaValueRef();
	}
}


void LuaScriptUI::clearKeyboardFocusIfHeldBy(const LuaScriptUIWidget& widget)
{
	const GLUIWidgetRef glui_widget = widget.getGLUIWidget();
	if(glui_widget && (gl_ui->getKeyboardFocusWidget().ptr() == glui_widget.ptr()))
		gl_ui->setKeyboardFocusWidget(NULL);
}


void LuaScriptUI::think()
{
	if(hidden_script_ui)
	{
		hidden_script_ui->think(); // Does any unhides from the window, which re-run scripts.
		if(hidden_script_ui->close_soon)
			hidden_script_ui = nullptr;
	}

	if(hidden_ui_button_flashing)
	{
		if(hidden_ui_button_flash_timer.elapsed() >= HIDDEN_UI_BUTTON_FLASH_DURATION_S)
			hidden_ui_button_flashing = false;
		updateHiddenUIButton(); // Sets the button size for the current point in the flash, or back to normal when finished.
	}

	if((!clicked_widget_ids.empty() || !changed_value_ids.empty() || !entered_line_edit_ids.empty() || !closed_window_ids.empty()) && gui_client->world_state)
	{
		std::vector<int> closed_ids;
		closed_ids.swap(closed_window_ids);

		std::vector<int> clicked_ids;
		clicked_ids.swap(clicked_widget_ids);

		std::vector<int> changed_ids;
		changed_ids.swap(changed_value_ids);

		std::vector<int> entered_ids;
		entered_ids.swap(entered_line_edit_ids);

		WorldStateLock lock(gui_client->world_state->mutex);

		for(size_t i=0; i<clicked_ids.size(); ++i)
		{
			// Look up the widget again, as an earlier callback may have destroyed it.
			auto res = widgets.find(clicked_ids[i]);
			if(res != widgets.end())
			{
				LuaScriptUIWidgetRef widget = res->second; // Hold a reference, as the callback may destroy the widget.
				if(widget->callback_ref.ref != LUA_NOREF)
					widget->sub_lua_script->doOnUIEvent(widget->callback_ref, gui_client->client_avatar_uid, lock);
			}
		}

		for(size_t i=0; i<changed_ids.size(); ++i)
		{
			auto res = widgets.find(changed_ids[i]);
			if(res != widgets.end())
			{
				LuaScriptUIWidgetRef widget = res->second;
				widget->value_change_queued = false;
				if(widget->callback_ref.ref != LUA_NOREF)
				{
					if(widget->slider)
						widget->sub_lua_script->doOnUIValueChangedEvent(widget->callback_ref, widget->slider->getValue(), gui_client->client_avatar_uid, lock);
					else if(widget->spinbox)
						widget->sub_lua_script->doOnUIValueChangedEvent(widget->callback_ref, widget->spinbox->getValue(), gui_client->client_avatar_uid, lock);
					else if(widget->checkbox)
						widget->sub_lua_script->doOnUIBoolValueChangedEvent(widget->callback_ref, widget->checkbox->isChecked(), gui_client->client_avatar_uid, lock);
					else if(widget->dropdown)
						widget->sub_lua_script->doOnUIStringValueChangedEvent(widget->callback_ref, widget->dropdown->getCurrentValue(), gui_client->client_avatar_uid, lock);
					else if(widget->line_edit)
						widget->sub_lua_script->doOnUIStringValueChangedEvent(widget->callback_ref, widget->line_edit->getText(), gui_client->client_avatar_uid, lock);
				}
			}
		}

		// Run onEnter callbacks after onChange callbacks, so onChange has seen the final text.
		for(size_t i=0; i<entered_ids.size(); ++i)
		{
			auto res = widgets.find(entered_ids[i]);
			if(res != widgets.end())
			{
				LuaScriptUIWidgetRef widget = res->second;
				if(widget->line_edit && (widget->enter_callback_ref.ref != LUA_NOREF))
					widget->sub_lua_script->doOnUIStringValueChangedEvent(widget->enter_callback_ref, widget->line_edit->getText(), gui_client->client_avatar_uid, lock);
			}
		}

		// Hide closed windows, then call their onClose functions.  A callback can show the window again.
		for(size_t i=0; i<closed_ids.size(); ++i)
		{
			auto res = widgets.find(closed_ids[i]);
			if(res != widgets.end())
			{
				LuaScriptUIWidgetRef widget = res->second;
				if(widget->window && widget->visible)
				{
					setWidgetVisible(*widget, false);
					if(widget->callback_ref.ref != LUA_NOREF)
						widget->sub_lua_script->doOnUIEvent(widget->callback_ref, gui_client->client_avatar_uid, lock);
				}
			}
		}
	}
}


void LuaScriptUI::viewportResized()
{
	context_menu->close();

	if(hidden_script_ui)
		hidden_script_ui->viewportResized();

	updateHiddenUIButton();

	for(auto it = widgets.begin(); it != widgets.end(); ++it)
		if(it->second->parent_id == 0) // Widgets in grids are positioned by updateWidgetTransform() on the outermost grid.
			updateWidgetTransform(*it->second);
}


void LuaScriptUI::setVisible(bool visible)
{
	if(!visible)
		context_menu->close();

	ui_visible = visible;
	for(auto it = widgets.begin(); it != widgets.end(); ++it)
		updateWidgetDrawn(*it->second);

	updateHiddenUIButton();
}


// Called when the hidden objects or creators change.
void LuaScriptUI::hiddenListsChanged()
{
	if(hidden_script_ui)
		hidden_script_ui->listChanged();

	if(gui_client->ui_interface)
		gui_client->ui_interface->scriptUIBlockedListsChanged();
}


// The button and the Builder AI expand button are placed symmetrically about the middle of the right edge of the viewport, with this one below.
// While flashing, the button pulses in size about its centre.
void LuaScriptUI::updateHiddenUIButton()
{
	const float button_w = gl_ui->getUIWidthForDevIndepPixelWidth(40); // Same size and margin as the Builder AI button.
	const float margin   = gl_ui->getUIWidthForDevIndepPixelWidth(10);

	float pulse = 0.f; // In [0, 1].  0 when not flashing.
	if(hidden_ui_button_flashing)
	{
		const double t = hidden_ui_button_flash_timer.elapsed();
		pulse = (float)std::fabs(std::sin(t * Maths::pi<double>() * HIDDEN_UI_BUTTON_FLASH_NUM_PULSES / HIDDEN_UI_BUTTON_FLASH_DURATION_S));
	}

	const Vec2f centre(1.f - margin - button_w / 2, -margin / 2 - button_w / 2);
	const float w = button_w * (1.f + HIDDEN_UI_BUTTON_FLASH_MAX_EXTRA_SCALE * pulse);
	hidden_ui_button->setPosAndDims(centre - Vec2f(w / 2), Vec2f(w));

	// The button colour multiplies the icon texture, so the white parts of the icon take on the flash colour.
	hidden_ui_button->setButtonColour(Colour3f(1.f) * (1.f - pulse) + HIDDEN_UI_BUTTON_FLASH_COLOUR * pulse);

	hidden_ui_button->setVisible(ui_visible);
}


// Flashes the hidden UI button, to show the user where blocked items can be found.
void LuaScriptUI::flashHiddenUIButton()
{
	hidden_ui_button_flashing = true;
	hidden_ui_button_flash_timer.reset();
}


// Right-clicking on a script widget opens the script UI context menu for the widget's object, at the mouse position.
void LuaScriptUI::contextMenuRequestedEventOccurred(GLUICallbackContextMenuEvent& event)
{
	for(auto it = widgets.begin(); it != widgets.end(); ++it)
	{
		LuaScriptUIWidget* widget = it->second.ptr();
		if(widget->getGLUIWidget().ptr() == event.widget)
		{
			const WorldObject* ob = widget->sub_lua_script->world_object;
			if(ob)
			{
				context_menu->open(ob->uid, ob->creator_id, ob->creator_name, event.ui_coords);
				event.accepted = true;
			}
			return;
		}
	}
}


bool LuaScriptUI::isUIHiddenForScript(const SubstrataLuaScript* sub_lua_script) const
{
	const WorldObject* ob = sub_lua_script->world_object;
	return ob && ((hidden_objects.count(ob->uid) > 0) || (hidden_creators.count(ob->creator_id) > 0));
}


void LuaScriptUI::hideUIForObject(const UID& ob_uid, const std::string& creator_name)
{
	HiddenScriptUIEntry entry;
	entry.hidden_time = TimeStamp::currentTime();
	entry.creator_name = creator_name;

	hidden_objects[ob_uid] = entry;
	saveHiddenEntry(hiddenObjectsMapPath(), ob_uid.toString(), entry);

	removeWidgetsForHiddenScripts();

	flashHiddenUIButton(); // Show the user where the blocked object went.

	hiddenListsChanged();
}


void LuaScriptUI::hideUIForCreator(const UserID& creator_id, const std::string& creator_name)
{
	HiddenScriptUIEntry entry;
	entry.hidden_time = TimeStamp::currentTime();
	entry.creator_name = creator_name;

	hidden_creators[creator_id] = entry;
	saveHiddenEntry(hiddenCreatorsMapPath(), creator_id.toString(), entry);

	removeWidgetsForHiddenScripts();

	flashHiddenUIButton(); // Show the user where the blocked user went.

	hiddenListsChanged();
}


void LuaScriptUI::unhideUIForObject(const UID& ob_uid)
{
	const std::vector<WorldObjectRef> obs_with_hidden_UI = getObjectsWithHiddenUI();

	hidden_objects.erase(ob_uid);
	gui_client->settings->removeMapEntry(hiddenObjectsMapPath(), ob_uid.toString());

	rerunScriptsWithUnhiddenUI(obs_with_hidden_UI);

	hiddenListsChanged();
}


void LuaScriptUI::unhideUIForCreator(const UserID& creator_id)
{
	const std::vector<WorldObjectRef> obs_with_hidden_UI = getObjectsWithHiddenUI();

	hidden_creators.erase(creator_id);
	gui_client->settings->removeMapEntry(hiddenCreatorsMapPath(), creator_id.toString());

	rerunScriptsWithUnhiddenUI(obs_with_hidden_UI);

	hiddenListsChanged();
}


void LuaScriptUI::openHiddenScriptUI()
{
	if(!hidden_script_ui)
		hidden_script_ui = new HiddenScriptUI(this, gl_ui);
}


// Returns the loaded objects with Lua scripts that UI is hidden for.
std::vector<WorldObjectRef> LuaScriptUI::getObjectsWithHiddenUI()
{
	std::vector<WorldObjectRef> obs;
	if(gui_client->world_state)
	{
		WorldStateLock lock(gui_client->world_state->mutex);
		for(auto it = gui_client->world_state->objects.valuesBegin(); it != gui_client->world_state->objects.valuesEnd(); ++it)
		{
			WorldObject* ob = it.getValue().ptr();
			if(ob->sub_lua_script && isUIHiddenForScript(ob->sub_lua_script.ptr()))
				obs.push_back(ob);
		}
	}
	return obs;
}


// Re-runs the scripts of the objects that UI is no longer hidden for, so that the scripts create their widgets again.
// An object can still be hidden, e.g. if its creator is hidden as well as the object.
void LuaScriptUI::rerunScriptsWithUnhiddenUI(const std::vector<WorldObjectRef>& obs)
{
	for(size_t i=0; i<obs.size(); ++i)
	{
		WorldObject* ob = obs[i].ptr();
		if(ob->sub_lua_script && !isUIHiddenForScript(ob->sub_lua_script.ptr()))
		{
			BitUtils::setBit(ob->changed_flags, WorldObject::SCRIPT_CHANGED); // So that reloadChangedScriptForObject() runs the script again.
			gui_client->reloadChangedScriptForObject(ob);
		}
	}
}


// Writes the entry fields to map_path/key/ in the settings store.
void LuaScriptUI::saveHiddenEntry(const std::vector<std::string>& map_path, const std::string& key, const HiddenScriptUIEntry& entry)
{
	const std::string entry_path = StringUtils::join(map_path, "/") + "/" + key;
	gui_client->settings->setDoubleValue(entry_path + "/hidden_time", (double)entry.hidden_time.time); // Stored as a double, as int settings are 32-bit.
	gui_client->settings->setStringValue(entry_path + "/creator_name", entry.creator_name);
}


// Reads the entry fields from map_path/key/ in the settings store.
HiddenScriptUIEntry LuaScriptUI::loadHiddenEntry(const std::vector<std::string>& map_path, const std::string& key)
{
	const std::string entry_path = StringUtils::join(map_path, "/") + "/" + key;

	HiddenScriptUIEntry entry;
	entry.hidden_time  = TimeStamp((uint64)gui_client->settings->getDoubleValue(entry_path + "/hidden_time", /*default value=*/0.0));
	entry.creator_name = gui_client->settings->getStringValue(entry_path + "/creator_name", /*default value=*/"");
	return entry;
}


std::vector<std::string> LuaScriptUI::hiddenObjectsMapPath() const
{
	return std::vector<std::string>({"hidden_list", "worlds", server_hostname, "hidden_objects"});
}


std::vector<std::string> LuaScriptUI::hiddenCreatorsMapPath() const
{
	return std::vector<std::string>({"hidden_list", "worlds", server_hostname, "hidden_creators"});
}


void LuaScriptUI::loadHiddenLists(const std::string& server_hostname_)
{
	server_hostname = server_hostname_;
	hidden_objects.clear();
	hidden_creators.clear();

	if(!server_hostname.empty()) // server_hostname is empty if not connected to a server yet.
	{
		// Load hidden objects
		const std::vector<std::string> objects_map_path = hiddenObjectsMapPath();
		const std::vector<std::string> object_keys = gui_client->settings->getMapKeys(objects_map_path);
		for(size_t i=0; i<object_keys.size(); ++i)
		{
			try
			{
				const UID ob_uid(stringToUInt64(object_keys[i]));
				hidden_objects[ob_uid] = loadHiddenEntry(objects_map_path, object_keys[i]);
			}
			catch(glare::Exception& e)
			{
				conPrint("Warning: invalid hidden object UID '" + object_keys[i] + "' in settings: " + e.what());
			}
		}

		// Load hidden creators
		const std::vector<std::string> creators_map_path = hiddenCreatorsMapPath();
		const std::vector<std::string> creator_keys = gui_client->settings->getMapKeys(creators_map_path);
		for(size_t i=0; i<creator_keys.size(); ++i)
		{
			try
			{
				const uint64 user_id = stringToUInt64(creator_keys[i]);
				if(user_id > std::numeric_limits<uint32>::max())
					throw glare::Exception("user ID out of range");
				hidden_creators[UserID((uint32)user_id)] = loadHiddenEntry(creators_map_path, creator_keys[i]);
			}
			catch(glare::Exception& e)
			{
				conPrint("Warning: invalid hidden creator user ID '" + creator_keys[i] + "' in settings: " + e.what());
			}
		}
	}

	hiddenListsChanged();
}


// Removes the widgets of scripts that UI is hidden for.  The scripts' widget handles then act as dummy widgets, as for widgets that have been destroyed.
void LuaScriptUI::removeWidgetsForHiddenScripts()
{
	for(auto it = widgets.begin(); it != widgets.end();)
	{
		if(isUIHiddenForScript(it->second->sub_lua_script))
		{
			removeWidgetFromUI(*it->second);
			it = widgets.erase(it);
		}
		else
			++it;
	}
}


void LuaScriptUI::eventOccurred(GLUICallbackEvent& event)
{
	if(event.widget == hidden_ui_button.ptr())
	{
		// Toggle the hidden script UI list
		if(hidden_script_ui)
			hidden_script_ui = nullptr;
		else
			openHiddenScriptUI();
		event.accepted = true;
		return;
	}

	for(auto it = widgets.begin(); it != widgets.end(); ++it)
	{
		LuaScriptUIWidget* widget = it->second.ptr();
		if((widget->button.ptr() == event.widget) || (widget->image_button.ptr() == event.widget))
		{
			clicked_widget_ids.push_back(it->first);
			event.accepted = true;
			return;
		}
		if(widget->checkbox.ptr() == event.widget) // Checkbox was toggled
		{
			queueValueChanged(it->first);
			event.accepted = true;
			return;
		}
	}
}


void LuaScriptUI::sliderValueChangedEventOccurred(GLUISliderValueChangedEvent& event)
{
	for(auto it = widgets.begin(); it != widgets.end(); ++it)
	{
		LuaScriptUIWidget* widget = it->second.ptr();
		if(widget->slider.ptr() == event.widget)
		{
			queueValueChanged(it->first);
			event.accepted = true;
			return;
		}
	}
}


// GLUILineEdit calls on_text_changed for every key press, including ones that don't change the text, such as arrow keys.  So check the text has changed before queuing onChange.
void LuaScriptUI::lineEditTextChanged(int widget_id)
{
	auto res = widgets.find(widget_id);
	if(res != widgets.end() && res->second->line_edit && (res->second->line_edit->getText() != res->second->last_reported_text))
	{
		res->second->last_reported_text = res->second->line_edit->getText();
		queueValueChanged(widget_id);
	}
}


void LuaScriptUI::queueValueChanged(int widget_id)
{
	auto res = widgets.find(widget_id);
	if(res != widgets.end() && !res->second->value_change_queued)
	{
		res->second->value_change_queued = true;
		changed_value_ids.push_back(widget_id);
	}
}


void LuaScriptUI::updateWidgetTransform(LuaScriptUIWidget& widget_in_tree)
{
	LuaScriptUIWidget& widget = getRootWidget(widget_in_tree); // Widgets in a grid are positioned by the grid, so lay out and position the outermost grid.

	const float max_y = gl_ui->getViewportMinMaxY();
	const Vec2f fractions = anchorFractions(widget.anchor);
	const Vec2f anchor_pos(-1.f + 2.f * fractions.x, -max_y + 2.f * max_y * fractions.y);
	const Vec2f offset = gl_ui->getUIWidthForDevIndepPixelWidths(widget.offset_px);

	// Place the widget so that the matching point on the widget (e.g. its bottom-right corner for a bottom-right anchor) is at anchor_pos + offset.
	if(widget.image)
	{
		const Vec2f dims = gl_ui->getUIWidthForDevIndepPixelWidths(widget.size_px);
		widget.image->setPosAndDims(anchor_pos + offset - Vec2f(fractions.x * dims.x, fractions.y * dims.y), dims);
	}
	if(widget.button)
	{
		// GLUITextButton::setPos() sets the button rect to the text view rect, which can differ from the rect after construction or setText().
		// So position the button once first, so that getDims() returns the dimensions of the drawn button.
		widget.button->setPos(anchor_pos + offset);
		const Vec2f dims = widget.button->getDims();
		widget.button->setPos(anchor_pos + offset - Vec2f(fractions.x * dims.x, fractions.y * dims.y));
	}
	if(widget.image_button)
	{
		const Vec2f dims = widget.image_button->getDims(); // GLUIButton::viewportResized() updates the dims from the fixed pixel size.
		widget.image_button->setPos(anchor_pos + offset - Vec2f(fractions.x * dims.x, fractions.y * dims.y));
	}
	if(widget.slider)
	{
		const Vec2f dims = widget.slider->getDims(); // GLUISlider::viewportResized() updates the dims from the fixed pixel size.
		widget.slider->setPos(anchor_pos + offset - Vec2f(fractions.x * dims.x, fractions.y * dims.y));
	}
	if(widget.spinbox)
	{
		const Vec2f dims = widget.spinbox->getDims(); // GLUISpinBox::viewportResized() updates the dims from the fixed pixel size.
		widget.spinbox->setPos(anchor_pos + offset - Vec2f(fractions.x * dims.x, fractions.y * dims.y));
	}
	if(widget.checkbox)
	{
		const Vec2f dims = widget.checkbox->getDims(); // GLUICheckBox::viewportResized() updates the dims from the fixed pixel size.
		widget.checkbox->setPos(anchor_pos + offset - Vec2f(fractions.x * dims.x, fractions.y * dims.y));
	}
	if(widget.dropdown)
	{
		const Vec2f dims = widget.dropdown->getDims(); // GLUIDropDownList::viewportResized() updates the dims from the fixed pixel size.
		widget.dropdown->setPos(anchor_pos + offset - Vec2f(fractions.x * dims.x, fractions.y * dims.y));
	}
	if(widget.line_edit)
	{
		const Vec2f dims = widget.line_edit->getDims();
		widget.line_edit->setPos(anchor_pos + offset - Vec2f(fractions.x * dims.x, fractions.y * dims.y));
	}
	if(widget.label)
	{
		const Vec2f dims = widget.label->getDims();
		widget.label->setPos(anchor_pos + offset - Vec2f(fractions.x * dims.x, fractions.y * dims.y));
	}
	if(widget.grid)
	{
		widget.grid->recomputeLayout(); // Lays out nested grids as well, and sets the grid dims from the contents.
		const Vec2f dims = widget.grid->getDims();
		widget.grid->setPos(anchor_pos + offset - Vec2f(fractions.x * dims.x, fractions.y * dims.y)); // Moves the contained widgets as well.
	}
	if(widget.window)
	{
		widget.window->recomputeLayout(); // Lays out the body widget as well, and sets the window dims from the body and title.
		const Vec2f dims = widget.window->getDims();
		widget.window->setPos(anchor_pos + offset - Vec2f(fractions.x * dims.x, fractions.y * dims.y)); // Moves the body widget as well.
	}
}


void LuaScriptUI::updateWidgetDrawn(LuaScriptUIWidget& widget)
{
	const bool drawn = isEffectivelyVisible(widget);

	if(widget.image)
		widget.image->setVisible(drawn && widget.image_texture_assigned);
	if(widget.button)
		widget.button->setVisible(drawn);
	if(widget.image_button)
		widget.image_button->setVisible(drawn && widget.image_texture_assigned);
	if(widget.slider)
		widget.slider->setVisible(drawn);
	if(widget.spinbox)
		widget.spinbox->setVisible(drawn); // Also removes keyboard focus from its text field if hiding.
	if(widget.checkbox)
		widget.checkbox->setVisible(drawn);
	if(widget.dropdown)
		widget.dropdown->setVisible(drawn); // Also closes the options list if hiding.
	if(widget.line_edit)
		widget.line_edit->setVisible(drawn);

	if(!drawn)
		clearKeyboardFocusIfHeldBy(widget);
	if(widget.label)
		widget.label->setVisible(drawn);
	if(widget.grid || widget.window)
	{
		// Setting the visibility of a grid or window sets the visibility of all contained widgets as well, so set it for each contained widget afterwards.
		if(widget.grid)
			widget.grid->setVisible(drawn);
		if(widget.window)
			widget.window->setVisible(drawn);

		for(auto it = widgets.begin(); it != widgets.end(); ++it)
			if(it->second->parent_id == widget.id)
				updateWidgetDrawn(*it->second);
	}
}


void LuaScriptUI::assignImageTexture(LuaScriptUIWidget& widget, const OpenGLTextureRef& tex)
{
	if(widget.image)
		widget.image->setTexture(tex);
	if(widget.image_button)
		widget.image_button->setTexture(tex);
	widget.image_texture_assigned = true;
	updateWidgetDrawn(widget);
}


// Assigns the texture to any images and image buttons that are waiting for it.
void LuaScriptUI::handleUploadedTexture(const URLString& URL, const OpenGLTextureRef& tex)
{
	for(auto it = widgets.begin(); it != widgets.end(); ++it)
	{
		LuaScriptUIWidget& widget = *it->second;
		if((widget.image || widget.image_button) && !widget.image_texture_assigned && (widget.image_URL == URL))
			assignImageTexture(widget, tex);
	}
}


// If the image texture is loaded, assign it to the image widget.  Otherwise start loading it, if the image file is present.
// If the image is still downloading or loading, the texture is assigned in handleUploadedTexture() when it has loaded.
void LuaScriptUI::checkAssignImageTexture(LuaScriptUIWidget& widget)
{
	ResourceRef resource = gui_client->resource_manager->getExistingResourceForURL(widget.image_URL);
	if(!resource)
		return;

	const OpenGLTextureKey local_path(gui_client->resource_manager->getLocalAbsPathForResource(*resource));

	OpenGLTextureRef tex = gui_client->opengl_engine->getTextureIfLoaded(local_path);
	if(tex)
	{
		assignImageTexture(widget, tex);
	}
	else if(resource->getState() == Resource::State_Present)
	{
		TextureParams tex_params;
		tex_params.wrapping = OpenGLTexture::Wrapping_Clamp;
		tex_params.allow_compression = false;

		// startLoadingTextureForLocalPath() does nothing if the texture is already being loaded.
		gui_client->startLoadingTextureForLocalPath(local_path, resource, gui_client->cam_controller.getFirstPersonPosition().toVec4fPoint(), /*aabb_ws_longest_len=*/1.f,
			/*max task dist=*/1.0e10f, /*importance factor=*/1.f, tex_params);
	}
}
