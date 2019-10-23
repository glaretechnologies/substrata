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
#include "../opengl/OpenGLEngine.h"
#include "../shared/ResourceManager.h"
#include "../shared/WorldObject.h"
#include "../indigo/ThreadContext.h"
#include "../utils/CameraController.h"
#include "../utils/ArgumentParser.h"
#include "../utils/Timer.h"
#include "../utils/TaskManager.h"
#include "../utils/StandardPrintOutput.h"
#include <QtCore/QEvent>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtWidgets/QMainWindow>
#include <string>
#include <fstream>
#include <unordered_set>
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


class MainWindow : public QMainWindow
{
	Q_OBJECT
public:
	MainWindow(const std::string& base_dir_path, const std::string& appdata_path, const ArgumentParser& args,
		QWidget *parent = 0);
	~MainWindow();

	void initialise();

	void connectToServer(const std::string& hostname, const std::string& worldname); // Disconnect from any current server, connect to new server

	void afterGLInitInitialise();

	void updateGroundPlane();

	void print(const std::string& message); // Print to log and console

	// Semicolon is for intellisense, see http://www.qtsoftware.com/developer/faqs/faq.2007-08-23.5900165993
signals:;
	void resolutionChanged(int, int);

private slots:;
	void on_actionAvatarSettings_triggered();
	void on_actionAddObject_triggered();
	void on_actionAddHypercard_triggered();
	void on_actionAdd_Voxels_triggered();
	void on_actionCloneObject_triggered();
	void on_actionDeleteObject_triggered();
	void on_actionReset_Layout_triggered();
	void on_actionLogIn_triggered();
	void on_actionSignUp_triggered();
	void on_actionLogOut_triggered();
	void on_actionShow_Parcels_triggered();
	void on_actionFly_Mode_triggered();
	
	void passwordResetRequested();

	void sendChatMessageSlot();

	void glWidgetMouseClicked(QMouseEvent* e);
	void glWidgetMouseDoubleClicked(QMouseEvent* e);
	void glWidgetMouseMoved(QMouseEvent* e);
	void glWidgetKeyPressed(QKeyEvent* e);
	void glWidgetkeyReleased(QKeyEvent* e);
	void glWidgetMouseWheelEvent(QWheelEvent* e);
	void cameraUpdated();
	void onIndigoViewDockWidgetVisibilityChanged(bool v);

	void objectEditedSlot();
	void URLChangedSlot();
	void materialSelectedInBrowser(const std::string& path);
private:
	void closeEvent(QCloseEvent* event);
	virtual void timerEvent(QTimerEvent* event);
	void rotateObject(WorldObjectRef ob, const Vec4f& axis, float angle);
	void deleteSelectedObject();
	void deselectObject();
	void deselectParcel();
	GLObjectRef makeNameTagGLObject(const std::string& nametag);
	Reference<OpenGLTexture> makeHypercardTexMap(const std::string& content, ImageMapUInt8Ref& uint8_map_out);
	void loadModelForObject(WorldObject* ob);
	void loadScriptForObject(WorldObject* ob);
	void showErrorNotification(const std::string& message);
	void showInfoNotification(const std::string& message);
	void startDownloadingResourcesForObject(WorldObject* ob);
	void startDownloadingResource(const std::string& url); // For every resource that the object uses (model, textures etc..), if the resource is not present locally, start downloading it.
	void evalObjectScript(WorldObject* ob, double cur_time);
	void updateStatusBar();
	bool haveParcelObjectCreatePermissions(const Vec3d& new_ob_pos, bool& in_parcel_out);
	bool haveObjectWritePermissions(const js::AABBox& new_aabb_ws, bool& ob_pos_in_parcel_out);
	void addParcelObjects();
	void removeParcelObjects();
	void recolourParcelsForLoggedInState();
	void updateSelectedObjectPlacementBeam();
	void updateInstancedCopiesOfObject(WorldObject* ob);
	void removeInstancesOfObject(const WorldObject* ob);
	
	bool objectModificationAllowed(const WorldObject& ob);
	bool objectModificationAllowedWithMsg(const WorldObject& ob, const std::string& action); // Also shows error notifications if modification is not allowed.
	// Action will be printed in error message, could be "modify" or "delete"

	void updateOnlineUsersList(); // Works off world state avatars.
	bool areEditingVoxels();
	Vec4f getDirForPixelTrace(int pixel_pos_x, int pixel_pos_y);
	void updateVoxelEditMarkers();
	void pickUpSelectedObject();
	void dropSelectedObject();
	void setUIForSelectedObject(); // Enable/disable delete object action etc..

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

	void startLoadingTexturesForObject(const WorldObject& ob);
	void removeAndDeleteGLAndPhysicsObjectsForOb(WorldObject& ob);
	void addPlaceholderObjectsForOb(WorldObject& ob);

	//BuildUInt8MapTextureDataScratchState build_uint8_map_scratch_state;
private:
	std::string base_dir_path;
	std::string appdata_path;
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

	IndigoAtomic num_non_net_resources_downloading;
	IndigoAtomic num_net_resources_downloading;

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

	std::set<WorldObjectRef> active_objects; // Objects that have moved recently and so need interpolation done on them.

	ThreadContext thread_context;

	std::ofstream logfile;

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

	Reference<OpenGLMeshRenderData> hypercard_quad_opengl_mesh; // Also used for name tags.
	Reference<RayMesh> hypercard_quad_raymesh;

	Reference<RayMesh> unit_cube_raymesh;

	Reference<GLObject> ob_placement_beam;
	Reference<GLObject> ob_placement_marker;

	Reference<GLObject> voxel_edit_marker;
	bool voxel_edit_marker_in_engine;
	Reference<GLObject> voxel_edit_face_marker;
	bool voxel_edit_face_marker_in_engine;

	Reference<GLObject> ob_denied_move_marker; // Prototype object
	std::vector<Reference<GLObject> > ob_denied_move_markers;

	Reference<OpenGLProgram> parcel_shader_prog;

	StandardPrintOutput print_output;
	Indigo::TaskManager task_manager;
	Indigo::TaskManager model_building_task_manager; // For use in ModelLoading::makeGLObjectForModelURLAndMaterials in LoadModelTask etc..
public:
	Indigo::TaskManager texture_loader_task_manager;
private:
	MeshManager mesh_manager;

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

	Timer fps_display_timer;
	int num_frames;

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
};
