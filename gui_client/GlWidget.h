#pragma once


#include "../utils/IncludeWindows.h" // This needs to go first for NOMINMAX.
#include "../opengl/OpenGLEngine.h"
#include "../maths/vec2.h"
#include "../maths/vec3.h"
#include "../utils/Timer.h"
#include "../utils/Reference.h"
#include "../utils/RefCounted.h"
#include <QtCore/QEvent>
#include <QtOpenGL/QGLWidget>
#include <map>


namespace Indigo { class Mesh; }
class CameraController;
class PlayerPhysics;
class TextureServer;
class EnvEmitter;


typedef std::map<Reference<Indigo::Mesh>, Reference<OpenGLMeshRenderData> > GLRenderDataMap;
//typedef std::map<Indigo::SceneNodeUID, OpenGLMaterial> GLMaterialMap;


class GlWidget : public QGLWidget
{
	Q_OBJECT        // must include this if you use Qt signals/slots

public:
	GlWidget(QWidget *parent = 0);
	~GlWidget();

	void setCameraController(CameraController* cam_controller_);
	void setPlayerPhysics(PlayerPhysics* player_physics_);

	void addObject(const Reference<GLObject>& object);
	void addOverlayObject(const Reference<OverlayObject>& object);

	void setEnvMat(OpenGLMaterial& mat);
	
	void playerPhyicsThink(); // Process keys held down.

protected:

	virtual void initializeGL();
	virtual void resizeGL(int w, int h); // Gets called whenever the widget has been resized (and also when it is shown for the first time because all newly created widgets get a resize event automatically).
	virtual void paintGL(); // Gets called whenever the widget needs to be updated.

	virtual void keyPressEvent(QKeyEvent* e);
	virtual void keyReleaseEvent(QKeyEvent* e);
	virtual void mousePressEvent(QMouseEvent* e);
	virtual void mouseReleaseEvent(QMouseEvent* e);
	virtual void mouseMoveEvent(QMouseEvent* e);
	virtual void wheelEvent(QWheelEvent* e);
	
	void showEvent(QShowEvent* e);

	void buildMaterial(OpenGLMaterial& mat);

signals:;
	void cameraUpdated();
	void widgetShowSignal();
	void mouseClicked(QMouseEvent* e);
	void mouseMoved(QMouseEvent* e);
	void keyPressed(QKeyEvent* e);
	void mouseWheelSignal(QWheelEvent* e);

private:
	GLRenderDataMap mesh_render_data; // mesh node SceneNodeUID to tri and quad data
	//GLMaterialMap material_data; // material node SceneNodeUID to OpenGLMaterial.
	//std::map<std::string, OpenGLMaterial> opengl_resized_textures;

	QPoint mouse_move_origin;
	QPoint last_mouse_press_pos;
	CameraController* cam_controller;
	PlayerPhysics* player_physics;

	std::string indigo_base_dir;

	int viewport_w, viewport_h;

	bool SHIFT_down, A_down, W_down, S_down, D_down;
public:
	float viewport_aspect_ratio;
	TextureServer* texture_server_ptr;
	Reference<OpenGLEngine> opengl_engine;
};
