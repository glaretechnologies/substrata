/*=====================================================================
HeadUpDisplayUI.cpp
-------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "HeadUpDisplayUI.h"


#include "GUIClient.h"
#include <graphics/SRGBUtils.h>


static const float AVATAR_MARKER_DOT_Z = -0.8f;


HeadUpDisplayUI::HeadUpDisplayUI()
:	gui_client(NULL)
{}


HeadUpDisplayUI::~HeadUpDisplayUI()
{}


void HeadUpDisplayUI::create(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_)
{
	opengl_engine = opengl_engine_;
	gui_client = gui_client_;
	gl_ui = gl_ui_;
}


void HeadUpDisplayUI::destroy()
{
	gl_ui = NULL;
	opengl_engine = NULL;
}


void HeadUpDisplayUI::think()
{
}


void HeadUpDisplayUI::viewportResized(int w, int h)
{
	if(gl_ui.nonNull())
	{
		updateWidgetPositions();
	}
}


void HeadUpDisplayUI::updateWidgetPositions()
{
}


void HeadUpDisplayUI::updateMarkerForAvatar(Avatar* avatar, const Vec3d& avatar_pos)
{
	// We want to show a marker for the avatar if it is in front of the camera, and the avatar is sufficiently far away from camera.
	// If the marker is off-screen, we want to clamp it to the screen, and also display an arrow pointing towards the avatar.
	
	const Vec3d cam_to_avatar = avatar_pos - gui_client->cam_controller.getPosition();
	
	// If avatar is behind camera, move to front of camera so we can work out the direction off-screen to the marker.
	const double back_dist = dot(cam_to_avatar, gui_client->cam_controller.getForwardsVec());
	Vec3d moved_avatar_pos = avatar_pos;
	if(back_dist < 0)
	{
		// Move marker forwards so there is a 45 degree angle between cam forwards vec and vec to new marker position.
		const float dx = (float)dot(cam_to_avatar, gui_client->cam_controller.getRightVec());
		const float dy = (float)dot(cam_to_avatar, gui_client->cam_controller.getUpVec());
		const double side_dist = sqrt(Maths::square(dx) + Maths::square(dy));

		moved_avatar_pos = avatar_pos + gui_client->cam_controller.getForwardsVec() * (-back_dist + side_dist);
	}

	Vec2f ui_coords;
	bool visible = gui_client->getGLUICoordsForPoint(moved_avatar_pos.toVec4fPoint(), ui_coords);
	const bool should_display = visible && (avatar_pos.getDist(gui_client->cam_controller.getPosition()) > 40.0);
	if(should_display)
	{
#if 0
		const float margin_w = 0.03f;
		float x_edge_mag = 1 - margin_w;
		float y_edge_mag = gl_ui->getViewportMinMaxY(opengl_engine) - margin_w;

		// Clamp marker to screen.
		Vec2f clamped_ui_coords;
		clamped_ui_coords.x = myClamp(ui_coords.x, -x_edge_mag, x_edge_mag);
		clamped_ui_coords.y = myClamp(ui_coords.y, -y_edge_mag, y_edge_mag);

		// Work out arrow pos and rotation
		Vec2f arrow_pos(-1000.f); // By default arrow is off-screen
		float arrow_rotation = 0; // Rotation of right-pointing arrow image.
		if(clamped_ui_coords.x > ui_coords.x) // If marker was clamped to left side of screen:
		{
			arrow_pos = clamped_ui_coords - Vec2f(margin_w * 0.5f, 0); // Just to left of marker
			arrow_rotation = Maths::pi<float>();
		}
		else if(clamped_ui_coords.x < ui_coords.x) // If was clamped to right side of screen:
		{
			arrow_pos = clamped_ui_coords + Vec2f(margin_w * 0.5f, 0);
		}

		if(clamped_ui_coords.y > ui_coords.y) // If was clamped to bottom side of screen:
		{
			arrow_pos = clamped_ui_coords - Vec2f(0, margin_w * 0.5f);
			arrow_rotation = -Maths::pi<float>() / 2;
		}
		else if(clamped_ui_coords.y < ui_coords.y) // If was clamped to top side of screen:
		{
			arrow_pos = clamped_ui_coords + Vec2f(0, margin_w * 0.5f);
			arrow_rotation = Maths::pi<float>() / 2;
		}

		// Handle case where marker ends up in corner of screen, in this case we want the arrow to be diagonally pointed.
		const float edge_move_x = margin_w * 0.4f;
		if(clamped_ui_coords.x > ui_coords.x && clamped_ui_coords.y < ui_coords.y) // If was clamped to left side and top of screen:
		{
			arrow_pos = clamped_ui_coords + Vec2f(-edge_move_x, edge_move_x);
			arrow_rotation = Maths::pi<float>() * (3 / 4.f);
		}
		else if(clamped_ui_coords.x > ui_coords.x && clamped_ui_coords.y > ui_coords.y) // If was clamped to left side and bottom of screen:
		{
			arrow_pos = clamped_ui_coords + Vec2f(-edge_move_x, -edge_move_x);
			arrow_rotation = Maths::pi<float>() * (5 / 4.f);
		}
		else if(clamped_ui_coords.x < ui_coords.x && clamped_ui_coords.y > ui_coords.y) // If was clamped to right side and bottom of screen:
		{
			arrow_pos = clamped_ui_coords + Vec2f(edge_move_x, -edge_move_x);
			arrow_rotation = -Maths::pi<float>() * (1 / 4.f);
		}
		else if(clamped_ui_coords.x < ui_coords.x && clamped_ui_coords.y < ui_coords.y) // If was clamped to right side and top of screen:
		{
			arrow_pos = clamped_ui_coords + Vec2f(edge_move_x, edge_move_x);
			arrow_rotation = Maths::pi<float>() * (1 / 4.f);
		}
#else
		// Don't clamp marker to screen, just hide if off screen.
		Vec2f clamped_ui_coords = ui_coords;
		const bool on_screen = (ui_coords.x >= -1) || (ui_coords.x <= 1) || (ui_coords.y >= gl_ui->getViewportMinMaxY()) || (ui_coords.y <= gl_ui->getViewportMinMaxY());
#endif
		
		// Create (if needed) and/or position marker dot.
		const float im_width = gl_ui->getUIWidthForDevIndepPixelWidth(10);
		const Vec2f dot_corner_pos = clamped_ui_coords - Vec2f(im_width/2);
		if(avatar->hud_marker.isNull()) // If marker does not exist yet:
		{
			// Create marker dot
			GLUIImageRef im = new GLUIImage(*gl_ui, opengl_engine, gui_client->resources_dir_path + "/dot.png", dot_corner_pos, Vec2f(im_width), /*tooltip=*/avatar->name, AVATAR_MARKER_DOT_Z);
			im->setColour(toLinearSRGB(Colour3f(5,0,0))); // Glowing red colour
			im->setMouseOverColour(toLinearSRGB(Colour3f(5))); // Glowing white

			gl_ui->addWidget(im);
			avatar->hud_marker = im;
		}
		else
			avatar->hud_marker->setPosAndDims(dot_corner_pos, Vec2f(im_width), /*z=*/AVATAR_MARKER_DOT_Z);

		avatar->hud_marker->setVisible(on_screen);

#if 0
		// Create (if needed) and/or position marker arrow.
		const float arrow_im_width = gl_ui->getUIWidthForDevIndepPixelWidth(14);
		const Vec2f arrow_corner_pos = arrow_pos - Vec2f(arrow_im_width/2);
		if(avatar->hud_marker_arrow.isNull()) // If marker arrow does not exist yet:
		{
			// Create marker arrow
			GLUIImageRef im = new GLUIImage();
			im->create(*gl_ui, opengl_engine, gui_client->resources_dir_path + "/arrow.png", arrow_corner_pos, Vec2f(arrow_im_width), /*tooltip=*/avatar->name);
			im->setColour(toLinearSRGB(Colour3f(5,0,0))); // Glowing red colour
			im->setMouseOverColour(toLinearSRGB(Colour3f(5))); // Glowing white
			
			gl_ui->addWidget(im);
			avatar->hud_marker_arrow = im;
		}
		else
			avatar->hud_marker_arrow->setTransform(arrow_corner_pos, Vec2f(arrow_im_width), arrow_rotation);
#endif
	}
	else // Else if shouldn't display marker:
	{
		if(avatar->hud_marker.nonNull())
			avatar->hud_marker->setPosAndDims(Vec2f(-1000.0f), Vec2f(0.1f)); // Position off-screen
		if(avatar->hud_marker_arrow.nonNull())
			avatar->hud_marker_arrow->setPosAndDims(Vec2f(-1000.0f), Vec2f(0.1f)); // Position off-screen
	}
}


void HeadUpDisplayUI::removeMarkerForAvatar(Avatar* avatar)
{
	if(avatar->hud_marker.nonNull())
	{
		gl_ui->removeWidget(avatar->hud_marker);
		avatar->hud_marker = NULL;
	}

	if(avatar->hud_marker_arrow.nonNull())
	{
		gl_ui->removeWidget(avatar->hud_marker_arrow);
		avatar->hud_marker_arrow = NULL;
	}
}


void HeadUpDisplayUI::eventOccurred(GLUICallbackEvent& event)
{
}
