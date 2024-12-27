/*=====================================================================
IndigoView.cpp
--------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "IndigoView.h"


#if INDIGO_SUPPORT

#include <dll/include/IndigoContext.h>
#include <dll/include/IndigoException.h>
#include <dll/include/IndigoString.h>
#include <dll/include/IndigoErrorCodes.h>
#include <dll/include/IndigoLogMessageInterface.h>
#include <dll/include/IndigoErrorMessageInterface.h>
#include <dll/include/IndigoPassDoneMessageInterface.h>
#include <dll/include/IndigoToneMapper.h>
#include <dll/include/IndigoDataManager.h>
#include <dll/include/IndigoHardwareInfo.h>
#include <dll/include/IndigoSettings.h>
#include <dll/include/Renderer.h>
#include <dll/include/RenderBuffer.h>
#include <dll/include/SceneNodeRoot.h>
#include <dll/include/SceneNodeTonemapping.h>
#include <dll/include/SceneNodeCamera.h>
#include <dll/include/SceneNodeRenderSettings.h>

#endif // INDIGO_SUPPORT

#include <utils/ConPrint.h>
#include <utils/PlatformUtils.h>
#include <utils/GlareProcess.h>
#include <utils/FileUtils.h>

#include <QtWidgets/QPushButton>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLayout>
#include <QtWidgets/QVBoxLayout>
#include <QtGui/QResizeEvent>
#include <QtCore/QTimer>

#include "IndigoConversion.h"
#include "CameraController.h"
#include "GlWidget.h"
#include "WorldState.h"


static const bool DO_REALTIME_VIEW = true;


#if INDIGO_SUPPORT
// Standard conversions between std::string and Indigo::String.
static const std::string toStdString(const Indigo::String& s)
{
	return std::string(s.dataPtr(), s.length());
}

static const Indigo::String toIndigoString(const std::string& s)
{
	return Indigo::String(s.c_str(), s.length());
}
#endif // INDIGO_SUPPORT


IndigoView::IndigoView(QWidget* parent)
:	QWidget(parent),
	resize_timer(new QTimer(this))
{
	this->setContentsMargins(0, 0, 0, 0);

	this->label = new QLabel(this);
	this->label->setContentsMargins(0, 0, 0, 0);
	this->label->setMargin(0);

	this->setLayout(new QVBoxLayout());
	this->layout()->addWidget(this->label);

	this->layout()->setContentsMargins(0, 0, 0, 0);
	//this->layout()->setMargin(0);

	this->label->setMinimumSize(QSize(64, 64));

	clearPreview();

	// Set up timer.
	resize_timer->setSingleShot(true);
	resize_timer->setInterval(200);

	connect(resize_timer, SIGNAL(timeout()), this, SLOT(resizeDone()));
}


IndigoView::~IndigoView()
{}


void IndigoView::initialise(const std::string& base_dir_path)
{
#if INDIGO_SUPPORT
	conPrint("=====================================================");
	conPrint("IndigoView::initialise");
	conPrint("=====================================================");
	if(this->renderer.nonNull()) // Return if already initialised.
		return;

	try
	{
		this->context = new Indigo::IndigoContext();

#ifndef NDEBUG
		const std::string dll_dir = "C:/programming/indigo/output/vs2022/indigo_x64/Debug";
#else
		const std::string dll_dir = "C:/programming/indigo/output/vs2022/indigo_x64/RelWithDebInfo";
#endif
		//const std::string dll_dir = base_dir_path; // base_dir_path is the dir the main executable is in.

		Indigo::String error_msg;
		indResult res = this->context->initialise(toIndigoString(dll_dir), Indigo::IndigoContext::getDefaultAppDataPath(), error_msg);
		if(res != Indigo::INDIGO_SUCCESS)
			throw Indigo::IndigoException(error_msg);


		this->root_node = new Indigo::SceneNodeRoot();

		// Create camera node
		{
			this->camera_node = new Indigo::SceneNodeCamera();
			this->camera_node->sensor_width = GlWidget::sensorWidth();
			this->camera_node->lens_sensor_dist = GlWidget::lensSensorDist();
			this->camera_node->autofocus = true;

			this->camera_node->setPos(Indigo::Vec3d(0, 0, 2));

			this->root_node->addChildNode(this->camera_node);
		}

		// Create tonemapping node
		{
			Indigo::SceneNodeTonemappingRef tonemapping = new Indigo::SceneNodeTonemapping(0.003);

			this->root_node->addChildNode(tonemapping);
		}

		// Create render settings node
		{
			this->settings_node = Indigo::SceneNodeRenderSettings::getDefaults();

			settings_node->bidirectional.setValue(false);
			settings_node->metropolis.setValue(false);
			settings_node->width.setValue(1920);
			settings_node->height.setValue(1280);
			settings_node->gpu.setValue(false);//TEMP
			settings_node->vignetting.setValue(false);
			settings_node->merging.setValue(false); // Set to false for now, to allow moving objects etc.. without requiring full rebuilds.  Will be improved in Indigo SDK soon.

			settings_node->setWhitePoint(Indigo::SceneNodeRenderSettings::getWhitepointForWhiteBalance("D65"));

			this->root_node->addChildNode(settings_node);
		}

		// Create background settings / sun-sky node
		{
			Indigo::SunSkyMaterialRef sunsky = new Indigo::SunSkyMaterial();

			const float sun_phi = 1.f;
			const float sun_theta = Maths::pi<float>() / 4;
			const Vec4f sundir = normalise(Vec4f(std::cos(sun_phi) * sin(sun_theta), std::sin(sun_phi) * sun_theta, cos(sun_theta), 0));

			sunsky->sundir = Indigo::Vec3d(sundir[0], sundir[1], sundir[2]);
			sunsky->model = "captured-simulation";

			Indigo::SceneNodeBackgroundSettingsRef background_settings = new Indigo::SceneNodeBackgroundSettings(sunsky);

			this->root_node->addChildNode(background_settings);
		}

		//  Create ground plane geometry, material and model.
		{
			Indigo::MeshRef mesh = new Indigo::Mesh();

			// Make a single quad
			Indigo::Quad q;
			q.mat_index = 0;
			for(int i = 0; i < 4; ++i)
				q.vertex_indices[i] = q.uv_indices[i] = i;
			mesh->quads.push_back(q);

			const float W = 2000;
			mesh->vert_positions.push_back(Indigo::Vec3f(-W, -W, 0));
			mesh->vert_positions.push_back(Indigo::Vec3f(-W, W, 0));
			mesh->vert_positions.push_back(Indigo::Vec3f(W, W, 0));
			mesh->vert_positions.push_back(Indigo::Vec3f(W, -W, 0));

			// Make a UV mapping for the quad
			mesh->num_uv_mappings = 1;

			mesh->uv_pairs.push_back(Indigo::Vec2f(0, 0));
			mesh->uv_pairs.push_back(Indigo::Vec2f(0, W*2));
			mesh->uv_pairs.push_back(Indigo::Vec2f(W*2, W*2));
			mesh->uv_pairs.push_back(Indigo::Vec2f(W*2, 0));

			Indigo::SceneNodeMeshRef mesh_node = new Indigo::SceneNodeMesh(mesh);
			mesh_node->setName("Ground Mesh");

			//==================== Create the ground material =========================
			Indigo::DiffuseMaterialRef diffuse = new Indigo::DiffuseMaterial(
				new Indigo::TextureWavelengthDependentParam(Indigo::Texture("resources/obstacle.png")) // albedo param
			);

			Indigo::SceneNodeMaterialRef mat_node = new Indigo::SceneNodeMaterial(diffuse);
			mat_node->setName("Ground diffuse material");

			//==================== Create the ground object =========================
			Indigo::SceneNodeModelRef model = new Indigo::SceneNodeModel();
			model->setName("Ground Object");
			model->setGeometry(mesh_node);
			model->keyframes.push_back(Indigo::KeyFrame());
			model->rotation = new Indigo::MatrixRotation();
			model->setMaterials(Indigo::Vector<Indigo::SceneNodeMaterialRef>(1, mat_node));

			this->root_node->addChildNode(model); // Add node to scene graph.
		}


		// Query GPU devices
		Indigo::HardwareInfoRef hardware_info = new Indigo::HardwareInfo(this->context);

		const Indigo::Vector<Indigo::OpenCLDevice> devices = hardware_info->queryOpenCLDevices()->getOpenCLDevices();

		// Work out which devices to use.  We will use all GPU devices.
		Indigo::Vector<Indigo::OpenCLDevice> devices_to_use;
		for(size_t i=0; i<devices.size(); ++i)
		{
			if(!devices[i].is_cpu_device) // If this is a GPU device:
			{
				conPrint("Using device " + toStdString(devices[i].device_name));
				devices_to_use.push_back(devices[i]);
			}
		}

		if(!devices_to_use.empty()) // If we found at least once GPU device:
		{
			settings_node->gpu.setValue(true); // Enable GPU rendering
			settings_node->enabled_opencl_devices = devices_to_use; // Tell indigo which devices to use.
		}
		else
			settings_node->gpu.setValue(false); // Disable GPU rendering

		this->root_node->finalise("dummy_scene_path");

		this->data_manager = new Indigo::DataManager(this->context);

		this->renderer = new Indigo::Renderer(this->context);

		this->render_buffer = new Indigo::RenderBuffer(this->root_node);

		this->uint8_buffer = new Indigo::UInt8Buffer();

		this->tone_mapper = new Indigo::ToneMapper(this->context, this->render_buffer, this->uint8_buffer, /*float_buffer=*/NULL);

		this->tone_mapper->update(this->root_node->getToneMapping(), this->settings_node, this->camera_node);

		Indigo::Vector<Indigo::String> command_line_args;
		command_line_args.push_back("dummy_scene_path");

		if(DO_REALTIME_VIEW)
		{
			res = this->renderer->initialiseWithScene(this->root_node, render_buffer, command_line_args, data_manager, tone_mapper);
			if(res != Indigo::INDIGO_SUCCESS)
				throw Indigo::IndigoException("initialiseWithScene error.");

			this->renderer->startRendering();
		}
	}
	catch(Indigo::IndigoException& e)
	{
		conPrint("Indigo initialisation error: " + toStdString(e.what()));
		return;
	}
#endif
}


void IndigoView::shutdown()
{
	// conPrint("=====================================================");
	// conPrint("IndigoView::shutdown");
	// conPrint("=====================================================");
#if INDIGO_SUPPORT
	try
	{
		this->renderer = NULL;
		this->tone_mapper = NULL;
		this->uint8_buffer = NULL;
		this->render_buffer = NULL;
		this->data_manager = NULL;
		this->root_node = NULL;
		this->settings_node = NULL;
		this->camera_node = NULL;
		this->context = NULL;
	}
	catch(Indigo::IndigoException& e)
	{
		conPrint("Error while deleting Indigo API objects: " + toStdString(e.what()));
	}
#endif
}


void IndigoView::addExistingObjects(const WorldState& world_state, ResourceManager& resource_manager)
{
#if INDIGO_SUPPORT
	if(this->renderer.isNull())
		return;

	for(auto it = world_state.objects.valuesBegin(); it != world_state.objects.valuesEnd(); ++it)
	{
		WorldObject* ob = it.getValue().ptr();

		if(ob->physics_object.nonNull())
		{
			Indigo::SceneNodeModelRef model_node = IndigoConversion::convertObject(*ob, resource_manager);

			{
				Indigo::Lock lock(this->root_node->getMutex());

				this->root_node->addChildNode(model_node);

				ob->indigo_model_node = model_node;
			}
		}
	}

	this->renderer->updateScene();
#endif
}


void IndigoView::objectAdded(WorldObject& object, ResourceManager& resource_manager)
{
#if INDIGO_SUPPORT
	if(this->renderer.isNull())
		return;

	Indigo::SceneNodeModelRef model_node = IndigoConversion::convertObject(object, resource_manager);

	{
		Indigo::Lock lock(this->root_node->getMutex());

		this->root_node->addChildNode(model_node);
	}

	this->renderer->updateScene();

	object.indigo_model_node = model_node;
#endif
}


// NOTE: This code currently has no effect on the Indigo rendering, will be fixed soon.
void IndigoView::objectRemoved(WorldObject& object)
{
#if INDIGO_SUPPORT
	if(this->renderer.isNull())
		return;

	if(object.indigo_model_node.nonNull())
	{
		{
			Indigo::Lock lock(this->root_node->getMutex());

			this->root_node->removeChildNode(object.indigo_model_node);
		}

		this->renderer->updateScene();

		object.indigo_model_node = NULL;
	}
#endif
}

#if INDIGO_SUPPORT
// Without translation
static const Matrix4f obToWorldMatrix(const WorldObject* ob)
{
	return Matrix4f::rotationMatrix(normalise(ob->axis.toVec4fVector()), ob->angle) *
		Matrix4f::scaleMatrix(ob->scale.x, ob->scale.y, ob->scale.z);
}


inline static Indigo::Vec3d toIndigoVec3d(const Vec3d& c)
{
	return Indigo::Vec3d(c.x, c.y, c.z);
}
#endif


// NOTE: This code generally kicks of a full scene rebuild, will be fixed soon to only do partial rebuilds.
void IndigoView::objectTransformChanged(WorldObject& object)
{
#if INDIGO_SUPPORT
	if(this->renderer.isNull())
		return;

	if(object.indigo_model_node.nonNull())
	{
		{
			Indigo::Lock lock(this->root_node->getMutex());

			object.indigo_model_node->keyframes = Indigo::Vector<Indigo::KeyFrame>(1, Indigo::KeyFrame(
				0.0,
				toIndigoVec3d(object.pos),
				Indigo::AxisAngle::identity()
			));

			object.indigo_model_node->rotation = new Indigo::MatrixRotation(obToWorldMatrix(&object).getUpperLeftMatrix().e);

			object.indigo_model_node->setDirtyFlags(Indigo::SceneNode::IsDirty | Indigo::SceneNode::TransformChanged);
		}

		this->renderer->updateScene();
	}
#endif
}


void IndigoView::cameraUpdated(const CameraController& cam_controller)
{
#if INDIGO_SUPPORT
	if(this->renderer.isNull())
		return;

	const Vec3d pos = cam_controller.getPosition();
	const Vec3d fwd = cam_controller.getForwardsVec();
	{
		Indigo::Lock lock(this->root_node->getMutex());

		this->camera_node->setPosAndForwards(Indigo::Vec3d(pos.x, pos.y, pos.z), Indigo::Vec3d(fwd.x, fwd.y, fwd.z));

		this->camera_node->setDirtyFlags(Indigo::SceneNode::IsDirty | Indigo::SceneNode::TransformChanged);
	}

	this->renderer->updateScene();
#endif
}


void IndigoView::saveSceneToDisk()
{
#if INDIGO_SUPPORT
	if(this->root_node.isNull())
		return;

	try
	{
		conPrint("Saving scene to disk...");

		const std::string scene_path = PlatformUtils::getOrCreateAppDataDirectory("Cyberspace") + "/indigo_scenes/scene.igs";
		FileUtils::createDirsForPath(scene_path);

		Indigo::SceneNodeRoot::WriteToXmlFileOptions options;
		options.disk_path = toIndigoString(scene_path);

		root_node->writeToXMLFileOnDisk2(options);

		conPrint("Done writing to scene on disk.");

		// Try and launch indigo process

#if defined(_WIN32)
		const std::string indigo_dir = PlatformUtils::getStringRegKey(PlatformUtils::RegHKey_LocalMachine, "SOFTWARE\\Glare Technologies\\Indigo Renderer", "InstallDirectory");

		const std::string indigo_path = indigo_dir + "\\indigo.exe";

		//PlatformUtils::execute("\"" + indigo_path + "\" \"" + toStdString(options.disk_path) + "\"");
		std::vector<std::string> command_line_args;
		command_line_args.push_back(indigo_path);
		command_line_args.push_back(toStdString(options.disk_path));
		glare::Process process(indigo_path, command_line_args);

#endif
	}
	catch(FileUtils::FileUtilsExcep& e)
	{
		conPrint("Error saving scene to disk: " + e.what());
	}
	catch(PlatformUtils::PlatformUtilsExcep& e)
	{
		conPrint("Error saving scene to disk: " + e.what());
	}
	catch(Indigo::IndigoException& e)
	{
		conPrint("Error saving scene to disk: " + toStdString(e.what()));
	}
#endif
}


void IndigoView::clearPreview()
{
	const QSize use_res = this->label->size();

	QPixmap p(use_res);
	p.fill(Qt::darkGray);

	this->label->setPixmap(p);
}


void IndigoView::resizeEvent(QResizeEvent* ev)
{
	resize_timer->start();
}


void IndigoView::resizeDone()
{
	clearPreview();
#if INDIGO_SUPPORT
	if(this->renderer.isNull())
		return;

	const int W = this->label->width();
	const int H = this->label->height();

	{
		Indigo::Lock lock(this->root_node->getMutex());
		settings_node->width.setValue(W);
		settings_node->height.setValue(H);
		this->settings_node->setDirtyFlags(Indigo::SceneNode::IsDirty);
	}

	this->renderer->updateScene();
#endif
}


void IndigoView::timerThink()
{
#if INDIGO_SUPPORT
	if(this->renderer.isNull())
		return;

	if(this->tone_mapper->isImageFresh())
	{
		// Copy image from the Indigo API Uint8 buffer, to the Qt label, in order to display it.
		Indigo::Lock lock(this->uint8_buffer->getMutex());

		const int W = (int)this->uint8_buffer->width();
		const int H = (int)this->uint8_buffer->height();
		QImage im(W, H, QImage::Format_RGB32);
			
		for(int y=0; y<H; ++y)
		{
			uint32* scanline = (uint32*)im.scanLine(y);

			for(int x=0; x<W; ++x)
			{
				const uint8* src = this->uint8_buffer->getPixel(x, y);
				scanline[x] = qRgb(src[0], src[1], src[2]);
			}
		}

		this->label->setPixmap(QPixmap::fromImage(im));
	}

	// Print out any progress logs and error messages coming back from Indigo.
	{
		Indigo::Vector<Indigo::Handle<Indigo::MessageInterface> > messages;
		this->renderer->getMessages(messages);

		for(size_t i=0; i<messages.size(); ++i)
		{
			switch(messages[i]->getType())
			{
			case Indigo::MessageInterface::LOG_MESSAGE:
			{
				const Indigo::LogMessageInterface* m = static_cast<const Indigo::LogMessageInterface*>(messages[i].getPointer());
				conPrintStr("INDIGO: " + toStdString(m->getMessage()));
				break;
			}
			case Indigo::MessageInterface::ERROR_MESSAGE:
			{
				const Indigo::ErrorMessageInterface* m = static_cast<const Indigo::ErrorMessageInterface*>(messages[i].getPointer());
				conPrint("INDIGO ERROR: " + toStdString(m->getMessage()));
				break;
			}
			default:
				break;
			}
		}
	}
#endif
}
