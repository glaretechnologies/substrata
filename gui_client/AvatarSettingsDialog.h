/*=====================================================================
AvatarSettingsDialog.h
----------------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "ui_AvatarSettingsDialog.h"
#include <QtCore/QString>
class QSettings;
struct GLObject;


/*=====================================================================
AvatarSettingsDialog
-------------

=====================================================================*/
class AvatarSettingsDialog : public QDialog, private Ui_AvatarSettingsDialog
{
	Q_OBJECT
public:
	AvatarSettingsDialog(QSettings* settings, TextureServer* texture_server_ptr);
	~AvatarSettingsDialog();

	std::string getAvatarName();
private slots:;
	void timerEvent();
	void accepted();

	void avatarFilenameChanged(QString& filename);
	
private:
	QSettings* settings;
	QTimer* timer;

	Reference<GLObject> avatar_gl_ob;

	bool loaded_model;
public:
	std::string result_path;
	uint64 model_hash;
};
