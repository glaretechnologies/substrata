/*=====================================================================
AvatarSettingsDialog.cpp
------------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "AvatarSettingsDialog.h"


#include "ModelLoading.h"
#include "../shared/ResourceManager.h"
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


AvatarSettingsDialog::AvatarSettingsDialog(const std::string& base_dir_path_, QSettings* settings_, TextureServer* texture_server_ptr,
	Reference<ResourceManager> resource_manager_)
:	base_dir_path(base_dir_path_),
	settings(settings_),
	resource_manager(resource_manager_),
	done_initial_load(false),
	pre_ob_to_world_matrix(Matrix4f::identity())
{
	setupUi(this);

	this->usernameLabel->hide();
	this->usernameLineEdit->hide();

	std::string display_str;

	display_str += "<br/><a href=\"https://substrata.readyplayer.me/\">Create a ReadyPlayerMe avatar</a>.  After creating, download and select in file browser above.";

	this->createReadyPlayerMeLabel->setText(QtUtils::toQString(display_str));
	this->createReadyPlayerMeLabel->setOpenExternalLinks(true);

	this->avatarPreviewGLWidget->setBaseDir(base_dir_path);
	this->avatarPreviewGLWidget->texture_server_ptr = texture_server_ptr;

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

	connect(this->animationComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(animationComboBoxIndexChanged(int)));

	this->avatarPreviewGLWidget->texture_server_ptr = texture_server_ptr;

	startTimer(10);
}


AvatarSettingsDialog::~AvatarSettingsDialog()
{
	settings->setValue("AvatarSettingsDialog/geometry", saveGeometry());
}


void AvatarSettingsDialog::shutdownGL()
{
	// Make sure we have set the gl context to current as we destroy avatarPreviewGLWidget.
	preview_gl_ob = NULL;
	avatarPreviewGLWidget->shutdown();
}


//std::string AvatarSettingsDialog::getAvatarName()
//{
//	return QtUtils::toStdString(usernameLineEdit->text());
//}


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
		(base_dir_path + "/resources/xbot.glb") :
		local_path;

	this->avatarPreviewGLWidget->makeCurrent();

	this->loaded_object = new WorldObject();
	this->loaded_object->scale.set(1, 1, 1);
	this->loaded_object->axis = Vec3f(1, 0, 0);
	this->loaded_object->angle = 0;
	this->pre_ob_to_world_matrix = Matrix4f::identity();

	// Try and load model
	try
	{
		if(preview_gl_ob.nonNull())
			avatarPreviewGLWidget->opengl_engine->removeObject(preview_gl_ob); // Remove previous object from engine.

		glare::TaskManager task_manager;

		preview_gl_ob = ModelLoading::makeGLObjectForModelFile(*avatarPreviewGLWidget->opengl_engine, *avatarPreviewGLWidget->opengl_engine->vert_buf_allocator, task_manager, use_local_path,
			this->loaded_mesh, // mesh out
			*this->loaded_object
		);

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
			FileInStream file(base_dir_path + "/resources/extracted_avatar_anim.bin");
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
		for(size_t i=0; i<preview_gl_ob->materials.size(); ++i)
		{
			if(!preview_gl_ob->materials[i].tex_path.empty() && !hasExtension(preview_gl_ob->materials[i].tex_path, "mp4"))
			{
				preview_gl_ob->materials[i].albedo_texture = avatarPreviewGLWidget->opengl_engine->getTexture(preview_gl_ob->materials[i].tex_path);
			}
		}

		avatarPreviewGLWidget->addObject(preview_gl_ob);
	}
	catch(Indigo::IndigoException& e)
	{
		this->loaded_object = NULL;

		if(show_error_dialogs)
			QtUtils::showErrorMessageDialog(QtUtils::toQString(e.what()), this);
	}
	catch(glare::Exception& e)
	{
		this->loaded_object = NULL;

		if(show_error_dialogs)
			QtUtils::showErrorMessageDialog(QtUtils::toQString(e.what()), this);
	}
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
