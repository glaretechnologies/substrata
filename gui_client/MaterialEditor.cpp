#include "MaterialEditor.h"


#include "PlayerPhysics.h"
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
#include "../utils/CameraController.h"
#include "../utils/TaskManager.h"
#include "../qt/SignalBlocker.h"
#include "../qt/QtUtils.h"
#include <QtGui/QMouseEvent>
#include <set>
#include <stack>
#include <algorithm>


MaterialEditor::MaterialEditor(QWidget *parent)
:	QWidget(parent)
{
	setupUi(this);	

	connect(this->colourRDoubleSpinBox,				SIGNAL(valueChanged(double)),		this, SIGNAL(materialChanged()));
	connect(this->colourGDoubleSpinBox,				SIGNAL(valueChanged(double)),		this, SIGNAL(materialChanged()));
	connect(this->colourBDoubleSpinBox,				SIGNAL(valueChanged(double)),		this, SIGNAL(materialChanged()));
	connect(this->textureFileSelectWidget,			SIGNAL(filenameChanged(QString&)),	this, SIGNAL(materialChanged()));

	connect(this->roughnessDoubleSpinBox,			SIGNAL(valueChanged(double)),		this, SIGNAL(materialChanged()));
	connect(this->metallicFractionDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(materialChanged()));

	connect(this->opacityDoubleSpinBox,				SIGNAL(valueChanged(double)),		this, SIGNAL(materialChanged()));
	//setControlsEnabled(false);
}


MaterialEditor::~MaterialEditor()
{
}


void MaterialEditor::setFromMaterial(const WorldMaterial& mat)
{
	// Set colour controls
	SignalBlocker::setValue(this->colourRDoubleSpinBox, mat.colour_rgb.r);
	SignalBlocker::setValue(this->colourGDoubleSpinBox, mat.colour_rgb.g);
	SignalBlocker::setValue(this->colourBDoubleSpinBox, mat.colour_rgb.b);
	this->textureFileSelectWidget->setFilename(QtUtils::toQString(mat.colour_texture_url));


	if(mat.roughness->type == ScalarVal::ScalarValType_Constant)
	{
		SignalBlocker::setValue(this->roughnessDoubleSpinBox, mat.roughness.downcastToPtr<ConstantScalarVal>()->val);
	}
	else
	{
		SignalBlocker::setValue(this->roughnessDoubleSpinBox, 0.5f);
	}
	
	if(mat.opacity->type == ScalarVal::ScalarValType_Constant)
	{
		SignalBlocker::setValue(this->opacityDoubleSpinBox, mat.opacity.downcastToPtr<ConstantScalarVal>()->val);
	}
	else
	{
		SignalBlocker::setValue(this->opacityDoubleSpinBox, 0.5f);
	}
	
	if(mat.metallic_fraction->type == ScalarVal::ScalarValType_Constant)
	{
		SignalBlocker::setValue(this->metallicFractionDoubleSpinBox, mat.metallic_fraction.downcastToPtr<ConstantScalarVal>()->val);
	}
	else
	{
		SignalBlocker::setValue(this->metallicFractionDoubleSpinBox, 0.5f);
	}
}


void MaterialEditor::toMaterial(WorldMaterial& mat_out)
{
	mat_out.colour_rgb = Colour3f(
		(float)this->colourRDoubleSpinBox->value(),
		(float)this->colourGDoubleSpinBox->value(),
		(float)this->colourBDoubleSpinBox->value()
	);
	mat_out.colour_texture_url = QtUtils::toIndString(this->textureFileSelectWidget->filename());


	mat_out.roughness			= new ConstantScalarVal(this->roughnessDoubleSpinBox->value());
	mat_out.metallic_fraction	= new ConstantScalarVal(this->metallicFractionDoubleSpinBox->value());
	mat_out.opacity				= new ConstantScalarVal(this->opacityDoubleSpinBox->value());
}


void MaterialEditor::setControlsEnabled(bool enabled)
{
	this->setEnabled(enabled);
}


void MaterialEditor::setControlsEditable(bool editable)
{
	this->colourRDoubleSpinBox->setReadOnly(!editable);
	this->colourGDoubleSpinBox->setReadOnly(!editable);
	this->colourBDoubleSpinBox->setReadOnly(!editable);

	this->textureFileSelectWidget->setReadOnly(!editable);

	this->roughnessDoubleSpinBox->setReadOnly(!editable);
	this->opacityDoubleSpinBox->setReadOnly(!editable);
}

