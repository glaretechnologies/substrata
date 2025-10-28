/*=====================================================================
SDLClient.cpp
-------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/


#include "GUIClient.h"
#include "SDLUIInterface.h"
#if EMSCRIPTEN
#include <settings/EmscriptenSettingsStore.h>
#else
#include <settings/RegistrySettingsStore.h>
#include <settings/XMLSettingsStore.h>
#endif
#include "TestSuite.h"
#include "URLParser.h"
#include "ModelLoading.h"
#include "CEF.h"
#include <maths/GeometrySampling.h>
#include <graphics/FormatDecoderGLTF.h>
#include <graphics/MeshSimplification.h>
#include <graphics/TextRenderer.h>
#include <graphics/EXRDecoder.h>
#include <opengl/OpenGLEngine.h>
#include <opengl/RenderStatsWidget.h>
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
#include <utils/LimitedAllocator.h>
#include <utils/ComObHandle.h>
#include <utils/FileInStream.h>
#include <networking/URL.h>
#include <webserver/Escaping.h>
#include <direct3d/Direct3DUtils.h>
#include <GL/gl3w.h>
#include <SDL_opengl.h>
#include <SDL.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl2.h>
#include <string>
#if EMSCRIPTEN
#include <emscripten.h>
#include <emscripten/html5.h>
#include <unistd.h>
#include "emscripten_browser_clipboard.h"
#endif
#include <tracy/Tracy.hpp>
#include <tracy/TracyOpenGL.hpp>
#ifdef _WIN32
#include <d3d11.h>
#include <d3d11_4.h>
#include <mfobjects.h>
#endif


// If we are building on Windows, and we are not in Release mode (e.g. BUILD_TESTS is enabled), then make sure the console window is shown.
// Unfortunately the console window does not stay open if no breakpoint is hit.  The only way I know of fixing this is to manually set the 
// subsystem in the VS project settings (Linker > System > SubSystem)
#if defined(_WIN32) && defined(BUILD_TESTS)
#pragma comment(linker, "/SUBSYSTEM:CONSOLE")
#endif

#if !defined(EMSCRIPTEN)
#define EM_BOOL bool
#endif

static void doOneMainLoopIter();


class SDLClientGLUICallbacks : public GLUICallbacks
{
public:
	SDLClientGLUICallbacks() : sys_cursor_arrow(NULL), sys_cursor_Ibeam(NULL) {}

	virtual void startTextInput()
	{
		//conPrint("startTextInput");
		SDL_StartTextInput();
	}

	virtual void stopTextInput()
	{
		//conPrint("stopTextInput");
		SDL_StopTextInput();
	}

	virtual void setMouseCursor(MouseCursor cursor)
	{
		//conPrint("setMouseCursor");
		if(cursor == MouseCursor_Arrow)
		{
			if(!sys_cursor_arrow)
				sys_cursor_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
			SDL_SetCursor(sys_cursor_arrow);
		}
		else if(cursor == MouseCursor_IBeam)
		{
			if(!sys_cursor_Ibeam)
				sys_cursor_Ibeam = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
			SDL_SetCursor(sys_cursor_Ibeam);
		}
		else
			assert(0);
	}

	SDL_Cursor* sys_cursor_arrow;
	SDL_Cursor* sys_cursor_Ibeam;
};


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

#if defined(_WIN32)
ComObHandle<ID3D11Device> d3d_device;
ComObHandle<IMFDXGIDeviceManager> device_manager;
#endif

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

Reference<RenderStatsWidget> CPU_render_stats_widget;
Reference<RenderStatsWidget> GPU_render_stats_widget;


static double cur_canvas_css_W = 800; // Current device-independent pixel width.  Canvas element css width in WebGL.
static double cur_canvas_css_H = 800;

#if EMSCRIPTEN

// Define getLocationHost() function
EM_JS(char*, getLocationHost, (), {
	return stringToNewUTF8(window.location.host);
});

// Define getLocationSearch() function
// Get the ?a=b part of the URL (see https://developer.mozilla.org/en-US/docs/Web/API/Location)
EM_JS(char*, getLocationSearch, (), {
	return stringToNewUTF8(window.location.search);
});

// Define updateURL(const char* new_URL) function
EM_JS(void, updateURL, (const char* new_URL), {
	history.replaceState(null, "",  UTF8ToString(new_URL)); // See https://developer.mozilla.org/en-US/docs/Web/API/History/replaceState
});

// Define getUserAgentString() function
EM_JS(char*, getUserAgentString, (), {
	return stringToNewUTF8(window.navigator.userAgent);
});

// From https://groups.google.com/g/angleproject/c/0ZuTYrgaXYw/m/UNdsgYLLCgAJ
EM_JS(char*, getTranslatedShaderSource, (int32_t nm), {
	var ext = GLctx.getExtension('WEBGL_debug_shaders');
	if (ext) {
		if (GL.shaders[nm]) {
			var jsString = ext.getTranslatedShaderSource(GL.shaders[nm]);
			var lengthBytes = lengthBytesUTF8(jsString) + 1;
			var stringOnWasmHeap = _malloc(lengthBytes);
			stringToUTF8(jsString, stringOnWasmHeap, lengthBytes);
			return stringOnWasmHeap;
		}
		else {
			return 0;
		}
	}
	else {
		return 0;
	}
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
		const std::string base_dir = "";
#else
		const std::string base_dir = PlatformUtils::getResourceDirectoryPath();
#endif

		// NOTE: this code is also in MainWindow.cpp
#if defined(_WIN32)
		const std::string font_path       = PlatformUtils::getFontsDirPath() + "/Segoeui.ttf"; // SegoeUI is shipped with Windows 7 onwards: https://learn.microsoft.com/en-us/typography/fonts/windows_7_font_list
		const std::string emoji_font_path = PlatformUtils::getFontsDirPath() + "/Seguiemj.ttf";
#elif defined(__APPLE__)
		const std::string font_path       = "/System/Library/Fonts/SFNS.ttf";
		const std::string emoji_font_path = "/System/Library/Fonts/SFNS.ttf";
#else
		// Linux:
		const std::string font_path       = base_dir + "/data/resources/TruenoLight-E2pg.otf";
		const std::string emoji_font_path = base_dir + "/data/resources/TruenoLight-E2pg.otf"; 
#endif

		TextRendererRef text_renderer = new TextRenderer();

		TextRendererFontFaceSizeSetRef fonts       = new TextRendererFontFaceSizeSet(text_renderer, font_path);
		TextRendererFontFaceSizeSetRef emoji_fonts = new TextRendererFontFaceSizeSet(text_renderer, emoji_font_path);


		timer = new Timer();
		time_since_last_frame = new Timer();
		stats_timer = new Timer();
		diagnostics_timer = new Timer();
		mem_usage_sampling_timer = new Timer();
		last_update_URL_timer = new Timer();

		const std::string appdata_path = PlatformUtils::getOrCreateAppDataDirectory("Cyberspace");

#if EMSCRIPTEN
		Reference<EmscriptenSettingsStore> settings_store = new EmscriptenSettingsStore();
#elif defined(_WIN32)
		Reference<RegistrySettingsStore> settings_store = new RegistrySettingsStore("Glare Technologies", "Cyberspace");
#else
		Reference<XMLSettingsStore> settings_store = new XMLSettingsStore(appdata_path + "/settings_store.xml");
#endif
		


#if EMSCRIPTEN
		const double device_pixel_ratio = emscripten_get_device_pixel_ratio();
#else
		const double device_pixel_ratio = 1.0;
#endif
		printVar(device_pixel_ratio);


		//=========================== Init SDL and OpenGL ================================
		SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas"); // Target the canvas element

		if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0)
			throw glare::Exception("SDL_Init Error: " + std::string(SDL_GetError()));


		// Set GL attributes, needs to be done before window creation.
#if EMSCRIPTEN
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		setGLAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);

		SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8); // Enable alpha channel for video alpha cutout

#elif defined(__APPLE__)
		setGLAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4); // We need to request a specific version for a core profile.
		setGLAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
		setGLAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#else
		setGLAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4); // We need to request a specific version for a core profile.
		setGLAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
		setGLAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif

#if EMSCRIPTEN
		// device_pixel_ratio > 1 is probably a mobile device
		const bool low_memory_mode = device_pixel_ratio > 1.0;
#else
		const bool low_memory_mode = false;
#endif
		conPrint("Using low memory mode: " + boolToString(low_memory_mode));


#if EMSCRIPTEN
		// In theory, since we are not doing hi dpi rendering, we can have MSAA on.
		// However MSAA causes iPad to give context lost errors, so just disable if device_pixel_ratio != 1.
		const bool use_MSAA = !low_memory_mode;
#else
		const bool use_MSAA = settings_store->getBoolValue("setting/MSAA", /*default value=*/true);
#endif
		conPrint("Using MSAA: " + boolToString(use_MSAA));
		if(use_MSAA)
		{
			setGLAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1); // Enable MULTISAMPLE
			setGLAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
		}
		else
		{
			setGLAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0); // Disable MULTISAMPLE
			setGLAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
		}

		int primary_W = 1800;
		int primary_H = 1100;

#if EMSCRIPTEN
		// This seems to return the canvas width and height before it is properly sized to the full window width (e.g. is 300x150px), so don't bother calling it.
		// emscripten_get_canvas_element_size("#canvas", &primary_W, &primary_H);
		
		primary_W = 256; // Use small resolution in case these values are used, in which case we don't want to allocate a massive buffer that then gets thrown away.
		primary_H = 256;
#endif

#if EMSCRIPTEN
		const char* window_name = "Substrata Web Client"; // Seems to get used for the web page title
#else
		const char* window_name = "Substrata SDL Client";
#endif
		// SDL_WINDOW_ALLOW_HIGHDPI results in 1:1 drawable (viewport) pixels to device/hardware pixels.
		// This is too high res for mobile - rendering is too slow.  So just leave it off for now.
		win = SDL_CreateWindow(window_name, 600, 100, primary_W, primary_H, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
		if(win == nullptr)
			throw glare::Exception("SDL_CreateWindow Error: " + std::string(SDL_GetError()));

		cur_canvas_css_W = primary_W;
		cur_canvas_css_H = primary_H;

#if EMSCRIPTEN
		EmscriptenWebGLContextAttributes attrs;
		emscripten_webgl_init_context_attributes(&attrs);

		attrs.premultipliedAlpha = EM_FALSE; // Disable premultiplied alpha, to get desired blending behaviour for our alpha=0 cutout regions for video playing on web.
		attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE; // This is required, otherwise we get a "WebGL: lost context" error on iPad.

		EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context("canvas", &attrs);
		if(ctx == 0)
			throw glare::Exception("emscripten_webgl_create_context failed.");

		emscripten_webgl_make_context_current(ctx);
		//gl_context = (SDL_GLContext)ctx;
		//SDL_GL_MakeCurrent(win, gl_context);

		gl_context = SDL_GL_CreateContext(win);
		if(!gl_context)
			throw glare::Exception("OpenGL context could not be created! SDL Error: " + std::string(SDL_GetError()));
#else
		gl_context = SDL_GL_CreateContext(win);
		if(!gl_context)
			throw glare::Exception("OpenGL context could not be created! SDL Error: " + std::string(SDL_GetError()));
#endif


		// SDL_GL_SetSwapInterval(0); // Disable Vsync

		//SDL_SetHint("SDL_HINT_MOUSE_RELATIVE_WARP_MOTION", "true");


		SDL_GameController* game_controller = nullptr;
		if(SDL_NumJoysticks() < 1)
		{
			conPrint("No joysticks / gamepads connected!\n");
		}
		else
		{
			// Load joystick
			game_controller = SDL_GameControllerOpen(/*device index=*/0);
			if(game_controller == nullptr)
				conPrint("Warning: Unable to open game controller! SDL Error: " + std::string(SDL_GetError()));
		}


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
		Reference<glare::Allocator> mem_allocator = new glare::MallocAllocator();
		Reference<glare::Allocator> worker_allocator = new glare::LimitedAllocator(/*max_size_B=*/512 * 1024 * 1024ull);
		//Reference<glare::Allocator> mem_allocator = new glare::MallocAllocator();


		EXRDecoder::setTaskManager(main_task_manager);

		bool on_apple_device = false;
#if defined(EMSCRIPTEN)
		char* user_agent_str = getUserAgentString();
		const std::string user_agent(user_agent_str);
		free(user_agent_str);

		conPrint("user_agent: " + user_agent);
		on_apple_device = StringUtils::containsString(user_agent, "Mac OS") || StringUtils::containsString(user_agent, "iPhone OS");
#else
		#if defined(__APPLE__)
		on_apple_device = true;
		#endif
#endif
		printVar(on_apple_device);


		// Initialise ImGUI
		ImGui::CreateContext();
		ImGui_ImplSDL2_InitForOpenGL(win, gl_context);
		ImGui_ImplOpenGL3_Init();

		// Create OpenGL engine
		OpenGLEngineSettings settings;
		settings.compress_textures = true;
		settings.shadow_mapping = true;
		settings.depth_fog = true;
		settings.render_water_caustics = !low_memory_mode;
		settings.msaa_samples = use_MSAA ? 4 : 1;
		settings.render_to_offscreen_renderbuffers = !low_memory_mode;
		settings.ssao_support = false;
		settings.ssao = false;

		if(parsed_args.isArgPresent("--no_MDI"))
			settings.allow_multi_draw_indirect = false;
		if(parsed_args.isArgPresent("--no_bindless"))
			settings.allow_bindless_textures = false;

		if(on_apple_device)
			settings.use_multiple_phong_uniform_bufs = true; // Work around Mac OpenGL bug with changing the phong uniform buffer between rendering batches (see https://issues.chromium.org/issues/338348430)

#if defined(EMSCRIPTEN)
		settings.max_tex_CPU_mem_usage = 512 * 1024 * 1024ull;
#endif
		if(low_memory_mode)
			settings.shadow_mapping_detail = OpenGLEngineSettings::ShadowMappingDetail_low;

		opengl_engine = new OpenGLEngine(settings);

		
		std::string data_dir = "/data";
#if !defined(EMSCRIPTEN)
		if(PlatformUtils::isEnvironmentVariableDefined("GLARE_CORE_TRUNK_DIR"))
			data_dir = PlatformUtils::getEnvironmentVariable("GLARE_CORE_TRUNK_DIR") + "/opengl";
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




		gui_client = new GUIClient(base_dir, appdata_path, parsed_args);
		gui_client->opengl_engine = opengl_engine;

		// Don't use lightmaps on mobile devices for now, to reduce RAM usage.
		if(low_memory_mode)
			gui_client->use_lightmaps = false;

		std::string cache_dir = appdata_path;


		sdl_ui_interface = new SDLUIInterface();
		sdl_ui_interface->window = win;
		sdl_ui_interface->gl_context = gl_context;
		sdl_ui_interface->gui_client = gui_client;
		sdl_ui_interface->game_controller = game_controller;
		sdl_ui_interface->appdata_path = appdata_path;
		sdl_ui_interface->settings_store = settings_store;
		sdl_ui_interface->d3d11_device = nullptr;


		gui_client->preConnectInitialise(cache_dir, settings_store, sdl_ui_interface, high_priority_task_manager, worker_allocator);


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
			const std::map<std::string, std::string> queries = URL::parseQuery(search.substr(1)); // Remove '?' prefix from search string, then parse into keys and values.

			URLParser::processQueryKeyValues(queries, url_parse_results);
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

		gui_client->postConnectInitialise();

#ifdef _WIN32
		// Create a GPU device.  Needed to get hardware accelerated video decoding and for hardware texture sharing for CEF.
		Direct3DUtils::createGPUDeviceAndMFDeviceManager(d3d_device, device_manager);

		sdl_ui_interface->d3d11_device = (void*)d3d_device.ptr;
#endif //_WIN32


		CEF::initialiseCEF(base_dir, appdata_path);

		// NOTE: use 1 for device_pixel_ratio as we are not doing high DPI rendering.
		gui_client->afterGLInitInitialise(/*device_pixel_ratio*/1.f, opengl_engine, fonts, emoji_fonts);

		gui_client->gl_ui->callbacks = new SDLClientGLUICallbacks();

		// A high device_pixel_ratio indicates the UI is going to be very small unless it is scaled up a bit.
		if(device_pixel_ratio > 2.f)
			gui_client->gl_ui->setUIScale((float)device_pixel_ratio / 2.f);





#if EMSCRIPTEN
		// Stop SDL from accepting Ctrl+V events, so that paste events will be properly triggered.
		EM_ASM({
			window.addEventListener('keydown', function(event){
				if (event.ctrlKey && event.key == 'v')
					event.stopImmediatePropagation();
			}, true);
		});

		// Set paste event callback.  The lambda function will execute when a paste action is detected in the browser.
		emscripten_browser_clipboard::paste([](const std::string& paste_data, void* /*callback_data*/){
			TextInputEvent text_input_event;
			text_input_event.text = paste_data;
			gui_client->gl_ui->handleTextInputEvent(text_input_event);
		});
#endif


		//---------------------- Set env material -------------------
		{
			OpenGLMaterial env_mat;
			opengl_engine->setEnvMat(env_mat);
		}
		opengl_engine->setCirrusTexture(opengl_engine->getTexture(base_dir + "/data/resources/cirrus.exr"));


#if EMSCRIPTEN
		// Just disable bloom on mobile devices, is not working properly, probably due to lack of floating point buffer formats or something similar.
		const bool bloom = device_pixel_ratio <= 1.0;
#else
		const bool bloom = settings_store->getBoolValue("setting/bloom", /*default val=*/true);
#endif
		conPrint("Bloom enabled: " + boolToString(bloom));
		if(bloom)
			opengl_engine->getCurrentScene()->bloom_strength = 0.3f;

		opengl_engine->getCurrentScene()->draw_aurora = true;

		
		
		conPrint("Starting main loop...");
#if EMSCRIPTEN
		//emscripten_request_animation_frame_loop(doOneMainLoopIter, 0);

		emscripten_set_main_loop(doOneMainLoopIter, /*fps=*/0, /*simulate_infinite_loop=*/true); // fps 0 to use requestAnimationFrame as recommended.
#else
		while(!quit)
		{
			doOneMainLoopIter();
		}
#endif

		conPrint("main finished...");

		gui_client->shutdown();
		delete gui_client;
		gui_client = NULL;

		CEF::shutdownCEF();

		CPU_render_stats_widget = NULL;
		GPU_render_stats_widget = NULL;

		opengl_engine = NULL;

		high_priority_task_manager->waitForTasksToComplete();
		main_task_manager->waitForTasksToComplete();

		EXRDecoder::clearTaskManager(); // Needs to be shut down before main_task_manager is destroyed as uses it.

		delete high_priority_task_manager;
		delete main_task_manager;


		GUIClient::staticShutdown();

		SDL_Quit();
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


static Vec2f GLCoordsForGLWidgetPos(OpenGLEngine& gl_engine, const Vec2f widget_pos, double device_pixel_ratio)
{
	const int vp_width  = gl_engine.getViewPortWidth();
	const int vp_height = gl_engine.getViewPortHeight();

	const double use_vp_width  = vp_width  / device_pixel_ratio;
	const double use_vp_height = vp_height / device_pixel_ratio;

	return Vec2f(
		(float)( (widget_pos.x - use_vp_width /2) / (use_vp_width /2)),
		(float)(-(widget_pos.y - use_vp_height/2) / (use_vp_height/2))
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
		case SDLK_RETURN: return Key_Return;
		case SDLK_KP_ENTER: return Key_Enter;
		case SDLK_LEFTBRACKET: return Key_LeftBracket;
		case SDLK_RIGHTBRACKET: return Key_RightBracket;
		case SDLK_PAGEUP: return Key_PageUp;
		case SDLK_PAGEDOWN: return Key_PageDown;
		case SDLK_HOME: return Key_Home;
		case SDLK_END: return Key_End;
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


static uint32 convertSDLMouseButtonState(uint32 sdl_state)
{
	uint32 res = 0;
	if(sdl_state & SDL_BUTTON_LMASK) res |= (uint32)MouseButton::Left;
	if(sdl_state & SDL_BUTTON_MMASK) res |= (uint32)MouseButton::Middle;
	if(sdl_state & SDL_BUTTON_RMASK) res |= (uint32)MouseButton::Right;
	return res;
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

static void convertFromSDLTextInputEvent(SDL_Event ev, TextInputEvent& text_input_event)
{
	text_input_event.text = std::string(ev.text.text);
}


static bool do_graphics_diagnostics = false;
static bool do_physics_diagnostics = false;
static bool do_terrain_diagnostics = false;
static bool show_frame_time_graphs = false;

static size_t last_total_memory = 0;
static uintptr_t last_dynamic_top = 0;

static double last_timerEvent_CPU_work_elapsed = 0;
double last_updateGL_time = 0;

static bool doing_cam_rotate_mouse_drag = false; // Is the mouse pointer hidden, and will moving the mouse rotate the camera?
// TODO: replace with ui_interface->getCamRotationOnMouseDragEnabled

static bool have_received_input = false;
static bool tried_initialise_audio_engine = false;


static void doOneMainLoopIter()
{
	Timer loop_iter_timer;


#ifdef EMSCRIPTEN
	// Web browsers need to wait for an input gesture is completed before trying to play sounds.
	if(have_received_input && !tried_initialise_audio_engine)
	{
		gui_client->initAudioEngine();
		tried_initialise_audio_engine = true;
	}
#endif

	CEF::doMessageLoopWork();


	if(SDL_GL_MakeCurrent(win, gl_context) != 0)
		conPrint("SDL_GL_MakeCurrent failed.");


#if 0 // EMSCRIPTEN // Print when memory size increases
	const size_t total_memory = (size_t)EM_ASM_PTR(return HEAP8.length);
	const uintptr_t dynamic_top = (uintptr_t)sbrk(0);
	if(total_memory != last_total_memory)
	{
		conPrint("************* total_memory increased to " + ::getMBSizeString(total_memory));
		last_total_memory = total_memory;
	}
	if(dynamic_top != last_dynamic_top)
	{
		conPrint("************* dynamic_top increased to " + ::getMBSizeString((size_t)dynamic_top));
		last_dynamic_top = dynamic_top;
	}
#endif

	double canvas_drawable_pixels_per_css_pixels = (double)opengl_engine->getMainViewPortWidth() / cur_canvas_css_W;

	// Handle any events
	SDL_Event e;
	while(SDL_PollEvent(&e))
	{
		if(show_imgui_info_window)
			ImGui_ImplSDL2_ProcessEvent(&e); // Pass event onto ImGUI

		const bool imgui_captures_mouse_ev    = show_imgui_info_window && ImGui::GetIO().WantCaptureMouse;
		const bool imgui_captures_keyboard_ev = show_imgui_info_window && ImGui::GetIO().WantCaptureKeyboard;

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

				conPrint("Got size changed event, SDL drawable size is " + toString(w) + " x " + toString(h));
						
				opengl_engine->setViewportDims(w, h);
				opengl_engine->setMainViewportDims(w, h);

#if EMSCRIPTEN
				emscripten_get_element_css_size("canvas", &cur_canvas_css_W, &cur_canvas_css_H);
				// conPrint("emscripten_get_element_css_size returned " + toString(cur_canvas_css_W) + " x " + toString(cur_canvas_css_H));
#else
				cur_canvas_css_W = w;
				cur_canvas_css_H = h;
#endif

				canvas_drawable_pixels_per_css_pixels = (double)opengl_engine->getMainViewPortWidth() / cur_canvas_css_W;

				gui_client->gl_ui->setCurrentDevicePixelRatio((float)canvas_drawable_pixels_per_css_pixels); // Use the actual computed device pixel ratio for the canvas element.

				gui_client->viewportResized(w, h); // Do this last so UI will reposition
			}
		}
		else if(e.type == SDL_KEYDOWN)
		{
			if(e.key.keysym.sym == SDLK_F1 || e.key.keysym.sym == SDLK_F2)
				show_imgui_info_window = !show_imgui_info_window;

			if(e.key.keysym.sym == SDLK_F3)
			{
				// Dump out the translated shader source (e.g. ANGLE's HLSL output)
#if EMSCRIPTEN
				for(int i=0; i<100; ++i)
				{
					char* translated_code = getTranslatedShaderSource(i);
					if(translated_code)
					{
						conPrint("shader " + toString(i) + " translated_code:");
						conPrint(std::string(translated_code));
						free(translated_code);
					}
					else
						conPrint("translated_code was NULL");
				}
#endif
			}
			else if(e.key.keysym.sym == SDLK_F5)
			{
#if EMSCRIPTEN
				EM_ASM( window.location.reload(); ); // Just passing F5 on to the browser doesn't trigger a reload.  Need to call this.
				continue;
#endif
			}

			if(!imgui_captures_keyboard_ev)
			{
				KeyEvent key_event;
				convertFromSDKKeyEvent(e, key_event);

				if(key_event.key == Key_X && (key_event.modifiers & (uint32)Modifiers::Ctrl)) // Check for cut command
				{
					//conPrint("CTRL + X detected");
					std::string new_clipboard_contents;
					gui_client->gl_ui->handleCutEvent(new_clipboard_contents);
#if EMSCRIPTEN
					emscripten_browser_clipboard::copy(new_clipboard_contents);
#else
					SDL_SetClipboardText(new_clipboard_contents.c_str());
#endif
				}
				else if(key_event.key == Key_C && (key_event.modifiers & (uint32)Modifiers::Ctrl)) // Check for copy command
				{
					//conPrint("CTRL + C detected");
					std::string new_clipboard_contents;
					gui_client->gl_ui->handleCopyEvent(new_clipboard_contents);
#if EMSCRIPTEN
					emscripten_browser_clipboard::copy(new_clipboard_contents);
#else
					SDL_SetClipboardText(new_clipboard_contents.c_str());
#endif
				}
				else if(key_event.key == Key_V && (key_event.modifiers & (uint32)Modifiers::Ctrl)) // Check for paste command
				{
					TextInputEvent text_input_event;
					char* keyboard_text = SDL_GetClipboardText(); // Caller must call SDL_free() on the returned pointer when done with it
					text_input_event.text = std::string(keyboard_text);
					SDL_free(keyboard_text);

					gui_client->gl_ui->handleTextInputEvent(text_input_event);
				}
				else
					gui_client->keyPressed(key_event);
			}
		}
		else if(e.type == SDL_KEYUP)
		{
			if(!imgui_captures_keyboard_ev)
			{
				KeyEvent key_event;
				convertFromSDKKeyEvent(e, key_event);

				gui_client->keyReleased(key_event);
			}

			have_received_input = true;
		}
		else if(e.type == SDL_TEXTINPUT)
		{
			TextInputEvent text_input_event;
			convertFromSDLTextInputEvent(e, text_input_event);

			gui_client->gl_ui->handleTextInputEvent(text_input_event);
		}
		else if(e.type == SDL_MOUSEMOTION)
		{
			// conPrint("SDL_MOUSEMOTION, pos: " + Vec2i(e.motion.x, e.motion.y).toString());
			if(!imgui_captures_mouse_ev)
			{
				if(doing_cam_rotate_mouse_drag && (e.motion.state & SDL_BUTTON_LMASK))
				{
					Vec2i delta(e.motion.xrel, -e.motion.yrel);

					// conPrint("delta: " + delta.toString());

#if EMSCRIPTEN
					const double speed_factor = 1.0; // This gives similar results in a web browser to the 0.35 below.
#else
					const double speed_factor = 0.35; // To make delta similar to what Qt gives.
#endif
					gui_client->cam_controller.updateRotation(/*pitch_delta=*/delta.y * speed_factor, /*heading_delta=*/delta.x * speed_factor);

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
				move_event.gl_coords = GLCoordsForGLWidgetPos(*opengl_engine, Vec2f((float)e.motion.x, (float)e.motion.y), canvas_drawable_pixels_per_css_pixels);
				move_event.modifiers = convertSDLModifiers(SDL_GetModState());
				move_event.button_state = convertSDLMouseButtonState(e.motion.state);
				gui_client->mouseMoved(move_event);
			}
		}
		else if(e.type == SDL_MOUSEBUTTONDOWN)
		{
			if(!imgui_captures_mouse_ev)
			{
				// conPrint("SDL_MOUSEBUTTONDOWN, pos: " + Vec2i(e.button.x, e.button.y).toString() + ", clicks: " + toString(e.button.clicks));

				SDL_GetMouseState(&mouse_move_origin.x, &mouse_move_origin.y);

				MouseEvent mouse_event;
				mouse_event.cursor_pos = Vec2i(e.button.x, e.button.y);
				mouse_event.gl_coords = GLCoordsForGLWidgetPos(*opengl_engine, Vec2f((float)e.button.x, (float)e.button.y), canvas_drawable_pixels_per_css_pixels);
				mouse_event.button = convertSDLMouseButton(e.button.button);
				mouse_event.modifiers = convertSDLModifiers(SDL_GetModState());

				if(e.button.clicks == 1) // Single click:
				{
					gui_client->mousePressed(mouse_event);
					if(!mouse_event.accepted)
					{
						//conPrint("Entering relative mouse mode...");
						SDL_SetRelativeMouseMode(SDL_TRUE); // Hide mouse cursor and constrain to window and report relative mouse motion.
						doing_cam_rotate_mouse_drag = true;
					}
				}
				else if(e.button.clicks == 2) // Double click:
				{
					gui_client->mouseDoubleClicked(mouse_event);
				}
			}
		}
		else if(e.type == SDL_MOUSEBUTTONUP)
		{
			if(!imgui_captures_mouse_ev)
			{
				MouseEvent mouse_event;
				mouse_event.cursor_pos = Vec2i(e.button.x, e.button.y);
				mouse_event.gl_coords = GLCoordsForGLWidgetPos(*opengl_engine, Vec2f((float)e.button.x, (float)e.button.y), canvas_drawable_pixels_per_css_pixels);
				mouse_event.button = convertSDLMouseButton(e.button.button);
				mouse_event.modifiers = convertSDLModifiers(SDL_GetModState());
				gui_client->gl_ui->handleMouseRelease(mouse_event);

				// conPrint("SDL_MOUSEBUTTONUP, pos: " + Vec2i(e.button.x, e.button.y).toString() + ", clicks: " + toString(e.button.clicks));

				if(e.button.clicks == 1) // Single click:
				{
					SDL_SetRelativeMouseMode(SDL_FALSE);
					doing_cam_rotate_mouse_drag = false;
				}
			}

			have_received_input = true;
		}
		else if(e.type == SDL_MOUSEWHEEL)
		{
			if(!imgui_captures_mouse_ev)
			{
				MouseWheelEvent wheel_event;
				wheel_event.cursor_pos = Vec2i(e.wheel.mouseX, e.wheel.mouseY);
				wheel_event.gl_coords = GLCoordsForGLWidgetPos(*opengl_engine, Vec2f((float)e.wheel.mouseX, (float)e.wheel.mouseY), canvas_drawable_pixels_per_css_pixels);
				wheel_event.angle_delta = Vec2f(e.wheel.preciseX, e.wheel.preciseY) * 15.f; // Most mouse wheels have 15 degree increments, preciseY seems to be -1 or 1.
				wheel_event.modifiers = convertSDLModifiers(SDL_GetModState());
				gui_client->onMouseWheelEvent(wheel_event);
			}
		}
	}


	MouseCursorState mouse_cursor_state;
	{
		SDL_GetMouseState(&mouse_cursor_state.cursor_pos.x, &mouse_cursor_state.cursor_pos.y); // Get mouse cursor pos
		mouse_cursor_state.gl_coords = GLCoordsForGLWidgetPos(*opengl_engine, Vec2f((float)mouse_cursor_state.cursor_pos.x, (float)mouse_cursor_state.cursor_pos.y), canvas_drawable_pixels_per_css_pixels);
	
		const SDL_Keymod mod_state = SDL_GetModState();
		mouse_cursor_state.ctrl_key_down = (mod_state & KMOD_CTRL) != 0;
		mouse_cursor_state.alt_key_down  = (mod_state & KMOD_ALT)  != 0;
	}
	
	try
	{
		gui_client->timerEvent(mouse_cursor_state);
	}
	catch(glare::Exception& e)
	{
		conPrint("ERROR: Excep while calling gui_client->timerEvent(): " + e.what());
	}

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

	last_timerEvent_CPU_work_elapsed = loop_iter_timer.elapsed(); // Everything before graphics draw

	Timer drawing_timer;

	int gl_w, gl_h;
	SDL_GL_GetDrawableSize(win, &gl_w, &gl_h);
	if(gl_w > 0 && gl_h > 0)
	{
		// Work out current camera transform
		Matrix4f world_to_camera_space_matrix;
		gui_client->cam_controller.getWorldToCameraMatrix(world_to_camera_space_matrix);

		const float near_draw_dist = 0.22f;
		const float max_draw_dist = 100000.f;

		const float sensor_width = sensorWidth();
		const float lens_sensor_dist = (float)gui_client->cam_controller.lens_sensor_dist;
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
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();
		
		//ImGui::ShowDemoWindow();
		
		ImGui::SetNextWindowPos(ImVec2(400, 10), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(600, 900), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowCollapsed(false, ImGuiCond_FirstUseEver);
		if(ImGui::Begin("Info"))
		{
			// ImGui::InputFloat("proj_len_viewable_threshold", &proj_len_viewable_threshold, 0.0001f, 0.0001f, "%.5f");

			ImGui::TextColored(ImVec4(1,1,0,1), "Stats");
			ImGui::TextUnformatted(("FPS: " + doubleToStringNDecimalPlaces(fps, 1)).c_str());
		
#if TRACE_ALLOCATIONS
			ImGui::TextUnformatted("mem usage (MB)");
			ImGui::PlotLines("mem usage", mem_usage_values.data(), (int)mem_usage_values.size(),
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
					last_diagnostics = gui_client->getDiagnosticsString(do_graphics_diagnostics, do_physics_diagnostics, do_terrain_diagnostics, last_timerEvent_CPU_work_elapsed, last_updateGL_time);
					diagnostics_timer->reset();
				}

				//--------------------------------
				if(ImGui::Checkbox("show frame time graphs", &show_frame_time_graphs))
				{
					if(show_frame_time_graphs && !CPU_render_stats_widget)
					{
						opengl_engine->setProfilingEnabled(true);

						CPU_render_stats_widget = new RenderStatsWidget(opengl_engine, gui_client->gl_ui, /*widget index=*/0);
						GPU_render_stats_widget = new RenderStatsWidget(opengl_engine, gui_client->gl_ui, /*widget index=*/1);
					}
					else if(!show_frame_time_graphs && CPU_render_stats_widget.nonNull())
					{
						opengl_engine->setProfilingEnabled(false);

						CPU_render_stats_widget = nullptr;
						GPU_render_stats_widget = nullptr;
					}
				}
				//--------------------------------

				ImGui::TextUnformatted(last_diagnostics.c_str());
			}
		}
		ImGui::End();
		
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}

	// Plot the total time spent on CPU work this frame.
	// Note that we can't just measure the time of this timerEvent method with glWidget->updateGL(), because updateGL() will block for vsync, so it will include a lot of waiting time.
	// Instead use the sum of work time in this method plus the work time in OpenGLEngine::draw().
	if(CPU_render_stats_widget)
		CPU_render_stats_widget->addFrameTime((float)(last_timerEvent_CPU_work_elapsed + opengl_engine->last_draw_CPU_time));

	if(GPU_render_stats_widget)
		GPU_render_stats_widget->addFrameTime((float)opengl_engine->last_total_draw_GPU_time);
	
	// Display
	SDL_GL_SwapWindow(win);
	FrameMark; // Tracy profiler

	last_updateGL_time = drawing_timer.elapsed();

	time_since_last_frame->reset();

	

#if EMSCRIPTEN
	// Update URL with current camera position
	// We can't do this too often or we will get an "Attempt to use history.replacestate() more than 100 times per 30 seconds" error in Safari.
	if(last_update_URL_timer->elapsed() > 0.5)
	{
		const std::string url_path = gui_client->getCurrentWebClientURLPath();
	
		updateURL(url_path.c_str());

		last_update_URL_timer->reset();
	}
#endif

	num_frames++;
}


static std::string sanitiseString(const std::string& s)
{
	std::string res = s;
	for(size_t i=0; i<s.size(); ++i)
		if(!::isAlphaNumeric(s[i]))
			res[i] = '_';
	return res;
}


// processAvatarModelFile is called from JS code in webclient.html.
extern "C" 
#if EMSCRIPTEN
EMSCRIPTEN_KEEPALIVE
#endif
void processAvatarModelFile(unsigned char* data, int length, const char* filename_)
{
	const std::string filename(filename_);

	// conPrint("In C++ processAvatarModelFile: " + toString(length) + ", filename: '" + filename + "'");

	// conPrint("--Content--");
	// conPrint(std::string(data, data + length));
	// conPrint("--end Content--");

	std::string model_path;
	if(!filename.empty())
	{
		model_path = "/tmp/" + sanitiseString(removeDotAndExtension(filename)) + "_2." + getExtension(filename);
		FileUtils::writeEntireFile(model_path, (const char*)data, length);
	}

	// Try and load model
	try
	{
		ModelLoading::MakeGLObjectResults results;
		if(!model_path.empty())
		{
			ModelLoading::makeGLObjectForModelFile(*gui_client->opengl_engine, *gui_client->opengl_engine->vert_buf_allocator, /*allocator=*/nullptr, model_path, /*do_opengl_stuff=*/false,
				results
			);
		}
		else
		{
			// We will be using the default Xbot model
			results.ob_to_world = Matrix4f::rotationAroundXAxis(Maths::pi_2<float>()); // If we used the default xbot bmesh we need to rotate it upright.
			
			results.materials.resize(2);
			results.materials[0] = new WorldMaterial();
			results.materials[0]->colour_rgb = Avatar::defaultMat0Col();
			results.materials[0]->metallic_fraction.val = Avatar::default_mat0_metallic_frac;
			results.materials[0]->roughness.val = Avatar::default_mat0_roughness;

			results.materials[1] = new WorldMaterial();
			results.materials[1]->colour_rgb = Avatar::defaultMat1Col();
			results.materials[1]->metallic_fraction.val = Avatar::default_mat1_metallic_frac;
		}

		// NOTE: a bunch of this code is duplicated from AvatarSettingsDialog.

		Vec4f original_toe_pos = results.batched_mesh ? results.batched_mesh->animation_data.getNodePositionModelSpace("LeftToe_End", /*use_retarget_adjustment=*/false) : Vec4f(0,0.0362269469f,0,1);

		// TEMP: Load animation data for ready-player-me type avatars
		float foot_bottom_height = original_toe_pos[1] - 0.0362269469f; // Should be ~= 0

		bool do_retargetting = true;
#if EMSCRIPTEN
		do_retargetting = gui_client->extracted_anim_data_loaded;
#endif

		if(do_retargetting && results.batched_mesh)
		{
#if EMSCRIPTEN
			FileInStream file("/extracted_avatar_anim.bin");
#else
			FileInStream file(gui_client->resources_dir_path + "/extracted_avatar_anim.bin");
#endif
			// Do retargetting on a copy as we don't want to send the animation data with all animations from extracted_avatar_anim.bin to the server when uploading the mesh.
			AnimationData animation_data_copy = results.batched_mesh->animation_data;
			animation_data_copy.loadAndRetargetAnim(file);

			Vec4f new_toe_pos = animation_data_copy.getNodePositionModelSpace("LeftToe_End", /*use_retarget_adjustment=*/true);
			conPrint("new_toe_pos: " + new_toe_pos.toStringNSigFigs(4));

			foot_bottom_height = new_toe_pos[1] - 0.03f; // Height of foot bottom for avatar with retargetted animation, off ground.

			conPrint("foot_bottom_height: " + doubleToStringNSigFigs(foot_bottom_height, 4));
		}


		// Construct transformation to bring ready-player-me avatars to z-up and standing on the ground.
		// We want to translate the avatar down from 1.67 metres in the sky (which is the default substrata eye height), to the ground
		const Matrix4f pre_ob_to_world_matrix = Matrix4f::translationMatrix(0, 0, -1.67f - foot_bottom_height) * results.ob_to_world;

		// conPrint("processAvatarModelFile(): pre_ob_to_world_matrix: " + pre_ob_to_world_matrix.toString());

		gui_client->updateOurAvatarModel(results.batched_mesh, model_path, pre_ob_to_world_matrix, results.materials);

		// conPrint("processAvatarModelFile() done.");
	}
	catch(glare::Exception& e)
	{
		conPrint("Excep: " + e.what());

		gui_client->showErrorNotification(e.what());
	}
}
