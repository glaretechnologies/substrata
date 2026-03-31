/*=====================================================================
AvatarPreviewGLUIWidget.cpp
---------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "AvatarPreviewGLUIWidget.h"


#include <opengl/OpenGLEngine.h>
#include <opengl/FrameBuffer.h>
#include <opengl/RenderBuffer.h>
#include <opengl/IncludeOpenGL.h>
#include <graphics/SRGBUtils.h>
#include <maths/Matrix4f.h>
#include <maths/GeometrySampling.h>


AvatarPreviewGLUIWidget::AvatarPreviewGLUIWidget(GLUI& glui_)
:	drag_active(false),
	drag_start_x(0.f),
	drag_start_phi(0.f),
	middle_drag_active(false),
	middle_drag_last_cursor_pos(0, 0)
{
	glui = &glui_;
	opengl_engine = glui_.opengl_engine.ptr();
	m_z = -0.9f;

	cam_phi        = 0;
	cam_dist       = 2.0f;
	cam_theta      = 1.4f;
	cam_target_pos = Vec4f(0, 0, 1.0f, 1.f);

	overlay_ob = new OverlayObject();
	overlay_ob->mesh_data = opengl_engine->getUnitQuadMeshData();
	overlay_ob->ob_to_world_matrix = Matrix4f::translationMatrix(1000.f, 1000.f, 0.f); // offscreen until positioned

	rect = Rect2f(Vec2f(0.f), Vec2f(0.1f));

	opengl_engine->addOverlayObject(overlay_ob);

	setFixedDimsPx(Vec2f(200, 400), glui_);
}


AvatarPreviewGLUIWidget::~AvatarPreviewGLUIWidget()
{
	checkRemoveOverlayObAndSetRefToNull(opengl_engine, overlay_ob);
}


void AvatarPreviewGLUIWidget::setPos(const Vec2f& botleft)
{
	rect = Rect2f(botleft, botleft + getDims());
	updateOverlayTransform();
}


void AvatarPreviewGLUIWidget::setPosAndDims(const Vec2f& botleft, const Vec2f& dims)
{
	rect = Rect2f(botleft, botleft + dims);
	updateOverlayTransform();
}


void AvatarPreviewGLUIWidget::setClipRegion(const Rect2f& clip_rect)
{
	if(overlay_ob)
		overlay_ob->clip_region = glui->OpenGLRectCoordsForUICoords(clip_rect);
}


// Called when e.g. the viewport changes size
void AvatarPreviewGLUIWidget::updateGLTransform()
{
	const Vec2f dims = computeDims(/*old dims=*/this->getDims(), *glui);
	const Vec2f bot_left = getRect().getMin();
	rect = Rect2f::fromMinAndSpan(bot_left, dims);

	updateOverlayTransform();
}


void AvatarPreviewGLUIWidget::setVisible(bool visible)
{
	if(overlay_ob) 
		overlay_ob->draw = visible;
}


bool AvatarPreviewGLUIWidget::isVisible()
{
	return overlay_ob && overlay_ob->draw;
}


void AvatarPreviewGLUIWidget::handleMousePress(MouseEvent& e)
{
	const Vec2f ui_coords = glui->UICoordsForOpenGLCoords(e.gl_coords);
	if(rect.inClosedRectangle(ui_coords))
	{
		if(e.button == MouseButton::Left)
		{
			drag_active    = true;
			drag_start_x   = ui_coords.x;
			drag_start_phi = cam_phi;
			e.accepted     = true;
		}
		else if(e.button == MouseButton::Middle)
		{
			middle_drag_active          = true;
			middle_drag_last_cursor_pos = e.cursor_pos;
			e.accepted                  = true;
		}
	}
}


void AvatarPreviewGLUIWidget::handleMouseRelease(MouseEvent& e)
{
	if(e.button == MouseButton::Left)   drag_active        = false;
	if(e.button == MouseButton::Middle) middle_drag_active = false;
}


void AvatarPreviewGLUIWidget::doHandleMouseMoved(MouseEvent& e)
{
	if(drag_active)
	{
		const Vec2f ui_coords = glui->UICoordsForOpenGLCoords(e.gl_coords);
		// One full revolution (2pi) per 1.0 UI-coord of horizontal drag.
		cam_phi = drag_start_phi + (ui_coords.x - drag_start_x) * Maths::get2Pi<float>();
	}

	if(middle_drag_active)
	{
		const Vec2i delta               = e.cursor_pos - middle_drag_last_cursor_pos;
		middle_drag_last_cursor_pos     = e.cursor_pos;

		const float move_scale = 0.005f;
		const Vec4f forwards = GeometrySampling::dirForSphericalCoords(-cam_phi + Maths::pi_2<float>(), Maths::pi<float>() - cam_theta);
		const Vec4f right    = normalise(crossProduct(forwards, Vec4f(0, 0, 1, 0)));
		const Vec4f up       = crossProduct(right, forwards);
		cam_target_pos += right * -(float)delta.x * move_scale + up * (float)delta.y * move_scale;
	}
}


void AvatarPreviewGLUIWidget::doHandleMouseWheelEvent(MouseWheelEvent& e)
{
	const Vec2f ui_coords = glui->UICoordsForOpenGLCoords(e.gl_coords);
	if(rect.inClosedRectangle(ui_coords))
	{
		// angle_delta.y is in degrees (Qt angleDelta / 8); scale matches AvatarPreviewWidget::wheelEvent.
		cam_dist = myClamp(cam_dist - cam_dist * e.angle_delta.y * 0.016f, 0.01f, 20.f);
		e.accepted = true;
	}
}


void AvatarPreviewGLUIWidget::recreateFBO(int avatar_preview_w, int avatar_preview_h)
{
	// MSAA colour renderbuffer and depth renderbuffer
	const int msaa_samples = opengl_engine->settings.msaa_samples;
	avatar_preview_color_rb = new RenderBuffer(avatar_preview_w, avatar_preview_h, msaa_samples, Format_RGBA_Linear_Uint8);
	avatar_preview_depth_rb = new RenderBuffer(avatar_preview_w, avatar_preview_h, msaa_samples, Format_Depth_Float);

	// MSAA FBO — rendered into each frame
	avatar_preview_fbo = new FrameBuffer();
	avatar_preview_fbo->attachRenderBuffer(*avatar_preview_color_rb, GL_COLOR_ATTACHMENT0);
	avatar_preview_fbo->attachRenderBuffer(*avatar_preview_depth_rb, GL_DEPTH_ATTACHMENT);
	assert(avatar_preview_fbo->isComplete());

	// Resolve texture — regular (non-MSAA) texture shown in the widget
	avatar_preview_tex = new OpenGLTexture(avatar_preview_w, avatar_preview_h, opengl_engine,
		ArrayRef<uint8>(),
		Format_SRGBA_Uint8,
		OpenGLTexture::Filtering_Bilinear,
		OpenGLTexture::Wrapping_Clamp,
		/*has_mipmaps=*/false
	);

	// Resolve FBO — target of the MSAA blit, backed by avatar_preview_tex
	avatar_preview_resolve_fbo = new FrameBuffer();
	avatar_preview_resolve_fbo->attachTexture(*avatar_preview_tex, GL_COLOR_ATTACHMENT0);
	assert(avatar_preview_resolve_fbo->isComplete());

	overlay_ob->material.albedo_texture = avatar_preview_tex;
}


void AvatarPreviewGLUIWidget::updateOverlayTransform()
{
	if(!overlay_ob) 
		return;
	const Vec2f bot_left = getRect().getMin();
	const Vec2f dims    = getDims();
	const float y_scale = opengl_engine->getViewPortAspectRatio();
	overlay_ob->ob_to_world_matrix =
		Matrix4f::translationMatrix(bot_left.x, bot_left.y * y_scale, m_z) *
		Matrix4f::scaleMatrix(dims.x, dims.y * y_scale, 1.f);
}


void AvatarPreviewGLUIWidget::renderAvatarPreview()
{
	if(avatar_preview_fbo.isNull())
		return;

	opengl_engine->setTargetFrameBufferAndViewport(avatar_preview_fbo);
	opengl_engine->setNearDrawDistance(0.1f);
	opengl_engine->setMaxDrawDistance(100.f);


	// Camera orbits around cam_target_pos. All params are member variables updated by widget events.
	const Matrix4f T     = Matrix4f::translationMatrix(0.f, cam_dist, 0.f);
	const Matrix4f z_rot = Matrix4f::rotationAroundZAxis(cam_phi);
	const Matrix4f x_rot = Matrix4f::rotationAroundXAxis(-(cam_theta - Maths::pi_2<float>()));
	const Matrix4f world_to_cam = T * x_rot * z_rot * Matrix4f::translationMatrix(-cam_target_pos);

	const float sensor_width        = 0.035f;
	const float lens_sensor_dist    = 0.05f;
	const float render_aspect_ratio = (float)avatar_preview_fbo->xRes() / (float)avatar_preview_fbo->yRes();
	opengl_engine->setPerspectiveCameraTransform(world_to_cam, sensor_width, lens_sensor_dist, render_aspect_ratio, 0.f, 0.f);

	opengl_engine->draw();

	// Resolve MSAA: blit the MSAA FBO into the regular resolve texture.
	blitFrameBuffer(*avatar_preview_fbo, *avatar_preview_resolve_fbo, /*num_buffers_to_copy=*/1, /*copy_buf0_colour=*/true, /*copy_buf0_depth=*/false);
}
