/*=====================================================================
MiniMap.cpp
-----------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "MiniMap.h"


#include "IncludeOpenGL.h"
#include "GUIClient.h"
#include "ClientThread.h"
#include <settings/SettingsStore.h>
#include "../shared/Protocol.h"
#include "../shared/MessageUtils.h"
#include "../shared/ResourceManager.h"
#include "../shared/ImageDecoding.h"
#include <graphics/SRGBUtils.h>
#include <utils/RuntimeCheck.h>
#include <tracy/Tracy.hpp>


static const float x_margin = 0.03f; // margin around map in UI coordinates

static const float arrow_width_px = 22;

static const float clip_border_width_px = 6;

static const int TILE_GRID_RES = 5; // There will be a TILE_GRID_RES x TILE_GRID_RES grid of tiles centered on the camera

// -1 is near clip plane, +1 is the far clip plane.
static const float MINIMAP_Z = 0.3f;
static const float MINIMAP_TILE_Z              = MINIMAP_Z;
static const float BACKGROUND_IMAGE_Z          = MINIMAP_Z + 0.01f;
static const float ARROW_IMAGE_Z               = MINIMAP_Z - 0.03f; // -0.95f;
static const float AVATAR_MARKER_IMAGE_Z       = MINIMAP_Z - 0.01f; // -0.93f;
static const float AVATAR_MARKER_ARROW_IMAGE_Z = MINIMAP_Z - 0.02f; //-0.94f; // Slightly behind the marker dot


MiniMap::MiniMap(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_)
:	gui_client(NULL),
	last_requested_campos(Vec3d(-1000000)),
	last_requested_tile_z(-1000),
	map_width_ws(500.f),
	scratch_packet(SocketBufferOutStream::DontUseNetworkByteOrder)
{
	opengl_engine = opengl_engine_;
	gui_client = gui_client_;
	gl_ui = gl_ui_;

#if EMSCRIPTEN
	const bool default_minimap_expanded = false; // On small mobile screens, make collapsed by default.
#else
	const bool default_minimap_expanded = true;
#endif
	expanded = gui_client_->getSettingsStore()->getBoolValue("setting/show_minimap", /*default_value=*/default_minimap_expanded);
	
	// Create minimap (background) image.  Will be mostly covered by tiles, but keep for the sizing logic and mouse handler.
	const float y_margin = computeMiniMapTopMargin();
	const float minimap_width = computeMiniMapWidth();
	minimap_image = new GLUIImage(*gl_ui, opengl_engine, "", Vec2f(1 - x_margin - minimap_width, gl_ui->getViewportMinMaxY() - y_margin - minimap_width), Vec2f(minimap_width), /*tooltip=*/"", BACKGROUND_IMAGE_Z);
	minimap_image->setColour(Colour3f(0.1f));
	minimap_image->setAlpha(0.8f);
	minimap_image->setMouseOverColour(Colour3f(0.1f));
	minimap_image->handler = this;
	gl_ui->addWidget(minimap_image);

	// Create facing arrow image
	arrow_image = new GLUIImage(*gl_ui, opengl_engine, gui_client->resources_dir_path + "/facing_arrow.png", Vec2f(1 - x_margin - minimap_width/2, gl_ui->getViewportMinMaxY() - y_margin - minimap_width/2), Vec2f(0.005f), 
		/*tooltip=*/"You", ARROW_IMAGE_Z);
	arrow_image->overlay_ob->material.tex_matrix = Matrix2f::identity(); // Since we are using a texture rendered in OpenGL we don't need to flip it.
	arrow_image->handler = this;
	gl_ui->addWidget(arrow_image);

	
	{
		GLUIButton::CreateArgs args;
		args.tooltip = "Hide minimap";
		//args.button_colour = Colour3f(0.2f);
		//args.mouseover_button_colour = Colour3f(0.4f);
		collapse_button = new GLUIButton(*gl_ui, opengl_engine, gui_client->resources_dir_path + "/buttons/right_tab.png", /*botleft=*/Vec2f(10.f), /*dims=*/Vec2f(0.1f), args);
		collapse_button->handler = this;
		gl_ui->addWidget(collapse_button);
	}

	{
		GLUIButton::CreateArgs args;
		args.tooltip = "Show minimap";
		expand_button = new GLUIButton(*gl_ui, opengl_engine, gui_client->resources_dir_path + "/buttons/minimap_icon.png", /*botleft=*/Vec2f(10.f), /*dims=*/Vec2f(0.1f), args);
		expand_button->handler = this;
		expand_button->setVisible(false);
		gl_ui->addWidget(expand_button);
	}


	{
		ImageMapUInt8Ref default_detail_col_map = new ImageMapUInt8(1, 1, 4);
		default_detail_col_map->getPixel(0, 0)[0] = 150;
		default_detail_col_map->getPixel(0, 0)[1] = 150;
		default_detail_col_map->getPixel(0, 0)[2] = 150;
		default_detail_col_map->getPixel(0, 0)[3] = 255;
		tile_placeholder_tex = opengl_engine->getOrLoadOpenGLTextureForMap2D(OpenGLTextureKey("__tile_placeholder_tex__"), *default_detail_col_map);
	}


	tiles.resize(TILE_GRID_RES, TILE_GRID_RES);
	last_centre_x = -1000000;
	last_centre_y = -1000000;


	for(int y=0; y<TILE_GRID_RES; ++y)
	for(int x=0; x<TILE_GRID_RES; ++x)
	{
		Reference<OverlayObject> ob = new OverlayObject();
		ob->mesh_data = opengl_engine->getUnitQuadMeshData();
		ob->material.albedo_texture = tile_placeholder_tex;
		ob->material.tex_matrix = Matrix2f(1,0,0,-1);
		ob->material.tex_translation = Vec2f(0, 1);
		ob->ob_to_world_matrix = Matrix4f::identity();

		opengl_engine->addOverlayObject(ob);

		tiles.elem(x, y).ob = ob;
	}


	updateWidgetPositions();
	setWidgetVisibilityForExpanded();
}


MiniMap::~MiniMap()
{
	if(gl_ui.nonNull())
	{
		// Remove any existing avatar markers
		if(gui_client->world_state.nonNull())
		{
			Lock lock(gui_client->world_state->mutex);
			for(auto it = gui_client->world_state->avatars.begin(); it != gui_client->world_state->avatars.end(); ++it)
				removeMarkerForAvatar(it->second.ptr());
		}

		tile_placeholder_tex = NULL;

		for(int y=0; y<TILE_GRID_RES; ++y)
		for(int x=0; x<TILE_GRID_RES; ++x)
		{
			opengl_engine->removeOverlayObject(tiles.elem(x, y).ob);
			tiles.elem(x, y).ob = NULL;
		}
		tiles.resize(0, 0);

		if(minimap_image.nonNull())
		{
			gl_ui->removeWidget(minimap_image);
			minimap_image = NULL;
		}
		
		if(arrow_image.nonNull())
		{
			gl_ui->removeWidget(arrow_image);
			arrow_image = NULL;
		}

		if(collapse_button.nonNull())
		{
			gl_ui->removeWidget(collapse_button);
			collapse_button = NULL;
		}

		if(expand_button.nonNull())
		{
			gl_ui->removeWidget(expand_button);
			expand_button = NULL;
		}
	}

	gl_ui = NULL;
	opengl_engine = NULL;
}


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


void MiniMap::setVisible(bool visible)
{
	this->expand_button->setVisible(visible && !expanded);
	this->collapse_button->setVisible(visible && expanded);

	setMapAndMarkersVisible(visible && expanded);
}


void MiniMap::setMapAndMarkersVisible(bool visible)
{
	if(gl_ui.nonNull())
	{
		minimap_image->setVisible(visible);

		for(int j=0; j<TILE_GRID_RES; ++j)
		for(int i=0; i<TILE_GRID_RES; ++i)
		{
			MapTile& tile = tiles.elem(i, j);
			tile.ob->draw = visible;
		}

		arrow_image->setVisible(visible);

		// Set visibility of avatar markers
		if(gui_client->world_state.nonNull())
		{
			Lock lock(gui_client->world_state->mutex);
			for(auto it = gui_client->world_state->avatars.begin(); it != gui_client->world_state->avatars.end(); ++it)
			{
				if(it->second->minimap_marker.nonNull())
					it->second->minimap_marker->setVisible(visible);

				if(it->second->minimap_marker_arrow.nonNull())
					it->second->minimap_marker_arrow->setVisible(visible);
			}
		}
	}
}


void MiniMap::think()
{
	ZoneScoped; // Tracy profiler

	if(gl_ui.isNull())
		return;

	if(minimap_image.nonNull() && !minimap_image->isVisible()) // If map is hidden, don't do any work.
		return;

	const Vec3d campos = gui_client->cam_controller.getFirstPersonPosition();

	if((gui_client->connection_state == GUIClient::ServerConnectionState_Connected) && (gui_client->server_protocol_version >= 39)) // QueryMapTiles message was introduced in protocol version 39.
	{
		const int tile_z = getTileZForMapWidthWS(map_width_ws);
		const float tile_w_ws = getTileWidthWSForTileZ(tile_z);

		if((campos.getDist(last_requested_campos) > 300.0) || (tile_z != last_requested_tile_z))
		{
			// Send request to server to get map tile image URLs, for any nearby tiles that we have not already done a request to the server for.
			{
				const int new_centre_x = Maths::floorToInt((float)campos.x / tile_w_ws);
				const int new_centre_y = Maths::floorToInt((float)campos.y / tile_w_ws);

				const int query_rad = 2; // Query 'radius'
				std::vector<Vec3i> query_indices;
				query_indices.reserve(Maths::square(2*query_rad + 1) * 2);
			
				for(int x=new_centre_x - query_rad; x <= new_centre_x + query_rad; ++x)
				for(int y=new_centre_y - query_rad; y <= new_centre_y + query_rad; ++y)
				{
					Vec3i tile_coords = Vec3i(x, y, tile_z);
					
					// Walk out zoom levels and query those tiles as well, in case we don't have a tile at this level and we need to use the zoomed-out tile
					while(tile_coords.z >= 0)
					{
						if(queried_tile_coords.count(tile_coords) == 0) // If we haven't queried this tile yet:
						{
							query_indices.push_back(tile_coords);
							queried_tile_coords.insert(tile_coords); // Mark tile as queried
						}

						tile_coords.x = Maths::divideByTwoRoundedDown(tile_coords.x);
						tile_coords.y = Maths::divideByTwoRoundedDown(tile_coords.y);
						tile_coords.z--;
					}
				}

				if(!query_indices.empty()) // If we actually have some tile coords we haven't queried yet:
				{
					// Make QueryMapTiles packet and enqueue to send
					MessageUtils::initPacket(scratch_packet, Protocol::QueryMapTiles);
			
					scratch_packet.writeUInt32((uint32)query_indices.size());
					scratch_packet.writeData(query_indices.data(), query_indices.size() * sizeof(Vec3i));

					MessageUtils::updatePacketLengthField(scratch_packet);
					gui_client->client_thread->enqueueDataToSend(scratch_packet.buf);
				}
			}

			last_requested_campos = campos;
			last_requested_tile_z = tile_z;
		}
	}


	checkUpdateTilesForCurCamPosition();


	// Set transforms of tile overlay objects
	setTileOverlayObjectTransforms();

	
	const float heading = (float)gui_client->cam_controller.getAngles().x;

	const float arrow_width = gl_ui->getUIWidthForDevIndepPixelWidth(arrow_width_px);
	const float minimap_width = computeMiniMapWidth();
	const float y_margin = computeMiniMapTopMargin();
	arrow_image->setTransform(Vec2f(1 - x_margin - minimap_width/2 - arrow_width/2, gl_ui->getViewportMinMaxY() - y_margin - minimap_width/2 - arrow_width/2), 
		/*dims=*/Vec2f(arrow_width), 
		/*rotation=*/heading,
		/*z=*/ARROW_IMAGE_Z);
}


// Set transforms of tile overlay objects
void MiniMap::setTileOverlayObjectTransforms()
{
	// The logic of the transform is something like:
	// Create tiles in world space (let the map == the territory), transform them into camera space for a camera looking down at the player position,
	// then translate and scale them to the minimap UI rectangle.

	const Vec3d campos = gui_client->cam_controller.getFirstPersonPosition();

	const Rect2f minimap_gl_rect = gl_ui->OpenGLRectCoordsForUICoords(minimap_image->rect);
	Matrix4f world_to_overlay_space_matrix = Matrix4f::translationMatrix(minimap_gl_rect.getMin().x, minimap_gl_rect.getMax().y, MINIMAP_TILE_Z) * 
		Matrix4f::scaleMatrix(minimap_gl_rect.getWidths().x * 1.f/map_width_ws, minimap_gl_rect.getWidths().y * 1.f/map_width_ws, 1) * 
		Matrix4f::translationMatrix((float)-campos.x + map_width_ws/2.f, (float)-campos.y - map_width_ws/2.f, 0);

	const int x0     = last_centre_x  - TILE_GRID_RES/2; // unwrapped grid x coordinate of lower left grid cell in square grid around new camera position
	const int y0     = last_centre_y  - TILE_GRID_RES/2;
	const int wrapped_x0     = Maths::intMod(x0,     TILE_GRID_RES); // x0 mod TILE_GRID_RES                        [Using euclidean modulo]
	const int wrapped_y0     = Maths::intMod(y0,     TILE_GRID_RES); // y0 mod TILE_GRID_RES                        [Using euclidean modulo]

	const int tile_z = getTileZForMapWidthWS(map_width_ws);
	const float tile_w_ws = getTileWidthWSForTileZ(tile_z);

	for(int j=0; j<TILE_GRID_RES; ++j)
	for(int i=0; i<TILE_GRID_RES; ++i)
	{
		// Get unwrapped coords
		const int x = x0 + i - wrapped_x0 + ((i >= wrapped_x0) ? 0 : TILE_GRID_RES);
		const int y = y0 + j - wrapped_y0 + ((j >= wrapped_y0) ? 0 : TILE_GRID_RES);

		tiles.elem(i, j).ob->ob_to_world_matrix = world_to_overlay_space_matrix * ::translationMulScaleMatrix(Vec4f(x * tile_w_ws, y * tile_w_ws, 0, 0), tile_w_ws, tile_w_ws, 1);
	}
}


#define VISUALISE_TILES 0
#if VISUALISE_TILES
static inline uint32_t uint32Hash(uint32_t a)
{
	a = (a ^ 61) ^ (a >> 16);
	a = a + (a << 3);
	a = a ^ (a >> 4);
	a = a * 0x27d4eb2d;
	a = a ^ (a >> 15);
	return a;
}
#endif


void MiniMap::checkUpdateTilesForCurCamPosition()
{
	if(!opengl_engine)
		return;

	const Vec3d campos = gui_client->cam_controller.getFirstPersonPosition();

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

				tile.ob->material.albedo_texture = tile_placeholder_tex;

				// Look up our tile info for these coordinates.
				// If we don't have a tile image for these coordinates, zoom out by decreasing tile z, until we find a tile we do actually have, then use that.
				// See diagrams below.
				bool found_image = false;
				int tile_x = x;
				int tile_y = y;
				Vec2f lower_left_coords(0.f);
				float scale = 1;
				for(int z = tile_z; z >= 0 && !found_image; --z)
				{
					auto res = tile_infos.find(Vec3i(tile_x, tile_y, z));
					if(res != tile_infos.end())
					{
						const MapTileInfo& info = res->second;
						if(!info.image_URL.empty())
						{
							// conPrint("Found tile (" + toString(tile_x) + ", " + toString(tile_y) + ", " + toString(z) + "), lower_left_coords: " + lower_left_coords.toString());

							ResourceRef resource = gui_client->resource_manager->getExistingResourceForURL(info.image_URL);
							if(resource.nonNull())
							{
								tile.ob->material.tex_matrix = Matrix2f(scale, 0, 0, -scale);  // See diagrams below for explanation
								tile.ob->material.tex_translation = Vec2f(lower_left_coords.x, 1 - lower_left_coords.y);

								const OpenGLTextureKey local_path = OpenGLTextureKey(gui_client->resource_manager->getLocalAbsPathForResource(*resource));

								OpenGLTextureRef tile_tex = opengl_engine->getTextureIfLoaded(local_path);
								if(tile_tex)
								{
									tile.ob->material.albedo_texture = tile_tex;
								}
								else
								{
									// If tile_tex is null, keep placeholder texture assigned to tile object for now.
									// Start loading the texture (may never have been loaded, or may have been unloaded if wasn't used for a while)

									const Vec3d tile_pos(0.0); // TEMP HACK could use tile position in world space? or cam position?

									TextureParams tex_params;
									tex_params.wrapping = OpenGLTexture::Wrapping_Clamp;
									tex_params.allow_compression = false;
									tex_params.filtering = OpenGLTexture::Filtering_Bilinear;
									tex_params.use_mipmaps = false;

									if(resource->getState() == Resource::State_Present)
										gui_client->startLoadingTextureForLocalPath(local_path, resource, tile_pos.toVec4fPoint(), tile_w_ws, /*max task dist=*/1.0e10f, /*importance factor=*/1.f, tex_params);

									loading_texture_URL_to_tile_indices_map[info.image_URL] = Vec3i(tile_x, tile_y, z);
								}
							}

							found_image = true; // We have found a tile image, break loop
						}
					}
					
					if(!found_image)
					{
						// conPrint("tile (" + toString(tile_x) + ", " + toString(tile_y) + ", " + toString(z) + ") not found");

						/*
						-----------------------------
						|             |             |
						|             |             |
						|             |             |
						|   (4, 5, 2) |   (5, 5, 2) |
						|             |             |
						|             |             |
						-----------------------------
						|             |             |
						|             |             |
						|   (4, 4, 2) |   (5, 4, 2) |
						|             |             |
						|             |             |
						|             |             |
						-----------------------------
						           (2, 2, 1)
						
						Suppose we don't have a tile for x=4, y=4 and z=2.
						Then instead we can use the next level zoomed out tile, i.e. x=2, y=2, z=1
						We need to adjust tex coords so it shows the expected texture.


						          (0, 1, 1)                    (1, 1, 1)
						---------------------------------------------------------
						|             |             |             |             |
						|             |             |             |             |
						|             |             |             |             |
						|   (0, 3, 2) |   (1, 3, 2) |   (2, 3, 2) |   (3, 3, 2) |
						|             |             |     X       |             |
						|             |             |             |             |
						---------------------------------------------------------
						|             |             |             |             |
						|             |             |             |             |
						|   (0, 2, 2) |   (1, 2, 2) |   (2, 2, 2) |   (3, 2, 2) |
						|             |             |             |             |
						|             |             |             |             |
						|             |             |             |             |
						---------------------------------------------------------   (0, 0, 0)
						|             |             |             |             |
						|             |             |             |             |
						|             |             |             |             |
						|   (0, 1, 2) |   (1, 1, 2) |   (2, 1, 2) |   (3, 1, 2) |
						|             |             |             |             |
						|             |             |             |             |
						---------------------------------------------------------
						|             |             |             |             |
						|             |             |             |             |
						|   (0, 0, 2) |   (1, 0, 2) |   (2, 0, 2) |   (3, 0, 2) |
						|             |             |             |             |
						|             |             |             |             |
						|             |             |             |             |
						----------------------------------------------------------
						           (0, 0, 1)                   (1,0 1)

						Take X square (2, 3, 2)
						We want lower_left_coords = (0.5, 0.75), scale = 1/4
						And final tex matrix (considering we need to flip texture upside down due to opengl texture loading)
						(1/4    0)
						(0   -1/4)
						and translation (0.5, 1 - 0.75) = (0.5, 0.25)

						examples:  
						(u, v) = (0, 0):
						tex coords = (0.5, 0.25)
						(u, v) = (1, 1):
						tex coords = (1/4, -1/4) + (0.5, 0.25) = (0.75, 0)
						*/
						lower_left_coords *= 0.5f;

						const int x_mod_2 = Maths::intMod(tile_x, 2);
						const int y_mod_2 = Maths::intMod(tile_y, 2);
						lower_left_coords.x += (float)x_mod_2 * 0.5f;
						lower_left_coords.y += (float)y_mod_2 * 0.5f;

						tile_x = Maths::divideByTwoRoundedDown(tile_x);
						tile_y = Maths::divideByTwoRoundedDown(tile_y);
						scale *= 0.5f;
					}
				}

#if VISUALISE_TILES
				// Visualise individual tiles by colouring them differently
				tile.ob->material.albedo_linear_rgb.r = 0.3f + 0.7f * uint32Hash(x)    * (1.f / 4294967296.f);
				tile.ob->material.albedo_linear_rgb.g = 0.3f + 0.7f * uint32Hash(y)    * (1.f / 4294967296.f);
				tile.ob->material.albedo_linear_rgb.b = 0.3f + 0.7f * uint32Hash(x+10) * (1.f / 4294967296.f);
#endif

				tile.ob->ob_to_world_matrix = Matrix4f::identity(); // Will be set later in setTileOverlayObjectTransforms().
			}
		}

		last_centre_x = new_centre_x;
		last_centre_y = new_centre_y;
	}
}


void MiniMap::handleMapTilesResultReceivedMessage(const MapTilesResultReceivedMessage& msg)
{
	if(gl_ui.isNull())
		return;

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
			const URLString& URL = msg.tile_URLS[i];
			MapTileInfo info;
			info.image_URL = URL;
			tile_infos[indices] = info;

			if(!URL.empty() && ImageDecoding::hasSupportedImageExtension(URL))
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
				downloading_info.used_by_other = true;

				// conPrint("Starting to download screenshot '" + URL + "'...");
				gui_client->startDownloadingResource(URL, tile_pos.toVec4fPoint(), tile_w_ws, downloading_info);


				// Start loading the texture (if not already loaded)
				gui_client->startLoadingTextureIfPresent(URL, tile_pos.toVec4fPoint(), tile_w_ws, /*max task dist=*/1.0e10f, /*importance factor=*/1.f, tex_params);

				loading_texture_URL_to_tile_indices_map[URL] = indices;
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

	setTileOverlayObjectTransforms();
}


void MiniMap::handleUploadedTexture(const OpenGLTextureKey& path, const URLString& URL, const OpenGLTextureRef& opengl_tex)
{
	ZoneScoped; // Tracy profiler

	// conPrint("MiniMap::handleUploadedTexture: " + path);

	auto res = loading_texture_URL_to_tile_indices_map.find(URL);
	if(res != loading_texture_URL_to_tile_indices_map.end())
	{
		const Vec3i indices = res->second;

		const int x0     = last_centre_x  - TILE_GRID_RES/2; // unwrapped grid x coordinate of lower left grid cell in square grid around new camera position
		const int y0     = last_centre_y  - TILE_GRID_RES/2;
		const int wrapped_x0     = Maths::intMod(x0,     TILE_GRID_RES); // x0 mod TILE_GRID_RES                        [Using euclidean modulo]
		const int wrapped_y0     = Maths::intMod(y0,     TILE_GRID_RES); // y0 mod TILE_GRID_RES                        [Using euclidean modulo]
	
		const int tile_z = getTileZForMapWidthWS(map_width_ws);

		// Iterate over wrapped coordinates
		MapTile* const tiles_data = tiles.getData();
		for(int j=0; j<TILE_GRID_RES; ++j)
		for(int i=0; i<TILE_GRID_RES; ++i)
		{
			MapTile& tile = tiles_data[i + j * TILE_GRID_RES];

			// Get unwrapped coords
			const int x = x0 + i - wrapped_x0 + ((i >= wrapped_x0) ? 0 : TILE_GRID_RES);
			const int y = y0 + j - wrapped_y0 + ((j >= wrapped_y0) ? 0 : TILE_GRID_RES);
			assert(x >= x0 && x < x0 + TILE_GRID_RES);
			assert(y >= y0 && y < y0 + TILE_GRID_RES);

			const Vec3i tile_indices(x, y, tile_z);
			if((tile_indices == indices) && tile.ob)
				tile.ob->material.albedo_texture = opengl_tex;
		}
	}
}


void MiniMap::viewportResized(int w, int h)
{
	if(gl_ui)
		updateWidgetPositions();
}


static const float button_spacing_px = 10;
static const float button_w_px = 20;
static const float button_h_px = 50;


void MiniMap::handleMouseMoved(MouseEvent& mouse_event)
{
	if(gl_ui.isNull())
		return;

	const Vec2f coords = gl_ui->UICoordsForOpenGLCoords(mouse_event.gl_coords);

	if(expanded)
	{
		const float extra_mouse_over_px = 10;
		const float to_button_left_w = gl_ui->getUIWidthForDevIndepPixelWidth(button_spacing_px + button_w_px + extra_mouse_over_px);

		const Vec2f last_minimap_bot_left_pos = minimap_image->rect.getMin();
		const bool mouse_over = 
			coords.x > (last_minimap_bot_left_pos.x - to_button_left_w) && 
			coords.y > (last_minimap_bot_left_pos.y - gl_ui->getUIWidthForDevIndepPixelWidth(extra_mouse_over_px));

		collapse_button->setVisible(mouse_over);
	}
}


void MiniMap::updateWidgetPositions()
{
	if(gl_ui.nonNull())
	{
		if(minimap_image)
		{
			const float minimap_width = computeMiniMapWidth();
			const float y_margin = computeMiniMapTopMargin();
			minimap_image->setPosAndDims(Vec2f(1 - minimap_width - x_margin, gl_ui->getViewportMinMaxY() - minimap_width - y_margin), Vec2f(minimap_width), BACKGROUND_IMAGE_Z);

			//last_minimap_bot_left_pos = minimap_image->rect.getMin();

			Vec2f minimap_botleft = minimap_image->getPos();
			//---------------------------- Update collapse_button ----------------------------
			const float button_w = gl_ui->getUIWidthForDevIndepPixelWidth(button_w_px);
			const float button_h = gl_ui->getUIWidthForDevIndepPixelWidth(button_h_px);
			collapse_button->setPosAndDims(Vec2f(minimap_botleft.x - gl_ui->getUIWidthForDevIndepPixelWidth(button_w_px + button_spacing_px), minimap_botleft.y), Vec2f(button_w, button_h));

			//---------------------------- Update expand_button ----------------------------
			const float expand_button_w_px = 36;
			const float expand_button_w = gl_ui->getUIWidthForDevIndepPixelWidth(expand_button_w_px);
			const float margin_px = 18;
			expand_button->setPosAndDims(Vec2f(1.f - gl_ui->getUIWidthForDevIndepPixelWidth(margin_px + expand_button_w_px), gl_ui->getViewportMinMaxY() - gl_ui->getUIWidthForDevIndepPixelWidth(margin_px + expand_button_w_px)), 
				Vec2f(expand_button_w, expand_button_w));

			//---------------------------- Update tile clip regions ----------------------------
			const float clip_border_width = gl_ui->getUIWidthForDevIndepPixelWidth(clip_border_width_px);
			for(int y=0; y<TILE_GRID_RES; ++y)
			for(int x=0; x<TILE_GRID_RES; ++x)
				tiles.elem(x, y).ob->clip_region = gl_ui->OpenGLRectCoordsForUICoords(Rect2f(minimap_image->rect.getMin() + Vec2f(clip_border_width), minimap_image->rect.getMax() - Vec2f(clip_border_width)));
		}
	}
}


Vec2f MiniMap::mapUICoordsForWorldSpacePos(const Vec3d& pos)
{
	const Vec3d cam_to_pos_ws = pos - gui_client->cam_controller.getFirstPersonPosition();

	const Vec2f cam_to_pos_xy_ws((float)cam_to_pos_ws.x, (float)cam_to_pos_ws.y);

	const float minimap_width = computeMiniMapWidth();
	const Vec2f map_centre_ui(1 - x_margin - minimap_width/2,  gl_ui->getViewportMinMaxY() - computeMiniMapTopMargin() - minimap_width/2);

	return map_centre_ui + minimap_width * cam_to_pos_xy_ws / map_width_ws;
}


float MiniMap::computeMiniMapWidth()
{
	return myClamp(gl_ui->getUIWidthForDevIndepPixelWidth(250), 0.f, 1.f);
}


float MiniMap::computeMiniMapTopMargin()
{
	if(computeMiniMapWidth() > 0.8f) // If minimap extends sufficiently far across screen (will be case for narrow screens such as on phones)
		return gl_ui->getUIWidthForDevIndepPixelWidth(60); // lower map so does not cover login/signup buttons.
	else
		return x_margin;
}


void MiniMap::updateMarkerForAvatar(Avatar* avatar, const Vec3d& avatar_pos)
{
	if(gl_ui.isNull())
		return;

	if(minimap_image && !minimap_image->isVisible()) // If map is hidden, don't do any work.
		return;


	const Vec2f ui_coords = mapUICoordsForWorldSpacePos(avatar_pos);

	// Clamp marker to screen.
	const float in_map_margin_w = 0.02f;
	const float minimap_width = computeMiniMapWidth();
	const float map_min_x = 1 - x_margin - minimap_width + in_map_margin_w;
	const float map_max_x = 1 - x_margin - in_map_margin_w;
	const float map_min_y = gl_ui->getViewportMinMaxY() - computeMiniMapTopMargin() - minimap_width + in_map_margin_w;
	const float map_max_y = gl_ui->getViewportMinMaxY() - computeMiniMapTopMargin() - in_map_margin_w;

	Vec2f clamped_ui_coords;
	clamped_ui_coords.x = myClamp(ui_coords.x, map_min_x, map_max_x);
	clamped_ui_coords.y = myClamp(ui_coords.y, map_min_y, map_max_y);

	// Work out arrow pos and rotation
	Vec2f arrow_pos(-1000.f); // By default arrow is off-screen
	float arrow_rotation = 0; // Rotation of right-pointing arrow image.
	if(clamped_ui_coords.x > ui_coords.x) // If marker was clamped to left side of screen:
	{
		arrow_pos = clamped_ui_coords - Vec2f(in_map_margin_w * 0.5f, 0); // Just to left of marker
		arrow_rotation = Maths::pi<float>();
	}
	else if(clamped_ui_coords.x < ui_coords.x) // If was clamped to right side of screen:
	{
		arrow_pos = clamped_ui_coords + Vec2f(in_map_margin_w * 0.5f, 0);
	}

	if(clamped_ui_coords.y > ui_coords.y) // If was clamped to bottom side of screen:
	{
		arrow_pos = clamped_ui_coords - Vec2f(0, in_map_margin_w * 0.5f);
		arrow_rotation = -Maths::pi<float>() / 2;
	}
	else if(clamped_ui_coords.y < ui_coords.y) // If was clamped to top side of screen:
	{
		arrow_pos = clamped_ui_coords + Vec2f(0, in_map_margin_w * 0.5f);
		arrow_rotation = Maths::pi<float>() / 2;
	}

	// Handle case where marker ends up in corner of screen, in this case we want the arrow to be diagonally pointed.
	const float edge_move_x = in_map_margin_w * 0.4f;
	if(clamped_ui_coords.x > ui_coords.x && clamped_ui_coords.y < ui_coords.y) // If was clamped to left side and top of screen:
	{
		arrow_pos = clamped_ui_coords + Vec2f(-edge_move_x, edge_move_x);
		arrow_rotation = Maths::pi<float>() * (3 / 4.f);
	}
	else if(clamped_ui_coords.x > ui_coords.x && clamped_ui_coords.y > ui_coords.y) // If was clamped to left side and bottom of screen:
	{
		arrow_pos = clamped_ui_coords + Vec2f(-edge_move_x, -edge_move_x);
		arrow_rotation = Maths::pi<float>() * (5 / 4.f);
	}
	else if(clamped_ui_coords.x < ui_coords.x && clamped_ui_coords.y > ui_coords.y) // If was clamped to right side and bottom of screen:
	{
		arrow_pos = clamped_ui_coords + Vec2f(edge_move_x, -edge_move_x);
		arrow_rotation = -Maths::pi<float>() * (1 / 4.f);
	}
	else if(clamped_ui_coords.x < ui_coords.x && clamped_ui_coords.y < ui_coords.y) // If was clamped to right side and top of screen:
	{
		arrow_pos = clamped_ui_coords + Vec2f(edge_move_x, edge_move_x);
		arrow_rotation = Maths::pi<float>() * (1 / 4.f);
	}

	/*const bool on_map = 
		(ui_coords.x >= 1 - margin - minimap_width) && 
		(ui_coords.y >= gl_ui->getViewportMinMaxY(opengl_engine) - margin - minimap_width) && 
		(ui_coords.x < 1 - margin) && 
		(ui_coords.y < gl_ui->getViewportMinMaxY(opengl_engine) - margin);*/

	// Create (if needed) and/or position marker dot.
	const float im_width = gl_ui->getUIWidthForDevIndepPixelWidth(8);
	const Vec2f dot_corner_pos = clamped_ui_coords - Vec2f(im_width/2);
	if(avatar->minimap_marker.isNull()) // If marker does not exist yet:
	{
		// Create marker dot
		GLUIImageRef im = new GLUIImage(*gl_ui, opengl_engine, gui_client->resources_dir_path + "/dot.png", dot_corner_pos, Vec2f(im_width), /*tooltip=*/avatar->name);
		im->setColour(toLinearSRGB(Colour3f(5,0,0))); // Glowing red colour
		im->setMouseOverColour(toLinearSRGB(Colour3f(5))); // Glowing white

		gl_ui->addWidget(im);
		avatar->minimap_marker = im;
	}
	else
		avatar->minimap_marker->setPosAndDims(dot_corner_pos, Vec2f(im_width), /*z=*/AVATAR_MARKER_IMAGE_Z);

	// Create (if needed) and/or position marker arrow.
	const float arrow_im_width = gl_ui->getUIWidthForDevIndepPixelWidth(14);
	const Vec2f arrow_corner_pos = arrow_pos - Vec2f(arrow_im_width/2);
	if(avatar->minimap_marker_arrow.isNull()) // If marker arrow does not exist yet:
	{
		// Create marker arrow
		GLUIImageRef im = new GLUIImage(*gl_ui, opengl_engine, gui_client->resources_dir_path + "/arrow.png", arrow_corner_pos, Vec2f(arrow_im_width), /*tooltip=*/avatar->name);
		im->setColour(toLinearSRGB(Colour3f(5,0,0))); // Glowing red colour
		im->setMouseOverColour(toLinearSRGB(Colour3f(5))); // Glowing white
			
		gl_ui->addWidget(im);
		avatar->minimap_marker_arrow = im;
	}
	else
		avatar->minimap_marker_arrow->setTransform(arrow_corner_pos, Vec2f(arrow_im_width), arrow_rotation, /*z=*/AVATAR_MARKER_ARROW_IMAGE_Z);

	//avatar->minimap_marker->setVisible(on_map);
}


void MiniMap::removeMarkerForAvatar(Avatar* avatar)
{
	if(gl_ui.isNull())
		return;

	if(avatar->minimap_marker.nonNull())
	{
		gl_ui->removeWidget(avatar->minimap_marker);
		avatar->minimap_marker = NULL;
	}

	if(avatar->minimap_marker_arrow.nonNull())
	{
		gl_ui->removeWidget(avatar->minimap_marker_arrow);
		avatar->minimap_marker_arrow = NULL;
	}
}


//bool MiniMap::doHandleMouseMoved(const Vec2f& coords) override
//{
//	conPrint("MiniMap::doHandleMouseMoved");
//}


void MiniMap::setWidgetVisibilityForExpanded()
{
	setMapAndMarkersVisible(expanded);

	collapse_button->setVisible(expanded);
	expand_button->setVisible(!expanded);
}


void MiniMap::eventOccurred(GLUICallbackEvent& event)
{
	if(event.widget == this->collapse_button.ptr())
	{
		assert(expanded);
		expanded = false;
	}
	else if(event.widget == this->expand_button.ptr())
	{
		assert(!expanded);
		expanded = true;
	}

	setWidgetVisibilityForExpanded();

	gui_client->getSettingsStore()->setBoolValue("setting/show_minimap", expanded);
}


void MiniMap::mouseWheelEventOccurred(GLUICallbackMouseWheelEvent& event)
{
	if(gl_ui.isNull())
		return;

	if(minimap_image && !minimap_image->isVisible()) // If map is hidden, don't do any work.
		return;


	//conPrint("MiniMap::mouseWheelEventOccurred, angle_delta_y: " + toString(event.wheel_event->angle_delta_y));
	event.accepted = true;

	const int last_tile_z = getTileZForMapWidthWS(map_width_ws);

	map_width_ws = myClamp(map_width_ws * (1.0f - event.wheel_event->angle_delta.y * 0.016f), 80.f, 10000.f);

	const int new_tile_z = getTileZForMapWidthWS(map_width_ws);
	if(new_tile_z != last_tile_z)
	{
		// Force reassignment of tile textures, now that tile_z changed
		last_centre_x = -10000;
		last_centre_y = -10000;
		checkUpdateTilesForCurCamPosition();

		setTileOverlayObjectTransforms();
	}
}
