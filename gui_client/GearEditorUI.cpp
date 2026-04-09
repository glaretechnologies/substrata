/*=====================================================================
GearEditorUI.cpp
----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "GearEditorUI.h"


#include "AvatarPreviewGLUIWidget.h"
#include "GUIClient.h"
#include "../shared/ResourceManager.h"
#include <opengl/ui/GLUIInertWidget.h>
#include <opengl/ui/GLUISpinBox.h>
#include <opengl/ui/GLUIDropDownList.h>
#include <opengl/OpenGLEngine.h>
#include <opengl/FrameBuffer.h>
#include <opengl/RenderBuffer.h>
#include <opengl/IncludeOpenGL.h>
#include <graphics/SRGBUtils.h>
#include <maths/Matrix4f.h>
#include <maths/GeometrySampling.h>


static const float INTERIOR_GRID_PADDING_PX = 6;


// Ready Player Me (RPM) bone names.
// These seem to be from the Mixamo rig, but with the "mixamorig:" prefix removed.
// These are the bone names our animation data uses.
static const char* RPM_bone_names[] = {
	"Hips",
	"Spine",
	"Spine1",
	"Spine2",
	"Neck",
	"Head",
	"HeadTop_End",
	"LeftEye",
	"RightEye",
	"LeftShoulder",
	"LeftArm",
	"LeftForeArm",
	"LeftHand",
	"LeftHandThumb1",
	"LeftHandThumb2",
	"LeftHandThumb3",
	"LeftHandThumb4",
	"LeftHandIndex1",
	"LeftHandIndex2",
	"LeftHandIndex3",
	"LeftHandIndex4",
	"LeftHandMiddle1",
	"LeftHandMiddle2",
	"LeftHandMiddle3",
	"LeftHandMiddle4",
	"LeftHandRing1",
	"LeftHandRing2",
	"LeftHandRing3",
	"LeftHandRing4",
	"LeftHandPinky1",
	"LeftHandPinky2",
	"LeftHandPinky3",
	"LeftHandPinky4",
	"RightShoulder",
	"RightArm",
	"RightForeArm",
	"RightHand",
	"RightHandThumb1",
	"RightHandThumb2",
	"RightHandThumb3",
	"RightHandThumb4",
	"RightHandRing1",
	"RightHandRing2",
	"RightHandRing3",
	"RightHandRing4",
	"RightHandMiddle1",
	"RightHandMiddle2",
	"RightHandMiddle3",
	"RightHandMiddle4",
	"RightHandIndex1",
	"RightHandIndex2",
	"RightHandIndex3",
	"RightHandIndex4",
	"RightHandPinky1",
	"RightHandPinky2",
	"RightHandPinky3",
	"RightHandPinky4",
	"LeftUpLeg",
	"LeftLeg",
	"LeftFoot",
	"LeftToeBase",
	"LeftToe_End",
	"RightUpLeg",
	"RightLeg",
	"RightFoot",
	"RightToeBase",
	"RightToe_End"
};


GearEditorUI::GearEditorUI(GUIClient* gui_client_, GLUIRef gl_ui_, GearItemRef gear_item_)
{
	gui_client = gui_client_;
	gl_ui = gl_ui_;
	gear_item = gear_item_;
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
	//{
	//	GLUITextView::CreateArgs args;
	//	args.background_alpha = 0;
	//	GLUITextViewRef avatar_header_text = new GLUITextView(*gl_ui, "Preview", Vec2f(0.f), args);
	//	outer_grid->setCellWidget(/*x=*/0, /*y=*/0, avatar_header_text);
	//}
	{
		avatar_preview_widget = new AvatarPreviewGLUIWidget(*gl_ui);
		outer_grid->setCellWidget(/*x=*/0, /*y=*/0, avatar_preview_widget);
	}

	const float GEAR_GRID_BACKGROUND_ALPHA = 0;


	/*
	
	                  outer_grid
	-------------------------------------------------
	|            |                                  |
	|            |   numeric_controls_grid          |
	|   avatar   |                                  |
	|   preview  |----------------------------------|
	|            |                                  |
	|            |                                  |
	-------------------------------------------------
	              <-------- controls_grid ---------->
	*/

	// Column 1: Controls
	//{
	//	GLUITextView::CreateArgs args;
	//	args.background_alpha = 0;
	//	GLUITextViewRef equipped_header_text = new GLUITextView(*gl_ui, "C", Vec2f(0.f), args);
	//	outer_grid->setCellWidget(/*x=*/1, /*y=*/0, equipped_header_text);
	//}
	{
		// Controls grid
		GLUIGridContainer::CreateArgs args;
		args.background_alpha = GEAR_GRID_BACKGROUND_ALPHA;
		args.background_colour =  Colour3f(0.0f);
		args.interior_cell_x_padding_px = INTERIOR_GRID_PADDING_PX;
		args.interior_cell_y_padding_px = INTERIOR_GRID_PADDING_PX;
		controls_grid = new GLUIGridContainer(*gl_ui, args);
		controls_grid->debug_name = "controls_grid";
		outer_grid->setCellWidget(/*x=*/1, /*y=*/0, controls_grid);
	}

	//---------------------- Add bone control ----------------------
	{
		const int controls_grid_y = (int)controls_grid->cell_widgets.getHeight();

		// Label
		{
			GLUITextView::CreateArgs args;
			args.background_alpha = 0;
			GLUITextViewRef text = new GLUITextView(*gl_ui, "Bone", Vec2f(0.f), args);
			controls_grid->setCellWidget(/*x=*/0, /*y=*/controls_grid_y, text);
		}

		// name line edit
		{
			GLUIDropDownList::CreateArgs args;
			args.options.resize(staticArrayNumElems(RPM_bone_names));
			for(size_t i=0; i<staticArrayNumElems(RPM_bone_names); ++i)
				args.options[i] = std::string(RPM_bone_names[i]);
			args.initial_value = gear_item->bone_name;

			bone_list = new GLUIDropDownList(*gl_ui, args);
			bone_list->handler_func = [this](GLUIDropDownListValueChangedEvent&) { this->gearItemChanged(); };
			controls_grid->setCellWidget(/*x=*/0, /*y=*/controls_grid_y + 1, bone_list);
		}
	}




	GLUIGridContainerRef numeric_controls_grid;
	{
		const int controls_grid_y = (int)controls_grid->cell_widgets.getHeight();

		// numeric controls grid
		GLUIGridContainer::CreateArgs args;
		args.background_alpha = GEAR_GRID_BACKGROUND_ALPHA;
		args.background_colour =  Colour3f(0.0f);
		args.interior_cell_x_padding_px = INTERIOR_GRID_PADDING_PX;
		args.interior_cell_y_padding_px = INTERIOR_GRID_PADDING_PX;
		numeric_controls_grid = new GLUIGridContainer(*gl_ui, args);
		numeric_controls_grid->debug_name = "controls_grid";
		controls_grid->setCellWidget(/*x=*/0, /*y=*/controls_grid_y, numeric_controls_grid);
	}

	int numeric_controls_grid_y = 0;

	//------------------------ Add scale controls ----------------------
	// Label
	{
		GLUITextView::CreateArgs args;
		args.background_alpha = 0;
		GLUITextViewRef equipped_header_text = new GLUITextView(*gl_ui, "Scale x/y/z", Vec2f(0.f), args);
		numeric_controls_grid->setCellWidget(/*x=*/0, /*y=*/numeric_controls_grid_y, equipped_header_text);
	}

	// X/Y/Z scale spinboxes
	{
		GLUISpinBox::CreateArgs args;
		args.step = 0.05;
		args.initial_value = gear_item->scale.x;
		scale_x_spinbox = new GLUISpinBox(*gl_ui, args);
		scale_x_spinbox->handler_func = [this] (GLUISpinBoxValueChangedEvent& ev) { this->gearItemChanged(); };
		numeric_controls_grid->setCellWidget(/*x=*/1, /*y=*/numeric_controls_grid_y, scale_x_spinbox);
	}
	{
		GLUISpinBox::CreateArgs args;
		args.step = 0.05;
		args.initial_value = gear_item->scale.y;
		scale_y_spinbox = new GLUISpinBox(*gl_ui, args);
		scale_y_spinbox->handler_func = [this] (GLUISpinBoxValueChangedEvent& ev) { this->gearItemChanged(); };
		numeric_controls_grid->setCellWidget(/*x=*/2, /*y=*/numeric_controls_grid_y, scale_y_spinbox);
	}
	{
		GLUISpinBox::CreateArgs args;
		args.step = 0.05;
		args.initial_value = gear_item->scale.z;
		scale_z_spinbox = new GLUISpinBox(*gl_ui, args);
		scale_z_spinbox->handler_func = [this] (GLUISpinBoxValueChangedEvent& ev) { this->gearItemChanged(); };
		numeric_controls_grid->setCellWidget(/*x=*/3, /*y=*/numeric_controls_grid_y, scale_z_spinbox);
	}
	numeric_controls_grid_y++;

	//------------------------ Add rotation controls ----------------------
	// Label
	{
		GLUITextView::CreateArgs args;
		args.background_alpha = 0;
		GLUITextViewRef equipped_header_text = new GLUITextView(*gl_ui, "Rotation z/y/x", Vec2f(0.f), args);
		numeric_controls_grid->setCellWidget(/*x=*/0, /*y=*/numeric_controls_grid_y, equipped_header_text);
	}

	// See ObjectEditor::setTransformFromObject
	const Matrix3f rot_mat = Matrix3f::rotationMatrix(normalise(gear_item->axis), gear_item->angle);
	const Vec3f angles = rot_mat.getAngles();


	// Z/Y/X rotation spinboxes
	{
		GLUISpinBox::CreateArgs args;
		args.initial_value = ::radToDegree(angles.z);
		rot_z_spinbox = new GLUISpinBox(*gl_ui, args);
		rot_z_spinbox->handler_func = [this] (GLUISpinBoxValueChangedEvent& ev) { this->gearItemChanged(); };
		numeric_controls_grid->setCellWidget(/*x=*/1, /*y=*/numeric_controls_grid_y, rot_z_spinbox);
	}
	{
		GLUISpinBox::CreateArgs args;
		args.initial_value = ::radToDegree(angles.y);
		rot_y_spinbox = new GLUISpinBox(*gl_ui, args);
		rot_y_spinbox->handler_func = [this] (GLUISpinBoxValueChangedEvent& ev) { this->gearItemChanged(); };
		numeric_controls_grid->setCellWidget(/*x=*/2, /*y=*/numeric_controls_grid_y, rot_y_spinbox);
	}
	{
		GLUISpinBox::CreateArgs args;
		args.initial_value = ::radToDegree(angles.x);
		rot_x_spinbox = new GLUISpinBox(*gl_ui, args);
		rot_x_spinbox->handler_func = [this] (GLUISpinBoxValueChangedEvent& ev) { this->gearItemChanged(); };
		numeric_controls_grid->setCellWidget(/*x=*/3, /*y=*/numeric_controls_grid_y, rot_x_spinbox);
	}
	numeric_controls_grid_y++;

	//------------------------ Add position controls ----------------------
	// Label
	{
		GLUITextView::CreateArgs args;
		args.background_alpha = 0;
		GLUITextViewRef equipped_header_text = new GLUITextView(*gl_ui, "Position x/y/z", Vec2f(0.f), args);
		numeric_controls_grid->setCellWidget(/*x=*/0, /*y=*/numeric_controls_grid_y, equipped_header_text);
	}

	const double POSITION_STEP = 0.01;

	// X/Y/Z position spinboxes
	{
		GLUISpinBox::CreateArgs args;
		args.step = POSITION_STEP;
		args.initial_value = gear_item->translation.x;
		pos_x_spinbox = new GLUISpinBox(*gl_ui, args);
		pos_x_spinbox->handler_func = [this] (GLUISpinBoxValueChangedEvent& ev) { this->gearItemChanged(); };
		numeric_controls_grid->setCellWidget(/*x=*/1, /*y=*/numeric_controls_grid_y, pos_x_spinbox);
	}
	{
		GLUISpinBox::CreateArgs args;
		args.step = POSITION_STEP;
		args.initial_value = gear_item->translation.y;
		pos_y_spinbox = new GLUISpinBox(*gl_ui, args);
		pos_y_spinbox->handler_func = [this] (GLUISpinBoxValueChangedEvent& ev) { this->gearItemChanged(); };
		numeric_controls_grid->setCellWidget(/*x=*/2, /*y=*/numeric_controls_grid_y, pos_y_spinbox);
	}
	{
		GLUISpinBox::CreateArgs args;
		args.step = POSITION_STEP;
		args.initial_value = gear_item->translation.z;
		pos_z_spinbox = new GLUISpinBox(*gl_ui, args);
		pos_z_spinbox->handler_func = [this] (GLUISpinBoxValueChangedEvent& ev) { this->gearItemChanged(); };
		numeric_controls_grid->setCellWidget(/*x=*/3, /*y=*/numeric_controls_grid_y, pos_z_spinbox);
	}
	numeric_controls_grid_y++;


	// Add name control
	{
		const int controls_grid_y = (int)controls_grid->cell_widgets.getHeight();
		// Label
		{
			GLUITextView::CreateArgs args;
			args.background_alpha = 0;
			GLUITextViewRef text = new GLUITextView(*gl_ui, "Name", Vec2f(0.f), args);
			controls_grid->setCellWidget(/*x=*/0, /*y=*/controls_grid_y, text);
		}

		// name line edit
		{
			GLUILineEdit::CreateArgs args;
			args.sizing_type_x = GLUIWidget::SizingType_FixedSizePx;
			args.fixed_size.x = 600;
			//args.background_alpha = 0;
			GLUILineEditRef name_line_edit = new GLUILineEdit(*gl_ui, Vec2f(0.f), args);
			name_line_edit->setText(gear_item->name);
			controls_grid->setCellWidget(/*x=*/0, /*y=*/controls_grid_y + 1, name_line_edit);
		}
	}

	// Add description control
	{
		const int controls_grid_y = (int)controls_grid->cell_widgets.getHeight();
		// Label
		{
			GLUITextView::CreateArgs args;
			args.background_alpha = 0;
			GLUITextViewRef text = new GLUITextView(*gl_ui, "Description", Vec2f(0.f), args);
			controls_grid->setCellWidget(/*x=*/0, /*y=*/controls_grid_y, text);
		}

		// description line edit.  TODO: add and use multiline edit
		{
			GLUILineEdit::CreateArgs args;
			args.sizing_type_x = GLUIWidget::SizingType_FixedSizePx;
			args.fixed_size.x = 600;
			//args.background_alpha = 0;
			GLUILineEditRef desc_line_edit = new GLUILineEdit(*gl_ui, Vec2f(0.f), args);
			desc_line_edit->setText(gear_item->description);
			controls_grid->setCellWidget(/*x=*/0, /*y=*/controls_grid_y + 1, desc_line_edit);
		}
	}

	// Window containing the outer grid
	{
		GLUIWindow::CreateArgs args;
		args.title = "Gear Editor";
		args.background_alpha = 0.9f;
		args.background_colour = Colour3f(0.1f);
		args.padding_px = 0;
		args.z = -0.3f;
		window = new GLUIWindow(*gl_ui, args);
		window->setBodyWidget(outer_grid);
		window->handler = this;
		window->debug_name = "gear editor window";

		window->recomputeLayout(); // TEMP

		gl_ui->addWidget(window);
	}

	// Create FBO/texture sized for the current viewport. Also sets avatar_preview_widget display size.
	recreateAvatarPreviewFBO();


	updateWidgetPositions();
}


GearEditorUI::~GearEditorUI()
{
	// Clean up the avatar preview scene and its objects
	if(avatar_preview_scene)
	{
		OpenGLEngine* engine = gui_client->opengl_engine.ptr();
		OpenGLSceneRef old_scene = engine->getCurrentScene();
		engine->setCurrentScene(avatar_preview_scene);

		checkRemoveObAndSetRefToNull(*engine, avatar_preview_gl_ob);

		// Remove old gear preview objects
		checkRemoveObAndSetRefToNull(engine, equipped_gear_graphics.gear_gl_ob);

		engine->setCurrentScene(old_scene);
		engine->removeScene(avatar_preview_scene);
	}

	checkRemoveAndDeleteWidget(gl_ui, window);
}


void GearEditorUI::recreateAvatarPreviewFBO()
{
	const float w_dev_ind_px = 500; // THUMBNAIL_SIZE_PX * GEAR_GRID_COLS + INTERIOR_GRID_PADDING_PX*2 * (GEAR_GRID_COLS - 1);
	const float h_dev_ind_px = 800; // THUMBNAIL_SIZE_PX * GEAR_GRID_ROWS + INTERIOR_GRID_PADDING_PX*2 * (GEAR_GRID_ROWS - 1);

	const int avatar_preview_w = myMax(16, (int)(gl_ui->getDevicePixelRatio() * w_dev_ind_px));
	const int avatar_preview_h = myMax(16, (int)(gl_ui->getDevicePixelRatio() * h_dev_ind_px));

	if(avatar_preview_widget)
	{
		avatar_preview_widget->recreateFBO(avatar_preview_w, avatar_preview_h);

		avatar_preview_widget->setFixedDimsPx(Vec2f(w_dev_ind_px, h_dev_ind_px));
	}
}


void GearEditorUI::gearItemChanged()
{
	conPrint("gearItemChanged");

	// Read values from controls
	gear_item->bone_name = bone_list->getCurrentValue();

	gear_item->translation = Vec3f((float)pos_x_spinbox->getValue(),   (float)pos_y_spinbox->getValue(),   (float)pos_z_spinbox->getValue());
	gear_item->scale       = Vec3f((float)scale_x_spinbox->getValue(), (float)scale_y_spinbox->getValue(), (float)scale_z_spinbox->getValue());

	const Vec3f angles(
		(float)::degreeToRad(this->rot_x_spinbox->getValue()),
		(float)::degreeToRad(this->rot_y_spinbox->getValue()),
		(float)::degreeToRad(this->rot_z_spinbox->getValue())
	);

	// Convert angles to rotation matrix, then the rotation matrix to axis-angle.
	const Matrix3f rot_matrix = Matrix3f::fromAngles(angles);
	rot_matrix.rotationMatrixToAxisAngle(/*unit axis out=*/gear_item->axis, /*angle out=*/gear_item->angle);

	if(gear_item->axis.length() < 1.0e-5f)
	{
		gear_item->axis = Vec3f(0,0,1);
		gear_item->angle = 0;
	}

	this->equipped_gear_graphics.transform = gear_item->gearObToBoneSpaceMatrix();

	this->equipped_gear_graphics.bone_node_i = this->avatar_preview_gl_ob->mesh_data->animation_data.getNodeIndex(gear_item->bone_name);

	gui_client->gearItemChangedOnOurAvatar(gear_item.ptr());
}


void GearEditorUI::think()
{
	renderAvatarPreview();
}


void GearEditorUI::renderAvatarPreview()
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
		EquippedGearGraphics& gear = equipped_gear_graphics;
		if(gear.gear_gl_ob && gear.bone_node_i != -1)
		{
			if(gear.bone_node_i >= 0 && gear.bone_node_i < (int)avatar_preview_gl_ob->anim_node_data.size())
			{
				gear.gear_gl_ob->ob_to_world_matrix = (avatar_preview_gl_ob->ob_to_world_matrix * avatar_preview_gl_ob->anim_node_data[gear.bone_node_i].node_hierarchical_to_object) * gear.transform;

				engine->updateObjectTransformData(*gear.gear_gl_ob);
			}
		}
	}

	avatar_preview_widget->renderAvatarPreview();


	// Restore previous state (viewport will be reset by the main render call)
	engine->setCurrentScene(old_scene);
	engine->setTargetFrameBuffer(old_fbo);
}


void GearEditorUI::setAvatarGLObject(/*const AvatarGraphics& av_graphics, */const Reference<GLObject>& avatar_gl_ob, const Matrix4f& pre_ob_to_world_matrix,
	const std::vector<EquippedGearGraphics>& all_equipped_gear_graphics)
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
		checkRemoveObAndSetRefToNull(engine, equipped_gear_graphics.gear_gl_ob);


		// Try and load gear GL object
		for(size_t i=0; i<all_equipped_gear_graphics.size(); ++i)
		{
			if(all_equipped_gear_graphics[i].gear_id == this->gear_item->id)
			{
				const GLObject* src_gear_gl_ob = all_equipped_gear_graphics[i].gear_gl_ob.ptr();
				GLObjectRef preview_ob;
				if(src_gear_gl_ob)
				{
					preview_ob = engine->allocateObject();
					preview_ob->mesh_data = src_gear_gl_ob->mesh_data;
					preview_ob->materials = src_gear_gl_ob->materials;
					preview_ob->ob_to_world_matrix = Matrix4f::translationMatrix(0, 0, AvatarGraphics::getEyeHeight()) * src_gear_gl_ob->ob_to_world_matrix; // wrong but will be updated
		
					engine->addObject(preview_ob);

					this->equipped_gear_graphics = all_equipped_gear_graphics[i];
					this->equipped_gear_graphics.gear_gl_ob = preview_ob;
				}
			}
		}

		// Add gear GL objects
		//for(size_t i=0; i<av_graphics.equipped_gear_graphics.size(); ++i)
		//{
		//	const GLObject* gear_gl_ob = av_graphics.equipped_gear_graphics[i].gear_gl_ob.ptr();
		//	GLObjectRef preview_ob;
		//	if(gear_gl_ob)
		//	{
		//		preview_ob = engine->allocateObject();
		//		preview_ob->mesh_data = gear_gl_ob->mesh_data;
		//		preview_ob->materials = gear_gl_ob->materials;
		//		preview_ob->ob_to_world_matrix = Matrix4f::translationMatrix(0, 0, AvatarGraphics::getEyeHeight()) * gear_gl_ob->ob_to_world_matrix;
		//
		//		engine->addObject(preview_ob);
		//	}
		//
		//	EquippedGearGraphics gear_graphics = av_graphics.equipped_gear_graphics[i];
		//	gear_graphics.gear_gl_ob = preview_ob;
		//
		//	equipped_gear_graphics = gear_graphics;
		//}
	}

	engine->setCurrentScene(old_scene);
}


void GearEditorUI::handleUploadedTexture(const OpenGLTextureKey& path, const URLString& URL, const OpenGLTextureRef& opengl_tex)
{
	conPrint("GearEditorUI::handleUploadedTexture: " + toStdString(path));
}


void GearEditorUI::updateWidgetPositions()
{
	window->recomputeLayout();

	// Centre window win viewport
	const float left_margin = myMax(0.f, (2.f - window->getDims().x) / 2);
	const float bot_margin  = myMax(0.f, (gl_ui->getViewportMinMaxY()*2 - window->getDims().y) / 2);
	
	window->setPos(Vec2f(-1.f + left_margin, -gl_ui->getViewportMinMaxY() + bot_margin));
}


void GearEditorUI::viewportResized(int w, int h)
{
	recreateAvatarPreviewFBO();

	updateWidgetPositions();
}


void GearEditorUI::eventOccurred(GLUICallbackEvent& event)
{
	if(!gui_client)
		return;
}


void GearEditorUI::closeWindowEventOccurred(GLUICallbackEvent& event)
{
	assert(event.widget == window.ptr());

	close_soon = true; // gui_client should destroy this UI asap.  (Can't tell gui_client to destroy this now as are in event handler from it)
}
