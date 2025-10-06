/*=====================================================================
AnimatedTextureManager.h
------------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include <opengl/OpenGLTexture.h>
#include <utils/RefCounted.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/Reference.h>
#include <string>
#include <vector>
#include <set>
#include <map>
class GUIClient;
class WorldObject;
class EmbeddedBrowser;
class OpenGLEngine;
class OpenGLMaterial;
struct GLObject;
class TextureData;
class OpenGLTexture;
class PCG32;
class AnimatedTextureManager;


// Each material can potentially have its own animation.  This is thus per-material data.
struct AnimatedTexData : public RefCounted
{ 
	AnimatedTexData(bool is_mp4);
	~AnimatedTexData();

	static double maxVidPlayDist() { return 20.0; }

	Reference<EmbeddedBrowser> browser;

	bool is_mp4;
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

	void rescanObjectForAnimatedTextures(OpenGLEngine* opengl_engine, WorldObject* ob, PCG32& rng, AnimatedTextureManager& animated_tex_manager);

private:
	void processMP4AnimatedTex(GUIClient* gui_client, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt,
		OpenGLMaterial& mat, AnimatedTexData& animation_data, const OpenGLTextureKey& tex_path, bool is_refl_tex);
};




struct AnimatedTexUse
{
	Reference<GLObject> ob;
	size_t mat_index;

	bool operator < (const AnimatedTexUse& other) const
	{
		if(ob < other.ob)
			return true;
		else if(other.ob < ob)
			return false;
		return mat_index < other.mat_index;
	}
};


struct AnimatedTexInfo : public ThreadSafeRefCounted
{
	std::set<AnimatedTexUse> uses;

	int last_loaded_frame_i;
	int cur_frame_i; // -1 = reached EOS

	Reference<OpenGLTexture> original_tex;
	Reference<OpenGLTexture> other_tex;
	int cur_displayed_tex_i; // Index of texture currently assigned to objects.  0 = original_tex, 1 = other_tex
	int next_tex_i;          // Index of next texture to load next animation frame into.  0 = original_tex, 1 = other_tex
};


/*=====================================================================
AnimatedTextureManager
----------------------
Needed to handle multiple materials using the same animated texture.
Keeps track of all uses of animated textures, and does swapping between the ping-ponged textures
when a new frame is uploaded.
=====================================================================*/
class AnimatedTextureManager : public ThreadSafeRefCounted
{
public:
	AnimatedTextureManager();

	void think(GUIClient* gui_client, OpenGLEngine* opengl_engine, double anim_time, double dt);

	std::string diagnostics();

	void doTextureSwap(OpenGLEngine* opengl_engine, const Reference<OpenGLTexture>& old_tex, const Reference<OpenGLTexture>& new_tex);

	void checkAddTextureUse(const Reference<OpenGLTexture>& tex, Reference<GLObject> ob, size_t mat_index);
	void removeTextureUse(const Reference<OpenGLTexture>& tex, Reference<GLObject> ob, size_t mat_index);

	std::map<OpenGLTextureKey, Reference<AnimatedTexInfo>> tex_info;

	int last_num_textures_visible_and_close;
};
