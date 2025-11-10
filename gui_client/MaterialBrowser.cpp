/*=====================================================================
MaterialBrowser.cpp
-------------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#include "MaterialBrowser.h"


#include <QtWidgets/QPushButton>
#include "../qt/FlowLayout.h"
#include "../shared/WorldMaterial.h"
#include "ModelLoading.h"
#include <opengl/OpenGLEngine.h>
#include <opengl/FrameBuffer.h>
#include <graphics/PNGDecoder.h>
#include <graphics/jpegdecoder.h>
#include <graphics/SRGBUtils.h>
#include <qt/QtUtils.h>
#include <FileUtils.h>
#include <StringUtils.h>
#include <ConPrint.h>
#include <IncludeXXHash.h>
#include <Exception.h>


static const int PREVIEW_SIZE = 160;


MaterialBrowser::MaterialBrowser()
:	print_output(NULL)
{
}


MaterialBrowser::~MaterialBrowser()
{
}


void MaterialBrowser::init(QWidget* parent, const std::string& basedir_path_, const std::string& appdata_path_, PrintOutput* print_output_)
{
	basedir_path = basedir_path_;
	appdata_path = appdata_path_;
	print_output = print_output_;

	setupUi(this);

	flow_layout = new FlowLayout(this);

	// Scan for all materials on disk, make preview buttons for them.

	// NOTE: disabled rendering of previews for now, just use pre-computed JPGs instead.
	// Rendering was slow, seldom tested, and prone to crashing.

	try
	{
		const std::vector<std::string> filepaths = FileUtils::getFilesInDirWithExtensionFullPaths(basedir_path + "/data/resources/materials", "submat");

		for(size_t i=0; i<filepaths.size(); ++i)
		{
			const std::string mat_name = ::removeDotAndExtension(FileUtils::getFilename(filepaths[i]));

			const std::string mat_dir = ::removeDotAndExtension(filepaths[i]);
			const std::string preview_path = mat_dir + "/preview.jpg";
		
			QImage image;
			image.load(QtUtils::toQString(preview_path));

			QPushButton* button = new QPushButton();

			button->setFixedWidth(PREVIEW_SIZE);
			button->setFixedHeight(PREVIEW_SIZE);
			button->setIconSize(QSize(PREVIEW_SIZE, PREVIEW_SIZE));

			button->setIcon(QPixmap::fromImage(image));

			button->setToolTip(QtUtils::toQString(mat_name));

			flow_layout->addWidget(button);

			connect(button, SIGNAL(clicked()), this, SLOT(buttonClicked()));

			browser_buttons.push_back(button);
			mat_paths.push_back(filepaths[i]);
		}
	}
	catch(glare::Exception& e)
	{
		if(print_output)
			print_output->print("MaterialBrowser: " + e.what());
		conPrint("Error: " + e.what());
	}
}


void MaterialBrowser::buttonClicked()
{
	assert(mat_paths.size() == browser_buttons.size());

	QPushButton* button = static_cast<QPushButton*>(QObject::sender());
	for(size_t z=0; z<browser_buttons.size(); ++z)
		if(browser_buttons[z] == button)
		{
			emit materialSelected(mat_paths[z]);
		}
}


void MaterialBrowser::renderThumbnails(Reference<OpenGLEngine> opengl_engine)
{
	OpenGLSceneRef old_scene = opengl_engine->getCurrentScene();

	try
	{
		const std::string materials_dir = "C:/code/substrata/resources/materials";
		const std::vector<std::string> file_paths = FileUtils::getFilesInDirWithExtensionFullPaths(materials_dir, "submat");

		for(size_t i=0; i<file_paths.size(); ++i)
		{
			const std::string mat_name = ::removeDotAndExtension(FileUtils::getFilename(file_paths[i]));
			const std::string mat_dir  = ::removeDotAndExtension(file_paths[i]);
			const std::string preview_png_path = mat_dir + "/preview.png";
			const std::string preview_jpeg_path = mat_dir + "/preview.jpg";

			OpenGLSceneRef scene = new OpenGLScene(*opengl_engine);
			opengl_engine->addScene(scene);
			opengl_engine->setCurrentScene(scene);

			// Add cube object with the material applied
			{
				GLObjectRef cube_ob = opengl_engine->allocateObject();
				cube_ob->materials.resize(1);
			
				WorldMaterialRef mat = WorldMaterial::loadFromXMLOnDisk(file_paths[i], /*convert_rel_paths_to_abs_disk_paths=*/true);
			
				ModelLoading::setGLMaterialFromWorldMaterialWithLocalPaths(*mat, cube_ob->materials[0]);
			
				const float cube_w = 0.5f;
				cube_ob->ob_to_world_matrix = Matrix4f::translationMatrix(0, 0, cube_w/2) * Matrix4f::rotationAroundZAxis(Maths::pi_4<float>()) *
					Matrix4f::uniformScaleMatrix(cube_w) * Matrix4f::translationMatrix(-0.5f, -0.5f, -0.5f);
				cube_ob->mesh_data = opengl_engine->getCubeMeshData();
			
				// Load any textures
				for(size_t z=0; z<cube_ob->materials.size(); ++z)
					if(!cube_ob->materials[z].tex_path.empty())
						cube_ob->materials[z].albedo_texture = opengl_engine->getTexture(toStdString(cube_ob->materials[z].tex_path));
			
				opengl_engine->addObject(cube_ob);
			}

			// Add env mat
			{
				OpenGLMaterial env_mat;
				opengl_engine->setEnvMat(env_mat);

				opengl_engine->setSunDir(normalise(Vec4f(0.5f, 1.f, 1.f, 0)));
			}

			// Add a ground plane
			{
				const float W = 200;
			
				GLObjectRef ob = opengl_engine->allocateObject();
				ob->materials.resize(1);
				ob->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(0.9f));
				ob->materials[0].albedo_texture = opengl_engine->getTexture(basedir_path + "/data/resources/obstacle.png");
				ob->materials[0].roughness = 0.8f;
				ob->materials[0].fresnel_scale = 0.5f;
				ob->materials[0].tex_matrix = Matrix2f(W, 0, 0, W);
			
				ob->ob_to_world_matrix = Matrix4f::scaleMatrix(W, W, 1) * Matrix4f::translationMatrix(-0.5f, -0.5f, 0);
				ob->mesh_data = opengl_engine->getUnitQuadMeshData();
			
				opengl_engine->addObject(ob);
			}

			const Matrix4f world_to_camera_space_matrix = Matrix4f::rotationAroundXAxis(0.5f) * Matrix4f::translationMatrix(0, 0.8, -0.6) * Matrix4f::rotationAroundZAxis(2.5);

			const float sensor_width = 0.035f;
			const float lens_sensor_dist = 0.03f;
			const float render_aspect_ratio = 1.0;

			opengl_engine->setMainViewportDims(PREVIEW_SIZE, PREVIEW_SIZE);
			opengl_engine->setViewportDims(PREVIEW_SIZE, PREVIEW_SIZE);
			opengl_engine->setNearDrawDistance(0.1f);
			opengl_engine->setMaxDrawDistance(100.f);
			opengl_engine->setPerspectiveCameraTransform(world_to_camera_space_matrix, sensor_width, lens_sensor_dist, render_aspect_ratio, /*lens shift up=*/0.f, /*lens shift right=*/0.f);


			OpenGLTextureRef target_tex = new OpenGLTexture(PREVIEW_SIZE, PREVIEW_SIZE, opengl_engine.ptr(),
				ArrayRef<uint8>(),
				OpenGLTextureFormat::Format_RGBA_Linear_Uint8,
				OpenGLTexture::Filtering_Nearest,
				OpenGLTexture::Wrapping_Clamp
			);

			opengl_engine->waitForAllBuildingProgramsToBuild();

			ImageMapUInt8Ref im = opengl_engine->drawToBufferAndReturnImageMap();
			
			FileUtils::createDirIfDoesNotExist(FileUtils::getDirectory(preview_jpeg_path));
			JPEGDecoder::save(im->extract3ChannelImage(), preview_jpeg_path, JPEGDecoder::SaveOptions());

			// PNGDecoder::write(*im, preview_png_path);

			conPrint("Wrote to " + preview_jpeg_path);

			opengl_engine->removeScene(scene);
		}
	}
	catch(glare::Exception& e)
	{
		conPrint(e.what());
	}
	
	// Restore old scene
	opengl_engine->setCurrentScene(old_scene);
}
