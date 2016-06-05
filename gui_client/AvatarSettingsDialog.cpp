/*=====================================================================
AvatarSettingsDialog.cpp
------------------------
File created by ClassTemplate on Wed Oct 07 15:16:48 2009
Code By Nicholas Chapman.
=====================================================================*/
#include "AvatarSettingsDialog.h"


#include "../dll/include/IndigoMesh.h"
#include "../graphics/formatdecoderobj.h"
#include "../dll/IndigoStringUtils.h"
#include "../utils/FileUtils.h"
#include "../utils/Exception.h"
#include "../utils/PlatformUtils.h"
#include "../utils/StringUtils.h"
#include "../utils/ConPrint.h"
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

	this->avatarSelectWidget->setType(FileSelectWidget::Type_File);
	this->avatarSelectWidget->setFilename(settings->value("avatarPath").toString());

	connect(this->avatarSelectWidget, SIGNAL(filenameChanged(QString&)), this, SLOT(avatarFilenameChanged(QString&)));
	connect(this->buttonBox, SIGNAL(accepted()), this, SLOT(accepted()));

	this->avatarPreviewGLWidget->texture_server_ptr = texture_server_ptr;

	timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(timerEvent()));
    timer->start(10);

	loaded_model = false;
}


AvatarSettingsDialog::~AvatarSettingsDialog()
{
	settings->setValue("AvatarSettingsDialog/geometry", saveGeometry());
}


void AvatarSettingsDialog::accepted()
{
	this->settings->setValue("avatarPath", this->avatarSelectWidget->filename());
}


void AvatarSettingsDialog::avatarFilenameChanged(QString& filename)
{
	const std::string path = QtUtils::toIndString(filename);

	conPrint("AvatarSettingsDialog::avatarFilenameChanged: filename = " + path);

	if(filename.isEmpty())
		return;

	this->avatarPreviewGLWidget->makeCurrent();

	// Try and load model
	try
	{
		Indigo::MeshRef mesh = new Indigo::Mesh();
		if(hasExtension(path, "obj"))
		{
			FormatDecoderObj::streamModel(path, *mesh, 1.f);
		}
		else if(hasExtension(path, "igmesh"))
		{
			Indigo::Mesh::readFromFile(toIndigoString(path), *mesh);
		}

		if(!mesh->vert_positions.empty())
		{
			// Remove existing model from preview engine.
			if(avatar_gl_ob.nonNull())
				avatarPreviewGLWidget->opengl_engine->removeObject(avatar_gl_ob);

			avatar_gl_ob = new GLObject();
			avatar_gl_ob->materials.resize(8);
			avatar_gl_ob->materials[0].albedo_rgb = Colour3f(0.6f, 0.2f, 0.2f);
			avatar_gl_ob->materials[0].fresnel_scale = 1;

			avatar_gl_ob->ob_to_world_matrix.setToTranslationMatrix(0.0, 0.0, 0.0);
			avatar_gl_ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh);

			avatarPreviewGLWidget->addObject(avatar_gl_ob);
		}
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


void AvatarSettingsDialog::timerEvent()
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
