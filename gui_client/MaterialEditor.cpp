#include "MaterialEditor.h"


#include "PlayerPhysics.h"
#include "CameraController.h"
#include "../shared/WorldObject.h"
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
#include <QtWidgets/QColorDialog>


MaterialEditor::MaterialEditor(QWidget *parent)
:	QWidget(parent)
{
	setupUi(this);	

	connect(this->textureFileSelectWidget,			SIGNAL(filenameChanged(QString&)),	this, SIGNAL(materialChanged()));

	connect(this->textureXScaleDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SIGNAL(materialChanged()));
	connect(this->textureYScaleDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SIGNAL(materialChanged()));

	connect(this->roughnessDoubleSpinBox,			SIGNAL(valueChanged(double)),		this, SIGNAL(materialChanged()));
	connect(this->metallicFractionDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(materialChanged()));

	connect(this->opacityDoubleSpinBox,				SIGNAL(valueChanged(double)),		this, SIGNAL(materialChanged()));
	
	connect(this->hologramCheckBox,					SIGNAL(toggled(bool)),				this, SIGNAL(materialChanged()));

	connect(this->metallicRoughnessFileSelectWidget,SIGNAL(filenameChanged(QString&)),	this, SIGNAL(materialChanged()));
	
	connect(this->normalMapFileSelectWidget,		SIGNAL(filenameChanged(QString&)),	this, SIGNAL(materialChanged()));

	connect(this->emissionTextureFileSelectWidget,	SIGNAL(filenameChanged(QString&)),	this, SIGNAL(materialChanged()));
	connect(this->luminanceDoubleSpinBox,			SIGNAL(valueChanged(double)),		this, SIGNAL(materialChanged()));
	
	connect(this->windCheckBox,						SIGNAL(toggled(bool)),				this, SIGNAL(materialChanged()));
	connect(this->doubleSidedCheckBox,				SIGNAL(toggled(bool)),				this, SIGNAL(materialChanged()));
	connect(this->decalCheckBox,					SIGNAL(toggled(bool)),				this, SIGNAL(materialChanged()));
}


MaterialEditor::~MaterialEditor()
{
}


void MaterialEditor::updateColourButton()
{
	const int COLOUR_BUTTON_W = 30;
	QImage image(COLOUR_BUTTON_W, COLOUR_BUTTON_W, QImage::Format_RGB32);
	image.fill(QColor(qRgba(
		(int)(this->col.r * 255),
		(int)(this->col.g * 255),
		(int)(this->col.b * 255),
		255
	)));
	QIcon icon;
	QPixmap pixmap = QPixmap::fromImage(image);
	icon.addPixmap(pixmap);
	this->colourPushButton->setIcon(icon);
	this->colourPushButton->setIconSize(QSize(COLOUR_BUTTON_W, COLOUR_BUTTON_W));
	//this->colourPushButton->setStyleSheet("QPushButton { margin: 0px; padding: 0px; border: none;}");
}


void MaterialEditor::updateEmissionColourButton()
{
	const int COLOUR_BUTTON_W = 30;
	QImage image(COLOUR_BUTTON_W, COLOUR_BUTTON_W, QImage::Format_RGB32);
	image.fill(QColor(qRgba(
		(int)(this->emission_col.r * 255),
		(int)(this->emission_col.g * 255),
		(int)(this->emission_col.b * 255),
		255
	)));
	QIcon icon;
	QPixmap pixmap = QPixmap::fromImage(image);
	icon.addPixmap(pixmap);
	this->emissionColourPushButton->setIcon(icon);
	this->emissionColourPushButton->setIconSize(QSize(COLOUR_BUTTON_W, COLOUR_BUTTON_W));
}


void MaterialEditor::setFromMaterial(const WorldMaterial& mat)
{
	// Set colour controls
	col = mat.colour_rgb;
	this->textureFileSelectWidget->setFilename(QtUtils::toQString(mat.colour_texture_url));

	SignalBlocker::setValue(this->textureXScaleDoubleSpinBox, mat.tex_matrix.elem(0, 0));
	SignalBlocker::setValue(this->textureYScaleDoubleSpinBox, mat.tex_matrix.elem(1, 1));

	SignalBlocker::setValue(this->roughnessDoubleSpinBox, mat.roughness.val);
	SignalBlocker::setValue(this->opacityDoubleSpinBox, mat.opacity.val);
	SignalBlocker::setValue(this->metallicFractionDoubleSpinBox, mat.metallic_fraction.val);

	this->metallicRoughnessFileSelectWidget->setFilename(QtUtils::toQString(mat.roughness.texture_url));
	
	this->normalMapFileSelectWidget->setFilename(QtUtils::toQString(mat.normal_map_url));

	SignalBlocker::setChecked(this->hologramCheckBox, (mat.flags & WorldMaterial::HOLOGRAM_FLAG) != 0);

	emission_col = mat.emission_rgb;
	this->emissionTextureFileSelectWidget->setFilename(QtUtils::toQString(mat.emission_texture_url));

	SignalBlocker::setValue(this->luminanceDoubleSpinBox, mat.emission_lum_flux_or_lum);
	
	SignalBlocker::setChecked(this->windCheckBox,        (mat.flags & WorldMaterial::USE_VERT_COLOURS_FOR_WIND) != 0);
	SignalBlocker::setChecked(this->doubleSidedCheckBox, (mat.flags & WorldMaterial::DOUBLE_SIDED_FLAG) != 0);
	SignalBlocker::setChecked(this->decalCheckBox,       (mat.flags & WorldMaterial::DECAL_FLAG) != 0);

	updateColourButton();
}


static void checkStringSize(URLString& s, size_t max_size)
{
	// TODO: throw exception instead?
	if(s.size() > max_size)
		s = s.substr(0, max_size);
}


void MaterialEditor::toMaterial(WorldMaterial& mat_out)
{
	mat_out.colour_rgb = col;
	mat_out.colour_texture_url = QtUtils::toIndString(this->textureFileSelectWidget->filename());
	checkStringSize(mat_out.colour_texture_url, WorldObject::MAX_URL_SIZE);

	mat_out.tex_matrix = Matrix2f(
		(float)this->textureXScaleDoubleSpinBox->value(), 0.f,
		0.f, (float)this->textureYScaleDoubleSpinBox->value()
	);

	mat_out.roughness			= ScalarVal(this->roughnessDoubleSpinBox->value());
	mat_out.metallic_fraction	= ScalarVal(this->metallicFractionDoubleSpinBox->value());
	mat_out.opacity				= ScalarVal(this->opacityDoubleSpinBox->value());

	// NOTE: don't zero flags, so that COLOUR_TEX_HAS_ALPHA_FLAG etc. are maintained.
	BitUtils::setOrZeroBit(mat_out.flags, WorldMaterial::HOLOGRAM_FLAG, this->hologramCheckBox->isChecked());

	mat_out.roughness.texture_url = QtUtils::toIndString(this->metallicRoughnessFileSelectWidget->filename());
	checkStringSize(mat_out.roughness.texture_url, WorldObject::MAX_URL_SIZE);

	mat_out.normal_map_url = QtUtils::toIndString(this->normalMapFileSelectWidget->filename());
	checkStringSize(mat_out.normal_map_url, WorldObject::MAX_URL_SIZE);

	mat_out.emission_rgb = emission_col;
	mat_out.emission_texture_url = QtUtils::toIndString(this->emissionTextureFileSelectWidget->filename());
	checkStringSize(mat_out.emission_texture_url, WorldObject::MAX_URL_SIZE);

	mat_out.emission_lum_flux_or_lum = this->luminanceDoubleSpinBox->value();

	BitUtils::setOrZeroBit(mat_out.flags, WorldMaterial::USE_VERT_COLOURS_FOR_WIND, this->windCheckBox->isChecked());
	BitUtils::setOrZeroBit(mat_out.flags, WorldMaterial::DOUBLE_SIDED_FLAG, this->doubleSidedCheckBox->isChecked());
	BitUtils::setOrZeroBit(mat_out.flags, WorldMaterial::DECAL_FLAG, this->decalCheckBox->isChecked());
}


void MaterialEditor::setControlsEnabled(bool enabled)
{
	this->setEnabled(enabled);
}


void MaterialEditor::setControlsEditable(bool editable)
{
	this->textureFileSelectWidget->setReadOnly(!editable);

	this->textureXScaleDoubleSpinBox->setReadOnly(!editable);
	this->textureYScaleDoubleSpinBox->setReadOnly(!editable);

	this->roughnessDoubleSpinBox->setReadOnly(!editable);
	this->metallicFractionDoubleSpinBox->setReadOnly(!editable);
	this->opacityDoubleSpinBox->setReadOnly(!editable);

	this->metallicRoughnessFileSelectWidget->setReadOnly(!editable);

	this->normalMapFileSelectWidget->setReadOnly(!editable);

	this->emissionTextureFileSelectWidget->setReadOnly(!editable);

	this->emissionTextureFileSelectWidget->setReadOnly(!editable);
	this->luminanceDoubleSpinBox->setReadOnly(!editable);
}


void MaterialEditor::on_colourPushButton_clicked(bool checked)
{
	const QColor initial_col(qRgba(
		(int)(col.r * 255),
		(int)(col.g * 255),
		(int)(col.b * 255),
		255
	));

	QColorDialog d(initial_col, this);
	const int res = d.exec();
	if(res == QDialog::Accepted)
	{
		const QColor new_col = d.currentColor();

		this->col.r = new_col.red()   / 255.f;
		this->col.g = new_col.green() / 255.f;
		this->col.b = new_col.blue()  / 255.f;

		updateColourButton();

		emit materialChanged();
	}
}


void MaterialEditor::on_emissionColourPushButton_clicked(bool checked)
{
	const QColor initial_col(qRgba(
		(int)(emission_col.r * 255),
		(int)(emission_col.g * 255),
		(int)(emission_col.b * 255),
		255
	));

	QColorDialog d(initial_col, this);
	const int res = d.exec();
	if(res == QDialog::Accepted)
	{
		const QColor new_col = d.currentColor();

		this->emission_col.r = new_col.red()   / 255.f;
		this->emission_col.g = new_col.green() / 255.f;
		this->emission_col.b = new_col.blue()  / 255.f;

		updateEmissionColourButton();

		emit materialChanged();
	}
}
