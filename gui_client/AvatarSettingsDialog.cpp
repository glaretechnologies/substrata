/*=====================================================================
AvatarSettingsDialog.cpp
------------------------
File created by ClassTemplate on Wed Oct 07 15:16:48 2009
Code By Nicholas Chapman.
=====================================================================*/
#include "AvatarSettingsDialog.h"


#include "ModelLoading.h"
#include "../dll/include/IndigoMesh.h"
#include "../graphics/formatdecoderobj.h"
#include "../dll/IndigoStringUtils.h"
#include "../utils/FileUtils.h"
#include "../utils/Exception.h"
#include "../utils/PlatformUtils.h"
#include "../utils/StringUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/FileChecksum.h"
#include "../qt/QtUtils.h"
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QErrorMessage>
#include <QtCore/QSettings>
#include <QtCore/QTimer>


AvatarSettingsDialog::AvatarSettingsDialog(QSettings* settings_, TextureServer* texture_server_ptr)
:	settings(settings_)
{
	setupUi(this);

	// Load main window geometry and state
	this->restoreGeometry(settings->value("AvatarSettingsDialog/geometry").toByteArray());

	this->usernameLineEdit->setText(settings->value("username").toString());

	this->avatarSelectWidget->setType(FileSelectWidget::Type_File);
	this->avatarSelectWidget->setFilename(settings->value("avatarPath").toString());

	connect(this->avatarSelectWidget, SIGNAL(filenameChanged(QString&)), this, SLOT(avatarFilenameChanged(QString&)));
	connect(this->buttonBox, SIGNAL(accepted()), this, SLOT(accepted()));

	this->avatarPreviewGLWidget->texture_server_ptr = texture_server_ptr;

	startTimer(10);

	loaded_model = false;
}


AvatarSettingsDialog::~AvatarSettingsDialog()
{
	settings->setValue("AvatarSettingsDialog/geometry", saveGeometry());
}


std::string AvatarSettingsDialog::getAvatarName()
{
	return QtUtils::toStdString(usernameLineEdit->text());
}


void AvatarSettingsDialog::accepted()
{
	this->settings->setValue("avatarPath", this->avatarSelectWidget->filename());
	this->settings->setValue("username", this->usernameLineEdit->text());
}


void AvatarSettingsDialog::avatarFilenameChanged(QString& filename)
{
	const std::string path = QtUtils::toIndString(filename);
	this->result_path = path;
	

	conPrint("AvatarSettingsDialog::avatarFilenameChanged: filename = " + path);

	if(filename.isEmpty())
		return;

	this->avatarPreviewGLWidget->makeCurrent();

	// Try and load model
	try
	{
		this->model_hash = FileChecksum::fileChecksum(path);

		if(avatar_gl_ob.nonNull())
		{
			// Remove previous object from engine.
			avatarPreviewGLWidget->opengl_engine->removeObject(avatar_gl_ob);
		}

		Indigo::MeshRef mesh;
		float suggested_scale;
		std::vector<WorldMaterialRef> loaded_materials;
		avatar_gl_ob = ModelLoading::makeGLObjectForModelFile(path, Matrix4f::translationMatrix(Vec4f(0,0,0,1)), mesh, suggested_scale, loaded_materials);
		
		avatarPreviewGLWidget->addObject(avatar_gl_ob);
	}
	catch(Indigo::IndigoException& e)
	{
		// Show error
		conPrint(toStdString(e.what()));
		QErrorMessage m;
		m.showMessage(QtUtils::toQString(e.what()));
		m.exec();
	}
	catch(Indigo::Exception& e)
	{
		// Show error
		conPrint(e.what());
		QErrorMessage m;
		m.showMessage(QtUtils::toQString(e.what()));
		m.exec();
	}
}


void AvatarSettingsDialog::timerEvent(QTimerEvent* event)
{
	// Once the OpenGL widget has initialised, we can add the model.
	if(avatarPreviewGLWidget->opengl_engine->initSucceeded() && !loaded_model)
	{
		QString path = settings->value("avatarPath").toString();
		avatarFilenameChanged(path);
		loaded_model = true;
	}

	avatarPreviewGLWidget->updateGL();
}
