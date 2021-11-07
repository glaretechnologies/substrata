/*=====================================================================
MainWindow.h
------------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#pragma once


#include "PhysicsWorld.h"
#include "ModelLoading.h"
#include "PlayerPhysics.h"
#include "ClientThread.h"
#include "WorldState.h"
#include "CameraController.h"
#include "ProximityLoader.h"
#include "AnimatedTextureManager.h"
#include "UndoBuffer.h"
#include "LogWindow.h"
#include "GestureUI.h"
#include "DownloadingResourceQueue.h"
#include "../opengl/OpenGLEngine.h"
#include "../opengl/TextureLoading.h"
#include "../opengl/WGL.h"
#include "../shared/ResourceManager.h"
#include "../shared/WorldObject.h"
#include "../indigo/ThreadContext.h"
#include "../utils/ArgumentParser.h"
#include "../utils/Timer.h"
#include "../utils/TaskManager.h"
#include "../utils/StandardPrintOutput.h"
#include "../utils/CircularBuffer.h"
#include "../utils/ComObHandle.h"
#include "../maths/PCG32.h"
#include "../maths/LineSegment4f.h"
#include "../video/VideoReader.h"
#include "../audio/AudioEngine.h"
#include <QtCore/QEvent>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtWidgets/QMainWindow>
#include <string>
#include <fstream>
#include <unordered_set>
#include <unordered_map>
#include <deque>
class ArgumentParser;
namespace Ui { class MainWindow; }
class TextureServer;
class QSettings;
class UserDetailsWidget;
class URLWidget;
class QLabel;
class ModelLoadedThreadMessage;
class TextureLoadedThreadMessage;
struct tls_config;
class SubstrataVideoReaderCallback;
struct CreateVidReaderTask;
class BiomeManager;

struct ID3D11Device;
struct IMFDXGIDeviceManager;


class MainWindow : public QMainWindow, public ObLoadingCallbacks, public PrintOutput
{
	Q_OBJECT
public:
	MainWindow(const std::string& base_dir_path, const std::string& appdata_path, const ArgumentParser& args,
		QWidget *parent = 0);
	~MainWindow();

	friend struct AnimatedTexObData;

	void initialise();

	void connectToServer(const std::string& URL/*const std::string& hostname, const std::string& worldname*/); // Disconnect from any current server, connect to new server

	void afterGLInitInitialise();

	void updateGroundPlane();

	void logMessage(const std::string& msg); // Print to console, log file, and append to LogWindow log display

	// PrintOutput interface
	virtual void print(const std::string& s); // Print a message and a newline character.
	virtual void printStr(const std::string& s); // Print a message without a newline character.


	// Semicolon is for intellisense, see http://www.qtsoftware.com/developer/faqs/faq.2007-08-23.5900165993
signals:;
	void resolutionChanged(int, int);

private slots:;
	void on_actionAvatarSettings_triggered();
	void on_actionAddObject_triggered();
	void on_actionAddHypercard_triggered();
	void on_actionAdd_Voxels_triggered();
	void on_actionAdd_Spotlight_triggered();
	void on_actionAdd_Audio_Source_triggered();
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
	void on_actionFind_Object_triggered();
	void on_actionExport_view_to_Indigo_triggered();
	void on_actionTake_Screenshot_triggered();
	void on_actionAbout_Substrata_triggered();
	void on_actionOptions_triggered();
	void on_actionUndo_triggered();
	void on_actionRedo_triggered();
	void on_actionShow_Log_triggered();
	void on_actionBake_Lightmaps_fast_for_all_objects_in_parcel_triggered();
	void on_actionBake_lightmaps_high_quality_for_all_objects_in_parcel_triggered();

	void applyUndoOrRedoObject(const Reference<WorldObject>& ob);

	void sendChatMessageSlot();

	void glWidgetMouseClicked(QMouseEvent* e);
	void glWidgetMousePressed(QMouseEvent* e);
	void glWidgetMouseDoubleClicked(QMouseEvent* e);
	void glWidgetMouseMoved(QMouseEvent* e);
	void glWidgetKeyPressed(QKeyEvent* e);
	void glWidgetkeyReleased(QKeyEvent* e);
	void glWidgetMouseWheelEvent(QWheelEvent* e);
	void glWidgetViewportResized(int w, int h);
	void cameraUpdated();
	void onIndigoViewDockWidgetVisibilityChanged(bool v);

	void objectEditedSlot();
	void bakeObjectLightmapSlot(); // Bake the currently selected object lightmap
	void bakeObjectLightmapHighQualSlot(); // Bake the currently selected object lightmap
	void removeLightmapSignalSlot();
	void posAndRot3DControlsToggledSlot();
	void URLChangedSlot();
	void materialSelectedInBrowser(const std::string& path);
	void sendLightmapNeededFlagsSlot();
	void handleURL(const QUrl &url);
private:
	bool nativeEvent(const QByteArray& event_type, void* message, long* result);
	void closeEvent(QCloseEvent* event);
	virtual void timerEvent(QTimerEvent* event);
	void rotateObject(WorldObjectRef ob, const Vec4f& axis, float angle);
	void selectObject(const WorldObjectRef& ob, int selected_tri_index);
	void deleteSelectedObject();
	void deselectObject();
	void deselectParcel();
	GLObjectRef makeNameTagGLObject(const std::string& nametag);
public:
	OpenGLTextureRef makeToolTipTexture(const std::string& tooltip_text);
private:
	Reference<OpenGLTexture> makeHypercardTexMap(const std::string& content, ImageMapUInt8Ref& uint8_map_out);
	void loadModelForObject(WorldObject* ob);
	void loadModelForAvatar(Avatar* ob);
	void loadScriptForObject(WorldObject* ob);
	void doBiomeScatteringForObject(WorldObject* ob);
	void loadAudioForObject(WorldObject* ob);
	void showErrorNotification(const std::string& message);
	void showInfoNotification(const std::string& message);
	void startDownloadingResourcesForObject(WorldObject* ob, int ob_lod_level);
	void startDownloadingResourcesForAvatar(Avatar* ob, int ob_lod_level);
	void startDownloadingResource(const std::string& url, const Vec4f& pos_ws, const js::AABBox& ob_aabb_ws); // For every resource that the object uses (model, textures etc..), if the resource is not present locally, start downloading it.
	void evalObjectScript(WorldObject* ob, float use_global_time);
	void updateStatusBar();
	bool haveParcelObjectCreatePermissions(const Vec3d& new_ob_pos, bool& in_parcel_out);
	bool haveObjectWritePermissions(const js::AABBox& new_aabb_ws, bool& ob_pos_in_parcel_out);
	void addParcelObjects();
	void removeParcelObjects();
	void recolourParcelsForLoggedInState();
	void updateSelectedObjectPlacementBeam();
	void updateInstancedCopiesOfObject(WorldObject* ob);
	void removeInstancesOfObject(WorldObject* ob);
	void bakeLightmapsForAllObjectsInParcel(uint32 lightmap_flag);
	
	bool objectModificationAllowed(const WorldObject& ob);
	bool objectModificationAllowedWithMsg(const WorldObject& ob, const std::string& action); // Also shows error notifications if modification is not allowed.
	// Action will be printed in error message, could be "modify" or "delete"
	bool objectIsInParcelOwnedByLoggedInUser(const WorldObject& ob);

	void updateOnlineUsersList(); // Works off world state avatars.
	bool areEditingVoxels();
	Vec4f getDirForPixelTrace(int pixel_pos_x, int pixel_pos_y);

	bool getPixelForPoint(const Vec4f& point_ws, Vec2f& pixel_coords_out); // Returns true if point is visible from camera.
	Vec4f pointOnLineWorldSpace(const Vec4f& p_a_ws, const Vec4f& p_b_ws, const Vec2f& pixel_coords);

	void updateVoxelEditMarkers();
	void pickUpSelectedObject();
	void dropSelectedObject();
	void setUIForSelectedObject(); // Enable/disable delete object action etc..

	void checkForLODChanges();

	int mouseOverAxisArrowOrRotArc(const Vec2f& pixel_coords, Vec4f& closest_seg_point_ws_out); // Returns closest axis arrow or -1 if no close.

	struct EdgeMarker
	{
		EdgeMarker(const Vec4f& p, const Vec4f& n, float scale_) : pos(p), normal(n), scale(scale_) {}
		EdgeMarker() {}
		Vec4f pos;
		Vec4f normal;
		float scale;
	};
	
	// If the object was not in a parcel with write permissions at all, returns false.
	// If the object can not be made to fit in the current parcel, returns false.
	// new_ob_pos_out is set to new, clamped position.
	bool clampObjectPositionToParcelForNewTransform(GLObjectRef& opengl_ob, const Vec3d& old_ob_pos,
		const Matrix4f& tentative_to_world_matrix, js::Vector<EdgeMarker, 16>& edge_markers_out, Vec3d& new_ob_pos_out);
public:
	bool checkAddTextureToProcessedSet(const std::string& path); // returns true if was not in processed set (and hence this call added it), false if it was.
	bool isTextureProcessed(const std::string& path) const;
	
	bool checkAddModelToProcessedSet(const std::string& url); // returns true if was not in processed set (and hence this call added it), false if it was.
	bool isModelProcessed(const std::string& url) const;

	bool checkAddAudioToProcessedSet(const std::string& url); // returns true if was not in processed set (and hence this call added it), false if it was.
	bool isAudioProcessed(const std::string& url) const;

	void startLoadingTexturesForObject(const WorldObject& ob, int ob_lod_level);
	void startLoadingTexturesForAvatar(const Avatar& ob, int ob_lod_level);
	void removeAndDeleteGLAndPhysicsObjectsForOb(WorldObject& ob);
	void removeAndDeleteGLObjectForAvatar(Avatar& ob);
	void addPlaceholderObjectsForOb(WorldObject& ob);
	void setUpForScreenshot();
	void saveScreenshot();

	// ObLoadingCallbacks interface
	virtual void loadObject(WorldObjectRef ob);
	virtual void unloadObject(WorldObjectRef ob);
	virtual void newCellInProximity(const Vec3<int>& cell_coords);

	void tryToMoveObject(/*const Matrix4f& tentative_new_to_world*/const Vec4f& desired_new_ob_pos);

	void updateObjectModelForChangedDecompressedVoxels(WorldObjectRef& ob);

	void performGestureClicked(const std::string& gesture_name, bool animate_head, bool loop_anim);
	void stopGestureClicked(const std::string& gesture_name);
	void setSelfieModeEnabled(bool enabled);

	//BuildUInt8MapTextureDataScratchState build_uint8_map_scratch_state;

	std::string base_dir_path;
	std::string appdata_path;
private:
	ArgumentParser parsed_args;

	Timer total_timer;

public:
	Ui::MainWindow* ui;

	CameraController cam_controller;

	Reference<PhysicsWorld> physics_world;

	PlayerPhysics player_physics;

	Timer time_since_last_timer_ev;
	Timer time_since_update_packet_sent;

	Reference<ClientThread> client_thread;
	ThreadManager client_thread_manager;
	ThreadManager resource_upload_thread_manager;
	ThreadManager resource_download_thread_manager;
	ThreadManager net_resource_download_thread_manager;
	ThreadManager save_resources_db_thread_manager;

	glare::AtomicInt num_non_net_resources_downloading;
	glare::AtomicInt num_net_resources_downloading;
	glare::AtomicInt num_resources_uploading;

	Reference<WorldState> world_state;

	//std::map<const Avatar*, GLObjectRef> avatar_gl_objects;
	//std::map<GLObjectRef*, bool> avatars_

	TextureServer* texture_server;

	QSettings* settings;

	ThreadSafeQueue<Reference<ThreadMessage> > msg_queue; // for messages from ClientThread etc.. to this object.

	WorldObjectRef selected_ob;
	Vec4f selection_vec_cs; // Vector from camera to selected point on object, in camera space
	Vec4f selection_point_os; // Point on selected object where selection ray hit, in object space.
	bool selected_ob_picked_up; // Is selected object 'picked up' e.g. being moved?

	ParcelRef selected_parcel;

	std::string resources_dir;
	Reference<ResourceManager> resource_manager;

	std::unordered_set<WorldObjectRef, WorldObjectRefHash> active_objects; // Objects that have moved recently and so need interpolation done on them.
	std::unordered_map<WorldObjectRef, AnimatedTexObData, WorldObjectRefHash> obs_with_animated_tex; // Objects with animated textures (e.g. gifs)

	ThreadContext thread_context;

	//std::ofstream logfile;

	UserDetailsWidget* user_details;
	URLWidget* url_widget;

	enum ServerConnectionState
	{
		ServerConnectionState_NotConnected,
		ServerConnectionState_Connecting,
		ServerConnectionState_Connected
	};
	ServerConnectionState connection_state;

	UserID logged_in_user_id;
	std::string logged_in_user_name;

	bool shown_object_modification_error_msg;

	Reference<Indigo::Mesh> ground_quad_mesh;
	Reference<OpenGLMeshRenderData> ground_quad_mesh_opengl_data;
	Reference<RayMesh> ground_quad_raymesh;

	struct GroundQuad
	{
		GLObjectRef gl_ob;
		Reference<PhysicsObject> phy_ob;
	};
	std::map<Vec2i, GroundQuad> ground_quads;

	ProximityLoader proximity_loader;

	Reference<OpenGLMeshRenderData> hypercard_quad_opengl_mesh; // Also used for name tags.
	Reference<RayMesh> hypercard_quad_raymesh;

	Reference<Indigo::Mesh> spotlight_mesh;
	Reference<OpenGLMeshRenderData> spotlight_opengl_mesh;
	Reference<RayMesh> spotlight_raymesh;


	Reference<RayMesh> unit_cube_raymesh;

	Reference<GLObject> ob_placement_beam;
	Reference<GLObject> ob_placement_marker;

	Reference<GLObject> voxel_edit_marker;
	bool voxel_edit_marker_in_engine;
	Reference<GLObject> voxel_edit_face_marker;
	bool voxel_edit_face_marker_in_engine;

	Reference<GLObject> ob_denied_move_marker; // Prototype object
	std::vector<Reference<GLObject> > ob_denied_move_markers;

	LineSegment4f axis_arrow_segments[3];
	GLObjectRef axis_arrow_objects[3]; // For ob placement

	std::vector<LineSegment4f> rot_handle_lines[3];
	GLObjectRef rot_handle_arc_objects[3];

	int grabbed_axis; // -1 if no axis grabbed, [0, 3) if grabbed a translation arrow, [3, 6) if grabbed a rotation arc.
	Vec4f grabbed_point_ws; // Approximate point on arrow line we grabbed, in world space.
	Vec4f ob_origin_at_grab;

	float grabbed_angle;
	float original_grabbed_angle;
	float grabbed_arc_angle_offset;

	Reference<OpenGLProgram> parcel_shader_prog;

	StandardPrintOutput print_output;
	glare::TaskManager task_manager; // General purpose task manager, for quick/blocking multithreaded builds of stuff.
	
	glare::TaskManager model_and_texture_loader_task_manager;
public:
private:
	glare::TaskManager model_building_subsidary_task_manager; // Just for use in ModelLoading::makeGLObjectForModelURLAndMaterials in LoadModelTask etc..
public:
	MeshManager mesh_manager;
private:
	struct Notification
	{
		double creation_time;
		QLabel* label;
	};

	std::list<Notification> notifications;

	bool need_help_info_dock_widget_position; // We may need to position the Help info dock widget to the bottom right of the GL view.
	// But we need to wait until the gl view has been reszied before we do this, so set this flag to do in a timer event.

	std::string server_hostname; // e.g. "substrata.info" or "localhost"
	std::string server_worldname; // e.g. "" or "ono-sendai"
	int url_parcel_uid; // Was there a parcel UID in the URL? e.g. was it like sub://localhost/parcel/200?  If so we want to move there when the parcels are loaded and we know where it is. 
	// -1 if no parcel UID in URL.

	Timer fps_display_timer;
	int num_frames_since_fps_timer_reset;
	double last_fps;

	// ModelLoadedThreadMessages that have been sent to this thread, but are still to be processed.
	std::deque<Reference<ModelLoadedThreadMessage> > model_loaded_messages_to_process;
	
	std::deque<Reference<TextureLoadedThreadMessage> > texture_loaded_messages_to_process;
	
	bool process_model_loaded_next;



	mutable Mutex textures_processed_mutex;
	// Textures being loaded or already loaded.
	// We have this set so that we don't process the same texture from multiple LoadTextureTasks running in parallel.
	std::unordered_set<std::string> textures_processed;

	mutable Mutex models_processed_mutex;
	// Models being loaded or already loaded.
	// We have this set so that we don't process the same model from multiple LoadModelTasks running in parallel.
	std::unordered_set<std::string> models_processed;

	mutable Mutex audio_processed_mutex;
	// Audio files being loaded or already loaded.
	// We have this set so that we don't process the same audio from multiple LoadAudioTasks running in parallel.
	std::unordered_set<std::string> audio_processed;


	QTimer* lightmap_flag_timer;

	std::set<WorldObjectRef> objs_with_lightmap_rebuild_needed;

	Timer stats_timer;

public:
	Vec3d screenshot_campos;
	Vec3d screenshot_camangles;
	int screenshot_width_px;
	float screenshot_ortho_sensor_width_m;
	int screenshot_highlight_parcel_id;
	std::string screenshot_output_path;
	bool done_screenshot_setup;
	bool run_as_screenshot_slave;
	bool taking_map_screenshot;
	MySocketRef screenshot_command_socket;
	Timer time_since_last_screenshot;

private:
	struct tls_config* client_tls_config;

	PCG32 rng;
#if defined(_WIN32)
	ComObHandle<ID3D11Device> d3d_device;
	ComObHandle<IMFDXGIDeviceManager> device_manager;
	HANDLE interop_device_handle;

	WGL wgl_funcs;
#endif

	glare::AudioEngine audio_engine;

	UndoBuffer undo_buffer;

	LogWindow* log_window;
public:
	std::vector<GLObjectRef> test_obs;
	std::vector<glare::AudioSourceRef> test_srcs;

	//std::vector<AudioSourceRef> footstep_sources;

	Timer last_footstep_timer;
	int last_foostep_side;

	double last_timerEvent_CPU_work_elapsed;

	Timer time_since_object_edited; // For undo edit merging.
	bool force_new_undo_edit; // // Multiple edits using the object editor, in a short timespan, will be merged together, unless force_new_undo_edit is true (is set when undo or redo is issued).
	std::map<UID, UID> recreated_ob_uid; // Map from old object UID to recreated object UID when an object deletion is undone.

	UID last_restored_ob_uid_in_edit;


	GestureUI gesture_ui;

	bool running_destructor;

	BiomeManager* biome_manager;

	DownloadingResourceQueue download_queue;
	Timer download_queue_sort_timer;

	SocketBufferOutStream scratch_packet;
};
