/*=====================================================================
AnimatedTextureManager.h
------------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "../opengl/OpenGLEngine.h"
#include "../opengl/WGL.h"
#include "../video/VideoReader.h"
#include <map>


class SubstrataVideoReaderCallback;
class MainWindow;
class WorldObject;
class QMediaPlayer;
class SubstrataVideoSurface;
class ResourceIODeviceWrapper;
class QBuffer;
struct CreateVidReaderTask;

struct ID3D11Device;
struct IMFDXGIDeviceManager;


struct OpenGLAndD3DTex
{
	OpenGLTextureRef opengl_tex;
#ifdef _WIN32
	HANDLE interop_handle;
#endif
};


struct AnimatedTexData : public RefCounted
{ 
	AnimatedTexData();
	~AnimatedTexData();

	void shutdown(
#ifdef _WIN32
		WGL& wgl_funcs, HANDLE interop_device_handle
#endif
	);

	std::map<void*, OpenGLAndD3DTex> opengl_tex_for_d3d_tex;

	Reference<VideoReader> video_reader;
	double in_anim_time; // Current time along timeline of video.  Doesn't change if video is paused.

	SubstrataVideoSurface* video_surface;
	QMediaPlayer* media_player;

	Reference<ResourceIODeviceWrapper> resource_io_wrapper;
	//QBuffer* resource_qbuffer;

	ThreadSafeQueue<SampleInfoRef> sample_queue; // Queue of samples from VidReader to this class.

	CircularBuffer<SampleInfoRef> vid_frame_queue;

	SampleInfoRef current_video_frame;

	Reference<CreateVidReaderTask> create_vid_reader_task;

	SubstrataVideoReaderCallback* callback;

	OpenGLTextureRef textures[2];


	Reference<TextureData> texdata;
	std::string texdata_tex_path; // The path that texdata corresponds to.
	int cur_frame_i; // -1 = reached EOS
	bool at_vidreader_EOS; // Has the vid reader sent us a NULL sample (signifying EOS)?
	int num_samples_pending; // Number of samples we have started reading, that we have not read back from the sample_queue yet.

	bool encounted_error;
#ifdef _WIN32
	HANDLE locked_interop_tex_ob;
#endif
};

struct AnimatedTexObData // : public RefCounted
{
	std::vector<Reference<AnimatedTexData>> mat_animtexdata; // size() == ob.material.size()

	void process(MainWindow* main_window, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt);

	std::vector<float> temp_buf;//TEMP
};



//class AnimatedTextureManager
//{
//public:
//
//};
