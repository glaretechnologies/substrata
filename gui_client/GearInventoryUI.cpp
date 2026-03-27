/*=====================================================================
GearInventoryUI.cpp
-------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "GearInventoryUI.h"


#include "GUIClient.h"
#include "../shared/ResourceManager.h"


static const float THUMBNAIL_SIZE_PX = 64.f;
static const int   GEAR_GRID_COLS    = 4;


GearInventoryUI::GearInventoryUI(GUIClient* gui_client_, GLUIRef gl_ui_)
{
	gui_client = gui_client_;
	gl_ui = gl_ui_;
	need_rebuild_equipped = false;
	need_rebuild_all_gear = false;
	close_soon = false;

	// Outer 3-column grid: [avatar | equipped | all gear], each column has a header at row 0 and content at row 1.
	{
		GLUIGridContainer::CreateArgs args;
		args.background_alpha = 0.f;
		args.cell_x_padding_px = 12;
		args.cell_y_padding_px = 4;
		outer_grid = new GLUIGridContainer(*gl_ui, args);
		outer_grid->setPosAndDims(Vec2f(0.f), Vec2f(0.01f));
		gl_ui->addWidget(outer_grid);
	}

	// Column 0: avatar header + avatar image
	{
		GLUITextView::CreateArgs args;
		args.background_alpha = 0;
		GLUITextViewRef avatar_header_text = new GLUITextView(*gl_ui, "Avatar", Vec2f(0.f), args);
		outer_grid->setCellWidget(/*x=*/0, /*y=*/0, avatar_header_text);
	}
	{
		avatar_image = new GLUIImage(*gl_ui, gui_client->resources_dir_path + "/buttons/avatar.png", "Avatar preview");
		avatar_image->setFixedDimsPx(Vec2f(THUMBNAIL_SIZE_PX * 2.f, THUMBNAIL_SIZE_PX * 3.f), *gl_ui);
		gl_ui->addWidget(avatar_image);
		outer_grid->setCellWidget(/*x=*/0, /*y=*/1, avatar_image);
	}

	// Column 1: equipped gear header + inner grid
	{
		GLUITextView::CreateArgs args;
		args.background_alpha = 0;
		GLUITextViewRef equipped_header_text = new GLUITextView(*gl_ui, "Equipped", Vec2f(0.f), args);
		outer_grid->setCellWidget(/*x=*/1, /*y=*/0, equipped_header_text);
	}
	{
		GLUIGridContainer::CreateArgs args;
		args.background_alpha = 0.15f;
		args.background_colour = Colour3f(0.1f);
		args.cell_x_padding_px = 4;
		args.cell_y_padding_px = 4;
		equipped_grid = new GLUIGridContainer(*gl_ui, args);
		equipped_grid->setPosAndDims(Vec2f(0.f), Vec2f(0.01f));
		gl_ui->addWidget(equipped_grid);
		outer_grid->setCellWidget(/*x=*/1, /*y=*/1, equipped_grid);
	}

	// Column 2: all gear header + inner grid
	{
		GLUITextView::CreateArgs args;
		args.background_alpha = 0;
		GLUITextViewRef all_gear_header_text = new GLUITextView(*gl_ui, "All Gear", Vec2f(0.f), args);
		outer_grid->setCellWidget(/*x=*/2, /*y=*/0, all_gear_header_text);
	}
	{
		GLUIGridContainer::CreateArgs args;
		args.background_alpha = 0.15f;
		args.background_colour = Colour3f(0.1f);
		args.cell_x_padding_px = 4;
		args.cell_y_padding_px = 4;
		all_gear_grid = new GLUIGridContainer(*gl_ui, args);
		all_gear_grid->setPosAndDims(Vec2f(0.f), Vec2f(0.01f));
		gl_ui->addWidget(all_gear_grid);
		outer_grid->setCellWidget(/*x=*/2, /*y=*/1, all_gear_grid);
	}

	// Window containing the outer grid
	{
		GLUIWindow::CreateArgs args;
		args.title = "Gear Inventory";
		args.background_alpha = 0.6f;
		args.background_colour = Colour3f(0.1f);
		window = new GLUIWindow(*gl_ui, args);
		window->setBodyWidget(outer_grid);
		window->handler = this;
		gl_ui->addWidget(window);
	}

	updateWidgetPositions();
}


GearInventoryUI::~GearInventoryUI()
{
	conPrint("GearInventoryUI::~GearInventoryUI");

	checkRemoveAndDeleteWidget(gl_ui, avatar_image);

	for(auto& ui : equipped_gear_ui)
		checkRemoveAndDeleteWidget(gl_ui, ui.thumbnail);
	for(auto& ui : all_gear_ui)
		checkRemoveAndDeleteWidget(gl_ui, ui.thumbnail);

	checkRemoveAndDeleteWidget(gl_ui, equipped_grid);
	checkRemoveAndDeleteWidget(gl_ui, all_gear_grid);
	checkRemoveAndDeleteWidget(gl_ui, outer_grid);
	checkRemoveAndDeleteWidget(gl_ui, window);
}


static std::string thumbnailPathForGearItem(GUIClient* gui_client, const GearItem& item)
{
	//if(!item.preview_image_URL.empty() && gui_client->resource_manager->isFileForURLPresent(item.preview_image_URL))
	//	return gui_client->resource_manager->pathForURL(item.preview_image_URL);
	return gui_client->resources_dir_path + "/dot.png"; // Placeholder until resource is downloaded
}


void GearInventoryUI::rebuildEquippedGrid()
{
	GLUIButton* button = nullptr;
	if(equipped_gear_ui.size() >= 1)
		button = equipped_gear_ui[0].thumbnail.ptr();

	equipped_grid->clear();

	for(size_t i=0; i<equipped_gear_ui.size(); ++i)
		checkRemoveAndDeleteWidget(gl_ui, equipped_gear_ui[i].thumbnail);
	equipped_gear_ui.clear();

	for(size_t i=0; i<equipped_gear.items.size(); ++i)
	{
		const GearItemRef& item = equipped_gear.items[i];

		GLUIButton::CreateArgs args;
		args.tooltip = item->name;
		args.sizing_type_x = GLUIWidget::SizingType_FixedSizePx;
		args.sizing_type_y = GLUIWidget::SizingType_FixedSizePx;
		args.fixed_size = Vec2f(THUMBNAIL_SIZE_PX);
		GLUIButtonRef thumbnail = new GLUIButton(*gl_ui, thumbnailPathForGearItem(gui_client, *item), args);
		thumbnail->handler = this;
		gl_ui->addWidget(thumbnail);
		equipped_grid->setCellWidget(/*x=*/(int)(i % GEAR_GRID_COLS), /*y=*/(int)(i / GEAR_GRID_COLS), thumbnail);

		GearItemUI ui;
		ui.thumbnail = thumbnail;
		ui.gear_item = item;
		equipped_gear_ui.push_back(ui);
	}

	updateWidgetPositions();
}


void GearInventoryUI::rebuildAllGearGrid()
{
	all_gear_grid->clear();

	for(auto& ui : all_gear_ui)
		checkRemoveAndDeleteWidget(gl_ui, ui.thumbnail);
	all_gear_ui.clear();

	for(size_t i=0; i < all_gear.items.size(); ++i)
	{
		const GearItemRef& item = all_gear.items[i];

		GLUIButton::CreateArgs args;
		args.tooltip = item->name;
		args.sizing_type_x = GLUIWidget::SizingType_FixedSizePx;
		args.sizing_type_y = GLUIWidget::SizingType_FixedSizePx;
		args.fixed_size = Vec2f(THUMBNAIL_SIZE_PX);
		GLUIButtonRef thumbnail = new GLUIButton(*gl_ui, thumbnailPathForGearItem(gui_client, *item), args);
		thumbnail->handler = this;
		gl_ui->addWidget(thumbnail);
		all_gear_grid->setCellWidget(/*x=*/(int)(i % GEAR_GRID_COLS), /*y=*/(int)(i / GEAR_GRID_COLS), thumbnail);

		GearItemUI ui;
		ui.thumbnail = thumbnail;
		ui.gear_item = item;
		all_gear_ui.push_back(ui);
	}

	updateWidgetPositions();
}


void GearInventoryUI::setEquippedGear(const GearItems& equipped_gear_)
{
	equipped_gear = equipped_gear_;
	need_rebuild_equipped = true;
}


void GearInventoryUI::setAllGear(const GearItems& all_gear_)
{
	all_gear = all_gear_;
	need_rebuild_all_gear = true;
}


void GearInventoryUI::think()
{
	if(need_rebuild_equipped)
	{
		rebuildEquippedGrid();
		need_rebuild_equipped = false;
	}

	if(need_rebuild_all_gear)
	{
		rebuildAllGearGrid();
		need_rebuild_all_gear = false;
	}
}


void GearInventoryUI::updateWidgetPositions()
{
	const float margin = gl_ui->getUIWidthForDevIndepPixelWidth(60);

	window->setPosAndDims(
		Vec2f(-1.f + margin, -gl_ui->getViewportMinMaxY() + margin),
		Vec2f(myMax(0.01f, 2.f - 2 * margin), myMax(0.01f, 2 * gl_ui->getViewportMinMaxY() - 2 * margin))
	);

	window->recomputeLayout();
}


void GearInventoryUI::viewportResized(int w, int h)
{
	updateWidgetPositions();
}


void GearInventoryUI::eventOccurred(GLUICallbackEvent& event)
{
	if(!gui_client)
		return;

	GLUIButton* button = dynamic_cast<GLUIButton*>(event.widget);
	if(button)
	{
		for(size_t i=0; i< equipped_gear_ui.size(); ++i)
		{
			if(equipped_gear_ui[i].thumbnail.ptr() == button)
			{
				gui_client->equippedGearItemClicked(equipped_gear_ui[i].gear_item);
				event.accepted = true;
				return;
			}
		}

		for(size_t i = 0; i < all_gear_ui.size(); ++i)
		{
			if(all_gear_ui[i].thumbnail.ptr() == button)
			{
				gui_client->gearItemClicked(all_gear_ui[i].gear_item);
				event.accepted = true;
				return;
			}
		}
	}
}


void GearInventoryUI::closeWindowEventOccurred(GLUICallbackEvent& event)
{
	assert(event.widget == window.ptr());

	close_soon = true; // gui_client should destroy this UI asap.  (Can't tell gui_client to destroy this now as are in event handler from it)
}
