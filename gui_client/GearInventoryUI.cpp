/*=====================================================================
GearInventoryUI.cpp
-------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "GearInventoryUI.h"


#include "AvatarPreviewGLUIWidget.h"
#include "GUIClient.h"
#include "../shared/ResourceManager.h"
#include <opengl/ui/GLUIInertWidget.h>
#include <opengl/OpenGLEngine.h>
#include <opengl/FrameBuffer.h>
#include <opengl/RenderBuffer.h>
#include <opengl/IncludeOpenGL.h>
#include <graphics/SRGBUtils.h>
#include <maths/Matrix4f.h>
#include <maths/GeometrySampling.h>


static const float THUMBNAIL_SIZE_PX = 100.f;
static const int   GEAR_GRID_COLS    = 4;
static const int   GEAR_GRID_ROWS    = 6;
	
static const float INTERIOR_GRID_PADDING_PX = 6;


GearInventoryUI::GearInventoryUI(GUIClient* gui_client_, GLUIRef gl_ui_)
{
	gui_client = gui_client_;
	gl_ui = gl_ui_;
	need_rebuild_equipped = false;
	need_rebuild_all_gear = false;
	close_soon = false;

	// Outer 3-column grid: [avatar preview | equipped | all gear], each column has a header at row 0 and content at row 1.
	{
		GLUIGridContainer::CreateArgs args;
		args.background_alpha = 0.f;
		args.interior_cell_x_padding_px = INTERIOR_GRID_PADDING_PX * 4; // So there is effectively a gap of INTERIOR_GRID_PADDING_PX * 8 between outer columns.
		args.interior_cell_y_padding_px = INTERIOR_GRID_PADDING_PX * 4;
		args.exterior_cell_x_padding_px = INTERIOR_GRID_PADDING_PX * 8;
		args.exterior_cell_y_padding_px = INTERIOR_GRID_PADDING_PX * 8;
		outer_grid = new GLUIGridContainer(*gl_ui, args);
		outer_grid->setPosAndDims(Vec2f(0.f), Vec2f(0.01f));
		outer_grid->debug_name = "GearInventoryUI outer grid";
	}

	// Create avatar preview scene.
	{
		OpenGLEngine* engine = gui_client->opengl_engine.ptr();

		avatar_preview_scene = new OpenGLScene(*engine);
		avatar_preview_scene->draw_water = false;
		avatar_preview_scene->water_level_z = -10000.0;
		avatar_preview_scene->background_colour = Colour3f(0.2f);
		engine->addScene(avatar_preview_scene);

		{
			OpenGLSceneRef old_scene = engine->getCurrentScene();
			engine->setCurrentScene(avatar_preview_scene);

			// Set up environment material for the preview scene.
			OpenGLMaterial env_mat;
			engine->setEnvMat(env_mat);
			engine->setSunDir(normalise(Vec4f(1,-1,1,0)));

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

			engine->setCurrentScene(old_scene); // Restore old scene
		}
	}

	// Column 0: avatar header + avatar preview widget
	{
		GLUITextView::CreateArgs args;
		args.background_alpha = 0;
		GLUITextViewRef avatar_header_text = new GLUITextView(*gl_ui, "Preview", Vec2f(0.f), args);
		outer_grid->setCellWidget(/*x=*/0, /*y=*/0, avatar_header_text);
	}
	{
		avatar_preview_widget = new AvatarPreviewGLUIWidget(*gl_ui);
		outer_grid->setCellWidget(/*x=*/0, /*y=*/1, avatar_preview_widget);
	}

	const float GEAR_GRID_BACKGROUND_ALPHA = 0;

	// Column 1: equipped gear header + inner grid
	{
		GLUITextView::CreateArgs args;
		args.background_alpha = 0;
		GLUITextViewRef equipped_header_text = new GLUITextView(*gl_ui, "Equipped Gear", Vec2f(0.f), args);
		outer_grid->setCellWidget(/*x=*/1, /*y=*/0, equipped_header_text);
	}
	{
		// 'Equipped gear' grid
		GLUIGridContainer::CreateArgs args;
		args.background_alpha = GEAR_GRID_BACKGROUND_ALPHA;
		args.background_colour =  Colour3f(0.0f);
		args.interior_cell_x_padding_px = INTERIOR_GRID_PADDING_PX;
		args.interior_cell_y_padding_px = INTERIOR_GRID_PADDING_PX;
		equipped_grid = new GLUIGridContainer(*gl_ui, args);
		equipped_grid->setPosAndDims(Vec2f(0.f), Vec2f(0.01f));
		equipped_grid->debug_name = "GearInventoryUI equipped_grid";
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
		args.background_alpha = GEAR_GRID_BACKGROUND_ALPHA;
		args.background_colour =  Colour3f(0.0f);
		args.interior_cell_x_padding_px = INTERIOR_GRID_PADDING_PX;
		args.interior_cell_y_padding_px = INTERIOR_GRID_PADDING_PX;
		all_gear_grid = new GLUIGridContainer(*gl_ui, args);
		all_gear_grid->setPosAndDims(Vec2f(0.f), Vec2f(0.01f));
		all_gear_grid->debug_name = "GearInventoryUI all_gear_grid";
		outer_grid->setCellWidget(/*x=*/2, /*y=*/1, all_gear_grid);
	}

	// Window containing the outer grid
	{
		GLUIWindow::CreateArgs args;
		args.title = "Gear Inventory";
		args.background_alpha = 0.9f;
		args.background_colour = Colour3f(0.1f);
		args.padding_px = 0;
		args.z = -0.2f;
		window = new GLUIWindow(*gl_ui, args);
		window->setBodyWidget(outer_grid);
		window->handler = this;
		gl_ui->addWidget(window);
	}

	// Create FBO/texture sized for the current viewport. Also sets avatar_preview_widget display size.
	recreateAvatarPreviewFBO();
}


GearInventoryUI::~GearInventoryUI()
{
	// Clean up the avatar preview scene and its objects
	if(avatar_preview_scene)
	{
		OpenGLEngine* engine = gui_client->opengl_engine.ptr();
		OpenGLSceneRef old_scene = engine->getCurrentScene();
		engine->setCurrentScene(avatar_preview_scene);

		checkRemoveObAndSetRefToNull(*engine, avatar_preview_gl_ob);

		// Remove old gear preview objects
		for(auto& graphics : equipped_gear_graphics)
			checkRemoveObAndSetRefToNull(engine, graphics.gear_gl_ob);
		equipped_gear_graphics.clear();

		engine->setCurrentScene(old_scene);
		engine->removeScene(avatar_preview_scene);
	}

	for(auto& ui : equipped_gear_ui)
		checkRemoveAndDeleteWidget(gl_ui, ui.thumbnail);
	for(auto& ui : all_gear_ui)
		checkRemoveAndDeleteWidget(gl_ui, ui.thumbnail);

	checkRemoveAndDeleteWidget(gl_ui, window);
}


void GearInventoryUI::recreateAvatarPreviewFBO()
{
	const float w_dev_ind_px = THUMBNAIL_SIZE_PX * GEAR_GRID_COLS + INTERIOR_GRID_PADDING_PX*2 * (GEAR_GRID_COLS - 1);
	const float h_dev_ind_px = THUMBNAIL_SIZE_PX * GEAR_GRID_ROWS + INTERIOR_GRID_PADDING_PX*2 * (GEAR_GRID_ROWS - 1);

	const int avatar_preview_w = myMax(16, (int)(gl_ui->getDevicePixelRatio() * w_dev_ind_px));
	const int avatar_preview_h = myMax(16, (int)(gl_ui->getDevicePixelRatio() * h_dev_ind_px));

	if(avatar_preview_widget)
	{
		avatar_preview_widget->recreateFBO(avatar_preview_w, avatar_preview_h);

		avatar_preview_widget->setFixedDimsPx(Vec2f(w_dev_ind_px, h_dev_ind_px), *gl_ui);
	}
}


static std::string thumbnailPathForGearItem(GUIClient* gui_client, const GearItem& item)
{
	if(!item.preview_image_URL.empty())
	{
		if(gui_client->resource_manager->isFileForURLPresent(item.preview_image_URL))
			return gui_client->resource_manager->pathForURL(item.preview_image_URL);
		else
		{
			// Start downloading it
			DownloadingResourceInfo info;
			info.pos = gui_client->cam_controller.getFirstPersonPosition();
			info.size_factor = 1.f;
			info.used_by_other = true;
			gui_client->startDownloadingResource(item.preview_image_URL, gui_client->cam_controller.getFirstPersonPosition().toVec4fPoint(),
				1.f, info);
		}
	}

	return gui_client->resources_dir_path + "/dot.png"; // Placeholder until resource is downloaded
}


static const float    DUMMY_ITEM_ALPHA = 0.5;
static const Colour3f DUMMY_ITEM_COLOUR = Colour3f(0.3f);


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
		args.tooltip = "Unequip " + item->name;
		args.sizing_type_x = GLUIWidget::SizingType_FixedSizePx;
		args.sizing_type_y = GLUIWidget::SizingType_FixedSizePx;
		args.fixed_size = Vec2f(THUMBNAIL_SIZE_PX);
		GLUIButtonRef thumbnail = new GLUIButton(*gl_ui, thumbnailPathForGearItem(gui_client, *item), args);
		thumbnail->handler = this;
		equipped_grid->setCellWidget(/*x=*/(int)(i % GEAR_GRID_COLS), /*y=*/(int)(i / GEAR_GRID_COLS), thumbnail);

		GearItemUI ui;
		ui.thumbnail = thumbnail;
		ui.gear_item = item;
		equipped_gear_ui.push_back(ui);
	}

	// Add dummy widgets to make grid obvious
	for(size_t i=equipped_gear.items.size(); i<GEAR_GRID_COLS * GEAR_GRID_ROWS; ++i)
	{
		GLUIInertWidget::CreateArgs args;
		args.sizing_type_x = GLUIWidget::SizingType_FixedSizePx;
		args.sizing_type_y = GLUIWidget::SizingType_FixedSizePx;
		args.fixed_size = Vec2f(THUMBNAIL_SIZE_PX);
		args.background_alpha  = DUMMY_ITEM_ALPHA;
		args.background_colour = DUMMY_ITEM_COLOUR;
		GLUIInertWidgetRef dummy_button = new GLUIInertWidget(*gl_ui, args);
		equipped_grid->setCellWidget(/*x=*/(int)(i % GEAR_GRID_COLS), /*y=*/(int)(i / GEAR_GRID_COLS), dummy_button);
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
		args.tooltip = "Equip " + item->name;
		args.sizing_type_x = GLUIWidget::SizingType_FixedSizePx;
		args.sizing_type_y = GLUIWidget::SizingType_FixedSizePx;
		args.fixed_size = Vec2f(THUMBNAIL_SIZE_PX);
		GLUIButtonRef thumbnail = new GLUIButton(*gl_ui, thumbnailPathForGearItem(gui_client, *item), args);
		thumbnail->handler = this;
		all_gear_grid->setCellWidget(/*x=*/(int)(i % GEAR_GRID_COLS), /*y=*/(int)(i / GEAR_GRID_COLS), thumbnail);

		GearItemUI ui;
		ui.thumbnail = thumbnail;
		ui.gear_item = item;
		all_gear_ui.push_back(ui);
	}

	// Add dummy widgets to make grid obvious
	for(size_t i=all_gear.items.size(); i<GEAR_GRID_COLS * GEAR_GRID_ROWS; ++i)
	{
		GLUIInertWidget::CreateArgs args;
		args.sizing_type_x = GLUIWidget::SizingType_FixedSizePx;
		args.sizing_type_y = GLUIWidget::SizingType_FixedSizePx;
		args.fixed_size = Vec2f(THUMBNAIL_SIZE_PX);
		args.background_alpha  = DUMMY_ITEM_ALPHA;
		args.background_colour = DUMMY_ITEM_COLOUR;
		GLUIInertWidgetRef dummy_button = new GLUIInertWidget(*gl_ui, args);
		all_gear_grid->setCellWidget(/*x=*/(int)(i % GEAR_GRID_COLS), /*y=*/(int)(i / GEAR_GRID_COLS), dummy_button);
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
	if(avatar_preview_scene.isNull())// || avatar_preview_widget->avatar_preview_fbo.isNull())
		return;

	OpenGLEngine* engine = gui_client->opengl_engine.ptr();

	// Save current state
	OpenGLSceneRef old_scene = engine->getCurrentScene();
	FrameBufferRef old_fbo = engine->getTargetFrameBuffer();
	

	engine->setCurrentScene(avatar_preview_scene);
	
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

	avatar_preview_widget->renderAvatarPreview();


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
			checkRemoveObAndSetRefToNull(engine, graphics.gear_gl_ob);
		equipped_gear_graphics.clear();

		// Add gear GL objects
		for(size_t i=0; i<av_graphics.equipped_gear_graphics.size(); ++i)
		{
			const GLObject* gear_gl_ob = av_graphics.equipped_gear_graphics[i].gear_gl_ob.ptr();
			GLObjectRef preview_ob;
			if(gear_gl_ob)
			{
				preview_ob = engine->allocateObject();
				preview_ob->mesh_data = gear_gl_ob->mesh_data;
				preview_ob->materials = gear_gl_ob->materials;
				preview_ob->ob_to_world_matrix = Matrix4f::translationMatrix(0, 0, AvatarGraphics::getEyeHeight()) * gear_gl_ob->ob_to_world_matrix;

				engine->addObject(preview_ob);
			}

			EquippedGearGraphics gear_graphics = av_graphics.equipped_gear_graphics[i];
			gear_graphics.gear_gl_ob = preview_ob;

			equipped_gear_graphics.push_back(gear_graphics);
		}
	}

	engine->setCurrentScene(old_scene);
}


void GearInventoryUI::handleUploadedTexture(const OpenGLTextureKey& path, const URLString& URL, const OpenGLTextureRef& opengl_tex)
{
	conPrint("GearInventoryUI::handleUploadedTexture: " + toStdString(path));

	for(auto& ui : equipped_gear_ui)
		if(ui.gear_item->model_url == URL)
			ui.thumbnail->setTexture(opengl_tex);

	for(auto& ui : all_gear_ui)
		if(ui.gear_item->model_url == URL)
			ui.thumbnail->setTexture(opengl_tex);
}


void GearInventoryUI::updateWidgetPositions()
{
	window->recomputeLayout();

	// Centre window win viewport
	const float left_margin = myMax(0.f, (2.f - window->getDims().x) / 2);
	const float bot_margin  = myMax(0.f, (gl_ui->getViewportMinMaxY()*2 - window->getDims().y) / 2);
	
	window->setPos(Vec2f(-1.f + left_margin, -gl_ui->getViewportMinMaxY() + bot_margin));
}


void GearInventoryUI::viewportResized(int w, int h)
{
	recreateAvatarPreviewFBO();

	updateWidgetPositions();
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
