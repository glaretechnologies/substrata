/*=====================================================================
UIInterface.h
-------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "../shared/UID.h"
#include <graphics/ImageMap.h>
#include <maths/vec2.h>
#include <string>
class WorldObject;
class Parcel;


/*=====================================================================
UIInterface
-----------
UI Functionality provided by the host platform.
The implementation will be specific to Qt or a web browser etc.
=====================================================================*/
class UIInterface
{
public:
	virtual void appendChatMessage(const std::string& msg) = 0;
	virtual void clearChatMessages() = 0;

	virtual bool isShowParcelsEnabled() const = 0;

	virtual void updateOnlineUsersList() = 0;

	virtual void showHTMLMessageBox(const std::string& title, const std::string& msg) = 0;
	virtual void showPlainTextMessageBox(const std::string& title, const std::string& msg) = 0;

	virtual void logMessage(const std::string& msg) = 0;

	// Lua scripting:
	// A lua script created by the logged in user printed something
	virtual void printFromLuaScript(const std::string& msg, UID object_uid) {}
	virtual void luaErrorOccurred(const std::string& msg, UID object_uid) {}

	// UserDetailsWidget:
	virtual void setTextAsNotLoggedIn() = 0;
	virtual void setTextAsLoggedIn(const std::string& username) = 0;

	// Login/signup buttons
	virtual void loginButtonClicked() = 0;
	virtual void signUpButtonClicked() = 0;
	virtual void loggedInButtonClicked() = 0;

	// worldSettingsWidget:
	virtual void updateWorldSettingsControlsEditable() = 0;
	virtual void updateWorldSettingsUIFromWorldSettings() = 0;

	virtual bool diagnosticsVisible() = 0;
	virtual bool showObAABBsEnabled() = 0;
	virtual bool showPhysicsObOwnershipEnabled() = 0;
	virtual bool showVehiclePhysicsVisEnabled() = 0;
	virtual bool showPlayerPhysicsVisEnabled() = 0;
	virtual bool showLodChunksVisEnabled() = 0;

	virtual void writeTransformMembersToObject(WorldObject& ob) = 0; // Get updated transform members from object editor and store in ob.
	virtual void objectLastModifiedUpdated(const WorldObject& ob) = 0; // ob.last_modified_time has been updated, update corresponding UI label.
	virtual void objectModelURLUpdated(const WorldObject& ob) = 0; // Update model URL in UI.
	virtual void objectLightmapURLUpdated(const WorldObject& ob) = 0; // Update lightmap URL in UI.

	
	virtual void showEditorDockWidget() = 0;
	// Parcel editor
	virtual void showParcelEditor() = 0; // Show parcel editor and hide object editor.
	virtual void setParcelEditorForParcel(const Parcel& parcel) = 0;
	virtual void setParcelEditorEnabled(bool enabled) = 0;
	// Object editor
	virtual void showObjectEditor() = 0; // Show object editor and hide parcel editor.
	virtual void setObjectEditorControlsEditable(bool editable) = 0;
	virtual void setObjectEditorEnabled(bool enabled) = 0;
	virtual void setObjectEditorFromOb(const WorldObject& ob, int selected_mat_index, bool ob_in_editing_users_world) = 0;
	virtual int getSelectedMatIndex() = 0; 
	virtual void objectEditorToObject(WorldObject& ob) = 0; // Sets changed_flags on object as well.
	virtual void objectEditorObjectPickedUp() = 0;
	virtual void objectEditorObjectDropped() = 0;
	virtual bool snapToGridCheckBoxChecked() = 0;
	virtual double gridSpacing() = 0;
	virtual bool posAndRot3DControlsEnabled() = 0;
	virtual void startObEditorTimerIfNotActive() = 0;
	virtual void startLightmapFlagTimer() = 0;

	virtual void showAvatarSettings() = 0; // Show avatar settings dialog.
	
	virtual void setUIForSelectedObject() = 0; // Enable/disable delete object action etc. based on if there is a selected object or not.

	
	virtual bool isCursorHidden() = 0;
	virtual void hideCursor() = 0;

	virtual void setCamRotationOnMouseDragEnabled(bool enabled) = 0; // Do we want mouse click + dragging to move the camera?

	virtual void setKeyboardCameraMoveEnabled(bool enabled) = 0; // Do we want WASD keys etc. to move the camera?  We don't want this while e.g. we enter text into a webview.
	virtual bool isKeyboardCameraMoveEnabled() = 0;

	virtual bool hasFocus() = 0; // Does OpenGL widget have focus?

	virtual void setHelpInfoLabelToDefaultText() = 0;
	virtual void setHelpInfoLabel(const std::string& text) = 0;

	virtual void toggleFlyMode() = 0;

	// TODO: simplify this interface
	virtual void enableThirdPersonCamera() = 0;
	virtual void enableThirdPersonCameraIfNotAlreadyEnabled() = 0;
	virtual void toggleThirdPersonCameraMode() = 0;
	virtual void enableFirstPersonCamera() = 0;

	virtual void openURL(const std::string& URL) = 0;

	virtual Vec2i getMouseCursorWidgetPos() = 0; // Get mouse cursor position, relative to gl widget.

	// Credential manager
	virtual std::string getUsernameForDomain(const std::string& domain) = 0; // Returns empty string if no stored username for domain
	virtual std::string getDecryptedPasswordForDomain(const std::string& domain) = 0; // Returns empty string if no stored password for domain

	virtual bool inScreenshotTakingMode() = 0;
	virtual void takeScreenshot() = 0;
	virtual void showScreenshots() = 0;

	virtual void setGLWidgetContextAsCurrent() = 0;

	virtual Vec2i getGlWidgetPosInGlobalSpace() = 0; // Get top left of the GLWidget in global screen coordinates.

	virtual void webViewDataLinkHovered(const std::string& text) = 0;

	// Gamepad
	virtual bool gamepadAttached() = 0;
	virtual float gamepadButtonL2() = 0;
	virtual float gamepadButtonR2() = 0;
	virtual float gamepadAxisLeftX() = 0;
	virtual float gamepadAxisLeftY() = 0;
	virtual float gamepadAxisRightX() = 0;
	virtual float gamepadAxisRightY() = 0;
	

	// OpenGL
	virtual bool supportsSharedGLContexts() const = 0;
	virtual void* makeNewSharedGLContext() = 0;
	virtual void makeGLContextCurrent(void* context) = 0;
};
