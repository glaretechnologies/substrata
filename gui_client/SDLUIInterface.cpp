/*=====================================================================
SDLUIInterface.cpp
------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "SDLUIInterface.h"


#include "GUIClient.h"
#include <utils/ConPrint.h>
#include <graphics/ImageMap.h>
#include <graphics/TextRenderer.h>
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