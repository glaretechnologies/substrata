/*=====================================================================
AddObjectDialog.cpp
-------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "AddObjectDialog.h"


#include "ModelLoading.h"
#include "MeshBuilding.h"
#include "NetDownloadResourcesThread.h"
#include "../shared/LODGeneration.h"
#include "../shared/ImageDecoding.h"
#include "../dll/include/IndigoMesh.h"
#include "../dll/include/IndigoException.h"
#include "../simpleraytracer/raymesh.h"
#include "../dll/IndigoStringUtils.h"
#include "../utils/FileUtils.h"
#include "../utils/Exception.h"
#include "../utils/PlatformUtils.h"
#include "../utils/StringUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/FileChecksum.h"
#include "../utils/TaskManager.h"
#include "../indigo/TextureServer.h"
#include "../qt/QtUtils.h"
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QErrorMessage>
#include <QtWidgets/QListWidget>
#include <QtCore/QSettings>
#include <QtCore/QTimer>


AddObjectDialog::AddObjectDialog(const std::string& base_dir_path_, QSettings* settings_, Reference<ResourceManager> resource_manager_, IMFDXGIDeviceManager* dev_manager_,
	glare::TaskManager* main_task_manager_, glare::TaskManager* high_priority_task_manager_)
:	settings(settings_),
	resource_manager(resource_manager_),
	base_dir_path(base_dir_path_),
	dev_manager(dev_manager_),
	loaded_mesh_is_image_cube(false),
	main_task_manager(main_task_manager_),
	high_priority_task_manager(high_priority_task_manager_)
{
	setupUi(this);

	texture_server = new TextureServer(/*use_canonical_path_keys=*/false); // To cache textures for textureHasAlphaChannel

	this->objectPreviewGLWidget->init(base_dir_path, settings_, texture_server, main_task_manager, high_priority_task_manager);

	// Load main window geometry and state
	this->restoreGeometry(settings->value("AddObjectDialog/geometry").toByteArray());

	this->tabWidget->setCurrentIndex(settings->value("AddObjectDialog/tabIndex").toInt());

	this->avatarSelectWidget->setType(FileSelectWidget::Type_File);
	//this->avatarSelectWidget->setFilename(settings->value("AddObjectDialogPath").toString());

	connect(this->listWidget, SIGNAL(itemClicked(QListWidgetItem*)), this, SLOT(modelSelected(QListWidgetItem*)));
	connect(this->listWidget, SIGNAL(itemDoubleClicked(QListWidgetItem*)), this, SLOT(modelDoubleClicked(QListWidgetItem*)));
	connect(this->avatarSelectWidget, SIGNAL(filenameChanged(QString&)), this, SLOT(filenameChanged(QString&)));
	connect(this->buttonBox, SIGNAL(accepted()), this, SLOT(accepted()));

	connect(this->urlLineEdit, SIGNAL(textChanged(const QString&)), this, SLOT(urlChanged(const QString&)));
	connect(this->urlLineEdit, SIGNAL(editingFinished()), this, SLOT(urlEditingFinished()));

	connect(this, SIGNAL(finished(int)), this, SLOT(dialogFinished()));

	startTimer(10);

	thread_manager.addThread(new NetDownloadResourcesThread(&this->msg_queue, resource_manager_, &num_net_resources_downloading));

	
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

	for(size_t i=0; i<models.size(); ++i)
	{
		const std::string image_path = base_dir_path + "/data/resources/models/" + models[i] + ".png";

		listWidget->addItem(new QListWidgetItem(QIcon(QtUtils::toQString(image_path)), QtUtils::toQString(models[i])));
	}
}


AddObjectDialog::~AddObjectDialog()
{
	settings->setValue("AddObjectDialog/geometry", saveGeometry());

	settings->setValue("AddObjectDialog/tabIndex", this->tabWidget->currentIndex());
}


void AddObjectDialog::shutdownGL()
{
	// Make sure we have set the gl context to current as we destroy objectPreviewGLWidget.
	this->objectPreviewGLWidget->makeCurrent();

	preview_gl_ob = NULL;
	objectPreviewGLWidget->shutdown();
}


// Called when user presses ESC key, or clicks OK or cancel button.
void AddObjectDialog::dialogFinished()
{
	shutdownGL();
}


void AddObjectDialog::accepted()
{
	this->settings->setValue("AddObjectDialogPath", this->avatarSelectWidget->filename());
}


void AddObjectDialog::modelSelected(QListWidgetItem* selected_item)
{
	if(this->listWidget->currentItem())
	{
		const std::string model = QtUtils::toStdString(this->listWidget->currentItem()->text());

		this->listWidget->setCurrentItem(NULL);

		const std::string model_path = base_dir_path + "/data/resources/models/" + model + ".obj";

		this->result_path = model_path;

		loadModelIntoPreview(model_path);
	}
}


void AddObjectDialog::modelDoubleClicked(QListWidgetItem* selected_item)
{
	const std::string model = QtUtils::toStdString(selected_item->text());
	const std::string model_path = base_dir_path + "/data/resources/models/" + model + ".obj";
	this->result_path = model_path;
	accept();
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


// Sets preview_gl_ob and loaded_object
void AddObjectDialog::makeMeshForWidthAndHeight(const std::string& local_image_or_vid_path, int w, int h)
{
	this->preview_gl_ob = ModelLoading::makeImageCube(*objectPreviewGLWidget->opengl_engine, *objectPreviewGLWidget->opengl_engine->vert_buf_allocator, local_image_or_vid_path, w, h,
		this->loaded_mesh, 
		this->loaded_materials, // world_materials_out
		this->scale // scale_out
	);
}


void AddObjectDialog::loadModelIntoPreview(const std::string& local_path)
{
	this->objectPreviewGLWidget->makeCurrent();

	this->loaded_mesh_is_image_cube = false;

	this->ob_cam_right_translation = 0;
	this->ob_cam_up_translation = 0;

	this->loaded_materials.clear();
	this->scale = Vec3f(1.f);
	this->axis = Vec3f(0, 0, 1);
	this->angle = 0;

	// Try and load model
	try
	{
		if(preview_gl_ob.nonNull())
		{
			// Remove previous object from engine.
			objectPreviewGLWidget->opengl_engine->removeObject(preview_gl_ob);
		}

		if(ImageDecoding::hasSupportedImageExtension(local_path))
		{
			// Load image to get aspect ratio of image.
			// We will scale our model so it has the same aspect ratio.
			Reference<Map2D> im = ImageDecoding::decodeImage(base_dir_path, local_path);

			makeMeshForWidthAndHeight(local_path, (int)im->getMapWidth(), (int)im->getMapHeight());
			
			BitUtils::setOrZeroBit(loaded_materials[0]->flags, WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG, LODGeneration::textureHasAlphaChannel(local_path, im)); // Set COLOUR_TEX_HAS_ALPHA_FLAG flag

			this->loaded_mesh_is_image_cube = true;
		}
		else if(ModelLoading::hasSupportedModelExtension(local_path))
		{
			ModelLoading::MakeGLObjectResults results;
			ModelLoading::makeGLObjectForModelFile(*objectPreviewGLWidget->opengl_engine, *objectPreviewGLWidget->opengl_engine->vert_buf_allocator, /*allocator=*/nullptr, local_path, /*do_opengl_stuff=*/true,
				results
			);

			if(results.materials.size() > WorldObject::maxNumMaterials())
				throw glare::Exception("Model had too many materials (" + toString(results.materials.size()) + "), max number allowed is " + toString(WorldObject::maxNumMaterials()) + ".");

			this->preview_gl_ob = results.gl_ob;

			this->loaded_mesh = results.batched_mesh;
			this->loaded_voxels = results.voxels.voxels;

			this->loaded_materials = results.materials;
			this->scale = results.scale;
			this->axis = results.axis;
			this->angle = results.angle;
		}
		else
			throw glare::Exception("file did not have a supported image or model extension: '" + getExtension(local_path) + "'");

		// Try and load textures
		tryLoadTexturesForPreviewOb(preview_gl_ob, this->loaded_materials, objectPreviewGLWidget->opengl_engine.ptr(), *texture_server, this);

		// Offset object vertically so it rests on the ground plane.
		const js::AABBox cur_aabb_ws = preview_gl_ob->mesh_data->aabb_os.transformedAABBFast(preview_gl_ob->ob_to_world_matrix);
		const float z_trans = -cur_aabb_ws.min_[2];
		preview_gl_ob->ob_to_world_matrix = ::leftTranslateAffine3(Vec4f(0, 0, z_trans, 0), preview_gl_ob->ob_to_world_matrix);

		objectPreviewGLWidget->opengl_engine->addObject(preview_gl_ob);
	}
	catch(Indigo::IndigoException& e)
	{
		this->loaded_materials.clear();

		QtUtils::showErrorMessageDialog(QtUtils::toQString(e.what()), this);
	}
	catch(glare::Exception& e)
	{
		this->loaded_materials.clear();

		QtUtils::showErrorMessageDialog(QtUtils::toQString(e.what()), this);
	}
}


// static
// Used by both AddObjectDialog and AvatarSettingsDialog
void AddObjectDialog::tryLoadTexturesForPreviewOb(Reference<GLObject> preview_gl_ob, std::vector<WorldMaterialRef>& world_materials/*WorldObjectRef loaded_object*/, OpenGLEngine* opengl_engine, 
	TextureServer& texture_server, QWidget* parent_widget)
{
	// Try and load textures.  Report any errors but continue with the loading.
	for(size_t i=0; i<preview_gl_ob->materials.size(); ++i)
	{
		const std::string albedo_tex_path = std::string(preview_gl_ob->materials[i].tex_path);
		if(!albedo_tex_path.empty() && !hasExtension(albedo_tex_path, "mp4"))
		{
			try
			{
				preview_gl_ob->materials[i].albedo_texture = opengl_engine->getTexture(albedo_tex_path); // Load texture

				Reference<Map2D> map = texture_server.getTexForPath(".", albedo_tex_path); // Hopefully is already loaded

				const bool has_alpha = LODGeneration::textureHasAlphaChannel(albedo_tex_path, map);// preview_gl_ob->materials[i].albedo_texture->hasAlpha();// && !preview_gl_ob->materials[i].albedo_texture->isAlphaChannelAllWhite();
				BitUtils::setOrZeroBit(world_materials[i]->flags, WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG, has_alpha);
			}
			catch(glare::Exception& e)
			{
				QtUtils::showErrorMessageDialog(QtUtils::toQString("Error while loading model texture: '" + albedo_tex_path + "': " + e.what() + 
					"\nLoading will continue without this texture"), parent_widget);

				world_materials[i]->colour_texture_url = ""; // Clear texture in WorldMaterial, so we don't insert an invalid texture into the world.
			}
		}

		const std::string metallic_roughness_tex_path = std::string(preview_gl_ob->materials[i].metallic_roughness_tex_path);
		if(!metallic_roughness_tex_path.empty() && !hasExtension(metallic_roughness_tex_path, "mp4"))
		{
			try
			{
				preview_gl_ob->materials[i].metallic_roughness_texture = opengl_engine->getTexture(metallic_roughness_tex_path);
			}
			catch(glare::Exception& e)
			{
				QtUtils::showErrorMessageDialog(QtUtils::toQString("Error while loading model texture: '" + metallic_roughness_tex_path + "': " + e.what() +
					"\nLoading will continue without this texture"), parent_widget);

				world_materials[i]->roughness.texture_url = ""; // Clear texture in WorldMaterial
			}
		}

		const std::string emission_tex_path = std::string(preview_gl_ob->materials[i].emission_tex_path);
		if(!emission_tex_path.empty() && !hasExtension(emission_tex_path, "mp4"))
		{
			try
			{
				preview_gl_ob->materials[i].emission_texture = opengl_engine->getTexture(emission_tex_path);
			}
			catch(glare::Exception& e)
			{
				QtUtils::showErrorMessageDialog(QtUtils::toQString("Error while loading model texture: '" + emission_tex_path + "': " + e.what() +
					"\nLoading will continue without this texture"), parent_widget);

				world_materials[i]->emission_texture_url = ""; // Clear texture in WorldMaterial
			}
		}

		const std::string normal_map_path = std::string(preview_gl_ob->materials[i].normal_map_path);
		if(!normal_map_path.empty() && !hasExtension(normal_map_path, "mp4"))
		{
			try
			{
				TextureParams params;
				params.use_sRGB = false;
				preview_gl_ob->materials[i].normal_map = opengl_engine->getTexture(normal_map_path, params);
			}
			catch(glare::Exception& e)
			{
				QtUtils::showErrorMessageDialog(QtUtils::toQString("Error while loading model texture: '" + normal_map_path + "': " + e.what() +
					"\nLoading will continue without this texture"), parent_widget);

				world_materials[i]->normal_map_url = ""; // Clear texture in WorldMaterial
			}
		}
	}
}


void AddObjectDialog::urlChanged(const QString& filename)
{
	const URLString url = toURLString(QtUtils::toStdString(urlLineEdit->text()));
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
			catch(glare::Exception& e)
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


// Will be called when the user clicks the 'X' button.
void AddObjectDialog::closeEvent(QCloseEvent* event)
{
	shutdownGL();
}


void AddObjectDialog::timerEvent(QTimerEvent* event)
{
	// Once the OpenGL widget has initialised, we can add the model.
	//if(objectPreviewGLWidget->opengl_engine->initSucceeded() && !loaded_model)
	//{
	//	//QString path = settings->value("AddObjectDialogPath").toString();
	//	//filenameChanged(path);
	//	//loaded_model = true;
	//}

	objectPreviewGLWidget->makeCurrent();
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	objectPreviewGLWidget->update();
#else
	objectPreviewGLWidget->updateGL();
#endif

	// Check msg queue
	Lock lock(this->msg_queue.getMutex());
	while(!msg_queue.unlockedEmpty())
	{
		Reference<ThreadMessage> msg;
		this->msg_queue.unlockedDequeue(msg);

		if(dynamic_cast<const ResourceDownloadedMessage*>(msg.getPointer()))
		{
			const ResourceDownloadedMessage* m = static_cast<const ResourceDownloadedMessage*>(msg.getPointer());

			//conPrint("ResourceDownloadedMessage, URL: " + m->URL);
			try
			{
				// Now that the model is downloaded, set the result to be the local path where it was downloaded to.
				this->result_path = resource_manager->pathForURL(m->URL); // TODO: catch

				loadModelIntoPreview(resource_manager->pathForURL(m->URL));
			}
			catch(glare::Exception& e)
			{
				conPrint(e.what());
			}
		}
	}
}
