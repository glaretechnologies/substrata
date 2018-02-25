/*=====================================================================
AddObjectDialog.cpp
------------------------
File created by ClassTemplate on Wed Oct 07 15:16:48 2009
Code By Nicholas Chapman.
=====================================================================*/
#include "AddObjectDialog.h"


#include "ModelLoading.h"
#include "NetDownloadResourcesThread.h"
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
#include <QtWidgets/QListWidget>
#include <QtCore/QSettings>
#include <QtCore/QTimer>


AddObjectDialog::AddObjectDialog(const std::string& base_dir_path_, QSettings* settings_, TextureServer* texture_server_ptr, Reference<ResourceManager> resource_manager_)
:	settings(settings_),
	resource_manager(resource_manager_),
	base_dir_path(base_dir_path_)
{
	setupUi(this);

	// Load main window geometry and state
	this->restoreGeometry(settings->value("AddObjectDialog/geometry").toByteArray());

	this->tabWidget->setCurrentIndex(settings->value("AddObjectDialog/tabIndex").toInt());

	this->avatarSelectWidget->setType(FileSelectWidget::Type_File);
	//this->avatarSelectWidget->setFilename(settings->value("AddObjectDialogPath").toString());

	connect(this->listWidget, SIGNAL(itemClicked(QListWidgetItem*)), this, SLOT(modelSelected(QListWidgetItem*)));
	connect(this->avatarSelectWidget, SIGNAL(filenameChanged(QString&)), this, SLOT(filenameChanged(QString&)));
	connect(this->buttonBox, SIGNAL(accepted()), this, SLOT(accepted()));

	connect(this->urlLineEdit, SIGNAL(textChanged(const QString&)), this, SLOT(urlChanged(const QString&)));
	connect(this->urlLineEdit, SIGNAL(editingFinished()), this, SLOT(urlEditingFinished()));

	this->objectPreviewGLWidget->texture_server_ptr = texture_server_ptr;

	startTimer(10);

	loaded_model = false;

	thread_manager.addThread(new NetDownloadResourcesThread(&this->msg_queue, resource_manager_));

	
	listWidget->setViewMode(QListWidget::IconMode);
	listWidget->setIconSize(QSize(200, 200));
	listWidget->setResizeMode(QListWidget::Adjust);
	listWidget->setSelectionMode(QAbstractItemView::NoSelection);
	 
	models.push_back("Quad");
	models.push_back("Cube");
	models.push_back("Capsule");
	models.push_back("Cylinder");
	models.push_back("Icosahedron");
	models.push_back("Platonic_Solid");
	models.push_back("Torus");

	for(int i=0; i<models.size(); ++i)
	{
		const std::string image_path = base_dir_path + "/resources/models/" + models[i] + ".png";

		listWidget->addItem(new QListWidgetItem(QIcon(QtUtils::toQString(image_path)), QtUtils::toQString(models[i])));
	}
}


AddObjectDialog::~AddObjectDialog()
{
	settings->setValue("AddObjectDialog/geometry", saveGeometry());

	settings->setValue("AddObjectDialog/tabIndex", this->tabWidget->currentIndex());

	// Make sure we have set the gl context to current as we destroy objectPreviewGLWidget.
	objectPreviewGLWidget->makeCurrent();
	objectPreviewGLWidget = NULL;
}


void AddObjectDialog::accepted()
{
	this->settings->setValue("AddObjectDialogPath", this->avatarSelectWidget->filename());
}


void AddObjectDialog::modelSelected(QListWidgetItem* selected_item)
{
	if(this->listWidget->currentItem())
	{
		std::string model = QtUtils::toStdString(this->listWidget->currentItem()->text());

		this->listWidget->setCurrentItem(NULL);

		const std::string model_path = base_dir_path + "/resources/models/" + model + ".obj";

		this->result_path = model_path;

		loadModelIntoPreview(model_path);
	}
}


void AddObjectDialog::filenameChanged(QString& filename)
{
	const std::string path = QtUtils::toIndString(filename);
	this->result_path = path;

	conPrint("AddObjectDialog::filenameChanged: filename = " + path);

	if(filename.isEmpty())
		return;

	loadModelIntoPreview(path);
}


void AddObjectDialog::loadModelIntoPreview(const std::string& local_path)
{
	this->objectPreviewGLWidget->makeCurrent();

	// Try and load model
	try
	{
		if(preview_gl_ob.nonNull())
		{
			// Remove previous object from engine.
			objectPreviewGLWidget->opengl_engine->removeObject(preview_gl_ob);
		}

		preview_gl_ob = ModelLoading::makeGLObjectForModelFile(local_path, Matrix4f::translationMatrix(Vec4f(0, 0, 0, 1)), 
			this->loaded_mesh, // mesh out
			this->suggested_scale, // suggested scale out
			this->loaded_materials // loaded materials out
		);

		objectPreviewGLWidget->addObject(preview_gl_ob);
	}
	catch(Indigo::IndigoException& e)
	{
		// Show error
		conPrint(toStdString(e.what()));
		QErrorMessage m(this);
		m.showMessage(QtUtils::toQString(e.what()));
		m.exec();
	}
	catch(Indigo::Exception& e)
	{
		// Show error
		conPrint(e.what());
		QErrorMessage m(this);
		m.showMessage(QtUtils::toQString(e.what()));
		m.exec();
	}
}


void AddObjectDialog::urlChanged(const QString& filename)
{
	const std::string url = QtUtils::toStdString(urlLineEdit->text());
	if(url != last_url)
	{
		last_url = url;
		

		// Process new URL:

		if(resource_manager->isFileForURLPresent(url))
		{
			try
			{
				loadModelIntoPreview(resource_manager->pathForURL(url));
			}
			catch(Indigo::Exception& e)
			{
				conPrint(e.what());
			}
		}
		else
		{
			// Download the model:
			thread_manager.enqueueMessage(new DownloadResourceMessage(url));
		}
	}
}


void AddObjectDialog::urlEditingFinished()
{}


void AddObjectDialog::timerEvent(QTimerEvent* event)
{
	// Once the OpenGL widget has initialised, we can add the model.
	if(objectPreviewGLWidget->opengl_engine->initSucceeded() && !loaded_model)
	{
		//QString path = settings->value("AddObjectDialogPath").toString();
		//filenameChanged(path);
		//loaded_model = true;
	}

	objectPreviewGLWidget->makeCurrent();
	objectPreviewGLWidget->updateGL();

	// Check msg queue
	Lock lock(this->msg_queue.getMutex());
	while(!msg_queue.unlockedEmpty())
	{
		Reference<ThreadMessage> msg;
		this->msg_queue.unlockedDequeue(msg);

		if(dynamic_cast<const ResourceDownloadedMessage*>(msg.getPointer()))
		{
			const ResourceDownloadedMessage* m = static_cast<const ResourceDownloadedMessage*>(msg.getPointer());

			conPrint("ResourceDownloadedMessage, URL: " + m->URL);
			try
			{
				// Now that the model is downloaded, set the result to be the local path where it was downloaded to.
				this->result_path = resource_manager->pathForURL(m->URL); // TODO: catch

				loadModelIntoPreview(resource_manager->pathForURL(m->URL));
			}
			catch(Indigo::Exception& e)
			{
				conPrint(e.what());
			}
		}
	}
}
