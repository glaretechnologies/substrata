/*=====================================================================
AvatarSettingsDialog.cpp
------------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "AvatarSettingsDialog.h"


#include "AddObjectDialog.h"
#include "AvatarGroundingUtils.h"
#include "ModelLoading.h"
#include "AnimationManager.h"
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


AvatarSettingsDialog::AvatarSettingsDialog(const std::string& base_dir_path_, QSettings* settings_, Reference<ResourceManager> resource_manager_, AnimationManager* anim_manager_)
:	base_dir_path(base_dir_path_),
	settings(settings_),
	resource_manager(resource_manager_),
	done_initial_load(false),
	pre_ob_to_world_matrix(Matrix4f::identity()),
	anim_manager(anim_manager_)
{
	setupUi(this);

	texture_server = new TextureServer(/*use_canonical_path_keys=*/false);

	this->usernameLabel->hide();
	this->usernameLineEdit->hide();

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


static const char* anim_names[] = {
	"Walking",
	"Idle",
	"Walking Backward",
	"Running",
	"Running Backward",
	"Floating",
	"Flying",
	"Left Turn",
	"Right Turn",
};


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
		ModelLoading::makeGLObjectForModelFile(*avatarPreviewGLWidget->opengl_engine, *avatarPreviewGLWidget->opengl_engine->vert_buf_allocator, /*allocator=*/nullptr, use_local_path, /*do_opengl_stuff=*/true,
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

		AnimationData& animation_data = preview_gl_ob->mesh_data->animation_data;
		AvatarGrounding::GroundingInfo grounding = AvatarGrounding::computeGroundingInfo(
			animation_data,
			/*use_retarget_adjustment=*/false,
			AvatarGrounding::kDefaultToeBottomOffsetM
		);
		float foot_bottom_height = grounding.foot_bottom_height;
		float avatar_anchor_height = grounding.anchor_height_from_origin;
		//printVar(foot_bottom_height);
		if(true)
		{
			// Append the first animation (Idle) and build the retargetting data.
			animation_data.loadAndRetargetAnim(*anim_manager->getAnimation("Idle.subanim", *resource_manager));
		
			// Append all the other animations
			for(size_t i=0; i<staticArrayNumElems(anim_names); ++i)
				animation_data.appendAnimationData(*anim_manager->getAnimation(URLString(anim_names[i]) + ".subanim", *resource_manager));

			grounding = AvatarGrounding::computeGroundingInfo(
				animation_data,
				/*use_retarget_adjustment=*/true,
				AvatarGrounding::kRetargetedToeBottomOffsetM
			);
			foot_bottom_height = grounding.foot_bottom_height;
			avatar_anchor_height = grounding.anchor_height_from_origin;

			conPrint("foot_bottom_height: " + doubleToStringNSigFigs(foot_bottom_height, 4));
			conPrint("avatar_anchor_height: " + doubleToStringNSigFigs(avatar_anchor_height, 4));
		}

		// Populate current animation combobox with anim names
		animationComboBox->clear();
		for(size_t i=0; i<preview_gl_ob->mesh_data->animation_data.animations.size(); ++i)
			animationComboBox->addItem(QtUtils::toQString(preview_gl_ob->mesh_data->animation_data.animations[i]->name));
		animationComboBox->setMaxVisibleItems(50);

		// Select Idle animation initially
		preview_gl_ob->current_anim_i = myMax(0, preview_gl_ob->mesh_data->animation_data.getAnimationIndex("Idle"));
		SignalBlocker::setCurrentIndex(animationComboBox, preview_gl_ob->current_anim_i);

		// Align the avatar eye level with the camera origin using eye bones when available.
		this->pre_ob_to_world_matrix = Matrix4f::translationMatrix(0, 0, -avatar_anchor_height) * preview_gl_ob->ob_to_world_matrix;

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
