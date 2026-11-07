/*=====================================================================
KeyHelpUI.cpp
-------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "KeyHelpUI.h"


#include <opengl/ui/GLUITextView.h>


static const int font_size_px = 13;


KeyHelpUI::KeyHelpUI(GLUIRef gl_ui_)
:	gl_ui(gl_ui_),
	visible(true)
{
	rebuild();
}


KeyHelpUI::~KeyHelpUI()
{
	checkRemoveAndDeleteWidget(gl_ui, grid_container);
}


void KeyHelpUI::setHelp(const std::string& id, const std::string& title, const std::vector<KeyHelpEntry>& entries)
{
	HelpSection* section = NULL;
	for(size_t i=0; i<sections.size(); ++i)
		if(sections[i].id == id)
			section = &sections[i];

	if(!section)
	{
		sections.push_back(HelpSection());
		section = &sections.back();
		section->id = id;
	}

	section->title = title;
	section->entries = entries;

	rebuild();
}


void KeyHelpUI::removeHelp(const std::string& id)
{
	for(size_t i=0; i<sections.size(); ++i)
		if(sections[i].id == id)
		{
			sections.erase(sections.begin() + i);
			rebuild();
			return;
		}
}


void KeyHelpUI::setVisible(bool visible_)
{
	visible = visible_;
	if(grid_container)
		grid_container->setVisible(visible);
}


void KeyHelpUI::viewportResized(int /*w*/, int /*h*/)
{
	updateWidgetPositions();
}


// Recreate all widgets from sections.
void KeyHelpUI::rebuild()
{
	if(!gl_ui)
		return;

	checkRemoveAndDeleteWidget(gl_ui, grid_container);

	if(sections.empty())
		return;

	// Outer grid: one column, with a title row and a key/action grid row for each section.
	{
		GLUIGridContainer::CreateArgs args;
		args.background_colour = Colour3f(0.f);
		args.background_alpha = 0.5f;
		args.interior_cell_x_padding_px = 0;
		args.interior_cell_y_padding_px = 3;
		args.exterior_cell_x_padding_px = 12;
		args.exterior_cell_y_padding_px = 10;
		grid_container = new GLUIGridContainer(*gl_ui, args);
		grid_container->debug_name = "key help grid container";
		gl_ui->addWidget(grid_container);
	}

	for(size_t s=0; s<sections.size(); ++s)
	{
		const HelpSection& section = sections[s];

		if(!section.title.empty())
		{
			GLUITextView::CreateArgs args;
			args.background_alpha = 0;
			args.text_colour = Colour3f(0.45f);
			args.font_size_px = font_size_px - 1;
			args.padding_px = 1;
			args.text_selectable = false;
			grid_container->addWidgetOnNewRow(new GLUITextView(*gl_ui, section.title, Vec2f(0.f), args));
		}

		GLUIGridContainer::CreateArgs entries_grid_args;
		entries_grid_args.background_alpha = 0;
		entries_grid_args.interior_cell_x_padding_px = 7;
		entries_grid_args.interior_cell_y_padding_px = 4;
		GLUIGridContainerRef entries_grid = new GLUIGridContainer(*gl_ui, entries_grid_args);

		for(size_t i=0; i<section.entries.size(); ++i)
		{
			// Keys
			{
				GLUITextView::CreateArgs args;
				args.background_alpha = 0;
				args.text_colour = Colour3f(1.f, 0.7f, 0.25f); // Linear
				args.font_size_px = font_size_px;
				args.padding_px = 1;
				args.text_selectable = false;
				entries_grid->setCellWidget(/*x=*/0, /*y=*/(int)i, new GLUITextView(*gl_ui, section.entries[i].keys, Vec2f(0.f), args));
			}
			// Action
			{
				GLUITextView::CreateArgs args;
				args.background_alpha = 0;
				args.text_colour = Colour3f(1.f);
				args.font_size_px = font_size_px;
				args.padding_px = 1;
				args.text_selectable = false;
				entries_grid->setCellWidget(/*x=*/1, /*y=*/(int)i, new GLUITextView(*gl_ui, section.entries[i].action, Vec2f(0.f), args));
			}
		}

		grid_container->addWidgetOnNewRow(entries_grid);
	}

	grid_container->setVisible(visible);

	updateWidgetPositions();
}


void KeyHelpUI::updateWidgetPositions()
{
	if(!gl_ui || !grid_container)
		return;

	grid_container->recomputeLayout();

	// Place at the left of the viewport, vertically centred.
	const float left_margin = gl_ui->getUIWidthForDevIndepPixelWidth(20);
	const float height = grid_container->getDims().y;

	grid_container->setPos(/*botleft=*/Vec2f(-1.f + left_margin, -height * 0.5f));
}
