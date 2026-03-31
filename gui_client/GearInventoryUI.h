/*=====================================================================
GearInventoryUI.h
-----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "AvatarGraphics.h"
#include "../shared/GearItem.h"
#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUIButton.h>
#include <opengl/ui/GLUITextView.h>
#include <opengl/ui/GLUICallbackHandler.h>
#include <opengl/ui/GLUIGridContainer.h>
#include <opengl/ui/GLUIWindow.h>


class GUIClient;
struct GLObject;
class AvatarPreviewGLUIWidget;
class FrameBuffer;
class RenderBuffer;


/*=====================================================================
GearInventoryUI
---------------
Shows the gear inventory:
- Left panel:   rendered avatar preview (drag left/right to rotate)
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

	// Set the avatar GL object to display in the preview scene.
	// Creates a new GL object in the preview scene sharing the same mesh data, placed at the origin using pre_ob_to_world_matrix.
	// Pass nullptr to remove any existing avatar preview object.
	void setAvatarGLObject(const AvatarGraphics& graphics, const Reference<GLObject>& avatar_gl_ob, const Matrix4f& pre_ob_to_world_matrix);

	virtual void eventOccurred(GLUICallbackEvent& event) override; // From GLUICallbackHandler
	virtual void closeWindowEventOccurred(GLUICallbackEvent& event) override; // From GLUICallbackHandler

	void updateWidgetPositions();

	void handleUploadedTexture(const OpenGLTextureKey& path, const URLString& URL, const OpenGLTextureRef& opengl_tex);
private:
	void rebuildEquippedGrid();
	void rebuildAllGearGrid();
	void renderAvatarPreview(); // Renders avatar_preview_scene to avatar_preview_fbo. Requires active GL context.

	// (Re-)creates avatar_preview_tex, avatar_preview_depth_rb, and avatar_preview_fbo sized for the
	// given physical viewport dimensions, and refreshes the avatar_preview_widget display size.
	// Call from the constructor and from viewportResized().
	void recreateAvatarPreviewFBO();

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

	Reference<AvatarPreviewGLUIWidget> avatar_preview_widget; // Avatar panel: displays FBO texture, handles drag-to-rotate.

	GLUIGridContainerRef outer_grid;    // 3-column container: [avatar | equipped | all gear]
	GLUIGridContainerRef equipped_grid; // Inner grid of equipped item thumbnails
	GLUIGridContainerRef all_gear_grid; // Inner grid of all item thumbnails

	OpenGLSceneRef          avatar_preview_scene;
	GLObjectRef             avatar_preview_gl_ob;
	std::vector<EquippedGearGraphics> equipped_gear_graphics;

public:
	GLUIWindowRef window;
	bool close_soon; // Set in closeWindowEventOccurred; If true, gui_client should destroy this UI asap.

private:
	bool need_rebuild_equipped; // Delay rebuilds of gear UI until next think() as gear can change from GUIClient::gearItemClicked etc. after a gear thumb is clicked on.
	bool need_rebuild_all_gear;
};
