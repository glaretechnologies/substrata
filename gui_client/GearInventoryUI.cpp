/*=====================================================================
GearInventoryUI.cpp
-------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "GearInventoryUI.h"


#include "GUIClient.h"
#include "../shared/ResourceManager.h"
#include <opengl/OpenGLEngine.h>
#include <opengl/FrameBuffer.h>
#include <opengl/RenderBuffer.h>
#include <opengl/IncludeOpenGL.h>
#include <graphics/SRGBUtils.h>
#include <maths/Matrix4f.h>
#include <maths/GeometrySampling.h>


static const float THUMBNAIL_SIZE_PX = 64.f;
static const int   GEAR_GRID_COLS    = 4;


/*=====================================================================
InventoryAvatarPreviewWidget
----------------------------
Displays the avatar FBO texture and handles drag-to-rotate and scroll-to-zoom.
Drag left/right to orbit the camera (updates cam_phi).
Scroll wheel to zoom in/out (updates cam_dist).
=====================================================================*/
class InventoryAvatarPreviewWidget final : public GLUIWidget
{
public:
	InventoryAvatarPreviewWidget(GLUI& glui_, float& cam_phi_ref, float& cam_dist_ref, float& cam_theta_ref, Vec4f& cam_target_pos_ref)
	:	cam_phi(cam_phi_ref),
		cam_dist(cam_dist_ref),
		cam_theta(cam_theta_ref),
		cam_target_pos(cam_target_pos_ref),
		drag_active(false),
		drag_start_x(0.f),
		drag_start_phi(0.f),
		middle_drag_active(false),
		middle_drag_last_cursor_pos(0, 0)
	{
		glui = &glui_;
		opengl_engine = glui_.opengl_engine.ptr();
		m_z = -0.9f;

		overlay_ob = new OverlayObject();
		overlay_ob->mesh_data = opengl_engine->getUnitQuadMeshData();
		overlay_ob->ob_to_world_matrix = Matrix4f::translationMatrix(1000.f, 1000.f, 0.f); // offscreen until positioned
		rect = Rect2f(Vec2f(0.f), Vec2f(0.1f));
		opengl_engine->addOverlayObject(overlay_ob);
	}

	~InventoryAvatarPreviewWidget()
	{
		checkRemoveOverlayObAndSetRefToNull(opengl_engine, overlay_ob);
	}

	virtual void setPos(const Vec2f& botleft) override
	{
		rect = Rect2f(botleft, botleft + getDims());
		updateOverlayTransform();
	}

	virtual void setPosAndDims(const Vec2f& botleft, const Vec2f& dims) override
	{
		rect = Rect2f(botleft, botleft + dims);
		updateOverlayTransform();
	}

	virtual void setClipRegion(const Rect2f& clip_rect) override
	{
		if(overlay_ob)
			overlay_ob->clip_region = glui->OpenGLRectCoordsForUICoords(clip_rect);
	}

	virtual void updateGLTransform() override
	{
		Vec2f dims = getDims();
		if(sizing_type_x == SizingType_FixedSizePx)
			dims.x = glui->getUIWidthForDevIndepPixelWidth(fixed_size.x);
		if(sizing_type_y == SizingType_FixedSizePx)
			dims.y = glui->getUIWidthForDevIndepPixelWidth(fixed_size.y);
		rect = Rect2f(getRect().getMin(), getRect().getMin() + dims);
		updateOverlayTransform();
	}

	virtual void setVisible(bool visible) override
	{
		if(overlay_ob) overlay_ob->draw = visible;
	}

	virtual bool isVisible() override
	{
		return overlay_ob.nonNull() && overlay_ob->draw;
	}

	virtual void handleMousePress(MouseEvent& e) override
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

	virtual void handleMouseRelease(MouseEvent& e) override
	{
		if(e.button == MouseButton::Left)   drag_active        = false;
		if(e.button == MouseButton::Middle) middle_drag_active = false;
	}

	virtual void doHandleMouseMoved(MouseEvent& e) override
	{
		if(drag_active)
		{
			const Vec2f ui_coords = glui->UICoordsForOpenGLCoords(e.gl_coords);
			// One full revolution (2π) per 1.0 UI-coord of horizontal drag.
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

	virtual void doHandleMouseWheelEvent(MouseWheelEvent& e) override
	{
		const Vec2f ui_coords = glui->UICoordsForOpenGLCoords(e.gl_coords);
		if(rect.inClosedRectangle(ui_coords))
		{
			// angle_delta.y is in degrees (Qt angleDelta / 8); scale matches AvatarPreviewWidget::wheelEvent.
			cam_dist = myClamp(cam_dist - cam_dist * e.angle_delta.y * 0.016f, 0.01f, 20.f);
			e.accepted = true;
		}
	}

	OverlayObjectRef overlay_ob;

private:
	void updateOverlayTransform()
	{
		if(!overlay_ob) return;
		const Vec2f botleft = getRect().getMin();
		const Vec2f dims    = getDims();
		const float y_scale = opengl_engine->getViewPortAspectRatio();
		overlay_ob->ob_to_world_matrix =
			Matrix4f::translationMatrix(botleft.x, botleft.y * y_scale, m_z) *
			Matrix4f::scaleMatrix(dims.x, dims.y * y_scale, 1.f);
	}

	float&  cam_phi;        // Reference into GearInventoryUI::cam_phi
	float&  cam_dist;       // Reference into GearInventoryUI::cam_dist
	float&  cam_theta;      // Reference into GearInventoryUI::cam_theta
	Vec4f&  cam_target_pos; // Reference into GearInventoryUI::cam_target_pos
	bool    drag_active;
	float   drag_start_x;
	float   drag_start_phi;
	bool    middle_drag_active;
	Vec2i   middle_drag_last_cursor_pos;
};


GearInventoryUI::GearInventoryUI(GUIClient* gui_client_, GLUIRef gl_ui_)
{
	gui_client = gui_client_;
	gl_ui = gl_ui_;
	need_rebuild_equipped = false;
	need_rebuild_all_gear = false;
	close_soon = false;
	cam_phi        = 0;
	cam_dist       = 2.0f;
	cam_theta      = 1.4f;
	cam_target_pos = Vec4f(0, 0, 1.0f, 1.f);

	// Outer 3-column grid: [avatar | equipped | all gear], each column has a header at row 0 and content at row 1.
	{
		GLUIGridContainer::CreateArgs args;
		args.background_alpha = 0.f;
		args.cell_x_padding_px = 12;
		args.cell_y_padding_px = 14;
		outer_grid = new GLUIGridContainer(*gl_ui, args);
		outer_grid->setPosAndDims(Vec2f(0.f), Vec2f(0.01f));
		gl_ui->addWidget(outer_grid);
	}

	// Create avatar preview scene.
	// We set render_to_main_render_framebuffer = false so the engine renders directly to our FBO
	// without any post-processing (bloom, MSAA, SSAO), keeping the setup simple.
	{
		OpenGLEngine* engine = gui_client->opengl_engine.ptr();

		avatar_preview_scene = new OpenGLScene(*engine);
		avatar_preview_scene->render_to_main_render_framebuffer = false;
		avatar_preview_scene->draw_water = false;
		avatar_preview_scene->water_level_z = -10000.0;
		avatar_preview_scene->background_colour = Colour3f(0.2f);
		engine->addScene(avatar_preview_scene);

		// Set up environment material for the preview scene.
		// setSunDir() is engine-global so we leave that at whatever the main scene uses.
		{
			OpenGLSceneRef old_scene = engine->getCurrentScene();
			engine->setCurrentScene(avatar_preview_scene);

			OpenGLMaterial env_mat;
			engine->setEnvMat(env_mat);

			//engine->setSunDir(normalise(Vec4f(1,-1,1,0)));

			// Load a ground plane into the GL engine
			{
				const float W = 200;

				GLObjectRef ob = engine->allocateObject();
				ob->materials.resize(1);
				ob->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(0.8f));
				try
				{
					ob->materials[0].albedo_texture = engine->getTexture(gui_client->base_dir_path + "/data/resources/obstacle.png");
				}
				catch(glare::Exception& e)
				{
					assert(0);
					conPrint("ERROR: " + e.what());
				}
				ob->materials[0].roughness = 0.8f;
				ob->materials[0].fresnel_scale = 0.5f;
				ob->materials[0].tex_matrix = Matrix2f(W, 0, 0, W);

				ob->ob_to_world_matrix = Matrix4f::scaleMatrix(W, W, 1) * Matrix4f::translationMatrix(-0.5f, -0.5f, 0);
				ob->mesh_data = engine->getUnitQuadMeshData();

				engine->addObject(ob);
			}

			engine->setCurrentScene(old_scene);
		}
	}

	avatar_preview_w = 0;
	avatar_preview_h = 0;

	// Column 0: avatar header + avatar preview widget
	{
		// GLUITextView::CreateArgs args;
		// args.background_alpha = 0;
		// GLUITextViewRef avatar_header_text = new GLUITextView(*gl_ui, "Avatar", Vec2f(0.f), args);
		// outer_grid->setCellWidget(/*x=*/0, /*y=*/0, avatar_header_text);
	}
	{
		// Texture is assigned in recreateAvatarPreviewFBO(), called below.
		// cam_phi is stored in GearInventoryUI and the widget holds a reference to it.
		avatar_preview_widget = new InventoryAvatarPreviewWidget(*gl_ui, cam_phi, cam_dist, cam_theta, cam_target_pos);
		avatar_preview_widget->overlay_ob->material.tex_matrix = Matrix2f(1, 0, 0, 1); // No V-flip: FBO texture is already in GL convention
		gl_ui->addWidget(avatar_preview_widget);
		outer_grid->setCellWidget(/*x=*/0, /*y=*/1, avatar_preview_widget);
	}

	// Column 1: equipped gear header + inner grid
	{
		GLUITextView::CreateArgs args;
		args.background_alpha = 0;
		GLUITextViewRef equipped_header_text = new GLUITextView(*gl_ui, "Equipped", Vec2f(0.f), args);
		outer_grid->setCellWidget(/*x=*/1, /*y=*/0, equipped_header_text);
	}
	{
		// 'Equipped gear' grid
		GLUIGridContainer::CreateArgs args;
		//args.background_alpha = 0.15f;
		//args.background_colour = Colour3f(0.1f);
		args.background_alpha = 0.45f;
		args.background_colour =  Colour3f(0.0f); // Colour3f(0.1f, 0.1f, 0.1f); // Colour3f(0.1f);
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
		// 'All Gear' grid
		GLUIGridContainer::CreateArgs args;
		args.background_alpha = 0.45f;
		args.background_colour =  Colour3f(0.0f); // Colour3f(0.1f, 0.1f, 0.1f); // Colour3f(0.1f);
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
		args.z = -0.2f;
		window = new GLUIWindow(*gl_ui, args);
		window->setBodyWidget(outer_grid);
		window->handler = this;
		gl_ui->addWidget(window);
	}

	// Create FBO/texture sized for the current viewport. Also sets avatar_preview_widget display size.
	{
		OpenGLEngine* engine = gui_client->opengl_engine.ptr();
		recreateAvatarPreviewFBO(engine->getViewPortWidth(), engine->getViewPortHeight());
	}
}


GearInventoryUI::~GearInventoryUI()
{
	conPrint("GearInventoryUI::~GearInventoryUI");

	// Clean up the avatar preview scene and its objects
	if(avatar_preview_scene.nonNull())
	{
		OpenGLEngine* engine = gui_client->opengl_engine.ptr();
		OpenGLSceneRef old_scene = engine->getCurrentScene();
		engine->setCurrentScene(avatar_preview_scene);

		checkRemoveObAndSetRefToNull(*engine, avatar_preview_gl_ob);

		// Remove old gear preview objects
		for(auto& graphics : equipped_gear_graphics)
			engine->removeObject(graphics.gear_gl_ob);
		equipped_gear_graphics.clear();

		engine->setCurrentScene(old_scene);
		engine->removeScene(avatar_preview_scene);
		avatar_preview_scene = nullptr;
	}

	checkRemoveAndDeleteWidget(gl_ui, avatar_preview_widget);

	for(auto& ui : equipped_gear_ui)
		checkRemoveAndDeleteWidget(gl_ui, ui.thumbnail);
	for(auto& ui : all_gear_ui)
		checkRemoveAndDeleteWidget(gl_ui, ui.thumbnail);

	checkRemoveAndDeleteWidget(gl_ui, equipped_grid);
	checkRemoveAndDeleteWidget(gl_ui, all_gear_grid);
	checkRemoveAndDeleteWidget(gl_ui, outer_grid);
	checkRemoveAndDeleteWidget(gl_ui, window);
}


void GearInventoryUI::recreateAvatarPreviewFBO(int viewport_w, int viewport_h)
{
	avatar_preview_w = myMax(16, viewport_w / 4);
	avatar_preview_h = avatar_preview_w * 2;

	OpenGLEngine* engine = gui_client->opengl_engine.ptr();

	const int msaa_samples = engine->settings.msaa_samples;

	// MSAA colour renderbuffer and depth renderbuffer
	avatar_preview_color_rb = new RenderBuffer(avatar_preview_w, avatar_preview_h, msaa_samples, Format_RGBA_Linear_Uint8);
	avatar_preview_depth_rb = new RenderBuffer(avatar_preview_w, avatar_preview_h, msaa_samples, Format_Depth_Float);

	// MSAA FBO — rendered into each frame
	avatar_preview_fbo = new FrameBuffer();
	avatar_preview_fbo->attachRenderBuffer(*avatar_preview_color_rb, GL_COLOR_ATTACHMENT0);
	avatar_preview_fbo->attachRenderBuffer(*avatar_preview_depth_rb, GL_DEPTH_ATTACHMENT);
	assert(avatar_preview_fbo->isComplete());

	// Resolve texture — regular (non-MSAA) texture shown in the widget
	avatar_preview_tex = new OpenGLTexture(avatar_preview_w, avatar_preview_h, engine,
		ArrayRef<uint8>(),
		Format_RGBA_Linear_Uint8,
		OpenGLTexture::Filtering_Bilinear,
		OpenGLTexture::Wrapping_Clamp,
		/*has_mipmaps=*/false
	);

	// Resolve FBO — target of the MSAA blit, backed by avatar_preview_tex
	avatar_preview_resolve_fbo = new FrameBuffer();
	avatar_preview_resolve_fbo->attachTexture(*avatar_preview_tex, GL_COLOR_ATTACHMENT0);
	assert(avatar_preview_resolve_fbo->isComplete());

	// Wire updated texture into avatar_preview_widget and resize it to match.
	// Display size: 25% of viewport width (= 0.5 UI coords), 2:1 portrait.
	if(avatar_preview_widget.nonNull())
	{
		avatar_preview_widget->overlay_ob->material.albedo_texture = avatar_preview_tex;

		const float w_dev_indep = gl_ui->getDevIndepPixelWidthForUIWidth(0.5f);
		const float h_dev_indep = w_dev_indep * 2.f;
		avatar_preview_widget->setFixedDimsPx(Vec2f(w_dev_indep, h_dev_indep), *gl_ui);
	}

	updateWidgetPositions();
}


static std::string thumbnailPathForGearItem(GUIClient* gui_client, const GearItem& item)
{
	//if(!item.preview_image_URL.empty() && gui_client->resource_manager->isFileForURLPresent(item.preview_image_URL))
	//	return gui_client->resource_manager->pathForURL(item.preview_image_URL);
	return gui_client->resources_dir_path + "/dot.png"; // Placeholder until resource is downloaded
}


void GearInventoryUI::rebuildEquippedGrid()
{
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

	renderAvatarPreview();
}


void GearInventoryUI::renderAvatarPreview()
{
	if(avatar_preview_scene.isNull() || avatar_preview_fbo.isNull())
		return;

	OpenGLEngine* engine = gui_client->opengl_engine.ptr();

	// Save current state
	OpenGLSceneRef old_scene = engine->getCurrentScene();
	Reference<FrameBuffer> old_fbo = engine->getTargetFrameBuffer();

	engine->setCurrentScene(avatar_preview_scene);
	engine->setTargetFrameBuffer(avatar_preview_fbo);
	engine->setViewportDims(avatar_preview_w, avatar_preview_h);
	engine->setNearDrawDistance(0.1f);
	engine->setMaxDrawDistance(100.f);

	// Set equipped gear transforms
	if(avatar_preview_gl_ob)
	{
		for(size_t i=0; i<equipped_gear_graphics.size(); ++i)
		{
			EquippedGearGraphics& gear = equipped_gear_graphics[i];
			if(gear.gear_gl_ob && gear.bone_node_i != -1)
			{
				if(gear.bone_node_i >= 0 && gear.bone_node_i < (int)avatar_preview_gl_ob->anim_node_data.size())
				{
					gear.gear_gl_ob->ob_to_world_matrix = (avatar_preview_gl_ob->ob_to_world_matrix * avatar_preview_gl_ob->anim_node_data[gear.bone_node_i].node_hierarchical_to_object) * gear.transform;

					engine->updateObjectTransformData(*gear.gear_gl_ob);
				}
			}
		}
	}

	// Camera orbits around cam_target_pos. All params are member variables updated by widget events.
	const Matrix4f T     = Matrix4f::translationMatrix(0.f, cam_dist, 0.f);
	const Matrix4f z_rot = Matrix4f::rotationAroundZAxis(cam_phi);
	const Matrix4f x_rot = Matrix4f::rotationAroundXAxis(-(cam_theta - Maths::pi_2<float>()));
	const Matrix4f world_to_cam = T * x_rot * z_rot * Matrix4f::translationMatrix(-cam_target_pos);

	const float sensor_width     = 0.035f;
	const float lens_sensor_dist = 0.05f;
	const float render_aspect    = (float)avatar_preview_w / (float)avatar_preview_h;
	engine->setPerspectiveCameraTransform(world_to_cam, sensor_width, lens_sensor_dist, render_aspect, 0.f, 0.f);

	engine->draw();

	// Resolve MSAA: blit the MSAA FBO into the regular resolve texture.
	blitFrameBuffer(*avatar_preview_fbo, *avatar_preview_resolve_fbo, /*num_buffers_to_copy=*/1, /*copy_buf0_colour=*/true, /*copy_buf0_depth=*/false);

	// Restore previous state (viewport will be reset by the main render call)
	engine->setCurrentScene(old_scene);
	engine->setTargetFrameBuffer(old_fbo);
}


void GearInventoryUI::setAvatarGLObject(const AvatarGraphics& av_graphics, const Reference<GLObject>& avatar_gl_ob, const Matrix4f& pre_ob_to_world_matrix)
{
	if(avatar_preview_scene.isNull())
		return;

	OpenGLEngine* engine = gui_client->opengl_engine.ptr();
	OpenGLSceneRef old_scene = engine->getCurrentScene();
	engine->setCurrentScene(avatar_preview_scene);

	// Remove old preview object if present
	if(avatar_preview_gl_ob)
	{
		engine->removeObject(avatar_preview_gl_ob);
		avatar_preview_gl_ob = nullptr;
	}

	if(avatar_gl_ob)
	{
		// Create a new GLObject that shares the same mesh data as the avatar's GL object
		avatar_preview_gl_ob = engine->allocateObject();
		avatar_preview_gl_ob->mesh_data      = avatar_gl_ob->mesh_data;
		avatar_preview_gl_ob->materials      = avatar_gl_ob->materials;
		avatar_preview_gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(0, 0, AvatarGraphics::getEyeHeight()) * pre_ob_to_world_matrix;

		engine->addObject(avatar_preview_gl_ob);


		// Remove old gear preview objects
		for(auto& graphics : equipped_gear_graphics)
			engine->removeObject(graphics.gear_gl_ob);
		equipped_gear_graphics.clear();

		// Add gear GL objects
		for(size_t i=0; i<av_graphics.equipped_gear_graphics.size(); ++i)
		{
			const GLObject* gear_gl_ob = av_graphics.equipped_gear_graphics[i].gear_gl_ob.ptr();

			GLObjectRef preview_ob = engine->allocateObject();
			preview_ob->mesh_data = gear_gl_ob->mesh_data;
			preview_ob->materials = gear_gl_ob->materials;
			preview_ob->ob_to_world_matrix = Matrix4f::translationMatrix(0, 0, AvatarGraphics::getEyeHeight()) * gear_gl_ob->ob_to_world_matrix;

			engine->addObject(preview_ob);

			EquippedGearGraphics gear_graphics = av_graphics.equipped_gear_graphics[i];
			gear_graphics.gear_gl_ob = preview_ob;

			equipped_gear_graphics.push_back(gear_graphics);
		}
	}

	engine->setCurrentScene(old_scene);
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
	recreateAvatarPreviewFBO(w, h);
}


void GearInventoryUI::eventOccurred(GLUICallbackEvent& event)
{
	if(!gui_client)
		return;

	GLUIButton* button = dynamic_cast<GLUIButton*>(event.widget);
	if(button)
	{
		for(size_t i=0; i<equipped_gear_ui.size(); ++i)
		{
			if(equipped_gear_ui[i].thumbnail.ptr() == button)
			{
				gui_client->equippedGearItemClicked(equipped_gear_ui[i].gear_item);
				event.accepted = true;
				return;
			}
		}

		for(size_t i=0; i<all_gear_ui.size(); ++i)
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
