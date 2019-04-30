/*=====================================================================
MaterialBrowser.h
--------------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#pragma once


#include "ui_MaterialBrowser.h"
#include <utils/Reference.h>
#include <string>
#include <vector>

class QPushButton;
class QOffscreenSurface;
class FlowLayout;
class TextureServer;
class OpenGLEngine;
class OpenGLMaterial;
class QOpenGLFramebufferObject;


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

	void init(QWidget* parent, const std::string& basedir_path, const std::string& appdata_path, TextureServer* texture_server_ptr);

signals:;
	void materialSelected(const std::string& path);

private slots:;
	void buttonClicked();

private:
	void createOpenGLEngineAndSurface();

	TextureServer* texture_server_ptr;
	FlowLayout *				flow_layout;
	std::vector<QPushButton *>	browser_buttons;
	std::vector<std::string>	mat_paths;

	QOpenGLFramebufferObject* fbo;
	QOpenGLContext* context;
	QOffscreenSurface* offscreen_surface;
	
	Reference<OpenGLEngine> opengl_engine;
	std::string basedir_path, appdata_path;
};
