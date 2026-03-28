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
class InventoryAvatarPreviewWidget;
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

private:
	void rebuildEquippedGrid();
	void rebuildAllGearGrid();
	void renderAvatarPreview(); // Renders avatar_preview_scene to avatar_preview_fbo. Requires active GL context.

	// (Re-)creates avatar_preview_tex, avatar_preview_depth_rb, and avatar_preview_fbo sized for the
	// given physical viewport dimensions, and refreshes the avatar_preview_widget display size.
	// Call from the constructor and from viewportResized().
	void recreateAvatarPreviewFBO(int viewport_w, int viewport_h);

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

	Reference<InventoryAvatarPreviewWidget> avatar_preview_widget; // Avatar panel: displays FBO texture, handles drag-to-rotate.

	GLUIGridContainerRef outer_grid;    // 3-column container: [avatar | equipped | all gear]
	GLUIGridContainerRef equipped_grid; // Inner grid of equipped item thumbnails
	GLUIGridContainerRef all_gear_grid; // Inner grid of all item thumbnails

	// Avatar preview rendering — dimensions are computed from viewport size in recreateAvatarPreviewFBO().
	int avatar_preview_w; // Physical pixels
	int avatar_preview_h; // Physical pixels

	OpenGLSceneRef          avatar_preview_scene;
	Reference<FrameBuffer>  avatar_preview_fbo;         // MSAA FBO rendered into
	Reference<FrameBuffer>  avatar_preview_resolve_fbo; // Resolve FBO backed by avatar_preview_tex
	OpenGLTextureRef        avatar_preview_tex;          // Regular (non-MSAA) texture shown in the widget
	Reference<RenderBuffer> avatar_preview_color_rb;    // MSAA colour renderbuffer
	Reference<RenderBuffer> avatar_preview_depth_rb;    // MSAA depth renderbuffer
	GLObjectRef             avatar_preview_gl_ob;
	//std::vector<GLObjectRef> equipped_gear_preview_gl_obs;
	std::vector<EquippedGearGraphics> equipped_gear_graphics;

	float cam_phi;        // Camera orbit angle around the Z axis (radians).
	float cam_dist;       // Camera distance from target.
	float cam_theta;      // Camera polar angle (elevation).
	Vec4f cam_target_pos; // World-space point the camera orbits around.

public:
	GLUIWindowRef window;
	bool close_soon; // Set in closeWindowEventOccurred; If true, gui_client should destroy this UI asap.

private:
	bool need_rebuild_equipped; // Delay rebuilds of gear UI until next think() as gear can change from GUIClient::gearItemClicked etc. after a gear thumb is clicked on.
	bool need_rebuild_all_gear;
};
