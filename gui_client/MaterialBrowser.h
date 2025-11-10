/*=====================================================================
MaterialBrowser.h
--------------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#pragma once


#include "ui_MaterialBrowser.h"
#include <utils/Reference.h>
#include <string>
#include <vector>

class QPushButton;
class FlowLayout;
class OpenGLEngine;
class PrintOutput;


/*=====================================================================
MaterialBrowser
---------------
Displays a bunch of buttons with material preview images on them.
=====================================================================*/
class MaterialBrowser : public QWidget, public Ui_MaterialBrowser
{
	Q_OBJECT
public:
	MaterialBrowser();
	~MaterialBrowser();

	void init(QWidget* parent, const std::string& basedir_path, const std::string& appdata_path, PrintOutput* print_output);


	void renderThumbnails(Reference<OpenGLEngine> opengl_engine);

signals:;
	void materialSelected(const std::string& path);

private slots:;
	void buttonClicked();

private:
	FlowLayout *				flow_layout;
	std::vector<QPushButton *>	browser_buttons;
	std::vector<std::string>	mat_paths;

	std::string basedir_path, appdata_path;
	PrintOutput* print_output;
};
