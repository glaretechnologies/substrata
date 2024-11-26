/*=====================================================================
AvatarSettingsDialog.cpp
------------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "AvatarSettingsDialog.h"


#include "AddObjectDialog.h"
#include "ModelLoading.h"
#include "../shared/ResourceManager.h"
#include "../indigo/TextureServer.h"
#include "graphics/SRGBUtils.h"
#include "../dll/include/IndigoMesh.h"
#include "../dll/include/IndigoException.h"
#include "../dll/IndigoStringUtils.h"
#include "../utils/FileUtils.h"
#include "../utils/Exception.h"
#include "../utils/PlatformUtils.h"
#include "../utils/StringUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/FileChecksum.h"
#include "../utils/FileInStream.h"
#include "../utils/TaskManager.h"
#include "../qt/QtUtils.h"
#include "../qt/SignalBlocker.h"
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QErrorMessage>
#include <QtCore/QSettings>
#include <QtCore/QTimer>


AvatarSettingsDialog::AvatarSettingsDialog(const std::string& base_dir_path_, QSettings* settings_, Reference<ResourceManager> resource_manager_)
:	base_dir_path(base_dir_path_),
	settings(settings_),
	resource_manager(resource_manager_),
	done_initial_load(false),
	pre_ob_to_world_matrix(Matrix4f::identity())
{
	setupUi(this);

	texture_server = new TextureServer(/*use_canonical_path_keys=*/false);

	this->usernameLabel->hide();
	this->usernameLineEdit->hide();

	std::string display_str;

	display_str += "<br/><a href=\"https://substrata.readyplayer.me/\">Create a ReadyPlayerMe avatar</a>.  After creating, download and select in file browser above.";

	this->createReadyPlayerMeLabel->setText(QtUtils::toQString(display_str));
	this->createReadyPlayerMeLabel->setOpenExternalLinks(true);

	this->avatarPreviewGLWidget->init(base_dir_path, settings_, texture_server);

	// Load main window geometry and state
	this->restoreGeometry(settings->value("AvatarSettingsDialog/geometry").toByteArray());

	//this->usernameLineEdit->setText(settings->value("username").toString());

	{
		SignalBlocker b(this->avatarSelectWidget);
		this->avatarSelectWidget->setType(FileSelectWidget::Type_File);
		this->avatarSelectWidget->setFilename(settings->value("avatarPath").toString());
	}

	connect(this->avatarSelectWidget, SIGNAL(filenameChanged(QString&)), this, SLOT(avatarFilenameChanged(QString&)));
	connect(this->buttonBox, SIGNAL(accepted()), this, SLOT(accepted()));
	connect(this, SIGNAL(finished(int)), this, SLOT(dialogFinished()));

	connect(this->animationComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(animationComboBoxIndexChanged(int)));

	startTimer(10);
}


AvatarSettingsDialog::~AvatarSettingsDialog()
{
	settings->setValue("AvatarSettingsDialog/geometry", saveGeometry());
}


void AvatarSettingsDialog::shutdownGL()
{
	// Make sure we have set the widget gl context to current as we destroy OpenGL stuff.
	this->avatarPreviewGLWidget->makeCurrent();

	preview_gl_ob = NULL;
	avatarPreviewGLWidget->shutdown();
}


//std::string AvatarSettingsDialog::getAvatarName()
//{
//	return QtUtils::toStdString(usernameLineEdit->text());
//}


// Called when user presses ESC key, or clicks OK or cancel button.
void AvatarSettingsDialog::dialogFinished()
{
	//this->settings->setValue("username", this->usernameLineEdit->text());

	shutdownGL();
}


void AvatarSettingsDialog::accepted()
{
	this->settings->setValue("avatarPath", this->avatarSelectWidget->filename());
	//this->settings->setValue("username", this->usernameLineEdit->text());
}


void AvatarSettingsDialog::avatarFilenameChanged(QString& filename)
{
	const std::string path = QtUtils::toIndString(filename);
	const bool changed = this->result_path != path;
	this->result_path = path;
	
	// conPrint("AvatarSettingsDialog::avatarFilenameChanged: filename = " + path);

	if(changed)
	{
		loadModelIntoPreview(path, /*show_error_dialogs=*/true);
	}
}


void AvatarSettingsDialog::animationComboBoxIndexChanged(int index)
{
	const std::string anim_name = QtUtils::toStdString(this->animationComboBox->itemText(index));
	preview_gl_ob->current_anim_i = myMax(0, preview_gl_ob->mesh_data->animation_data.getAnimationIndex(anim_name));
}


void AvatarSettingsDialog::loadModelIntoPreview(const std::string& local_path, bool show_error_dialogs)
{
	const std::string use_local_path = local_path.empty() ? 
		(base_dir_path + "/data/resources/xbot_glb_3242545562312850498.bmesh") :
		local_path;

	this->avatarPreviewGLWidget->makeCurrent();

	this->pre_ob_to_world_matrix = Matrix4f::identity();

	// Try and load model
	try
	{
		if(preview_gl_ob.nonNull())
			avatarPreviewGLWidget->opengl_engine->removeObject(preview_gl_ob); // Remove previous object from engine.

		ModelLoading::MakeGLObjectResults results;
		ModelLoading::makeGLObjectForModelFile(*avatarPreviewGLWidget->opengl_engine, *avatarPreviewGLWidget->opengl_engine->vert_buf_allocator, use_local_path, /*do_opengl_stuff=*/true,
			results
		);

		this->preview_gl_ob = results.gl_ob;
		this->loaded_mesh = results.batched_mesh;
		this->loaded_materials = results.materials;

		if(local_path.empty()) // If we used xbot_glb_3242545562312850498.bmesh we need to rotate it upright
		{
			this->preview_gl_ob->ob_to_world_matrix = Matrix4f::rotationAroundXAxis(Maths::pi_2<float>());

			this->preview_gl_ob->materials[0].albedo_linear_rgb = toLinearSRGB(Avatar::defaultMat0Col());
			this->preview_gl_ob->materials[0].metallic_frac = Avatar::default_mat0_metallic_frac;
			this->preview_gl_ob->materials[0].roughness = Avatar::default_mat0_roughness;
			this->preview_gl_ob->materials[0].albedo_texture = NULL;
			this->preview_gl_ob->materials[0].tex_path.clear();

			this->preview_gl_ob->materials[1].albedo_linear_rgb = toLinearSRGB(Avatar::defaultMat1Col());
			this->preview_gl_ob->materials[1].metallic_frac = Avatar::default_mat1_metallic_frac;
			this->preview_gl_ob->materials[1].albedo_texture = NULL;
			this->preview_gl_ob->materials[1].tex_path.clear();


			loaded_materials.resize(2);
			loaded_materials[0] = new WorldMaterial();
			loaded_materials[0]->colour_rgb = Avatar::defaultMat0Col();
			loaded_materials[0]->metallic_fraction.val = Avatar::default_mat0_metallic_frac;
			loaded_materials[0]->roughness.val = Avatar::default_mat0_roughness;

			loaded_materials[1] = new WorldMaterial();
			loaded_materials[1]->colour_rgb = Avatar::defaultMat1Col();
			loaded_materials[1]->metallic_fraction.val = Avatar::default_mat1_metallic_frac;
		}

		/*Vec4f original_left_eye_pos = preview_gl_ob->mesh_data->animation_data.getNodePositionModelSpace("LeftEye");
		if(original_left_eye_pos == Vec4f(0,0,0,1))
		{
			assert(0);
			original_left_eye_pos = Vec4f(0,1.67,0,1);
		}*/

		Vec4f original_toe_pos = preview_gl_ob->mesh_data->animation_data.getNodePositionModelSpace("LeftToe_End", /*use_retarget_adjustment=*/false);

		// TEMP: Load animation data for ready-player-me type avatars
		//float eye_height_adjustment = 0;
		float foot_bottom_height = original_toe_pos[1] - 0.0362269469; // Should be ~= 0
		//printVar(foot_bottom_height);
		if(true)
		{
			FileInStream file(base_dir_path + "/data/resources/extracted_avatar_anim.bin");
			preview_gl_ob->mesh_data->animation_data.loadAndRetargetAnim(file);

			// If we loaded the extracted_avatar_anim bone data, then the avatar will be floating off the ground for female leg lengths, so move down.
			//eye_height_adjustment = -1.67 + original_left_eye_pos[1];

			Vec4f new_toe_pos = preview_gl_ob->mesh_data->animation_data.getNodePositionModelSpace("LeftToe_End", /*use_retarget_adjustment=*/true);
			conPrint("new_toe_pos: " + new_toe_pos.toStringNSigFigs(4));

			foot_bottom_height = new_toe_pos[1] - 0.03; // Height of foot bottom for avatar with retargetted animation, off ground.

			conPrint("foot_bottom_height: " + doubleToStringNSigFigs(foot_bottom_height, 4));
		}

		// Populate current animation combobox with anim names
		animationComboBox->clear();
		for(size_t i=0; i<preview_gl_ob->mesh_data->animation_data.animations.size(); ++i)
			animationComboBox->addItem(QtUtils::toQString(preview_gl_ob->mesh_data->animation_data.animations[i]->name));
		animationComboBox->setMaxVisibleItems(50);

		// Select Idle animation initially
		preview_gl_ob->current_anim_i = myMax(0, preview_gl_ob->mesh_data->animation_data.getAnimationIndex("Idle"));
		SignalBlocker::setCurrentIndex(animationComboBox, preview_gl_ob->current_anim_i);

		// Construct transformation to bring ready-player-me avatars to z-up and standing on the ground.
		// We want to translate the avatar down from 1.67 metres in the sky (which is the default substrata eye height), to the ground
		this->pre_ob_to_world_matrix = Matrix4f::translationMatrix(0, 0, -1.67 - foot_bottom_height) * preview_gl_ob->ob_to_world_matrix;

		preview_gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(0, 0, -foot_bottom_height) * preview_gl_ob->ob_to_world_matrix;

		// Try and load textures
		AddObjectDialog::tryLoadTexturesForPreviewOb(preview_gl_ob, this->loaded_materials, avatarPreviewGLWidget->opengl_engine.ptr(), *texture_server, this);

		avatarPreviewGLWidget->opengl_engine->addObject(preview_gl_ob);
	}
	catch(Indigo::IndigoException& e)
	{
		this->loaded_mesh = NULL;

		if(show_error_dialogs)
			QtUtils::showErrorMessageDialog(QtUtils::toQString(e.what()), this);
	}
	catch(glare::Exception& e)
	{
		this->loaded_mesh = NULL;

		if(show_error_dialogs)
			QtUtils::showErrorMessageDialog(QtUtils::toQString(e.what()), this);
	}
}


// Will be called when the user clicks the 'X' button.
void AvatarSettingsDialog::closeEvent(QCloseEvent* event)
{
	shutdownGL();
}


void AvatarSettingsDialog::timerEvent(QTimerEvent* event)
{
	avatarPreviewGLWidget->makeCurrent();
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	avatarPreviewGLWidget->update();
#else
	avatarPreviewGLWidget->updateGL();
#endif

	// Once the OpenGL widget has initialised, we can add the model.
	if(avatarPreviewGLWidget->opengl_engine.nonNull() && avatarPreviewGLWidget->opengl_engine->initSucceeded() && !done_initial_load)
	{
		const QString path = settings->value("avatarPath").toString();
		this->result_path = QtUtils::toStdString(path);
		loadModelIntoPreview(QtUtils::toStdString(path), /*show_error_dialogs=*/false);
		done_initial_load = true;
	}
}
