/*==============//=======================================================
SDLClient.cpp
-------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/


#include "GUIClient.h"
#include "SDLUIInterface.h"
#include "SDLSettingsStore.h"
#include "TestSuite.h"
#include <maths/GeometrySampling.h>
#include <graphics/FormatDecoderGLTF.h>
#include <graphics/MeshSimplification.h>
#include <graphics/TextRenderer.h>
#include <opengl/OpenGLEngine.h>
#include <opengl/GLMeshBuilding.h>
#include <indigo/TextureServer.h>
#include <opengl/MeshPrimitiveBuilding.h>
#include <utils/Exception.h>
#include <utils/StandardPrintOutput.h>
#include <utils/IncludeWindows.h>
#include <utils/PlatformUtils.h>
#include <utils/FileUtils.h>
#include <utils/ConPrint.h>
#include <utils/StringUtils.h>
#include <GL/gl3w.h>
#include <SDL_opengl.h>
#include <SDL.h>
#include <string>
#if EMSCRIPTEN
#include <emscripten.h>
#include <emscripten/html5.h>
#endif


// If we are building on Windows, and we are not in Release mode (e.g. BUILD_TESTS is enabled), then make sure the console window is shown.
#if defined(_WIN32) && defined(BUILD_TESTS)
#pragma comment(linker, "/SUBSYSTEM:CONSOLE")
#endif


#if !defined(EMSCRIPTEN)
#define EM_BOOL bool
#endif

static void doOneMainLoopIter();


static void setGLAttribute(SDL_GLattr attr, int value)
{
	const int result = SDL_GL_SetAttribute(attr, value);
	if(result != 0)
	{
		const char* err = SDL_GetError();
		throw glare::Exception("Failed to set OpenGL attribute: " + (err ? std::string(err) : "[Unknown]"));
	}
}


Reference<OpenGLEngine> opengl_engine;

Timer* timer;
Timer* time_since_last_frame;
Timer* stats_timer;
int num_frames = 0;
std::string last_diagnostics;
bool reset = false;
double fps = 0;
bool wireframes = false;

SDL_Window* win;
SDL_GLContext gl_context;

float sun_phi = 1.f;
float sun_theta = Maths::pi<float>() / 4;

StandardPrintOutput print_output;
bool quit = false;

GUIClient* gui_client = NULL;
SDLUIInterface* sdl_ui_interface = NULL;

Vec2i mouse_move_origin(0, 0);

int main(int argc, char** argv)
{
	try
	{
		GUIClient::staticInit();


		std::map<std::string, std::vector<ArgumentParser::ArgumentType> > syntax;
		syntax["--test"] = std::vector<ArgumentParser::ArgumentType>(); // Run unit tests
		syntax["-h"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // Specify hostname to connect to
		syntax["-u"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // Specify server URL to connect to
		syntax["-linku"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // Specify server URL to connect to, when a user has clicked on a substrata URL hyperlink.
		syntax["--extractanims"] = std::vector<ArgumentParser::ArgumentType>(2, ArgumentParser::ArgumentType_string); // Extract animation data
		syntax["--screenshotslave"] = std::vector<ArgumentParser::ArgumentType>(); // Run GUI as a screenshot-taking slave.
		syntax["--testscreenshot"] = std::vector<ArgumentParser::ArgumentType>(); // Test screenshot taking
		syntax["--no_MDI"] = std::vector<ArgumentParser::ArgumentType>(); // Disable MDI in graphics engine
		syntax["--no_bindless"] = std::vector<ArgumentParser::ArgumentType>(); // Disable bindless textures in graphics engine

		std::vector<std::string> args;
		for(int i=0; i<argc; ++i)
			args.push_back(argv[i]);

		if(args.size() == 3 && args[1] == "-NSDocumentRevisionsDebugMode")
			args.resize(1); // This is some XCode debugging rubbish, remove it

		ArgumentParser parsed_args(args, syntax);

		if(parsed_args.isArgPresent("--test"))
		{
			TestSuite::test();
			return 0;
		}


#if defined(EMSCRIPTEN)
		const std::string base_dir = "/data";
#else
		const std::string base_dir = PlatformUtils::getResourceDirectoryPath();
#endif

		// NOTE: this code is also in MainWindow.cpp
#if defined(_WIN32)
		const std::string font_path = PlatformUtils::getFontsDirPath() + "/Segoeui.ttf"; // SegoeUI is shipped with Windows 7 onwards: https://learn.microsoft.com/en-us/typography/fonts/windows_7_font_list
#elif defined(__APPLE__)
		const std::string font_path = "/System/Library/Fonts/SFNS.ttf";
#else
		// Linux:
		const std::string font_path = base_dir + "/resources/TruenoLight-E2pg.otf";
#endif

		TextRendererRef text_renderer = new TextRenderer();
		TextRendererFontFaceRef font = new TextRendererFontFace(text_renderer, font_path, 40);


		timer = new Timer();
		time_since_last_frame = new Timer();
		stats_timer = new Timer();
	
		//=========================== Init SDL and OpenGL ================================
		if(SDL_Init(SDL_INIT_VIDEO) != 0)
			throw glare::Exception("SDL_Init Error: " + std::string(SDL_GetError()));


		// Set GL attributes, needs to be done before window creation.


#if EMSCRIPTEN
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		setGLAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
		setGLAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4); // We need to request a specific version for a core profile.
		setGLAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
		setGLAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

		setGLAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
#endif

		int primary_W = 1600;
		int primary_H = 1000;
#if EMSCRIPTEN
		//int width, height;
		//emscripten_get_canvas_element_size("canvas", &width, &height);
		//primary_W = (int)width;
		//primary_H = (int)height;
#endif
		

		win = SDL_CreateWindow("Substrata SDL Client", 100, 100, primary_W, primary_H, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
		if(win == nullptr)
			throw glare::Exception("SDL_CreateWindow Error: " + std::string(SDL_GetError()));


		gl_context = SDL_GL_CreateContext(win);
		if(!gl_context)
			throw glare::Exception("OpenGL context could not be created! SDL Error: " + std::string(SDL_GetError()));


		//SDL_SetHint("SDL_HINT_MOUSE_RELATIVE_WARP_MOTION", "true");

#if !defined(EMSCRIPTEN)
		gl3wInit();
#endif



		// Create OpenGL engine
		OpenGLEngineSettings settings;
		settings.compress_textures = true;
		settings.shadow_mapping = true;
		settings.depth_fog = true;
		settings.render_water_caustics = true;
		settings.allow_multi_draw_indirect = true; // TEMP
		settings.allow_bindless_textures = true; // TEMP
#if defined(EMSCRIPTEN)
		settings.use_general_arena_mem_allocator = false;
#endif
		opengl_engine = new OpenGLEngine(settings);

		TextureServer* texture_server = new TextureServer(/*use_canonical_path_keys=*/false);

#if defined(EMSCRIPTEN)
		const std::string data_dir = "/data";
#else
		const std::string data_dir = PlatformUtils::getEnvironmentVariable("GLARE_CORE_TRUNK_DIR") + "/opengl";
#endif
		
		opengl_engine->initialise(data_dir, texture_server, &print_output);
		if(!opengl_engine->initSucceeded())
			throw glare::Exception("OpenGL init failed: " + opengl_engine->getInitialisationErrorMsg());
		opengl_engine->setViewportDims(primary_W, primary_H);
		opengl_engine->setMainViewportDims(primary_W, primary_H);


		sun_phi = 1.f;
		sun_theta = Maths::pi<float>() / 4;
		opengl_engine->setSunDir(normalise(Vec4f(std::cos(sun_phi) * sin(sun_theta), std::sin(sun_phi) * sin(sun_theta), cos(sun_theta), 0)));
		opengl_engine->setEnvMapTransform(Matrix3f::rotationMatrix(Vec3f(0,0,1), sun_phi));


		
		
		const std::string appdata_path = PlatformUtils::getOrCreateAppDataDirectory("Cyberspace");

		

		gui_client = new GUIClient(base_dir, appdata_path, parsed_args);
		gui_client->opengl_engine = opengl_engine;
		gui_client->texture_server = texture_server;


		std::string cache_dir = appdata_path;


		SDLSettingsStore* settings_store = new SDLSettingsStore();


		sdl_ui_interface = new SDLUIInterface();
		sdl_ui_interface->window = win;
		sdl_ui_interface->gl_context = gl_context;
		sdl_ui_interface->gui_client = gui_client;
		sdl_ui_interface->font = font;

		gui_client->initialise(cache_dir, settings_store, sdl_ui_interface);

		gui_client->afterGLInitInitialise(1.0, true, opengl_engine, font);



		std::string server_URL = "sub://substrata.info";
		bool server_URL_explicitly_specified = false;

		if(parsed_args.isArgPresent("-h"))
		{
			server_URL = "sub://" + parsed_args.getArgStringValue("-h");
			server_URL_explicitly_specified = true;
		}
		if(parsed_args.isArgPresent("-u"))
		{
			server_URL = parsed_args.getArgStringValue("-u");
			server_URL_explicitly_specified = true;
		}

		gui_client->connectToServer(server_URL);


		//---------------------- Set env material -------------------
		{
			OpenGLMaterial env_mat;
			opengl_engine->setEnvMat(env_mat);
		}
		opengl_engine->setCirrusTexture(opengl_engine->getTexture(base_dir + "/resources/cirrus.exr"));

		
		
		conPrint("Starting main loop...");
#if EMSCRIPTEN
		//emscripten_request_animation_frame_loop(doOneMainLoopIter, 0);

		emscripten_set_main_loop(doOneMainLoopIter, /*fps=*/0, /*simulate_infinite_loop=*/true);
#else
		while(!quit)
		{
			doOneMainLoopIter();
		}

		SDL_Quit();

		opengl_engine = NULL;
#endif

		
		conPrint("main finished...");

		GUIClient::staticShutdown();
		return 0;
	}
	catch(glare::Exception& e)
	{
		stdErrPrint(e.what());
		return 1;
	}
}


static float sensorWidth() { return 0.035f; }
static float lensSensorDist() { return 0.025f; }


static Vec2f GLCoordsForGLWidgetPos(OpenGLEngine& gl_engine, const Vec2f widget_pos)
{
	const int vp_width  = gl_engine.getViewPortWidth();
	const int vp_height = gl_engine.getViewPortHeight();


	const int device_pixel_ratio = 1; // TEMP main_window->ui->glWidget->devicePixelRatio(); // For retina screens this is 2, meaning the gl viewport width is in physical pixels, of which have twice the density of qt pixel coordinates.
	const int use_vp_width  = vp_width  / device_pixel_ratio;
	const int use_vp_height = vp_height / device_pixel_ratio;


	return Vec2f(
		 (widget_pos.x - use_vp_width /2) / (use_vp_width /2),
		-(widget_pos.y - use_vp_height/2) / (use_vp_height/2)
	);
}


static inline Key getKeyForSDLKey(SDL_Keycode sym)
{
	switch(sym)
	{
		case SDLK_ESCAPE: return Key_Escape;
		case SDLK_BACKSPACE: return Key_Backspace;
		case SDLK_DELETE: return Key_Delete;
		case SDLK_SPACE: return Key_Space;
		case SDLK_LEFTBRACKET: return Key_LeftBracket;
		case SDLK_RIGHTBRACKET: return Key_RightBracket;
		case SDLK_PAGEUP: return Key_PageUp;
		case SDLK_PAGEDOWN: return Key_PageDown;
		case SDLK_EQUALS: return Key_Equals;
		case SDLK_PLUS: return Key_Plus;
		case SDLK_MINUS: return Key_Minus;
		case SDLK_LEFT: return Key_Left;
		case SDLK_RIGHT: return Key_Right;
		case SDLK_UP: return Key_Up;
		case SDLK_DOWN: return Key_Down;
		default: break;
	};
		
	if(sym >= SDLK_a && sym <= SDLK_z)
		return (Key)(Key_A + (sym - SDLK_a));

	if(sym >= SDLK_0 && sym <= SDLK_9)
		return (Key)(Key_0 + (sym - SDLK_0));

	if(sym >= SDLK_F1 && sym <= SDLK_F12)
		return (Key)(Key_F1 + (sym - SDLK_F1));

	return Key_None;
}


static MouseButton convertSDLMouseButton(uint8 sdl_button)
{
	if(sdl_button == 1)
		return MouseButton::Left;
	else if(sdl_button == 2)
		return MouseButton::Right;
	else if(sdl_button == 3)
		return MouseButton::Middle;
	else
		return MouseButton::None;
}


static uint32 convertSDLModifiers(SDL_Keymod sdl_keymod)
{
	uint32 modifiers = 0;
	if((sdl_keymod & SDL_Keymod::KMOD_ALT) != 0)
		modifiers |= Modifiers::Alt;
	if((sdl_keymod & SDL_Keymod::KMOD_CTRL) != 0)
		modifiers |= Modifiers::Ctrl;
	if((sdl_keymod & SDL_Keymod::KMOD_SHIFT) != 0)
		modifiers |= Modifiers::Shift;
	return modifiers;
}


static void convertFromSDKKeyEvent(SDL_Event ev, KeyEvent& key_event)
{
	key_event.key = getKeyForSDLKey(ev.key.keysym.sym);
	key_event.native_virtual_key = 0; // TODO
	// TODO: set key_event.text
	key_event.modifiers = convertSDLModifiers((SDL_Keymod)ev.key.keysym.mod);
}


static void doOneMainLoopIter()
{
	if(SDL_GL_MakeCurrent(win, gl_context) != 0)
		conPrint("SDL_GL_MakeCurrent failed.");


	MouseCursorState mouse_cursor_state;
	{
		SDL_GetMouseState(&mouse_cursor_state.cursor_pos.x, &mouse_cursor_state.cursor_pos.y); // Get mouse cursor pos
		mouse_cursor_state.gl_coords = GLCoordsForGLWidgetPos(*opengl_engine, Vec2f((float)mouse_cursor_state.cursor_pos.x, (float)mouse_cursor_state.cursor_pos.y));
	
		const SDL_Keymod mod_state = SDL_GetModState();
		mouse_cursor_state.ctrl_key_down = (mod_state & KMOD_CTRL) != 0;
		mouse_cursor_state.alt_key_down  = (mod_state & KMOD_ALT)  != 0;
	}

	gui_client->timerEvent(mouse_cursor_state);

	if(stats_timer->elapsed() > 1.0)
	{
		last_diagnostics = opengl_engine->getDiagnostics();
		// Update statistics
		fps = num_frames / stats_timer->elapsed();
		stats_timer->reset();
		num_frames = 0;
	}


	int gl_w, gl_h;
	SDL_GL_GetDrawableSize(win, &gl_w, &gl_h);
	if(gl_w > 0 && gl_h > 0)
	{
		// Work out current camera transform
		Vec3d cam_pos, up, forwards, right;
		cam_pos = gui_client->cam_controller.getPosition();
		gui_client->cam_controller.getBasis(right, up, forwards);

		const Matrix4f rot = Matrix4f::fromRows(right.toVec4fVector(), forwards.toVec4fVector(), up.toVec4fVector(), Vec4f(0,0,0,1));

		Matrix4f world_to_camera_space_matrix;
		rot.rightMultiplyWithTranslationMatrix(-cam_pos.toVec4fVector(), /*result=*/world_to_camera_space_matrix);

		const float near_draw_dist = 0.22f;
		const float max_draw_dist = 100000.f;

		const float sensor_width = sensorWidth();
		const float lens_sensor_dist = lensSensorDist();
		const float viewport_aspect_ratio = (float)gl_w / (float)gl_h;
		const float render_aspect_ratio = viewport_aspect_ratio;
		opengl_engine->setViewportDims(gl_w, gl_h);
		opengl_engine->setNearDrawDistance(near_draw_dist);
		opengl_engine->setMaxDrawDistance(max_draw_dist);
		opengl_engine->setPerspectiveCameraTransform(world_to_camera_space_matrix, sensor_width, lens_sensor_dist, render_aspect_ratio, /*lens shift up=*/0.f, /*lens shift right=*/0.f);
		opengl_engine->draw();
	}

	
	// Display
	SDL_GL_SwapWindow(win);

	time_since_last_frame->reset();

	// Handle any events
	SDL_Event e;
	while(SDL_PollEvent(&e))
	{
		if(e.type == SDL_QUIT) // "An SDL_QUIT event is generated when the user clicks on the close button of the last existing window" - https://wiki.libsdl.org/SDL_EventType#Remarks
		{
			quit = true;
		}
		else if(e.type == SDL_WINDOWEVENT) // If user closes the window:
		{
			if(e.window.event == SDL_WINDOWEVENT_CLOSE)
			{
				quit = true;
			}
			else if(e.window.event == SDL_WINDOWEVENT_RESIZED || e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
			{
				int w, h;
				SDL_GL_GetDrawableSize(win, &w, &h);
						
				opengl_engine->setViewportDims(w, h);
				opengl_engine->setMainViewportDims(w, h);

				gui_client->viewportResized(w, h);
			}
		}
		else if(e.type == SDL_KEYDOWN)
		{
			//if(e.key.keysym.sym == SDLK_r)
			//	reset = true;

			KeyEvent key_event;
			convertFromSDKKeyEvent(e, key_event);

			gui_client->keyPressed(key_event);
		}
		else if(e.type == SDL_MOUSEMOTION)
		{
			// conPrint("SDL_MOUSEMOTION, pos: " + Vec2i(e.motion.x, e.motion.y).toString());

			if(true/* camcam_rot_on_mouse_move_enabled*/  && (e.motion.state & SDL_BUTTON_LMASK))
			{
				Vec2i delta(e.motion.xrel, -e.motion.yrel);

				// conPrint("delta: " + delta.toString());

				const double speed_factor = 0.35; // To make delta similar to what Qt gives.
				gui_client->cam_controller.update(Vec3d(0, 0, 0), Vec2d(delta.y, delta.x) * speed_factor);

				// On Windows/linux, reset the cursor position to where we started, so we never run out of space to move.
				// QCursor::setPos() does not work on mac, and also gives a message about Substrata trying to control the computer, which we want to avoid.
				// So don't use setPos() on Mac.
#if !defined(OSX)
				SDL_WarpMouseInWindow(win, mouse_move_origin.x, mouse_move_origin.y);
#endif

				SDL_GetMouseState(&mouse_move_origin.x, &mouse_move_origin.y);
			}
			

			MouseEvent move_event;
			move_event.cursor_pos = Vec2i(e.motion.x, e.motion.y);
			move_event.gl_coords = GLCoordsForGLWidgetPos(*opengl_engine, Vec2f((float)e.motion.x, (float)e.motion.y));
			move_event.modifiers = convertSDLModifiers(SDL_GetModState());
			gui_client->mouseMoved(move_event);
		}
		else if(e.type == SDL_MOUSEBUTTONDOWN)
		{
			// conPrint("SDL_MOUSEBUTTONDOWN, pos: " + Vec2i(e.button.x, e.button.y).toString() + ", clicks: " + toString(e.button.clicks));

			SDL_GetMouseState(&mouse_move_origin.x, &mouse_move_origin.y);

			MouseEvent mouse_event;
			mouse_event.cursor_pos = Vec2i(e.button.x, e.button.y);
			mouse_event.gl_coords = GLCoordsForGLWidgetPos(*opengl_engine, Vec2f((float)e.button.x, (float)e.button.y));
			mouse_event.button = convertSDLMouseButton(e.button.button);
			mouse_event.modifiers = convertSDLModifiers(SDL_GetModState());

			if(e.button.clicks == 1) // Single click:
			{
				SDL_SetRelativeMouseMode(SDL_TRUE);
				
				gui_client->mouseClicked(mouse_event);
			}
			else if(e.button.clicks == 2) // Double click:
			{
				gui_client->doObjectSelectionTraceForMouseEvent(mouse_event);
			}
		}
		else if(e.type == SDL_MOUSEBUTTONUP)
		{
			// conPrint("SDL_MOUSEBUTTONUP, pos: " + Vec2i(e.button.x, e.button.y).toString() + ", clicks: " + toString(e.button.clicks));

			if(e.button.clicks == 1) // Single click:
			{
				SDL_SetRelativeMouseMode(SDL_FALSE);

				MouseEvent mouse_event;
				mouse_event.cursor_pos = Vec2i(e.button.x, e.button.y);
				mouse_event.gl_coords = GLCoordsForGLWidgetPos(*opengl_engine, Vec2f((float)e.button.x, (float)e.button.y));
				mouse_event.button = convertSDLMouseButton(e.button.button);
				mouse_event.modifiers = convertSDLModifiers(SDL_GetModState());
				gui_client->mouseClicked(mouse_event);
			}
		}
		else if(e.type == SDL_MOUSEWHEEL)
		{
			MouseWheelEvent wheel_event;
			wheel_event.cursor_pos = Vec2i(e.wheel.mouseX, e.wheel.mouseY);
			wheel_event.gl_coords = GLCoordsForGLWidgetPos(*opengl_engine, Vec2f((float)e.wheel.mouseX, (float)e.wheel.mouseY));
			const float scale_factor = 100; // To bring in line with what we get from Qt's angleDelta().
			wheel_event.angle_delta = Vec2i((int)(e.wheel.preciseX * scale_factor), (int)(e.wheel.preciseY * scale_factor));
			wheel_event.modifiers = convertSDLModifiers(SDL_GetModState());
			gui_client->onMouseWheelEvent(wheel_event);

		}
	}

	num_frames++;
}
