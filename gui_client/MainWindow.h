#pragma once


#include "PhysicsWorld.h"
#include "../utils/CameraController.h"
#include "PlayerPhysics.h"
#include "ClientThread.h"
#include "../shared/WorldState.h"
#include "../utils/ArgumentParser.h"
#include "../utils/Timer.h"
#include "ui_MainWindow.h"
#include <QtCore/QEvent>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QSettings>
#include <string>
class ArgumentParser;


class MainWindow : public QMainWindow, public Ui::MainWindow
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

private:

	std::string base_dir_path;
	std::string appdata_path;
	ArgumentParser parsed_args;

	QTimer *timer;

	Timer total_timer;

public:
	CameraController cam_controller;

	Reference<PhysicsWorld> physics_world;

	PlayerPhysics player_physics;

	Timer time_since_last_timer_ev;
	Timer time_since_update_packet_sent;

	Reference<ClientThread> client_thread;

	Reference<WorldState> world_state;

	std::map<const Avatar*, GLObjectRef> avatar_gl_objects;
	//std::map<GLObjectRef*, bool> avatars_
};
