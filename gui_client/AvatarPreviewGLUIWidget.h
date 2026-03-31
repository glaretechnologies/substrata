/*=====================================================================
AvatarPreviewGLUIWidget.h
-------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUIWidget.h>


class FrameBuffer;
class RenderBuffer;


/*=====================================================================
AvatarPreviewGLUIWidget
-----------------------
Displays the avatar FBO texture and handles drag-to-rotate and scroll-to-zoom.
Drag left/right to orbit the camera (updates cam_phi).
Scroll wheel to zoom in/out (updates cam_dist).
=====================================================================*/
class AvatarPreviewGLUIWidget final : public GLUIWidget
{
public:
	AvatarPreviewGLUIWidget(GLUI& glui_);
	~AvatarPreviewGLUIWidget();

	virtual void setPos(const Vec2f& botleft) override;

	virtual void setPosAndDims(const Vec2f& botleft, const Vec2f& dims) override;

	virtual void setClipRegion(const Rect2f& clip_rect) override;

	// Called when e.g. the viewport changes size
	virtual void updateGLTransform() override;

	virtual void setVisible(bool visible) override;

	virtual bool isVisible() override;
	virtual void handleMousePress(MouseEvent& e) override;

	virtual void handleMouseRelease(MouseEvent& e) override;
	virtual void doHandleMouseMoved(MouseEvent& e) override;
	virtual void doHandleMouseWheelEvent(MouseWheelEvent& e) override;


	void recreateFBO(int avatar_preview_w, int avatar_preview_h);

	// Scene should have been set to avatar_preview_scene already.  Caller should restore scene and target FBO after calling.
	void renderAvatarPreview();
private:
	void updateOverlayTransform();

	OverlayObjectRef overlay_ob;

	Reference<FrameBuffer>   avatar_preview_fbo;         // MSAA FBO rendered into
	Reference<FrameBuffer>   avatar_preview_resolve_fbo; // Resolve FBO backed by avatar_preview_tex
	Reference<OpenGLTexture> avatar_preview_tex;          // Regular (non-MSAA) texture shown in the widget
	Reference<RenderBuffer>  avatar_preview_color_rb;    // MSAA colour renderbuffer
	Reference<RenderBuffer>  avatar_preview_depth_rb;    // MSAA depth renderbuffer

	float  cam_phi;
	float  cam_dist;
	float  cam_theta;
	Vec4f  cam_target_pos;
	bool    drag_active;
	float   drag_start_x;
	float   drag_start_phi;
	bool    middle_drag_active;
	Vec2i   middle_drag_last_cursor_pos;
};
