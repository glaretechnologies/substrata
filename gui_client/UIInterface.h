/*=====================================================================
UIInterface.h
-------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


class WorldObject;
class Parcel;
#include <graphics/ImageMap.h>
#include <maths/vec2.h>
#include <string>


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
	virtual void showErrorNotification(const std::string& msg) = 0;
	virtual void showInfoNotification(const std::string& msg) = 0;

	// UserDetailsWidget:
	virtual void setTextAsNotLoggedIn() = 0;
	virtual void setTextAsLoggedIn(const std::string& username) = 0;

	// worldSettingsWidget:
	virtual void updateWorldSettingsControlsEditable() = 0;
	virtual void updateWorldSettingsUIFromWorldSettings() = 0;

	virtual bool diagnosticsVisible() = 0;
	virtual bool showObAABBsEnabled() = 0;
	virtual bool showPhysicsObOwnershipEnabled() = 0;
	virtual bool showVehiclePhysicsVisEnabled() = 0;

	virtual void writeTransformMembersToObject(WorldObject& ob) = 0;
	virtual void objectLastModifiedUpdated(const WorldObject& ob) = 0;
	virtual void objectModelURLUpdated(const WorldObject& ob) = 0;
	virtual void objectLightmapURLUpdated(const WorldObject& ob) = 0; // Update lightmap URL in UI if we have selected the object.

	
	virtual void showEditorDockWidget() = 0;
	// Parcel editor
	virtual void showParcelEditor() = 0;
	virtual void setParcelEditorForParcel(const Parcel& parcel) = 0;
	virtual void setParcelEditorEnabled(bool enabled) = 0;
	// Object editor
	virtual void showObjectEditor() = 0;
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
	
	virtual void setUIForSelectedObject() = 0;

	virtual void setCamRotationOnMouseMoveEnabled(bool enabled) = 0;
	virtual bool isCursorHidden() = 0;
	virtual void hideCursor() = 0;

	virtual void setKeyboardCameraMoveEnabled(bool enabled) = 0;
	virtual bool isKeyboardCameraMoveEnabled() = 0;

	virtual bool hasFocus() = 0;

	virtual void setHelpInfoLabelToDefaultText() = 0;
	virtual void setHelpInfoLabel(const std::string& text) = 0;

	virtual void toggleFlyMode() = 0;

	virtual void enableThirdPersonCamera() = 0;
	virtual void enableThirdPersonCameraIfNotAlreadyEnabled() = 0;

	virtual void toggleThirdPersonCameraMode() = 0;

	virtual void firstPersonCameraEnabled() = 0;

	virtual void openURL(const std::string& URL) = 0;

	virtual Vec2i getMouseCursorWidgetPos() = 0;

	// Credential manager
	virtual std::string getUsernameForDomain(const std::string& domain) = 0; // Returns empty string if no stored username for domain
	virtual std::string getDecryptedPasswordForDomain(const std::string& domain) = 0; // Returns empty string if no stored password for domain

	virtual bool inScreenshotTakingMode() = 0;

	virtual Reference<ImageMap<uint8, UInt8ComponentValueTraits>> drawText(const std::string& text, int font_point_size) = 0;
};
