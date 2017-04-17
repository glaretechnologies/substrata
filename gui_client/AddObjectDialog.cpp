/*=====================================================================
AddObjectDialog.cpp
------------------------
File created by ClassTemplate on Wed Oct 07 15:16:48 2009
Code By Nicholas Chapman.
=====================================================================*/
#include "AddObjectDialog.h"


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


AddObjectDialog::AddObjectDialog(QSettings* settings_, TextureServer* texture_server_ptr)
:	settings(settings_)
{
	setupUi(this);

	// Load main window geometry and state
	this->restoreGeometry(settings->value("AddObjectDialog/geometry").toByteArray());

	this->avatarSelectWidget->setType(FileSelectWidget::Type_File);
	this->avatarSelectWidget->setFilename(settings->value("AddObjectDialogPath").toString());

	connect(this->avatarSelectWidget, SIGNAL(filenameChanged(QString&)), this, SLOT(filenameChanged(QString&)));
	connect(this->buttonBox, SIGNAL(accepted()), this, SLOT(accepted()));

	this->objectPreviewGLWidget->texture_server_ptr = texture_server_ptr;

	startTimer(10);

	loaded_model = false;
}


AddObjectDialog::~AddObjectDialog()
{
	settings->setValue("AddObjectDialog/geometry", saveGeometry());

	// Make sure we have set the gl context to current as we destroy objectPreviewGLWidget.
	objectPreviewGLWidget->makeCurrent();
	objectPreviewGLWidget = NULL;
}


void AddObjectDialog::accepted()
{
	this->settings->setValue("AddObjectDialogPath", this->avatarSelectWidget->filename());
}


//static GLObjectRef makeGLObjectForOBJFile(const std::string& path)
//{
//	MLTLibMaterials mats;
//	Indigo::MeshRef mesh = new Indigo::Mesh();
//	FormatDecoderObj::streamModel(path, *mesh, 1.f, true, mats);
//
//	// Make (dummy) materials as needed
//	GLObjectRef ob = new GLObject();
//	ob->materials.resize(mesh->num_materials_referenced);
//	for(uint32 i=0; i<ob->materials.size(); ++i)
//	{
//		// Have we parsed such a material from the .mtl file?
//		for(size_t z=0; z<mats.materials.size(); ++z)
//			if(mats.materials[z].name == toStdString(mesh->used_materials[z]))
//			{
//				ob->materials[i].albedo_rgb = mats.materials[z].Kd;
//				ob->materials[i].albedo_tex_path = mats.materials[z].map_Kd.path;
//				ob->materials[i].phong_exponent = mats.materials[z].Ns_exponent;
//				ob->materials[i].alpha = myClamp(mats.materials[z].d_opacity, 0.f, 1.f);
//			}
//	}
//	return ob;
//}

void AddObjectDialog::filenameChanged(QString& filename)
{
	const std::string path = QtUtils::toIndString(filename);
	this->result_path = path;

	conPrint("AddObjectDialog::filenameChanged: filename = " + path);

	if(filename.isEmpty())
		return;

	this->objectPreviewGLWidget->makeCurrent();

	// Try and load model
	try
	{
		if(preview_gl_ob.nonNull())
		{
			// Remove previous object from engine.
			objectPreviewGLWidget->opengl_engine->removeObject(preview_gl_ob);
		}

		preview_gl_ob = ModelLoading::makeGLObjectForModelFile(path, Matrix4f::translationMatrix(Vec4f(0,0,0,1)), this->loaded_mesh, this->suggested_scale, this->loaded_materials);

		//preview_gl_ob->ob_to_world_matrix = Matrix4f::uniformScaleMatrix(0.01f);

		//this->model_hash = FileChecksum::fileChecksum(path);

		//preview_gl_ob = new GLObject();

		//
		//if(hasExtension(path, "obj"))
		//{
		//	MLTLibMaterials mats;
		//	FormatDecoderObj::streamModel(path, *mesh, 1.f, true, mats);

		//	// Make (dummy) materials as needed
		//	preview_gl_ob->materials.resize(mesh->num_materials_referenced);
		//	for(uint32 i=0; i<preview_gl_ob->materials.size(); ++i)
		//	{
		//		// Have we parsed such a material from the .mtl file?
		//		for(size_t z=0; z<mats.materials.size(); ++z)
		//			if(mats.materials[z].name == toStdString(mesh->used_materials[z]))
		//			{
		//				preview_gl_ob->materials[i].albedo_rgb = Colour3f(0.7f, 0.7f, 0.7f);
		//				preview_gl_ob->materials[i].albedo_tex_path = "obstacle.png";
		//				preview_gl_ob->materials[i].phong_exponent = 10.f;
		//	}
		//}
		///*else if(hasExtension(path, "igmesh"))
		//{
		//	Indigo::Mesh::readFromFile(toIndigoString(path), *mesh);
		//}*/

		//	Indigo::MeshRef mesh = new Indigo::Mesh();
		//if(!mesh->vert_positions.empty())
		//{
		//	// Remove existing model from preview engine.
		//	if(preview_gl_ob.nonNull())
		//		avatarPreviewGLWidget->opengl_engine->removeObject(preview_gl_ob);

		//	// Make (dummy) materials as needed
		//	preview_gl_ob->materials.resize(mesh->num_materials_referenced);
		//	for(uint32 i=0; i<preview_gl_ob->materials.size(); ++i)
		//	{
		//		preview_gl_ob->materials[i].albedo_rgb = Colour3f(0.7f, 0.7f, 0.7f);
		//		preview_gl_ob->materials[i].albedo_tex_path = "obstacle.png";
		//		preview_gl_ob->materials[i].phong_exponent = 10.f;
		//	}

		//	preview_gl_ob->ob_to_world_matrix.setToTranslationMatrix(0.0, 0.0, 0.0);
		//	preview_gl_ob->mesh_data = OpenGLEngine::buildIndigoMesh(mesh);

			objectPreviewGLWidget->addObject(preview_gl_ob);
		//}
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


void AddObjectDialog::timerEvent(QTimerEvent* event)
{
	// Once the OpenGL widget has initialised, we can add the model.
	if(objectPreviewGLWidget->opengl_engine->initSucceeded() && !loaded_model)
	{
		QString path = settings->value("AddObjectDialogPath").toString();
		filenameChanged(path);
		loaded_model = true;
	}

	objectPreviewGLWidget->makeCurrent();
	objectPreviewGLWidget->updateGL();
}
