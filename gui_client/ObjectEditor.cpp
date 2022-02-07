#include "ObjectEditor.h"


#include "ShaderEditorDialog.h"
#include "../dll/include/IndigoMesh.h"
#include "../indigo/TextureServer.h"
#include "../indigo/globals.h"
#include "../graphics/Map2D.h"
#include "../graphics/imformatdecoder.h"
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
#include <set>
#include <stack>
#include <algorithm>


ObjectEditor::ObjectEditor(QWidget *parent)
:	QWidget(parent),
	selected_mat_index(0),
	edit_timer(new QTimer(this)),
	shader_editor(NULL),
	settings(NULL)
{
	setupUi(this);

	this->modelFileSelectWidget->force_use_last_dir_setting = true;
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

	connect(this->posXDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));
	connect(this->posYDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));
	connect(this->posZDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));

	connect(this->scaleXDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SLOT(xScaleChanged(double)));
	connect(this->scaleYDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SLOT(yScaleChanged(double)));
	connect(this->scaleZDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SLOT(zScaleChanged(double)));
	
	connect(this->rotAxisXDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));
	connect(this->rotAxisYDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));
	connect(this->rotAxisZDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));

	connect(this->collidableCheckBox,		SIGNAL(toggled(bool)),				this, SIGNAL(objectChanged()));

	connect(this->luminousFluxDoubleSpinBox,SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));

	connect(this->show3DControlsCheckBox,	SIGNAL(toggled(bool)),				this, SIGNAL(posAndRot3DControlsToggled()));

	connect(this->linkScaleCheckBox,		SIGNAL(toggled(bool)),				this, SLOT(linkScaleCheckBoxToggled(bool)));

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
}


ObjectEditor::~ObjectEditor()
{
}


void ObjectEditor::setFromObject(const WorldObject& ob, int selected_mat_index_)
{
	std::string ob_type;
	switch(ob.object_type)
	{
	case WorldObject::ObjectType_Generic: ob_type = "Generic"; break;
	case WorldObject::ObjectType_Hypercard: ob_type = "Hypercard"; break;
	case WorldObject::ObjectType_VoxelGroup: ob_type = "Voxel Group"; break;
	case WorldObject::ObjectType_Spotlight: ob_type = "Spotlight"; break;
	case WorldObject::ObjectType_WebView: ob_type = "Web View"; break;
	}

	this->objectTypeLabel->setText(QtUtils::toQString(ob_type + " (UID: " + ob.uid.toString() + ")"));

	this->cloned_materials.resize(ob.materials.size());
	for(size_t i=0; i<ob.materials.size(); ++i)
		this->cloned_materials[i] = ob.materials[i]->clone();

	const std::string creator_name = !ob.creator_name.empty() ? ob.creator_name :
		(ob.creator_id.valid() ? ("user id: " + ob.creator_id.toString()) : "[Unknown]");

	this->createdByLabel->setText(QtUtils::toQString(creator_name));
	this->createdTimeLabel->setText(QtUtils::toQString(ob.created_time.timeAgoDescription()));

	this->selected_mat_index = selected_mat_index_;
	this->modelFileSelectWidget->setFilename(QtUtils::toQString(ob.model_url));
	{
		SignalBlocker b(this->scriptTextEdit);
		this->scriptTextEdit->setPlainText(QtUtils::toQString(ob.script));
	}
	{
		SignalBlocker b(this->contentTextEdit);
		this->contentTextEdit->setText(QtUtils::toQString(ob.content));
	}
	{
		SignalBlocker b(this->targetURLLineEdit);
		this->targetURLLineEdit->setText(QtUtils::toQString(ob.target_url));
	}

	this->posXDoubleSpinBox->setEnabled(true);
	this->posYDoubleSpinBox->setEnabled(true);
	this->posZDoubleSpinBox->setEnabled(true);
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

	SignalBlocker::setChecked(this->collidableCheckBox, ob.isCollidable());
	
	lightmapURLLabel->setText(QtUtils::toQString(ob.lightmap_url));

	WorldMaterialRef selected_mat;
	if(selected_mat_index >= 0 && selected_mat_index < (int)ob.materials.size())
		selected_mat = ob.materials[selected_mat_index];
	else
		selected_mat = new WorldMaterial();

	SignalBlocker::setValue(this->luminousFluxDoubleSpinBox, selected_mat->emission_lum_flux);
	
	if(ob.object_type == WorldObject::ObjectType_Hypercard)
	{
		this->materialsGroupBox->hide();
		this->modelLabel->hide();
		this->modelFileSelectWidget->hide();
		this->spotlightGroupBox->hide();
		this->audioGroupBox->show();
	}
	else if(ob.object_type == WorldObject::ObjectType_VoxelGroup)
	{
		this->materialsGroupBox->show();
		this->modelLabel->hide();
		this->modelFileSelectWidget->hide();
		this->spotlightGroupBox->hide();
		this->audioGroupBox->show();
	}
	else if(ob.object_type == WorldObject::ObjectType_Spotlight)
	{
		this->materialsGroupBox->show();
		this->modelLabel->hide();
		this->modelFileSelectWidget->hide();
		this->spotlightGroupBox->show();
		this->audioGroupBox->hide();
	}
	else if(ob.object_type == WorldObject::ObjectType_WebView)
	{
		this->materialsGroupBox->show();
		this->modelLabel->hide();
		this->modelFileSelectWidget->hide();
		this->spotlightGroupBox->hide();
		this->audioGroupBox->hide();
	}
	else
	{
		this->materialsGroupBox->show();
		this->modelLabel->show();
		this->modelFileSelectWidget->show();
		this->spotlightGroupBox->hide();
		this->audioGroupBox->show();
	}

	if(ob.object_type == WorldObject::ObjectType_VoxelGroup || ob.object_type == WorldObject::ObjectType_Spotlight || ob.object_type == WorldObject::ObjectType_Generic || ob.object_type == WorldObject::ObjectType_WebView)
	{
		this->matEditor->setFromMaterial(*selected_mat);

		// Set materials combobox
		SignalBlocker blocker(this->materialComboBox);
		this->materialComboBox->clear();
		for(size_t i=0; i<ob.materials.size(); ++i)
			this->materialComboBox->addItem(QtUtils::toQString("Material " + toString(i)), (int)i);

		this->materialComboBox->setCurrentIndex(selected_mat_index);
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


void ObjectEditor::updateObjectPos(const WorldObject& ob)
{
	SignalBlocker::setValue(this->posXDoubleSpinBox, ob.pos.x);
	SignalBlocker::setValue(this->posYDoubleSpinBox, ob.pos.y);
	SignalBlocker::setValue(this->posZDoubleSpinBox, ob.pos.z);
}


void ObjectEditor::toObject(WorldObject& ob_out)
{
	ob_out.model_url  = QtUtils::toIndString(this->modelFileSelectWidget->filename());
	ob_out.script     = QtUtils::toIndString(this->scriptTextEdit->toPlainText());
	ob_out.content    = QtUtils::toIndString(this->contentTextEdit->toPlainText());
	ob_out.target_url    = QtUtils::toIndString(this->targetURLLineEdit->text());

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

	ob_out.setCollidable(this->collidableCheckBox->isChecked());

	if(selected_mat_index >= cloned_materials.size())
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

	if(ob_out.object_type == WorldObject::ObjectType_Spotlight) // NOTE: is ob_out.object_type set?
	{
		if(ob_out.materials.size() >= 1)
		{
			ob_out.materials[0]->emission_lum_flux = (float)this->luminousFluxDoubleSpinBox->value();
		}
	}

	const std::string new_audio_source_url = QtUtils::toStdString(this->audioFileWidget->filename());
	if(ob_out.audio_source_url != new_audio_source_url)
		ob_out.changed_flags |= WorldObject::AUDIO_SOURCE_URL_CHANGED;
	ob_out.audio_source_url = new_audio_source_url;
	ob_out.audio_volume = volumeDoubleSpinBox->value();
}


void ObjectEditor::objectModelURLUpdated(const WorldObject& ob)
{
	this->modelFileSelectWidget->setFilename(QtUtils::toQString(ob.model_url));
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

	if(index < this->cloned_materials.size())
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
		shader_editor = new ShaderEditorDialog(NULL, base_dir_path);

		shader_editor->setWindowTitle("Script Editor");

		QObject::connect(shader_editor, SIGNAL(shaderChanged()), SLOT(scriptChangedFromEditor()));
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

	emit objectChanged();
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
		WorldMaterialRef mat = WorldMaterial::loadFromXMLOnDisk(path);

		if(selected_mat_index >= 0 && selected_mat_index < this->cloned_materials.size())
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

	emit objectChanged();
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

	emit objectChanged();
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

	emit objectChanged();
}


void ObjectEditor::linkScaleCheckBoxToggled(bool val)
{
	assert(settings);
	if(settings)
		settings->setValue("objectEditor/linkScaleCheckBoxChecked", this->linkScaleCheckBox->isChecked());
}
