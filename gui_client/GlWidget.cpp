/*=====================================================================
GlWidget.cpp
------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "GlWidget.h"


#include "CameraController.h"
#include "MainOptionsDialog.h"
#include "../dll/include/IndigoMesh.h"
#include "../indigo/TextureServer.h"
#include "../indigo/globals.h"
#include "../graphics/Map2D.h"
#include "../graphics/ImageMap.h"
#include "../maths/vec3.h"
#include "../maths/GeometrySampling.h"
#include "../utils/Lock.h"
#include "../utils/Mutex.h"
#include "../utils/Clock.h"
#include "../utils/Timer.h"
#include "../utils/Platform.h"
#include "../utils/FileUtils.h"
#include "../utils/Reference.h"
#include "../utils/StringUtils.h"
#include "../utils/PlatformUtils.h"
#include "../utils/TaskManager.h"
#include "../qt/QtUtils.h"
#include <QtGui/QMouseEvent>
#include <QtCore/QSettings>
#include <QtWidgets/QShortcut>
#include <QtGamepad/QGamepad>
#include <QtGui/QOpenGLContext>
#if defined(_WIN32)
#include <QtPlatformHeaders/QWGLNativeContext>
#endif
#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>
#include <tracy/Tracy.hpp>
#include <set>
#include <stack>
#include <algorithm>


// Export some symbols to indicate to the system that we want to run on a dedicated GPU if present.
// See https://stackoverflow.com/a/39047129
#if defined(_WIN32)
extern "C"
{
	__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif


// https://wiki.qt.io/How_to_use_OpenGL_Core_Profile_with_Qt
// https://developer.apple.com/opengl/capabilities/GLInfo_1085_Core.html
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))

static QSurfaceFormat makeFormat()
{
	// We need to request a 'core' profile.  Otherwise on OS X, we get an OpenGL 2.1 interface, whereas we require a v3+ interface.
	QSurfaceFormat format;
	// We need to request version 3.2 (or above?) on OS X, otherwise we get legacy version 2.
#ifdef OSX
	format.setVersion(3, 2);
#endif
	format.setProfile(QSurfaceFormat::CoreProfile);
	format.setSamples(4); // Enable multisampling

	return format;
}

#else

static QGLFormat makeFormat()
{
	// We need to request a 'core' profile.  Otherwise on OS X, we get an OpenGL 2.1 interface, whereas we require a v3+ interface.
	QGLFormat format;
	// We need to request version 3.2 (or above?) on OS X, otherwise we get legacy version 2.
#ifdef OSX
	format.setVersion(3, 2);
#endif
//	format.setVersion(4, 6); // TEMP NEW
	format.setProfile(QGLFormat::CoreProfile);
	format.setSampleBuffers(true); // Enable multisampling

//	format.setSwapInterval(0); // TEMP: turn off vsync

	return format;
}

#endif


GlWidget::GlWidget(QWidget *parent)
:	
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	QOpenGLWidget(parent),
#else
	QGLWidget(makeFormat(), parent),
#endif
	imgui_initialised(false),
	cam_controller(NULL),
	cam_rot_on_mouse_move_enabled(true),
	cam_move_on_key_input_enabled(true),
	near_draw_dist(0.22f), // As large as possible as we can get without clipping becoming apparent.
	max_draw_dist(1000.f),
	gamepad(NULL),
	print_output(NULL),
	settings(NULL),
	take_map_screenshot(false),
	screenshot_ortho_sensor_width_m(10),
	show_imgui_window(false),
	allow_bindless_textures(true),
	allow_multi_draw_indirect(true)
{
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	setFormat(makeFormat());
#endif

	viewport_w = viewport_h = 100;

	// Needed to get keyboard events.
	setFocusPolicy(Qt::StrongFocus);

	setMouseTracking(true); // Set this so we get mouse move events even when a mouse button is not down.


	gamepad_init_timer = new QTimer(this);
	gamepad_init_timer->setSingleShot(true);
	connect(gamepad_init_timer, SIGNAL(timeout()), this, SLOT(initGamepadsSlot()));
	gamepad_init_timer->start(/*msec=*/500); 

	//// See if we have any attached gamepads
	//QGamepadManager* manager = QGamepadManager::instance();

	//const QList<int> list = manager->connectedGamepads();

	//if(!list.isEmpty())
	//{
	//	gamepad = new QGamepad(list.at(0));

	//	connect(gamepad, SIGNAL(axisLeftXChanged(double)), this, SLOT(gamepadInputSlot()));
	//	connect(gamepad, SIGNAL(axisLeftYChanged(double)), this, SLOT(gamepadInputSlot()));
	//}


	// Create a CTRL+C shortcut just for this widget, so it doesn't interfere with the global CTRL+C shortcut for copying text from the chat etc.
	QShortcut* copy_shortcut = new QShortcut(QKeySequence(tr("Ctrl+C")), this);
	connect(copy_shortcut, SIGNAL(activated()), this, SIGNAL(copyShortcutActivated()));
	copy_shortcut->setContext(Qt::WidgetWithChildrenShortcut); // We only want CTRL+C to work for the graphics view when it has focus.

	QShortcut* cut_shortcut = new QShortcut(QKeySequence(tr("Ctrl+X")), this);
	connect(cut_shortcut, SIGNAL(activated()), this, SIGNAL(cutShortcutActivated()));
	cut_shortcut->setContext(Qt::WidgetWithChildrenShortcut); // We only want CTRL+X to work for the graphics view when it has focus.

	QShortcut* paste_shortcut = new QShortcut(QKeySequence(tr("Ctrl+V")), this);
	connect(paste_shortcut, SIGNAL(activated()), this, SIGNAL(pasteShortcutActivated()));
	paste_shortcut->setContext(Qt::WidgetWithChildrenShortcut); // We only want CTRL+V to work for the graphics view when it has focus.
}


void GlWidget::initGamepadsSlot()
{
#if 1 // If use Qt for gamepad input:
	// See if we have any attached gamepads
	QGamepadManager* manager = QGamepadManager::instance();

	const QList<int> list = manager->connectedGamepads();

	if(print_output) print_output->print("Found " + toString(list.size()) + " connected gamepad(s).");

	if(!list.isEmpty())
	{
		gamepad = new QGamepad(list.at(0));
		
		const std::string name = QtUtils::toStdString(gamepad->name());
		if(print_output) print_output->print("Using gamepad '" + name + "'...");

		//connect(gamepad, SIGNAL(axisLeftXChanged(double)), this, SLOT(gamepadInputSlot()));
		//connect(gamepad, SIGNAL(axisLeftYChanged(double)), this, SLOT(gamepadInputSlot()));

		connect(gamepad, SIGNAL(buttonXChanged(bool)), this, SLOT(buttonXChangedSlot(bool)));
		connect(gamepad, SIGNAL(buttonXChanged(bool)), this, SIGNAL(gamepadButtonXChangedSignal(bool)));
		connect(gamepad, SIGNAL(buttonAChanged(bool)), this, SIGNAL(gamepadButtonAChangedSignal(bool)));
	}
#endif
}


void GlWidget::buttonXChangedSlot(bool pressed)
{
	// printVar(pressed);

}


void GlWidget::gamepadInputSlot()
{
	// TODO: have to handle interactions with click and drag to rotate camera code.
	// hideCursor();
}


GlWidget::~GlWidget()
{
	shutdownImGui();

	opengl_engine = NULL;
}


void GlWidget::shutdown()
{
	shutdownImGui();

	opengl_engine = NULL;
}


//======================================== ImGui support ========================================
// ImGui only ships with an SDL2 platform backend (as used by SDLClient), so for the Qt build we feed the Qt input events into ImGui ourselves.
// The OpenGL rendering backend (imgui_impl_opengl3) is used as-is.


static ImGuiKey imGuiKeyForQtKey(int qt_key)
{
	if(qt_key >= Qt::Key_A  && qt_key <= Qt::Key_Z)   return (ImGuiKey)(ImGuiKey_A  + (qt_key - Qt::Key_A));
	if(qt_key >= Qt::Key_0  && qt_key <= Qt::Key_9)   return (ImGuiKey)(ImGuiKey_0  + (qt_key - Qt::Key_0));
	if(qt_key >= Qt::Key_F1 && qt_key <= Qt::Key_F12) return (ImGuiKey)(ImGuiKey_F1 + (qt_key - Qt::Key_F1));

	switch(qt_key)
	{
	case Qt::Key_Tab:			return ImGuiKey_Tab;
	case Qt::Key_Left:			return ImGuiKey_LeftArrow;
	case Qt::Key_Right:			return ImGuiKey_RightArrow;
	case Qt::Key_Up:			return ImGuiKey_UpArrow;
	case Qt::Key_Down:			return ImGuiKey_DownArrow;
	case Qt::Key_PageUp:		return ImGuiKey_PageUp;
	case Qt::Key_PageDown:		return ImGuiKey_PageDown;
	case Qt::Key_Home:			return ImGuiKey_Home;
	case Qt::Key_End:			return ImGuiKey_End;
	case Qt::Key_Insert:		return ImGuiKey_Insert;
	case Qt::Key_Delete:		return ImGuiKey_Delete;
	case Qt::Key_Backspace:		return ImGuiKey_Backspace;
	case Qt::Key_Space:			return ImGuiKey_Space;
	case Qt::Key_Return:		return ImGuiKey_Enter;
	case Qt::Key_Enter:			return ImGuiKey_KeypadEnter;
	case Qt::Key_Escape:		return ImGuiKey_Escape;
	case Qt::Key_Control:		return ImGuiKey_LeftCtrl;
	case Qt::Key_Shift:			return ImGuiKey_LeftShift;
	case Qt::Key_Alt:			return ImGuiKey_LeftAlt;
	case Qt::Key_Meta:			return ImGuiKey_LeftSuper;
	case Qt::Key_Menu:			return ImGuiKey_Menu;
	case Qt::Key_CapsLock:		return ImGuiKey_CapsLock;
	case Qt::Key_ScrollLock:	return ImGuiKey_ScrollLock;
	case Qt::Key_NumLock:		return ImGuiKey_NumLock;
	case Qt::Key_Print:			return ImGuiKey_PrintScreen;
	case Qt::Key_Pause:			return ImGuiKey_Pause;
	case Qt::Key_Apostrophe:	return ImGuiKey_Apostrophe;
	case Qt::Key_Comma:			return ImGuiKey_Comma;
	case Qt::Key_Minus:			return ImGuiKey_Minus;
	case Qt::Key_Period:		return ImGuiKey_Period;
	case Qt::Key_Slash:			return ImGuiKey_Slash;
	case Qt::Key_Semicolon:		return ImGuiKey_Semicolon;
	case Qt::Key_Equal:			return ImGuiKey_Equal;
	case Qt::Key_BracketLeft:	return ImGuiKey_LeftBracket;
	case Qt::Key_Backslash:		return ImGuiKey_Backslash;
	case Qt::Key_BracketRight:	return ImGuiKey_RightBracket;
	case Qt::Key_QuoteLeft:		return ImGuiKey_GraveAccent;
	default:					return ImGuiKey_None;
	}
}


// Map from a Qt mouse button to an ImGui mouse button index.  Returns -1 for buttons ImGui doesn't handle.
static int imGuiMouseButtonForQtButton(Qt::MouseButton button)
{
	switch(button)
	{
	case Qt::LeftButton:	return 0;
	case Qt::RightButton:	return 1;
	case Qt::MiddleButton:	return 2;
	default:				return -1;
	}
}


static void addImGuiKeyModifierEvents(Qt::KeyboardModifiers modifiers)
{
	ImGuiIO& io = ImGui::GetIO();
	io.AddKeyEvent(ImGuiMod_Ctrl,  (modifiers & Qt::ControlModifier) != 0);
	io.AddKeyEvent(ImGuiMod_Shift, (modifiers & Qt::ShiftModifier)   != 0);
	io.AddKeyEvent(ImGuiMod_Alt,   (modifiers & Qt::AltModifier)     != 0);
	io.AddKeyEvent(ImGuiMod_Super, (modifiers & Qt::MetaModifier)    != 0);
}


void GlWidget::initImGui()
{
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.BackendPlatformName = "substrata_qt";

	ImGui::StyleColorsDark();

	ImGui_ImplOpenGL3_Init("#version 150"); // We have a core profile context (see makeFormat()), so use a GLSL version that is valid for core profiles.

	imgui_frame_timer.reset();
	imgui_initialised = true;
}


void GlWidget::shutdownImGui()
{
	if(imgui_initialised)
	{
		makeCurrent(); // ImGui_ImplOpenGL3_Shutdown() destroys GL objects, so needs the GL context to be current.

		ImGui_ImplOpenGL3_Shutdown();
		ImGui::DestroyContext();

		imgui_initialised = false;
	}
}


void GlWidget::checkInitImGui()
{
	if(!imgui_initialised)
		initImGui();
}


bool GlWidget::imGuiWantsMouseInput() const
{
	return imgui_initialised && show_imgui_window && ImGui::GetIO().WantCaptureMouse;
}


bool GlWidget::imGuiWantsKeyboardInput() const
{
	return imgui_initialised && show_imgui_window && ImGui::GetIO().WantCaptureKeyboard;
}


// Note that ImGui mouse positions are in logical (device-independent) pixels, matching io.DisplaySize, which we set in paintGL().
static void addImGuiMousePosEvent(const QPoint& widget_pos)
{
	ImGui::GetIO().AddMousePosEvent((float)widget_pos.x(), (float)widget_pos.y());
}
//===============================================================================================


void GlWidget::setCameraController(CameraController* cam_controller_)
{
	cam_controller = cam_controller_;
}


void GlWidget::resizeGL(int width_, int height_)
{
	assert(QGLContext::currentContext() == this->context());

	viewport_w = width_;
	viewport_h = height_;

	if(this->opengl_engine)
	{
		this->opengl_engine->setViewportDims(viewport_w, viewport_h);

#if QT_VERSION_MAJOR >= 6
		// In Qt6, the GL widget uses a custom framebuffer (defaultFramebufferObject).  We want to make sure we draw to this.
		this->opengl_engine->setTargetFrameBuffer(new FrameBuffer(this->defaultFramebufferObject()));
#endif
	}

	emit viewportResizedSignal(width_, height_);
}


void GlWidget::initializeGL()
{
	assert(QGLContext::currentContext() == this->context()); // "There is no need to call makeCurrent() because this has already been done when this function is called."  (https://doc.qt.io/qt-5/qglwidget.html#initializeGL)

	const std::string opengl_vendor	= std::string((const char*)glGetString(GL_VENDOR));
	const bool is_AMD    = StringUtils::containsString(toLowerCase(opengl_vendor), "ati") || StringUtils::containsString(toLowerCase(opengl_vendor), "amd");
	const bool is_Nvidia = StringUtils::containsString(toLowerCase(opengl_vendor), "nvidia");

	// Only enable SSAO/SSGI by default on AMD and Nvidia GPUs, which are likely to be more powerful than stuff like (integrated) Intel GPUs, which
	// struggle with SSAO/SSGI.
	const bool default_use_SSAO = is_AMD || is_Nvidia;

	bool shadows = true;
	bool use_MSAA = true;
	bool bloom = true;
	bool use_SSAO = false;
	if(settings)
	{
		shadows  = settings->value(MainOptionsDialog::shadowsKey(),	/*default val=*/true).toBool();
		use_MSAA = settings->value(MainOptionsDialog::MSAAKey(),	/*default val=*/true).toBool();
		bloom    = settings->value(MainOptionsDialog::BloomKey(),	/*default val=*/true).toBool();
		use_SSAO = settings->value(MainOptionsDialog::SSAOKey(),    /*default val=*/default_use_SSAO).toBool();
	}

	// Enable debug output (glDebugMessageCallback) in Debug and RelWithDebugInfo mode, e.g. when BUILD_TESTS is 1.
	// Don't enable in Release mode, in case it has a performance cost.
#if BUILD_TESTS
	const bool enable_debug_outout = true;
#else
	const bool enable_debug_outout = false;
#endif

	OpenGLEngineSettings engine_settings;
	engine_settings.enable_debug_output = enable_debug_outout;
	engine_settings.shadow_mapping = shadows;
	engine_settings.compress_textures = true;
	engine_settings.depth_fog = true;
	//engine_settings.use_final_image_buffer = bloom;
	engine_settings.msaa_samples = use_MSAA ? 4 : -1;
	engine_settings.max_tex_CPU_mem_usage = 1536 * 1024 * 1024ull; // Should be large enough that we have some spare room for the LRU texture cache.
	engine_settings.max_tex_GPU_mem_usage = 1536 * 1024 * 1024ull; // Should be large enough that we have some spare room for the LRU texture cache.
	engine_settings.allow_multi_draw_indirect = this->allow_multi_draw_indirect;
	engine_settings.allow_bindless_textures = this->allow_bindless_textures;
	engine_settings.ssao = use_SSAO;
	engine_settings.irradiance_probes_support = false;

#ifdef OSX
	// Force SSAO to false for now on Mac, as when it's enabled, the number of texture units exceeds the max (16) for the terrain shader.
	engine_settings.ssao_support = false;
	engine_settings.ssao = false; 
#endif


	opengl_engine = new OpenGLEngine(engine_settings);

	std::string data_dir = base_dir_path + "/data";
#if BUILD_TESTS
	try
	{
		// For development, allow loading opengl engine data, particularly shaders, straight from the repo dir.
		data_dir = PlatformUtils::getEnvironmentVariable("SUBSTRATA_USE_OPENGL_DATA_DIR"); // SUBSTRATA_USE_OPENGL_DATA_DIR can be set to e.g. n:/glare-core/opengl
		conPrint("Using OpenGL data dir from Env var: '" + data_dir + "'");
	}
	catch(glare::Exception&)
	{}
	
	try
	{
		const std::string substrata_dir = PlatformUtils::getEnvironmentVariable("SUBSTRATA_TRUNK_DIR");
		opengl_engine->additional_shader_dirs.push_back(substrata_dir);
	}
	catch(glare::Exception&)
	{}
#endif
		
	opengl_engine->initialise(
		data_dir, // data dir (should contain 'shaders' and 'gl_data')
		NULL, // texture_server
		this->print_output,
		main_task_manager,
		high_priority_task_manager,
		main_mem_allocator
	);
	if(!opengl_engine->initSucceeded())
	{
		conPrint("opengl_engine init failed: " + opengl_engine->getInitialisationErrorMsg());
		initialisation_error_msg = opengl_engine->getInitialisationErrorMsg();
	}

	if(opengl_engine->initSucceeded())
	{
		try
		{
			opengl_engine->setCirrusTexture(opengl_engine->getTexture(base_dir_path + "/data/resources/cirrus.exr"));
		}
		catch(glare::Exception& e)
		{
			conPrint("Error: " + e.what());
		}
		
		try
		{
			// opengl_engine->setSnowIceTexture(opengl_engine->getTexture(base_dir_path + "/resources/snow-ice-01-normal.png"));
		}
		catch(glare::Exception& e)
		{
			conPrint("Error: " + e.what());
		}

		if(bloom)
			opengl_engine->getCurrentScene()->bloom_strength = 0.3f;

		opengl_engine->getCurrentScene()->draw_aurora = true;
	}
}


void* GlWidget::makeNewSharedGLContext()
{
#if defined(_WIN32)
	QOpenGLContext* window_context = this->context()->contextHandle();
	
	QOpenGLContext* new_window_context = new QOpenGLContext();
	new_window_context->setFormat(window_context->format());
	new_window_context->setShareContext(window_context);
	new_window_context->create();
	assert(new_window_context->isValid());


	QVariant nativeHandle = new_window_context->nativeHandle();
	assert(!nativeHandle.isNull() && nativeHandle.canConvert<QWGLNativeContext>());
	
	QWGLNativeContext nativeContext = nativeHandle.value<QWGLNativeContext>();
	HGLRC hglrc = nativeContext.context();

	return hglrc;
#else
	return nullptr;
#endif
}


void GlWidget::paintGL()
{
	assert(QGLContext::currentContext() == this->context()); // "There is no need to call makeCurrent() because this has already been done when this function is called."  (https://doc.qt.io/qt-5/qglwidget.html#initializeGL)
	if(opengl_engine.isNull())
		return;

	if(take_map_screenshot)
	{
		const Vec3d cam_pos =  cam_controller->getPosition();

		const Matrix4f world_to_camera_space_matrix = Matrix4f::rotationAroundXAxis(Maths::pi_2<float>()) * Matrix4f::translationMatrix(-(cam_pos.toVec4fVector()));

		opengl_engine->setViewportDims(viewport_w, viewport_h);
		opengl_engine->setNearDrawDistance(near_draw_dist);
		opengl_engine->setMaxDrawDistance(max_draw_dist);
		opengl_engine->setDiagonalOrthoCameraTransform(world_to_camera_space_matrix, /*sensor_width*/screenshot_ortho_sensor_width_m, /*render_aspect_ratio=*/1.f);
		//opengl_engine->setOrthoCameraTransform(world_to_camera_space_matrix, /*sensor_width*/screenshot_ortho_sensor_width_m, /*render_aspect_ratio=*/1.f, 0, 0);
		opengl_engine->draw();
		return;
	}


	if(cam_controller)
	{
		// Work out current camera transform
		Matrix4f world_to_camera_space_matrix;
		cam_controller->getWorldToCameraMatrix(world_to_camera_space_matrix);

		const float sensor_width = defaultSensorWidth();
		const float lens_sensor_dist = (float)cam_controller->lens_sensor_dist;
		const float render_aspect_ratio = (float)viewport_w / (float)viewport_h;
		opengl_engine->setViewportDims(viewport_w, viewport_h);
		opengl_engine->setNearDrawDistance(near_draw_dist);
		opengl_engine->setMaxDrawDistance(max_draw_dist);
		opengl_engine->setPerspectiveCameraTransform(world_to_camera_space_matrix, sensor_width, lens_sensor_dist, render_aspect_ratio, /*lens shift up=*/0.f, /*lens shift right=*/0.f);
		//opengl_engine->setOrthoCameraTransform(world_to_camera_space_matrix, 1000.f, render_aspect_ratio, /*lens shift up=*/0.f, /*lens shift right=*/0.f);
		opengl_engine->draw();
	}

	if(imgui_initialised && show_imgui_window)
	{
		ImGuiIO& io = ImGui::GetIO();
		// ImGui works in logical (device-independent) pixels, and multiplies by DisplayFramebufferScale to get the framebuffer size when rendering.
		const float device_pixel_ratio = (float)this->devicePixelRatio();
		io.DisplaySize = ImVec2((float)this->width(), (float)this->height());
		io.DisplayFramebufferScale = ImVec2(device_pixel_ratio, device_pixel_ratio);
		io.DeltaTime = myMax(1.0e-6f, (float)imgui_frame_timer.elapsed()); // DeltaTime must be > 0.
		imgui_frame_timer.reset();

		ImGui_ImplOpenGL3_NewFrame();
		ImGui::NewFrame();

		emit buildImGuiUISignal(); // Let MainWindow build the window contents.

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}

	//conPrint("FPS: " + doubleToStringNSigFigs(1 / fps_timer.elapsed(), 1));
	//fps_timer.reset();

	FrameMark; // Tracy profiler
}


void GlWidget::keyPressEvent(QKeyEvent* e)
{
	if(imgui_initialised && show_imgui_window)
	{
		ImGuiIO& io = ImGui::GetIO();

		addImGuiKeyModifierEvents(e->modifiers());

		const ImGuiKey imgui_key = imGuiKeyForQtKey(e->key());
		if(imgui_key != ImGuiKey_None)
			io.AddKeyEvent(imgui_key, /*down=*/true);

		const QString text = e->text();
		if(!text.isEmpty() && (text.at(0).unicode() >= 32) && (text.at(0).unicode() != 127)) // Ignore control characters (return, backspace, escape etc.)
			io.AddInputCharactersUTF8(text.toUtf8().constData());
	}

	// Note that we emit the event even when ImGui wants the keyboard input, so that the ImGui window toggle key keeps working.  MainWindow calls imGuiWantsKeyboardInput() to decide whether to pass the event on to the client.
	emit keyPressed(e);
}


void GlWidget::keyReleaseEvent(QKeyEvent* e)
{
	if(imgui_initialised && show_imgui_window)
	{
		addImGuiKeyModifierEvents(e->modifiers());

		const ImGuiKey imgui_key = imGuiKeyForQtKey(e->key());
		if(imgui_key != ImGuiKey_None)
			ImGui::GetIO().AddKeyEvent(imgui_key, /*down=*/false);
	}

	emit keyReleased(e);
}


// If this widget loses focus, just consider all keys up.
// Otherwise we might miss the key-up event, leading to our keys appearing to be stuck down.
void GlWidget::focusOutEvent(QFocusEvent* e)
{
	if(imgui_initialised)
		ImGui::GetIO().AddFocusEvent(/*focused=*/false); // Makes ImGui clear its input state as well.

	emit focusOutSignal();
}


void GlWidget::hideCursor()
{
	// Hide cursor when moving view.
	this->setCursor(QCursor(Qt::BlankCursor));
}


bool GlWidget::isCursorHidden()
{
	return this->cursor().shape() == Qt::BlankCursor;
}


void GlWidget::setCursorIfNotHidden(Qt::CursorShape new_shape)
{
	if(this->cursor().shape() != Qt::BlankCursor)
		this->setCursor(new_shape);
}


void GlWidget::mousePressEvent(QMouseEvent* e)
{
	//conPrint("mousePressEvent at " + toString(QCursor::pos().x()) + ", " + toString(QCursor::pos().y()));

	if(imgui_initialised && show_imgui_window)
	{
		const int imgui_button = imGuiMouseButtonForQtButton(e->button());
		if(imgui_button >= 0)
		{
			addImGuiMousePosEvent(e->pos());
			ImGui::GetIO().AddMouseButtonEvent(imgui_button, /*down=*/true);
		}
	}

	mouse_move_origin = QCursor::pos();
	last_mouse_press_pos = QCursor::pos();

	// Hide cursor when moving view.
	//this->setCursor(QCursor(Qt::BlankCursor));

	emit mousePressed(e);
}


void GlWidget::mouseReleaseEvent(QMouseEvent* e)
{
	if(imgui_initialised && show_imgui_window)
	{
		const int imgui_button = imGuiMouseButtonForQtButton(e->button());
		if(imgui_button >= 0)
		{
			addImGuiMousePosEvent(e->pos());
			ImGui::GetIO().AddMouseButtonEvent(imgui_button, /*down=*/false);
		}
	}

	// Unhide cursor.
	this->unsetCursor();

	//conPrint("mouseReleaseEvent at " + toString(QCursor::pos().x()) + ", " + toString(QCursor::pos().y()));

	//if((QCursor::pos() - last_mouse_press_pos).manhattanLength() < 4)
	{
		//conPrint("Click at " + toString(QCursor::pos().x()) + ", " + toString(QCursor::pos().y()));
		//conPrint("Click at " + toString(e->pos().x()) + ", " + toString(e->pos().y()));

		emit mouseReleased(e);
	}
}


void GlWidget::showEvent(QShowEvent* e)
{
	emit widgetShowSignal();
}


void GlWidget::mouseMoveEvent(QMouseEvent* e)
{
	if(imgui_initialised && show_imgui_window)
		addImGuiMousePosEvent(e->pos());

	Qt::MouseButtons mb = e->buttons();
	if(cam_rot_on_mouse_move_enabled && (cam_controller != NULL) && (mb & Qt::LeftButton) && !imGuiWantsMouseInput())// && (e->modifiers() & Qt::AltModifier))
	{
		/*switch(e->source())
		{
			case Qt::MouseEventNotSynthesized: conPrint("Qt::MouseEventNotSynthesized");
			case Qt::MouseEventSynthesizedBySystem: conPrint("Qt::MouseEventSynthesizedBySystem");
			case Qt::MouseEventSynthesizedByQt: conPrint("Qt::MouseEventSynthesizedByQt");
			case Qt::MouseEventSynthesizedByApplication: conPrint("Qt::MouseEventSynthesizedByApplication");
		}*/

		//double shift_scale = ((e->modifiers() & Qt::ShiftModifier) == 0) ? 1.0 : 0.35; // If shift is held, movement speed is roughly 1/3

		// Get new mouse position, movement vector and set previous mouse position to new.
		QPoint new_pos = QCursor::pos();
		QPoint delta(new_pos.x() - mouse_move_origin.x(),
					 mouse_move_origin.y() - new_pos.y()); // Y+ is down in screenspace, not up as desired.

		// QCursor::setPos() seems to generate mouse move events, which we don't want to affect the camera.  Only rotate camera based on actual mouse movements.
		if(e->source() == Qt::MouseEventNotSynthesized)
		{
			if(mb & Qt::LeftButton)  cam_controller->updateRotation(/*pitch_delta=*/delta.y(), /*heading_delta=*/delta.x()/* * shift_scale*/);
			//if(mb & Qt::MidButton)   cam_controller->update(Vec3d(delta.x(), 0, delta.y()) * shift_scale, Vec2d(0, 0));
			//if(mb & Qt::RightButton) cam_controller->update(Vec3d(0, delta.y(), 0) * shift_scale, Vec2d(0, 0));
		}

		// On Windows/linux, reset the cursor position to where we started, so we never run out of space to move.
		// QCursor::setPos() does not work on mac, and also gives a message about Substrata trying to control the computer, which we want to avoid.
		// So don't use setPos() on Mac.
#if defined(OSX)
		mouse_move_origin = QCursor::pos();
#else
		QCursor::setPos(mouse_move_origin);
		mouse_move_origin = QCursor::pos();
#endif

		//conPrint("mouseMoveEvent FPS: " + doubleToStringNSigFigs(1 / fps_timer.elapsed(), 1));
		//fps_timer.reset();
	}

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	QOpenGLWidget::mouseMoveEvent(e);
#else
	QGLWidget::mouseMoveEvent(e);
#endif

	emit mouseMoved(e);

	//conPrint("mouseMoveEvent time since last event: " + doubleToStringNSigFigs(fps_timer.elapsed(), 5));
	//fps_timer.reset();
}


void GlWidget::wheelEvent(QWheelEvent* e)
{
	if(imgui_initialised && show_imgui_window)
	{
		// angleDelta() is in eighths of a degree.  ImGui wheel units are in mouse wheel notches, which are 15 degrees = 120 eighths of a degree.
		ImGui::GetIO().AddMouseWheelEvent((float)e->angleDelta().x() / 120.f, (float)e->angleDelta().y() / 120.f);
	}

	emit mouseWheelSignal(e);
}


void GlWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
	if(imgui_initialised && show_imgui_window)
	{
		// Qt sends a double-click event instead of the second press event, so feed a press event to ImGui here, otherwise it sees an unmatched release.
		const int imgui_button = imGuiMouseButtonForQtButton(e->button());
		if(imgui_button >= 0)
		{
			addImGuiMousePosEvent(e->pos());
			ImGui::GetIO().AddMouseButtonEvent(imgui_button, /*down=*/true);
		}
	}

	emit mouseDoubleClickedSignal(e);
}
