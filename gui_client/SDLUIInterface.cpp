/*=====================================================================
SDLUIInterface.cpp
------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "SDLUIInterface.h"


#include "GUIClient.h"
#include <utils/ConPrint.h>
#include <utils/Base64.h>
#include <utils/BufferOutStream.h>
#include <utils/FileUtils.h>
#include <graphics/ImageMap.h>
#include <graphics/TextRenderer.h>
#include <graphics/jpegdecoder.h>
#include <webserver/Escaping.h>
#include <SDL.h>
#include <iostream>
#if EMSCRIPTEN
#include <emscripten.h>
#endif




#if EMSCRIPTEN

// Define openURLInNewBrowserTab(const char* URL) function
EM_JS(void, openURLInNewBrowserTab, (const char* URL), {
	window.open(UTF8ToString(URL), "mozillaTab"); // See https://developer.mozilla.org/en-US/docs/Web/API/Window/open
});

// Define navigateToURL(const char* URL) function
EM_JS(void, navigateToURL, (const char* URL), {
	window.location = UTF8ToString(URL);
});


EM_JS(void, downloadFile, (const char* dataUrl_cstr, const char* fileName_cstr), {

	let dataUrl  = UTF8ToString(dataUrl_cstr);
	let fileName = UTF8ToString(fileName_cstr);

	// Create a temporary anchor element
	const link = document.createElement('a');

	// Set the href to the data URL
	link.href = dataUrl;

	// Set the download attribute with filename
	link.download = fileName;

	// Append to body (necessary for Firefox)
	document.body.appendChild(link);

	// Simulate click
	link.click();
	
	// Clean up
	document.body.removeChild(link);
});

#endif


void SDLUIInterface::appendChatMessage(const std::string& msg)
{
}

void SDLUIInterface::clearChatMessages()
{
}

bool SDLUIInterface::isShowParcelsEnabled() const
{
	return false;
}

void SDLUIInterface::updateOnlineUsersList()
{
}

void SDLUIInterface::showHTMLMessageBox(const std::string& title, const std::string& msg)
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title.c_str(), msg.c_str(), window);
}

void SDLUIInterface::showPlainTextMessageBox(const std::string& title, const std::string& msg)
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title.c_str(), msg.c_str(), window);
}


// Print without flushing.
static void doPrint(const std::string& s)
{
	std::cout << s << "\n";
}


void SDLUIInterface::logMessage(const std::string& msg)
{
	doPrint("Log: " + msg); // TEMP
}

void SDLUIInterface::setTextAsNotLoggedIn()
{
	logged_in_username = "";
}

void SDLUIInterface::setTextAsLoggedIn(const std::string& username)
{
	logged_in_username = username;
}

void SDLUIInterface::loginButtonClicked()
{
#if EMSCRIPTEN
	const std::string return_url = gui_client->getCurrentWebClientURLPath();
	const std::string url = "/login?return=" + web::Escaping::URLEscape(return_url);
	navigateToURL(url.c_str());
#endif
}

void SDLUIInterface::signUpButtonClicked()
{
#if EMSCRIPTEN
	const std::string return_url = gui_client->getCurrentWebClientURLPath();
	const std::string url = "/signup?return=" + web::Escaping::URLEscape(return_url);
	navigateToURL(url.c_str());
#endif
}

void SDLUIInterface::loggedInButtonClicked()
{
#if EMSCRIPTEN
	navigateToURL("/account");
#endif
}

void SDLUIInterface::updateWorldSettingsControlsEditable()
{
}

void SDLUIInterface::updateWorldSettingsUIFromWorldSettings()
{
}

bool SDLUIInterface::diagnosticsVisible()
{
	return false;
}

bool SDLUIInterface::showObAABBsEnabled()
{
	return false;
}

bool SDLUIInterface::showPhysicsObOwnershipEnabled()
{
	return false;
}

bool SDLUIInterface::showVehiclePhysicsVisEnabled()
{
	return false;
}

bool SDLUIInterface::showPlayerPhysicsVisEnabled()
{
	return false;
}

bool SDLUIInterface::showLodChunksVisEnabled()
{
	return false;
}

void SDLUIInterface::writeTransformMembersToObject(WorldObject& ob)
{
}

void SDLUIInterface::objectLastModifiedUpdated(const WorldObject& ob)
{
}

void SDLUIInterface::objectModelURLUpdated(const WorldObject& ob)
{
}

void SDLUIInterface::objectLightmapURLUpdated(const WorldObject& ob)
{
}

void SDLUIInterface::showEditorDockWidget()
{
}

void SDLUIInterface::showParcelEditor()
{
}

void SDLUIInterface::setParcelEditorForParcel(const Parcel& parcel)
{
}

void SDLUIInterface::setParcelEditorEnabled(bool enabled)
{
}

void SDLUIInterface::showObjectEditor()
{
}

void SDLUIInterface::setObjectEditorEnabled(bool enabled)
{
}

void SDLUIInterface::setObjectEditorControlsEditable(bool editable)
{
}

void SDLUIInterface::setObjectEditorFromOb(const WorldObject& ob, int selected_mat_index, bool ob_in_editing_users_world)
{
}

int SDLUIInterface::getSelectedMatIndex()
{
	return 0;
}

void SDLUIInterface::objectEditorToObject(WorldObject& ob)
{
}

void SDLUIInterface::objectEditorObjectPickedUp()
{
}

void SDLUIInterface::objectEditorObjectDropped()
{
}

bool SDLUIInterface::snapToGridCheckBoxChecked()
{
	return false;
}

double SDLUIInterface::gridSpacing()
{
	return 1.0;
}

bool SDLUIInterface::posAndRot3DControlsEnabled()
{
	return true;
}

void SDLUIInterface::setUIForSelectedObject()
{
}

void SDLUIInterface::startObEditorTimerIfNotActive()
{
}

void SDLUIInterface::startLightmapFlagTimer()
{
}

void SDLUIInterface::showAvatarSettings() // Show avatar settings dialog.
{
#if EMSCRIPTEN
	EM_ASM({
		showAvatarSettingsWidget();
	});
#endif
}

void SDLUIInterface::setCamRotationOnMouseDragEnabled(bool enabled)
{

}

bool SDLUIInterface::isCursorHidden()
{
	return false;
}

void SDLUIInterface::hideCursor()
{
}

void SDLUIInterface::setKeyboardCameraMoveEnabled(bool enabled)
{
}

bool SDLUIInterface::isKeyboardCameraMoveEnabled()
{
	return true;
}

bool SDLUIInterface::hasFocus()
{
	return (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0; // NOTE: maybe want SDL_WINDOW_MOUSE_FOCUS?
}

void SDLUIInterface::setHelpInfoLabelToDefaultText()
{
}

void SDLUIInterface::setHelpInfoLabel(const std::string& text)
{
}

void SDLUIInterface::toggleFlyMode()
{
	gui_client->player_physics.setFlyModeEnabled(!gui_client->player_physics.flyModeEnabled());
}

void SDLUIInterface::enableThirdPersonCamera()
{
	gui_client->thirdPersonCameraToggled(true);
}

void SDLUIInterface::toggleThirdPersonCameraMode()
{
	gui_client->thirdPersonCameraToggled(!gui_client->cam_controller.thirdPersonEnabled());
}

void SDLUIInterface::enableThirdPersonCameraIfNotAlreadyEnabled()
{
	if(!gui_client->cam_controller.thirdPersonEnabled())
		gui_client->thirdPersonCameraToggled(true);
}

void SDLUIInterface::enableFirstPersonCamera()
{
	gui_client->thirdPersonCameraToggled(false);
}


void SDLUIInterface::openURL(const std::string& URL)
{
	conPrint("SDLUIInterface::openURL: URL: " + URL);
#if EMSCRIPTEN
	openURLInNewBrowserTab(URL.c_str());
#else
	SDL_OpenURL(URL.c_str());
#endif
}

Vec2i SDLUIInterface::getMouseCursorWidgetPos() // Get mouse cursor position, relative to gl widget.
{
	Vec2i p(0, 0);
	SDL_GetMouseState(&p.x, &p.y);
	return p;
}

std::string SDLUIInterface::getUsernameForDomain(const std::string& domain)
{
	return std::string();
}

std::string SDLUIInterface::getDecryptedPasswordForDomain(const std::string& domain)
{
	return std::string();
}

bool SDLUIInterface::inScreenshotTakingMode()
{
	return false;
}

void SDLUIInterface::takeScreenshot()
{
	gui_client->opengl_engine->getCurrentScene()->draw_overlay_objects = false; // Hide UI

	try
	{
		if(SDL_GL_MakeCurrent(window, gl_context) != 0)
			conPrint("SDL_GL_MakeCurrent failed.");

		ImageMapUInt8Ref map = gui_client->opengl_engine->drawToBufferAndReturnImageMap();
		if(map->hasAlphaChannel())
			map = map->extract3ChannelImage();

		// Write the JPEG to an in-mem buffer, since we will need it as a data URL for the web client.
		BufferOutStream buffer;
		JPEGDecoder::saveToStream(map, JPEGDecoder::SaveOptions(), buffer);

		const std::string filename = "substrata_screenshot_" + toString((uint64)Clock::getSecsSince1970()) + ".jpg";

#if EMSCRIPTEN
		std::string encoded;
		Base64::encode(buffer.buf.data(), buffer.buf.size(), encoded);
		const std::string image_data_url = "data:image/jpeg;base64," + encoded;

		downloadFile(image_data_url.c_str(), filename.c_str()); // Make the browser trigger a file download for the image.

		const std::string path = "/tmp/" + filename;
#else
		const std::string path = this->appdata_path + "/screenshots/" + filename;
		FileUtils::createDirIfDoesNotExist(FileUtils::getDirectory(path));
#endif

		// Write it to disk (so can be used for photo upload)
		FileUtils::writeEntireFile(path, (const char*)buffer.buf.data(), buffer.buf.size());
		settings_store->setStringValue("photo/last_saved_photo_path", path);

#if !EMSCRIPTEN
		gui_client->showInfoNotification("Saved screenshot to " + path);
#endif
	}
	catch(glare::Exception& e)
	{
#if EMSCRIPTEN
		gui_client->showErrorNotification("Error saving photo: " + e.what());
#else
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error saving photo", e.what().c_str(), window);
#endif
	}

	gui_client->opengl_engine->getCurrentScene()->draw_overlay_objects = true; // Unhide UI.
}


void SDLUIInterface::showScreenshots()
{
#if EMSCRIPTEN
#else
	SDL_OpenURL(("file:///" + this->appdata_path + "/screenshots/").c_str());
#endif
}


void SDLUIInterface::setGLWidgetContextAsCurrent()
{
	if(SDL_GL_MakeCurrent(window, gl_context) != 0)
		conPrint("SDL_GL_MakeCurrent failed.");
}

Vec2i SDLUIInterface::getGlWidgetPosInGlobalSpace()
{
	return Vec2i(0, 0);
}

void SDLUIInterface::webViewDataLinkHovered(const std::string& text)
{
}

bool SDLUIInterface::gamepadAttached()
{
	//return joystick != nullptr;
	return game_controller != nullptr;
}


static float removeDeadZone(float x)
{
	if(std::fabs(x) < (8000.f / 32768.f))
		return 0.f;
	else
		return x;
}

#if 0

float SDLUIInterface::gamepadButtonL2()
{
	const Sint16 val = SDL_JoystickGetAxis(joystick, /*axis=*/4);
	return (float)val / SDL_JOYSTICK_AXIS_MAX;
}

float SDLUIInterface::gamepadButtonR2()
{
	const Sint16 val = SDL_JoystickGetAxis(joystick, /*axis=*/5);
	return (float)val / SDL_JOYSTICK_AXIS_MAX;
}

float SDLUIInterface::gamepadAxisLeftX()
{
	const Sint16 val = SDL_JoystickGetAxis(joystick, /*axis=*/0);
	return removeDeadZone(val / 32768.f); // ((float)val - SDL_JOYSTICK_AXIS_MIN) / (SDL_JOYSTICK_AXIS_MAX - SDL_JOYSTICK_AXIS_MIN);
}

float SDLUIInterface::gamepadAxisLeftY()
{
	const Sint16 val = SDL_JoystickGetAxis(joystick, /*axis=*/1);
	return removeDeadZone(val / 32768.f); // ((float)val - SDL_JOYSTICK_AXIS_MIN) / (SDL_JOYSTICK_AXIS_MAX - SDL_JOYSTICK_AXIS_MIN);
}

float SDLUIInterface::gamepadAxisRightX()
{
	const Sint16 val = SDL_JoystickGetAxis(joystick, /*axis=*/2);
	return removeDeadZone(val / 32768.f); // ((float)val - SDL_JOYSTICK_AXIS_MIN) / (SDL_JOYSTICK_AXIS_MAX - SDL_JOYSTICK_AXIS_MIN);
}

float SDLUIInterface::gamepadAxisRightY()
{
	const Sint16 val = SDL_JoystickGetAxis(joystick, /*axis=*/3);
	return removeDeadZone(val / 32768.f); // ((float)val - SDL_JOYSTICK_AXIS_MIN) / (SDL_JOYSTICK_AXIS_MAX - SDL_JOYSTICK_AXIS_MIN);
}

#else

float SDLUIInterface::gamepadButtonL2()
{
	const Sint16 val = SDL_GameControllerGetAxis(game_controller, /*axis=*/SDL_CONTROLLER_AXIS_TRIGGERLEFT);
	return (float)val / SDL_JOYSTICK_AXIS_MAX;
}

float SDLUIInterface::gamepadButtonR2()
{
	const Sint16 val = SDL_GameControllerGetAxis(game_controller, /*axis=*/SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
	return (float)val / SDL_JOYSTICK_AXIS_MAX;
}

// NOTE: seems to be an issue in SDL that the left axis maps to the left keypad instead of left stick on a Logitech F310 gamepad.
float SDLUIInterface::gamepadAxisLeftX()
{
	const Sint16 val = SDL_GameControllerGetAxis(game_controller, /*axis=*/SDL_CONTROLLER_AXIS_LEFTX);
	return removeDeadZone(val / 32768.f);
}

float SDLUIInterface::gamepadAxisLeftY()
{
	const Sint16 val = SDL_GameControllerGetAxis(game_controller, /*axis=*/SDL_CONTROLLER_AXIS_LEFTY);
	return removeDeadZone(val / 32768.f);
}

float SDLUIInterface::gamepadAxisRightX()
{
	const Sint16 val = SDL_GameControllerGetAxis(game_controller, /*axis=*/SDL_CONTROLLER_AXIS_RIGHTX);
	return removeDeadZone(val / 32768.f);
}

float SDLUIInterface::gamepadAxisRightY()
{
	const Sint16 val = SDL_GameControllerGetAxis(game_controller, /*axis=*/SDL_CONTROLLER_AXIS_RIGHTY);
	return removeDeadZone(val / 32768.f);
}

#endif


bool SDLUIInterface::supportsSharedGLContexts() const
{
	return false;
}


void* SDLUIInterface::makeNewSharedGLContext()
{
	return nullptr;
}


void SDLUIInterface::makeGLContextCurrent(void* context)
{
	assert(0);
}
