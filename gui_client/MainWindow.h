/*=====================================================================
MainWindow.h
------------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#pragma once


#include "PhysicsWorld.h"
#include "ModelLoading.h"
#include "../opengl/OpenGLEngine.h"
#include "../shared/ResourceManager.h"
#include "../shared/WorldObject.h"
#include "../utils/CameraController.h"
#include "PlayerPhysics.h"
#include "ClientThread.h"
#include "../shared/WorldState.h"
#include "../indigo/ThreadContext.h"
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
class ArgumentParser;
namespace Ui { class MainWindow; }
class TextureServer;
class QSettings;
class UserDetailsWidget;
class URLWidget;
class QLabel;


class MainWindow : public QMainWindow
{
	Q_OBJECT
public:
	MainWindow(const std::string& base_dir_path, const std::string& appdata_path, const ArgumentParser& args,
		QWidget *parent = 0);
	~MainWindow();

	void initialise();

	void updateGroundPlane();

	// Semicolon is for intellisense, see http://www.qtsoftware.com/developer/faqs/faq.2007-08-23.5900165993
signals:;
	void resolutionChanged(int, int);

private slots:;
	void on_actionAvatarSettings_triggered();
	void on_actionAddObject_triggered();
	void on_actionAddHypercard_triggered();
	void on_actionCloneObject_triggered();
	void on_actionDeleteObject_triggered();
	void on_actionReset_Layout_triggered();
	void on_actionLogIn_triggered();
	void on_actionSignUp_triggered();
	void on_actionLogOut_triggered();
	void on_actionShow_Parcels_triggered();
	
	void passwordResetRequested();

	void sendChatMessageSlot();

	void glWidgetMouseClicked(QMouseEvent* e);
	void glWidgetMouseDoubleClicked(QMouseEvent* e);
	void glWidgetMouseMoved(QMouseEvent* e);
	void glWidgetKeyPressed(QKeyEvent* e);
	void glWidgetMouseWheelEvent(QWheelEvent* e);

	void objectEditedSlot();
	void URLChangedSlot();
private:
	virtual void timerEvent(QTimerEvent* event);
	void rotateObject(WorldObjectRef ob, const Vec4f& axis, float angle);
	void deleteSelectedObject();
	void deselectObject();
	void deselectParcel();
	GLObjectRef makeNameTagGLObject(const std::string& nametag);
	Reference<OpenGLTexture> makeHypercardTexMap(const std::string& content);
	void loadModelForObject(WorldObject* ob, bool start_downloading_missing_files);
	void loadScriptForObject(WorldObject* ob);
	void print(const std::string& message); // Print to log and console
	void showErrorNotification(const std::string& message);
	void showInfoNotification(const std::string& message);
	void startDownloadingResource(const std::string& url);
	void evalObjectScript(WorldObject* ob, double cur_time);
	void updateStatusBar();

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

	Reference<WorldState> world_state;

	//std::map<const Avatar*, GLObjectRef> avatar_gl_objects;
	//std::map<GLObjectRef*, bool> avatars_

	TextureServer* texture_server;

	QSettings* settings;

	ThreadSafeQueue<Reference<ThreadMessage> > msg_queue; // for messages from ClientThread etc..

	WorldObjectRef selected_ob;
	Vec4f selection_vec_cs; // Vector from camera to selected point on object, in camera space
	Vec4f selection_point_ws; // Point on selected object where selection ray hit, in world space.
	Vec4f selected_ob_pos_upon_selection;

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

	Reference<OpenGLMeshRenderData> hypercard_quad_opengl_mesh;
	Reference<RayMesh> hypercard_quad_raymesh;

	Reference<RayMesh> unit_cube_raymesh;

	StandardPrintOutput print_output;
	Indigo::TaskManager task_manager;

	MeshManager mesh_manager;

	struct Notification
	{
		double creation_time;
		QLabel* label;
	};

	std::list<Notification> notifications;

	bool need_help_info_dock_widget_position; // We may need to position the Help info dock widget to the bottom right of the GL view.
	// But we need to wait until the gl view has been reszied before we do this, so set this flag to do in a timer event.

	std::string server_hostname;

	size_t total_num_res_to_download;

	Timer fps_display_timer;
	int num_frames;
};
