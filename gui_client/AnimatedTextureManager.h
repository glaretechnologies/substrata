/*=====================================================================
AnimatedTextureManager.h
------------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include <utils/RefCounted.h>
#include <utils/Reference.h>
#include <string>
#include <vector>
class MainWindow;
class WorldObject;
class AnimatedTexCEFBrowser;
class OpenGLEngine;
class TextureData;


// Each material can potentially have its own animation.  This is thus per-material data.
struct AnimatedTexData : public RefCounted
{ 
	AnimatedTexData();
	~AnimatedTexData();

	static const double maxVidPlayDist() { return 20.0; }

	Reference<TextureData> texdata;
	std::string texdata_tex_path; // The path that texdata corresponds to.
	int last_loaded_frame_i;
	int cur_frame_i; // -1 = reached EOS

	Reference<AnimatedTexCEFBrowser> browser;
};


struct AnimatedTexObData : public RefCounted
{
	std::vector<Reference<AnimatedTexData>> mat_animtexdata; // size() == ob.material.size()

	void process(MainWindow* main_window, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt);

	std::vector<float> temp_buf; // Used for audio
};
