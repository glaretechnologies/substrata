/*=====================================================================
AddObjectDialog.h
----------------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "ui_AddObjectDialog.h"
#include "../dll/include/IndigoMesh.h"
#include "../shared/WorldMaterial.h"
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
	AddObjectDialog(QSettings* settings, TextureServer* texture_server_ptr);
	~AddObjectDialog();

private slots:;
	void timerEvent();
	void accepted();

	void filenameChanged(QString& filename);
	
private:
	QSettings* settings;
	QTimer* timer;

	Reference<GLObject> preview_gl_ob;

	bool loaded_model;

public:
	std::string result_path;
	//uint64 model_hash;
	Indigo::MeshRef loaded_mesh;
	float suggested_scale;
	std::vector<WorldMaterialRef> loaded_materials;
};
