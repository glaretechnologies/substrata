#include "ObjectEditor.h"


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


ObjectEditor::ObjectEditor(QWidget *parent)
:	QWidget(parent),
	selected_mat_index(0)
{
	setupUi(this);

	this->scaleXDoubleSpinBox->setMinimum(0.00001);
	this->scaleYDoubleSpinBox->setMinimum(0.00001);
	this->scaleZDoubleSpinBox->setMinimum(0.00001);

	connect(this->matEditor,				SIGNAL(materialChanged()),			this, SIGNAL(objectChanged()));

	connect(this->modelFileSelectWidget,	SIGNAL(filenameChanged(QString&)),	this, SIGNAL(objectChanged()));
	connect(this->scriptFileSelectWidget,	SIGNAL(filenameChanged(QString&)),	this, SIGNAL(objectChanged()));

	connect(this->contentTextEdit,			SIGNAL(textChanged()),				this, SIGNAL(objectChanged()));

	connect(this->scaleXDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));
	connect(this->scaleYDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));
	connect(this->scaleZDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));
	
	connect(this->rotAxisXDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));
	connect(this->rotAxisYDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));
	connect(this->rotAxisZDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));
	connect(this->rotAngleDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));
}


ObjectEditor::~ObjectEditor()
{
}


void ObjectEditor::setFromObject(const WorldObject& ob, int selected_mat_index_)
{
	const std::string creator_name = !ob.creator_name.empty() ? ob.creator_name :
		(ob.creator_id.valid() ? ("user id: " + ob.creator_id.toString()) : "[Unknown]");

	this->createdByLabel->setText(QtUtils::toQString(creator_name));
	this->createdTimeLabel->setText(QtUtils::toQString(ob.created_time.timeAgoDescription()));

	this->selected_mat_index = selected_mat_index_;
	this->modelFileSelectWidget->setFilename(QtUtils::toQString(ob.model_url));
	this->scriptFileSelectWidget->setFilename(QtUtils::toQString(ob.script_url));

	{
		SignalBlocker b(this->contentTextEdit);
		this->contentTextEdit->setText(QtUtils::toQString(ob.content));
	}

	SignalBlocker::setValue(this->scaleXDoubleSpinBox, ob.scale.x);
	SignalBlocker::setValue(this->scaleYDoubleSpinBox, ob.scale.y);
	SignalBlocker::setValue(this->scaleZDoubleSpinBox, ob.scale.z);
	
	SignalBlocker::setValue(this->rotAxisXDoubleSpinBox, ob.axis.x);
	SignalBlocker::setValue(this->rotAxisYDoubleSpinBox, ob.axis.y);
	SignalBlocker::setValue(this->rotAxisZDoubleSpinBox, ob.axis.z);
	SignalBlocker::setValue(this->rotAngleDoubleSpinBox, ob.angle);

	WorldMaterialRef selected_mat;
	if(selected_mat_index >= 0 && selected_mat_index < (int)ob.materials.size())
		selected_mat = ob.materials[selected_mat_index];
	else
		selected_mat = new WorldMaterial();
	
	if(ob.object_type == WorldObject::ObjectType_Hypercard)
	{
		this->matEditor->hide();
	}
	else
	{
		this->matEditor->show();
		this->matEditor->setFromMaterial(*selected_mat);
	}
}


void ObjectEditor::toObject(WorldObject& ob_out)
{
	ob_out.model_url  = QtUtils::toIndString(this->modelFileSelectWidget->filename());
	ob_out.script_url = QtUtils::toIndString(this->scriptFileSelectWidget->filename());
	ob_out.content    = QtUtils::toIndString(this->contentTextEdit->toPlainText());

	ob_out.scale.x = (float)this->scaleXDoubleSpinBox->value();
	ob_out.scale.y = (float)this->scaleYDoubleSpinBox->value();
	ob_out.scale.z = (float)this->scaleZDoubleSpinBox->value();

	ob_out.axis.x = (float)this->rotAxisXDoubleSpinBox->value();
	ob_out.axis.y = (float)this->rotAxisYDoubleSpinBox->value();
	ob_out.axis.z = (float)this->rotAxisZDoubleSpinBox->value();
	ob_out.angle  = (float)this->rotAngleDoubleSpinBox->value();

	if(ob_out.axis.length() < 1.0e-5f)
	{
		ob_out.axis = Vec3f(0,0,1);
		ob_out.angle = 0;
	}

	if(selected_mat_index >= ob_out.materials.size())
		ob_out.materials.resize(selected_mat_index + 1);
	for(size_t i=0; i<ob_out.materials.size(); ++i)
		if(ob_out.materials[i].isNull())
			ob_out.materials[i] = new WorldMaterial();

	this->matEditor->toMaterial(*ob_out.materials[selected_mat_index]);
}


void ObjectEditor::setControlsEnabled(bool enabled)
{
	this->setEnabled(enabled);
}


void ObjectEditor::setControlsEditable(bool editable)
{
	this->modelFileSelectWidget->setReadOnly(!editable);
	this->scriptFileSelectWidget->setReadOnly(!editable);
	this->contentTextEdit->setReadOnly(!editable);

	this->scaleXDoubleSpinBox->setReadOnly(!editable);
	this->scaleYDoubleSpinBox->setReadOnly(!editable);
	this->scaleZDoubleSpinBox->setReadOnly(!editable);

	this->rotAxisXDoubleSpinBox->setReadOnly(!editable);
	this->rotAxisYDoubleSpinBox->setReadOnly(!editable);
	this->rotAxisZDoubleSpinBox->setReadOnly(!editable);
	this->rotAngleDoubleSpinBox->setReadOnly(!editable);

	this->matEditor->setControlsEditable(editable);
}
