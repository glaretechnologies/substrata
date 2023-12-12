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
	last_requested_campos(Vec3d(-1000000)),
	last_requested_tile_z(-1000),
	map_width_ws(500.f)
{}


MiniMap::~MiniMap()
{}


static const float minimap_width = 0.29f; // Width in UI coordinates
static const float margin = 0.03f; // margin around map in UI coordinates

static const int TILE_GRID_RES = 7; // There will be a TILE_GRID_RES x TILE_GRID_RES grid of tiles centered on the camera


static int getTileZForMapWidthWS(float map_width_ws)
{
	/*
	Lets say we want ~= 2 tiles to span the map (tiles are 256 pixels wide):
	map_width_ws = 2 * tile_w
	and
	tile_w = 5120 / (2 ^ tile_z)
	so 
	map_width_ws = 2 * 5120 / (2 ^ tile_z)    [See updateMapTiles() in server.cpp]

	2 ^ tile_z = 2 * 5120 / map_width_ws;
	tile_z = log_2(2 * 5120 / map_width_ws)
	*/

	return myClamp((int)std::log2(2 * 5120 / map_width_ws), 0, 6);
}


// See updateMapTiles() in server.cpp
static float getTileWidthWSForTileZ(int tile_z)
{
	return 5120.f / (1 << tile_z);
}


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
	minimap_image->handler = this;

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

	const int tile_z = getTileZForMapWidthWS(map_width_ws);
	const float tile_w_ws = getTileWidthWSForTileZ(tile_z);

	for(int y=0; y<TILE_GRID_RES; ++y)
	for(int x=0; x<TILE_GRID_RES; ++x)
	{
		Reference<OverlayObject> ob = new OverlayObject();
		ob->mesh_data = opengl_engine->getUnitQuadMeshData();
		ob->material.tex_matrix = Matrix2f(1,0,0,-1);
		ob->material.tex_translation = Vec2f(0, 1);
		ob->ob_to_world_matrix = Matrix4f::translationMatrix(x * tile_w_ws, y * tile_w_ws, 0) *  Matrix4f::scaleMatrix(tile_w_ws, tile_w_ws, 1);

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
		const int tile_z = getTileZForMapWidthWS(map_width_ws);
		const float tile_w_ws = getTileWidthWSForTileZ(tile_z);

		if((campos.getDist(last_requested_campos) > 300.0) || (tile_z != last_requested_tile_z))
		{
			// Send request to server to get map tile image URLS

			// Send QueryObjectsInAABB for initial volume around camera to server
			{
				const int new_centre_x = Maths::floorToInt((float)campos.x / tile_w_ws);
				const int new_centre_y = Maths::floorToInt((float)campos.y / tile_w_ws);

			
				const int query_rad = 4;
				std::vector<Vec3i> query_indices;
				query_indices.reserve(Maths::square(2*query_rad + 1));
			
				for(int x=new_centre_x - query_rad; x <= new_centre_x + query_rad; ++x)
				for(int y=new_centre_y - query_rad; y <= new_centre_y + query_rad; ++y)
				{
					query_indices.push_back(Vec3i(x, y, tile_z));
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
			last_requested_tile_z = tile_z;
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

	const int tile_z = getTileZForMapWidthWS(map_width_ws);
	const float tile_w_ws = getTileWidthWSForTileZ(tile_z);

	const int new_centre_x = Maths::floorToInt((float)campos.x / tile_w_ws);
	const int new_centre_y = Maths::floorToInt((float)campos.y / tile_w_ws);
	
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

				tile.ob->material.albedo_texture = NULL;

				// Look up our tile info for these coordinates
				auto res = tile_infos.find(Vec3i(x, y, tile_z));
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
				// tile.ob->material.albedo_linear_rgb.r = 0.5 + x * 0.15f;
				// tile.ob->material.albedo_linear_rgb.g = 0.5 + y * 0.15f;

				tile.ob->ob_to_world_matrix = Matrix4f::translationMatrix(x * tile_w_ws, y * tile_w_ws, 0) *  Matrix4f::scaleMatrix(tile_w_ws, tile_w_ws, 1);
			}
		}

		last_centre_x = new_centre_x;
		last_centre_y = new_centre_y;
	}

	opengl_engine->setCurrentScene(last_scene); // Restore scene
}


void MiniMap::renderTilesToTexture()
{
	const Vec3d campos = main_window->cam_controller.getFirstPersonPosition();

	Vec3d botleft_pos = campos - Vec3d(map_width_ws/2, map_width_ws/2, 0);

	opengl_engine->setIdentityCameraTransform(); // Since we are just rendering overlays, camera transformation doesn't really matter

	this->scene->overlay_world_to_camera_space_matrix = Matrix4f::scaleMatrix(2.f/map_width_ws, 2.f/map_width_ws, 0) * Matrix4f::translationMatrix(-campos.x, -campos.y, 0);

	opengl_engine->setTargetFrameBufferAndViewport(frame_buffer);

	opengl_engine->draw();
}


void MiniMap::handleMapTilesResultReceivedMessage(const MapTilesResultReceivedMessage& msg)
{
	// conPrint("MiniMap::handleMapTilesResultReceivedMessage");

	runtimeCheck(msg.tile_indices.size() == msg.tile_URLS.size());

	const int tile_z = getTileZForMapWidthWS(map_width_ws);
	const float tile_w_ws = getTileWidthWSForTileZ(tile_z);

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

				TextureParams tex_params;
				tex_params.wrapping = OpenGLTexture::Wrapping_Clamp;
				tex_params.allow_compression = false;
				tex_params.filtering = OpenGLTexture::Filtering_Bilinear;
				tex_params.use_mipmaps = false;

				// Start loading or downloading this tile image (if not already downloaded)
				DownloadingResourceInfo downloading_info;
				downloading_info.texture_params = tex_params;
				downloading_info.pos = tile_pos;
				downloading_info.size_factor = LoadItemQueueItem::sizeFactorForAABBWS(tile_w_ws, /*importance_factor=*/1.f);

				main_window->startDownloadingResource(URL, tile_pos.toVec4fPoint(), tile_w_ws, downloading_info);


				// Start loading the texture (if not already loaded)
				main_window->startLoadingTexture(URL, tile_pos.toVec4fPoint(), tile_w_ws, /*max task dist=*/1.0e10f, /*importance factor=*/1.f, tex_params, /*is_terrain_map=*/false);
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


void MiniMap::mouseWheelEventOccurred(GLUICallbackMouseWheelEvent& event)
{
	//conPrint("MiniMap::mouseWheelEventOccurred, angle_delta_y: " + toString(event.wheel_event->angle_delta_y));
	event.accepted = true;

	const int last_tile_z = getTileZForMapWidthWS(map_width_ws);

	map_width_ws = myClamp(map_width_ws * (1.0f - event.wheel_event->angle_delta_y * 0.002f), 80.f, 10000.f);

	const int new_tile_z = getTileZForMapWidthWS(map_width_ws);
	if(new_tile_z != last_tile_z)
	{
		// Force reassignment of tile textures, now that tile_z changed
		last_centre_x = -10000;
		last_centre_y = -10000;
		checkUpdateTilesForCurCamPosition();
	}
}
