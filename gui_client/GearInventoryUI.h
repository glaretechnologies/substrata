/*=====================================================================
GearInventoryUI.h
-----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "../shared/GearItem.h"
#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUIButton.h>
#include <opengl/ui/GLUIImage.h>
#include <opengl/ui/GLUITextView.h>
#include <opengl/ui/GLUICallbackHandler.h>
#include <opengl/ui/GLUIGridContainer.h>
#include <opengl/ui/GLUIWindow.h>


class GUIClient;


/*=====================================================================
GearInventoryUI
---------------
Shows the gear inventory:
- Left panel:   rendered avatar preview
- Middle panel: equipped gear item thumbnails
- Right panel:  all gear item thumbnails owned by the user

Each thumbnail is a GLUIImage loaded from the gear item's preview_image_URL.
Clicking a thumbnail in 'All Gear' calls gearItemClicked() on the GUIClient.
Clicking a thumbnail in 'Equipped' calls equippedGearItemClicked() on the GUIClient.
=====================================================================*/
class GearInventoryUI : public GLUICallbackHandler, public ThreadSafeRefCounted
{
public:
	GearInventoryUI(GUIClient* gui_client_, GLUIRef gl_ui_);
	~GearInventoryUI();

	void think();

	void viewportResized(int w, int h);

	void setEquippedGear(const GearItems& equipped_gear_);
	void setAllGear(const GearItems& all_gear_);

	virtual void eventOccurred(GLUICallbackEvent& event) override; // From GLUICallbackHandler
	virtual void closeWindowEventOccurred(GLUICallbackEvent& event) override; // From GLUICallbackHandler

	void updateWidgetPositions();

private:
	void rebuildEquippedGrid();
	void rebuildAllGearGrid();

	GUIClient* gui_client;
	GLUIRef gl_ui;

	GearItems equipped_gear;
	GearItems all_gear;

	struct GearItemUI
	{
		GLUIButtonRef thumbnail;
		GearItemRef gear_item;
	};

	std::vector<GearItemUI> equipped_gear_ui;
	std::vector<GearItemUI> all_gear_ui;

	GLUIImageRef        avatar_image;

	GLUIGridContainerRef outer_grid;    // 3-column container: [avatar | equipped | all gear]
	GLUIGridContainerRef equipped_grid; // Inner grid of equipped item thumbnails
	GLUIGridContainerRef all_gear_grid; // Inner grid of all item thumbnails

public:
	GLUIWindowRef window;
	bool close_soon; // Set in closeWindowEventOccurred; If true, gui_client should destroy this UI asap.

private:
	bool need_rebuild_equipped; // Delay rebuilds of gear UI until next think() as gear can change from GUIClient::gearItemClicked etc. after a gear thumb is clicked on.
	bool need_rebuild_all_gear;
};
