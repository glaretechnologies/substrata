/*=====================================================================
CreateObjectsDialog.h
---------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "ui_CreateObjectsDialog.h"
#include <utils/ThreadSafeQueue.h>
#include <QtCore/QString>
class QSettings;


/*=====================================================================
CreateObjectsDialog
-------------------
Shows a log of progress when creating objects, from the 
LoadObjectsFromDisk command.
=====================================================================*/
class CreateObjectsDialog : public QDialog, public Ui_CreateObjectsDialog
{
	Q_OBJECT
public:
	CreateObjectsDialog(QSettings* settings);
	~CreateObjectsDialog();

	virtual void timerEvent(QTimerEvent* event) override;

	ThreadSafeQueue<std::string>* msg_queue;
private:
	QSettings* settings;
};
