/*=====================================================================
GUIClient.h
------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "PhysicsWorld.h"
#include "PlayerPhysics.h"
#include "CameraController.h"
#include "UIInterface.h"
#include "ProximityLoader.h"
#include "UndoBuffer.h"
#include "GestureUI.h"
#include "ObInfoUI.h"
#include "MiscInfoUI.h"
#include "HeadUpDisplayUI.h"
#include "PhotoModeUI.h"
#include "ChatUI.h"
#include "DownloadingResourceQueue.h"
#include "LoadItemQueue.h"
#include "MeshManager.h"
#include "URLParser.h"
#include "WorldState.h"
#include "EmscriptenResourceDownloader.h"
#include "UploadResourceThread.h"
#include "ScriptedObjectProximityChecker.h"
#include "../shared/WorldSettings.h"
#include "../shared/WorldDetails.h"
#include "../audio/AudioEngine.h"
#include "../audio/MicReadThread.h" // For MicReadStatus
#include "../opengl/TextureLoading.h"
#include "../opengl/PBOAsyncTextureUploader.h"
#include "../opengl/AsyncGeometryUploader.h"
#include "../shared/WorldObject.h"
#include "../shared/LuaScriptEvaluator.h"
#include "../shared/TimerQueue.h"
#include "../settings/SettingsStore.h"
#include <ui/UIEvents.h>
#include <utils/ArgumentParser.h>
#include <utils/Timer.h>
#include <utils/StackAllocator.h>
#include <utils/TaskManager.h>
#include <utils/StandardPrintOutput.h>
#include <utils/SocketBufferOutStream.h>
#include <utils/GenerationalArray.h>
#include <utils/UniqueRef.h>
#include <utils/ArenaAllocator.h>
#include <maths/PCG32.h>
#include <maths/LineSegment4f.h>
#include <networking/IPAddress.h>
#include <string>
#include <unordered_set>
#include <deque>
class UDPSocket;
namespace Ui { class MainWindow; }
class TextureServer;
class UserDetailsWidget;
class URLWidget;
class ModelLoadedThreadMessage;
class TextureLoadedThreadMessage;
struct tls_config;
class SubstrataVideoReaderCallback;
struct CreateVidReaderTask;
class BiomeManager;
class ScriptLoadedThreadMessage;
class ObjectPathController;
namespace glare { class FastPoolAllocator; }
class VehiclePhysics;
class TerrainSystem;
class TerrainDecalManager;
class ParticleManager;
struct Particle;
class ClientThread;
class MySocket;
class LogWindow;
class ResourceManager;
struct ID3D11Device;
struct IMFDXGIDeviceManager;
class SettingsStore;
class TextRendererFontFace;
class Resource;
class AsyncTextureLoader;
class SubstrataLuaVM;
struct LoadedBuffer;
struct AsyncUploadedGeometryInfo;
struct PBOAsyncUploadedTextureInfo;
class OpenGLUploadThread;
class AnimatedTextureManager;
class MiniMap;
class VBOPool;
class PBOPool;
class VBO;
class PBO;


struct ResourceUserList
{
	SmallVector<UID, 4> using_object_uids; // UIDs of objects that use the resource.
};


struct DownloadingResourceInfo
{
	DownloadingResourceInfo() : build_physics_ob(true), build_dynamic_physics_ob(false), used_by_terrain(false), used_by_other(false) {}

	TextureParams texture_params; // For downloading textures.  We keep track of this so we can load e.g. metallic-roughness textures into the OpenGL engine without sRGB.

	bool build_physics_ob; // For downloading meshes.  Will be set to false for LODChunks.
	bool build_dynamic_physics_ob; // For downloading meshes.  Once the mesh is downloaded we need to know if we want to build the dynamic or static physics shape for it.

	Vec3d pos; // Position of object using the resource
	float size_factor;

	ResourceUserList using_objects;
	//SmallVector<UID, 4> using_avatar_uids; // UIDs of avatars that use the resource.

	bool used_by_terrain;
	bool used_by_other; // avatar or minimap or LOD chunk
};


//extern float proj_len_viewable_threshold; // TEMP for tweaking with ImGui.


/*=====================================================================
GUIClient
---------------
GUI client code that is used by both the Native GUI client that uses Qt,
and the web client that uses SDL.
=====================================================================*/
class GUIClient : public ObLoadingCallbacks, public PrintOutput, public PhysicsWorldEventListener, public LuaScriptOutputHandler
{
public:
	GUIClient(const std::string& base_dir_path, const std::string& appdata_path, const ArgumentParser& args);
	~GUIClient();


	static const int server_port = 7600;
	static const int server_UDP_port = 7601;


	static void staticInit();
	static void staticShutdown();

	void initialise(const std::string& cache_dir, const Reference<SettingsStore>& settings_store, UIInterface* ui_interface, glare::TaskManager* high_priority_task_manager_, Reference<glare::Allocator> worker_allocator);
	void afterGLInitInitialise(double device_pixel_ratio, Reference<OpenGLEngine> opengl_engine, 
		const TextRendererFontFaceSizeSetRef& fonts, const TextRendererFontFaceSizeSetRef& emoji_fonts);

	void initAudioEngine();

	void shutdown();

	void timerEvent(const MouseCursorState& mouse_cursor_state);


	Reference<SettingsStore> getSettingsStore() { return settings; }

	void setGLWidgetContextAsCurrent();
	Vec2i getGlWidgetPosInGlobalSpace();

	void webViewDataLinkHovered(const std::string& text);

	void logMessage(const std::string& msg);
	void logAndConPrintMessage(const std::string& msg);

	//----------------------- PrintOutput interface -----------------------
	virtual void print(const std::string& s) override;
	virtual void printStr(const std::string& s) override;
	//----------------------- End PrintOutput interface -----------------------

	void performGestureClicked(const std::string& gesture_name, bool animate_head, bool loop_anim);
	void stopGestureClicked(const std::string& gesture_name);
	void stopGesture();
	void setSelfieModeEnabled(bool enabled);
	void setPhotoModeEnabled(bool enabled);
	void setMicForVoiceChatEnabled(bool enabled);

	void startDownloadingResourcesForObject(WorldObject* ob, int ob_lod_level);
	void startDownloadingResourcesForAvatar(Avatar* ob, int ob_lod_level, bool our_avatar);

	void startDownloadingResource(const URLString& url, const Vec4f& centroid_ws, float aabb_ws_longest_len, const DownloadingResourceInfo& resouce_info); // For every resource that the object uses (model, textures etc..), if the resource is not present locally, start downloading it.
	
	std::string getDiagnosticsString(bool do_graphics_diagnostics, bool do_physics_diagnostics, bool do_terrain_diagnostics, double last_timerEvent_CPU_work_elapsed, double last_updateGL_time);
	void diagnosticsSettingsChanged();
	void updateVoxelEditMarkers(const MouseCursorState& mouse_cursor_state);

	struct EdgeMarker
	{
		EdgeMarker(const Vec4f& p, const Vec4f& n, float scale_) : pos(p), normal(n), scale(scale_) {}
		EdgeMarker() {}
		Vec4f pos;
		Vec4f normal;
		float scale;
	};
	
	void thirdPersonCameraToggled(bool enabled);
	void applyUndoOrRedoObject(const WorldObjectRef& restored_ob);
	void summonBike();
	void summonHovercar();
	void summonBoat();
	void summonCar();
	void objectTransformEdited();
	void objectEdited();
	void posAndRot3DControlsToggled(bool enabled);
	void mousePressed(MouseEvent& e);
	void mouseReleased(MouseEvent& e);
	void mouseDoubleClicked(MouseEvent& e);
	void doObjectSelectionTraceForMouseEvent(MouseEvent& e);
	void updateInfoUIForMousePosition(const Vec2i& cursor_pos, const Vec2f& gl_coords, MouseEvent* mouse_event, bool cursor_is_mouse_cursor);
	void mouseMoved(MouseEvent& mouse_event);
	void onMouseWheelEvent(MouseWheelEvent& e);
	void gamepadButtonXChanged(bool pressed);
	void gamepadButtonAChanged(bool pressed);
	void viewportResized(int w, int h);
	void updateGroundPlane();
	void sendLightmapNeededFlagsSlot();
	void useActionTriggered(bool use_mouse_cursor); // if use_mouse_cursor is false, use crosshair as cursor instead.
	void loginButtonClicked();
	void signupButtonClicked();
	void loggedInButtonClicked();
	std::string getCurrentWebClientURLPath() const;
public:
	void rotateObject(WorldObjectRef ob, const Vec4f& axis, float angle);
	void selectObject(const WorldObjectRef& ob, int selected_mat_index);
	void deleteSelectedObject();
	void deselectObject();
	void deselectParcel();
	void visitSubURL(const std::string& URL); // Visit a substrata 'sub://' URL.  Checks hostname and only reconnects if the hostname is different from the current one.
	GLObjectRef makeNameTagGLObject(const std::string& nametag);
	GLObjectRef makeSpeakerGLObject();
public:
	void loadModelForObject(WorldObject* ob, WorldStateLock& world_state_lock);
	void loadPresentObjectGraphicsAndPhysicsModels(WorldObject* ob, const Reference<MeshData>& mesh_data, const Reference<PhysicsShapeData>& physics_shape_data, int ob_lod_level, int ob_model_lod_level, int voxel_subsample_factor, WorldStateLock& world_state_lock);
	void loadPresentAvatarModel(Avatar* avatar, int av_lod_level, const Reference<MeshData>& mesh_data);
	void loadModelForAvatar(Avatar* avatar);
	void loadScriptForObject(WorldObject* ob, WorldStateLock& world_state_lock);
	void handleScriptLoadedForObUsingScript(ScriptLoadedThreadMessage* loaded_msg, WorldObject* ob);
	void doBiomeScatteringForObject(WorldObject* ob);
	void loadAudioForObject(WorldObject* ob, const Reference<LoadedBuffer>& loaded_buffer);
	void showErrorNotification(const std::string& message);
	void showInfoNotification(const std::string& message);
	void showScriptMessage(const std::string& message);
	void updateNotifications(double cur_time);
	void setNotificationsVisible(bool visible);
	void updateParcelGraphics();
	void assignLODChunkSubMeshPlaceholderToOb(const LODChunk* chunk, WorldObject* const ob);
	void updateLODChunkGraphics();
	void updateAvatarGraphics(double cur_time, double dt, const Vec3d& cam_angles, bool our_move_impulse_zero);
	void setThirdPersonCameraPosition(double dt);
	void handleMessages(double global_time, double cur_time);
	bool haveParcelObjectCreatePermissions(const Vec3d& new_ob_pos, bool& in_parcel_out);
	bool haveObjectWritePermissions(const WorldObject& ob, const js::AABBox& new_aabb_ws, bool& ob_pos_in_parcel_out);
	void addParcelObjects();
	void removeParcelObjects();
	void recolourParcelsForLoggedInState();
	void updateSelectedObjectPlacementBeamAndGizmos();
	void updateInstancedCopiesOfObject(WorldObject* ob);
	void removeInstancesOfObject(WorldObject* ob);
	void removeObScriptingInfo(WorldObject* ob);
	void bakeLightmapsForAllObjectsInParcel(uint32 lightmap_flag);
	std::string serialiseAllObjectsInParcelToXML(size_t& num_obs_serialised_out);
	void deleteAllParcelObjects(size_t& num_obs_deleted_out);
	void setMaterialFlagsForObject(WorldObject* ob);
	bool isResourceCurrentlyNeededForObject(const URLString& url, const WorldObject* ob) const;
	bool isResourceCurrentlyNeededForObjectGivenIsDependency(const URLString& url, const WorldObject* ob) const;
	bool isDownloadingResourceCurrentlyNeeded(const URLString& url) const;
public:
	bool objectModificationAllowed(const WorldObject& ob);
	bool connectedToUsersWorldOrGodUser();
	bool objectModificationAllowedWithMsg(const WorldObject& ob, const std::string& action); // Also shows error notifications if modification is not allowed.
	// Action will be printed in error message, could be "modify" or "delete"
	bool objectIsInParcelForWhichLoggedInUserHasWritePerms(const WorldObject& ob) const;
	bool selectedObjectIsVoxelOb() const;
	bool isObjectWithPosition(const Vec3d& pos);
	Vec4f getDirForPixelTrace(int pixel_pos_x, int pixel_pos_y) const;
public:
	bool getPixelForPoint(const Vec4f& point_ws, Vec2f& pixel_coords_out) const; // Get screen-space coordinates for a world-space point.  Returns true if point is visible from camera.
	bool getGLUICoordsForPoint(const Vec4f& point_ws, Vec2f& coords_out) const; // Returns true if point is visible from camera.
	Vec4f pointOnLineWorldSpace(const Vec4f& p_a_ws, const Vec4f& p_b_ws, const Vec2f& pixel_coords) const;

	void pickUpSelectedObject();
	void dropSelectedObject();

	void checkForLODChanges(Timer& timer_event_timer);
	void checkForAudioRangeChanges();

	int mouseOverAxisArrowOrRotArc(const Vec2f& pixel_coords, Vec4f& closest_seg_point_ws_out); // Returns closest axis arrow or -1 if no close.
	void sendChatMessage(const std::string& message);

	// If the object was not in a parcel with write permissions at all, returns false.
	// If the object can not be made to fit in the current parcel, returns false.
	// new_ob_pos_out is set to new, clamped position.
	bool clampObjectPositionToParcelForNewTransform(const WorldObject& ob, GLObjectRef& opengl_ob, const Vec3d& old_ob_pos,
		const Matrix4f& tentative_to_world_matrix, js::Vector<EdgeMarker, 16>& edge_markers_out, Vec3d& new_ob_pos_out);
	bool checkAddTextureToProcessingSet(const OpenGLTextureKey& path); // returns true if was not in processed set (and hence this call added it), false if it was.
	bool checkAddModelToProcessingSet(const URLString& url, bool dynamic_physics_shape); // returns true if was not in processed set (and hence this call added it), false if it was.
	bool checkAddAudioToProcessingSet(const URLString& url); // returns true if was not in processed set (and hence this call added it), false if it was.
	bool checkAddScriptToProcessingSet(const std::string& script_content); // returns true if was not in processed set (and hence this call added it), false if it was.

	void startLoadingTextureIfPresent(const URLString& tex_url, const Vec4f& centroid_ws, float aabb_ws_longest_len, float max_task_dist, float importance_factor, 
		const TextureParams& tex_params);
	void startLoadingTextureForLocalPath(const OpenGLTextureKey& local_abs_tex_path, const Reference<Resource>& resource, const Vec4f& centroid_ws, float aabb_ws_longest_len, float max_task_dist, float importance_factor, 
		const TextureParams& tex_params);
	void startLoadingTextureForObjectOrAvatar(const UID& ob_uid, const UID& avatar_uid, const Vec4f& centroid_ws, float aabb_ws_longest_len, float max_dist_for_ob_lod_level, float max_dist_for_ob_lod_level_clamped_0, float importance_factor, const WorldMaterial& world_mat, 
		int ob_lod_level, const URLString& texture_url, bool tex_has_alpha, bool use_sRGB, bool allow_compression);
	void startLoadingTexturesForObject(const WorldObject& ob, int ob_lod_level, float max_dist_for_ob_lod_level, float max_dist_for_ob_lod_level_clamped_0);
	void startLoadingTexturesForAvatar(const Avatar& ob, int ob_lod_level, float max_dist_for_ob_lod_level, bool our_avatar);
	void removeAndDeleteGLObjectsForOb(WorldObject& ob);
	void removeAndDeleteGLAndPhysicsObjectsForOb(WorldObject& ob);
	void removeAndDeleteGLObjectForAvatar(Avatar& ob);

	//----------------------- ObLoadingCallbacks interface -----------------------
	//virtual void loadObject(WorldObjectRef ob);
	virtual void unloadObject(WorldObjectRef ob) override;
	virtual void newCellInProximity(const Vec3<int>& cell_coords) override;
	//----------------------- End ObLoadingCallbacks interface -----------------------

	void tryToMoveObject(WorldObjectRef ob, /*const Matrix4f& tentative_new_to_world*/const Vec4f& desired_new_ob_pos);
	void doMoveObject(WorldObjectRef ob, const Vec3d& new_ob_pos, const js::AABBox& aabb_os) REQUIRES(world_state->mutex);
	void doMoveAndRotateObject(WorldObjectRef ob, const Vec3d& new_ob_pos, const Vec3f& new_axis, float new_angle, const js::AABBox& aabb_os, bool summoning_object) REQUIRES(world_state->mutex);

	void updateObjectModelForChangedDecompressedVoxels(WorldObjectRef& ob);

	void createObject(const std::string& mesh_path, BatchedMeshRef loaded_mesh, bool loaded_mesh_is_image_cube,
		const glare::AllocatorVector<Voxel, 16>& decompressed_voxels, const Vec3d& ob_pos, const Vec3f& scale, const Vec3f& axis, float angle, const std::vector<WorldMaterialRef>& materials);
	void createObjectLoadedFromXML(WorldObjectRef ob, PrintOutput& use_print_output);
	void createImageObject(const std::string& local_image_path);
	void createModelObject(const std::string& local_model_path);
	void createImageObjectForWidthAndHeight(const std::string& local_image_path, int w, int h, bool has_alpha);

	void keyPressed(KeyEvent& e);
	void keyReleased(KeyEvent& e);
	void handleTextInputEvent(TextInputEvent& text_input_event);
	void focusOut();

	void disconnectFromServerAndClearAllObjects(); // Remove any WorldObjectRefs held by MainWindow.

	void connectToServer(const URLParseResults& url_results);

	void processLoading(Timer& timer_event_timer);
	void sendGeometryDataToGarbageDeleterThread(const Reference<OpenGLMeshRenderData>& gl_meshdata);
	void sendWinterShaderEvaluatorToGarbageDeleterThread(const Reference<WinterShaderEvaluator>& script_evaluator);
	ObjectPathController* getPathControllerForOb(const WorldObject& ob);
	void createPathControlledPathVisObjects(const WorldObject& ob);
	Reference<VehiclePhysics> createVehicleControllerForScript(WorldObject* ob);
	
	bool isObjectPhysicsOwnedBySelf(WorldObject& ob, double global_time) const;
	bool isObjectPhysicsOwnedByOther(WorldObject& ob, double global_time) const;
	bool isObjectPhysicsOwned(WorldObject& ob, double global_time);
	bool isObjectVehicleBeingDrivenByOther(WorldObject& ob) REQUIRES(world_state->mutex);
	bool doesVehicleHaveAvatarInSeat(WorldObject& ob, uint32 seat_index) const REQUIRES(world_state->mutex);
	void destroyVehiclePhysicsControllingObject(WorldObject* ob);
	void takePhysicsOwnershipOfObject(WorldObject& ob, double global_time);
	void checkRenewalOfPhysicsOwnershipOfObject(WorldObject& ob, double global_time);
	
	void updateDiagnosticAABBForObject(WorldObject* ob); // Returns if vis still valid/needed.
	void updateObjectsWithDiagnosticVis();

	void processPlayerPhysicsInput(float dt, bool world_render_has_keyboard_focus, PlayerPhysicsInput& input_out);

	void enableMaterialisationEffectOnOb(WorldObject& ob);
	void enableMaterialisationEffectOnAvatar(Avatar& ob);

	void createGLAndPhysicsObsForText(const Matrix4f& ob_to_world_matrix, WorldObject* ob, bool use_materialise_effect, PhysicsObjectRef& physics_ob_out, GLObjectRef& opengl_ob_out);
	void updateSpotlightGraphicsEngineData(const Matrix4f& ob_to_world_matrix, WorldObject* ob);
	void recreateTextGraphicsAndPhysicsObs(WorldObject* ob);

	void handleLODChunkMeshLoaded(const URLString& mesh_URL, Reference<MeshData> mesh_data, WorldStateLock& lock);

	void assignLoadedOpenGLTexturesToMats(WorldObject* ob);

	void handleUploadedMeshData(const URLString& lod_model_url, int loaded_model_lod_level, bool dynamic_physics_shape, OpenGLMeshRenderDataRef mesh_data, PhysicsShape& physics_shape, 
		int voxel_subsample_factor, uint64 voxel_hash);
	void handleUploadedTexture(const OpenGLTextureKey& path, const URLString& URL, const OpenGLTextureRef& opengl_tex, const TextureDataRef& tex_data, const Map2DRef& terrain_map);

	void updateOurAvatarModel(BatchedMeshRef loaded_mesh, const std::string& local_model_path, const Matrix4f& pre_ob_to_world_matrix, const std::vector<WorldMaterialRef>& materials);

	//----------------------- LuaScriptOutputHandler interface -----------------------
	virtual void printFromLuaScript(LuaScript* script, const char* s, size_t len) override;
	virtual void errorOccurredFromLuaScript(LuaScript* script, const std::string& msg) override;
	//----------------------- end LuaScriptOutputHandler interface -----------------------

public:
	//----------------------- PhysicsWorldEventListener interface -----------------------
	virtual void physicsObjectEnteredWater(PhysicsObject& ob) override;

	// NOTE: called off main thread, needs to be threadsafe
	virtual void contactAdded(const JPH::Body &inBody1, const JPH::Body &inBody2/*PhysicsObject* ob_a, PhysicsObject* ob_b*/, const JPH::ContactManifold& contact_manifold) override;

	// NOTE: called off main thread, needs to be threadsafe
	virtual void contactPersisted(const JPH::Body &inBody1, const JPH::Body &inBody2/*PhysicsObject* ob_a, PhysicsObject* ob_b*/, const JPH::ContactManifold& contact_manifold) override;
	//----------------------- end PhysicsWorldEventListener interface -----------------------
	

	Reference<TextureLoadedThreadMessage> allocTextureLoadedThreadMessage();

public:
	Reference<OpenGLEngine> opengl_engine;

	Reference<SettingsStore> settings;


	std::string base_dir_path;
	std::string resources_dir_path; // = base_dir_path + "/data/resources"
	std::string appdata_path;
	ArgumentParser parsed_args;

	CameraController cam_controller;

	Reference<PhysicsWorld> physics_world;

	PlayerPhysics player_physics;

	std::map<WorldObject*, Reference<VehiclePhysics>> vehicle_controllers; // Map from controlled object to vehicle controller for that object.
	Reference<VehiclePhysics> vehicle_controller_inside; // Vehicle controller that is controlling the vehicle the user is currently inside of.
	uint32 cur_seat_index; // Current vehicle seat index.

	double last_vehicle_renewal_msg_time;

	Timer time_since_last_timer_ev;
	Timer time_since_update_packet_sent;

	Reference<UDPSocket> udp_socket;

	Reference<ClientThread> client_thread;
	ThreadManager client_thread_manager;
	ThreadManager client_udp_handler_thread_manager;
	ThreadManager mic_read_thread_manager;
	ThreadManager resource_upload_thread_manager;
	ThreadManager resource_download_thread_manager;
	ThreadManager net_resource_download_thread_manager;
	ThreadManager save_resources_db_thread_manager;
	ThreadManager garbage_deleter_thread_manager;

	glare::AtomicInt num_non_net_resources_downloading;
	glare::AtomicInt num_net_resources_downloading;
	glare::AtomicInt num_resources_uploading;

	ThreadSafeQueue<Reference<ResourceToUpload>> upload_queue;

	EmscriptenResourceDownloader emscripten_resource_downloader;

	Reference<WorldState> world_state;
private:
	Reference<TextureServer> texture_server;
public:
	ThreadSafeQueue<Reference<ThreadMessage> > msg_queue; // for messages from ClientThread etc.. to this object.

	WorldObjectRef selected_ob;
	Vec4f selection_vec_cs; // Vector from camera to selected point on object, in camera space
	Vec4f selection_point_os; // Point on selected object where selection ray hit, in object space.
	bool selected_ob_picked_up; // Is selected object 'picked up' e.g. being moved?

	ParcelRef selected_parcel;

	Reference<ResourceManager> resource_manager;


	// NOTE: these object sets need to be cleared in connectToServer(), also when removing a dead object in ob->state == WorldObject::State_Dead case in timerEvent, the object needs to be removed
	// from any of these sets it is in.

	// NOTE: Use std::set instead of unordered_set, so that iteration over objects is in memory order.
	std::set<WorldObjectRef> active_objects; // Objects that have moved recently and so need interpolation done on them.
	std::set<WorldObjectRef> obs_with_animated_tex; // Objects with animated textures (e.g. gifs or mp4s)
	std::set<WorldObjectRef> web_view_obs;
	std::set<WorldObjectRef> browser_vid_player_obs;
	std::set<WorldObjectRef> audio_obs; // Objects with an audio_source or a non-empty audio_source_url, or objects that may play audio such as web-views or videos.
	std::set<WorldObjectRef> obs_with_scripts; // Objects with non-null script_evaluator
	std::set<WorldObjectRef> obs_with_diagnostic_vis;


	WorldDetails connected_world_details;
	WorldSettings connected_world_settings; // Settings for the world we are connected to, if any.
	

	Reference<Indigo::Mesh> ground_quad_mesh;
	Reference<OpenGLMeshRenderData> ground_quad_mesh_opengl_data;
	PhysicsShape ground_quad_shape;


	Reference<OpenGLMeshRenderData> hypercard_quad_opengl_mesh; // Also used for name tags.
	PhysicsShape hypercard_quad_shape;

	PhysicsShape text_quad_shape;

	Reference<OpenGLMeshRenderData> image_cube_opengl_mesh; // For images, web-views etc.
	PhysicsShape image_cube_shape;

	Reference<OpenGLMeshRenderData> spotlight_opengl_mesh;
	PhysicsShape spotlight_shape;

	PhysicsShape unit_cube_shape;

	Reference<MeshData> single_voxel_meshdata;
	Reference<PhysicsShapeData> single_voxel_shapedata;

	Reference<GLObject> ob_denied_move_marker; // Prototype object
	std::vector<Reference<GLObject> > ob_denied_move_markers;

	GLObjectRef aabb_os_vis_gl_ob; // Used for visualising the object-space AABB of the selected object.
	GLObjectRef aabb_ws_vis_gl_ob; // Used for visualising the world-space AABB of the selected object.
	std::vector<GLObjectRef> selected_ob_vis_gl_obs; // Used for visualising paths for path-controlled objects.

	static const int NUM_AXIS_ARROWS = 3;
	LineSegment4f axis_arrow_segments[NUM_AXIS_ARROWS];
	GLObjectRef axis_arrow_objects[NUM_AXIS_ARROWS]; // For ob placement

	std::vector<LineSegment4f> rot_handle_lines[3];
	GLObjectRef rot_handle_arc_objects[3];

	bool axis_and_rot_obs_enabled; // Are the axis arrow objects and rotation arcs inserted into the opengl engine? (and grabbable)

	int grabbed_axis; // -1 if no axis grabbed, [0, 3) if grabbed a translation arrow, [3, 6) if grabbed a rotation arc.
	Vec4f grabbed_point_ws; // Approximate point on arrow line we grabbed, in world space.
	Vec4f ob_origin_at_grab;

	float grabbed_angle;
	float original_grabbed_angle;
	float grabbed_arc_angle_offset;

	OpenGLTextureRef default_array_tex;

	Reference<OpenGLProgram> parcel_shader_prog;

	StandardPrintOutput print_output;
	//glare::TaskManager* task_manager; // General purpose task manager, for quick/blocking multithreaded builds of stuff. Currently just used for LODGeneration::generateLODTexturesForMaterialsIfNotPresent(). Lazily created.
	
	glare::TaskManager model_and_texture_loader_task_manager;

	glare::TaskManager* high_priority_task_manager;

	MeshManager mesh_manager;

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

	CircularBuffer<Reference<ModelLoadedThreadMessage> > async_model_loaded_messages_to_process;
	CircularBuffer<Reference<TextureLoadedThreadMessage> > async_texture_loaded_messages_to_process;

	Reference<VBO> dummy_vert_vbo;
	Reference<VBO> dummy_index_vbo;
	Reference<PBO> dummy_pbo;
	Reference<PBO> temp_pbo;
	Reference<OpenGLTexture> dummy_opengl_tex;
	
	bool process_model_loaded_next;

	// Textures being loaded or already loaded.
	// We have this set so that we don't process the same texture from multiple LoadTextureTasks running in parallel.
	std::unordered_set<OpenGLTextureKey> textures_processing;

	// We build a different physics mesh for dynamic objects, so we need to keep track of which mesh we are building.
	struct ModelProcessingKey
	{
		ModelProcessingKey(const URLString& URL_, const bool dynamic_physics_shape_) : URL(URL_), dynamic_physics_shape(dynamic_physics_shape_) {}

		URLString URL;
		bool dynamic_physics_shape;

		bool operator < (const ModelProcessingKey& other) const
		{
			if(URL < other.URL)
				return true;
			else if(URL > other.URL)
				return false;
			else
				return !dynamic_physics_shape && other.dynamic_physics_shape;
		}
		bool operator == (const ModelProcessingKey& other) const { return URL == other.URL && dynamic_physics_shape == other.dynamic_physics_shape; }
	};
	struct ModelProcessingKeyHasher
	{
		size_t operator() (const ModelProcessingKey& key) const
		{
			std::hash<string_view> h;
			return h(key.URL);
		}
	};
	// Models being loaded or already loaded.
	// We have this set so that we don't process the same model from multiple LoadModelTasks running in parallel.
	std::unordered_set<ModelProcessingKey, ModelProcessingKeyHasher> models_processing;

	// Audio files being loaded or already loaded.
	// We have this set so that we don't process the same audio from multiple LoadAudioTasks running in parallel.
	std::unordered_set<URLString> audio_processing;

	std::unordered_set<std::string> script_content_processing;

	//std::unordered_set<UID, UIDHasher> scatter_info_processing;

	std::set<WorldObjectRef> objs_with_lightmap_rebuild_needed;

	


	struct tls_config* client_tls_config;

	PCG32 rng;

	glare::AudioEngine audio_engine;
	UndoBuffer undo_buffer;

	glare::AudioSourceRef wind_audio_source;

	Timer last_footstep_timer;
	int last_foostep_side;

	double last_animated_tex_time;
	double last_model_and_tex_loading_time;
	double last_eval_script_time;
	int last_num_gif_textures_processed;
	int last_num_mp4_textures_processed;
	int last_num_scripts_processed;

	Timer time_since_object_edited; // For undo edit merging.
	bool force_new_undo_edit; // // Multiple edits using the object editor, in a short timespan, will be merged together, unless force_new_undo_edit is true (is set when undo or redo is issued).
	std::map<UID, UID> recreated_ob_uid; // Map from old object UID to recreated object UID when an object deletion is undone.

	UID last_restored_ob_uid_in_edit;

	GLUIRef gl_ui;
	GestureUI gesture_ui; // Draws gesture buttons, also selfie and enable mic button
	ObInfoUI ob_info_ui; // For object info and hyperlinks etc.
	MiscInfoUI misc_info_ui; // For showing messages from the server, vehicle speed etc.
	HeadUpDisplayUI hud_ui; // Draws stuff like markers for other avatars
	ChatUI chat_ui; // Draws chat user-interface, showing chat from other users plus the line edit for chatting.
	PhotoModeUI photo_mode_ui;
	Reference<MiniMap> minimap;

	bool running_destructor;

	BiomeManager* biome_manager;

	DownloadingResourceQueue download_queue;
	Timer download_queue_sort_timer;
	Timer load_item_queue_sort_timer;

	LoadItemQueue load_item_queue;

	SocketBufferOutStream scratch_packet;

	js::Vector<Vec4f, 16> temp_av_positions;

	std::unordered_map<URLString, DownloadingResourceInfo> URL_to_downloading_info; // Map from URL to info about the resource, for currently downloading resources.

	std::map<ModelProcessingKey, std::set<UID>> loading_model_URL_to_world_ob_UID_map;
	std::unordered_map<URLString, std::set<UID>> loading_model_URL_to_avatar_UID_map;

	std::unordered_map<URLString, std::set<UID>> loading_texture_URL_to_world_ob_UID_map;
	std::unordered_map<URLString, std::set<UID>> loading_texture_URL_to_avatar_UID_map;

	std::unordered_map<OpenGLTextureKey, std::set<UID>> loading_texture_key_to_hypercard_UID_map;

	std::unordered_map<URLString, Vec3i> loading_mesh_URL_to_chunk_coords_map;
	std::unordered_map<URLString, Vec3i> loading_texture_URL_to_chunk_coords_map;

	std::vector<Reference<GLObject> > player_phys_debug_spheres;

	std::vector<Reference<GLObject> > wheel_gl_objects;
	Reference<GLObject> car_body_gl_object;

	Reference<GLObject> mouseover_selected_gl_ob;

	MeshDataLoadingProgress mesh_data_loading_progress;
	Reference<OpenGLMeshRenderData> cur_loading_mesh_data;
	URLString cur_loading_lod_model_url;
	int cur_loading_model_lod_level;
	bool cur_loading_dynamic_physics_shape;
	uint64 cur_loading_voxel_hash;
	int cur_loading_voxel_subsample_factor;
	PhysicsShape cur_loading_physics_shape;

	// Current loading texture information
	Map2DRef cur_loading_terrain_map; // Non-null iff we are currently loading a map used for the terrain system into OpenGL.
	OpenGLTextureLoadingProgress tex_loading_progress;

	Reference<glare::FastPoolAllocator> world_ob_pool_allocator;

	std::vector<Reference<ObjectPathController>> path_controllers;

	UID client_avatar_uid; // When we connect to a server, the server assigns a UID to the client/avatar.
	uint32 server_protocol_version;
	uint32 server_capabilities;

	uint64 frame_num;
	size_t next_lod_changes_begin_i;

	MicReadStatus mic_read_status;

	IPAddress server_ip_addr;

	Reference<TerrainSystem> terrain_system;
	Reference<TerrainDecalManager> terrain_decal_manager;

	Reference<ParticleManager> particle_manager;

	Reference<AsyncTextureLoader> async_texture_loader;

	glare::StackAllocator stack_allocator;
	Reference<glare::Allocator> worker_allocator;

	glare::ArenaAllocator arena_allocator;


	Mutex particles_creation_buf_mutex;
	js::Vector<Particle, 16> particles_creation_buf GUARDED_BY(particles_creation_buf_mutex);


	ProximityLoader proximity_loader;
	float load_distance, load_distance2;

	enum ServerConnectionState
	{
		ServerConnectionState_NotConnected,
		ServerConnectionState_Connecting,
		ServerConnectionState_Connected
	};
	ServerConnectionState connection_state;

	UserID logged_in_user_id;
	std::string logged_in_user_name;
	uint32 logged_in_user_flags;

	bool server_using_lod_chunks; // Should be equal to !world_state->lod_chunks.empty(), cached in a boolean.

	bool server_has_basis_textures;
	bool server_has_basisu_terrain_detail_maps;
	bool server_has_optimised_meshes;
	int server_opt_mesh_version;

	bool shown_object_modification_error_msg;

	Reference<GLObject> ob_placement_beam;
	Reference<GLObject> ob_placement_marker;

	Reference<GLObject> voxel_edit_marker;
	bool voxel_edit_marker_in_engine;
	Reference<GLObject> voxel_edit_face_marker;
	bool voxel_edit_face_marker_in_engine;

	UIInterface* ui_interface;

	Timer total_timer;
	Timer discovery_udp_packet_timer;


	bool SHIFT_down, CTRL_down, A_down, W_down, S_down, D_down, space_down, C_down, left_down, right_down, up_down, down_down, B_down;

	js::Vector<Reference<ThreadMessage>, 16> temp_msgs;

	bool extracted_anim_data_loaded;

	URLParseResults last_url_parse_results;

	struct Notification
	{
		double creation_time;
		GLUITextViewRef text_view;
	};
	std::list<Notification> notifications;

	std::list<Notification> script_messages;

	Reference<SubstrataLuaVM> lua_vm;

	TimerQueue timer_queue;
	std::vector<TimerQueueTimer> temp_triggered_timers;

	struct ContactAddedEvent
	{
		WorldObjectRef ob;
	};

	ScriptedObjectProximityChecker scripted_ob_proximity_checker;

	ParcelID cur_in_parcel_id;

	bool last_cursor_movement_was_from_mouse; // as opposed to from gamepad moving crosshair.

	bool sent_perform_gesture_without_stop_gesture;

	// Info stored about an upload to the GPU using a PBO while it is taking place
	struct PBOAsyncTextureUploading : public UploadingTextureUserInfo
	{
		OpenGLTextureKey path;
		URLString URL;
		Reference<TextureData> tex_data;
		Reference<OpenGLTexture> opengl_tex;
		Map2DRef terrain_map;
		bool loading_into_existing_opengl_tex;
	};


	struct AsyncGeometryUploading : public UploadingGeometryUserInfo
	{
		URLString lod_model_url;
		int ob_model_lod_level;
		bool dynamic_physics_shape;
		PhysicsShape physics_shape;
		int voxel_subsample_factor;
		uint64 voxel_hash;
	};
	

	js::Vector<AsyncUploadedGeometryInfo, 16> temp_uploaded_geom_infos;
	js::Vector<PBOAsyncUploadedTextureInfo, 16> temp_loaded_texture_infos;

	Reference<glare::FastPoolAllocator> texture_loaded_msg_allocator; // For TextureLoadedThreadMessage

	bool use_lightmaps;
	Timer retry_connection_timer;


	ThreadManager opengl_worker_thread_manager;

	Reference<OpenGLUploadThread> opengl_upload_thread;

	Reference<PBOPool> pbo_pool;
	PBOAsyncTextureUploader pbo_async_tex_loader;
	Reference<VBOPool> vbo_pool;
	Reference<VBOPool> index_vbo_pool;
	AsyncGeometryUploader async_geom_loader;
	AsyncGeometryUploader async_index_geom_loader;

	Reference<AnimatedTextureManager> animated_texture_manager;
};
