#pragma once


#include "../utils/IncludeWindows.h" // This needs to go first for NOMINMAX.
#include "../opengl/OpenGLEngine.h"
#include "../opengl/OpenGLMeshRenderData.h"
#include "../maths/vec2.h"
#include "../maths/vec3.h"
#include "../utils/Timer.h"
#include "../utils/Reference.h"
#include "../utils/RefCounted.h"
#include "../utils/GlareAllocator.h"
#include "../utils/UniqueRef.h"
#include <QtCore/QEvent>
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#include <QtOpenGLWidgets/QOpenGLWidget>
#else
#include <QtOpenGL/QGLWidget>
#endif

#include <map>


namespace Indigo { class Mesh; }
class TextureServer;
class EnvEmitter;
class QSettings;
class GaussianSplatRenderer;
class GaussianSplatData;


class AddObjectPreviewWidget : public 
#if 0 // QT_VERSION_MAJOR >= 6
	QOpenGLWidget
#else
	QGLWidget
#endif
{
	Q_OBJECT        // must include this if you use Qt signals/slots

public:
	AddObjectPreviewWidget(QWidget *parent = 0);
	~AddObjectPreviewWidget();

	void init(const std::string& base_dir_path_, QSettings* settings_, Reference<TextureServer> texture_server, glare::TaskManager* main_task_manager, glare::TaskManager* high_priority_task_manager);
	void shutdown();

	// Shows a Gaussian splat cloud as the preview object, replacing any previously shown one, and frames the camera on it.
	// Splat clouds aren't GLObjects, so they can't go through the normal preview path.  Pass a null splat_data to just clear
	// any existing preview cloud.
	void setPreviewSplatCloud(const Reference<GaussianSplatData>& splat_data, const Vec3f& axis, float angle);

protected:
	virtual void initializeGL() override;
	virtual void resizeGL(int w, int h) override; // Gets called whenever the widget has been resized (and also when it is shown for the first time because all newly created widgets get a resize event automatically).
	virtual void paintGL() override; // Gets called whenever the widget needs to be updated.

	virtual void keyPressEvent(QKeyEvent* e) override;
	virtual void keyReleaseEvent(QKeyEvent* e) override;
	virtual void mousePressEvent(QMouseEvent* e) override;
	virtual void mouseMoveEvent(QMouseEvent* e) override;
	virtual void wheelEvent(QWheelEvent* e) override;
	
	virtual void showEvent(QShowEvent* e) override;

signals:;
	void widgetShowSignal();

private:
	std::string base_dir_path;
	QPoint mouse_prev_pos;
	QPoint mouse_move_origin;

	std::string indigo_base_dir;

	float viewport_aspect_ratio;
	int viewport_w, viewport_h;

	float cam_phi, cam_theta, cam_dist;
	Vec4f cam_target_pos;
public:
	Reference<TextureServer> texture_server;
	Reference<OpenGLEngine> opengl_engine;

	Reference<GLObject> target_marker_ob; // For debugging camera

	UniqueRef<GaussianSplatRenderer> splat_renderer; // For previewing .sog Gaussian splat clouds, which have no GLObject representation.

	Timer timer;
	QSettings* settings;

	glare::TaskManager* main_task_manager;
	glare::TaskManager* high_priority_task_manager;

	Reference<glare::MallocAllocator> mem_allocator;
};
