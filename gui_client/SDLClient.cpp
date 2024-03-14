/*=====================================================================
SDLClient.cpp
-------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/


#include "GUIClient.h"
#include "SDLUIInterface.h"
#include "SDLSettingsStore.h"
#include "TestSuite.h"
#include "URLParser.h"
#include <maths/GeometrySampling.h>
#include <graphics/FormatDecoderGLTF.h>
#include <graphics/MeshSimplification.h>
#include <graphics/TextRenderer.h>
#include <graphics/EXRDecoder.h>
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
//#include <utils/LimitedAllocator.h>
#include <networking/URL.h>
#include <webserver/Escaping.h>
#include <GL/gl3w.h>
#include <SDL_opengl.h>
#include <SDL.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl.h>
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
Timer* diagnostics_timer;
Timer* mem_usage_sampling_timer;
Timer* last_update_URL_timer;
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

static std::vector<float> mem_usage_values;

static bool show_imgui_info_window = false;


#if EMSCRIPTEN

EM_JS(char*, getLocationHost, (), {
	return stringToNewUTF8(window.location.host);
});

// Get the ?a=b part of the URL (see https://developer.mozilla.org/en-US/docs/Web/API/Location)
EM_JS(char*, getLocationSearch, (), {
	return stringToNewUTF8(window.location.search);
});

EM_JS(void, updateURL, (const char* new_URL), {
	history.replaceState(null, "",  UTF8ToString(new_URL)); // See https://developer.mozilla.org/en-US/docs/Web/API/History/replaceState
});

#endif

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

#if !defined(EMSCRIPTEN)
		if(parsed_args.isArgPresent("--test"))
		{
			TestSuite::test();
			return 0;
		}
#endif


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
		diagnostics_timer = new Timer();
		mem_usage_sampling_timer = new Timer();
		last_update_URL_timer = new Timer();
	
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
#endif
		setGLAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1); // enable MULTISAMPLE
		setGLAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

		int primary_W = 1800;
		int primary_H = 1100;

#if EMSCRIPTEN
		// This seems to return the canvas width and height before it is properly sized to the full window width, so don't bother calling it.
		// emscripten_get_canvas_element_size("#canvas", &primary_W, &primary_H);
#endif

#if EMSCRIPTEN
		const char* window_name = "Substrata Web Client"; // Seems to get used for the web page title
#else
		const char* window_name = "Substrata SDL Client";
#endif
		win = SDL_CreateWindow(window_name, 100, 100, primary_W, primary_H, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
		if(win == nullptr)
			throw glare::Exception("SDL_CreateWindow Error: " + std::string(SDL_GetError()));


		gl_context = SDL_GL_CreateContext(win);
		if(!gl_context)
			throw glare::Exception("OpenGL context could not be created! SDL Error: " + std::string(SDL_GetError()));


		// SDL_GL_SetSwapInterval(0); // Disable Vsync

		//SDL_SetHint("SDL_HINT_MOUSE_RELATIVE_WARP_MOTION", "true");

#if !defined(EMSCRIPTEN)
		gl3wInit();
#endif

		// Create main task manager.
		// This is for doing work like texture compression and EXR loading, that will be created by LoadTextureTasks etc.
		// Alloc these on the heap as Emscripten may have issues with stack-allocated objects before the emscripten_set_main_loop() call.
#if defined(EMSCRIPTEN)
		const size_t main_task_manager_num_threads = myClamp<size_t>(PlatformUtils::getNumLogicalProcessors(), 1, 8);
#else
		const size_t main_task_manager_num_threads = myClamp<size_t>(PlatformUtils::getNumLogicalProcessors(), 1, 512);
#endif
		glare::TaskManager* main_task_manager = new glare::TaskManager("main task manager", main_task_manager_num_threads);
		main_task_manager->setThreadPriorities(MyThread::Priority_Lowest);


		// Create high-priority task manager.
		// For short, processor intensive tasks that the main thread depends on, such as computing animation data for the current frame, or executing Jolt physics tasks.
#if defined(EMSCRIPTEN)
		const size_t high_priority_task_manager_num_threads = myClamp<size_t>(PlatformUtils::getNumLogicalProcessors(), 1, 8);
#else
		const size_t high_priority_task_manager_num_threads = myClamp<size_t>(PlatformUtils::getNumLogicalProcessors(), 1, 512);
#endif
		glare::TaskManager* high_priority_task_manager = new glare::TaskManager("high_priority_task_manager", high_priority_task_manager_num_threads);


		//Reference<glare::Allocator> mem_allocator = new glare::GeneralMemAllocator(/*arena_size_B=*/2 * 1024 * 1024 * 1024ull);
		//Reference<glare::Allocator> mem_allocator = new glare::LimitedAllocator(/*max_size_B=*/1536 * 1024 * 1024ull);
		Reference<glare::Allocator> mem_allocator = new glare::MallocAllocator();


		EXRDecoder::setTaskManager(main_task_manager);


		// Initialise ImGUI
		ImGui::CreateContext();
		ImGui_ImplSDL2_InitForOpenGL(win, gl_context);
		ImGui_ImplOpenGL3_Init();

		// Create OpenGL engine
		OpenGLEngineSettings settings;
		settings.compress_textures = true;
		settings.shadow_mapping = true;
		settings.depth_fog = true;
		settings.render_water_caustics = true;
		settings.allow_multi_draw_indirect = true; // TEMP
		settings.allow_bindless_textures = true; // TEMP
#if defined(EMSCRIPTEN)
		settings.max_tex_CPU_mem_usage = 512 * 1024 * 1024ull;
#endif

		opengl_engine = new OpenGLEngine(settings);

#if defined(EMSCRIPTEN)
		const std::string data_dir = "/data";
#else
		const std::string data_dir = PlatformUtils::getEnvironmentVariable("GLARE_CORE_TRUNK_DIR") + "/opengl";
#endif
		
		opengl_engine->initialise(data_dir, /*texture_server=*/NULL, &print_output, main_task_manager, high_priority_task_manager, mem_allocator);
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


		std::string cache_dir = appdata_path;


		SDLSettingsStore* settings_store = new SDLSettingsStore();


		sdl_ui_interface = new SDLUIInterface();
		sdl_ui_interface->window = win;
		sdl_ui_interface->gl_context = gl_context;
		sdl_ui_interface->gui_client = gui_client;
		sdl_ui_interface->font = font;

		gui_client->initialise(cache_dir, settings_store, sdl_ui_interface, high_priority_task_manager);

		gui_client->afterGLInitInitialise(/*device pixel ratio=*/1.0, /*show minimap=*/false, opengl_engine, font);


		URLParseResults url_parse_results;
#if EMSCRIPTEN
		// Extract URL details to connect to from from webpage URL
		char* location_host_str = getLocationHost();
		const std::string location_host(location_host_str);
		free(location_host_str);

		url_parse_results.hostname = location_host;
		
		char* search_str = getLocationSearch(); // Get the query part of the URL, will be something like "?world=bleh&x=1&y=2&z=3", or just the empty string
		const std::string search(search_str);
		free(search_str);

		if(search.size() >= 1)
		{
			std::map<std::string, std::string> queries = URL::parseQuery(search.substr(1)); // Remove '?' prefix from search string, then parse into keys and values.

			if(queries.count("world"))
				url_parse_results.userpath = queries["world"];

			if(queries.count("x"))
			{
				url_parse_results.x = stringToDouble(queries["x"]);
				url_parse_results.parsed_x = true;
			}
			if(queries.count("y"))
			{
				url_parse_results.y = stringToDouble(queries["y"]);
				url_parse_results.parsed_y = true;
			}
			if(queries.count("z"))
			{
				url_parse_results.z = stringToDouble(queries["z"]);
				url_parse_results.parsed_z = true;
			}
		}
#else
		std::string server_URL = "sub://substrata.info"; // Default URL

		if(parsed_args.isArgPresent("-h"))
		{
			server_URL = "sub://" + parsed_args.getArgStringValue("-h");
		}
		if(parsed_args.isArgPresent("-u"))
		{
			server_URL = parsed_args.getArgStringValue("-u");
		}

		try
		{
			url_parse_results = URLParser::parseURL(server_URL);
		}
		catch(glare::Exception& e) // Handle URL parse failure
		{
			conPrint(e.what());
			sdl_ui_interface->showPlainTextMessageBox("Error parsing URL", e.what());
			return 1;
		}

#endif

		gui_client->connectToServer(url_parse_results);


		//---------------------- Set env material -------------------
		{
			OpenGLMaterial env_mat;
			opengl_engine->setEnvMat(env_mat);
		}
		opengl_engine->setCirrusTexture(opengl_engine->getTexture(base_dir + "/resources/cirrus.exr"));

		
		
		conPrint("Starting main loop...");
#if EMSCRIPTEN
		//emscripten_request_animation_frame_loop(doOneMainLoopIter, 0);

		emscripten_set_main_loop(doOneMainLoopIter, /*fps=*/0, /*simulate_infinite_loop=*/true); // fps 0 to use requestAnimationFrame as recommended.
#else
		while(!quit)
		{
			doOneMainLoopIter();
		}

		SDL_Quit();

		opengl_engine = NULL;
#endif

		conPrint("main finished...");

		high_priority_task_manager->waitForTasksToComplete();
		main_task_manager->waitForTasksToComplete();

		EXRDecoder::clearTaskManager(); // Needs to be shut down before main_task_manager is destroyed as uses it.

		delete high_priority_task_manager;
		delete main_task_manager;

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


static bool do_graphics_diagnostics = false;
static bool do_physics_diagnostics = false;
static bool do_terrain_diagnostics = false;

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
		// Update statistics
		fps = num_frames / stats_timer->elapsed();
		// conPrint("fps: " + doubleToStringNDecimalPlaces(fps, 1));
		stats_timer->reset();
		num_frames = 0;
	}

#if TRACE_ALLOCATIONS
	if(mem_usage_sampling_timer->elapsed() > 0.25f)
	{
		mem_usage_sampling_timer->reset();
		mem_usage_values.push_back((float)MemAlloc::getTotalAllocatedB() / (1024 * 1024));
	}
#endif

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

	if(show_imgui_info_window)
	{
		// Draw ImGUI GUI controls
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame(win);
		ImGui::NewFrame();
		
		//ImGui::ShowDemoWindow();
		
		ImGui::SetNextWindowPos(ImVec2(400, 10), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(600, 900), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowCollapsed(false, ImGuiCond_FirstUseEver);
		if(ImGui::Begin("Info"))
		{
			ImGui::TextColored(ImVec4(1,1,0,1), "Stats");
			ImGui::TextUnformatted(("FPS: " + doubleToStringNDecimalPlaces(fps, 1)).c_str());
		
#if TRACE_ALLOCATIONS
			ImGui::TextUnformatted("mem usage (MB)");
			ImGui::PlotLines("", mem_usage_values.data(), (int)mem_usage_values.size(),
					/*values offset=*/0, /* overlay text=*/NULL, 
				/*scale min=*/0.0, /*scale max=*/std::numeric_limits<float>::max(), 
				/*graph size=*/ImVec2(500, 200));
#endif

			ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
			if(ImGui::CollapsingHeader("Diagnostics"))
			{
				bool diag_changed = false;
				diag_changed = diag_changed || ImGui::Checkbox("graphics", &do_graphics_diagnostics);
				diag_changed = diag_changed || ImGui::Checkbox("physics", &do_physics_diagnostics);
				diag_changed = diag_changed || ImGui::Checkbox("terrain", &do_terrain_diagnostics);

				if((diagnostics_timer->elapsed() > 1.0) || diag_changed)
				{
					double last_timerEvent_CPU_work_elapsed = 0;
					double last_updateGL_time = 0;
					last_diagnostics = gui_client->getDiagnosticsString(do_graphics_diagnostics, do_physics_diagnostics, do_terrain_diagnostics, last_timerEvent_CPU_work_elapsed, last_updateGL_time);
					diagnostics_timer->reset();
				}

				ImGui::TextUnformatted(last_diagnostics.c_str());
			}
		}
		ImGui::End();
		
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}
	
	// Display
	SDL_GL_SwapWindow(win);

	time_since_last_frame->reset();

	// Handle any events
	SDL_Event e;
	while(SDL_PollEvent(&e))
	{
		if(!SDL_GetRelativeMouseMode() && ImGui::GetIO().WantCaptureMouse)
		{
			ImGui_ImplSDL2_ProcessEvent(&e); // Pass event onto ImGUI
			continue;
		}

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
			else if(/*e.window.event == SDL_WINDOWEVENT_RESIZED || */e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
			{
				int w, h;
				SDL_GL_GetDrawableSize(win, &w, &h);

				// conPrint("Got size changed event, SDL drawable size is " + toString(w) + " x " + toString(h));
						
				opengl_engine->setViewportDims(w, h);
				opengl_engine->setMainViewportDims(w, h);

				gui_client->viewportResized(w, h);
			}
		}
		else if(e.type == SDL_KEYDOWN)
		{
			if(e.key.keysym.sym == SDLK_F1)
				show_imgui_info_window = !show_imgui_info_window;

			KeyEvent key_event;
			convertFromSDKKeyEvent(e, key_event);

			gui_client->keyPressed(key_event);
		}
		else if(e.type == SDL_KEYUP)
		{
			KeyEvent key_event;
			convertFromSDKKeyEvent(e, key_event);

			gui_client->keyReleased(key_event);
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
#if !defined(OSX) && !defined(EMSCRIPTEN)
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

#if EMSCRIPTEN
	// Update URL with current camera position
	if(last_update_URL_timer->elapsed() > 0.1)
	{
		std::string url_path = "/webclient?";

		if(!gui_client->server_worldname.empty()) // Append world if != empty string.
			url_path += "world=" + web::Escaping::URLEscape(gui_client->server_worldname) + '&';

		const Vec3d pos = gui_client->cam_controller.getFirstPersonPosition();

		// const heading = floatMod(cam_controller.heading * 180 / Math.PI, 360.0);

		url_path += "x=" + doubleToStringNDecimalPlaces(pos.x, 1) + "&y=" + doubleToStringNDecimalPlaces(pos.y, 1) + "&z=" + doubleToStringNDecimalPlaces(pos.z, 1);
		//  + '&heading=' + heading.toFixed(0);
	
		updateURL(url_path.c_str());

		last_update_URL_timer->reset();
	}
#endif

	num_frames++;
}
