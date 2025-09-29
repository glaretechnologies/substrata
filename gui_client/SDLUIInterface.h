/*=====================================================================
SDLUIInterface.h
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "UIInterface.h"
#include <settings/SettingsStore.h>
#include <SDL.h>
struct SDL_Window;
class GUIClient;
class TextRendererFontFace;


class SDLUIInterface final : public UIInterface
{
public:
	virtual void appendChatMessage(const std::string& msg) override;
	virtual void clearChatMessages() override;
	virtual bool isShowParcelsEnabled() const override;
	virtual void updateOnlineUsersList() override; // Works off world state avatars.
	virtual void showHTMLMessageBox(const std::string& title, const std::string& msg) override;
	virtual void showPlainTextMessageBox(const std::string& title, const std::string& msg) override;

	virtual void logMessage(const std::string& msg) override; // Appends to LogWindow log display.

	// UserDetailsWidget:
	virtual void setTextAsNotLoggedIn() override;
	virtual void setTextAsLoggedIn(const std::string& username) override;

	// Login/signup buttons
	virtual void loginButtonClicked() override;
	virtual void signUpButtonClicked() override;
	virtual void loggedInButtonClicked() override;

	// worldSettingsWidget:
	virtual void updateWorldSettingsControlsEditable() override;

	virtual void updateWorldSettingsUIFromWorldSettings() override;

	virtual bool diagnosticsVisible() override;
	virtual bool showObAABBsEnabled() override;
	virtual bool showPhysicsObOwnershipEnabled() override;
	virtual bool showVehiclePhysicsVisEnabled() override;
	virtual bool showPlayerPhysicsVisEnabled() override;
	virtual bool showLodChunksVisEnabled() override;

	virtual void writeTransformMembersToObject(WorldObject& ob) override;
	virtual void objectLastModifiedUpdated(const WorldObject& ob) override;
	virtual void objectModelURLUpdated(const WorldObject& ob) override;
	virtual void objectLightmapURLUpdated(const WorldObject& ob) override; // Update lightmap URL in UI if we have selected the object.

	virtual void showEditorDockWidget() override;

	// Parcel editor
	virtual void showParcelEditor() override;
	virtual void setParcelEditorForParcel(const Parcel& parcel) override;
	virtual void setParcelEditorEnabled(bool enabled) override;

	// Object editor
	virtual void showObjectEditor() override;
	virtual void setObjectEditorEnabled(bool enabled) override;
	virtual void setObjectEditorControlsEditable(bool editable) override;
	virtual void setObjectEditorFromOb(const WorldObject& ob, int selected_mat_index, bool ob_in_editing_users_world) override; // 
	virtual int getSelectedMatIndex() override; //
	virtual void objectEditorToObject(WorldObject& ob) override;
	virtual void objectEditorObjectPickedUp() override;
	virtual void objectEditorObjectDropped() override;
	virtual bool snapToGridCheckBoxChecked() override;
	virtual double gridSpacing() override;
	virtual bool posAndRot3DControlsEnabled() override;
	virtual void setUIForSelectedObject() override; // Enable/disable delete object action etc..
	virtual void startObEditorTimerIfNotActive() override;
	virtual void startLightmapFlagTimer() override;

	virtual void showAvatarSettings() override; // Show avatar settings dialog.

	virtual void setCamRotationOnMouseDragEnabled(bool enabled) override;
	virtual bool isCursorHidden() override;
	virtual void hideCursor() override;

	virtual void setKeyboardCameraMoveEnabled(bool enabled) override; // Do we want WASD keys etc. to move the camera?  We don't want this while e.g. we enter text into a webview.
	virtual bool isKeyboardCameraMoveEnabled() override;

	virtual bool hasFocus() override;

	virtual void setHelpInfoLabelToDefaultText() override;
	virtual void setHelpInfoLabel(const std::string& text) override;

	
	virtual void toggleFlyMode() override;
	virtual void enableThirdPersonCamera() override;
	virtual void toggleThirdPersonCameraMode() override;
	virtual void enableThirdPersonCameraIfNotAlreadyEnabled() override;
	virtual void enableFirstPersonCamera() override;

	virtual void openURL(const std::string& URL) override;

	virtual Vec2i getMouseCursorWidgetPos() override; // Get mouse cursor position, relative to gl widget.

	// Credential manager
	virtual std::string getUsernameForDomain(const std::string& domain) override; // Returns empty string if no stored username for domain
	virtual std::string getDecryptedPasswordForDomain(const std::string& domain) override; // Returns empty string if no stored password for domain

	virtual bool inScreenshotTakingMode() override;
	virtual void takeScreenshot() override;
	virtual void showScreenshots() override;

	virtual void setGLWidgetContextAsCurrent() override;

	virtual Vec2i getGlWidgetPosInGlobalSpace() override; // Get top left of the GLWidget in global screen coordinates.

	virtual void webViewDataLinkHovered(const std::string& text) override;

	// Gamepad
	virtual bool gamepadAttached() override;
	virtual float gamepadButtonL2() override;
	virtual float gamepadButtonR2() override;
	virtual float gamepadAxisLeftX() override;
	virtual float gamepadAxisLeftY() override;
	virtual float gamepadAxisRightX() override;
	virtual float gamepadAxisRightY() override;

	// OpenGL
	virtual bool supportsSharedGLContexts() const override;
	virtual void* makeNewSharedGLContext() override;
	virtual void makeGLContextCurrent(void* context) override;


	SDL_Window* window;
	SDL_GLContext gl_context;
	std::string logged_in_username;
	GUIClient* gui_client;
	//SDL_Joystick* joystick;
	SDL_GameController* game_controller;
	std::string appdata_path;

	Reference<SettingsStore> settings_store;

	//Reference<TextRendererFontFace> font;
};
