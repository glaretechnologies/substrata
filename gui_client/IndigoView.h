/*=====================================================================
IndigoView.h
------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "utils/Timer.h"
#include "utils/Reference.h"
#include "utils/RefCounted.h"
#include <QtWidgets/QWidget>


namespace Indigo { class Mesh; }
namespace Indigo { class Renderer; }
namespace Indigo { class IndigoContext; }
namespace Indigo { class RenderBuffer; }
namespace Indigo { class UInt8Buffer; }
namespace Indigo { class ToneMapper; }
namespace Indigo { class DataManager; }
namespace Indigo { class SceneNodeRoot; }
namespace Indigo { class SceneNodeCamera; }
namespace Indigo { class SceneNodeRenderSettings; }
class WorldObject;
class WorldState;
class QLabel;
class QTimer;
class ResourceManager;
class CameraController;


class IndigoView : public QWidget
{
	Q_OBJECT
public:
	IndigoView(QWidget* parent = 0);
	~IndigoView();

	void initialise(const std::string& base_dir_path);
	void shutdown();

	void addExistingObjects(const WorldState& world_state, ResourceManager& resource_manager);

	void objectAdded(WorldObject& object, ResourceManager& resource_manager);
	void objectRemoved(WorldObject& object);
	void objectTransformChanged(WorldObject& object);

	void cameraUpdated(const CameraController& cam_controller);

	void saveSceneToDisk();

	void timerThink();

private slots:;
	void resizeDone();

protected:
	void resizeEvent(QResizeEvent *event);

private:
	void clearPreview();
#if INDIGO_SUPPORT
	Reference<Indigo::SceneNodeRoot> root_node;
	Reference<Indigo::SceneNodeRenderSettings> settings_node;
	Reference<Indigo::SceneNodeCamera> camera_node;

	Reference<Indigo::Renderer> renderer;
	Reference<Indigo::IndigoContext> context;
	Reference<Indigo::RenderBuffer> render_buffer;
	Reference<Indigo::UInt8Buffer> uint8_buffer;
	Reference<Indigo::ToneMapper> tone_mapper;
	Reference<Indigo::DataManager> data_manager;
#endif
	QLabel* label;
	QTimer* resize_timer;
};
