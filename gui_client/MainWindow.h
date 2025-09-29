/*=====================================================================
MainWindow.h
------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "UIInterface.h"
#include "GUIClient.h"
#include "CredentialManager.h"
#include <utils/ArgumentParser.h>
#include <utils/Timer.h>
#include <utils/ComObHandle.h>
#include <utils/SocketBufferOutStream.h>
#include <QtWidgets/QMainWindow>
#include <string>
namespace Ui { class MainWindow; }
namespace glare { class TaskManager; }
class QSettings;
class QSettingsStore;
class UserDetailsWidget;
class URLWidget;
class QLabel;
class LogWindow;
class QMimeData;
struct ID3D11Device;
struct IMFDXGIDeviceManager;
struct _SDL_GameController;
class RenderStatsWidget;


class MainWindow final : public QMainWindow, public PrintOutput, public UIInterface
{
	Q_OBJECT
public:
	MainWindow(const std::string& base_dir_path, const std::string& appdata_path, const ArgumentParser& args, QWidget* parent = 0);
	~MainWindow();

	void initialise();
	void afterGLInitInitialise();

	void logAndConPrintMessage(const std::string& msg); // Print to console, and appends to LogWindow log display.

	// PrintOutput interface
	virtual void print(const std::string& s) override; // Print a message and a newline character.
	virtual void printStr(const std::string& s) override; // Print a message without a newline character.

	// Semicolon is for intellisense, see http://www.qtsoftware.com/developer/faqs/faq.2007-08-23.5900165993
signals:;
	void resolutionChanged(int, int);

private slots:;
	void on_actionAvatarSettings_triggered();
	void on_actionAddObject_triggered();
	void on_actionAddHypercard_triggered();
	void on_actionAdd_Text_triggered();
	void on_actionAdd_Voxels_triggered();
	void on_actionAdd_Spotlight_triggered();
	void on_actionAdd_Web_View_triggered();
	void on_actionAdd_Video_triggered();
	void on_actionAdd_Audio_Source_triggered();
	void on_actionAdd_Decal_triggered();
	void on_actionCopy_Object_triggered();
	void on_actionPaste_Object_triggered();
	void on_actionCloneObject_triggered();
	void on_actionDeleteObject_triggered();
	void on_actionReset_Layout_triggered();
	void on_actionLogIn_triggered();
	void on_actionSignUp_triggered();
	void on_actionLogOut_triggered();
	void on_actionShow_Parcels_triggered();
	void on_actionFly_Mode_triggered();
	void on_actionThird_Person_Camera_triggered();
	void on_actionGoToMainWorld_triggered();
	void on_actionGoToPersonalWorld_triggered();
	void on_actionGo_to_CryptoVoxels_World_triggered();
	void on_actionGo_to_Parcel_triggered();
	void on_actionGo_to_Position_triggered();
	void on_actionSet_Start_Location_triggered();
	void on_actionGo_To_Start_Location_triggered();
	void on_actionFind_Object_triggered();
	void on_actionList_Objects_Nearby_triggered();
	void on_actionExport_view_to_Indigo_triggered();
	void on_actionTake_Screenshot_triggered();
	void on_actionShow_Screenshot_Folder_triggered();
	void on_actionAbout_Substrata_triggered();
	void on_actionOptions_triggered();
	void on_actionUndo_triggered();
	void on_actionRedo_triggered();
	void on_actionShow_Log_triggered();
	void on_actionBake_Lightmaps_fast_for_all_objects_in_parcel_triggered();
	void on_actionBake_lightmaps_high_quality_for_all_objects_in_parcel_triggered();
	void on_actionSummon_Bike_triggered();
	void on_actionSummon_Hovercar_triggered();
	void on_actionSummon_Boat_triggered();
	void on_actionSummon_Car_triggered();
	void on_actionMute_Audio_toggled(bool checked);
	void on_actionSave_Object_To_Disk_triggered();
	void on_actionSave_Parcel_Objects_To_Disk_triggered();
	void on_actionLoad_Objects_From_Disk_triggered();
	void on_actionDelete_All_Parcel_Objects_triggered();
	void on_actionEnter_Fullscreen_triggered();

	void diagnosticsWidgetChanged();
	void diagnosticsReloadTerrain();
	void sendChatMessageSlot();
	void sendLightmapNeededFlagsSlot();

	void glWidgetMousePressed(QMouseEvent* e);
	void glWidgetMouseReleased(QMouseEvent* e);
	void glWidgetMouseDoubleClicked(QMouseEvent* e);
	void glWidgetMouseMoved(QMouseEvent* e);
	void glWidgetKeyPressed(QKeyEvent* e);
	void glWidgetkeyReleased(QKeyEvent* e);
	void glWidgetFocusOut();
	void glWidgetMouseWheelEvent(QWheelEvent* e);
	void gamepadButtonXChanged(bool pressed);
	void gamepadButtonAChanged(bool pressed);
	void glWidgetViewportResized(int w, int h);
	void onIndigoViewDockWidgetVisibilityChanged(bool v);
	void glWidgetCutShortcutTriggered();
	void glWidgetCopyShortcutTriggered();
	void glWidgetPasteShortcutTriggered();

	void enterFullScreenMode();
	void exitFromFullScreenMode();

	void objectTransformEditedSlot();
	void objectEditedSlot();
	void scriptChangedFromEditorSlot();
	void parcelEditedSlot();
	void worldSettingsAppliedSlot();
	void environmentSettingChangedSlot();
	void bakeObjectLightmapSlot(); // Bake the currently selected object lightmap
	void bakeObjectLightmapHighQualSlot(); // Bake the currently selected object lightmap
	void removeLightmapSignalSlot();
	void posAndRot3DControlsToggledSlot();
	void URLChangedSlot();
	void materialSelectedInBrowser(const std::string& path);
	void updateObjectEditorObTransformSlot();
	void handleURL(const QUrl& url);
	void openServerScriptLogSlot();
public:
	bool connectedToUsersWorldOrGodUser();
	void webViewMouseDoubleClicked(QMouseEvent* e);
private:
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	bool nativeEvent(const QByteArray& event_type, void* message, qintptr* result);
#else
	bool nativeEvent(const QByteArray& event_type, void* message, long* result) override;
#endif
	virtual void closeEvent(QCloseEvent* event) override;
	virtual void timerEvent(QTimerEvent* event) override;
	virtual void changeEvent(QEvent *event) override;
	void startMainTimer();
	void visitSubURL(const std::string& URL); // Visit a substrata 'sub://' URL.  Checks hostname and only reconnects if the hostname is different from the current one.
	void doObjectSelectionTraceForMouseEvent(QMouseEvent* e);
private:
	void updateStatusBar();
	void updateDiagnostics();
	void runScreenshotCode();
	void setUpForScreenshot();
	void saveScreenshot(); // Throws glare::Exception on failure

	virtual void dragEnterEvent(QDragEnterEvent* event) override;
	virtual void dropEvent(QDropEvent* event) override;

	void handlePasteOrDropMimeData(const QMimeData* mime_data);

public:
	void showErrorNotification(const std::string& msg);
	void showInfoNotification(const std::string& msg);

	//------------------------------------------------- UIInterface -----------------------------------------------------------
	virtual void appendChatMessage(const std::string& msg) override;
	virtual void clearChatMessages() override;
	virtual bool isShowParcelsEnabled() const override;
	virtual void updateOnlineUsersList() override; // Works off world state avatars.
	virtual void showHTMLMessageBox(const std::string& title, const std::string& msg) override;
	virtual void showPlainTextMessageBox(const std::string& title, const std::string& msg) override;

	virtual void logMessage(const std::string& msg) override; // Appends to LogWindow log display.

	// Lua scripting:
	// A lua script created by the logged in user printed something
	virtual void printFromLuaScript(const std::string& msg, UID object_uid) override;
	virtual void luaErrorOccurred(const std::string& msg, UID object_uid) override;

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

	virtual void showAvatarSettings() override;

	virtual void setCamRotationOnMouseDragEnabled(bool enabled) override;
	virtual bool isCursorHidden() override;
	virtual void hideCursor() override;

	virtual void setKeyboardCameraMoveEnabled(bool enabled) override; // 
	virtual bool isKeyboardCameraMoveEnabled() override;

	virtual bool hasFocus() override;

	virtual void setHelpInfoLabelToDefaultText() override;
	virtual void setHelpInfoLabel(const std::string& text) override;

	
	virtual void enableThirdPersonCamera() override;
	virtual void toggleFlyMode() override;
	virtual void toggleThirdPersonCameraMode() override;
	virtual void enableThirdPersonCameraIfNotAlreadyEnabled() override;
	virtual void enableFirstPersonCamera() override;

	virtual void openURL(const std::string& URL) override;

	virtual Vec2i getMouseCursorWidgetPos() override;

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
	virtual bool gamepadAttached();
	virtual float gamepadButtonL2();
	virtual float gamepadButtonR2();
	virtual float gamepadAxisLeftX();
	virtual float gamepadAxisLeftY();
	virtual float gamepadAxisRightX();
	virtual float gamepadAxisRightY();


	// OpenGL
	virtual bool supportsSharedGLContexts() const override;
	virtual void* makeNewSharedGLContext()  override;
	virtual void makeGLContextCurrent(void* context) override;
	//------------------------------------------------- End UIInterface -----------------------------------------------------------

public:

	std::string base_dir_path;
	std::string appdata_path;
private:
	ArgumentParser parsed_args;

	Timer total_timer;

public:
	Ui::MainWindow* ui;

	SocketBufferOutStream scratch_packet;
	
	std::string screenshot_output_path;
	bool run_as_screenshot_slave;
	Reference<MySocket> screenshot_command_socket;
	bool taking_map_screenshot;
	bool test_screenshot_taking;
	int screenshot_highlight_parcel_id;
	bool done_screenshot_setup;
	int screenshot_width_px;
	float screenshot_ortho_sensor_width_m;
	Vec3d screenshot_campos;
	Vec3d screenshot_camangles;
	
	Timer time_since_last_screenshot;
	Timer time_since_last_waiting_msg;

	
	QSettings* settings;
	Reference<QSettingsStore> settings_store;

	UserDetailsWidget* user_details;
	URLWidget* url_widget;

	double last_timerEvent_CPU_work_elapsed;
	double last_updateGL_time;
private:
	bool need_help_info_dock_widget_position; // We may need to position the Help info dock widget to the bottom right of the GL view.
	// But we need to wait until the gl view has been resized before we do this, so set this flag to do in a timer event.
	
	QTimer* update_ob_editor_transform_timer;
	QTimer* lightmap_flag_timer;
	int main_timer_id; // ID of Main QT timer.

public:
#if defined(_WIN32)
	ComObHandle<ID3D11Device> d3d_device;
	ComObHandle<IMFDXGIDeviceManager> device_manager;
	//HANDLE interop_device_handle;
	//WGL wgl_funcs;
#endif

	LogWindow* log_window;

	bool in_CEF_message_loop;
	bool should_close;
	bool closing; // Timer events keep firing after closeEvent(), annoyingly, so keep track of if we are closing the Window, in which case we can early-out of timerEvent().
	bool running_destructor;

	Reference<OpenGLEngine> opengl_engine;
	
	GUIClient gui_client;

	CredentialManager credential_manager;

	glare::TaskManager* main_task_manager;
	glare::TaskManager* high_priority_task_manager;

	Reference<glare::Allocator> main_mem_allocator;

	//struct _SDL_GameController* game_controller;
	QByteArray pre_fullscreen_window_state;

	Reference<RenderStatsWidget> CPU_render_stats_widget;
	Reference<RenderStatsWidget> GPU_render_stats_widget;
};
