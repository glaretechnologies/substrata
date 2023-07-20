/*=====================================================================
AddVideoDialog.h
----------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "ui_AddVideoDialog.h"
#include "../shared/ResourceManager.h"
class QSettings;
struct GLObject;
struct IMFDXGIDeviceManager;
class TextureServer;
class OpenGLEngine;


/*=====================================================================
AddVideoDialog
--------------

=====================================================================*/
class AddVideoDialog : public QDialog, private Ui_AddVideoDialog
{
	Q_OBJECT
public:
	AddVideoDialog(QSettings* settings, TextureServer* texture_server_ptr, Reference<ResourceManager> resource_manager, IMFDXGIDeviceManager* dev_manager);
	~AddVideoDialog();

	bool wasResultLocalPath();
	std::string getVideoLocalPath();
	std::string getVideoURL();

private slots:;
	void accepted();
	void dialogFinished();

	void filenameChanged(QString& filename);

private:
	void getDimensionsForLocalVideoPath(const std::string& local_path);

	virtual void closeEvent(QCloseEvent *event);

public:
	int video_width;
	int video_height;

private:
	QSettings* settings;
	Reference<ResourceManager> resource_manager;
	IMFDXGIDeviceManager* dev_manager;
};
