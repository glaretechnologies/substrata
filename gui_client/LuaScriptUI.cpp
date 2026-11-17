/*=====================================================================
LuaScriptUI.cpp
---------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "LuaScriptUI.h"


#include "GUIClient.h"
#include "../shared/SubstrataLuaScript.h"
#include "../shared/ResourceManager.h"
#include "../shared/WorldStateLock.h"
#include <lua/LuaScript.h>
#include <utils/Exception.h>
#include <utils/StringUtils.h>
#include <utils/UTF8Utils.h>
#include <lua.h>
#include <cmath>


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
	if(slider)	return slider;
	if(label)	return label;
	if(grid)	return grid;
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


LuaScriptUI::LuaScriptUI(GUIClient* gui_client_, const GLUIRef& gl_ui_)
:	gui_client(gui_client_),
	gl_ui(gl_ui_),
	next_widget_id(1),
	ui_visible(true)
{
}


LuaScriptUI::~LuaScriptUI()
{
	removeAllWidgets();
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
	widget->parent_grid_id = 0;
	widget->anchor = anchor;
	widget->offset_px = offset_px;
	widget->size_px = size_px;
	widget->image_URL = image_URL;
	widget->image_texture_assigned = false;
	widget->visible = visible;
	widget->slider_change_queued = false;

	widget->image = new GLUIImage(*gl_ui, /*tex_path=*/"", /*tooltip=*/"");
	widget->image->setMouseOverColour(Colour3f(1.f)); // No mouse-over highlight
	widget->image->setFixedDimsPx(size_px); // So that a grid containing the image knows its size.
	gl_ui->addWidget(widget->image);

	updateWidgetTransform(*widget);
	updateWidgetDrawn(*widget); // Not drawn until the texture is assigned.

	// Start downloading the image if it isn't present already.  Images declared with @resource will usually have been downloaded along with the object already.
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

	checkAssignImageTexture(*widget);

	widgets[widget->id] = widget;
	return widget->id;
}


int LuaScriptUI::createButton(SubstrataLuaScript* sub_lua_script, const std::string& text, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px, LuaVM::LuaValueRef onClick_ref, bool visible)
{
	if(numWidgetsForScript(sub_lua_script) >= MAX_NUM_WIDGETS_PER_SCRIPT)
		throw glare::Exception("Too many UI widgets created by script (max " + toString(MAX_NUM_WIDGETS_PER_SCRIPT) + ")");

	LuaScriptUIWidgetRef widget = new LuaScriptUIWidget();
	widget->id = next_widget_id++;
	widget->sub_lua_script = sub_lua_script;
	widget->parent_grid_id = 0;
	widget->anchor = anchor;
	widget->offset_px = offset_px;
	widget->size_px = Vec2f(0.f);
	widget->image_texture_assigned = false;
	widget->visible = visible;
	widget->callback_ref = onClick_ref;
	widget->slider_change_queued = false;

	GLUITextButton::CreateArgs args;
	widget->button = new GLUITextButton(*gl_ui, UTF8Utils::sanitiseUTF8String(text), args);
	widget->button->handler = this;
	gl_ui->addWidget(widget->button);

	updateWidgetTransform(*widget);
	updateWidgetDrawn(*widget);

	widgets[widget->id] = widget;
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
	widget->parent_grid_id = 0;
	widget->anchor = anchor;
	widget->offset_px = offset_px;
	widget->size_px = size_px;
	widget->image_texture_assigned = false;
	widget->visible = visible;
	widget->callback_ref = onChange_ref;
	widget->slider_change_queued = false;

	GLUISlider::CreateArgs args;
	args.fixed_size = size_px;
	args.min_value = min_value;
	args.max_value = max_value;
	args.initial_value = myClamp(initial_value, min_value, max_value);
	widget->slider = new GLUISlider(*gl_ui, args);
	widget->slider->handler = this;
	gl_ui->addWidget(widget->slider);

	updateWidgetTransform(*widget);
	updateWidgetDrawn(*widget);

	widgets[widget->id] = widget;
	return widget->id;
}


int LuaScriptUI::createLabel(SubstrataLuaScript* sub_lua_script, const std::string& text, const Colour3f& text_colour_linear, const Colour3f& background_colour_linear, float background_alpha, int font_size_px,
	LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px, bool visible)
{
	if(numWidgetsForScript(sub_lua_script) >= MAX_NUM_WIDGETS_PER_SCRIPT)
		throw glare::Exception("Too many UI widgets created by script (max " + toString(MAX_NUM_WIDGETS_PER_SCRIPT) + ")");

	LuaScriptUIWidgetRef widget = new LuaScriptUIWidget();
	widget->id = next_widget_id++;
	widget->sub_lua_script = sub_lua_script;
	widget->parent_grid_id = 0;
	widget->anchor = anchor;
	widget->offset_px = offset_px;
	widget->size_px = Vec2f(0.f);
	widget->image_texture_assigned = false;
	widget->visible = visible;
	widget->slider_change_queued = false;

	GLUITextView::CreateArgs args;
	args.text_colour = text_colour_linear;
	args.background_colour = background_colour_linear;
	args.background_alpha = background_alpha;
	args.font_size_px = font_size_px;
	args.padding_px = (int)std::ceil(font_size_px * 0.6f); // Same ratio as the GLUITextView default.
	args.fixed_size = Vec2f(0.f); // Size the label to fit the text.
	args.text_selectable = false; // So that clicking on a label doesn't take keyboard focus.
	widget->label = new GLUITextView(*gl_ui, UTF8Utils::sanitiseUTF8String(text), args);
	gl_ui->addWidget(widget->label);

	updateWidgetTransform(*widget);
	updateWidgetDrawn(*widget);

	widgets[widget->id] = widget;
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
	widget->parent_grid_id = 0;
	widget->anchor = anchor;
	widget->offset_px = offset_px;
	widget->size_px = Vec2f(0.f);
	widget->image_texture_assigned = false;
	widget->visible = visible;
	widget->slider_change_queued = false;

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

	widgets[widget->id] = widget;
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

	removeFromParentGrid(*res->second);
	destroyWidgetAndContents(id);
}


void LuaScriptUI::destroyWidgetAndContents(int id)
{
	std::vector<int> child_ids;
	for(auto it = widgets.begin(); it != widgets.end(); ++it)
		if(it->second->parent_grid_id == id)
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
}


void LuaScriptUI::setSliderValue(LuaScriptUIWidget& widget, double value)
{
	if(widget.slider)
		widget.slider->setValueNoEvent(value); // Clamps to the slider range.
}


void LuaScriptUI::setGridCell(LuaScriptUIWidget& grid, int cell_x, int cell_y, LuaScriptUIWidget* child)
{
	if(!grid.grid)
		throw glare::Exception("UI widget: only grids have cells");
	if(cell_x < 0 || cell_y < 0 || cell_x >= (int)MAX_GRID_SIZE || cell_y >= (int)MAX_GRID_SIZE)
		throw glare::Exception("UI grid: too many rows or columns (max " + toString(MAX_GRID_SIZE) + ")");

	if(child)
	{
		for(LuaScriptUIWidget* w = &grid; w; w = getParentGrid(*w))
			if(w == child)
				throw glare::Exception("UI grid: can't put a grid inside itself");

		removeFromParentGrid(*child);
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
					removeFromParentGrid(*w);
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
		child->parent_grid_id = grid.id;
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


LuaScriptUIWidget* LuaScriptUI::getParentGrid(const LuaScriptUIWidget& widget)
{
	if(widget.parent_grid_id == 0)
		return NULL;
	auto res = widgets.find(widget.parent_grid_id);
	return (res != widgets.end()) ? res->second.ptr() : NULL;
}


LuaScriptUIWidget& LuaScriptUI::getRootWidget(LuaScriptUIWidget& widget)
{
	LuaScriptUIWidget* w = &widget;
	while(LuaScriptUIWidget* parent = getParentGrid(*w))
		w = parent;
	return *w;
}


bool LuaScriptUI::isEffectivelyVisible(const LuaScriptUIWidget& widget)
{
	if(!ui_visible)
		return false;
	for(const LuaScriptUIWidget* w = &widget; w; w = getParentGrid(*w))
		if(!w->visible)
			return false;
	return true;
}


void LuaScriptUI::removeFromParentGrid(LuaScriptUIWidget& widget)
{
	LuaScriptUIWidget* parent = getParentGrid(widget);
	widget.parent_grid_id = 0;
	if(parent)
	{
		parent->grid->removeWidget(widget.getGLUIWidget());
		updateWidgetTransform(*parent); // The grid size may have changed.
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
	for(auto it = widgets.begin(); it != widgets.end(); ++it)
		removeWidgetFromUI(*it->second);
	widgets.clear();
	clicked_widget_ids.clear();
	changed_slider_ids.clear();
}


void LuaScriptUI::removeWidgetFromUI(LuaScriptUIWidget& widget)
{
	checkRemoveAndDeleteWidget(gl_ui, widget.image);
	checkRemoveAndDeleteWidget(gl_ui, widget.button);
	checkRemoveAndDeleteWidget(gl_ui, widget.slider);
	checkRemoveAndDeleteWidget(gl_ui, widget.label);
	checkRemoveAndDeleteWidget(gl_ui, widget.grid);

	if(widget.callback_ref.ref != LUA_NOREF)
	{
		widget.sub_lua_script->freeRefToFunction(widget.callback_ref);
		widget.callback_ref = LuaVM::LuaValueRef();
	}
}


void LuaScriptUI::think()
{
	for(auto it = widgets.begin(); it != widgets.end(); ++it)
		if(it->second->image && !it->second->image_texture_assigned)
			checkAssignImageTexture(*it->second);

	if((!clicked_widget_ids.empty() || !changed_slider_ids.empty()) && gui_client->world_state)
	{
		std::vector<int> clicked_ids;
		clicked_ids.swap(clicked_widget_ids);

		std::vector<int> changed_ids;
		changed_ids.swap(changed_slider_ids);

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
				widget->slider_change_queued = false;
				if(widget->callback_ref.ref != LUA_NOREF)
					widget->sub_lua_script->doOnUIValueChangedEvent(widget->callback_ref, widget->slider->getValue(), gui_client->client_avatar_uid, lock);
			}
		}
	}
}


void LuaScriptUI::viewportResized()
{
	for(auto it = widgets.begin(); it != widgets.end(); ++it)
		if(it->second->parent_grid_id == 0) // Widgets in grids are positioned by updateWidgetTransform() on the outermost grid.
			updateWidgetTransform(*it->second);
}


void LuaScriptUI::setVisible(bool visible)
{
	ui_visible = visible;
	for(auto it = widgets.begin(); it != widgets.end(); ++it)
		updateWidgetDrawn(*it->second);
}


void LuaScriptUI::eventOccurred(GLUICallbackEvent& event)
{
	for(auto it = widgets.begin(); it != widgets.end(); ++it)
	{
		if(it->second->button.ptr() == event.widget)
		{
			clicked_widget_ids.push_back(it->first);
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
			if(!widget->slider_change_queued)
			{
				widget->slider_change_queued = true;
				changed_slider_ids.push_back(it->first);
			}
			event.accepted = true;
			return;
		}
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
	if(widget.slider)
	{
		const Vec2f dims = widget.slider->getDims(); // GLUISlider::viewportResized() updates the dims from the fixed pixel size.
		widget.slider->setPos(anchor_pos + offset - Vec2f(fractions.x * dims.x, fractions.y * dims.y));
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
}


void LuaScriptUI::updateWidgetDrawn(LuaScriptUIWidget& widget)
{
	const bool drawn = isEffectivelyVisible(widget);

	if(widget.image)
		widget.image->setVisible(drawn && widget.image_texture_assigned);
	if(widget.button)
		widget.button->setVisible(drawn);
	if(widget.slider)
		widget.slider->setVisible(drawn);
	if(widget.label)
		widget.label->setVisible(drawn);
	if(widget.grid)
	{
		widget.grid->setVisible(drawn); // Sets the visibility of all contained widgets as well, so set it for each contained widget afterwards.

		for(auto it = widgets.begin(); it != widgets.end(); ++it)
			if(it->second->parent_grid_id == widget.id)
				updateWidgetDrawn(*it->second);
	}
}


// If the image texture is loaded, assign it to the image widget.  Otherwise start loading it, if the image file is present.
void LuaScriptUI::checkAssignImageTexture(LuaScriptUIWidget& widget)
{
	ResourceRef resource = gui_client->resource_manager->getExistingResourceForURL(widget.image_URL);
	if(!resource)
		return;

	const OpenGLTextureKey local_path(gui_client->resource_manager->getLocalAbsPathForResource(*resource));

	OpenGLTextureRef tex = gui_client->opengl_engine->getTextureIfLoaded(local_path);
	if(tex)
	{
		widget.image->setTexture(tex);
		widget.image_texture_assigned = true;
		updateWidgetDrawn(widget);
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
