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
class GUIClient;
class WorldObject;
class EmbeddedBrowser;
class OpenGLEngine;
class OpenGLMaterial;
class TextureData;
class OpenGLTexture;
class PCG32;


// Each material can potentially have its own animation.  This is thus per-material data.
struct AnimatedTexData : public RefCounted
{ 
	AnimatedTexData(bool is_mp4, double time_offset_);
	~AnimatedTexData();

	static double maxVidPlayDist() { return 20.0; }

	int last_loaded_frame_i;
	int cur_frame_i; // -1 = reached EOS

	Reference<EmbeddedBrowser> browser;

	bool is_mp4;

	double time_offset;
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
	int num_gif_frames_advanced;
};

struct AnimatedTexObData : public RefCounted
{
	std::vector<MaterialAnimatedTexData> mat_animtexdata; // size() == ob.material.size()

	AnimatedTexObDataProcessStats process(GUIClient* gui_client, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt);

	void rescanObjectForAnimatedTextures(WorldObject* ob, PCG32& rng);

private:
	void processGIFAnimatedTex(GUIClient* gui_client, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt,
		OpenGLMaterial& mat, Reference<OpenGLTexture>& texture, AnimatedTexData& animation_data, const std::string& tex_path, bool is_refl_tex, int& nun_gif_frames_advanced_in_out);

	void processMP4AnimatedTex(GUIClient* gui_client, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt,
		OpenGLMaterial& mat, AnimatedTexData& animation_data, const std::string& tex_path, bool is_refl_tex);
};
