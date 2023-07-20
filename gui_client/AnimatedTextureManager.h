/*=====================================================================
AnimatedTextureManager.h
------------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include <utils/RefCounted.h>
#include <utils/Reference.h>
#include <string>
#include <vector>
class MainWindow;
class WorldObject;
class EmbeddedBrowser;
class OpenGLEngine;
class OpenGLMaterial;
class TextureData;
class OpenGLTexture;


// Each material can potentially have its own animation.  This is thus per-material data.
struct AnimatedTexData : public RefCounted
{ 
	AnimatedTexData();
	~AnimatedTexData();

	static double maxVidPlayDist() { return 20.0; }

	int last_loaded_frame_i;
	int cur_frame_i; // -1 = reached EOS

	Reference<EmbeddedBrowser> browser;
};


struct MaterialAnimatedTexData
{
	Reference<AnimatedTexData> refl_col_animated_tex_data;
	Reference<AnimatedTexData> emission_col_animated_tex_data;
};


struct AnimatedTexObDataProcessStats
{
	int num_gif_textures_processed;
	int num_mp4_textures_processed;
};

struct AnimatedTexObData : public RefCounted
{
	std::vector<MaterialAnimatedTexData> mat_animtexdata; // size() == ob.material.size()

	AnimatedTexObDataProcessStats process(MainWindow* main_window, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt);

private:
	void processGIFAnimatedTex(MainWindow* main_window, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt,
		OpenGLMaterial& mat, Reference<OpenGLTexture>& texture, AnimatedTexData& animation_data, const std::string& tex_path, bool is_refl_tex);

	void processMP4AnimatedTex(MainWindow* main_window, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt,
		OpenGLMaterial& mat, AnimatedTexData& animation_data, const std::string& tex_path, bool is_refl_tex);

	std::vector<float> temp_buf; // Used for audio
};
