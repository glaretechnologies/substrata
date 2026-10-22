/*=====================================================================
VoxelsParcelImportDialog.cpp
----------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "VoxelsParcelImportDialog.h"


#include "VoxelsParcelImporter.h"
#include "../qt/QtUtils.h"
#include <utils/ThreadManager.h>
#include <QtCore/QSettings>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollBar>


VoxelsParcelImportDialog::VoxelsParcelImportDialog(QWidget* parent, QSettings* settings_, ThreadSafeQueue<std::string>* log_queue_, ThreadManager* import_thread_manager_)
:	QDialog(parent),
	settings(settings_),
	log_queue(log_queue_),
	import_thread_manager(import_thread_manager_),
	finished(false),
	stop_requested(false)
{
	setupUi(this);

	// Remove question mark from the title bar (see https://stackoverflow.com/questions/81627/how-can-i-hide-delete-the-help-button-on-the-title-bar-of-a-qt-dialog)
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	setAttribute(Qt::WA_DeleteOnClose);

	this->restoreGeometry(settings->value("VoxelsParcelImportDialog/geometry").toByteArray());

	startTimer(100);
}


VoxelsParcelImportDialog::~VoxelsParcelImportDialog()
{
	settings->setValue("VoxelsParcelImportDialog/geometry", saveGeometry());
}


void VoxelsParcelImportDialog::timerEvent(QTimerEvent* event)
{
	while(!log_queue->empty())
	{
		const std::string msg = log_queue->dequeue();

		if(msg == VoxelsParcelImportThread::finishedMarker())
		{
			finished = true;
			buttonBox->button(QDialogButtonBox::Cancel)->setText("Close");
			buttonBox->button(QDialogButtonBox::Cancel)->setEnabled(true);
		}
		else
		{
			// Keep following the end of the log, unless the user has scrolled up to read something.
			QScrollBar* scrollbar = logPlainTextEdit->verticalScrollBar();
			const bool at_end = scrollbar->value() == scrollbar->maximum();

			logPlainTextEdit->appendPlainText(QtUtils::toQString(msg));

			if(at_end)
				scrollbar->setValue(scrollbar->maximum());
		}
	}
}


void VoxelsParcelImportDialog::reject()
{
	if(finished)
	{
		QDialog::reject(); // Closes and deletes the dialog.
		return;
	}

	// Ask the import to stop.  It stops at the next feature or download wait, then writes the finished marker, which turns the button into Close.
	if(!stop_requested)
	{
		stop_requested = true;
		import_thread_manager->killThreadsNonBlocking();
		buttonBox->button(QDialogButtonBox::Cancel)->setEnabled(false);
		logPlainTextEdit->appendPlainText("Stopping...");
	}
}
