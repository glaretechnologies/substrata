#pragma once


#include "PhysicsWorld.h"
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
#include <QtCore/QEvent>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtWidgets/QMainWindow>
#include <string>
class ArgumentParser;
namespace Ui { class MainWindow; }
class TextureServer;
class QSettings;


class MainWindow : public QMainWindow
{
	Q_OBJECT
public:
	MainWindow(const std::string& base_dir_path, const std::string& appdata_path, const ArgumentParser& args,
		QWidget *parent = 0);
	~MainWindow();

	void initialise();
	
	// Semicolon is for intellisense, see http://www.qtsoftware.com/developer/faqs/faq.2007-08-23.5900165993
signals:;
	void resolutionChanged(int, int);

public slots:;
	void timerEvent();
private slots:;
	void on_actionAvatarSettings_triggered();
	void on_actionAddObject_triggered();
	void on_actionCloneObject_triggered();
	void on_actionDeleteObject_triggered();
	void on_actionReset_Layout_triggered();

	void sendChatMessageSlot();

	void glWidgetMouseClicked(QMouseEvent* e);
	void glWidgetMouseDoubleClicked(QMouseEvent* e);
	void glWidgetMouseMoved(QMouseEvent* e);
	void glWidgetKeyPressed(QKeyEvent* e);
	void glWidgetMouseWheelEvent(QWheelEvent* e);

	void objectEditedSlot();
private:
	void rotateObject(WorldObjectRef ob, const Vec4f& axis, float angle);
	void deleteSelectedObject();
	GLObjectRef makeNameTagGLObject(const std::string& nametag);
	void loadModelForObject(WorldObject* ob);

	std::string base_dir_path;
	std::string appdata_path;
	ArgumentParser parsed_args;

	QTimer *timer;

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

	std::string resources_dir;
	Reference<ResourceManager> resource_manager;

	std::set<WorldObjectRef> active_objects; // Objects that have moved recently and so need interpolation done on them.

	ThreadContext thread_context;
};
