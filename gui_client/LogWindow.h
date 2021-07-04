/*=====================================================================
LogWindow.h
-----------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "ui_LogWindow.h"
#include <string>

class QSettings;


/*=====================================================================
LogWindow
---------

=====================================================================*/
class LogWindow : public QMainWindow, public Ui_LogWindow
{
	Q_OBJECT
public:
	LogWindow(QWidget* parent, QSettings* settings);
	~LogWindow();

	void appendLine(const std::string& msg);

	void closeEvent(QCloseEvent* event);
private:
	void saveWindowState();

	QSettings* settings;
};
