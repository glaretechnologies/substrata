/*=====================================================================
AddObjectDialog.h
----------------------
Copyright Glare Technologies Limited 2016 -
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


/*=====================================================================
AvatarSettingsDialog
-------------

=====================================================================*/
class AddObjectDialog : public QDialog, private Ui_AddObjectDialog
{
	Q_OBJECT
public:
	AddObjectDialog(const std::string& base_dir_path_, QSettings* settings, TextureServer* texture_server_ptr, 
		Reference<ResourceManager> resource_manager);
	~AddObjectDialog();

private slots:;
	void accepted();

	void modelSelected(QListWidgetItem*);
	void modelDoubleClicked(QListWidgetItem*);

	void filenameChanged(QString& filename);

	void urlChanged(const QString& filename);
	void urlEditingFinished();
	
private:
	virtual void timerEvent(QTimerEvent* event);

	void loadModelIntoPreview(const std::string& local_path);

	QSettings* settings;

	Reference<GLObject> preview_gl_ob;

	bool loaded_model;

public:
	std::string result_path;
	//uint64 model_hash;
	BatchedMeshRef loaded_mesh;
	WorldObjectRef loaded_object;

private:
	std::string base_dir_path;
	std::vector<std::string> models;

	std::string last_url;

	ThreadSafeQueue<Reference<ThreadMessage> > msg_queue; // From threads
	ThreadManager thread_manager; // For NetDownloadResourcesThread
	IndigoAtomic num_net_resources_downloading;

	Reference<ResourceManager> resource_manager;
};
