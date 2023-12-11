/*=====================================================================
MiniMap.cpp
-----------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "MiniMap.h"


#include "IncludeOpenGL.h"
#include "MainWindow.h"
#include "ClientThread.h"
#include "../shared/Protocol.h"
#include "../shared/MessageUtils.h"
#include "../shared/ResourceManager.h"
#include <graphics/SRGBUtils.h>
#include <utils/RuntimeCheck.h>


MiniMap::MiniMap()
:	main_window(NULL),
	last_requested_campos(Vec3d(-1000000))
{}


MiniMap::~MiniMap()
{}


static const float minimap_width = 0.29f;
static const float margin = 0.03f;

static const int TILE_GRID_RES = 9;


static const float tile_w = 160;

//const float TILE_WIDTH_M = 5120.f / (1 << z); //TILE_WIDTH_PX * metres_per_pixel;   See updateMapTiles() in server.cpp
// If z = 5: 
// TILE_WIDTH_M = 5120 / 32 = 160

void MiniMap::create(Reference<OpenGLEngine>& opengl_engine_, MainWindow* main_window_, GLUIRef gl_ui_)
{
	opengl_engine = opengl_engine_;
	main_window = main_window_;
	gl_ui = gl_ui_;
	
	minimap_texture = new OpenGLTexture(256, 256, opengl_engine.ptr(), ArrayRef<uint8>(NULL, 0), OpenGLTexture::Format_RGB_Linear_Uint8, OpenGLTexture::Filtering_Bilinear);

	// Create marker dot
	minimap_image = new GLUIImage();
	minimap_image->create(*gl_ui, opengl_engine, "", Vec2f(1 - minimap_width - margin, gl_ui->getViewportMinMaxY(opengl_engine) - minimap_width - margin), Vec2f(minimap_width), /*tooltip=*/"minimap");
	minimap_image->overlay_ob->material.albedo_texture = minimap_texture;
	minimap_image->overlay_ob->material.tex_matrix = Matrix2f::identity(); // Since we are using a texture rendered in OpenGL we don't need to flip it.

	gl_ui->addWidget(minimap_image);

	//--------------------- Create minimap tile scene ------------------------
	Reference<OpenGLScene> last_scene = opengl_engine->getCurrentScene();

	scene = new OpenGLScene(*opengl_engine);
	scene->shadow_mapping = false;
	scene->draw_water = false;
	scene->use_main_render_framebuffer = false;
	scene->use_z_up = false;
	scene->env_ob = NULL;

	opengl_engine->addScene(scene);
	opengl_engine->setCurrentScene(scene);

	tiles.resize(TILE_GRID_RES, TILE_GRID_RES);
	last_centre_x = -1000000;
	last_centre_y = -1000000;

	for(int y=0; y<TILE_GRID_RES; ++y)
	for(int x=0; x<TILE_GRID_RES; ++x)
	{
		Reference<OverlayObject> ob = new OverlayObject();
		ob->mesh_data = opengl_engine->getUnitQuadMeshData();
		ob->material.tex_matrix = Matrix2f(1,0,0,-1);
		ob->material.tex_translation = Vec2f(0, 1);
		ob->ob_to_world_matrix = Matrix4f::translationMatrix(x * tile_w, y * tile_w, 0) *  Matrix4f::scaleMatrix(tile_w, tile_w, 1);

		opengl_engine->addOverlayObject(ob);

		tiles.elem(x, y).ob = ob;
	}

	opengl_engine->setCurrentScene(last_scene); // Restore
	//-------------------------------------------------------------------

	frame_buffer = new FrameBuffer();
	frame_buffer->bindTextureAsTarget(*minimap_texture, GL_COLOR_ATTACHMENT0);

	GLenum is_complete = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if(is_complete != GL_FRAMEBUFFER_COMPLETE)
	{
		conPrint("Error: renderMaskMap(): framebuffer is not complete.");
		assert(0);
	}
}


void MiniMap::destroy()
{
	if(gl_ui.nonNull())
	{
		//------------------- Destroy the minimap tile scene ------------------------
		Reference<OpenGLScene> last_scene = opengl_engine->getCurrentScene();
		opengl_engine->setCurrentScene(scene);

		for(int y=0; y<TILE_GRID_RES; ++y)
		for(int x=0; x<TILE_GRID_RES; ++x)
		{
			opengl_engine->removeOverlayObject(tiles.elem(x, y).ob);
			tiles.elem(x, y).ob = NULL;
		}
		tiles.resize(0, 0);

		opengl_engine->removeScene(scene);
		scene = NULL;
		opengl_engine->setCurrentScene(last_scene);
		//---------------------------------------------------------------------------

		if(minimap_image.nonNull())
		{
			gl_ui->removeWidget(minimap_image);
			minimap_image->destroy();
		}
	}

	gl_ui = NULL;
	opengl_engine = NULL;
}


void MiniMap::think()
{
	if(gl_ui.isNull())
		return;

	const Vec3d campos = main_window->cam_controller.getFirstPersonPosition();

	if((main_window->connection_state == MainWindow::ServerConnectionState_Connected) && (main_window->server_protocol_version >= 39)) // QueryMapTiles message was introduced in protocol version 39.
	{
		if(campos.getDist(last_requested_campos) > 300.0)
		{
			// Send request to server to get map tile image URLS

			// Send QueryObjectsInAABB for initial volume around camera to server
			{
				const int new_centre_x = Maths::floorToInt((float)campos.x / tile_w);
				const int new_centre_y = Maths::floorToInt((float)campos.y / tile_w);

			
				const int query_rad = 4;
				std::vector<Vec3i> query_indices;
				query_indices.reserve(Maths::square(2*query_rad + 1));
			
				for(int x=new_centre_x - query_rad; x <= new_centre_x + query_rad; ++x)
				for(int y=new_centre_y - query_rad; y <= new_centre_y + query_rad; ++y)
				{
					query_indices.push_back(Vec3i(x, y, 5));
				}

				SocketBufferOutStream scratch_packet(SocketBufferOutStream::DontUseNetworkByteOrder);

				// Make QueryMapTiles packet and enqueue to send
				MessageUtils::initPacket(scratch_packet, Protocol::QueryMapTiles);
			
				scratch_packet.writeUInt32((uint32)query_indices.size());
				for(size_t i=0; i<query_indices.size(); ++i)
				{
					scratch_packet.writeInt32(query_indices[i].x);
					scratch_packet.writeInt32(query_indices[i].y);
					scratch_packet.writeInt32(query_indices[i].z);
				}

				MessageUtils::updatePacketLengthField(scratch_packet);
				main_window->client_thread->enqueueDataToSend(scratch_packet.buf);
			}

			last_requested_campos = campos;
		}
	}


	checkUpdateTilesForCurCamPosition();
	
	Reference<OpenGLScene> last_scene = opengl_engine->getCurrentScene();
	const Vec2i last_viewport = opengl_engine->getViewportDims();
	Reference<FrameBuffer> last_target_framebuffer = opengl_engine->getTargetFrameBuffer();
	
	opengl_engine->setCurrentScene(this->scene);
	
	renderTilesToTexture();

	opengl_engine->setCurrentScene(last_scene);
	opengl_engine->setViewportDims(last_viewport);
	opengl_engine->setTargetFrameBuffer(last_target_framebuffer);
}


void MiniMap::checkUpdateTilesForCurCamPosition()
{
	Reference<OpenGLScene> last_scene = opengl_engine->getCurrentScene();
	opengl_engine->setCurrentScene(this->scene);

	const Vec3d campos = main_window->cam_controller.getFirstPersonPosition();

	const int new_centre_x = Maths::floorToInt((float)campos.x / tile_w);
	const int new_centre_y = Maths::floorToInt((float)campos.y / tile_w);
	
	// Update tree info and imposters
	if(new_centre_x != last_centre_x || new_centre_y != last_centre_y)
	{
		const int x0     = new_centre_x  - TILE_GRID_RES/2; // unwrapped grid x coordinate of lower left grid cell in square grid around new camera position
		const int y0     = new_centre_y  - TILE_GRID_RES/2;
		const int old_x0 = last_centre_x - TILE_GRID_RES/2; // unwrapped grid x coordinate of lower left grid cell in square grid around old camera position
		const int old_y0 = last_centre_y - TILE_GRID_RES/2;
		const int wrapped_x0     = Maths::intMod(x0,     TILE_GRID_RES); // x0 mod TILE_GRID_RES                        [Using euclidean modulo]
		const int wrapped_y0     = Maths::intMod(y0,     TILE_GRID_RES); // y0 mod TILE_GRID_RES                        [Using euclidean modulo]
		const int old_wrapped_x0 = Maths::intMod(old_x0, TILE_GRID_RES);
		const int old_wrapped_y0 = Maths::intMod(old_y0, TILE_GRID_RES);

		// Iterate over wrapped coordinates
		MapTile* const tiles_data = tiles.getData();
		for(int j=0; j<TILE_GRID_RES; ++j)
		for(int i=0; i<TILE_GRID_RES; ++i)
		{
			// Compute old unwrapped cell indices
			// See if they are in range of new camera position
			// If not unload objects in that cell, and load in objects for new camera position

			// Compute old unwrapped cell indices:
			const int old_x = old_x0 + i - old_wrapped_x0 + ((i >= old_wrapped_x0) ? 0 : TILE_GRID_RES);
			const int old_y = old_y0 + j - old_wrapped_y0 + ((j >= old_wrapped_y0) ? 0 : TILE_GRID_RES);

			if( old_x < x0 || old_x >= x0 + TILE_GRID_RES || 
				old_y < y0 || old_y >= y0 + TILE_GRID_RES)
			{
				// This chunk is out of range of new camera.
					
				MapTile& tile = tiles_data[i + j * TILE_GRID_RES];

				// Get unwrapped coords
				const int x = x0 + i - wrapped_x0 + ((i >= wrapped_x0) ? 0 : TILE_GRID_RES);
				const int y = y0 + j - wrapped_y0 + ((j >= wrapped_y0) ? 0 : TILE_GRID_RES);
				assert(x >= x0 && x < x0 + TILE_GRID_RES);
				assert(y >= y0 && y < y0 + TILE_GRID_RES);

				// Look up our tile info for these coordinates
				auto res = tile_infos.find(Vec3i(x, y, 5));
				if(res != tile_infos.end())
				{
					const MapTileInfo& info = res->second;
					if(!info.image_URL.empty())
					{
						ResourceRef resource = main_window->resource_manager->getExistingResourceForURL(info.image_URL);
						if(resource.nonNull())
						{
							const std::string local_path = resource->getLocalAbsPath(main_window->resources_dir);

							tile.ob->material.albedo_texture = opengl_engine->getTextureIfLoaded(OpenGLTextureKey(local_path));
				
							if(tile.ob->material.albedo_texture.isNull())
								tile.ob->material.tex_path = local_path; // Store path so texture will be assigned later when loaded.
						}
					}
				}

				// Visualise individual tiles by colouring them differently
				//	tile.ob->material.albedo_linear_rgb.r = 0.5 + x * 0.1f;
				//	tile.ob->material.albedo_linear_rgb.g = 0.5 + y * 0.1f;

				tile.ob->ob_to_world_matrix = Matrix4f::translationMatrix(x * tile_w, y * tile_w, 0) *  Matrix4f::scaleMatrix(tile_w, tile_w, 1);
			}
		}

		last_centre_x = new_centre_x;
		last_centre_y = new_centre_y;
	}

	opengl_engine->setCurrentScene(last_scene); // Restore scene
}


void MiniMap::renderTilesToTexture()
{
	const Vec3d campos = main_window->cam_controller.getPosition();

	const float map_w_ws = 500;
	Vec3d botleft_pos = campos - Vec3d(map_w_ws/2, map_w_ws/2, 0);

	opengl_engine->setOrthoCameraTransform(Matrix4f::translationMatrix(-campos.x, -campos.y, -10), /*sensor width=*/map_w_ws, /*render aspect ratio=*/1, 0, 0);

	this->scene->overlay_world_to_camera_space_matrix = Matrix4f::scaleMatrix(2.f/map_w_ws, 2.f/map_w_ws, 0) * Matrix4f::translationMatrix(-campos.x, -campos.y, 0);

	opengl_engine->setTargetFrameBufferAndViewport(frame_buffer);

	opengl_engine->draw();
}


void MiniMap::handleMapTilesResultReceivedMessage(const MapTilesResultReceivedMessage& msg)
{
	// conPrint("MiniMap::handleMapTilesResultReceivedMessage");

	runtimeCheck(msg.tile_indices.size() == msg.tile_URLS.size());

	for(size_t i=0; i<msg.tile_indices.size(); ++i)
	{
		Vec3i indices = msg.tile_indices[i];
		
		auto res = tile_infos.find(indices);
		if(res == tile_infos.end())
		{
			const std::string& URL = msg.tile_URLS[i];
			MapTileInfo info;
			info.image_URL = URL;
			tile_infos[indices] = info;

			if(!URL.empty())
			{
				const Vec3d tile_pos(0.0); // TEMP HACK could use tile position in world space? or cam position?

				// Start loading or downloading this tile image (if not already downloaded)
				DownloadingResourceInfo downloading_info;
				downloading_info.pos = tile_pos;
				downloading_info.size_factor = LoadItemQueueItem::sizeFactorForAABBWS(tile_w, /*importance_factor=*/1.f);
				downloading_info.is_minimap_tile = true;

				main_window->startDownloadingResource(URL, tile_pos.toVec4fPoint(), tile_w, downloading_info);


				// Start loading the texture (if not already loaded)
				main_window->startLoadingTexture(URL, tile_pos.toVec4fPoint(), tile_w, /*max task dist=*/1.0e10f, /*importance factor=*/1.f, 
					/*use_sRGB=*/true, /*allow_compression=*/true, /*is_terrain_map=*/false, /*is_minimap_tile=*/true);
			}
		}
		else
		{
			// Update map tile info URL?
		}
	}

	// Force reassignment of tile textures, now that we may know URLs for the textures
	last_centre_x = -10000;
	last_centre_y = -10000;
	checkUpdateTilesForCurCamPosition();
}


void MiniMap::viewportResized(int w, int h)
{
	if(gl_ui.nonNull())
	{
		updateWidgetPositions();
	}
}


void MiniMap::updateWidgetPositions()
{
	if(minimap_image.nonNull())
	{
		minimap_image->setPosAndDims(Vec2f(1 - minimap_width - margin, gl_ui->getViewportMinMaxY(opengl_engine) - minimap_width - margin), Vec2f(minimap_width));
	}
}


//bool MiniMap::doHandleMouseMoved(const Vec2f& coords) override
//{
//	conPrint("MiniMap::doHandleMouseMoved");
//}


void MiniMap::eventOccurred(GLUICallbackEvent& event)
{
	conPrint("MiniMap::eventOccurred");
}
