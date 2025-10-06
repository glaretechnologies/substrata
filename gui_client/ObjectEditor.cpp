#include "ObjectEditor.h"


#include "ShaderEditorDialog.h"
#include "../dll/include/IndigoMesh.h"
#include "../indigo/TextureServer.h"
#include "../indigo/globals.h"
#include "../graphics/Map2D.h"
#include "../graphics/ImageMap.h"
#include "../maths/vec3.h"
#include "../maths/GeometrySampling.h"
#include "../utils/Lock.h"
#include "../utils/Mutex.h"
#include "../utils/Clock.h"
#include "../utils/Timer.h"
#include "../utils/Platform.h"
#include "../utils/FileUtils.h"
#include "../utils/Reference.h"
#include "../utils/StringUtils.h"
#include "../utils/TaskManager.h"
#include "../qt/SignalBlocker.h"
#include "../qt/QtUtils.h"
#include <QtGui/QMouseEvent>
#include <QtGui/QDesktopServices>
#include <QtWidgets/QErrorMessage>
#include <QtCore/QTimer>
#include <QtWidgets/QColorDialog>
#include <set>
#include <stack>
#include <algorithm>


// NOTE: these max volume levels should be the same as in maxAudioVolumeForObject() in WorkerThread.cpp (runs on server)
static const float DEFAULT_MAX_VOLUME = 4;
static const float DEFAULT_MAX_VIDEO_VOLUME = 4;
static const float SLIDER_MAX_VOLUME = 2;


ObjectEditor::ObjectEditor(QWidget *parent)
:	QWidget(parent),
	selected_mat_index(0),
	edit_timer(new QTimer(this)),
	shader_editor(NULL),
	settings(NULL),
	spotlight_col(0.85f)
{
	setupUi(this);

	this->modelFileSelectWidget->force_use_last_dir_setting = true;
	this->videoURLFileSelectWidget->force_use_last_dir_setting = true;
	this->audioFileWidget->force_use_last_dir_setting = true;

	//this->scaleXDoubleSpinBox->setMinimum(0.00001);
	//this->scaleYDoubleSpinBox->setMinimum(0.00001);
	//this->scaleZDoubleSpinBox->setMinimum(0.00001);

	SignalBlocker::setChecked(show3DControlsCheckBox, true); // On by default.

	connect(this->matEditor,				SIGNAL(materialChanged()),			this, SIGNAL(objectChanged()));

	connect(this->modelFileSelectWidget,	SIGNAL(filenameChanged(QString&)),	this, SIGNAL(objectChanged()));
	connect(this->scriptTextEdit,			SIGNAL(textChanged()),				this, SLOT(scriptTextEditChanged()));
	connect(this->contentTextEdit,			SIGNAL(textChanged()),				this, SIGNAL(objectChanged()));
	
	connect(this->targetURLLineEdit,		SIGNAL(editingFinished()),			this, SIGNAL(objectChanged()));
	connect(this->targetURLLineEdit,		SIGNAL(editingFinished()),			this, SLOT(targetURLChanged()));


	connect(this->audioFileWidget,			SIGNAL(filenameChanged(QString&)),	this, SIGNAL(objectChanged()));
	connect(this->volumeDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));

	connect(this->posXDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SIGNAL(objectTransformChanged()));
	connect(this->posYDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SIGNAL(objectTransformChanged()));
	connect(this->posZDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SIGNAL(objectTransformChanged()));

	connect(this->scaleXDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SLOT(xScaleChanged(double)));
	connect(this->scaleYDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SLOT(yScaleChanged(double)));
	connect(this->scaleZDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SLOT(zScaleChanged(double)));
	
	connect(this->rotAxisXDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectTransformChanged()));
	connect(this->rotAxisYDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectTransformChanged()));
	connect(this->rotAxisZDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectTransformChanged()));

	connect(this->collidableCheckBox,		SIGNAL(toggled(bool)),				this, SIGNAL(objectChanged()));
	connect(this->dynamicCheckBox,			SIGNAL(toggled(bool)),				this, SIGNAL(objectChanged()));
	connect(this->sensorCheckBox,			SIGNAL(toggled(bool)),				this, SIGNAL(objectChanged()));

	connect(this->massDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));
	connect(this->frictionDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));
	connect(this->restitutionDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));

	connect(this->COMOffsetXDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));
	connect(this->COMOffsetYDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));
	connect(this->COMOffsetZDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));


	connect(this->luminousFluxDoubleSpinBox,SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));

	connect(this->show3DControlsCheckBox,	SIGNAL(toggled(bool)),				this, SIGNAL(posAndRot3DControlsToggled()));

	connect(this->linkScaleCheckBox,		SIGNAL(toggled(bool)),				this, SLOT(linkScaleCheckBoxToggled(bool)));

	connect(this->videoAutoplayCheckBox,	SIGNAL(toggled(bool)),				this, SIGNAL(objectChanged()));
	connect(this->videoLoopCheckBox,		SIGNAL(toggled(bool)),				this, SIGNAL(objectChanged()));
	connect(this->videoMutedCheckBox,		SIGNAL(toggled(bool)),				this, SIGNAL(objectChanged()));

	connect(this->videoURLFileSelectWidget,	SIGNAL(filenameChanged(QString&)),	this, SIGNAL(objectChanged()));
	connect(this->videoVolumeDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));

	connect(this->audioAutoplayCheckBox,	SIGNAL(toggled(bool)),				this, SIGNAL(objectChanged()));
	connect(this->audioLoopCheckBox,		SIGNAL(toggled(bool)),				this, SIGNAL(objectChanged()));


	this->volumeDoubleSpinBox->setMaximum(DEFAULT_MAX_VOLUME);
	this->volumeDoubleSpinBox->setSliderMaximum(SLIDER_MAX_VOLUME);

	this->videoVolumeDoubleSpinBox->setMaximum(DEFAULT_MAX_VIDEO_VOLUME);
	this->videoVolumeDoubleSpinBox->setSliderMaximum(SLIDER_MAX_VOLUME);

	this->visitURLLabel->hide();

	// Set up script edit timer.
	edit_timer->setSingleShot(true);
	edit_timer->setInterval(300);

	connect(edit_timer, SIGNAL(timeout()), this, SLOT(editTimerTimeout()));
}


void ObjectEditor::init() // settings should be set before this.
{
	show3DControlsCheckBox->setChecked(settings->value("objectEditor/show3DControlsCheckBoxChecked", /*default val=*/true).toBool());
	SignalBlocker::setChecked(linkScaleCheckBox, settings->value("objectEditor/linkScaleCheckBoxChecked", /*default val=*/true).toBool());

	SignalBlocker::setValue(gridSpacingDoubleSpinBox, settings->value("objectEditor/gridSpacing", /*default val=*/1.0).toDouble());
	SignalBlocker::setChecked(snapToGridCheckBox, settings->value("objectEditor/snapToGridCheckBoxChecked", /*default val=*/false).toBool());
}


ObjectEditor::~ObjectEditor()
{
	settings->setValue("objectEditor/gridSpacing", gridSpacingDoubleSpinBox->value());
	settings->setValue("objectEditor/snapToGridCheckBoxChecked", snapToGridCheckBox->isChecked());
}


void ObjectEditor::updateInfoLabel(const WorldObject& ob)
{
	const std::string creator_name = !ob.creator_name.empty() ? ob.creator_name :
		(ob.creator_id.valid() ? ("user id: " + ob.creator_id.toString()) : "[Unknown]");

	std::string ob_type;
	switch(ob.object_type)
	{
	case WorldObject::ObjectType_Generic: ob_type = "Generic object"; break;
	case WorldObject::ObjectType_Hypercard: ob_type = "Hypercard"; break;
	case WorldObject::ObjectType_VoxelGroup: ob_type = "Voxel Group"; break;
	case WorldObject::ObjectType_Spotlight: ob_type = "Spotlight"; break;
	case WorldObject::ObjectType_WebView: ob_type = "Web View"; break;
	case WorldObject::ObjectType_Video: ob_type = "Video"; break;
	case WorldObject::ObjectType_Text: ob_type = "Text"; break;
	}

	std::string info_text = ob_type + " (UID: " + ob.uid.toString() + "), \ncreated by '" + creator_name + "' " + ob.created_time.timeAgoDescription();
	
	// Show last-modified time only if it differs from created_time.
	if(ob.created_time.time != ob.last_modified_time.time)
		info_text += ", last modified " + ob.last_modified_time.timeAgoDescription();

	this->infoLabel->setText(QtUtils::toQString(info_text));
}


void ObjectEditor::setFromObject(const WorldObject& ob, int selected_mat_index_, bool ob_in_editing_users_world)
{
	this->editing_ob_uid = ob.uid;

	//this->objectTypeLabel->setText(QtUtils::toQString(ob_type + " (UID: " + ob.uid.toString() + ")"));

	if(ob_in_editing_users_world)
	{
		// If the user is logged in, and we are connected to the user's personal world, set a high maximum volume.
		this->volumeDoubleSpinBox->setMaximum(1000);
		this->volumeDoubleSpinBox->setSliderMaximum(SLIDER_MAX_VOLUME);

		this->videoVolumeDoubleSpinBox->setMaximum(1000);
		this->videoVolumeDoubleSpinBox->setSliderMaximum(SLIDER_MAX_VOLUME);
	}
	else
	{
		// Otherwise just use the default max volume values
		this->volumeDoubleSpinBox->setMaximum(DEFAULT_MAX_VOLUME);
		this->volumeDoubleSpinBox->setSliderMaximum(SLIDER_MAX_VOLUME);

		this->videoVolumeDoubleSpinBox->setMaximum(DEFAULT_MAX_VIDEO_VOLUME);
		this->videoVolumeDoubleSpinBox->setSliderMaximum(SLIDER_MAX_VOLUME);
	}

	this->cloned_materials.resize(ob.materials.size());
	for(size_t i=0; i<ob.materials.size(); ++i)
		this->cloned_materials[i] = ob.materials[i]->clone();

	//this->createdByLabel->setText(QtUtils::toQString(creator_name));
	//this->createdTimeLabel->setText(QtUtils::toQString(ob.created_time.timeAgoDescription()));

	updateInfoLabel(ob);

	this->selected_mat_index = selected_mat_index_;

	// The spotlight model has multiple materials, we want to edit material 0 though.
	if(ob.object_type == WorldObject::ObjectType_Spotlight)
		this->selected_mat_index = 0;

	this->modelFileSelectWidget->setFilename(QtUtils::toQString(ob.model_url));
	{
		SignalBlocker b(this->scriptTextEdit);
		this->scriptTextEdit->setPlainText(QtUtils::toQString(ob.script));
	}
	{
		SignalBlocker b(this->contentTextEdit);
		this->contentTextEdit->setPlainText(QtUtils::toQString(ob.content));
	}
	{
		SignalBlocker b(this->targetURLLineEdit);
		this->targetURLLineEdit->setText(QtUtils::toQString(ob.target_url));
	}

	this->posXDoubleSpinBox->setEnabled(true);
	this->posYDoubleSpinBox->setEnabled(true);
	this->posZDoubleSpinBox->setEnabled(true);

	setTransformFromObject(ob);

	SignalBlocker::setChecked(this->collidableCheckBox, ob.isCollidable());
	SignalBlocker::setChecked(this->dynamicCheckBox, ob.isDynamic());
	SignalBlocker::setChecked(this->sensorCheckBox, ob.isSensor());
	
	SignalBlocker::setValue(this->massDoubleSpinBox,		ob.mass);
	SignalBlocker::setValue(this->frictionDoubleSpinBox,	ob.friction);
	SignalBlocker::setValue(this->restitutionDoubleSpinBox, ob.restitution);

	SignalBlocker::setValue(COMOffsetXDoubleSpinBox, ob.centre_of_mass_offset_os.x);
	SignalBlocker::setValue(COMOffsetYDoubleSpinBox, ob.centre_of_mass_offset_os.y);
	SignalBlocker::setValue(COMOffsetZDoubleSpinBox, ob.centre_of_mass_offset_os.z);

	
	SignalBlocker::setChecked(this->videoAutoplayCheckBox, BitUtils::isBitSet(ob.flags, WorldObject::VIDEO_AUTOPLAY));
	SignalBlocker::setChecked(this->videoLoopCheckBox,     BitUtils::isBitSet(ob.flags, WorldObject::VIDEO_LOOP));
	SignalBlocker::setChecked(this->videoMutedCheckBox,    BitUtils::isBitSet(ob.flags, WorldObject::VIDEO_MUTED));

	SignalBlocker::setChecked(this->audioAutoplayCheckBox, BitUtils::isBitSet(ob.flags, WorldObject::AUDIO_AUTOPLAY));
	SignalBlocker::setChecked(this->audioLoopCheckBox,     BitUtils::isBitSet(ob.flags, WorldObject::AUDIO_LOOP));

	this->videoURLFileSelectWidget->setFilename(QtUtils::toQString((!ob.materials.empty()) ? ob.materials[0]->emission_texture_url : ""));

	SignalBlocker::setValue(videoVolumeDoubleSpinBox, ob.audio_volume);
	
	lightmapURLLabel->setText(QtUtils::toQString(ob.lightmap_url));

	WorldMaterialRef selected_mat;
	if(selected_mat_index >= 0 && selected_mat_index < (int)ob.materials.size())
		selected_mat = ob.materials[selected_mat_index];
	else
		selected_mat = new WorldMaterial();
	
	if(ob.object_type == WorldObject::ObjectType_Hypercard)
	{
		this->materialsGroupBox->hide();
		this->lightmapGroupBox->hide();
		this->modelLabel->hide();
		this->modelFileSelectWidget->hide();
		this->spotlightGroupBox->hide();
		this->audioGroupBox->show();
		this->physicsSettingsGroupBox->show();
		this->videoGroupBox->hide();
	}
	else if(ob.object_type == WorldObject::ObjectType_VoxelGroup)
	{
		this->materialsGroupBox->show();
		this->lightmapGroupBox->show();
		this->modelLabel->hide();
		this->modelFileSelectWidget->hide();
		this->spotlightGroupBox->hide();
		this->audioGroupBox->show();
		this->physicsSettingsGroupBox->show();
		this->videoGroupBox->hide();
	}
	else if(ob.object_type == WorldObject::ObjectType_Spotlight)
	{
		this->materialsGroupBox->hide();
		this->lightmapGroupBox->hide();
		this->modelLabel->hide();
		this->modelFileSelectWidget->hide();
		this->spotlightGroupBox->show();
		this->audioGroupBox->hide();
		this->physicsSettingsGroupBox->show();
		this->videoGroupBox->hide();
	}
	else if(ob.object_type == WorldObject::ObjectType_WebView)
	{
		this->materialsGroupBox->show();
		this->lightmapGroupBox->hide();
		this->modelLabel->hide();
		this->modelFileSelectWidget->hide();
		this->spotlightGroupBox->hide();
		this->audioGroupBox->hide();
		this->physicsSettingsGroupBox->hide();
		this->videoGroupBox->hide();
	}
	else if(ob.object_type == WorldObject::ObjectType_Video)
	{
		this->materialsGroupBox->hide();
		this->lightmapGroupBox->hide();
		this->modelLabel->hide();
		this->modelFileSelectWidget->hide();
		this->spotlightGroupBox->hide();
		this->audioGroupBox->hide();
		this->physicsSettingsGroupBox->hide();
		this->videoGroupBox->show();
	}
	else if(ob.object_type == WorldObject::ObjectType_Text)
	{
		this->materialsGroupBox->show();
		this->lightmapGroupBox->hide();
		this->modelLabel->hide();
		this->modelFileSelectWidget->hide();
		this->spotlightGroupBox->hide();
		this->audioGroupBox->show();
		this->physicsSettingsGroupBox->show();
		this->videoGroupBox->hide();
	}
	else
	{
		this->materialsGroupBox->show();
		this->lightmapGroupBox->show();
		this->modelLabel->show();
		this->modelFileSelectWidget->show();
		this->spotlightGroupBox->hide();
		this->audioGroupBox->show();
		this->physicsSettingsGroupBox->show();
		this->videoGroupBox->hide();
	}

	if(ob.object_type != WorldObject::ObjectType_Hypercard)
	{
		this->matEditor->setFromMaterial(*selected_mat);

		// Set materials combobox
		SignalBlocker blocker(this->materialComboBox);
		this->materialComboBox->clear();
		for(size_t i=0; i<ob.materials.size(); ++i)
			this->materialComboBox->addItem(QtUtils::toQString("Material " + toString(i)), (int)i);

		this->materialComboBox->setCurrentIndex(selected_mat_index);
	}


	// For spotlight:
	if(ob.object_type == WorldObject::ObjectType_Spotlight)
	{
		SignalBlocker::setValue(this->luminousFluxDoubleSpinBox, selected_mat->emission_lum_flux_or_lum);

		this->spotlight_col = selected_mat->colour_rgb; // Spotlight light colour is in colour_rgb instead of emission_rgb for historical reasons.

		updateSpotlightColourButton();
	}


	//this->targetURLLabel->setVisible(ob.object_type == WorldObject::ObjectType_Hypercard);
	//this->targetURLLineEdit->setVisible(ob.object_type == WorldObject::ObjectType_Hypercard);
	this->visitURLLabel->setVisible(/*ob.object_type == WorldObject::ObjectType_Hypercard && */!ob.target_url.empty());

	if(ob.lightmap_baking)
	{
		lightmapBakeStatusLabel->setText("Lightmap is baking...");
	}
	else
	{
		lightmapBakeStatusLabel->setText("");
	}

	this->audioFileWidget->setFilename(QtUtils::toQString(ob.audio_source_url));
	SignalBlocker::setValue(volumeDoubleSpinBox, ob.audio_volume);
}


void ObjectEditor::setTransformFromObject(const WorldObject& ob)
{
	SignalBlocker::setValue(this->posXDoubleSpinBox, ob.pos.x);
	SignalBlocker::setValue(this->posYDoubleSpinBox, ob.pos.y);
	SignalBlocker::setValue(this->posZDoubleSpinBox, ob.pos.z);

	SignalBlocker::setValue(this->scaleXDoubleSpinBox, ob.scale.x);
	SignalBlocker::setValue(this->scaleYDoubleSpinBox, ob.scale.y);
	SignalBlocker::setValue(this->scaleZDoubleSpinBox, ob.scale.z);

	this->last_x_scale_over_z_scale = ob.scale.x / ob.scale.z;
	this->last_x_scale_over_y_scale = ob.scale.x / ob.scale.y;
	this->last_y_scale_over_z_scale = ob.scale.y / ob.scale.z;


	const Matrix3f rot_mat = Matrix3f::rotationMatrix(normalise(ob.axis), ob.angle);

	const Vec3f angles = rot_mat.getAngles();

	SignalBlocker::setValue(this->rotAxisXDoubleSpinBox, angles.x * 360 / Maths::get2Pi<float>());
	SignalBlocker::setValue(this->rotAxisYDoubleSpinBox, angles.y * 360 / Maths::get2Pi<float>());
	SignalBlocker::setValue(this->rotAxisZDoubleSpinBox, angles.z * 360 / Maths::get2Pi<float>());

	updateInfoLabel(ob); // Update info label, which includes last-modified time.
}


static void checkStringSize(std::string& s, size_t max_size)
{
	// TODO: throw exception instead?
	if(s.size() > max_size)
		s = s.substr(0, max_size);
}
static void checkStringSize(URLString& s, size_t max_size)
{
	// TODO: throw exception instead?
	if(s.size() > max_size)
		s = s.substr(0, max_size);
}

void ObjectEditor::toObject(WorldObject& ob_out)
{
	const URLString new_model_url = toURLString(QtUtils::toIndString(this->modelFileSelectWidget->filename()));
	if(ob_out.model_url != new_model_url)
		ob_out.changed_flags |= WorldObject::MODEL_URL_CHANGED;
	ob_out.model_url = new_model_url;
	checkStringSize(ob_out.model_url, WorldObject::MAX_URL_SIZE);

	const std::string new_script =  QtUtils::toIndString(this->scriptTextEdit->toPlainText());
	if(ob_out.script != new_script)
		ob_out.changed_flags |= WorldObject::SCRIPT_CHANGED;
	ob_out.script = new_script;
	checkStringSize(ob_out.script, WorldObject::MAX_SCRIPT_SIZE);

	const std::string new_content = QtUtils::toIndString(this->contentTextEdit->toPlainText());
	if(ob_out.content != new_content)
		ob_out.changed_flags |= WorldObject::CONTENT_CHANGED;
	ob_out.content = new_content;
	checkStringSize(ob_out.content, WorldObject::MAX_CONTENT_SIZE);

	ob_out.target_url    = QtUtils::toIndString(this->targetURLLineEdit->text());
	checkStringSize(ob_out.target_url, WorldObject::MAX_URL_SIZE);

	writeTransformMembersToObject(ob_out); // Set ob_out transform members
	
	ob_out.setCollidable(this->collidableCheckBox->isChecked());
	const bool new_dynamic = this->dynamicCheckBox->isChecked();
	if(new_dynamic != ob_out.isDynamic())
		ob_out.changed_flags |= WorldObject::DYNAMIC_CHANGED;
	ob_out.setDynamic(new_dynamic);

	const bool new_is_sensor = this->sensorCheckBox->isChecked();
	if(new_is_sensor != ob_out.isSensor())
		ob_out.changed_flags |= WorldObject::PHYSICS_VALUE_CHANGED;
	ob_out.setIsSensor(new_is_sensor);

	const float new_mass		= (float)this->massDoubleSpinBox->value();
	const float new_friction	= (float)this->frictionDoubleSpinBox->value();
	const float new_restitution	= (float)this->restitutionDoubleSpinBox->value();
	const Vec3f new_COM_offset(
		(float)this->COMOffsetXDoubleSpinBox->value(),
		(float)this->COMOffsetYDoubleSpinBox->value(),
		(float)this->COMOffsetZDoubleSpinBox->value()
	);

	if(new_mass != ob_out.mass || new_friction != ob_out.friction || new_restitution != ob_out.restitution || new_COM_offset != ob_out.centre_of_mass_offset_os)
		ob_out.changed_flags |= WorldObject::PHYSICS_VALUE_CHANGED;

	ob_out.mass = new_mass;
	ob_out.friction = new_friction;
	ob_out.restitution = new_restitution;
	ob_out.centre_of_mass_offset_os = new_COM_offset;


	BitUtils::setOrZeroBit(ob_out.flags, WorldObject::VIDEO_AUTOPLAY, this->videoAutoplayCheckBox->isChecked());
	BitUtils::setOrZeroBit(ob_out.flags, WorldObject::VIDEO_LOOP,     this->videoLoopCheckBox    ->isChecked());
	BitUtils::setOrZeroBit(ob_out.flags, WorldObject::VIDEO_MUTED,    this->videoMutedCheckBox   ->isChecked());

	BitUtils::setOrZeroBit(ob_out.flags, WorldObject::AUDIO_AUTOPLAY, this->audioAutoplayCheckBox->isChecked());
	BitUtils::setOrZeroBit(ob_out.flags, WorldObject::AUDIO_LOOP,     this->audioLoopCheckBox    ->isChecked());

	if(ob_out.object_type != WorldObject::ObjectType_Hypercard) // Don't store materials for hypercards. (doesn't use them, and matEditor may have old/invalid data)
	{
		if(selected_mat_index >= (int)cloned_materials.size())
		{
			cloned_materials.resize(selected_mat_index + 1);
			for(size_t i=0; i<cloned_materials.size(); ++i)
				if(cloned_materials[i].isNull())
					cloned_materials[i] = new WorldMaterial();
		}

		this->matEditor->toMaterial(*cloned_materials[selected_mat_index]);

		ob_out.materials.resize(cloned_materials.size());
		for(size_t i=0; i<cloned_materials.size(); ++i)
			ob_out.materials[i] = cloned_materials[i]->clone();
	}

	// Set the emission_texture_url from the video URL control.  NOTE: needs to go after setting materials above. 
	if(ob_out.object_type == WorldObject::ObjectType_Video)
	{
		if(ob_out.materials.size() >= 1)
		{
			ob_out.materials[0]->emission_texture_url = QtUtils::toIndString(this->videoURLFileSelectWidget->filename());
			checkStringSize(ob_out.materials[0]->emission_texture_url, WorldObject::MAX_URL_SIZE);
		}
	}

	if(ob_out.object_type == WorldObject::ObjectType_Spotlight) // NOTE: is ob_out.object_type set?
	{
		if(ob_out.materials.size() >= 1)
		{
			ob_out.materials[0]->emission_lum_flux_or_lum = (float)this->luminousFluxDoubleSpinBox->value();

			ob_out.materials[0]->colour_rgb = this->spotlight_col;
			ob_out.materials[0]->emission_rgb = this->spotlight_col;
		}

		updateSpotlightColourButton();
	}

	const URLString new_audio_source_url = toURLString(QtUtils::toStdString(this->audioFileWidget->filename()));
	if(ob_out.audio_source_url != new_audio_source_url)
		ob_out.changed_flags |= WorldObject::AUDIO_SOURCE_URL_CHANGED;
	ob_out.audio_source_url = new_audio_source_url;
	checkStringSize(ob_out.audio_source_url, WorldObject::MAX_URL_SIZE);

	if(ob_out.object_type == WorldObject::ObjectType_Video)
		ob_out.audio_volume = videoVolumeDoubleSpinBox->value();
	else
		ob_out.audio_volume = volumeDoubleSpinBox->value();
}


void ObjectEditor::writeTransformMembersToObject(WorldObject& ob_out)
{
	ob_out.pos.x = this->posXDoubleSpinBox->value();
	ob_out.pos.y = this->posYDoubleSpinBox->value();
	ob_out.pos.z = this->posZDoubleSpinBox->value();

	ob_out.scale.x = (float)this->scaleXDoubleSpinBox->value();
	ob_out.scale.y = (float)this->scaleYDoubleSpinBox->value();
	ob_out.scale.z = (float)this->scaleZDoubleSpinBox->value();

	const Vec3f angles(
		(float)(this->rotAxisXDoubleSpinBox->value() / 360 * Maths::get2Pi<double>()),
		(float)(this->rotAxisYDoubleSpinBox->value() / 360 * Maths::get2Pi<double>()),
		(float)(this->rotAxisZDoubleSpinBox->value() / 360 * Maths::get2Pi<double>())
	);

	// Convert angles to rotation matrix, then the rotation matrix to axis-angle.

	const Matrix3f rot_matrix = Matrix3f::fromAngles(angles);

	rot_matrix.rotationMatrixToAxisAngle(/*unit axis out=*/ob_out.axis, /*angle out=*/ob_out.angle);

	if(ob_out.axis.length() < 1.0e-5f)
	{
		ob_out.axis = Vec3f(0,0,1);
		ob_out.angle = 0;
	}
}


void ObjectEditor::objectModelURLUpdated(const WorldObject& ob)
{
	this->modelFileSelectWidget->setFilename(QtUtils::toQString(ob.model_url));

	updateInfoLabel(ob); // Update info label, which includes last-modified time.
}


void ObjectEditor::objectLightmapURLUpdated(const WorldObject& ob)
{
	lightmapURLLabel->setText(QtUtils::toQString(ob.lightmap_url));

	if(ob.lightmap_baking)
	{
		lightmapBakeStatusLabel->setText("Lightmap is baking...");
	}
	else
	{
		lightmapBakeStatusLabel->setText("Lightmap baked.");
	}

	updateInfoLabel(ob); // Update info label, which includes last-modified time.
}


void ObjectEditor::objectPickedUp()
{
	this->posXDoubleSpinBox->setEnabled(false);
	this->posYDoubleSpinBox->setEnabled(false);
	this->posZDoubleSpinBox->setEnabled(false);
}


void ObjectEditor::objectDropped()
{
	this->posXDoubleSpinBox->setEnabled(true);
	this->posYDoubleSpinBox->setEnabled(true);
	this->posZDoubleSpinBox->setEnabled(true);
}


void ObjectEditor::setControlsEnabled(bool enabled)
{
	this->setEnabled(enabled);
}


void ObjectEditor::setControlsEditable(bool editable)
{
	this->modelFileSelectWidget->setReadOnly(!editable);
	this->scriptTextEdit->setReadOnly(!editable);
	this->contentTextEdit->setReadOnly(!editable);
	this->targetURLLineEdit->setReadOnly(!editable);

	this->posXDoubleSpinBox->setReadOnly(!editable);
	this->posYDoubleSpinBox->setReadOnly(!editable);
	this->posZDoubleSpinBox->setReadOnly(!editable);

	this->scaleXDoubleSpinBox->setReadOnly(!editable);
	this->scaleYDoubleSpinBox->setReadOnly(!editable);
	this->scaleZDoubleSpinBox->setReadOnly(!editable);

	this->rotAxisXDoubleSpinBox->setReadOnly(!editable);
	this->rotAxisYDoubleSpinBox->setReadOnly(!editable);
	this->rotAxisZDoubleSpinBox->setReadOnly(!editable);

	this->collidableCheckBox->setEnabled(editable);
	this->dynamicCheckBox->setEnabled(editable);
	this->sensorCheckBox->setEnabled(editable);

	this->matEditor->setControlsEditable(editable);

	this->editScriptPushButton->setEnabled(editable);
	this->bakeLightmapPushButton->setEnabled(editable);
	this->bakeLightmapHighQualPushButton->setEnabled(editable);
	this->removeLightmapPushButton->setEnabled(editable);

	this->audioFileWidget->setReadOnly(!editable);
	this->volumeDoubleSpinBox->setReadOnly(!editable);
}


void ObjectEditor::on_visitURLLabel_linkActivated(const QString&)
{
	std::string url = QtUtils::toStdString(this->targetURLLineEdit->text());
	if(StringUtils::containsString(url, "://"))
	{
		// URL already has protocol prefix
		const std::string protocol = url.substr(0, url.find("://", 0));
		if(protocol == "http" || protocol == "https")
		{
			QDesktopServices::openUrl(QtUtils::toQString(url));
		}
		else
		{
			// Don't open this URL, might be something potentially unsafe like a file on disk
			QErrorMessage m;
			m.showMessage("This URL is potentially unsafe and will not be opened.");
			m.exec();
		}
	}
	else
	{
		url = "http://" + url;
		QDesktopServices::openUrl(QtUtils::toQString(url));
	}
}


void ObjectEditor::on_materialComboBox_currentIndexChanged(int index)
{
	this->selected_mat_index = index;

	if(index < (int)this->cloned_materials.size())
		this->matEditor->setFromMaterial(*this->cloned_materials[index]);
}


void ObjectEditor::on_newMaterialPushButton_clicked(bool checked)
{
	this->selected_mat_index = this->materialComboBox->count();

	this->materialComboBox->addItem(QtUtils::toQString("Material " + toString(selected_mat_index)), selected_mat_index);

	{
		SignalBlocker blocker(this->materialComboBox);
		this->materialComboBox->setCurrentIndex(this->selected_mat_index);
	}

	this->cloned_materials.push_back(new WorldMaterial());
	this->matEditor->setFromMaterial(*this->cloned_materials.back());

	emit objectChanged();
}


void ObjectEditor::on_editScriptPushButton_clicked(bool checked)
{
	if(!shader_editor)
	{
		shader_editor = new ShaderEditorDialog(this, base_dir_path);

		shader_editor->setWindowTitle("Script Editor");

		QObject::connect(shader_editor, SIGNAL(shaderChanged()), SLOT(scriptChangedFromEditor()));

		QObject::connect(shader_editor, SIGNAL(openServerScriptLogSignal()), this, SIGNAL(openServerScriptLogSignal()));
	}

	shader_editor->initialise(QtUtils::toIndString(this->scriptTextEdit->toPlainText()));

	shader_editor->show();
	shader_editor->raise();
}


void ObjectEditor::on_bakeLightmapPushButton_clicked(bool checked)
{
	lightmapBakeStatusLabel->setText("Lightmap is baking...");

	emit bakeObjectLightmap();
}


void ObjectEditor::on_bakeLightmapHighQualPushButton_clicked(bool checked)
{
	lightmapBakeStatusLabel->setText("Lightmap is baking...");

	emit bakeObjectLightmapHighQual();
}


void ObjectEditor::on_removeLightmapPushButton_clicked(bool checked)
{
	this->lightmapURLLabel->clear();
	emit removeLightmapSignal();
}


void ObjectEditor::targetURLChanged()
{
	this->visitURLLabel->setVisible(!this->targetURLLineEdit->text().isEmpty());
}


void ObjectEditor::scriptTextEditChanged()
{
	edit_timer->start();

	if(shader_editor)
		shader_editor->update(QtUtils::toIndString(scriptTextEdit->toPlainText()));
}


void ObjectEditor::scriptChangedFromEditor()
{
	{
		SignalBlocker b(this->scriptTextEdit);
		this->scriptTextEdit->setPlainText(shader_editor->getShaderText());
	}

	emit scriptChangedFromEditorSignal(); // objectChanged();
}


void ObjectEditor::editTimerTimeout()
{
	emit objectChanged();
}


void ObjectEditor::materialSelectedInBrowser(const std::string& path)
{
	// Load material
	try
	{
		WorldMaterialRef mat = WorldMaterial::loadFromXMLOnDisk(path, /*convert_rel_paths_to_abs_disk_paths=*/true);

		if(selected_mat_index >= 0 && selected_mat_index < (int)this->cloned_materials.size())
		{
			this->cloned_materials[this->selected_mat_index] = mat;
			this->matEditor->setFromMaterial(*mat);

			emit objectChanged();
		}
	}
	catch(glare::Exception& e)
	{
		QErrorMessage m;
		m.showMessage("Error while opening material: " + QtUtils::toQString(e.what()));
		m.exec();
	}
}


void ObjectEditor::printFromLuaScript(const std::string& msg, UID object_uid)
{
	if((this->editing_ob_uid == object_uid) && shader_editor)
		shader_editor->printFromLuaScript(msg);
}


void ObjectEditor::luaErrorOccurred(const std::string& msg, UID object_uid)
{
	if((this->editing_ob_uid == object_uid) && shader_editor)
		shader_editor->luaErrorOccurred(msg);
}


void ObjectEditor::xScaleChanged(double new_x)
{
	if(this->linkScaleCheckBox->isChecked())
	{
		// Update y and z scales to maintain the previous ratios between scales.
		
		// we want new_y / new_x = old_y / old_x
		// new_y = (old_y / old_x) * new_x
		// new_y = new_x * old_y / old_x = new_x * (old_y / old_x) = new_x / (old_x / old_y)
		double new_y = new_x / last_x_scale_over_y_scale;

		// new_z = (old_z / old_x) * new_x = new_x / (old_x / old_z)
		double new_z = new_x / last_x_scale_over_z_scale;

		SignalBlocker::setValue(scaleYDoubleSpinBox, new_y);
		SignalBlocker::setValue(scaleZDoubleSpinBox, new_z);
	}
	else
	{
		// x value has changed, so update ratios.
		this->last_x_scale_over_z_scale = new_x / scaleZDoubleSpinBox->value();
		this->last_x_scale_over_y_scale = new_x / scaleYDoubleSpinBox->value();
	}

	emit objectTransformChanged();
}


void ObjectEditor::yScaleChanged(double new_y)
{
	if(this->linkScaleCheckBox->isChecked())
	{
		// Update x and z scales to maintain the previous ratios between scales.

		// we want new_x / new_y = old_x / old_y
		// new_x = (old_x / old_y) * new_y
		double new_x = last_x_scale_over_y_scale * new_y;

		// we want new_z / new_y = old_z / old_y
		// new_z = (old_z / old_y) * new_y = new_y / (old_y / old_z)
		double new_z = new_y / last_y_scale_over_z_scale;

		SignalBlocker::setValue(scaleXDoubleSpinBox, new_x);
		SignalBlocker::setValue(scaleZDoubleSpinBox, new_z);
	}
	else
	{
		// y value has changed, so update ratios.
		this->last_x_scale_over_y_scale = scaleXDoubleSpinBox->value() / new_y;
		this->last_y_scale_over_z_scale = new_y / scaleZDoubleSpinBox->value();
	}

	emit objectTransformChanged();
}


void ObjectEditor::zScaleChanged(double new_z)
{
	if(this->linkScaleCheckBox->isChecked())
	{
		// Update x and y scales to maintain the previous ratios between scales.
		
		// we want new_x / new_z = old_x / old_z
		// new_x = (old_x / old_z) * new_z
		double new_x = last_x_scale_over_z_scale * new_z;

		// we want new_y / new_z = old_y / old_z
		// new_y = (old_y / old_z) * new_z
		double new_y = last_y_scale_over_z_scale * new_z;

		// Set x and y scales
		SignalBlocker::setValue(scaleXDoubleSpinBox, new_x);
		SignalBlocker::setValue(scaleYDoubleSpinBox, new_y);
	}
	else
	{
		// z value has changed, so update ratios.
		this->last_x_scale_over_z_scale = scaleXDoubleSpinBox->value() / new_z;
		this->last_y_scale_over_z_scale = scaleYDoubleSpinBox->value() / new_z;
	}

	emit objectTransformChanged();
}


void ObjectEditor::linkScaleCheckBoxToggled(bool val)
{
	assert(settings);
	if(settings)
		settings->setValue("objectEditor/linkScaleCheckBoxChecked", this->linkScaleCheckBox->isChecked());
}


void ObjectEditor::updateSpotlightColourButton()
{
	const int COLOUR_BUTTON_W = 30;
	QImage image(COLOUR_BUTTON_W, COLOUR_BUTTON_W, QImage::Format_RGB32);
	image.fill(QColor(qRgba(
		(int)(this->spotlight_col.r * 255),
		(int)(this->spotlight_col.g * 255),
		(int)(this->spotlight_col.b * 255),
		255
	)));
	QIcon icon;
	QPixmap pixmap = QPixmap::fromImage(image);
	icon.addPixmap(pixmap);
	this->spotlightColourPushButton->setIcon(icon);
	this->spotlightColourPushButton->setIconSize(QSize(COLOUR_BUTTON_W, COLOUR_BUTTON_W));
}


void ObjectEditor::on_spotlightColourPushButton_clicked(bool checked)
{
	const QColor initial_col(qRgba(
		(int)(spotlight_col.r * 255),
		(int)(spotlight_col.g * 255),
		(int)(spotlight_col.b * 255),
		255
	));

	QColorDialog d(initial_col, this);
	const int res = d.exec();
	if(res == QDialog::Accepted)
	{
		const QColor new_col = d.currentColor();

		this->spotlight_col.r = new_col.red()   / 255.f;
		this->spotlight_col.g = new_col.green() / 255.f;
		this->spotlight_col.b = new_col.blue()  / 255.f;

		updateSpotlightColourButton();

		emit objectChanged();
	}
}
