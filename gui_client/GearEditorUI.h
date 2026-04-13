/*=====================================================================
GearEditorUI.h
--------------
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
#include <opengl/ui/GLUISpinBox.h>
#include <opengl/ui/GLUIDropDownList.h>
#include <opengl/TransformGizmo.h>


class GUIClient;
struct GLObject;
class AvatarPreviewGLUIWidget;
class FrameBuffer;
class RenderBuffer;


/*=====================================================================
GearEditorUI
------------
Editing controls for a single gear item.

Calls gui_client->gearItemChangedOnOurAvatar(item) when an item is changed.
=====================================================================*/
class GearEditorUI : public GLUICallbackHandler, public ThreadSafeRefCounted
{
public:
	GearEditorUI(GUIClient* gui_client_, GLUIRef gl_ui_, GearItemRef gear_item);
	~GearEditorUI();

	void think();

	void viewportResized(int w, int h);

	// Set the avatar GL object to display in the preview scene.
	// Creates a new GL object in the preview scene sharing the same mesh data, placed at the origin using pre_ob_to_world_matrix.
	// Pass nullptr to remove any existing avatar preview object.
	void setAvatarGLObject(/*const AvatarGraphics& graphics, */const Reference<GLObject>& avatar_gl_ob, const Matrix4f& pre_ob_to_world_matrix, 
		const std::vector<EquippedGearGraphics>& all_equipped_gear_graphics);

	virtual void eventOccurred(GLUICallbackEvent& event) override; // From GLUICallbackHandler
	virtual void closeWindowEventOccurred(GLUICallbackEvent& event) override; // From GLUICallbackHandler

	void updateWidgetPositions();

	void handleUploadedTexture(const OpenGLTextureKey& path, const URLString& URL, const OpenGLTextureRef& opengl_tex);
private:
	void renderAvatarPreview(); // Renders avatar_preview_scene to avatar_preview_fbo. Requires active GL context.

	// Converts gl_coords to FBO pixel coords for the avatar preview widget.
	// Returns false if transform_gizmo is null or the point is outside the widget bounds.
	bool gizmoFBOPixelsForMouseEvent(const Vec2f& gl_coords, Vec2f& fbo_px_out) const;

	// (Re-)creates avatar_preview_tex, avatar_preview_depth_rb, and avatar_preview_fbo sized for the
	// given physical viewport dimensions, and refreshes the avatar_preview_widget display size.
	// Call from the constructor and from viewportResized().
	void recreateAvatarPreviewFBO();

	void gearItemChanged();
public:
	GUIClient* gui_client;
	OpenGLEngine* engine;
	GLUIRef gl_ui;

	GearItemRef gear_item;

	Reference<AvatarPreviewGLUIWidget> avatar_preview_widget; // Avatar panel: displays FBO texture, handles drag-to-rotate.

	GLUIGridContainerRef outer_grid;    // 2-column container: [avatar | controls ]
	GLUIGridContainerRef controls_grid; //

	OpenGLSceneRef          avatar_preview_scene;
	GLObjectRef             avatar_preview_gl_ob;
	EquippedGearGraphics    equipped_gear_graphics;

	Reference<TransformGizmo> transform_gizmo;


	GLUIDropDownListRef bone_list;

	GLUISpinBoxRef pos_x_spinbox;
	GLUISpinBoxRef pos_y_spinbox;
	GLUISpinBoxRef pos_z_spinbox;

	GLUISpinBoxRef rot_x_spinbox;
	GLUISpinBoxRef rot_y_spinbox;
	GLUISpinBoxRef rot_z_spinbox;

	GLUISpinBoxRef scale_x_spinbox;
	GLUISpinBoxRef scale_y_spinbox;
	GLUISpinBoxRef scale_z_spinbox;

public:
	GLUIWindowRef window;
	bool close_soon; // Set in closeWindowEventOccurred; If true, gui_client should destroy this UI asap.

	bool set_ui_from_gear_item_soon; // Set transform spinboxes from gear_item next think().  Can't do it immediately because there is a scene clash in gl engine
	// between UI and transform gizmo.

	Matrix4f on_grab_gear_transform;
};
