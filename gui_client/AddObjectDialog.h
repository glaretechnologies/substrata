/*=====================================================================
AddObjectDialog.h
----------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "ui_AddObjectDialog.h"
#include "../dll/include/IndigoMesh.h"
#include "../shared/WorldMaterial.h"
#include "../shared/WorldObject.h"
#include <utils/ThreadManager.h>
#include <graphics/BatchedMesh.h>
#include <QtCore/QString>
class QSettings;
struct GLObject;
struct IMFDXGIDeviceManager;
class TextureServer;


/*=====================================================================
AddObjectDialog
---------------

=====================================================================*/
class AddObjectDialog : public QDialog, private Ui_AddObjectDialog
{
	Q_OBJECT
public:
	AddObjectDialog(const std::string& base_dir_path_, QSettings* settings, 
		Reference<ResourceManager> resource_manager, IMFDXGIDeviceManager* dev_manager, glare::TaskManager* main_task_manager, glare::TaskManager* high_priority_task_manager);
	~AddObjectDialog();

	static void tryLoadTexturesForPreviewOb(Reference<GLObject> preview_gl_ob, std::vector<WorldMaterialRef>& world_materials, OpenGLEngine* opengl_engine, 
		TextureServer& texture_server, QWidget* parent_widget);

private slots:;
	void accepted();
	void dialogFinished();

	void modelSelected(QListWidgetItem*);
	void modelDoubleClicked(QListWidgetItem*);

	void filenameChanged(QString& filename);

	void urlChanged(const QString& filename);
	void urlEditingFinished();
	
private:
	virtual void closeEvent(QCloseEvent *event);
	virtual void timerEvent(QTimerEvent* event);

	void loadModelIntoPreview(const std::string& local_path);

	void shutdownGL();

	QSettings* settings;

	Reference<GLObject> preview_gl_ob;

public:
	std::string result_path;
	BatchedMeshRef loaded_mesh;

	glare::AllocatorVector<Voxel, 16> loaded_voxels;
	std::vector<WorldMaterialRef> loaded_materials; // Will be cleared if a valid object was not loaded.
	Vec3f scale;
	Vec3f axis;
	float angle;


	bool loaded_mesh_is_image_cube; // Are we using the standard model for displaying images/videos?

	float ob_cam_right_translation; // Amount the object position for the new object should be translated along the camera right vector.
	float ob_cam_up_translation;

private:
	void makeMeshForWidthAndHeight(const std::string& local_path, int w, int h);

	std::string base_dir_path;
	std::vector<std::string> models;

	URLString last_url;

	ThreadSafeQueue<Reference<ThreadMessage> > msg_queue; // From threads
	ThreadManager thread_manager; // For NetDownloadResourcesThread
	glare::AtomicInt num_net_resources_downloading;

	Reference<ResourceManager> resource_manager;

	IMFDXGIDeviceManager* dev_manager;

	Reference<TextureServer> texture_server;

	glare::TaskManager* main_task_manager;
	glare::TaskManager* high_priority_task_manager;
};
