/*=====================================================================
MiniMap.h
---------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "../shared/URLString.h"
#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUIButton.h>
#include <opengl/ui/GLUICallbackHandler.h>
#include <opengl/ui/GLUITextView.h>
#include <opengl/ui/GLUIImage.h>
#include <maths/vec3.h>
#include <utils/Array2D.h>
#include <utils/SocketBufferOutStream.h>
#include <map>


class GUIClient;
class Avatar;
class OpenGLScene;
class MapTilesResultReceivedMessage;


struct MapTile
{
	Reference<OverlayObject> ob;
};


struct MapTileInfo
{
	URLString image_URL;
};


/*=====================================================================
MiniMap
-------

=====================================================================*/
class MiniMap : public GLUICallbackHandler, public ThreadSafeRefCounted
{
public:
	MiniMap(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_);
	~MiniMap();

	void setVisible(bool visible); // Set expand, collapse button visibility, plus call setMapAndMarkersVisible().
	void setMapAndMarkersVisible(bool visible);

	void think();

	void viewportResized(int w, int h);

	void updateMarkerForAvatar(Avatar* avatar, const Vec3d& avatar_pos);
	void removeMarkerForAvatar(Avatar* avatar);

	void handleMouseMoved(MouseEvent& mouse_event);

	//virtual bool doHandleMouseMoved(const Vec2f& coords) override;

	// GLUICallbackHandler interface:
	void eventOccurred(GLUICallbackEvent& event) override;
	void mouseWheelEventOccurred(GLUICallbackMouseWheelEvent& event) override;

	void handleMapTilesResultReceivedMessage(const MapTilesResultReceivedMessage& msg);

	void handleUploadedTexture(const OpenGLTextureKey& path, const URLString& URL, const OpenGLTextureRef& opengl_tex);
private:
	void setWidgetVisibilityForExpanded();
	void checkUpdateTilesForCurCamPosition();
	void updateWidgetPositions();
	void setTileOverlayObjectTransforms();
	Vec2f mapUICoordsForWorldSpacePos(const Vec3d& pos);
	float computeMiniMapWidth();
	float computeMiniMapTopMargin();

	GUIClient* gui_client;
	GLUIRef gl_ui;
	Reference<OpenGLEngine> opengl_engine;

	GLUIImageRef minimap_image;
	GLUIImageRef arrow_image;

	bool expanded;
	GLUIButtonRef collapse_button;
	GLUIButtonRef expand_button;

	Array2D<MapTile> tiles;
	int last_centre_x, last_centre_y;

	std::set<Vec3i> queried_tile_coords;
	std::map<Vec3i, MapTileInfo> tile_infos;

	Vec3d last_requested_campos;
	int last_requested_tile_z;

	float map_width_ws;

	SocketBufferOutStream scratch_packet;

	OpenGLTextureRef tile_placeholder_tex;

	std::unordered_map<URLString, Vec3i> loading_texture_URL_to_tile_indices_map;
};
