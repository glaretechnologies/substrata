/*=====================================================================
AddObjectDialog.cpp
------------------------
File created by ClassTemplate on Wed Oct 07 15:16:48 2009
Code By Nicholas Chapman.
=====================================================================*/
#include "AddObjectDialog.h"


#include "ModelLoading.h"
#include "MeshBuilding.h"
#include "NetDownloadResourcesThread.h"
#include "SubstrataVideoSurface.h"
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
#if defined(_WIN32)
#include "../video/WMFVideoReader.h"
#endif


AddObjectDialog::AddObjectDialog(const std::string& base_dir_path_, QSettings* settings_, TextureServer* texture_server_ptr, Reference<ResourceManager> resource_manager_, IMFDXGIDeviceManager* dev_manager_)
:	settings(settings_),
	resource_manager(resource_manager_),
	base_dir_path(base_dir_path_),
	dev_manager(dev_manager_),
	loaded_mesh_is_image_cube(false)
{
	setupUi(this);

	this->objectPreviewGLWidget->setBaseDir(base_dir_path);
	this->objectPreviewGLWidget->texture_server_ptr = texture_server_ptr;

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

	startTimer(10);

	//loaded_model = false;

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
}


void AddObjectDialog::shutdownGL()
{
	// Make sure we have set the gl context to current as we destroy objectPreviewGLWidget.
	preview_gl_ob = NULL;
	objectPreviewGLWidget->shutdown();
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

		const std::string model_path = base_dir_path + "/resources/models/" + model + ".obj";

		this->result_path = model_path;

		loadModelIntoPreview(model_path);
	}
}


void AddObjectDialog::modelDoubleClicked(QListWidgetItem* selected_item)
{
	const std::string model = QtUtils::toStdString(selected_item->text());
	const std::string model_path = base_dir_path + "/resources/models/" + model + ".obj";
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
void AddObjectDialog::makeMeshForWidthAndHeight(glare::TaskManager& task_manager, const std::string& local_image_or_vid_path, int w, int h)
{
	this->preview_gl_ob = ModelLoading::makeImageCube(*objectPreviewGLWidget->opengl_engine->vert_buf_allocator, task_manager, local_image_or_vid_path, w, h,
		this->loaded_mesh, *this->loaded_object);
}


void AddObjectDialog::loadModelIntoPreview(const std::string& local_path)
{
	this->objectPreviewGLWidget->makeCurrent();

	this->loaded_mesh_is_image_cube = false;

	this->ob_cam_right_translation = 0;
	this->ob_cam_up_translation = 0;

	this->loaded_object = new WorldObject();
	this->loaded_object->scale.set(1, 1, 1);
	this->loaded_object->axis = Vec3f(0, 0, 1);
	this->loaded_object->angle = 0;

	// Try and load model
	try
	{
		if(preview_gl_ob.nonNull())
		{
			// Remove previous object from engine.
			objectPreviewGLWidget->opengl_engine->removeObject(preview_gl_ob);
		}


		glare::TaskManager task_manager;

		if(hasExtension(local_path, "mp4"))
		{
#if defined(_WIN32)
			Reference<WMFVideoReader> reader = new WMFVideoReader(false, /*just_read_audio=*/false, local_path, /*NULL,*/ /*reader callback=*/NULL, dev_manager, /*decode_to_d3d_tex=*/false);

			// Load first frame
			const SampleInfoRef frameinfo = reader->getAndLockNextSample(/*just_get_vid_sample=*/true);

			if(frameinfo.isNull())
				throw glare::Exception("frame was null. (EOS?)");

			makeMeshForWidthAndHeight(task_manager, local_path, (int)frameinfo->width, (int)frameinfo->height);

			// Load frame 0 into opengl texture
			preview_gl_ob->materials[0].albedo_texture = new OpenGLTexture(frameinfo->width, frameinfo->height, objectPreviewGLWidget->opengl_engine.ptr(),
				ArrayRef<uint8>(NULL, 0), // Just pass in NULL here so we can use loadIntoExistingTexture which takes a stride below
				OpenGLTexture::Format_SRGB_Uint8, // Just report a format without alpha so we cast shadows.
				GL_SRGB8_ALPHA8, // GL internal format
				GL_BGRA, // GL format.  Video frames are BGRA.
				OpenGLTexture::Filtering_Bilinear, // Use bilinear so the OpenGL driver doesn't have to compute mipmaps.
				OpenGLTexture::Wrapping_Repeat);

			ArrayRef<uint8> tex_data_arrayref(frameinfo->frame_buffer, frameinfo->height * frameinfo->stride_B);
			preview_gl_ob->materials[0].albedo_texture->loadIntoExistingTexture(frameinfo->width, frameinfo->height, frameinfo->stride_B, tex_data_arrayref);

			this->loaded_mesh_is_image_cube = true;

#else
			SubstrataVideoSurface* video_surface = new SubstrataVideoSurface(NULL);
			video_surface->load_into_opengl_tex = false; // Just copy into mem buffer.  OpenGL texture stuff gets tricky with multiple GL contexts.

			QMediaPlayer* media_player = new QMediaPlayer(NULL, QMediaPlayer::VideoSurface);
			media_player->setVideoOutput(video_surface);
			media_player->setMedia(QUrl(QtUtils::toQString(local_path)));
			media_player->play();

			// Wait until we have a valid video frame.
			Timer timer;
			while(timer.elapsed() < 10.0)
			{
				if(media_player->error() != QMediaPlayer::NoError)
					break;

				if(!video_surface->frame_copy.empty()) // We have a frame:
					break;

				QCoreApplication::processEvents();
				PlatformUtils::Sleep(1);
			}

			media_player->stop();

			this->objectPreviewGLWidget->makeCurrent(); // Gl context may have been made non-current in QCoreApplication::processEvents() above.

			if(!video_surface->frame_copy.empty())
			{
				const int W = video_surface->current_format.frameWidth();
				const int H = video_surface->current_format.frameHeight();
				makeMeshForWidthAndHeight(task_manager, local_path, W, H);

				// Load frame 0 into opengl texture
				preview_gl_ob->materials[0].albedo_texture = new OpenGLTexture(W, H, objectPreviewGLWidget->opengl_engine.ptr(),
					ArrayRef<uint8>(NULL, 0), // tex data
					OpenGLTexture::Format_SRGB_Uint8, // Just report a format without alpha so we cast shadows.
					GL_SRGB8_ALPHA8, // GL internal format
					GL_BGRA, // GL format.  Video frames are BGRA.  NOTE: FIXME for what qt returns
					OpenGLTexture::Filtering_Bilinear, // Use bilinear so the OpenGL driver doesn't have to compute mipmaps.
					OpenGLTexture::Wrapping_Repeat);
				
				ArrayRef<uint8> tex_data_arrayref(video_surface->frame_copy);

				const size_t stride_B = video_surface->frame_copy.size() / H;
				preview_gl_ob->materials[0].albedo_texture->loadIntoExistingTexture(W, H, stride_B, tex_data_arrayref);
			}
			else
			{
				makeMeshForWidthAndHeight(task_manager, local_path, 1024, 1024);
			}

			delete media_player;
			delete video_surface;
#endif
		}
		else if(ImageDecoding::hasSupportedImageExtension(local_path))
		{
			// Load image to get aspect ratio of image.
			// We will scale our model so it has the same aspect ratio.
			Reference<Map2D> im = ImageDecoding::decodeImage(base_dir_path, local_path);

			makeMeshForWidthAndHeight(task_manager, local_path, (int)im->getMapWidth(), (int)im->getMapHeight());
			
			BitUtils::setOrZeroBit(loaded_object->materials[0]->flags, WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG, LODGeneration::textureHasAlphaChannel(local_path, im)); // Set COLOUR_TEX_HAS_ALPHA_FLAG flag

			this->loaded_mesh_is_image_cube = true;
		}
		else // Else is not an image or an MP4 file:
		{
			preview_gl_ob = ModelLoading::makeGLObjectForModelFile(*objectPreviewGLWidget->opengl_engine->vert_buf_allocator, task_manager, local_path,
				this->loaded_mesh, // mesh out
				*this->loaded_object
			);
		}

		// Try and load textures
		for(size_t i=0; i<preview_gl_ob->materials.size(); ++i)
		{
			if(!preview_gl_ob->materials[i].tex_path.empty() && !hasExtension(preview_gl_ob->materials[i].tex_path, "mp4"))
			{
				preview_gl_ob->materials[i].albedo_texture = objectPreviewGLWidget->opengl_engine->getTexture(preview_gl_ob->materials[i].tex_path);

				Reference<Map2D> map = this->objectPreviewGLWidget->texture_server_ptr->getTexForPath(".", preview_gl_ob->materials[i].tex_path); // Hopefully is arleady loaded

				const bool has_alpha = LODGeneration::textureHasAlphaChannel(preview_gl_ob->materials[i].tex_path, map);// preview_gl_ob->materials[i].albedo_texture->hasAlpha();// && !preview_gl_ob->materials[i].albedo_texture->isAlphaChannelAllWhite();
				BitUtils::setOrZeroBit(loaded_object->materials[i]->flags, WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG, has_alpha);
			}

			if(!preview_gl_ob->materials[i].metallic_roughness_tex_path.empty())
				preview_gl_ob->materials[i].metallic_roughness_texture = objectPreviewGLWidget->opengl_engine->getTexture(preview_gl_ob->materials[i].metallic_roughness_tex_path);
		}

		objectPreviewGLWidget->addObject(preview_gl_ob);
	}
	catch(Indigo::IndigoException& e)
	{
		this->loaded_object = NULL;

		QtUtils::showErrorMessageDialog(QtUtils::toQString(e.what()), this);
	}
	catch(glare::Exception& e)
	{
		this->loaded_object = NULL;

		QtUtils::showErrorMessageDialog(QtUtils::toQString(e.what()), this);
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
