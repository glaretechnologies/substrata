#pragma once


#include "../utils/ArgumentParser.h"
#include "ui_MainWindow.h"
#include <QtCore/QEvent>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QSettings>
#include <string>
class ArgumentParser;


class MainWindow : public QMainWindow, public Ui::MainWindow
{
	Q_OBJECT
public:
	MainWindow(const std::string& base_dir_path, const std::string& appdata_path, const ArgumentParser& args,
		QWidget *parent = 0);
	~MainWindow();

	void initialise();
	
	// Semicolon is for intellisense, see http://www.qtsoftware.com/developer/faqs/faq.2007-08-23.5900165993
signals:;
	void resolutionChanged(int, int);

public slots:;

private slots:;

private:

	std::string base_dir_path;
	std::string appdata_path;
	ArgumentParser parsed_args;
};
