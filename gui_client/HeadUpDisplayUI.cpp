/*=====================================================================
HeadUpDisplayUI.cpp
-------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "HeadUpDisplayUI.h"


#include "MainWindow.h"
#include <graphics/SRGBUtils.h>


HeadUpDisplayUI::HeadUpDisplayUI()
:	main_window(NULL)
{}


HeadUpDisplayUI::~HeadUpDisplayUI()
{}


void HeadUpDisplayUI::create(Reference<OpenGLEngine>& opengl_engine_, MainWindow* main_window_, GLUIRef gl_ui_)
{
	opengl_engine = opengl_engine_;
	main_window = main_window_;
	gl_ui = gl_ui_;
}


void HeadUpDisplayUI::destroy()
{
	for(auto it = avatar_markers.begin(); it != avatar_markers.end(); ++it)
	{
		GLUIImageRef im = it->second;
		gl_ui->removeWidget(im);
	}
	avatar_markers.clear();

	gl_ui = NULL;
	opengl_engine = NULL;
}


void HeadUpDisplayUI::updateMarkerForAvatar(const Avatar* avatar, const Vec3d& avatar_pos)
{
	// We want to show a marker for the avatar if it is in front of the camera, and sufficiently far away from camera.
	Vec2f ui_coords;
	const bool visible = main_window->getGLUICoordsForPoint(avatar_pos.toVec4fPoint(), ui_coords);
	const bool should_display = visible && (avatar_pos.getDist(main_window->cam_controller.getPosition()) > 20.0);
	if(should_display)
	{
		const float im_width = gl_ui->getUIWidthForDevIndepPixelWidth(10);
		const Vec2f dot_pos = ui_coords - Vec2f(im_width/2);
		auto res = avatar_markers.find(avatar);
		if(res == avatar_markers.end())
		{
			GLUIImageRef im = new GLUIImage();
			im->create(*gl_ui, opengl_engine, main_window->base_dir_path + "/resources/dot.png", dot_pos, Vec2f(im_width), avatar->name);
			im->setColour(toLinearSRGB(Colour3f(5,0,0))); // Glowing red colour
			im->setMouseOverColour(toLinearSRGB(Colour3f(5))); // Glowing white

			gl_ui->addWidget(im);

			avatar_markers.insert(std::make_pair(avatar, im));
		}
		else
		{
			GLUIImage* im = res->second.ptr();
			im->setPosAndDims(dot_pos, Vec2f(im_width));
		}
	}
	else
	{
		auto res = avatar_markers.find(avatar);
		if(res != avatar_markers.end())
		{
			GLUIImage* im = res->second.ptr();
			im->setPosAndDims(Vec2f(-1000.0f), Vec2f(0.1f)); // Position off-screen
		}
	}
}


void HeadUpDisplayUI::removeMarkerForAvatar(const Avatar* avatar)
{
	auto res = avatar_markers.find(avatar);
	if(res != avatar_markers.end())
	{
		gl_ui->removeWidget(res->second);
		avatar_markers.erase(res);
	}
}


void HeadUpDisplayUI::eventOccurred(GLUICallbackEvent& event)
{
}
