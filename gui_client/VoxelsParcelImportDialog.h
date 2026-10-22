/*=====================================================================
VoxelsParcelImportDialog.h
--------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "ui_VoxelsParcelImportDialog.h"
#include <utils/ThreadSafeQueue.h>
#include <string>
class QSettings;
class ThreadManager;


/*=====================================================================
VoxelsParcelImportDialog
------------------------
Non-modal dialog showing the log of an Edit > Import Voxels Parcel import,
which can take many minutes, with a button to cancel it.

The import thread writes log lines to log_queue, and VoxelsParcelImportThread::finishedMarker()
when it has finished, at which point the Cancel button becomes Close.
Cancelling asks the import thread to stop, and keeps the dialog open until it has.
=====================================================================*/
class VoxelsParcelImportDialog : public QDialog, public Ui_VoxelsParcelImportDialog
{
	Q_OBJECT
public:
	// log_queue and import_thread_manager must outlive the dialog.
	VoxelsParcelImportDialog(QWidget* parent, QSettings* settings, ThreadSafeQueue<std::string>* log_queue, ThreadManager* import_thread_manager);
	~VoxelsParcelImportDialog();

	virtual void timerEvent(QTimerEvent* event) override;

public slots:
	virtual void reject() override; // Called for the Cancel/Close button, the window close button and Escape.

private:
	QSettings* settings;
	ThreadSafeQueue<std::string>* log_queue;
	ThreadManager* import_thread_manager;
	bool finished;
	bool stop_requested;
};
