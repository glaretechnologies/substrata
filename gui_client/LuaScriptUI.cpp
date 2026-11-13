/*=====================================================================
LuaScriptUI.cpp
---------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "LuaScriptUI.h"


#include "GUIClient.h"
#include "../shared/LuaScriptEvaluator.h"
#include "../shared/ResourceManager.h"
#include "../shared/WorldStateLock.h"
#include <lua/LuaScript.h>
#include <utils/Exception.h>
#include <utils/StringUtils.h>
#include <utils/UTF8Utils.h>
#include <lua.h>


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


size_t LuaScriptUI::numWidgetsForScript(LuaScriptEvaluator* script_evaluator) const
{
	size_t num = 0;
	for(auto it = widgets.begin(); it != widgets.end(); ++it)
		if(it->second->script_evaluator == script_evaluator)
			num++;
	return num;
}


int LuaScriptUI::createImage(LuaScriptEvaluator* script_evaluator, const URLString& image_URL, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px, const Vec2f& size_px, bool visible)
{
	if(numWidgetsForScript(script_evaluator) >= MAX_NUM_WIDGETS_PER_SCRIPT)
		throw glare::Exception("Too many UI widgets created by script (max " + toString(MAX_NUM_WIDGETS_PER_SCRIPT) + ")");

	LuaScriptUIWidgetRef widget = new LuaScriptUIWidget();
	widget->id = next_widget_id++;
	widget->script_evaluator = script_evaluator;
	widget->anchor = anchor;
	widget->offset_px = offset_px;
	widget->size_px = size_px;
	widget->image_URL = image_URL;
	widget->image_texture_assigned = false;
	widget->visible = visible;

	widget->image = new GLUIImage(*gl_ui, /*tex_path=*/"", /*tooltip=*/"");
	widget->image->setMouseOverColour(Colour3f(1.f)); // No mouse-over highlight
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


int LuaScriptUI::createButton(LuaScriptEvaluator* script_evaluator, const std::string& text, LuaScriptUIWidget::Anchor anchor, const Vec2f& offset_px, LuaVM::LuaValueRef onClick_ref, bool visible)
{
	if(numWidgetsForScript(script_evaluator) >= MAX_NUM_WIDGETS_PER_SCRIPT)
		throw glare::Exception("Too many UI widgets created by script (max " + toString(MAX_NUM_WIDGETS_PER_SCRIPT) + ")");

	LuaScriptUIWidgetRef widget = new LuaScriptUIWidget();
	widget->id = next_widget_id++;
	widget->script_evaluator = script_evaluator;
	widget->anchor = anchor;
	widget->offset_px = offset_px;
	widget->size_px = Vec2f(0.f);
	widget->image_texture_assigned = false;
	widget->visible = visible;
	widget->onClick_ref = onClick_ref;

	GLUITextButton::CreateArgs args;
	widget->button = new GLUITextButton(*gl_ui, UTF8Utils::sanitiseUTF8String(text), /*botleft=*/Vec2f(0.f), args);
	widget->button->handler = this;
	gl_ui->addWidget(widget->button);

	updateWidgetTransform(*widget);
	updateWidgetDrawn(*widget);

	widgets[widget->id] = widget;
	return widget->id;
}


LuaScriptUIWidget* LuaScriptUI::getWidget(LuaScriptEvaluator* script_evaluator, int id)
{
	auto res = widgets.find(id);
	if(res == widgets.end() || (res->second->script_evaluator != script_evaluator)) // Scripts can only access their own widgets.
		return NULL;
	return res->second.ptr();
}


void LuaScriptUI::destroyWidget(LuaScriptEvaluator* script_evaluator, int id)
{
	auto res = widgets.find(id);
	if(res == widgets.end() || (res->second->script_evaluator != script_evaluator))
		return;

	removeWidgetFromUI(*res->second);
	widgets.erase(res);
}


void LuaScriptUI::setWidgetVisible(LuaScriptUIWidget& widget, bool visible)
{
	widget.visible = visible;
	updateWidgetDrawn(widget);
}


void LuaScriptUI::setButtonText(LuaScriptUIWidget& widget, const std::string& text)
{
	if(widget.button)
	{
		widget.button->setText(UTF8Utils::sanitiseUTF8String(text));
		updateWidgetTransform(widget); // The button size may have changed.
	}
}


void LuaScriptUI::removeWidgetsForScript(LuaScriptEvaluator* script_evaluator)
{
	for(auto it = widgets.begin(); it != widgets.end();)
	{
		if(it->second->script_evaluator == script_evaluator)
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
}


void LuaScriptUI::removeWidgetFromUI(LuaScriptUIWidget& widget)
{
	checkRemoveAndDeleteWidget(gl_ui, widget.image);
	checkRemoveAndDeleteWidget(gl_ui, widget.button);

	if(widget.onClick_ref.ref != LUA_NOREF)
	{
		widget.script_evaluator->lua_script->freeRefToFunction(widget.onClick_ref);
		widget.onClick_ref = LuaVM::LuaValueRef();
	}
}


void LuaScriptUI::think()
{
	for(auto it = widgets.begin(); it != widgets.end(); ++it)
		if(it->second->image && !it->second->image_texture_assigned)
			checkAssignImageTexture(*it->second);

	if(!clicked_widget_ids.empty() && gui_client->world_state)
	{
		std::vector<int> ids;
		ids.swap(clicked_widget_ids);

		WorldStateLock lock(gui_client->world_state->mutex);

		for(size_t i=0; i<ids.size(); ++i)
		{
			// Look up the widget again, as an earlier callback may have destroyed it.
			auto res = widgets.find(ids[i]);
			if(res != widgets.end())
			{
				LuaScriptUIWidgetRef widget = res->second; // Hold a reference, as the callback may destroy the widget.
				if(widget->onClick_ref.ref != LUA_NOREF)
					widget->script_evaluator->doOnUIEvent(widget->onClick_ref, gui_client->client_avatar_uid, lock);
			}
		}
	}
}


void LuaScriptUI::viewportResized()
{
	for(auto it = widgets.begin(); it != widgets.end(); ++it)
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


void LuaScriptUI::updateWidgetTransform(LuaScriptUIWidget& widget)
{
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
}


void LuaScriptUI::updateWidgetDrawn(LuaScriptUIWidget& widget)
{
	if(widget.image)
		widget.image->setVisible(ui_visible && widget.visible && widget.image_texture_assigned);
	if(widget.button)
		widget.button->setVisible(ui_visible && widget.visible);
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
